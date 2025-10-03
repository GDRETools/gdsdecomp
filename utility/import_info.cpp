#include "import_info.h"
#include "compat/resource_compat_binary.h"
#include "compat/resource_loader_compat.h"
#include "core/error/error_list.h"
#include "core/string/string_builder.h"

#include "compat/variant_writer_compat.h"
#include "gdre_settings.h"
#include "utility/common.h"
#include "utility/glob.h"

String ImportInfo::to_string() {
	return as_text(false);
}

String ImportInfo::as_text(bool full) {
	String s = "ImportInfo: {";
	s += "\n\timport_md_path: " + import_md_path;
	s += "\n\tpath: " + get_path();
	s += "\n\ttype: " + get_type();
	s += "\n\timporter: " + get_importer();
	s += "\n\tsource_file: " + get_source_file();
	auto additional_sources = get_additional_sources();
	if (additional_sources.size() > 0) {
		s += "\n\tadditional_sources: [";
		for (int64_t i = 0; i < additional_sources.size(); i++) {
			if (i > 0) {
				s += ", ";
			}
			s += " " + additional_sources[i];
		}
		s += " ]";
	}
	s += "\n\tdest_files: [";
	Vector<String> dest_files = get_dest_files();
	for (int64_t i = 0; i < dest_files.size(); i++) {
		if (i > 0) {
			s += ", ";
		}
		s += " " + dest_files[i];
	}
	s += " ]";
	Dictionary metadata_prop = get_metadata_prop();
	if (!metadata_prop.is_empty()) {
		s += "\n\tremap_metadata: {";
		for (auto &E : metadata_prop) {
			s += "\n\t\t" + E.key.to_json_string() + ": " + E.value.to_json_string();
		}
		s += "\n\t}";
	}
	s += "\n\tparams: {";
	Dictionary params = get_params();
	auto keys = params.get_key_list();
	for (int64_t i = 0; i < keys.size(); i++) {
		const Variant &key = keys[i];
		// skip excessively long options list
		if (!full && i == 8) {
			s += "\n\t\t[..." + itos(keys.size() - i) + " others...]";
			break;
		}
		String t = key;
		s += "\n\t\t" + t + "=" + (String)params[t];
	}
	s += "\n\t}\n}";
	return s;
}

int ImportInfo::get_import_loss_type() const {
	String importer = get_importer();
	String source_file = get_source_file();
	Dictionary params = get_params();
	if (importer == "scene" || importer == "ogg_vorbis" || importer == "mp3" || importer == "wavefront_obj" || auto_converted_export) {
		//These are always imported as either native files or losslessly
		return LOSSLESS;
	}
	if (!is_import()) {
		return BASE;
	}
	String ext = source_file.get_extension();
	bool has_compress_param = params.has("compress/mode") && params["compress/mode"].is_num();

	// textures and layered textures
	if (importer.begins_with("texture")) {
		int stat = 0;
		// These are always imported in such a way that it is impossible to recover the original file
		// SVG in particular is converted to raster from vector
		// However, you can convert these and rewrite the imports such that they will be imported losslessly next time
		if (ext == "svg" || ext == "jpg") {
			stat |= IMPORTED_LOSSY;
		}
		if (ver_major == 2) { //v2 all textures
			if (params.has("storage") && params["storage"].is_num()) { //v2
				stat |= (int)params["storage"] != V2ImportEnums::Storage::STORAGE_COMPRESS_LOSSY ? LOSSLESS : STORED_LOSSY;
			}
		} else if (importer == "texture" && has_compress_param) { // non-layered textures
			if (ver_major == 4 || ver_major == 3) {
				// COMPRESSED_LOSSLESS or COMPRESS_VRAM_UNCOMPRESSED, same in v3 and v4
				stat |= ((int)params["compress/mode"] == 0 || (int)params["compress/mode"] == 3) ? LOSSLESS : STORED_LOSSY;
			}
		} else if (has_compress_param) { // layered textures
			if (ver_major == 4) {
				// COMPRESSED_LOSSLESS or COMPRESS_VRAM_UNCOMPRESSED
				stat |= ((int)params["compress/mode"] == 0 || (int)params["compress/mode"] == 3) ? LOSSLESS : STORED_LOSSY;
			} else if (ver_major == 3) {
				stat |= ((int)params["compress/mode"] != V3LTexCompressMode::COMPRESS_VIDEO_RAM) ? LOSSLESS : STORED_LOSSY;
			}
		}
		return LossType(stat);
	} else if (importer == "wav" || (ver_major == 2 && importer == "sample")) {
		// Not possible to recover asset used to import losslessly
		if (has_compress_param) {
			return (int)params["compress/mode"] == 0 ? LOSSLESS : STORED_LOSSY;
		}
	}

	if (ver_major == 2 && importer == "font") {
		// Not possible to recover asset used to import losslessly
		if (params.has("mode/mode") && params["mode/mode"].is_num()) {
			return (int)params["mode/mode"] == V2ImportEnums::FontMode::FONT_DISTANCE_FIELD ? LOSSLESS : STORED_LOSSY;
		}
	}

	// We can't say for sure
	return BASE;
}

Ref<ConfigFile> copy_config_file(Ref<ConfigFile> p_cf) {
	Ref<ConfigFile> r_cf;
	r_cf.instantiate();
	Vector<String> sections = p_cf->get_sections();
	for (int64_t i = 0; i < sections.size(); i++) {
		String section = sections[i];
		Vector<String> section_keys = p_cf->get_section_keys(section);
		for (int j = 0; j < section_keys.size(); j++) {
			String key = section_keys[j];
			r_cf->set_value(section, key, p_cf->get_value(section, key));
		}
	}
	return r_cf;
}

Ref<ResourceImportMetadatav2> copy_imd_v2(Ref<ResourceImportMetadatav2> p_cf) {
	Ref<ResourceImportMetadatav2> r_imd;
	r_imd.instantiate();
	r_imd->set_editor(p_cf->get_editor());
	int src_count = p_cf->get_source_count();
	for (int i = 0; i < src_count; i++) {
		r_imd->add_source(p_cf->get_source_path(i), p_cf->get_source_md5(i));
	}
	List<String> r_options;
	p_cf->get_options(&r_options);
	for (auto E = r_options.front(); E; E = E->next()) {
		r_imd->set_option(E->get(), p_cf->get_option(E->get()));
	}
	return r_imd;
}

Ref<ImportInfo> ImportInfo::copy(const Ref<ImportInfo> &p_iinfo) {
	ERR_FAIL_COND_V_MSG(p_iinfo.is_null(), Ref<ImportInfo>(), "ImportInfo is null");
	Ref<ImportInfo> r_iinfo;
	switch (p_iinfo->iitype) {
		case IInfoType::MODERN:
			r_iinfo = Ref<ImportInfo>(memnew(ImportInfoModern));
			((Ref<ImportInfoModern>)r_iinfo)->src_md5 = ((Ref<ImportInfoModern>)p_iinfo)->src_md5;
			((Ref<ImportInfoModern>)r_iinfo)->cf = copy_config_file(((Ref<ImportInfoModern>)p_iinfo)->cf);
			break;
		case IInfoType::V2:
			r_iinfo = Ref<ImportInfo>(memnew(ImportInfov2));
			((Ref<ImportInfov2>)r_iinfo)->type = ((Ref<ImportInfov2>)p_iinfo)->type;
			((Ref<ImportInfov2>)r_iinfo)->dest_files = ((Ref<ImportInfov2>)p_iinfo)->dest_files;
			((Ref<ImportInfov2>)r_iinfo)->v2metadata = copy_imd_v2(((Ref<ImportInfov2>)p_iinfo)->v2metadata);
			break;
		case IInfoType::DUMMY:
			r_iinfo = Ref<ImportInfo>(memnew(ImportInfoDummy));
			((Ref<ImportInfoDummy>)r_iinfo)->type = ((Ref<ImportInfoDummy>)p_iinfo)->type;
			((Ref<ImportInfoDummy>)r_iinfo)->source_file = ((Ref<ImportInfoDummy>)p_iinfo)->source_file;
			((Ref<ImportInfoDummy>)r_iinfo)->src_md5 = ((Ref<ImportInfoDummy>)p_iinfo)->src_md5;
			((Ref<ImportInfoDummy>)r_iinfo)->dest_files = ((Ref<ImportInfoDummy>)p_iinfo)->dest_files;
		case IInfoType::REMAP:
			r_iinfo = Ref<ImportInfo>(memnew(ImportInfoRemap));
			((Ref<ImportInfoRemap>)r_iinfo)->type = ((Ref<ImportInfoRemap>)p_iinfo)->type;
			((Ref<ImportInfoRemap>)r_iinfo)->source_file = ((Ref<ImportInfoRemap>)p_iinfo)->source_file;
			((Ref<ImportInfoRemap>)r_iinfo)->src_md5 = ((Ref<ImportInfoRemap>)p_iinfo)->src_md5;
			((Ref<ImportInfoRemap>)r_iinfo)->dest_files = ((Ref<ImportInfoRemap>)p_iinfo)->dest_files;
			((Ref<ImportInfoRemap>)r_iinfo)->importer = ((Ref<ImportInfoRemap>)p_iinfo)->importer;
			break;
		case IInfoType::GDEXT:
			r_iinfo = Ref<ImportInfo>(memnew(ImportInfoGDExt));
			((Ref<ImportInfoGDExt>)r_iinfo)->type = ((Ref<ImportInfoGDExt>)p_iinfo)->type;
			((Ref<ImportInfoGDExt>)r_iinfo)->source_file = ((Ref<ImportInfoGDExt>)p_iinfo)->source_file;
			((Ref<ImportInfoGDExt>)r_iinfo)->src_md5 = ((Ref<ImportInfoGDExt>)p_iinfo)->src_md5;
			((Ref<ImportInfoGDExt>)r_iinfo)->dest_files = ((Ref<ImportInfoGDExt>)p_iinfo)->dest_files;
			((Ref<ImportInfoGDExt>)r_iinfo)->importer = ((Ref<ImportInfoGDExt>)p_iinfo)->importer;
			((Ref<ImportInfoGDExt>)r_iinfo)->cf = copy_config_file(((Ref<ImportInfoGDExt>)p_iinfo)->cf);
		default:
			break;
	}
	r_iinfo->import_md_path = p_iinfo->import_md_path;
	r_iinfo->ver_major = p_iinfo->ver_major;
	r_iinfo->ver_minor = p_iinfo->ver_minor;
	r_iinfo->format_ver = p_iinfo->format_ver;
	r_iinfo->not_an_import = p_iinfo->not_an_import;
	r_iinfo->auto_converted_export = p_iinfo->auto_converted_export;
	r_iinfo->preferred_import_path = p_iinfo->preferred_import_path;
	r_iinfo->export_dest = p_iinfo->export_dest;
	r_iinfo->export_lossless_copy = p_iinfo->export_lossless_copy;
	return r_iinfo;
}

ImportInfo::ImportInfo() {
	import_md_path = "";
	ver_major = 0;
	ver_minor = 0;
	export_dest = "";
	iitype = IInfoType::BASE;
}

ImportInfoModern::ImportInfoModern() {
	cf.instantiate();
	iitype = IInfoType::MODERN;
}

ImportInfov2::ImportInfov2() {
	v2metadata.instantiate();
	iitype = IInfoType::V2;
}

ImportInfoDummy::ImportInfoDummy() {
	iitype = IInfoType::DUMMY;
}

ImportInfoRemap::ImportInfoRemap() {
	iitype = IInfoType::REMAP;
}

ImportInfoGDExt::ImportInfoGDExt() {
	iitype = IInfoType::GDEXT;
}

Error ImportInfo::get_resource_info(const String &p_path, Ref<ResourceInfo> &res_info) {
	Error err = OK;
	if (!FileAccess::exists(p_path)) {
		return ERR_FILE_NOT_FOUND;
	}
	if (!ResourceCompatLoader::handles_resource(p_path)) {
		return ERR_UNAVAILABLE;
	}
	res_info = ResourceCompatLoader::get_resource_info(p_path, "", &err);
	ERR_FAIL_COND_V_MSG(err != OK || !res_info.is_valid(), err, "Could not load resource info from " + p_path);
	return OK;
}

Ref<ImportInfo> ImportInfo::load_from_file(const String &p_path, int ver_major, int ver_minor) {
	Ref<ImportInfo> iinfo;
	Error err = OK;
	if (p_path.get_extension() == "import") {
		iinfo = Ref<ImportInfo>(memnew(ImportInfoModern));
		err = iinfo->_load(p_path);
		if (err == OK && iinfo.is_valid() && iinfo->ver_major == 0 && ver_major != 0) {
			iinfo->ver_major = ver_major;
			iinfo->ver_minor = ver_minor;
		}
	} else if (p_path.get_extension() == "remap") {
		// .remap file for an autoconverted export
		iinfo = Ref<ImportInfoRemap>(memnew(ImportInfoRemap));
		err = iinfo->_load(p_path);
	} else if (p_path.get_extension() == "gdnlib" || p_path.get_extension() == "gdextension") {
		iinfo = Ref<ImportInfoGDExt>(memnew(ImportInfoGDExt));
		err = iinfo->_load(p_path);
	} else {
		if (ver_major == 0 && ResourceCompatLoader::handles_resource(p_path)) {
			Ref<ResourceInfo> res_info;
			err = get_resource_info(p_path, res_info);
			ERR_FAIL_COND_V_MSG(err != OK, Ref<ImportInfo>(), "Could not load resource info for " + p_path);
			ver_major = res_info->ver_major;
		}
		if (err == OK) {
			if (ver_major <= 2) {
				iinfo = Ref<ImportInfo>(memnew(ImportInfov2));
				err = iinfo->_load(p_path);
			} else if (ResourceCompatLoader::handles_resource(p_path)) {
				iinfo = Ref<ImportInfo>(memnew(ImportInfoDummy));
				err = iinfo->_load(p_path);
			} else {
				ERR_FAIL_V_MSG(Ref<ImportInfo>(), "Could not load import info; not a resource: " + p_path);
			}
		}
	}
	if (err != OK) {
		return Ref<ImportInfo>();
	}
	return iinfo;
}

String ImportInfoModern::get_type() const {
	return cf->get_value("remap", "type", "");
}

void ImportInfoModern::set_type(const String &p_type) {
	cf->set_value("remap", "type", "");
}

String ImportInfoModern::get_compat_type() const {
	return ClassDB::get_compatibility_remapped_class(get_type());
}

String ImportInfoModern::get_importer() const {
	return cf->get_value("remap", "importer", "");
}

String ImportInfoModern::get_source_file() const {
	return cf->get_value("deps", "source_file", "");
}

void ImportInfoModern::set_source_file(const String &p_path) {
	cf->set_value("deps", "source_file", p_path);
	dirty = true;
}

void ImportInfoModern::set_source_and_md5(const String &path, const String &md5) {
	cf->set_value("deps", "source_file", path);
	src_md5 = md5;
	dirty = true;
	// TODO: change the md5 file?
}

String ImportInfoModern::get_source_md5() const {
	return src_md5;
}

void ImportInfoModern::set_source_md5(const String &md5) {
	src_md5 = md5;
}

String ImportInfoModern::get_uid() const {
	return cf->get_value("remap", "uid", "");
}

Vector<String> ImportInfoModern::get_dest_files() const {
	return cf->get_value("deps", "dest_files", Vector<String>());
}
namespace {
Vector<String> get_remap_paths(const Ref<ConfigFile> &cf) {
	Vector<String> remap_paths;
	Vector<String> remap_keys = cf->get_section_keys("remap");
	// iterate over keys in remap section
	for (int64_t i = 0; i < remap_keys.size(); i++) {
		// if we find a path key, we have a match
		if (remap_keys[i].begins_with("path.") || remap_keys[i] == "path") {
			String try_path = cf->get_value("remap", remap_keys[i], "");
			remap_paths.push_back(try_path);
		}
	}
	return remap_paths;
}
Array vec_to_array(const Vector<String> &vec) {
	Array arr;
	for (int64_t i = 0; i < vec.size(); i++) {
		arr.push_back(vec[i]);
	}
	return arr;
}
} //namespace

void ImportInfoModern::set_dest_files(const Vector<String> p_dest_files) {
	cf->set_value("deps", "dest_files", vec_to_array(p_dest_files));
	dirty = true;
	if (!cf->has_section("remap")) {
		return;
	}
	if (!cf->has_section_key("remap", "path")) {
		Vector<String> remap_keys = cf->get_section_keys("remap");
		// if set, we likely have multiple paths
		if (get_metadata_prop().has("imported_formats")) {
			for (int64_t i = 0; i < p_dest_files.size(); i++) {
				Vector<String> spl = p_dest_files[i].split(".");
				// second to last split
				ERR_FAIL_COND_MSG(spl.size() < 4, "Expected to see format in path " + p_dest_files[i]);
				String ext = spl[spl.size() - 2];
				int idx = remap_keys.find("path." + ext);

				if (idx == -1) {
					WARN_PRINT("Did not find key path." + ext + " in remap metadata, setting anwyway...");
				}
				cf->set_value("remap", "path." + ext, p_dest_files[i]);
			}
		} else {
			cf->set_value("remap", "path", p_dest_files[0]);
		}
	} else {
		cf->set_value("remap", "path", p_dest_files[0]);
	}
}

Dictionary ImportInfoModern::get_metadata_prop() const {
	return cf->get_value("remap", "metadata", Dictionary());
}

void ImportInfoModern::set_metadata_prop(Dictionary r_dict) {
	cf->set_value("remap", "metadata", Dictionary());
	dirty = true;
}

Variant ImportInfoModern::get_param(const String &p_key) const {
	if (!cf->has_section_key("params", p_key)) {
		return Variant();
	}
	return cf->get_value("params", p_key);
}

void ImportInfoModern::set_param(const String &p_key, const Variant &p_val) {
	// erase the dummy value if it exists, it's no longer needed
	cf->set_value("params", p_key, p_val);
	dirty = true;
}

bool ImportInfoModern::has_param(const String &p_key) const {
	return cf->has_section_key("params", p_key);
}

Variant ImportInfoModern::get_iinfo_val(const String &p_section, const String &p_prop) const {
	if (!cf->has_section_key(p_section, p_prop)) {
		return Variant();
	}
	return cf->get_value(p_section, p_prop);
}

void ImportInfoModern::set_iinfo_val(const String &p_section, const String &p_prop, const Variant &p_val) {
	cf->set_value(p_section, p_prop, p_val);
	dirty = true;
}

Dictionary ImportInfoModern::get_params() const {
	Dictionary params;
	if (cf->has_section("params")) {
		Vector<String> param_keys = cf->get_section_keys("params");
		params = Dictionary();
		for (int64_t i = 0; i < param_keys.size(); i++) {
			params[param_keys[i]] = cf->get_value("params", param_keys[i], "");
		}
	}
	return params;
}

void ImportInfoModern::set_params(Dictionary params) {
	auto param_keys = params.get_key_list();
	for (int64_t i = 0; i < param_keys.size(); i++) {
		const Variant &key = param_keys[i];
		cf->set_value("params", key, params[key]);
	}
	dirty = true;
}

Error ImportInfoModern::_load(const String &p_path) {
	cf.instantiate();
	String path = p_path;
	path = GDRESettings::get_singleton()->localize_path(p_path);
	Error err = cf->load(path);
	if (err) {
		cf = Ref<ConfigFile>();
	}
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not load " + path);
	import_md_path = path;
	preferred_import_path = cf->get_value("remap", "path", "");

	Vector<String> dest_files;

	// Godot 4.x started stripping the deps section from the .import file, need to recreate it
	if (!cf->has_section("deps")) {
		dirty = true;
		// the source file is the import_md path minus ".import"
		String source_file = import_md_path.get_basename();
		if (!preferred_import_path.is_empty()) {
			set_source_file(source_file);
			cf->set_value("deps", "dest_files", vec_to_array({ preferred_import_path }));
		} else {
			// this is a multi-path import, get all the "path.*" key values
			dest_files = get_remap_paths(cf);
			// No path values at all; may be a translation file
			if (dest_files.is_empty()) {
				String importer = cf->get_value("remap", "importer", "");
				if (importer == "csv_translation") {
					// They recently started removing the path from the [remap] section for these types
					String prefix = source_file.get_basename();
					if (dest_files.size() == 0) {
						dest_files = Glob::glob(prefix + ".*.translation");
					}
					// The reason for doing this is because the editor expects the files in the [deps]
					// section to be in the same order as the project settings, otherwise it will force a re-import
					PackedStringArray translation_files = GDRESettings::get_singleton()->get_project_setting("internationalization/locale/translations", PackedStringArray());
					Vector<String> translation_files_set;
					for (auto &file : translation_files) {
						if (dest_files.has(file)) {
							translation_files_set.push_back(file);
						}
					}
					if (dest_files.size() == translation_files_set.size()) {
						dest_files = translation_files_set;
					} else {
						// otherwise, just move the fallback locale to the front.
						String fallback_locale = GDRESettings::get_singleton()->get_project_setting("internationalization/locale/fallback", "en");
						String suffix = fallback_locale.to_lower() + ".translation";
						for (int i = 0; i < dest_files.size(); i++) {
							if (dest_files[i].ends_with(suffix)) {
								String first_file = dest_files[i];
								dest_files.remove_at(i);
								dest_files.insert(0, first_file);
								break;
							}
						}
					}
				}
			}
			if (dest_files.size() > 1) { // only write this if there are multiple files
				Array arr = gdre::hashset_to_array(gdre::vector_to_hashset(dest_files));
				// we still write this even if the deduped list is 1 file
				cf->set_value("deps", "files", arr);
			}
			// In import files, it goes "files", then "source_file", then "dest_files"
			set_source_file(source_file);
			cf->set_value("deps", "dest_files", vec_to_array(dest_files));
		}
	}

	if (!cf->has_section("params")) {
		dirty = true; // will be taken care of in save_to()
	}

	// "remap.path" does not exist if there are two or more destination files
	if (preferred_import_path.is_empty()) {
		//check destination files
		if (dest_files.size() == 0) {
			dest_files = get_remap_paths(cf);
			// Reverse the order; we want to get the s3tc textures first if they exist.
			dest_files.reverse();
		}
		if (dest_files.size() == 0) {
			dest_files = get_dest_files();
		}
		ERR_FAIL_COND_V_MSG(dest_files.size() == 0, ERR_FILE_CORRUPT, p_path + ": no destination files found in import data");
		for (int64_t i = 0; i < dest_files.size(); i++) {
			if (FileAccess::exists(dest_files[i])) {
				preferred_import_path = dest_files[i];
				break;
			}
		}
		if (preferred_import_path.is_empty()) {
			// just set it to the first one
			preferred_import_path = dest_files[0];
		}
	}
	// If we fail to find the import path, throw error
	if (preferred_import_path.is_empty() || get_type().is_empty()) {
		ERR_FAIL_COND_V_MSG(preferred_import_path.is_empty() || get_type().is_empty(), ERR_FILE_CORRUPT, p_path + ": file is corrupt");
	}
	bool suspicious;
	uint32_t major, minor;
	for (auto &E : dest_files) {
		if (ResourceFormatLoaderCompatBinary::get_ver_major_minor(E, major, minor, suspicious) == OK) {
			ver_major = major;
			ver_minor = minor;
			break;
		}
	}

	return OK;
}

Error ImportInfoDummy::_load(const String &p_path) {
	Error err;
	Ref<ResourceInfo> res_info;
	err = ImportInfo::get_resource_info(p_path, res_info);
	ERR_FAIL_COND_V_MSG(err == ERR_UNAVAILABLE, err, "Could not load resource info for " + p_path);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not load resource " + p_path);
	preferred_import_path = p_path;
	source_file = "";
	not_an_import = true;
	ver_major = res_info->ver_major;
	ver_minor = res_info->ver_minor;
	type = res_info->type;
	dest_files = Vector<String>({ p_path });
	import_md_path = "";
	return OK;
}

Ref<ImportInfo> ImportInfoDummy::create_dummy(const String &p_path) {
	Ref<ImportInfoDummy> iinfo = memnew(ImportInfoDummy);
	if (iinfo->_load(p_path) != OK) {
		return Ref<ImportInfo>();
	}
	return iinfo;
}

Error ImportInfoRemap::_load(const String &p_path) {
	Ref<ConfigFile> cf;
	cf.instantiate();
	source_file = p_path.get_basename(); // res://scene.tscn.remap -> res://scene.tscn
	String path = p_path;
	path = GDRESettings::get_singleton()->localize_path(p_path);
	Error err = cf->load(path);
	if (err) {
		cf = Ref<ConfigFile>();
	}
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not load " + path);
	Vector<String> remap_keys = cf->get_section_keys("remap");
	if (remap_keys.size() == 0) {
		ERR_FAIL_V_MSG(ERR_BUG, "Failed to load import data from " + path);
	}
	preferred_import_path = cf->get_value("remap", "path", "");
	const String src_ext = source_file.get_extension().to_lower();
	Ref<ResourceInfo> res_info;
	err = ImportInfo::get_resource_info(preferred_import_path, res_info);
	if (err == ERR_UNAVAILABLE) {
		print_line("WARNING: Can't load resource info from remap path " + preferred_import_path + "...");
	} else if (err == ERR_FILE_NOT_FOUND) {
		print_line("WARNING: Remap path " + preferred_import_path + " does not exist...");
	} else {
		ERR_FAIL_COND_V_MSG(err != OK, err, "Could not load resource info from remap path " + preferred_import_path);
		type = res_info->type;
		ver_major = res_info->ver_major;
		ver_minor = res_info->ver_minor;
	}
	dest_files = Vector<String>({ preferred_import_path });
	not_an_import = true;
	import_md_path = p_path;
	auto_converted_export = preferred_import_path != source_file;
	if (auto_converted_export) {
		if (src_ext == "gd") {
			importer = "script_bytecode";
		} else {
			importer = "autoconverted";
		}
	}
	return OK;
}

Error ImportInfov2::_load(const String &p_path) {
	Error err;
	Ref<ResourceInfo> res_info;
	preferred_import_path = p_path;
	err = ImportInfo::get_resource_info(preferred_import_path, res_info);
	if (err) {
		ERR_FAIL_V_MSG(err, "Could not load resource info from " + p_path);
	}
	String source_file;
	String importer;
	// This is an import file, possibly has import metadata
	type = res_info->type;
	import_md_path = p_path;
	dest_files.push_back(p_path);
	ver_major = res_info->ver_major;
	ver_minor = res_info->ver_minor;
	if (res_info->v2metadata.is_valid() && res_info->v2metadata->get_source_count() > 0 && !res_info->v2metadata->get_editor().is_empty()) {
		v2metadata = res_info->v2metadata;
		return OK;
	} else if (res_info->v2metadata.is_valid()) {
		// v2 sometimes wrote a "thumbnail" param to the import metadata, even if the resource was not an import
		v2metadata = res_info->v2metadata;
		if (v2metadata->get_source_count() > 0) {
			source_file = v2metadata->get_source_path(0);
		}
		importer = v2metadata->get_editor();
	} else {
		v2metadata.instantiate();
	}
	if (source_file.is_empty()) {
		source_file = GDRESettings::get_singleton()->get_remapped_source_path(p_path);
	}
	Vector<String> spl = p_path.get_file().split(".");
	// Otherwise, we dont have any meta data, and we have to guess what it is
	// If this is a "converted" file, then it won't have import metadata, and we expect that
	String old_ext = p_path.get_extension().to_lower();

	auto get_new_ext = [&](String e) {
		String new_ext;
		if (e == "tex") {
			new_ext = "png";
		} else if (e == "smp") {
			new_ext = "wav";
		} else if (e == "cbm") {
			new_ext = "cube";
		} else if (type == "AtlasTexture") {
			// auto-created AtlasTexture, it would be in the project directory
			new_ext = "png";
		} else if (e == "scn" || type == "PackedScene") {
			new_ext = "glb";
		} else {
			new_ext = "fixme";
		}
		return new_ext;
	};

	if ((old_ext == "gde" || old_ext == "gdc")) {
		auto_converted_export = true;
		source_file = source_file.is_empty() ? p_path.get_basename().trim_suffix(".converted") + ".gd" : source_file;
		importer = "script_bytecode";
	} else if (source_file.is_empty() && !p_path.contains(".converted.")) {
		String base_dir = "res://.assets";
		String new_ext = get_new_ext(old_ext);
		if (type == "AtlasTexture") {
			// auto-created AtlasTexture, it would be in the project directory
			base_dir = "res://";
		}
		// others??
		source_file = base_dir.path_join(p_path.replace("res://", "").get_base_dir().path_join(spl[0] + "." + new_ext));
	} else {
		auto_converted_export = true;
		if (source_file.is_empty()) {
			// if this doesn't match "filename.ext.converted.newext"
			String base = spl[0];
			String ext = spl.size() != 4 ? get_new_ext(old_ext) : spl[1];
			source_file = p_path.get_base_dir().path_join(base + "." + ext);
		}
		if (!res_info->get_type().to_lower().contains("texture") && !res_info->get_type().to_lower().contains("sample")) {
			importer = "autoconverted";
		}
	}

	not_an_import = true;
	// If it's a converted file without metadata, it won't have this, and we need it for checking if the file is lossy or not
	if (importer == "") {
		if (old_ext == "scn") {
			importer = "scene";
		} else if (old_ext == "res") {
			importer = "resource";
		} else if (old_ext == "tex") {
			importer = "texture";
		} else if (old_ext == "smp") {
			importer = "sample";
		} else if (old_ext == "fnt") {
			importer = "font";
		} else if (old_ext == "msh") {
			importer = "mesh";
		} else if (old_ext == "xl") {
			importer = "translation";
		} else if (old_ext == "pbm") {
			importer = "bitmask";
		} else if (old_ext == "cbm") {
			importer = "cubemap";
		} else if (old_ext == "atex") {
			importer = "texture_atlas";
		} else if (old_ext == "gdc" || old_ext == "gde") {
			importer = "script_bytecode";
		} else {
			importer = "";
		}
	}
	if (source_file.is_empty()) {
		print_line("WARNING: Can't find source file for " + p_path);
	} else if (v2metadata->get_source_count() == 0) {
		v2metadata->add_source_at(source_file, "", 0);
	}
	v2metadata->set_editor(importer);
	return OK;
}

String ImportInfov2::get_type() const {
	return type;
}

void ImportInfov2::set_type(const String &p_type) {
	type = p_type;
}

String ImportInfov2::get_compat_type() const {
	return ClassDB::get_compatibility_remapped_class(get_type());
}

String ImportInfov2::get_importer() const {
	return v2metadata->get_editor();
}

String ImportInfov2::get_source_file() const {
	if (v2metadata->get_source_count() > 0) {
		return v2metadata->get_source_path(0);
	}
	return "";
}

void ImportInfov2::set_source_file(const String &p_path) {
	set_source_and_md5(p_path, "");
	dirty = true;
}

void ImportInfov2::set_source_and_md5(const String &path, const String &md5) {
	if (v2metadata->get_source_count() > 0) {
		v2metadata->remove_source(0);
	}
	v2metadata->add_source_at(path, md5, 0);
	dirty = true;
}

String ImportInfov2::get_source_md5() const {
	if (v2metadata->get_source_count() > 0) {
		return v2metadata->get_source_md5(0);
	}
	return "";
}

void ImportInfov2::set_source_md5(const String &md5) {
	v2metadata->set_source_md5(0, md5);
	dirty = true;
}

Vector<String> ImportInfov2::get_dest_files() const {
	return dest_files;
}

void ImportInfov2::set_dest_files(const Vector<String> p_dest_files) {
	dest_files = p_dest_files;
}

Vector<String> ImportInfov2::get_additional_sources() const {
	Vector<String> srcs;
	for (int i = 1; i < v2metadata->get_source_count(); i++) {
		srcs.push_back(v2metadata->get_source_path(i));
	}
	return srcs;
}

void ImportInfov2::set_additional_sources(const Vector<String> &p_add_sources) {
	// TODO: md5s
	for (int64_t i = 1; i < p_add_sources.size(); i++) {
		if (v2metadata->get_source_count() >= i) {
			v2metadata->remove_source(i);
		}
		v2metadata->add_source_at(p_add_sources[i], "", i);
	}
	dirty = true;
}

Variant ImportInfov2::get_param(const String &p_key) const {
	return v2metadata->get_option(p_key);
}

void ImportInfov2::set_param(const String &p_key, const Variant &p_val) {
	dirty = true;
	return v2metadata->set_option(p_key, p_val);
}

bool ImportInfov2::has_param(const String &p_key) const {
	return v2metadata->has_option(p_key);
}

Variant ImportInfov2::get_iinfo_val(const String &p_section, const String &p_prop) const {
	if (p_section == "params" || p_section == "options") {
		return v2metadata->get_option(p_prop);
	}
	//TODO: others?
	return Variant();
}

void ImportInfov2::set_iinfo_val(const String &p_section, const String &p_prop, const Variant &p_val) {
	if (p_section == "params" || p_section == "options") {
		dirty = true;
		return v2metadata->set_option(p_prop, p_val);
	}
	//TODO: others?
}

Dictionary ImportInfov2::get_params() const {
	return v2metadata->get_options_as_dictionary();
}

void ImportInfov2::set_params(Dictionary params) {
	LocalVector<Variant> param_keys = params.get_key_list();
	for (auto &E : param_keys) {
		v2metadata->set_option(E, params[E]);
	}
	dirty = true;
}

String encode_cfg_to_text(const Ref<ConfigFile> &cf, int ver_major, int ver_minor, bool gdext = false) {
	StringBuilder sb;
	bool first = true;
	static const String null_replacement = String("\"") + ImportInfo::NULL_REPLACEMENT + "\"";
	for (const String &section : cf->get_sections()) {
		if (first) {
			first = false;
		} else {
			sb.append("\n");
		}
		if (!section.is_empty()) {
			sb.append("[" + section + "]\n");
			if (!gdext) {
				sb.append("\n");
			}
		}

		for (const String &key : cf->get_section_keys(section)) {
			String vstr;
			Variant value = cf->get_value(section, key);
			if (gdext && value.get_type() == Variant::DICTIONARY) {
				Dictionary dict = value;
				LocalVector<Variant> keys = dict.get_key_list();
				vstr = "{";
				String temp_vstr;
				for (size_t i = 0; i < keys.size(); i++) {
					const Variant &E = keys[i];
					temp_vstr.clear();
					VariantWriterCompat::write_to_string(E, temp_vstr, ver_major, ver_minor);
					vstr += temp_vstr;
					vstr += ": ";
					temp_vstr.clear();
					VariantWriterCompat::write_to_string(dict[E], temp_vstr, ver_major, ver_minor);
					vstr += temp_vstr;
					if (i < keys.size() - 1) {
						vstr += ", ";
					}
				}
				vstr += "}";
			} else {
				VariantWriterCompat::write_to_string(value, vstr, ver_major, ver_minor);
			}
			// ConfigFile interprets setting a key to null as erasing the key, so we have to use a special value that'll get replaced when saving.
			if (vstr == null_replacement) {
				vstr = "null";
			}
			sb.append(key.property_name_encode() + (gdext ? " = " : "=") + vstr + "\n");
			if (section == "deps" && key == "files") {
				// extra newline for some reason
				sb.append("\n");
			}
		}
	}
	return sb.as_string();
}

Error ImportInfoModern::save_to(const String &new_import_file) {
	Error err = gdre::ensure_dir(new_import_file.get_base_dir());
	ERR_FAIL_COND_V_MSG(err, err, "Failed to create directory for " + new_import_file);

	String content = encode_cfg_to_text(cf, ver_major, ver_minor);
	if (!cf->has_section("params")) {
		content += "\n[params]\n\n";
	}
	auto fa = FileAccess::open(new_import_file, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(fa.is_null(), ERR_FILE_CANT_OPEN, "Failed to open file " + new_import_file);
	fa->store_string(content);
	fa->flush();
	return OK;
}

Error ImportInfov2::save_to(const String &new_import_file) {
	Error err = gdre::ensure_dir(new_import_file.get_base_dir());
	ERR_FAIL_COND_V_MSG(err, err, "Failed to create directory for " + new_import_file);
	err = ResourceFormatLoaderCompatBinary::rewrite_v2_import_metadata(import_md_path, new_import_file, v2metadata);
	ERR_FAIL_COND_V_MSG(err, err, "Failed to rename file " + import_md_path + ".tmp");
	return err;
}

String ImportInfoModern::get_md5_file_path() const {
	return (ver_major <= 3 ? "res://.import/" : "res://.godot/imported/") + get_source_file().get_file() + "-" + get_source_file().md5_text() + ".md5";
}

Error ImportInfoModern::save_md5_file(const String &output_dir) {
	Vector<String> dest_files = get_dest_files();
	if (dest_files.size() == 0) {
		return ERR_PRINTER_ON_FIRE;
	}

	String actual_source = get_source_file();
	if (export_dest != actual_source) {
		return ERR_PRINTER_ON_FIRE;
	}
	// check if each exists
	for (int64_t i = 0; i < dest_files.size(); i++) {
		if (!FileAccess::exists(dest_files[i])) {
			//WARN_PRINT("Cannot find " + dest_files[i] + ", cannot compute dest_md5.");
			return ERR_PRINTER_ON_FIRE;
		}
	}
	// dest_md5 is the md5 of all the destination files together
	String dst_md5 = FileAccess::get_multiple_md5(dest_files);
	ERR_FAIL_COND_V_MSG(dst_md5.is_empty(), ERR_FILE_BAD_PATH, "Can't open import resources to check md5!");

	if (src_md5.is_empty()) {
		String exported_src_path = output_dir.path_join(actual_source.replace_first("res://", ""));
		src_md5 = FileAccess::get_md5(exported_src_path);
		if (src_md5.is_empty()) {
			ERR_FAIL_COND_V_MSG(src_md5.is_empty(), ERR_FILE_BAD_PATH, "Can't open exported resource to check md5!");
		}
	}
	String md5_file_path = output_dir.path_join(get_md5_file_path().trim_prefix("res://"));
	gdre::ensure_dir(md5_file_path.get_base_dir());
	Ref<FileAccess> md5_file = FileAccess::open(md5_file_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(md5_file.is_null(), ERR_FILE_CANT_OPEN, "Can't open exported resource to check md5!");
	md5_file->store_string("source_md5=\"" + src_md5 + "\"\ndest_md5=\"" + dst_md5 + "\"\n\n");
	md5_file->flush();
	return OK;
}

void ImportInfo::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("load_from_file", "path", "ver_major", "ver_minor"), &ImportInfo::load_from_file, DEFVAL(0), DEFVAL(0));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("copy", "import_info"), &ImportInfo::copy);

	ClassDB::bind_method(D_METHOD("get_iitype"), &ImportInfo::get_iitype);

	ClassDB::bind_method(D_METHOD("get_ver_major"), &ImportInfo::get_ver_major);
	ClassDB::bind_method(D_METHOD("get_ver_minor"), &ImportInfo::get_ver_minor);

	ClassDB::bind_method(D_METHOD("get_import_loss_type"), &ImportInfo::get_import_loss_type);

	ClassDB::bind_method(D_METHOD("get_path"), &ImportInfo::get_path);
	ClassDB::bind_method(D_METHOD("set_preferred_resource_path", "path"), &ImportInfo::set_preferred_resource_path);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "preferred_import_path"), "set_preferred_resource_path", "get_path");

	ClassDB::bind_method(D_METHOD("is_auto_converted"), &ImportInfo::is_auto_converted);
	ClassDB::bind_method(D_METHOD("is_import"), &ImportInfo::is_import);

	ClassDB::bind_method(D_METHOD("get_import_md_path"), &ImportInfo::get_import_md_path);
	ClassDB::bind_method(D_METHOD("set_import_md_path", "path"), &ImportInfo::set_import_md_path);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "import_md_path"), "set_import_md_path", "get_import_md_path");

	ClassDB::bind_method(D_METHOD("get_export_dest"), &ImportInfo::get_export_dest);
	ClassDB::bind_method(D_METHOD("set_export_dest", "path"), &ImportInfo::set_export_dest);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "export_dest"), "set_export_dest", "get_export_dest");

	ClassDB::bind_method(D_METHOD("get_export_lossless_copy"), &ImportInfo::get_export_lossless_copy);
	ClassDB::bind_method(D_METHOD("set_export_lossless_copy", "path"), &ImportInfo::set_export_lossless_copy);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "export_lossless_copy"), "set_export_lossless_copy", "get_export_lossless_copy");

	ClassDB::bind_method(D_METHOD("get_type"), &ImportInfo::get_type);
	ClassDB::bind_method(D_METHOD("set_type", "path"), &ImportInfo::set_type);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "type"), "set_type", "get_type");

	ClassDB::bind_method(D_METHOD("get_compat_type"), &ImportInfo::get_compat_type);

	ClassDB::bind_method(D_METHOD("get_importer"), &ImportInfo::get_importer);

	ClassDB::bind_method(D_METHOD("get_source_file"), &ImportInfo::get_source_file);
	ClassDB::bind_method(D_METHOD("set_source_file", "path"), &ImportInfo::set_source_file);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source_file"), "set_source_file", "get_source_file");

	ClassDB::bind_method(D_METHOD("get_source_md5"), &ImportInfo::get_source_md5);
	ClassDB::bind_method(D_METHOD("set_source_md5", "md5"), &ImportInfo::set_source_md5);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source_md5"), "set_source_md5", "get_source_md5");

	ClassDB::bind_method(D_METHOD("get_uid"), &ImportInfo::get_uid);

	ClassDB::bind_method(D_METHOD("get_dest_files"), &ImportInfo::get_dest_files);
	ClassDB::bind_method(D_METHOD("set_dest_files", "dest_files"), &ImportInfo::set_dest_files);
	ClassDB::bind_method(D_METHOD("has_dest_file", "dest_file"), &ImportInfo::has_dest_file);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "dest_files"), "set_dest_files", "get_dest_files");

	ClassDB::bind_method(D_METHOD("get_additional_sources"), &ImportInfo::get_additional_sources);
	ClassDB::bind_method(D_METHOD("set_additional_sources", "additional_sources"), &ImportInfo::set_additional_sources);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "additional_sources"), "set_additional_sources", "get_additional_sources");

	ClassDB::bind_method(D_METHOD("get_metadata_prop"), &ImportInfo::get_metadata_prop);
	ClassDB::bind_method(D_METHOD("set_metadata_prop", "metadata_prop"), &ImportInfo::set_metadata_prop);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "metadata_prop"), "set_metadata_prop", "get_metadata_prop");

	ClassDB::bind_method(D_METHOD("get_param", "key"), &ImportInfo::get_param);
	ClassDB::bind_method(D_METHOD("set_param", "key", "value"), &ImportInfo::set_param);
	ClassDB::bind_method(D_METHOD("has_param", "key"), &ImportInfo::has_param);

	ClassDB::bind_method(D_METHOD("get_iinfo_val", "p_section", "p_prop"), &ImportInfo::get_iinfo_val);
	ClassDB::bind_method(D_METHOD("set_iinfo_val", "p_section", "p_prop", "p_val"), &ImportInfo::set_iinfo_val);

	ClassDB::bind_method(D_METHOD("get_params"), &ImportInfo::get_params);
	ClassDB::bind_method(D_METHOD("set_params", "params"), &ImportInfo::set_params);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "params"), "set_params", "get_params");
	ClassDB::bind_method(D_METHOD("as_text", "full"), &ImportInfo::as_text, DEFVAL(true));

	ClassDB::bind_method(D_METHOD("save_to", "p_path"), &ImportInfo::save_to);

	BIND_ENUM_CONSTANT(UNKNOWN);
	BIND_ENUM_CONSTANT(LOSSLESS);
	BIND_ENUM_CONSTANT(STORED_LOSSY);
	BIND_ENUM_CONSTANT(IMPORTED_LOSSY);
	BIND_ENUM_CONSTANT(STORED_AND_IMPORTED_LOSSY);

	BIND_ENUM_CONSTANT(BASE);
	BIND_ENUM_CONSTANT(V2);
	BIND_ENUM_CONSTANT(MODERN);
	BIND_ENUM_CONSTANT(DUMMY);
	BIND_ENUM_CONSTANT(REMAP);
	BIND_ENUM_CONSTANT(GDEXT);
}

void ImportInfoModern::_bind_methods() {
}
void ImportInfov2::_bind_methods() {
}

Error ImportInfoGDExt::_load(const String &p_path) {
	Error err;
	cf = Ref<ConfigFile>(memnew(ConfigFile));
	err = cf->load(p_path);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not load resource " + p_path);
	return _load_after_cf(p_path);
}

Error ImportInfoGDExt::load_from_string(const String &p_fakepath, const String &p_string) {
	Error err;
	cf = Ref<ConfigFile>(memnew(ConfigFile));
	err = cf->parse(p_string);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not load resource " + p_fakepath);
	return _load_after_cf(p_fakepath);
}

Error ImportInfoGDExt::_load_after_cf(const String &p_path) {
	// compatibility_minimum
	import_md_path = GDRESettings::get_singleton()->localize_path(p_path);
	source_file = import_md_path;
	type = import_md_path.simplify_path().get_file().get_basename();

	not_an_import = true;

	if (p_path.get_extension().to_lower() == "gdnlib") {
		ver_major = 3;
		ver_minor = 0;
		importer = "gdnative";
	} else {
		String ver = get_compatibility_minimum();
		if (!ver.is_empty()) {
			Vector<String> spl = ver.split(".");
			if (spl.size() == 2) {
				ver_major = spl[0].to_int();
				ver_minor = spl[1].to_int();
			}
		} else {
			ver_major = 4;
			ver_minor = 0;
		}
	}
	preferred_import_path = import_md_path;
	dest_files = { import_md_path };
	// String platform = OS::get_singleton()->get_name().to_lower();
	// if (ver_major == 3) {
	// 	if (platform == "linux") {
	// 		platform = "X11";
	// 	} else if (platform == "macos") {
	// 		platform = "OSX";
	// 	} else if (platform == "windows") {
	// 		platform = "Windows";
	// 	}
	// }
	// auto libs = get_libaries();

	// for (int i = 0; i < libs.size(); i++) {
	// 	dest_files.push_back(libs[i].path);
	// 	if (libs[i].tags.has(platform)) {
	// 		preferred_import_path = libs[i].path;
	// 	}
	// }
	// auto deps = get_dependencies();
	// for (int i = 0; i < deps.size(); i++) {
	// 	dest_files.push_back(deps[i].path);
	// }
	return OK;
}

String ImportInfoGDExt::correct_path(const String &p_path) const {
	if (p_path.is_relative_path()) {
		return import_md_path.get_base_dir().path_join(p_path);
	}
	return p_path;
}

Vector<String> ImportInfoGDExt::normalize_tags(const Vector<String> &tags) {
	Vector<String> new_tags;
	for (int64_t i = 0; i < tags.size(); i++) {
		String tag = tags[i];
		if (tag == "64") {
			tag = "x86_64";
		} else if (tag == "32") {
			tag = "x86_32";
		} else if (tag == "Windows") {
			tag = "windows";
		} else if (tag == "Linux") {
			tag = "linux";
		} else if (tag == "OSX") {
			tag = "macos";
		} else if (tag == "HTML5") {
			tag = "web";
		}
		new_tags.push_back(tag);
	}
	return new_tags;
}

// virtual Dictionary get_libaries_section() const;
Vector<SharedObject> ImportInfoGDExt::get_dependencies(bool fix_rel_paths) const {
	Vector<SharedObject> deps;
	auto ret = get_grouped_dependencies(fix_rel_paths);
	for (auto &KV : ret) {
		deps.append_array(KV.value);
	}
	return deps;
}

HashMap<String, Vector<SharedObject>> ImportInfoGDExt::get_grouped_dependencies(bool fix_rel_paths) const {
	HashMap<String, Vector<SharedObject>> deps;
	if (cf->has_section("dependencies")) {
		Vector<String> dep_keys = cf->get_section_keys("dependencies");
		for (int64_t i = 0; i < dep_keys.size(); i++) {
			String key = dep_keys[i];
			auto var = cf->get_value("dependencies", key, Vector<String>{});
			Vector<String> deps_list;
			Vector<String> target_list;
			if (var.get_type() == Variant::PACKED_STRING_ARRAY) {
				deps_list = var;
			} else {
				if (var.get_type() == Variant::DICTIONARY) {
					Dictionary dict = var;
					for (int64_t j = 0; j < dict.size(); j++) {
						deps_list.push_back(dict.get_key_at_index(j));
						target_list.push_back(dict.get_value_at_index(j));
					}
				}
			}
			Vector<String> tags = normalize_tags(key.split("."));
			for (int64_t k = 0; k < deps_list.size(); k++) {
				SharedObject so;
				so.path = correct_path(deps_list[k]);
				so.path = fix_rel_paths ? correct_path(deps_list[k]) : deps_list[k];
				so.tags = tags;
				so.target = k < target_list.size() ? target_list[k] : "";
				if (!deps.has(key)) {
					deps.insert(key, Vector<SharedObject>());
				}
				deps[key].push_back(so);
			}
		}
	}
	return deps;
}

Vector<SharedObject> ImportInfoGDExt::get_libaries(bool fix_rel_paths) const {
	auto lib_map = get_libaries_section();
	Vector<SharedObject> libs;
	for (auto &E : lib_map) {
		SharedObject so;
		so.path = fix_rel_paths ? correct_path(E.value) : E.value;
		so.tags = normalize_tags(E.key.split("."));
		so.target = "";
		libs.push_back(so);
	}
	return libs;
}

HashMap<String, String> ImportInfoGDExt::get_libaries_section() const {
	/**
	a .gdextention file is a text file with the following format:
	```
	[configuration]
	entry_symbol = "godotsteam_init"
	compatibility_minimum = "4.1"

	[libraries]
	macos.debug = "osx/libgodotsteam.macos.template_debug.framework"
	macos.release = "osx/libgodotsteam.macos.template_release.framework"
	windows.debug.x86_64 = "win64/libgodotsteam.windows.template_debug.x86_64.dll"
	windows.debug.x86_32 = "win32/libgodotsteam.windows.template_debug.x86_32.dll"
	windows.release.x86_64 = "win64/libgodotsteam.windows.template_release.x86_64.dll"
	windows.release.x86_32 = "win32/libgodotsteam.windows.template_release.x86_32.dll"
	linux.debug.x86_64 = "linux64/libgodotsteam.linux.template_debug.x86_64.so"
	linux.debug.x86_32 = "linux32/libgodotsteam.linux.template_debug.x86_32.so"
	linux.release.x86_64 = "linux64/libgodotsteam.linux.template_release.x86_64.so"
	linux.release.x86_32 = "linux32/libgodotsteam.linux.template_release.x86_32.so"

	[dependencies]
	windows.x86_64 = { "win64/steam_api64.dll": "" }
	windows.x86_32 = { "win32/steam_api.dll": "" }
	linux.x86_64 = { "linux64/libsteam_api.so": "" }
	linux.x86_32 = { "linux32/libsteam_api.so": "" }
	```


	GDNative (.gdnlib) files go like this:
	```
	[general]

	singleton=false
	load_once=true
	symbol_prefix="godot_"
	reloadable=true

	[entry]

	X11.64="res://addons/godotsteam/x11/libgodotsteam.so"
	Windows.64="res://addons/godotsteam/win64/godotsteam.dll"
	OSX.64="res://addons/godotsteam/osx/libgodotsteam.dylib"

	[dependencies]

	X11.64=[ "res://addons/godotsteam/x11/libsteam_api.so" ]
	Windows.64=[ "res://addons/godotsteam/win64/steam_api64.dll" ]
	OSX.64=[ "res://addons/godotsteam/osx/libsteam_api.dylib" ]
	```
	 */
	HashMap<String, String> deps;
	String section_name = "libraries";
	if (importer == "gdnative") {
		section_name = "entry";
	}

	if (cf->has_section(section_name)) {
		Vector<String> dep_keys = cf->get_section_keys(section_name);
		for (int64_t i = 0; i < dep_keys.size(); i++) {
			deps[dep_keys[i]] = cf->get_value(section_name, dep_keys[i], String{});
		}
	}
	return deps;
}

String ImportInfoGDExt::get_compatibility_minimum() const {
	if (cf->has_section("configuration")) {
		if (cf->has_section_key("configuration", "compatibility_minimum")) {
			return cf->get_value("configuration", "compatibility_minimum", "");
		}
	}
	return {};
}

String ImportInfoGDExt::get_compatibility_maximum() const {
	if (cf->has_section("configuration")) {
		if (cf->has_section_key("configuration", "compatibility_maximum")) {
			return cf->get_value("configuration", "compatibility_maximum", "");
		}
	}
	return {};
}

// virtual Variant get_iinfo_val(const String &p_section, const String &p_prop) const override;
// virtual void set_iinfo_val(const String &p_section, const String &p_prop, const Variant &p_val) override;

Variant ImportInfoGDExt::get_iinfo_val(const String &p_section, const String &p_prop) const {
	if (cf->has_section(p_section)) {
		if (cf->has_section_key(p_section, p_prop)) {
			return cf->get_value(p_section, p_prop, "");
		}
	}
	return Variant();
}

void ImportInfoGDExt::set_iinfo_val(const String &p_section, const String &p_prop, const Variant &p_val) {
	cf->set_value(p_section, p_prop, p_val);
}

Dictionary ImportInfoGDExt::get_dependency_dict() const {
	Dictionary dict;
	if (cf->has_section("dependencies")) {
		Vector<String> dep_keys = cf->get_section_keys("dependencies");
		for (int64_t i = 0; i < dep_keys.size(); i++) {
			dict[dep_keys[i]] = cf->get_value("dependencies", dep_keys[i], Dictionary());
		}
	}
	return dict;
}

void ImportInfoGDExt::set_dependency_dict(const Dictionary &p_dict) {
	for (auto &KV : p_dict) {
		cf->set_value("dependencies", KV.key, KV.value);
	}
}

Error ImportInfoGDExt::save_to(const String &p_path) {
	Error err = gdre::ensure_dir(p_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err, err, "Failed to create directory for " + p_path);
	String content = encode_cfg_to_text(cf, ver_major, ver_minor, true);
	auto fa = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(fa.is_null(), ERR_FILE_CANT_OPEN, "Failed to open file " + p_path);
	fa->store_string(content);
	fa->flush();
	return OK;
}
