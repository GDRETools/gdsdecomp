#include "utility/common.h"
#include "bytecode/bytecode_base.h"
#include "external/tga/tga.h"
#include "utility/glob.h"

#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/io/http_client_tcp.h"
#include "core/io/image.h"
#include "core/io/missing_resource.h"
#include "modules/zip/zip_reader.h"
#include "vtracer/vtracer.h"

Vector<String> gdre::get_recursive_dir_list(const String &p_dir, const Vector<String> &wildcards, const bool absolute, const String &rel) {
	Vector<String> ret;
	Error err;
	Ref<DirAccess> da = DirAccess::open(p_dir.path_join(rel), &err);
	ERR_FAIL_COND_V_MSG(da.is_null(), ret, "Failed to open directory " + p_dir);

	if (da.is_null()) {
		return ret;
	}
	Vector<String> dirs;
	Vector<String> files;

	String base = absolute ? p_dir : "";
	da->list_dir_begin();
	String f = da->get_next();
	while (!f.is_empty()) {
		if (f == "." || f == "..") {
			f = da->get_next();
			continue;
		} else if (da->current_is_dir()) {
			dirs.push_back(f);
		} else {
			files.push_back(f);
		}
		f = da->get_next();
	}
	da->list_dir_end();

	dirs.sort_custom<FileNoCaseComparator>();
	files.sort_custom<FileNoCaseComparator>();
	for (auto &d : dirs) {
		ret.append_array(get_recursive_dir_list(p_dir, wildcards, absolute, rel.path_join(d)));
	}
	for (auto &file : files) {
		if (wildcards.size() > 0) {
			for (int i = 0; i < wildcards.size(); i++) {
				if (file.get_file().matchn(wildcards[i])) {
					ret.append(base.path_join(rel).path_join(file));
					break;
				}
			}
		} else {
			ret.append(base.path_join(rel).path_join(file));
		}
	}

	return ret;
}

bool gdre::dir_has_any_matching_wildcards(const String &p_dir, const Vector<String> &wildcards) {
	Vector<String> ret;
	Error err;
	Ref<DirAccess> da = DirAccess::open(p_dir, &err);
	ERR_FAIL_COND_V_MSG(da.is_null(), false, "Failed to open directory " + p_dir);
	Vector<String> dirs;

	da->list_dir_begin();
	String f = da->get_next();
	while (!f.is_empty()) {
		if (f == "." || f == "..") {
			f = da->get_next();
			continue;
		} else if (da->current_is_dir()) {
			if (dir_has_any_matching_wildcards(p_dir.path_join(f), wildcards)) {
				return true;
			}
		} else {
			for (auto &wc : wildcards) {
				if (f.get_file().matchn(wc)) {
					return true;
				}
			}
		}
		f = da->get_next();
	}
	da->list_dir_end();
	return false;
}

Error gdre::ensure_dir(const String &dst_dir) {
	Error err = OK;
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	ERR_FAIL_COND_V(da.is_null(), ERR_FILE_CANT_OPEN);
	// make_dir_recursive requires a mutex lock for every directory in the path, so it behooves us to check if the directory exists first
	if (!da->dir_exists(dst_dir)) {
		err = da->make_dir_recursive(dst_dir);
	}
	return err;
}

bool gdre::check_header(const Vector<uint8_t> &p_buffer, const char *p_expected_header, int p_expected_len) {
	if (p_buffer.size() < p_expected_len) {
		return false;
	}

	for (int i = 0; i < p_expected_len; i++) {
		if (p_buffer[i] != p_expected_header[i]) {
			return false;
		}
	}

	return true;
}

Error gdre::decompress_image(const Ref<Image> &img) {
	Error err;
	if (img->is_compressed()) {
		err = img->decompress();
		if (err == ERR_UNAVAILABLE) {
			return err;
		}
		ERR_FAIL_COND_V_MSG(err != OK || img.is_null(), err, "Failed to decompress image.");
	}
	return OK;
}

class GodotFileInterface : public tga::FileInterface {
	Ref<FileAccess> m_file;

public:
	GodotFileInterface(const String &p_path, FileAccess::ModeFlags p_mode) {
		m_file = FileAccess::open(p_path, p_mode);
	}

	// Returns true if we can read/write bytes from/into the file
	virtual bool ok() const override {
		return m_file.is_valid();
	}

	// Current position in the file
	virtual size_t tell() override {
		return m_file->get_position();
	}

	// Jump to the given position in the file
	virtual void seek(size_t absPos) override {
		m_file->seek(absPos);
	}

	// Returns the next byte in the file or 0 if ok() = false
	virtual uint8_t read8() override {
		return m_file->get_8();
	}

	// Writes one byte in the file (or do nothing if ok() = false)
	virtual void write8(uint8_t value) override {
		m_file->store_8(value);
	}
};

Error gdre::save_image_as_tga(const String &p_path, const Ref<Image> &p_img) {
	Vector<uint8_t> buffer;
	Ref<Image> source_image = p_img->duplicate();
	GDRE_ERR_DECOMPRESS_OR_FAIL(source_image);
	int width = source_image->get_width();
	int height = source_image->get_height();
	bool isRGB = true;
	bool isAlpha = source_image->detect_alpha();
	switch (source_image->get_format()) {
		case Image::FORMAT_L8:
			isRGB = false;
			break;
		case Image::FORMAT_LA8:
			isRGB = true;
			source_image->convert(Image::FORMAT_RGBA8);
			break;
		case Image::FORMAT_RGB8:
			// we still need to convert it to RGBA8 even if it doesn't have alpha due to the encoder requiring 4 bytes per pixel
			source_image->convert(Image::FORMAT_RGBA8);
			break;
		case Image::FORMAT_RGBA8:
			break;
		default:
			source_image->convert(Image::FORMAT_RGBA8);
			isRGB = true;
			break;
	}

	tga::Header header;
	header.idLength = 0;
	header.colormapType = 0;
	header.imageType = isRGB ? tga::ImageType::RleRgb : tga::ImageType::RleGray;
	header.colormapOrigin = 0;
	header.colormapLength = 0;
	header.colormapDepth = 0;

	header.xOrigin = 0;
	header.yOrigin = 0;
	header.width = width;
	header.height = height;
	header.bitsPerPixel = isRGB ? (isAlpha ? 32 : 24) : 8;
	header.imageDescriptor = isAlpha ? 0xf : 0; // top-left origin always
	tga::Image tga_image{};
	Vector<uint8_t> tga_data = source_image->get_data(); //isRGB ? rgba_to_bgra(source_image->get_data()) : source_image->get_data();
	tga_image.pixels = tga_data.ptrw();
	tga_image.bytesPerPixel = isRGB ? 4 : 1;
	tga_image.rowstride = width * tga_image.bytesPerPixel;
	GodotFileInterface file_interface(p_path, FileAccess::WRITE);
	if (!file_interface.ok()) {
		return ERR_FILE_CANT_WRITE;
	}
	tga::Encoder encoder(&file_interface);
	encoder.writeHeader(header);
	encoder.writeImage(header, tga_image);
	encoder.writeFooter();
	return OK;
}

Error gdre::save_image_as_svg(const String &p_path, const Ref<Image> &p_img) {
	VTracerConfig config;
	vtracer_set_default_config(&config);
	config.color_mode = V_TRACER_COLOR_MODE_COLOR;
	config.hierarchical = V_TRACER_HIERARCHICAL_STACKED;
	config.mode = V_TRACER_PATH_SIMPLIFY_MODE_NONE;
	config.filter_speckle = 0;
	config.color_precision = 8;
	config.layer_difference = 0;
	config.corner_threshold = 60;
	config.length_threshold = 4.0;
	config.splice_threshold = 45;
	config.max_iterations = 10;
	config.path_precision = 2;
	config.keying_threshold = 0.0;

	Ref<Image> img = p_img->duplicate();
	GDRE_ERR_DECOMPRESS_OR_FAIL(img);
	// check if the image is RGBA; if not, convert it to RGBA
	if (img->get_format() != Image::FORMAT_RGBA8) {
		img->convert(Image::FORMAT_RGBA8);
	}
	auto data = img->get_data();
	VTracerColorImage svg_data{
		data.ptrw(),
		(size_t)img->get_width(),
		(size_t)img->get_height(),
	};
	const char *err_str = vtracer_convert_image_memory_to_svg(&svg_data, p_path.utf8().get_data(), &config);
	String err_msg;
	if (err_str) {
		err_msg = vformat("Failed to convert image to SVG: %s", err_str);
		vtracer_free_string(err_str);
	}
	ERR_FAIL_COND_V_MSG(!err_msg.is_empty(), ERR_CANT_CREATE, err_msg);
	return OK;
}

Error gdre::save_image_as_webp(const String &p_path, const Ref<Image> &p_img, bool lossy) {
	Ref<Image> source_image = p_img->duplicate();
	Error err = OK;
	GDRE_ERR_DECOMPRESS_OR_FAIL(source_image);
	Vector<uint8_t> buffer;
	if (lossy) {
		buffer = Image::webp_lossy_packer(source_image, 1);
	} else {
		buffer = Image::webp_lossless_packer(source_image);
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err, err, vformat("Can't save WEBP at path: '%s'.", p_path));

	file->store_buffer(buffer.ptr(), buffer.size());
	if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF) {
		return ERR_CANT_CREATE;
	}
	return OK;
}

Error gdre::save_image_as_jpeg(const String &p_path, const Ref<Image> &p_img) {
	Vector<uint8_t> buffer;
	Ref<Image> source_image = p_img->duplicate();
	GDRE_ERR_DECOMPRESS_OR_FAIL(source_image);
	return source_image->save_jpg(p_path, 1.0f);
}

void gdre::get_strings_from_variant(const Variant &p_var, Vector<String> &r_strings, const String &engine_version) {
	if (p_var.get_type() == Variant::STRING || p_var.get_type() == Variant::STRING_NAME) {
		r_strings.push_back(p_var);
	} else if (p_var.get_type() == Variant::STRING_NAME) {
		r_strings.push_back(p_var);
	} else if (p_var.get_type() == Variant::PACKED_STRING_ARRAY) {
		Vector<String> p_strings = p_var;
		for (auto &E : p_strings) {
			r_strings.push_back(E);
		}
	} else if (p_var.get_type() == Variant::ARRAY) {
		Array arr = p_var;
		for (int i = 0; i < arr.size(); i++) {
			get_strings_from_variant(arr[i], r_strings, engine_version);
		}
	} else if (p_var.get_type() == Variant::DICTIONARY) {
		Dictionary d = p_var;
		Array keys = d.keys();
		for (int i = 0; i < keys.size(); i++) {
			get_strings_from_variant(keys[i], r_strings, engine_version);
			get_strings_from_variant(d[keys[i]], r_strings, engine_version);
		}
	} else if (p_var.get_type() == Variant::OBJECT) {
		Object *obj = Object::cast_to<Object>(p_var);
		if (obj) {
			List<PropertyInfo> p_list;
			obj->get_property_list(&p_list);
			for (List<PropertyInfo>::Element *E = p_list.front(); E; E = E->next()) {
				auto &p = E->get();
				get_strings_from_variant(obj->get(p.name), r_strings, engine_version);
			}
			List<StringName> m_list;
			obj->get_meta_list(&m_list);
			for (auto &name : m_list) {
				get_strings_from_variant(obj->get_meta(name), r_strings, engine_version);
			}
			if (!engine_version.is_empty()) {
				Ref<MissingResource> mr = p_var;
				if (obj->get_save_class() == "GDScript") {
					String code = obj->get("script/source");
					if (!code.is_empty()) {
						auto decomp = GDScriptDecomp::create_decomp_for_version(engine_version, true);
						if (!decomp.is_null()) {
							auto buf = decomp->compile_code_string(code);
							if (!buf.is_empty()) {
								decomp->get_script_strings_from_buf(buf, r_strings, false);
							}
						}
					}
				}
			}
		}
	}
}

Error gdre::unzip_file_to_dir(const String &zip_path, const String &output_dir) {
	Ref<ZIPReader> zip;
	zip.instantiate();

	Error err = zip->open(zip_path);
	if (err != OK) {
		return err;
	}
	auto files = zip->get_files();
	for (int i = 0; i < files.size(); i++) {
		auto file = files[i];
		auto data = zip->read_file(file, true);
		if (data.size() == 0) {
			continue;
		}
		String out_path = output_dir.path_join(file);
		ensure_dir(out_path.get_base_dir());
		Ref<FileAccess> fa = FileAccess::open(out_path, FileAccess::WRITE);
		if (fa.is_null()) {
			continue;
		}
		fa->store_buffer(data.ptr(), data.size());
		fa->close();
	}
	return OK;
}

String gdre::get_md5(const String &dir, bool ignore_code_signature) {
	if (dir.is_empty()) {
		return "";
	}
	auto da = DirAccess::create_for_path(dir);
	if (da->dir_exists(dir)) {
		return get_md5_for_dir(dir, ignore_code_signature);
	} else if (da->file_exists(dir)) {
		return FileAccess::get_md5(dir);
	}
	return "";
}

String gdre::get_md5_for_dir(const String &dir, bool ignore_code_signature) {
	auto paths = Glob::rglob(dir.path_join("**/*"), true);
	Vector<String> files;
	for (auto path : paths) {
		if (FileAccess::exists(path) && (!ignore_code_signature || !path.contains("_CodeSignature"))) {
			files.push_back(path);
		}
	}
	// sort the files
	files.sort();
	return FileAccess::get_multiple_md5(files);
}

Error gdre::wget_sync(const String &p_url, Vector<uint8_t> &response, int retries, float *p_progress, bool *p_cancelled) {
#define WGET_CANCELLED_CHECK()         \
	if (p_cancelled && *p_cancelled) { \
		return ERR_SKIP;               \
	}
	WGET_CANCELLED_CHECK();
	Ref<HTTPClientTCP> client;
	client.instantiate();
	client->set_blocking_mode(true);
	Error err;
	String url = p_url;
	auto connect_to_host_and_request = [&](const String &url) {
		WGET_CANCELLED_CHECK();
		bool is_https = url.begins_with("https://");
		String host = url.get_slice("://", 1).get_slice("/", 0);
		String thingy = (is_https ? "https://" : "http://") + host;
		Error err = client->connect_to_host(thingy, is_https ? 443 : 80);
		ERR_FAIL_COND_V_MSG(err, err, "Failed to connect to host " + url);
		while (client->get_status() == HTTPClient::STATUS_RESOLVING || client->get_status() == HTTPClient::STATUS_CONNECTING) {
			WGET_CANCELLED_CHECK();
			err = client->poll();
			if (err) {
				return err;
			}
		}
		if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
			return ERR_CANT_CONNECT;
		}
		WGET_CANCELLED_CHECK();
		err = client->request(HTTPClient::METHOD_GET, url, Vector<String>(), nullptr, 0);
		ERR_FAIL_COND_V_MSG(err, err, "Failed to connect to host " + url);
		return OK;
	};
	bool done = false;
	bool got_response = false;
	List<String> response_headers;
	int redirections = 0;
	int response_code = 0;
	int response_body_length = 0;
	auto _handle_response = [&]() -> Error {
		WGET_CANCELLED_CHECK();
		if (!client->has_response()) {
			return ERR_BUG;
		}

		got_response = true;
		response_code = client->get_response_code();
		List<String> rheaders;
		client->get_response_headers(&rheaders);
		response_headers.clear();

		for (const String &E : rheaders) {
			response_headers.push_back(E);
		}
		if (response_code == 404) {
			return ERR_FILE_NOT_FOUND;
		}
		if (response_code == 403) {
			return ERR_UNAUTHORIZED;
		}
		if (response_code == 401) {
			return ERR_UNAUTHORIZED;
		}
		if (response_code >= 400) {
			return ERR_BUG;
		}

		if (response_code == 301 || response_code == 302) {
			// Handle redirect.

			if (redirections >= 200) {
				return ERR_CANT_OPEN;
			}

			String new_request;

			for (const String &E : rheaders) {
				if (E.containsn("Location: ")) {
					new_request = E.substr(9, E.length()).strip_edges();
				}
			}

			if (!new_request.is_empty()) {
				// Process redirect.
				client->close();
				redirections += 1;
				got_response = false;
				url = new_request;
				return connect_to_host_and_request(new_request);
			}
		}

		return OK;
	};
	err = connect_to_host_and_request(p_url);

	auto _retry = [&](Error err) {
		WGET_CANCELLED_CHECK();
		if (retries <= 0) {
			ERR_FAIL_V_MSG(ERR_CONNECTION_ERROR, vformat("Failed to download file from %s", p_url));
		}
		// Don't bother retrying if the file doesn't exist or we don't have access to it
		if (response_code == 404 || response_code == 403 || response_code == 401) {
			return err;
		}
		retries--;
		response.clear();
		return wget_sync(p_url, response, retries, p_progress, p_cancelled);
	};

	while (!done) {
		WGET_CANCELLED_CHECK();
		auto status = client->get_status();
		switch (status) {
			case HTTPClient::STATUS_REQUESTING: {
				client->poll();
				break;
			}
			case HTTPClient::STATUS_BODY: {
				if (!got_response) {
					err = _handle_response();
					if (err != OK) {
						return _retry(err);
					}
					response_body_length = client->get_response_body_length();
					if (!client->is_response_chunked() && response_body_length == 0) {
						break;
					}
				} else {
					err = client->poll();
					if (err != OK) {
						return _retry(err);
					}
					response.append_array(client->read_response_body_chunk());
					if (p_progress) {
						*p_progress = float(response.size()) / float(response_body_length);
					}
				}
				break;
			}
			case HTTPClient::STATUS_CONNECTED: {
				if (!got_response) {
					err = _handle_response();
					if (err != OK) {
						return _retry(err);
					}
				} else {
					done = true;
				}
				break;
			}
			default: {
				return _retry(OK);
			}
		}
	}
	ERR_FAIL_COND_V_MSG(response.is_empty(), ERR_CANT_CREATE, "Failed to download file from " + p_url);
	return OK;
#undef WGET_CANCELLED_CHECK
}

Error gdre::download_file_sync(const String &p_url, const String &output_path, float *p_progress, bool *p_cancelled) {
	Vector<uint8_t> response;
	Error err = wget_sync(p_url, response, 5, p_progress, p_cancelled);
	if (err) {
		return err;
	}
	err = ensure_dir(output_path.get_base_dir());
	if (err) {
		return err;
	}
	Ref<FileAccess> fa = FileAccess::open(output_path, FileAccess::WRITE, &err);
	if (fa.is_null()) {
		return ERR_FILE_CANT_WRITE;
	}
	fa->store_buffer(response.ptr(), response.size());
	fa->close();
	return OK;
}

Error gdre::rimraf(const String &dir) {
	auto da = DirAccess::create_for_path(dir);
	if (da.is_null()) {
		return ERR_FILE_CANT_OPEN;
	}
	Error err = OK;
	if (da->dir_exists(dir)) {
		err = da->change_dir(dir);
		if (err != OK) {
			return err;
		}
		err = da->erase_contents_recursive();
		if (err != OK) {
			return err;
		}
		err = da->remove(dir);
	} else if (da->file_exists(dir)) {
		err = da->remove(dir);
	}
	return err;
}

bool gdre::dir_is_empty(const String &dir) {
	auto da = DirAccess::create_for_path(dir);

	if (da.is_null() || !da->dir_exists(dir) || da->change_dir(dir) != OK || da->list_dir_begin() != OK) {
		return false;
	}
	String f = da->get_next();
	while (!f.is_empty()) {
		if (f != "." && f != "..") {
			return false;
		}
		f = da->get_next();
	}
	return true;
}

Error gdre::touch_file(const String &path) {
	Ref<FileAccess> fa = FileAccess::open(path, FileAccess::READ_WRITE);
	if (fa.is_null()) {
		return ERR_FILE_CANT_OPEN;
	}
	size_t size = fa->get_length();
	fa->resize(size);
	fa->close();
	return OK;
}

//void get_chars_in_set(const String &s, const HashSet<char32_t> &chars, HashSet<char32_t> &ret);

void gdre::get_chars_in_set(const String &s, const HashSet<char32_t> &chars, HashSet<char32_t> &ret) {
	for (int i = 0; i < s.length(); i++) {
		if (chars.has(s[i])) {
			ret.insert(s[i]);
		}
	}
}

bool gdre::has_chars_in_set(const String &s, const HashSet<char32_t> &chars) {
	for (int i = 0; i < s.length(); i++) {
		if (chars.has(s[i])) {
			return true;
		}
	}
	return false;
}

String gdre::remove_chars(const String &s, const HashSet<char32_t> &chars) {
	String ret;
	for (int i = 0; i < s.length(); i++) {
		if (!chars.has(s[i])) {
			ret += s[i];
		}
	}
	return ret;
}

String gdre::remove_chars(const String &s, const Vector<char32_t> &chars) {
	return remove_chars(s, vector_to_hashset(chars));
}

String gdre::remove_whitespace(const String &s) {
	String ret;
	for (int i = 0; i < s.length(); i++) {
		if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') {
			ret += s[i];
		}
	}
	return ret;
}

Vector<String> gdre::_split_multichar(const String &s, const Vector<String> &splitters, bool allow_empty, int maxsplit) {
	HashSet<char32_t> splitter_chars;
	for (int i = 0; i < splitters.size(); i++) {
		if (splitters[i].length() > 1) {
			ERR_FAIL_V_MSG(Vector<String>(), "split_multichar only supports single-character splitters.");
		}
		splitter_chars.insert(splitters[i][0]);
	}
	return split_multichar(s, splitter_chars, allow_empty, maxsplit);
}

Vector<String> gdre::_rsplit_multichar(const String &s, const Vector<String> &splitters, bool allow_empty, int maxsplit) {
	HashSet<char32_t> splitter_chars;
	for (int i = 0; i < splitters.size(); i++) {
		if (splitters[i].length() > 1) {
			ERR_FAIL_V_MSG(Vector<String>(), "rsplit_multichar only supports single-character splitters.");
		}
		splitter_chars.insert(splitters[i][0]);
	}
	return rsplit_multichar(s, splitter_chars, allow_empty, maxsplit);
}

Vector<String> gdre::split_multichar(const String &s, const HashSet<char32_t> &splitters, bool allow_empty, int maxsplit) {
	Vector<String> ret;
	String current;
	int i;
	for (i = 0; i < s.length(); i++) {
		if (splitters.has(s[i])) {
			if (current.length() > 0 || allow_empty) {
				ret.push_back(current);
				current = "";
				if (maxsplit > 0 && ret.size() >= maxsplit - 1) {
					i++;
					break;
				}
			}
		} else {
			current += s[i];
		}
	}
	if (i < s.length()) {
		current += s.substr(i, s.length());
	}
	if (current.length() > 0 || allow_empty) {
		ret.push_back(current);
	}
	return ret;
}

Vector<String> gdre::rsplit_multichar(const String &s, const HashSet<char32_t> &splitters, bool allow_empty, int maxsplit) {
	Vector<String> ret;
	String current;
	int i;
	for (i = s.length() - 1; i >= 0; i--) {
		if (splitters.has(s[i])) {
			if (current.length() > 0 || allow_empty) {
				ret.push_back(current);
				current = "";
				if (maxsplit > 0 && ret.size() >= maxsplit - 1) {
					i--;
					break;
				}
			}
		} else {
			current = s[i] + current;
		}
	}
	if (i >= 0) {
		current = s.substr(0, i + 1) + current;
	}
	if (current.length() > 0 || allow_empty) {
		ret.push_back(current);
	}
	ret.reverse();
	return ret;
}

bool gdre::string_has_whitespace(const String &s) {
	for (int i = 0; i < s.length(); i++) {
		if (s[i] == ' ' || s[i] == '\t' || s[i] == '\n') {
			return true;
		}
	}
	return false;
}

bool gdre::string_is_ascii(const String &s) {
	for (int i = 0; i < s.length(); i++) {
		if (s[i] > 127) {
			return false;
		}
	}
	return true;
}

bool gdre::detect_utf8(const PackedByteArray &p_utf8_buf) {
	int cstr_size = 0;
	int str_size = 0;
	const char *p_utf8 = (const char *)p_utf8_buf.ptr();
	int p_len = p_utf8_buf.size();
	if (p_len == 0) {
		return true; // empty string
	}
	/* HANDLE BOM (Byte Order Mark) */
	if (p_len < 0 || p_len >= 3) {
		bool has_bom = uint8_t(p_utf8[0]) == 0xef && uint8_t(p_utf8[1]) == 0xbb && uint8_t(p_utf8[2]) == 0xbf;
		if (has_bom) {
			//8-bit encoding, byte order has no meaning in UTF-8, just skip it
			if (p_len >= 0) {
				p_len -= 3;
			}
			p_utf8 += 3;
		}
	}

	{
		const char *ptrtmp = p_utf8;
		const char *ptrtmp_limit = p_len >= 0 ? &p_utf8[p_len] : nullptr;
		int skip = 0;
		uint8_t c_start = 0;
		while (ptrtmp != ptrtmp_limit && *ptrtmp) {
#if CHAR_MIN == 0
			uint8_t c = *ptrtmp;
#else
			uint8_t c = *ptrtmp >= 0 ? *ptrtmp : uint8_t(256 + *ptrtmp);
#endif

			if (skip == 0) {
				/* Determine the number of characters in sequence */
				if ((c & 0x80) == 0) {
					skip = 0;
				} else if ((c & 0xe0) == 0xc0) {
					skip = 1;
				} else if ((c & 0xf0) == 0xe0) {
					skip = 2;
				} else if ((c & 0xf8) == 0xf0) {
					skip = 3;
				} else if ((c & 0xfc) == 0xf8) {
					skip = 4;
				} else if ((c & 0xfe) == 0xfc) {
					skip = 5;
				} else {
					skip = 0;
					// print_unicode_error(vformat("Invalid UTF-8 leading byte (%x)", c), true);
					// decode_failed = true;
					return false;
				}
				c_start = c;

				if (skip == 1 && (c & 0x1e) == 0) {
					// print_unicode_error(vformat("Overlong encoding (%x ...)", c));
					// decode_error = true;
					return false;
				}
				str_size++;
			} else {
				if ((c_start == 0xe0 && skip == 2 && c < 0xa0) || (c_start == 0xf0 && skip == 3 && c < 0x90) || (c_start == 0xf8 && skip == 4 && c < 0x88) || (c_start == 0xfc && skip == 5 && c < 0x84)) {
					// print_unicode_error(vformat("Overlong encoding (%x %x ...)", c_start, c));
					// decode_error = true;
					return false;
				}
				if (c < 0x80 || c > 0xbf) {
					// print_unicode_error(vformat("Invalid UTF-8 continuation byte (%x ... %x ...)", c_start, c), true);
					// decode_failed = true;
					return false;

					// skip = 0;
				} else {
					--skip;
				}
			}

			cstr_size++;
			ptrtmp++;
		}
		// not checking for last sequence because we pass in incomplete bytes
		// if (skip) {
		// print_unicode_error(vformat("Missing %d UTF-8 continuation byte(s)", skip), true);
		// decode_failed = true;
		// return false;
		// }
	}

	if (str_size == 0) {
		// clear();
		return true; // empty string
	}

	// resize(str_size + 1);
	// char32_t *dst = ptrw();
	// dst[str_size] = 0;

	int skip = 0;
	uint32_t unichar = 0;
	while (cstr_size) {
#if CHAR_MIN == 0
		uint8_t c = *p_utf8;
#else
		uint8_t c = *p_utf8 >= 0 ? *p_utf8 : uint8_t(256 + *p_utf8);
#endif

		if (skip == 0) {
			/* Determine the number of characters in sequence */
			if ((c & 0x80) == 0) {
				// *(dst++) = c;
				unichar = 0;
				skip = 0;
			} else if ((c & 0xe0) == 0xc0) {
				unichar = (0xff >> 3) & c;
				skip = 1;
			} else if ((c & 0xf0) == 0xe0) {
				unichar = (0xff >> 4) & c;
				skip = 2;
			} else if ((c & 0xf8) == 0xf0) {
				unichar = (0xff >> 5) & c;
				skip = 3;
			} else if ((c & 0xfc) == 0xf8) {
				unichar = (0xff >> 6) & c;
				skip = 4;
			} else if ((c & 0xfe) == 0xfc) {
				unichar = (0xff >> 7) & c;
				skip = 5;
			} else {
				// *(dst++) = _replacement_char;
				// unichar = 0;
				// skip = 0;
				return false;
			}
		} else {
			if (c < 0x80 || c > 0xbf) {
				// *(dst++) = _replacement_char;
				skip = 0;
			} else {
				unichar = (unichar << 6) | (c & 0x3f);
				--skip;
				if (skip == 0) {
					if (unichar == 0) {
						return false;
						// print_unicode_error("NUL character", true);
						// decode_failed = true;
						// unichar = _replacement_char;
					} else if ((unichar & 0xfffff800) == 0xd800) {
						return false;

						// print_unicode_error(vformat("Unpaired surrogate (%x)", unichar), true);
						// decode_failed = true;
						// unichar = _replacement_char;
					} else if (unichar > 0x10ffff) {
						return false;

						// print_unicode_error(vformat("Invalid unicode codepoint (%x)", unichar), true);
						// decode_failed = true;
						// unichar = _replacement_char;
					}
					// *(dst++) = unichar;
				}
			}
		}

		cstr_size--;
		p_utf8++;
	}
	if (skip) {
		// return false;
		// *(dst++) = 0x20;
	}

	return true;
}

Error gdre::copy_dir(const String &src, const String &dst) {
	auto da = DirAccess::open(src);
	ERR_FAIL_COND_V_MSG(da.is_null(), ERR_FILE_CANT_OPEN, "Failed to open source directory: " + src);
	gdre::ensure_dir(dst);
	return da->copy_dir(src, dst);
}

void GDRECommon::_bind_methods() {
	//	ClassDB::bind_static_method("GLTFCamera", D_METHOD("from_node", "camera_node"), &GLTFCamera::from_node);

	ClassDB::bind_static_method("GDRECommon", D_METHOD("get_recursive_dir_list", "dir", "wildcards", "absolute", "rel"), &gdre::get_recursive_dir_list, DEFVAL(PackedStringArray()), DEFVAL(true), DEFVAL(""));
	ClassDB::bind_static_method("GDRECommon", D_METHOD("dir_has_any_matching_wildcards", "dir", "wildcards"), &gdre::dir_has_any_matching_wildcards);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("ensure_dir", "dir"), &gdre::ensure_dir);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("save_image_as_tga", "path", "img"), &gdre::save_image_as_tga);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("save_image_as_webp", "path", "img", "lossy"), &gdre::save_image_as_webp);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("save_image_as_jpeg", "path", "img"), &gdre::save_image_as_jpeg);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("get_md5", "dir", "ignore_code_signature"), &gdre::get_md5);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("get_md5_for_dir", "dir", "ignore_code_signature"), &gdre::get_md5_for_dir);
	// string_has_whitespace, string_is_ascii, detect_utf8, remove_chars, remove_whitespace, split_multichar, rsplit_multichar, has_chars_in_set, get_chars_in_set
	ClassDB::bind_static_method("GDRECommon", D_METHOD("string_has_whitespace", "str"), &gdre::string_has_whitespace);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("string_is_ascii", "str"), &gdre::string_is_ascii);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("detect_utf8", "utf8_buf"), &gdre::detect_utf8);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("remove_whitespace", "str"), &gdre::remove_whitespace);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("split_multichar", "str", "splitters", "allow_empty", "maxsplit"), &gdre::_split_multichar);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("rsplit_multichar", "str", "splitters", "allow_empty", "maxsplit"), &gdre::_rsplit_multichar);
	ClassDB::bind_static_method("GDRECommon", D_METHOD("copy_dir", "src", "dst"), &gdre::copy_dir);
}