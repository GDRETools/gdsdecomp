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

Vector<String> gdre::get_recursive_dir_list(const String dir, const Vector<String> &wildcards, const bool absolute, const String rel, const bool &res) {
	Vector<String> ret;
	Error err;
	Ref<DirAccess> da = DirAccess::open(dir.path_join(rel), &err);
	ERR_FAIL_COND_V_MSG(da.is_null(), ret, "Failed to open directory " + dir);

	if (da.is_null()) {
		return ret;
	}
	String base = absolute ? dir : "";
	da->list_dir_begin();
	String f = da->get_next();
	while (!f.is_empty()) {
		if (f == "." || f == "..") {
			f = da->get_next();
			continue;
		} else if (da->current_is_dir()) {
			ret.append_array(get_recursive_dir_list(dir, wildcards, absolute, rel.path_join(f)));
		} else {
			if (wildcards.size() > 0) {
				for (int i = 0; i < wildcards.size(); i++) {
					if (f.get_file().match(wildcards[i])) {
						ret.append(base.path_join(rel).path_join(f));
						break;
					}
				}
			} else {
				ret.append(base.path_join(rel).path_join(f));
			}
		}
		f = da->get_next();
	}
	da->list_dir_end();
	return ret;
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

Error gdre::wget_sync(const String &p_url, Vector<uint8_t> &response, int retries) {
	Ref<HTTPClientTCP> client;
	client.instantiate();
	client->set_blocking_mode(true);
	Error err;
	String url = p_url;
	auto connect_to_host_and_request = [&](const String &url) {
		bool is_https = url.begins_with("https://");
		String host = url.get_slice("://", 1).get_slice("/", 0);
		String thingy = (is_https ? "https://" : "http://") + host;
		Error err = client->connect_to_host(thingy, is_https ? 443 : 80);
		ERR_FAIL_COND_V_MSG(err, err, "Failed to connect to host " + url);
		while (client->get_status() == HTTPClient::STATUS_RESOLVING || client->get_status() == HTTPClient::STATUS_CONNECTING) {
			err = client->poll();
			if (err) {
				return err;
			}
		}
		if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
			return ERR_CANT_CONNECT;
		}
		err = client->request(HTTPClient::METHOD_GET, url, Vector<String>(), nullptr, 0);
		ERR_FAIL_COND_V_MSG(err, err, "Failed to connect to host " + url);
		return OK;
	};
	bool done = false;
	bool got_response = false;
	List<String> response_headers;
	int redirections = 0;
	int response_code = 0;
	auto _handle_response = [&]() -> Error {
		if (!client->has_response()) {
			return ERR_CANT_OPEN;
		}

		got_response = true;
		response_code = client->get_response_code();
		List<String> rheaders;
		client->get_response_headers(&rheaders);
		response_headers.clear();

		for (const String &E : rheaders) {
			response_headers.push_back(E);
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

	auto _retry = [&] {
		if (retries <= 0) {
			ERR_FAIL_V_MSG(ERR_CONNECTION_ERROR, vformat("Failed to download file from %s", p_url));
		}
		retries--;
		response.clear();
		return wget_sync(p_url, response, retries);
	};

	while (!done) {
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
						return _retry();
					}
				} else {
					client->poll();
					response.append_array(client->read_response_body_chunk());
				}
				break;
			}
			case HTTPClient::STATUS_CONNECTED: {
				if (!got_response) {
					err = _handle_response();
					if (err != OK) {
						return _retry();
					}
				} else {
					done = true;
				}
				break;
			}
			default: {
				return _retry();
			}
		}
	}
	ERR_FAIL_COND_V_MSG(response.is_empty(), ERR_CANT_CREATE, "Failed to download file from " + p_url);
	return OK;
}

Error gdre::download_file_sync(const String &p_url, const String &output_path) {
	Vector<uint8_t> response;
	Error err = wget_sync(p_url, response);
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