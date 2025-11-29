
#include "import_exporter.h"

#include "bytecode/bytecode_base.h"
#include "compat/oggstr_loader_compat.h"
#include "compat/resource_loader_compat.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"
#include "exporters/export_report.h"
#include "exporters/gdextension_exporter.h"
#include "exporters/gdscript_exporter.h"
#include "exporters/resource_exporter.h"
#include "exporters/scene_exporter.h"
#include "exporters/translation_exporter.h"
#include "gdre_logger.h"
#include "utility/common.h"
#include "utility/gdre_config.h"
#include "utility/gdre_settings.h"
#include "utility/gdre_version.gen.h"
#include "utility/glob.h"
#include "utility/godot_mono_decomp_wrapper.h"

#include "godot_mono_decomp_wrapper.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "thirdparty/minimp3/minimp3_ex.h"
#include "utility/import_info.h"

#include <compat/script_loader.h>

using namespace gdre;

GDRESettings *get_settings() {
	return GDRESettings::get_singleton();
}

int get_ver_major() {
	return get_settings()->get_ver_major();
}
int get_ver_minor() {
	return get_settings()->get_ver_minor();
}
int get_ver_rev() {
	return get_settings()->get_ver_rev();
}

Ref<ImportExporterReport> ImportExporter::get_report() {
	return report;
}

namespace {
static FileNoCaseComparator file_no_case_comparator;
}
struct FileInfoComparator {
	bool operator()(const std::shared_ptr<ImportExporter::FileInfo> &a, const std::shared_ptr<ImportExporter::FileInfo> &b) const {
		String a_base_dir = a->file.get_base_dir();
		String b_base_dir = b->file.get_base_dir();
		if (a_base_dir != b_base_dir) {
			// subdirectories come last
			if (a_base_dir.begins_with(b_base_dir)) {
				return false;
			} else if (b_base_dir.begins_with(a_base_dir)) {
				return true;
			}
		}
		return file_no_case_comparator(a->file, b->file);
	}
};

// Error remove_remap(const String &src, const String &dst, const String &output_dir);
Error ImportExporter::handle_auto_converted_file(const String &autoconverted_file) {
	String prefix = autoconverted_file.replace_first("res://", "");
	if (!prefix.begins_with(".")) {
		String old_path = output_dir.path_join(prefix);
		if (FileAccess::exists(old_path)) {
			if (CONFIG_GET("delete_auto_converted_files", false)) {
				return gdre::rimraf(old_path);
			}
			String new_path = output_dir.path_join(".autoconverted").path_join(prefix);
			Error err = gdre::ensure_dir(new_path.get_base_dir());
			ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to create directory for remap " + new_path);
			Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
			ERR_FAIL_COND_V_MSG(da.is_null(), ERR_CANT_CREATE, "Failed to create directory for remap " + new_path);
			return da->rename(old_path, new_path);
		}
	}
	return OK;
}

Error ImportExporter::remove_remap_and_autoconverted(const String &source_file, const String &autoconverted_file) {
	Error err = get_settings()->remove_remap(source_file, autoconverted_file, output_dir);
	if (err != ERR_FILE_NOT_FOUND) {
		ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to remove remap for " + source_file + " -> " + autoconverted_file);
	}
	return handle_auto_converted_file(autoconverted_file);
}

void ImportExporter::save_filesystem_cache(const Vector<std::shared_ptr<FileInfo>> &reports, String p_output_dir) {
	if (get_ver_major() <= 3) {
		return;
	}
	static HashMap<int, String> minor_to_default_md5_hash = {
		{ 0, "fc8a56933c4b1c8d796fdb8f7a9f9475" },
		{ 1, "ea4bc82a6ad023ab7ee23ee620429895" },
		{ 2, "ea4bc82a6ad023ab7ee23ee620429895" },
		{ 3, "ea4bc82a6ad023ab7ee23ee620429895" },
		{ 4, "ea4bc82a6ad023ab7ee23ee620429895" },
		{ 5, "63f7b34db8d8cdea90c76aacccf841ec" },
	};

	String cache_path = get_ver_minor() < 4 ? "filesystem_cache8" : "filesystem_cache10";
	String editor_dir = p_output_dir.path_join(".godot").path_join("editor");
	gdre::ensure_dir(editor_dir);
	String cache_file = editor_dir.path_join(cache_path);
	Ref<FileAccess> p_file = FileAccess::open(cache_file, FileAccess::WRITE);
	//write an md5 hash of all 0s
	String default_md5_hash = minor_to_default_md5_hash.has(get_ver_minor()) ? minor_to_default_md5_hash[get_ver_minor()] : "00000000000000000000000000000000";
	p_file->store_line(default_md5_hash);
	String current_dir = "";
	const int64_t curr_time = OS::get_singleton()->get_unix_time();
	auto get_dir_modified_time = [&](const String &dir) {
		int64_t time = FileAccess::get_modified_time(p_output_dir.path_join(dir.trim_prefix("res://")));
		if (time <= 0) {
			return curr_time;
		}
		return time;
	};
	const bool is_v4_4_or_newer = get_ver_major() > 4 || (get_ver_major() == 4 && get_ver_minor() >= 4);
	for (int i = 0; i < reports.size(); i++) {
		auto &file_info = *reports[i];
		if (!file_info.verified) {
			continue;
		}
		const String &source_file = file_info.file;
		String base_dir = source_file.get_base_dir();
		if (base_dir != "res://") {
			base_dir += "/";
		}
		if (base_dir != current_dir) {
			if (current_dir.is_empty()) {
				p_file->store_line("::res://::" + String::num_int64(get_dir_modified_time("res://")));
			}
			if (base_dir != "res://") {
				String curr = "res://";
				Vector<String> parts = base_dir.trim_prefix("res://").trim_suffix("/").split("/");
				for (int i = 0; i < parts.size(); i++) {
					const String &part = parts[i];
					if (part.is_empty()) {
						continue;
					}
					curr = curr.path_join(part);
					if (current_dir.begins_with(curr)) {
						continue;
					}
					p_file->store_line("::" + curr + "/" + "::" + String::num_uint64(get_dir_modified_time(curr)));
				}
			}
			current_dir = base_dir;
		}

		String type = file_info.type;
		if (!file_info.resource_script_class.is_empty()) {
			type += "/" + String(file_info.resource_script_class);
		}
		Vector<String> class_info_parts = {
			file_info.class_info.name,
			file_info.class_info.extends,
			file_info.class_info.icon_path,
		};
		if (is_v4_4_or_newer) {
			class_info_parts.append_array({ itos(file_info.class_info.is_abstract),
					itos(file_info.class_info.is_tool),
					file_info.import_md5, String("<*>").join(file_info.import_dest_paths) });
		}

		PackedStringArray cache_string;
		cache_string.append(source_file.get_file());
		cache_string.append(type);
		cache_string.append(itos(file_info.uid));
		cache_string.append(itos(file_info.modified_time));
		cache_string.append(itos(file_info.import_modified_time));
		cache_string.append(itos(file_info.import_valid ? 1 : 0));
		cache_string.append(file_info.import_group_file);
		cache_string.append(String("<>").join(class_info_parts));
		cache_string.append(String("<>").join(file_info.deps));

		p_file->store_line(String("::").join(cache_string));
	}
}

Vector<std::shared_ptr<ImportExporter::FileInfo>> ImportExporter::read_filesystem_cache(const String &p_path) {
	Vector<std::shared_ptr<FileInfo>> result;

	Ref<FileAccess> p_file = FileAccess::open(p_path, FileAccess::READ);
	if (p_file.is_null()) {
		return result;
	}

	// Skip the first line (MD5 hash)
	if (!p_file->eof_reached()) {
		(void)p_file->get_line();
	}

	String current_dir = "";

	while (!p_file->eof_reached()) {
		String line = p_file->get_line().strip_edges();
		if (line.is_empty()) {
			continue;
		}

		Vector<String> parts = line.split("::", true, 8);
		if (parts.size() < 2) {
			continue;
		}

		// Check if this is a directory entry (format: ::<path>::<timestamp>)
		if (parts.size() == 3 && parts[0].is_empty() && parts[2].is_valid_int()) {
			String dir_path = parts[1];
			if (dir_path.ends_with("/")) {
				current_dir = dir_path;
			} else {
				current_dir = dir_path + "/";
			}
			continue;
		}

		// This is a file entry (format: <filename>::<type>::<uid>::<modified_time>::<import_modified_time>::<1>::<>::<class_info>::<deps>)
		if (parts.size() >= 8 && !parts[0].is_empty()) {
			auto file_info = std::make_shared<FileInfo>();

			// Reconstruct full path
			String filename = parts[0];
			if (current_dir.is_empty()) {
				file_info->file = "res://" + filename;
			} else {
				file_info->file = current_dir + filename;
			}

			// Parse type (may include "/resource_script_class")
			String type_str = parts[1];
			int type_slash = type_str.find("/");
			if (type_slash >= 0) {
				file_info->type = type_str.substr(0, type_slash);
				file_info->resource_script_class = type_str.substr(type_slash + 1);
			} else {
				file_info->type = type_str;
			}

			// Parse uid
			file_info->uid = parts[2].to_int();

			// Parse modified_time
			file_info->modified_time = parts[3].to_int();

			// Parse import_modified_time
			file_info->import_modified_time = parts[4].to_int();

			// Skip parts[6] (TODO field) and parts[7] (empty string)
			file_info->import_valid = parts[5].to_int() != 0;
			file_info->import_group_file = parts[6];
			// Parse class_info
			const String &class_info_str = parts[7];
			Vector<String> class_info_parts = class_info_str.split("<>", true);
			if (class_info_parts.size() >= 3) {
				file_info->class_info.name = class_info_parts[0];
				file_info->class_info.extends = class_info_parts[1];
				file_info->class_info.icon_path = class_info_parts[2];

				if (class_info_parts.size() >= 5) {
					file_info->class_info.is_abstract = class_info_parts[3].to_int() != 0;
					file_info->class_info.is_tool = class_info_parts[4].to_int() != 0;
					if (class_info_parts.size() >= 6) {
						file_info->import_md5 = class_info_parts[5];
						if (class_info_parts.size() >= 7 && !class_info_parts[6].is_empty()) {
							file_info->import_dest_paths = class_info_parts[6].split("<*>", true);
						}
					}
				}
			}

			// Parse deps
			if (parts.size() >= 9) {
				const String &deps_str = parts[8];
				if (!deps_str.is_empty()) {
					file_info->deps = deps_str.split("<>", false);
				}
			}

			file_info->verified = true;
			file_info->import_valid = true;

			result.push_back(file_info);
		}
	}

	return result;
}

void ImportExporter::_do_file_info(uint32_t i, std::shared_ptr<FileInfo> *file_infos) {
	auto &file_info = *file_infos[i];
	String ext = file_info.file.get_extension().to_lower();
	auto export_report = src_to_report.has(file_info.file) ? src_to_report.get(file_info.file) : Ref<ExportReport>();
	if (export_report.is_valid() && export_report->modified_time > 0) {
		auto iinfo = export_report->get_import_info();
		bool is_import = iinfo.is_valid() ? iinfo->is_import() : false;
		file_info.type = export_report->actual_type;
		file_info.resource_script_class = export_report->script_class;
		file_info.uid = is_import ? ResourceUID::get_singleton()->text_to_id(iinfo->get_uid()) : GDRESettings::get_singleton()->get_uid_for_path(file_info.file);
		file_info.modified_time = export_report->modified_time;
		file_info.import_modified_time = export_report->import_modified_time;
		file_info.import_md5 = export_report->import_md5;
		file_info.import_dest_paths = is_import ? iinfo->get_dest_files() : Vector<String>();
		file_info.import_valid = true;
		auto group_val = is_import ? iinfo->get_iinfo_val("remap", "group_file") : Variant();
		if (group_val.get_type() == Variant::STRING) {
			file_info.import_group_file = group_val;
		}
		file_info.deps = export_report->dependencies;
		file_info.verified = true;
	} else if (valid_extensions.has(ext)) {
		String path = output_dir.path_join(file_info.file.trim_prefix("res://"));
		file_info.modified_time = FileAccess::get_modified_time(path);
		if (FileAccess::exists(path + ".import")) {
			file_info.import_modified_time = FileAccess::get_modified_time(path + ".import");
			file_info.import_md5 = FileAccess::get_md5(path + ".import");
			Ref<ConfigFile> cf = Ref<ConfigFile>(memnew(ConfigFile));
			Error err = cf->load(path + ".import");
			if (err == OK) {
				if (cf->has_section_key("remap", "dest_files")) {
					file_info.import_dest_paths = cf->get_value("remap", "dest_files", Vector<String>());
				}
				if (cf->has_section_key("remap", "group_file")) {
					file_info.import_group_file = cf->get_value("remap", "group_file", "");
				}
				if (cf->has_section_key("remap", "uid")) {
					file_info.uid = ResourceUID::get_singleton()->text_to_id(cf->get_value("remap", "uid", ""));
				}
			}
		}
		if (file_info.uid == ResourceUID::INVALID_ID) {
			file_info.uid = GDRESettings::get_singleton()->get_uid_for_path(file_info.file);
		}
		file_info.type = ResourceCompatLoader::get_resource_type(path);
		file_info.resource_script_class = ResourceCompatLoader::get_resource_script_class(path);
		if (file_info.type == "" && textfile_extensions.has(ext)) {
			file_info.type = "TextFile";
		}
		if (file_info.type == "" && other_file_extensions.has(ext)) {
			file_info.type = "OtherFile";
		}
		List<String> deps;
		ResourceCompatLoader::get_dependencies(path, &deps, true);
		for (auto &dep : deps) {
			file_info.deps.push_back(dep);
		}
		file_info.import_valid = true;
		file_info.verified = true;
	} else {
		file_info.verified = false;
		return;
	}
	if (ext == "gd") {
		auto script_entry = GDRESettings::get_singleton()->get_cached_script_entry(file_info.file);
		file_info.class_info.name = script_entry.get("class", "");
		if (file_info.class_info.name.is_resource_file()) {
			file_info.class_info.name.clear();
		}
		file_info.class_info.extends = script_entry.get("base", "");
		file_info.class_info.icon_path = script_entry.get("icon", "");
		file_info.class_info.is_abstract = script_entry.get("is_abstract", false);
		file_info.class_info.is_tool = script_entry.get("is_tool", false);
	} else if (ext == "cs" && GDRESettings::get_singleton()->has_loaded_dotnet_assembly()) {
		GDRELogger::set_thread_local_silent_errors(true);
		Ref<FakeScript> script = ResourceCompatLoader::custom_load(file_info.file, "", ResourceInfo::LoadType::NON_GLOBAL_LOAD, nullptr, false, ResourceFormatLoader::CACHE_MODE_IGNORE);
		GDRELogger::set_thread_local_silent_errors(false);
		if (script.is_valid()) {
			file_info.class_info.name = script->is_global_class() ? (String)script->get_global_name() : "";
			file_info.class_info.extends = script->get_direct_base_type();
			file_info.class_info.icon_path = script->get_icon_path();
			file_info.class_info.is_abstract = script->is_abstract();
			file_info.class_info.is_tool = script->is_tool();
		}
	}
}

String ImportExporter::get_file_info_description(uint32_t i, std::shared_ptr<FileInfo> *file_info) {
	return file_info[i]->file;
}

HashSet<String> get_base_extensions_unique_to_nonv4() {
	HashSet<String> v2exts;
	HashSet<String> v3exts;
	HashSet<String> v4exts;
	HashSet<String> v2onlyexts;
	HashSet<String> v3onlyexts;
	HashSet<String> v4onlyexts;

	auto get_exts_func([&](HashSet<String> &ext, HashSet<String> &ext2, int ver_major) {
		List<String> exts;
		ResourceCompatLoader::get_base_extensions(&exts, ver_major);
		for (const String &extf : exts) {
			ext.insert(extf);
			ext2.insert(extf);
		}
	});
	get_exts_func(v2onlyexts, v2exts, 2);
	get_exts_func(v3onlyexts, v3exts, 3);
	get_exts_func(v4onlyexts, v4exts, 4);

	for (const String &ext : v2exts) {
		if (v4exts.has(ext) || v3exts.has(ext)) {
			v4onlyexts.erase(ext);
			v3onlyexts.erase(ext);
			v2onlyexts.erase(ext);
		}
	}

	for (const String &ext : v3exts) {
		if (v4exts.has(ext)) {
			v4onlyexts.erase(ext);
			v3onlyexts.erase(ext);
		}
	}

	add_all(v2onlyexts, v3onlyexts);

	return v2onlyexts;
}

void ImportExporter::update_exts() {
	valid_extensions.clear();
	textfile_extensions.clear();
	other_file_extensions.clear();

	valid_extensions.insert("cs");

	List<String> extensionsl;
	ResourceLoader::get_recognized_extensions_for_type("", &extensionsl);
	HashSet<String> to_remove = get_base_extensions_unique_to_nonv4();

	for (const String &E : extensionsl) {
		if (!to_remove.has(E)) {
			valid_extensions.insert(E);
		}
	}

	const Vector<String> textfile_ext = (get_settings()->get_project_setting("docks/filesystem/textfile_extensions", "txt,md,cfg,ini,log,json,yml,yaml,toml,xml").operator String().split(",", false));
	for (const String &E : textfile_ext) {
		if (valid_extensions.has(E)) {
			continue;
		}
		valid_extensions.insert(E);
		textfile_extensions.insert(E);
	}
	const Vector<String> other_file_ext = (get_settings()->get_project_setting("docks/filesystem/other_file_extensions", "ico,icns").operator String().split(",", false));
	for (const String &E : other_file_ext) {
		if (valid_extensions.has(E)) {
			continue;
		}
		valid_extensions.insert(E);
		other_file_extensions.insert(E);
	}
}

void ImportExporter::rewrite_metadata(ExportToken &token) {
	auto &token_report = token.report;
	ERR_FAIL_COND_MSG(token_report.is_null(), "Cannot rewrite metadata for null report");
	Error err = token_report->get_error();
	auto iinfo = token_report->get_import_info();
	auto if_err_func = [&]() {
		if (err != OK) {
			token_report->set_rewrote_metadata(ExportReport::FAILED);
		} else {
			token_report->set_rewrote_metadata(ExportReport::REWRITTEN);
		}
	};
	String new_md_path = output_dir.path_join(iinfo->get_import_md_path().replace("res://", ""));

	if (token_report->get_rewrote_metadata() == ExportReport::NOT_IMPORTABLE) {
		return;
	}
	if (!iinfo->is_import()) {
		if (iinfo->get_ver_major() >= 4) {
			List<String> deps;
			auto path = iinfo->get_path();
			if (ResourceCompatLoader::handles_resource(path)) {
				auto res_info = ResourceCompatLoader::get_resource_info(path);
				token_report->actual_type = res_info.is_valid() ? res_info->type : iinfo->get_type();
				token_report->script_class = res_info.is_valid() ? res_info->script_class : "";
			} else {
				token_report->actual_type = iinfo->get_type();
			}
			ResourceCompatLoader::get_dependencies(path, &deps, false);
			for (auto &dep : deps) {
				token_report->dependencies.push_back(dep);
			}
			token_report->import_md5 = "";
			token_report->import_modified_time = 0;
			token_report->modified_time = FileAccess::get_modified_time(token_report->get_saved_path());
		}
		return;
	}

	if (err != OK) {
		if ((err == ERR_UNAVAILABLE || err == ERR_PRINTER_ON_FIRE) && iinfo->get_ver_major() >= 4 && iinfo->is_dirty()) {
			iinfo->save_to(new_md_path);
			if_err_func();
		}
		return;
	}
	// ****REWRITE METADATA****
	bool not_in_res_tree = !iinfo->get_source_file().begins_with("res://");
	bool export_matches_source = token_report->get_source_path() == token_report->get_new_source_path();
	if (err == OK && (not_in_res_tree || !export_matches_source)) {
		if (iinfo->get_ver_major() <= 2) {
			// TODO: handle v2 imports with more than one source, like atlas textures
			err = rewrite_import_source(token_report->get_new_source_path(), iinfo);
			if_err_func();
		} else if (not_in_res_tree && iinfo->get_ver_major() >= 3 && (iinfo->get_source_file().find(token_report->get_new_source_path().replace("res://", "")) != -1)) {
			// Currently, we only rewrite the import data for v3 if the source file was somehow recorded as an absolute file path,
			// But is still in the project structure
			err = rewrite_import_source(token_report->get_new_source_path(), iinfo);
			if_err_func();
		} else if (iinfo->is_dirty()) {
			err = iinfo->save_to(new_md_path);
			if (err != OK) {
				token_report->set_rewrote_metadata(ExportReport::FAILED);
			} else if (!export_matches_source) {
				token_report->set_rewrote_metadata(ExportReport::NOT_IMPORTABLE);
			}
		}
	} else if (iinfo->is_dirty()) {
		if (err == OK) {
			err = iinfo->save_to(new_md_path);
			if_err_func();
		} else {
			token_report->set_rewrote_metadata(ExportReport::NOT_IMPORTABLE);
		}
	} else {
		token_report->set_rewrote_metadata(ExportReport::NOT_DIRTY);
	}
	auto mdat = token_report->get_rewrote_metadata();
	if (mdat == ExportReport::FAILED || mdat == ExportReport::NOT_IMPORTABLE) {
		return;
	}
	if (err == OK && iinfo->get_ver_major() > 2 && iinfo->get_iitype() == ImportInfo::MODERN) {
		err = ERR_LINK_FAILED;
		Ref<ImportInfoModern> modern_iinfo = iinfo;
		if (modern_iinfo.is_valid()) {
			err = modern_iinfo->save_md5_file(output_dir);
		}
		if (err) {
			token_report->set_rewrote_metadata(ExportReport::MD5_FAILED);
		}
	}
	if (!err && iinfo->get_ver_major() >= 4 && export_matches_source && token_report->get_rewrote_metadata() != ExportReport::NOT_IMPORTABLE) {
		String resource_script_class;
		List<String> deps;
		auto path = iinfo->get_path();
		if (ResourceCompatLoader::handles_resource(path)) {
			auto res_info = ResourceCompatLoader::get_resource_info(path);
			token_report->actual_type = res_info.is_valid() ? res_info->type : iinfo->get_type();
			token_report->script_class = res_info.is_valid() ? res_info->script_class : "";
		} else {
			token_report->actual_type = iinfo->get_type();
		}
		ResourceCompatLoader::get_dependencies(path, &deps, false);
		for (auto &dep : deps) {
			token_report->dependencies.push_back(dep);
		}
		token_report->import_md5 = FileAccess::get_md5(new_md_path);
		token_report->import_modified_time = FileAccess::get_modified_time(new_md_path);
		token_report->modified_time = FileAccess::get_modified_time(token_report->get_saved_path());
	}
	if (!err && iinfo->get_ver_major() >= 4 && iinfo->get_metadata_prop().get("has_editor_variant", false)) {
		// we need to make a copy of the resource with the editor variant
		String editor_variant_path = iinfo->get_path();
		if (FileAccess::exists(editor_variant_path)) {
			String ext = editor_variant_path.get_extension();
			editor_variant_path = editor_variant_path.trim_suffix("." + ext) + ".editor." + ext;
			String output_path = output_dir.path_join(editor_variant_path.trim_prefix("res://"));
			String output_md_path = output_path.trim_suffix(output_path.get_extension()) + "meta";
			if (!FileAccess::exists(output_path)) {
				gdre::ensure_dir(output_path.get_base_dir());
				Vector<uint8_t> buf = FileAccess::get_file_as_bytes(iinfo->get_path());
				Ref<FileAccess> f = FileAccess::open(output_path, FileAccess::WRITE);
				if (!f.is_null()) {
					f->store_buffer(buf);
				}
			}
			if (!FileAccess::exists(output_md_path)) {
				gdre::ensure_dir(output_md_path.get_base_dir());
				Ref<FileAccess> f = FileAccess::open(output_md_path, FileAccess::WRITE);
				if (!f.is_null()) {
					// empty dictionary
					store_var_compat(f, Dictionary(), iinfo->get_ver_major());
				}
			}
		}
	}
}

Error ImportExporter::unzip_and_copy_addon(const Ref<ImportInfoGDExt> &iinfo, const String &zip_path) {
	//append a random string
	String output = output_dir;
	String parent_tmp_dir = output_dir.path_join(".tmp").path_join(String::num_uint64(OS::get_singleton()->get_unix_time() + rand()));

	String tmp_dir = parent_tmp_dir;
	ERR_FAIL_COND_V_MSG(gdre::unzip_file_to_dir(zip_path, tmp_dir) != OK, ERR_FILE_CANT_WRITE, "Failed to unzip plugin zip to " + tmp_dir);
	// copy the files to the output_dir
	auto rel_gdext_path = iinfo->get_import_md_path().replace_first("res://", "");
	Vector<String> addons = Glob::rglob(tmp_dir.path_join("**").path_join(rel_gdext_path.get_file()), true);

	if (addons.size() > 0) {
		// check if the addons directory exists
		auto th = addons[0].simplify_path();
		if (th.contains(rel_gdext_path)) {
			// if it contains "addons/", we want to only copy that directory, because the mod may contain other files (demos, samples, etc.) that we don't want to copy
			if (rel_gdext_path.begins_with("addons/")) {
				rel_gdext_path = rel_gdext_path.trim_prefix("addons/");
				output = output_dir.path_join("addons");
			}
			auto idx = th.find(rel_gdext_path);
			auto subpath = th.substr(0, idx);
			tmp_dir = th.substr(0, idx);
		} else {
			// what we are going to do is pop off the left-side parts of the rel_gdext_path until we find something that matches
			String prefix = "";
			String suffix = rel_gdext_path;
			auto parts = rel_gdext_path.split("/");
			for (int i = 0; i < parts.size(); i++) {
				prefix = prefix.path_join(parts[i]);
				suffix = suffix.trim_prefix(parts[i] + "/");
				if (th.contains(suffix)) {
					break;
				}
			}
			tmp_dir = th.substr(0, th.find(suffix));
			output = output_dir.path_join(prefix);
		}
		if (addons.size() > 1) {
			WARN_PRINT("Found multiple addons directories in addon zip, using the first one.");
		}
	} else {
		ERR_FAIL_COND_V_MSG(addons.size() == 0, ERR_FILE_NOT_FOUND, "Failed to find our addon file in " + zip_path);
	}
	auto da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	auto plugin_files = gdre::get_recursive_dir_list(tmp_dir, {}, false);
	auto existing_files = gdre::vector_to_hashset(gdre::get_recursive_dir_list(output, {}, false));
	auto decomp = GDScriptDecomp::create_decomp_for_commit(GDRESettings::get_singleton()->get_bytecode_revision());
	for (auto &file : plugin_files) {
		String plugin_path = tmp_dir.path_join(file);
		String existing_path = output.path_join(file);
		if (existing_files.has(file)) {
			String ext = file.get_extension().to_lower();
			if (ext == "cs" || ext == "uid") {
				// not overwriting code or uid files because the project may have changed them in an incompatible way
				continue;
			} else if (ext == "gd") {
				// get the text minus whitespace at beginning and end
				String plugin_text = FileAccess::get_file_as_string(plugin_path).strip_edges();
				String existing_text = FileAccess::get_file_as_string(existing_path).strip_edges();
				if (plugin_text.is_empty()) {
					continue;
				} else if (existing_text.is_empty() || decomp.is_null()) {
					// do nothing, we'll copy it below
				} else if (plugin_text != existing_text) {
					// compile the code string to check if they're the same (ignoring whitespace and comments)
					// If not, don't overwrite the existing file
					Vector<uint8_t> plugin_bytes = decomp->compile_code_string(plugin_text);
					Vector<uint8_t> existing_bytes = decomp->compile_code_string(existing_text);
					if (decomp->test_bytecode_match(plugin_bytes, existing_bytes, true, false) != OK) {
#if DEBUG_ENABLED
						print_line(vformat("\n\n***** Different code for %s *****\n", file));
						// print_line(decomp->get_error_message());
#endif
						continue;
					}
				}
			}
		}
		gdre::ensure_dir(existing_path.get_base_dir());
		ERR_CONTINUE(DirAccess::copy_absolute(tmp_dir.path_join(file), output.path_join(file)) != OK);
	}
	gdre::rimraf(parent_tmp_dir);
	da->remove(zip_path);
	return OK;
}

void ImportExporter::_do_export(uint32_t i, ExportToken *tokens) {
	// Taken care of in the main thread
	auto &token = tokens[i];
	token.report = ResourceExporter::_check_for_existing_resources(token.iinfo);
	if (token.report.is_valid()) {
		return;
	}

	tokens[i].report = Exporter::export_resource(output_dir, tokens[i].iinfo);
	rewrite_metadata(tokens[i]);
	if (tokens[i].supports_multithread) {
		tokens[i].report->append_error_messages(GDRELogger::get_thread_errors());
	} else {
		tokens[i].report->append_error_messages(GDRELogger::get_errors());
	}
}

String ImportExporter::get_export_token_description(uint32_t i, ExportToken *tokens) {
	return tokens[i].iinfo.is_valid() ? tokens[i].iinfo->get_path() : "";
}

// TODO: rethink this, it's not really recovering any keys beyond the first time
Error ImportExporter::_reexport_translations(Vector<ImportExporter::ExportToken> &non_multithreaded_tokens, size_t token_size, Ref<EditorProgressGDDC> pr) {
	Vector<size_t> incomp_trans;
	bool found_keys = false;
	for (int i = 0; i < non_multithreaded_tokens.size(); i++) {
		if (non_multithreaded_tokens[i].iinfo->get_importer() == "csv_translation") {
			Dictionary extra_info = non_multithreaded_tokens[i].report->get_extra_info();
			int missing_keys = extra_info.get("missing_keys", 0);
			int total_keys = extra_info.get("total_keys", 0);
			if (missing_keys < total_keys) {
				found_keys = true;
			}
			if (non_multithreaded_tokens[i].iinfo->get_export_dest().contains("res://.assets")) {
				incomp_trans.push_back(i);
			}
		}
	}
	// order from largest to smallest
	incomp_trans.sort();
	incomp_trans.reverse();
	Vector<ExportToken> incomplete_translation_tokens;
	Error err = OK;
	if (incomp_trans.size() > 2 && found_keys) {
		for (auto idx : incomp_trans) {
			auto &token = non_multithreaded_tokens[idx];
			token.iinfo->set_export_dest(token.iinfo->get_export_dest().replace("res://.assets", "res://"));
			incomplete_translation_tokens.insert(0, token);
			non_multithreaded_tokens.remove_at(idx);
		}
		size_t start = token_size + non_multithreaded_tokens.size() - incomplete_translation_tokens.size() - 1;
		print_line("Re-exporting translations...");
		pr->step("Re-exporting translations...", start, true);
		err = TaskManager::get_singleton()->run_group_task_on_current_thread(
				this,
				&ImportExporter::_do_export,
				incomplete_translation_tokens.ptrw(),
				incomplete_translation_tokens.size(),
				&ImportExporter::get_export_token_description,
				"ImportExporter::export_imports",
				"Exporting resources...",
				true, pr, start);
		non_multithreaded_tokens.append_array(incomplete_translation_tokens);
	}
	return err;
}

void ImportExporter::recreate_uid_file(const String &src_path, bool is_import, const HashSet<String> &files_to_export_set) {
	auto uid = GDRESettings::get_singleton()->get_uid_for_path(src_path);
	if (uid != ResourceUID::INVALID_ID) {
		String output_file = output_dir.path_join(src_path.trim_prefix("res://"));
		String uid_path = output_file + ".uid";
		if ((is_import || files_to_export_set.has(src_path)) && FileAccess::exists(output_file)) {
			Ref<FileAccess> f = FileAccess::open(uid_path, FileAccess::WRITE);
			if (f.is_valid()) {
				f->store_string(ResourceUID::get_singleton()->id_to_text(uid) + "\n");
			}
		}
	}
}

struct ProcessRunnerStruct : public TaskRunnerStruct {
	String command;
	Vector<String> arguments;
	int error_code = -1;
	OS::ProcessID process_id = -1;
	String description;
	bool is_cancelled = false;
	Ref<FileAccess> fa_stdout;
	Ref<FileAccess> fa_stderr;
	String output;

	ProcessRunnerStruct() {
	}

	ProcessRunnerStruct(const String &p_command, const Vector<String> &p_arguments) :
			command(p_command), arguments(p_arguments) {
		description = "Running " + command + " " + String(" ").join(arguments);
	}

	void set_command(const String &p_command, const Vector<String> &p_arguments) {
		command = p_command;
		arguments = p_arguments;
		description = "Running " + command + " " + String(" ").join(arguments);
	}

	virtual int get_current_task_step_value() override {
		return 0;
	}

	virtual String get_current_task_step_description() override {
		return description;
	}

	virtual void cancel() override {
		if (process_id != -1) {
			OS::get_singleton()->kill(process_id);
		}
		is_cancelled = true;
	}

	// We have to start the process on the main thread because of inscrutable WINPROC stuff.
	bool pre_run() {
		List<String> args;
		for (const String &arg : arguments) {
			args.push_back(arg);
		}
		Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(command, args, false);
		if (pipe_info.is_empty()) {
			error_code = -2;
			after_run();
			return false;
		}
		fa_stdout = pipe_info["stdio"];
		fa_stderr = pipe_info["stderr"];
		process_id = pipe_info["pid"];
		return true;
	}

	virtual void run(void *p_userdata) override {
		if (process_id == -1) {
			return;
		}

		while (OS::get_singleton()->is_process_running(process_id) && !is_cancelled) {
			// we have to do continually read from the pipes or the process will hang
			output += fa_stdout->get_as_text() + fa_stderr->get_as_text();
			OS::get_singleton()->delay_usec(10000);
		}
		error_code = OS::get_singleton()->get_process_exit_code(process_id);
		process_id = -1;
		after_run();
	}

	virtual bool auto_close_progress_bar() override {
		return true;
	}

	void after_run() {
		if (error_code != 0) {
			if (error_code == -2) {
				ERR_PRINT("Failed to run process " + command + " (error code: " + String::num_int64(error_code) + "):\n" + "execute_with_pipe failed");
			} else {
				ERR_PRINT("Failed to run process " + command + " (error code: " + String::num_int64(error_code) + "):\n" + output);
			}
		} else {
			print_line(vformat("Successfully ran %s %s", command, String(" ").join(arguments)));
		}
	}

	virtual ~ProcessRunnerStruct() {
		if (fa_stdout.is_valid()) {
			fa_stdout->close();
		}
		if (fa_stderr.is_valid()) {
			fa_stderr->close();
		}
	}
};

// export all the imported resources
Error ImportExporter::export_imports(const String &p_out_dir, const Vector<String> &_files_to_export) {
	ERR_FAIL_COND_V_MSG(p_out_dir.is_empty(), ERR_INVALID_PARAMETER, "Output directory is empty!");
	reset_log();
	report = Ref<ImportExporterReport>(memnew(ImportExporterReport(get_settings()->get_version_string(), get_settings()->get_game_name())));
	report->log_file_location = get_settings()->get_log_file_path();
	ERR_FAIL_COND_V_MSG(!get_settings()->is_pack_loaded(), ERR_DOES_NOT_EXIST, "pack/dir not loaded!");
	output_dir = gdre::get_full_path(p_out_dir, DirAccess::ACCESS_FILESYSTEM);
	report->output_dir = output_dir;
	ERR_FAIL_COND_V_MSG(gdre::ensure_dir(output_dir) != OK, ERR_FILE_CANT_WRITE, "Failed to create output directory " + output_dir);
	Error err = OK;
	// TODO: make this use "copy"
	Array _files = get_settings()->get_import_files();
	if (_files.size() == 0) {
		WARN_PRINT("No import files found!");
		return OK;
	}

	ResourceCompatLoader::make_globally_available();
	ResourceCompatLoader::set_default_gltf_load(false);

	bool partial_export = (_files_to_export.size() > 0 && _files_to_export.size() != get_settings()->get_file_info_list({}).size());
	size_t export_files_count = partial_export ? _files_to_export.size() : _files.size();
	const HashSet<String> files_to_export_set = vector_to_hashset(partial_export ? _files_to_export : get_settings()->get_file_list());

	// *** Detect steam
	if (get_settings()->is_project_config_loaded()) {
		String custom_settings = get_settings()->get_project_setting("_custom_features");
		if (custom_settings.to_lower().contains("steam")) {
			report->godotsteam_detected = true;
			// now check if the godotsteam plugin is in the addons directory
			// If it is, we won't report it as detected, because you don't need the godotsteam editor to edit the project
			auto globs = Glob::glob("res://addons/*", true);
			for (int i = 0; i < globs.size(); i++) {
				if (globs[i].to_lower().contains("godotsteam")) {
					report->godotsteam_detected = false;
					break;
				}
			}
		}
	}
	std::shared_ptr<ProcessRunnerStruct> process_runner;
	TaskManager::TaskManagerID process_runner_task_id = -1;
	auto check_process_done = [&](bool p_cancelled = false) {
		if (process_runner_task_id != -1) {
			if (p_cancelled && process_runner && !process_runner->is_cancelled) {
				process_runner->cancel();
			}
			Error err = TaskManager::get_singleton()->wait_for_task_completion(process_runner_task_id);
			process_runner_task_id = -1;
			process_runner = nullptr;
			// err != OK means cancelled or timed out
			return err != OK;
		}
		return false;
	};

	auto reset_before_return = [&](bool cancelled = false) {
		if (cancelled) {
			print_line("Export cancelled!");
		}
		ResourceCompatLoader::unmake_globally_available();
		ResourceCompatLoader::set_default_gltf_load(false);
		check_process_done(cancelled);
	};

	// check if the pack has .cs files
	auto cs_files = GDRESettings::get_singleton()->get_file_list({ "*.cs" });
	if (cs_files.size() > 0) {
		report->mono_detected = true;
		Vector<String> exclude_files;
		for (int i = 0; i < cs_files.size(); i++) {
			if (!files_to_export_set.has(cs_files[i])) {
				exclude_files.push_back(cs_files[i]);
			}
		}
		if (exclude_files.size() == cs_files.size()) {
			// nothing to do
		} else if (GDRESettings::get_singleton()->has_loaded_dotnet_assembly()) {
			auto decompiler = GDRESettings::get_singleton()->get_dotnet_decompiler();

			String csproj_path = output_dir.path_join(GDRESettings::get_singleton()->get_project_dotnet_assembly_name() + ".csproj");
			err = decompiler->decompile_module(csproj_path, exclude_files);
			if (err != OK) {
				if (err == ERR_SKIP) {
					reset_before_return(true);
					return ERR_SKIP;
				}
				ERR_PRINT("Failed to decompile C# scripts!");
				report->failed_scripts.append_array(cs_files);
			} else {
				// compile the project to prevent editor errors
				if (GDREConfig::get_singleton()->get_setting("CSharp/compile_after_decompile", false)) {
					int ret_code;
					if (get_ver_major() >= 4 && OS::get_singleton()->execute("dotnet", { "--version" }, nullptr, &ret_code) == OK && ret_code == 0) {
						String solution_path = csproj_path.get_basename() + ".sln";
						process_runner = std::make_shared<ProcessRunnerStruct>("dotnet", Vector<String>({ "build", solution_path, "--property", "WarningLevel=0" }));
						if (process_runner->pre_run()) {
							process_runner_task_id = TaskManager::get_singleton()->add_task(process_runner, nullptr, "Compiling C# project...", -1, true, true);
						} else {
							// process failed to start
							process_runner = nullptr;
						}
					} else {
						print_line("Unable to compile C# project; ensure that the project is built in the editor before making any changes.");
					}
				}
				auto failed = decompiler->get_files_not_present_in_file_map();
				for (int i = 0; i < cs_files.size(); i++) {
					if (!failed.has(cs_files[i])) {
						report->decompiled_scripts.push_back(cs_files[i]);
					}
				}
				report->failed_scripts.append_array(failed);
			}
		} else {
			report->failed_scripts.append_array(cs_files);
		}
	}

	Ref<EditorProgressGDDC> pr = memnew(EditorProgressGDDC("export_imports", "Exporting resources...", export_files_count, true));

	Ref<DirAccess> dir = DirAccess::open(output_dir);

	recreate_plugin_configs();

	if (pr->step("Exporting resources...", 0, true)) {
		reset_before_return(true);
		return ERR_SKIP;
	}
	HashMap<String, Ref<ResourceExporter>> exporter_map;
	for (int i = 0; i < Exporter::exporter_count; i++) {
		Ref<ResourceExporter> exporter = Exporter::exporters[i];
		List<String> handled_importers;
		exporter->get_handled_importers(&handled_importers);
		for (const String &importer : handled_importers) {
			exporter_map[importer] = exporter;
		}
	}
	Vector<ExportToken> non_high_priority_tokens;
	Vector<ExportToken> tokens;
	Vector<ExportToken> non_multithreaded_tokens;
	Vector<Ref<ImportInfo>> scene_tokens;
	HashMap<String, Vector<Ref<ImportInfo>>> export_dest_to_iinfo;
	HashSet<String> dupes;
	for (int i = 0; i < _files.size(); i++) {
		Ref<ImportInfo> iinfo = _files[i];
		if (partial_export && !hashset_intersects_vector(files_to_export_set, iinfo->get_dest_files())) {
			continue;
		}
		String importer = iinfo->get_importer();
		if (importer == ImportInfo::NO_IMPORTER) {
			continue;
		}
		if (importer == "script_bytecode") {
			if (iinfo->get_path().get_extension().to_lower() == "gde" && GDRESettings::get_singleton()->had_encryption_error()) {
				// don't spam the logs with errors, just set the flag and skip
				report->failed_scripts.push_back(iinfo->get_path());
				report->had_encryption_error = true;
				continue;
			}
			if (GDRESettings::get_singleton()->get_bytecode_revision() == 0) {
				report->failed_scripts.push_back(iinfo->get_path());
				continue;
			}
		}
		if (iinfo->get_source_file().is_empty()) {
			continue;
		}
		// ***** Set export destination *****
		// This is a Godot asset that was imported outside of project directory
		if (!iinfo->get_source_file().begins_with("res://")) {
			if (get_ver_major() <= 2) {
				// import_md_path is the resource path in v2
				auto src = iinfo->get_source_file().simplify_path();
				if (!src.is_empty() && src.is_relative_path() && !src.begins_with("..")) {
					// just tack on "res://"
					iinfo->set_export_dest(String("res://").path_join(src));
				} else {
					iinfo->set_export_dest(String("res://.assets").path_join(iinfo->get_import_md_path().get_base_dir().path_join(iinfo->get_source_file().get_file()).replace("res://", "")));
				}
			} else {
				// import_md_path is the .import/.remap path in v3-v4
				// If the source_file path was not actually in the project structure, save it elsewhere
				if (iinfo->get_source_file().find(iinfo->get_export_dest().replace("res://", "")) == -1) {
					iinfo->set_export_dest(iinfo->get_export_dest().replace("res://", "res://.assets"));
				} else {
					iinfo->set_export_dest(iinfo->get_import_md_path().get_basename());
				}
			}
		} else {
			iinfo->set_export_dest(iinfo->get_source_file());
		}
		if (iinfo->get_ver_major() <= 2 && !iinfo->is_import() && !iinfo->is_auto_converted()) {
			// filter out v2 binary non-imports
			String ext = iinfo->get_path().get_extension().to_lower();
			if (ext == "scn" || ext == "res" || iinfo->get_source_file().has_extension("fixme")) {
				continue;
			}
		}
		bool supports_multithreading = !GDREConfig::get_singleton()->get_setting("force_single_threaded", false);
		bool is_high_priority = importer == "gdextension" || importer == "gdnative";
		if (exporter_map.has(importer)) {
			auto &exporter = exporter_map.get(importer);
			if (exporter->get_name() == "PackedScene") {
				scene_tokens.push_back(iinfo);
				export_dest_to_iinfo.insert(iinfo->get_export_dest(), Vector<Ref<ImportInfo>>({ iinfo }));
				continue;
			} else if (!exporter->supports_multithread()) {
				supports_multithreading = false;
			}
		} else {
			// Non-exportable resource that wasn't imported or auto-converted, don't report it
			if (iinfo->get_ver_major() <= 2 && !iinfo->is_import() && !iinfo->is_auto_converted()) {
				continue;
			}
			supports_multithreading = false;
		}
		if (is_high_priority) {
			if (supports_multithreading) {
				tokens.insert(0, { iinfo, nullptr, supports_multithreading });
			} else {
				non_multithreaded_tokens.insert(0, { iinfo, nullptr, supports_multithreading });
			}
		} else {
			if (supports_multithreading) {
				non_high_priority_tokens.push_back({ iinfo, nullptr, supports_multithreading });
			} else {
				non_multithreaded_tokens.push_back({ iinfo, nullptr, supports_multithreading });
			}
		}
		if (export_dest_to_iinfo.has(iinfo->get_export_dest())) {
			export_dest_to_iinfo[iinfo->get_export_dest()].push_back(iinfo);
			dupes.insert(iinfo->get_export_dest());
		} else {
			export_dest_to_iinfo.insert(iinfo->get_export_dest(), Vector<Ref<ImportInfo>>({ iinfo }));
		}
	}
	// Shuffle the vector to prevent situations where a bunch of large resources are exported at once and exhausts the memory
	gdre::shuffle_vector(non_high_priority_tokens);
	tokens.append_array(non_high_priority_tokens);

	pr->set_progress_length(false, tokens.size() + non_multithreaded_tokens.size());

	HashMap<String, String> dupe_to_orig_src;
	auto rewrite_dest = [&](const String &dest, const Ref<ImportInfo> &iinfo, bool is_autoconverted) {
		String curr_dest = iinfo->get_export_dest();
		String ext = "." + curr_dest.get_extension();
		String new_dest = curr_dest;
		if (!new_dest.begins_with("res://.assets")) {
			new_dest = String("res://.assets").path_join(new_dest.trim_prefix("res://"));
		}
		String pre_suffix = is_autoconverted ? ".converted" : "";
		String suffix = "";
		new_dest = new_dest.get_basename() + pre_suffix + suffix + ext;
		int j = 1;
		while (export_dest_to_iinfo.has(new_dest)) {
			new_dest = new_dest.trim_suffix(ext).trim_suffix(suffix).trim_suffix(pre_suffix);
			suffix = "." + String::num_int64(j);
			new_dest += pre_suffix + suffix + ext;
			j++;
		}
		iinfo->set_export_dest(new_dest);
		export_dest_to_iinfo.insert(new_dest, Vector<Ref<ImportInfo>>({ iinfo }));
		dupe_to_orig_src[new_dest] = curr_dest;
	};

	// duplicate export destinations for resources
	// usually only crops up in Godot 2.x
	if (dupes.size() > 0 && get_ver_major() > 2) {
		// if it pops up in >= 3.x, we want to know about it.
		WARN_PRINT("Found duplicate export destinations for resources! de-duping...");
	}
	for (auto &dup : dupes) {
		auto &iinfos = export_dest_to_iinfo[dup];
		if (iinfos.size() > 1) {
			String importer = iinfos[0]->get_importer();
			if (importer == "csv_translation" || importer == "translation_csv" || importer == "translation") {
				if (get_ver_major() <= 2) {
					// HACK: just add all the dest files to iinfos[0] and remove the duplicate tokens
					auto iinfo_copy = ImportInfo::copy(iinfos[0]);
					auto dest_files = iinfo_copy->get_dest_files();
					// remove the duplicate tokens
					for (int i = iinfos.size() - 1; i >= 1; i--) {
						dest_files.append_array(iinfos[i]->get_dest_files());
						iinfos.remove_at(i);
					}
					for (int i = non_multithreaded_tokens.size() - 1; i >= 0; i--) {
						if (non_multithreaded_tokens[i].iinfo == iinfos[0]) {
							non_multithreaded_tokens.write[i].iinfo = iinfo_copy;
						} else if (dest_files.has(non_multithreaded_tokens[i].iinfo->get_path())) {
							non_multithreaded_tokens.remove_at(i);
						}
					}

					iinfo_copy->set_dest_files(dest_files);
				} else {
					WARN_PRINT("DUPLICATE TRANSLATION CSV?!?!?!");
				}
				continue;
			}
			Vector<Ref<ImportInfo>> autoconverted;
			for (int i = iinfos.size() - 1; i >= 0; i--) {
				// auto-generated AtlasTexture spritesheet
				if (iinfos[i]->get_additional_sources().size() > 0) {
					autoconverted.push_back(iinfos[i]);
					iinfos.remove_at(i);
				}
				if (iinfos.size() == 1) {
					break;
				}
			}

			if (iinfos.size() > 1) {
				for (int i = iinfos.size() - 1; i >= 0; i--) {
					if (iinfos[i]->is_auto_converted()) {
						autoconverted.push_back(iinfos[i]);
						iinfos.remove_at(i);
					}
					if (iinfos.size() == 1) {
						break;
					}
				}
			}

			if (iinfos.size() > 1) {
				for (int i = iinfos.size() - 1; i >= 0; i--) {
					// The reason we check for auto-converts before non-imports is because
					// non-imports are usually higher quality than auto-converts in Godot 2.x
					if (!iinfos[i]->is_import()) {
						autoconverted.push_back(iinfos[i]);
						iinfos.remove_at(i);
					}
					if (iinfos.size() == 1) {
						break;
					}
				}
			}

			for (int i = 0; i < autoconverted.size(); i++) {
				auto &iinfo = autoconverted[i];
				rewrite_dest(dup, iinfo, iinfo->is_auto_converted());
			}
			if (iinfos.size() > 1) {
				for (int i = 1; i < iinfos.size(); i++) {
					rewrite_dest(dup, iinfos[i], false);
				}
			}
		}
	}

	int64_t num_multithreaded_tokens = tokens.size();
	// ***** Export resources *****
	GDRELogger::clear_error_queues();
	if (tokens.size() > 0) {
		err = TaskManager::get_singleton()->run_multithreaded_group_task(
				this,
				&ImportExporter::_do_export,
				tokens.ptrw(),
				tokens.size(),
				&ImportExporter::get_export_token_description,
				"ImportExporter::export_imports",
				"Exporting resources...",
				true, -1, true, pr, 0);
		if (err != OK) {
			reset_before_return(true);
			return err;
		}
	}
	GDRELogger::clear_error_queues();
	if (!non_multithreaded_tokens.is_empty()) {
		err = TaskManager::get_singleton()->run_group_task_on_current_thread(
				this,
				&ImportExporter::_do_export,
				non_multithreaded_tokens.ptrw(),
				non_multithreaded_tokens.size(),
				&ImportExporter::get_export_token_description,
				"ImportExporter::export_imports",
				"Exporting resources...",
				true, pr, num_multithreaded_tokens);
	}
	if (err != OK) {
		reset_before_return(true);
		return err;
	}

	if (scene_tokens.size() > 0) {
		if (GDRESettings::get_singleton()->is_headless()) {
			print_line("WARNING: Some scenes can fail to export in headless mode. This is due to a limitation of the Godot engine.\n"
					   "If some scenes fail to export, try running in GUI mode.");
		}
		// intentionally not checking for cancellation here; scene export can sometimes hang on certain scenes, and we don't want to cancel the entire export
		auto reports = SceneExporter::get_singleton()->batch_export_files(output_dir, scene_tokens);
		for (int i = 0; i < reports.size(); i++) {
			ExportToken token = { reports[i]->get_import_info(), reports[i], false };
			rewrite_metadata(token);
			non_multithreaded_tokens.push_back(token);
		}
	}

	if (err != OK) {
		reset_before_return(true);
		return err;
	}
	tokens.append_array(non_multithreaded_tokens);
	pr->step("Finalizing...", tokens.size() - 1, false);
	pr->set_progress_length(true);

	report->session_files_total = tokens.size();
	// add to report
	bool has_remaps = GDRESettings::get_singleton()->has_any_remaps();
	HashSet<String> success_paths;
	bool doing_cache = get_ver_major() >= 4;
	for (int i = 0; i < tokens.size(); i++) {
		const ExportToken &token = tokens[i];
		Ref<ImportInfo> iinfo = token.iinfo;
		String src_ext = iinfo->get_source_file().get_extension().to_lower();
		Ref<ExportReport> ret = token.report;
		if (ret.is_null()) {
			ERR_PRINT("Exporter returned null report for " + iinfo->get_path());
			continue;
		}
		if (doing_cache) {
			src_to_report[iinfo->get_source_file()] = ret;
		}
		String exporter = ret->get_exporter();
		err = ret->get_error();

		if (exporter == SceneExporter::EXPORTER_NAME && src_ext != "escn" && src_ext != "tscn") {
			report->exported_scenes = true;
			if (err != OK && GDRESettings::get_singleton()->is_headless()) {
				report->show_headless_warning = true;
			}
		}
		if (err == ERR_SKIP) {
			report->not_converted.push_back(ret);
			continue;
		} else if (err == ERR_UNAVAILABLE) {
			String type = iinfo->get_type();
			String format_type = src_ext;
			if (ret->get_unsupported_format_type() != "") {
				format_type = ret->get_unsupported_format_type();
			} else {
				ret->set_unsupported_format_type(format_type);
			}
			report_unsupported_resource(type, format_type, iinfo->get_importer(), iinfo->get_path());
			report->not_converted.push_back(ret);
			continue;
		} else if (err != OK) {
			if (exporter == GDScriptExporter::EXPORTER_NAME) {
				report->failed_scripts.push_back(iinfo->get_path());
				if (err == ERR_UNAUTHORIZED) {
					report->had_encryption_error = true;
				}
			}
			report->failed.push_back(ret);
			print_verbose("Failed to convert " + iinfo->get_type() + " resource " + iinfo->get_path());
			continue;
		}
		if (exporter == SceneExporter::EXPORTER_NAME && src_ext != "escn" && src_ext != "tscn") {
			// report->exported_scenes = true;
// This is currently forcing a reimport instead of preventing it, disabling for now
#if 0
			auto extra_info = ret->get_extra_info();
			if (extra_info.has("image_path_to_data_hash")) {
				// We have to rewrite the generator_parameters for the images if they were used as part of an imported scene
				Dictionary image_path_to_data_hash = extra_info["image_path_to_data_hash"];
				for (auto &E : image_path_to_data_hash) {
					String path = E.key;
					String data_hash = E.value;
					if (src_to_iinfo.has(path)) {
						Ref<ImportInfoModern> iinfo = src_to_iinfo[path];
						if (iinfo.is_null()) {
							continue;
						}
						Dictionary generator_parameters;
						generator_parameters["md5"] = data_hash;
						iinfo->set_iinfo_val("remap", "generator_parameters", generator_parameters);
						auto path = output_dir.path_join(iinfo->get_import_md_path().trim_prefix("res://"));
						iinfo->save_to(path);
						ret->import_modified_time = FileAccess::get_modified_time(path);
						ret->import_md5 = FileAccess::get_md5(path);
						// we have to touch the md5 file again
						auto md5_file_path = iinfo->get_md5_file_path();
						touch_file(md5_file_path);
					}
				}
			}
#endif
		} else if (exporter == TranslationExporter::EXPORTER_NAME) {
			report->translation_export_message += ret->get_message();
		} else if (exporter == GDScriptExporter::EXPORTER_NAME) {
			report->decompiled_scripts.push_back(iinfo->get_path());
			// 4.4 and higher have uid files for scripts that we have to recreate
			if ((get_ver_major() == 4 && get_ver_minor() >= 4) || get_ver_major() > 4) {
				recreate_uid_file(iinfo->get_source_file(), true, files_to_export_set);
			}
		} else if (exporter == GDExtensionExporter::EXPORTER_NAME) {
			if (!ret->get_message().is_empty()) {
				report->failed_gdnative_copy.push_back(ret->get_message());
				ret->set_message("Failed to copy GDExtension addon for this platform");
				report->failed.push_back(ret);
				continue;
			} else if (!ret->get_saved_path().is_empty() && ret->get_download_task_id() != -1) {
				Error dl_err = TaskManager::get_singleton()->wait_for_download_task_completion(ret->get_download_task_id());
				if (dl_err != OK) {
					report->failed_gdnative_copy.push_back(ret->get_saved_path());
					ret->set_message("Download failed");
					ret->set_error(dl_err);
					report->failed.push_back(ret);
					continue;
				}
				dl_err = unzip_and_copy_addon(iinfo, ret->get_saved_path());
				if (dl_err != OK) {
					report->failed_gdnative_copy.push_back(ret->get_saved_path());
					ret->set_message("Failed to unzip and copy GDExtension addon");
					ret->set_error(dl_err);
					report->failed.push_back(ret);
					continue;
				}
				report->downloaded_plugins.push_back(ret->get_extra_info());
			}
		}
		report->success.push_back(ret);
		success_paths.insert(iinfo->get_export_dest());
	}

	// remove remaps
	if (has_remaps) {
		for (auto &token : tokens) {
			auto &iinfo = token.iinfo;
			auto tkerr = token.report.is_null() ? ERR_BUG : token.report->get_error();
			if (iinfo.is_valid() && !tkerr) {
				auto src = iinfo->get_export_dest();
				if (success_paths.has(src)) {
					auto dest = iinfo->get_path();
					if (get_settings()->has_remap(src, dest)) {
						remove_remap_and_autoconverted(src, dest);
					} else if (iinfo->is_auto_converted() && dupe_to_orig_src.has(src)) {
						auto &orig_src = dupe_to_orig_src[src];
						if (success_paths.has(orig_src) && get_settings()->has_remap(orig_src, dest)) {
							remove_remap_and_autoconverted(orig_src, dest);
						}
					}
				}
			}
		}
	}

	// Need to recreate the uid files for the exported resources
	// check if we're at version 4.4 or higher
	if ((get_ver_major() == 4 && get_ver_minor() >= 4) || get_ver_major() > 4) {
		static const Vector<String> non_custom_uid_filters = {
			"*.image",
			"*.gdextension",
			"*.gd",
			// "*.gdc", -- these show up outside of res://.godot, so we don't want to recreate them
			"*.ctex",
			"*.ctexarray",
			"*.ccube",
			"*.ccubearray",
			"*.ctex3d",
			"*.shader",
			"*.gdshader",
			"*.gdshaderinc",
			"*.dds",
			"*.ktx",
			"*.ktx2",
			"*.ogv",
			"*.cs"
		};
		auto non_custom_uid_files = get_settings()->get_file_list(non_custom_uid_filters);
		for (int i = 0; i < non_custom_uid_files.size(); i++) {
			// any hidden directory
			if (non_custom_uid_files[i].begins_with("res://.")) {
				continue;
			}
			recreate_uid_file(non_custom_uid_files[i], false, files_to_export_set);
		}
	}

	if (get_settings()->is_project_config_loaded()) { // some pcks do not have project configs
		if constexpr (GDScriptDecomp::FORCE_SPACES_FOR_2_0) {
			// if we're at v4.5 or higher (<4.5 doesn't support editor_overrides), we want to set "editor_overrides/text_editor/behavior/indent/type" to "Spaces"
			// This avoids editor churn on the scripts when they're resaved by the editor
			if (get_ver_major() == 4 && get_ver_minor() >= 5 && !get_settings()->has_project_setting("editor_overrides/text_editor/behavior/indent/type")) {
				get_settings()->set_project_setting("editor_overrides/text_editor/behavior/indent/type", 1);
			}
		}
		if (get_settings()->save_project_config(output_dir) != OK) {
			print_line("ERROR: Failed to save project config!");
		} else {
			print_line("Saved project config.");
			String pcfg_file = get_settings()->get_project_config_path().get_file();
			if (pcfg_file.to_lower().ends_with(".cfb") || pcfg_file.to_lower().ends_with(".binary")) {
				// Remove binary project config, as editors will load from it instead of the text one
				dir->remove(pcfg_file);
			}
		}
	}
	// check if the .tmp directory is empty
	if (gdre::dir_is_empty(output_dir.path_join(".tmp"))) {
		dir->remove(output_dir.path_join(".tmp"));
	}

	// 4.1 and higher have a filesystem cache
	if (doing_cache) {
		String cache_path = get_ver_minor() < 4 ? "filesystem_cache8" : "filesystem_cache10";
		String editor_dir = output_dir.path_join(".godot").path_join("editor");
		gdre::ensure_dir(editor_dir);
		String cache_file = editor_dir.path_join(cache_path);
		Vector<std::shared_ptr<FileInfo>> file_infos;
		update_exts();
		if (!partial_export || !FileAccess::exists(cache_file)) {
			{
				auto list = gdre::get_recursive_dir_list_multithread(
						output_dir,
						{},
						false,
						false,
						{ "*.uid", "*.import" },
						true,
						true,
						true);
				file_infos.resize_initialized(list.size());
				for (int i = 0; i < list.size(); i++) {
					file_infos.write[i] = std::make_shared<FileInfo>();
					file_infos.write[i]->file = "res://" + list[i];
				}
			}

			err = TaskManager::get_singleton()->run_multithreaded_group_task(
					this,
					&ImportExporter::_do_file_info,
					file_infos.ptrw(),
					file_infos.size(),
					&ImportExporter::get_file_info_description,
					"ImportExporter::export_imports::filesystem_cache",
					"Generating filesystem cache...",
					true, -1, true);
		} else {
			HashSet<String> scan_set;
			for (auto &E : files_to_export_set) {
				if (E.contains("/.")) { // hidden files
					continue;
				}
				scan_set.insert(E);
			}
			for (auto &E : src_to_report) {
				if (E.key.contains("/.")) { // hidden files
					continue;
				}
				scan_set.insert(E.key);
			}
			file_infos = read_filesystem_cache(cache_file);
			HashMap<String, std::shared_ptr<FileInfo>> file_info_map;
			for (int i = 0; i < file_infos.size(); i++) {
				file_info_map[file_infos[i]->file] = file_infos[i];
			}
			Vector<std::shared_ptr<FileInfo>> to_scan;
			for (auto &path : scan_set) {
				if (file_info_map.has(path)) {
					to_scan.push_back(file_info_map[path]);
				} else {
					auto file_info = std::make_shared<FileInfo>();
					file_info->file = path;
					to_scan.push_back(file_info);
					file_infos.push_back(file_info);
				}
			}
			err = TaskManager::get_singleton()->run_multithreaded_group_task(
					this,
					&ImportExporter::_do_file_info,
					to_scan.ptrw(),
					to_scan.size(),
					&ImportExporter::get_file_info_description,
					"ImportExporter::export_imports::filesystem_cache",
					"Generating filesystem cache...",
					true, -1, true);
			file_infos.sort_custom<FileInfoComparator>();
		}
		if (file_infos.size() > 0) {
			save_filesystem_cache(file_infos, output_dir);
		}
	}

	pr = nullptr;
	reset_before_return(false);
	report->print_report();
	if (GDREConfig::get_singleton()->get_setting("write_json_report", false)) {
		String json_file = output_dir.path_join("gdre_export.json");
		Ref<FileAccess> f = FileAccess::open(json_file, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V_MSG(err || f.is_null(), ERR_FILE_CANT_WRITE, "can't open report.json for writing");
		f->store_string(JSON::stringify(report->to_json(), "\t", false, true));
	}
	return OK;
}

Error ImportExporter::recreate_plugin_config(const String &plugin_cfg_path) {
	Error err;
	Vector<String> wildcards = { "*.gdc", "*.gde", "*.gd" };

	HashSet<String> non_present_scripts;
	String abs_plugin_path = plugin_cfg_path.get_base_dir();
	String rel_plugin_path = abs_plugin_path.trim_prefix("res://");
	String plugin_dir = rel_plugin_path.trim_prefix("addons/").trim_prefix("Addons/");
	String plugin_name = plugin_dir.replace("_", " ").replace(".", " ").replace("/", " ");
	Ref<GodotMonoDecompWrapper> decompiler;
	if (GDRESettings::get_singleton()->has_loaded_dotnet_assembly()) {
		wildcards.push_back("*.cs");
		decompiler = GDRESettings::get_singleton()->get_dotnet_decompiler();
		non_present_scripts = gdre::vector_to_hashset(decompiler->get_files_not_present_in_file_map());
	}

	auto gd_scripts = gdre::get_recursive_dir_list(abs_plugin_path, wildcards, false);
	String main_script;

	if (gd_scripts.is_empty()) {
		return OK;
	}

	bool cant_decompile = false;

	for (int j = 0; j < gd_scripts.size(); j++) {
		auto ext = gd_scripts[j].get_extension().to_lower();
		if ((ext == "gde" || ext == "gdc") && GDRESettings::get_singleton()->get_bytecode_revision() == 0) {
			cant_decompile = true;
			continue;
		}
		String gd_script_abs_path = abs_plugin_path.path_join(gd_scripts[j]);
		if (ext == "cs") {
			if (decompiler.is_null() || non_present_scripts.has(gd_script_abs_path) || decompiler->get_script_info(gd_script_abs_path).is_empty()) {
				continue;
			}
		}
		Ref<FakeScript> gd_script = ResourceCompatLoader::non_global_load(gd_script_abs_path, "", &err);
		if (gd_script.is_valid()) {
			if (gd_script->get_instance_base_type() == "EditorPlugin") {
				main_script = gd_scripts[j];
				if (ext != "cs" && ext != "gd") {
					main_script = main_script.get_basename() + ".gd";
				}
				break;
			}
		}
	}
	if (main_script == "") {
		if (cant_decompile) {
			return ERR_UNCONFIGURED;
		} else {
			return ERR_CANT_CREATE;
		}
	}
	String plugin_cfg_text = String("[plugin]\n\n") +
			"name=\"" + plugin_name + "\"\n" +
			"description=\"" + plugin_name + " plugin\"\n" +
			"author=\"Unknown\"\n" +
			"version=\"1.0\"\n" +
			"script=\"" + main_script + "\"";
	String output_plugin_path = output_dir.path_join(rel_plugin_path);
	gdre::ensure_dir(output_plugin_path);
	Ref<FileAccess> f = FileAccess::open(output_plugin_path.path_join("plugin.cfg"), FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err || f.is_null(), ERR_FILE_CANT_WRITE, "can't open plugin.cfg for writing");
	ERR_FAIL_COND_V_MSG(!f->store_string(plugin_cfg_text), ERR_FILE_CANT_WRITE, "can't write plugin.cfg");
	print_verbose("Recreated plugin config for " + plugin_name);
	return OK;
}

// Recreates the "plugin.cfg" files for each plugin to avoid loading errors.
Error ImportExporter::recreate_plugin_configs() {
	Vector<String> enabled_plugins = GDRESettings::get_singleton()->get_project_setting("editor_plugins/enabled");
	if (enabled_plugins.is_empty()) {
		return OK;
	}

	Error err;
	print_line("Recreating plugin configs...");
	Vector<String> addons_dirs;
	String addons_dir = "res://addons/";
	Ref<DirAccess> da = DirAccess::open(addons_dir);
	if (da.is_null()) { // case-insensitive check for addons directory
		da = DirAccess::open("res://");
		auto dirs = da->get_directories();
		bool found = false;
		for (int i = 0; i < dirs.size(); i++) {
			if (dirs[i].filenocasecmp_to("addons") == 0) {
				addons_dir = "res://" + dirs[i] + "/";
				da->change_dir(dirs[i]);
				found = true;
				break;
			}
		}
		if (!found) {
			da = nullptr;
		}
	}
	if (!da.is_null()) {
		addons_dirs = gdre::get_directories_at_recursive(addons_dir, false, true);
	}
	for (int i = 0; i < enabled_plugins.size(); i++) {
		String &path = enabled_plugins.write[i];
		path = path.replace("res://addons/", addons_dir);
		String dir = path.get_base_dir();
		if (dir.is_empty() || !dir.is_absolute_path()) {
			bool found = false;
			for (int j = 0; j < addons_dirs.size(); j++) {
				if (addons_dirs[j].filenocasecmp_to(path) == 0) {
					path = addons_dir + addons_dirs[j];
					found = true;
					break;
				}
			}
			if (found) {
				if (!path.ends_with("plugin.cfg")) {
					path = path.path_join("plugin.cfg");
				}
				dir = path.get_base_dir();
			} else {
				report->failed_plugin_cfg_create.push_back(path);
				continue;
			}
		}
		// plugin was not included in the project
		if (!DirAccess::dir_exists_absolute(dir)) {
			report->failed_plugin_cfg_create.push_back(path.get_base_dir().get_file());
			continue;
		} else if (FileAccess::exists(path)) {
			continue; // don't try to recreate plugin.cfg if it already exists
		}
		err = recreate_plugin_config(path);
		if (err) {
			WARN_PRINT("Failed to recreate plugin.cfg for " + dir);
			report->failed_plugin_cfg_create.push_back(path.get_base_dir().get_file());
		}
	}
	return OK;
}

// Godot import data rewriting
// TODO: For Godot v3-v4, we have to rewrite any resources that have this resource as a dependency to remap to the new destination
// However, we currently only rewrite the import data if the source file was recorded as an absolute file path,
// but is still in the project directory structure, which means no resource rewriting is necessary
Error ImportExporter::rewrite_import_source(const String &rel_dest_path, const Ref<ImportInfo> &iinfo) {
	String new_source = rel_dest_path;
	String abs_file_path = output_dir.path_join(new_source.replace("res://", ""));
	String source_md5 = FileAccess::get_md5(abs_file_path);
	// hack for v2 translations
	if (iinfo->get_ver_major() <= 2 && iinfo->get_dest_files().size() > 1) {
		// dest files in v2 are the import_md files
		auto dest_files = iinfo->get_dest_files();
		for (int i = 0; i < dest_files.size(); i++) {
			Ref<ImportInfo> new_import = ImportInfo::copy(GDRESettings::get_singleton()->get_import_info_by_dest(dest_files[i]));
			ERR_CONTINUE_MSG(new_import.is_null(), "Failed to copy import info for " + dest_files[i]);
			String new_import_file = output_dir.path_join(dest_files[i].replace("res://", ""));
			new_import->set_source_and_md5(new_source, source_md5);
			Error err = new_import->save_to(new_import_file);
			ERR_FAIL_COND_V_MSG(err, err, "Failed to save import data for " + new_import_file);
		}
		return OK;
	}
	String new_import_file = output_dir.path_join(iinfo->get_import_md_path().replace("res://", ""));
	Ref<ImportInfo> new_import = ImportInfo::copy(iinfo);
	new_import->set_source_and_md5(new_source, source_md5);
	return new_import->save_to(new_import_file);
}

void ImportExporter::report_unsupported_resource(const String &type, const String &format_name, const String &importer, const String &import_path) {
	String type_format_str = type + "%" + format_name.to_lower() + "%" + importer;
	if (report->unsupported_types.find(type_format_str) == -1) {
		report->unsupported_types.push_back(type_format_str);
	}
	print_verbose("Did not convert " + type + " resource " + import_path);
}

void ImportExporter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("export_imports", "p_out_dir", "files_to_export"), &ImportExporter::export_imports, DEFVAL(PackedStringArray()));
	ClassDB::bind_method(D_METHOD("get_report"), &ImportExporter::get_report);
	ClassDB::bind_method(D_METHOD("reset"), &ImportExporter::reset);
}

void ImportExporter::reset_log() {
	report = Ref<ImportExporterReport>(memnew(ImportExporterReport));
}

void ImportExporter::reset() {
	output_dir.clear();
	src_to_report.clear();
	textfile_extensions.clear();
	other_file_extensions.clear();
	valid_extensions.clear();
	reset_log();
}

ImportExporter::ImportExporter() {
	reset_log();
}
ImportExporter::~ImportExporter() {
	reset();
}

void ImportExporterReport::set_ver(String p_ver) {
	this->ver = GodotVer::parse(p_ver);
}

String ImportExporterReport::get_ver() {
	return ver->as_text();
}

Dictionary ImportExporterReport::get_totals() {
	auto lossy_imports = _get_lossy_imports();
	auto rewrote_metadata = _get_rewrote_metadata();
	auto failed_rewrite_md = _get_failed_rewrite_md();
	auto failed_rewrite_md5 = _get_failed_rewrite_md5();
	Dictionary totals;
	totals["total"] = decompiled_scripts.size() + failed_scripts.size() + lossy_imports.size() + rewrote_metadata.size() + failed_rewrite_md.size() + failed_rewrite_md5.size() + failed.size() + success.size() + not_converted.size() + failed_plugin_cfg_create.size() + failed_gdnative_copy.size() + unsupported_types.size();
	totals["decompiled_scripts"] = decompiled_scripts.size();
	totals["success"] = success.size();
	totals["failed"] = failed.size();
	totals["not_converted"] = not_converted.size();
	totals["failed_scripts"] = failed_scripts.size();
	totals["lossy_imports"] = lossy_imports.size();
	totals["rewrote_metadata"] = rewrote_metadata.size();
	totals["failed_rewrite_md"] = failed_rewrite_md.size();
	totals["failed_rewrite_md5"] = failed_rewrite_md5.size();

	totals["failed_plugin_cfg_create"] = failed_plugin_cfg_create.size();
	totals["failed_gdnative_copy"] = failed_gdnative_copy.size();
	totals["unsupported_types"] = unsupported_types.size();
	return totals;
}

Dictionary ImportExporterReport::get_unsupported_types() {
	Dictionary unsupported;
	for (int i = 0; i < unsupported_types.size(); i++) {
		auto split = unsupported_types[i].split("%");
		unsupported[i] = unsupported_types[i];
	}
	return unsupported;
}

Dictionary ImportExporterReport::get_session_notes() {
	Dictionary notes;
	List<String> base_exts;
	HashSet<String> base_ext_set;
	ResourceCompatLoader::get_base_extensions(&base_exts, get_ver_major());
	for (auto &type : base_exts) {
		base_ext_set.insert(type);
	}
	base_ext_set.insert("tscn");
	base_ext_set.insert("tres");
	base_ext_set.insert("png");
	base_ext_set.insert("jpg");
	base_ext_set.insert("wav");
	base_ext_set.insert("ogg");
	base_ext_set.insert("mp3");

	if (!unsupported_types.is_empty()) {
		Dictionary unsupported;
		unsupported["title"] = "Unsupported Resources Detected";
		String message = "The following resource types were detected in the project that conversion is not implemented for yet.\n";
		message += "See Export Report to see which resources were not exported.\n";
		message += "You will still be able to edit the project in the editor regardless.";
		unsupported["message"] = message;
		PackedStringArray list;
		for (int i = 0; i < unsupported_types.size(); i++) {
			auto split = unsupported_types[i].split("%");
			String str = vformat("Resource Type: %-10s Format: %-8s Importer: %s", split[0], split[1], split[2]);
			if ((split[0] == "Resource" || split[1].size() == 3) && !base_ext_set.has(split[1])) {
				str += " (non-standard resource)";
			}
			list.push_back(str);
		}
		unsupported["details"] = list;
		notes["unsupported_types"] = unsupported;
	}

	if (had_encryption_error) {
		// notes["encryption_error"] = "Failed to decompile encrypted scripts!\nSet the correct key and try again!";
		Dictionary encryption_error;
		encryption_error["title"] = "Encryption Error";
		encryption_error["message"] = "Failed to decompile encrypted scripts!\nSet the correct key and try again!";
		encryption_error["details"] = PackedStringArray();
		notes["encryption_error"] = encryption_error;
	}

	if (!translation_export_message.is_empty()) {
		// notes["translation_export_message"] = translation_export_message;
		Dictionary translation_export;
		translation_export["title"] = "Translation Export Incomplete";
		translation_export["message"] = translation_export_message;
		translation_export["details"] = PackedStringArray();
		notes["translation_export_message"] = translation_export;
	}

	if (!failed_gdnative_copy.is_empty()) {
		// notes["failed_gdnative_copy"] = failed_gdnative_copy;
		Dictionary failed_gdnative;
		failed_gdnative["title"] = "Missing GDExtension Libraries";
		String message = "The following GDExtension addons could not be";
		if (GDREConfig::get_singleton()->get_setting("download_plugins")) {
			message += " detected and downloaded.\n";
		} else {
			message += " found for your platform.\n";
		}
		message += "Tip: Try finding the plugin in the Godot Asset Library or Github.\n";
		failed_gdnative["message"] = message;
		failed_gdnative["details"] = failed_gdnative_copy;
		notes["failed_gdnative"] = failed_gdnative;
	}

	if (!failed_plugin_cfg_create.is_empty()) {
		Dictionary failed_plugins;
		failed_plugins["title"] = "Incomplete Plugin Export";
		String message = "The following addons failed to have their plugin.cfg regenerated\n";
		message += "You may encounter editor errors due to this.\n";
		message += "Tip: Try finding the plugin in the Godot Asset Library or Github.\n";
		failed_plugins["message"] = message;
		failed_plugins["details"] = failed_plugin_cfg_create;
		notes["failed_plugins"] = failed_plugins;
	}
	if (ver->get_major() == 2) {
		// Godot 2.x's assets are all exported to .assets
		Dictionary godot_2_assets;
		godot_2_assets["title"] = "Godot 2.x Assets";
		godot_2_assets["message"] = "All exported assets can be found in the '.assets' directory in the project folder.";
		godot_2_assets["details"] = PackedStringArray();
		notes["godot_2_assets"] = godot_2_assets;
	}

	if (show_headless_warning) {
		Dictionary headless_warning;
		headless_warning["title"] = "Scene Export in headless mode is limited";
		headless_warning["message"] = "Some scenes can fail to export in headless mode. This is due to a limitation of the Godot engine.\n"
									  "Retry without `--headless`. (CLI commands will still work in GUI mode.)";
		headless_warning["details"] = PackedStringArray();
		notes["headless_warning"] = headless_warning;
	}

	return notes;
}

String ImportExporterReport::get_totals_string() {
	String report = "";
	auto lossy_imports = _get_lossy_imports();
	auto rewrote_metadata = _get_rewrote_metadata();
	auto failed_rewrite_md = _get_failed_rewrite_md();
	report += vformat("%-40s", "Totals: ") + String("\n");
	report += vformat("%-40s", "Decompiled scripts: ") + itos(decompiled_scripts.size()) + String("\n");
	report += vformat("%-40s", "Failed scripts: ") + itos(failed_scripts.size()) + String("\n");
	report += vformat("%-40s", "Imported resources for export session: ") + itos(session_files_total) + String("\n");
	report += vformat("%-40s", "Successfully converted: ") + itos(success.size()) + String("\n");
	if (opt_lossy) {
		report += vformat("%-40s", "Lossy: ") + itos(lossy_imports.size()) + String("\n");
	} else {
		report += vformat("%-40s", "Lossy not converted: ") + itos(lossy_imports.size()) + String("\n");
	}
	report += vformat("%-40s", "Rewrote metadata: ") + itos(rewrote_metadata.size()) + String("\n");
	report += vformat("%-40s", "Non-importable conversions: ") + itos(failed_rewrite_md.size()) + String("\n");
	report += vformat("%-40s", "Not converted: ") + itos(not_converted.size()) + String("\n");
	report += vformat("%-40s", "Failed conversions: ") + itos(failed.size()) + String("\n");
	return report;
}

void add_to_dict(Dictionary &dict, const Vector<Ref<ExportReport>> &vec) {
	for (int i = 0; i < vec.size(); i++) {
		dict[vec[i]->get_new_source_path()] = vec[i]->get_path();
	}
}

Dictionary ImportExporterReport::get_section_labels() {
	Dictionary labels;
	labels["success"] = "Successfully converted";
	labels["decompiled_scripts"] = "Decompiled scripts";
	labels["not_converted"] = "Not converted";
	labels["failed_scripts"] = "Failed scripts";
	labels["failed"] = "Failed conversions";
	labels["lossy_imports"] = "Lossy imports";
	labels["rewrote_metadata"] = "Rewrote metadata";
	labels["failed_rewrite_md"] = "Non-importable";
	labels["failed_rewrite_md5"] = "Failed to rewrite metadata MD5";
	labels["failed_plugin_cfg_create"] = "Failed to create plugin.cfg";
	labels["failed_gdnative_copy"] = "Failed to copy GDExtension libraries";
	labels["unsupported_types"] = "Unsupported types";
	labels["downloaded_plugins"] = "Downloaded plugins";
	return labels;
}

Dictionary ImportExporterReport::get_report_sections() {
	Dictionary sections;
	// sections["totals"] = get_totals();
	// sections["unsupported_types"] = get_unsupported_types();
	// sections["session_notes"] = get_session_notes();

	auto lossy_imports = _get_lossy_imports();
	auto rewrote_metadata = _get_rewrote_metadata();
	auto failed_rewrite_md = _get_failed_rewrite_md();
	auto failed_rewrite_md5 = _get_failed_rewrite_md5();
	if (!failed.is_empty()) {
		sections["failed"] = Dictionary();
		Dictionary failed_dict = sections["failed"];
		for (int i = 0; i < failed.size(); i++) {
			if (failed[i]->get_exporter() == GDExtensionExporter::EXPORTER_NAME) {
				continue;
			}
			failed_dict[failed[i]->get_new_source_path()] = Dictionary();
			Dictionary error_dict_item = failed_dict[failed[i]->get_new_source_path()];
			error_dict_item["Imported Resource"] = failed[i]->get_path();
			String message = failed[i]->get_message();
			if (!message.is_empty()) {
				error_dict_item["Message"] = message;
			}
			auto details = failed[i]->get_message_detail();
			if (!details.is_empty()) {
				error_dict_item["Details"] = details;
			}
			auto error_messages = failed[i]->get_error_messages();
			if (!error_messages.is_empty()) {
				error_dict_item["Error Messages"] = filter_error_backtraces(error_messages);
			}
		}
		if (failed_dict.size() == 0) {
			sections.erase("failed");
		}
	}
	if (!not_converted.is_empty()) {
		sections["not_converted"] = Dictionary();
		Dictionary not_converted_dict = sections["not_converted"];
		add_to_dict(not_converted_dict, not_converted);
	}
	if (!failed_scripts.is_empty()) {
		sections["failed_scripts"] = Dictionary();
		Dictionary failed_scripts_dict = sections["failed_scripts"];
		for (int i = 0; i < failed_scripts.size(); i++) {
			failed_scripts_dict[failed_scripts[i]] = failed_scripts[i];
		}
	}
	if (!lossy_imports.is_empty()) {
		sections["lossy_imports"] = Dictionary();
		Dictionary lossy_dict = sections["lossy_imports"];
		add_to_dict(lossy_dict, lossy_imports);
	}
	if (!failed_rewrite_md.is_empty()) {
		sections["failed_rewrite_md"] = Dictionary();
		Dictionary failed_rewrite_md_dict = sections["failed_rewrite_md"];
		add_to_dict(failed_rewrite_md_dict, failed_rewrite_md);
	}
	// plugins
	if (!failed_plugin_cfg_create.is_empty()) {
		sections["failed_plugin_cfg_create"] = Dictionary();
		Dictionary failed_plugin_cfg_create_dict = sections["failed_plugin_cfg_create"];
		for (int i = 0; i < failed_plugin_cfg_create.size(); i++) {
			failed_plugin_cfg_create_dict[failed_plugin_cfg_create[i]] = failed_plugin_cfg_create[i];
		}
	}
	if (!failed_gdnative_copy.is_empty()) {
		sections["failed_gdnative_copy"] = Dictionary();
		Dictionary failed_gdnative_copy_dict = sections["failed_gdnative_copy"];
		for (int i = 0; i < failed_gdnative_copy.size(); i++) {
			failed_gdnative_copy_dict[failed_gdnative_copy[i]] = failed_gdnative_copy[i];
		}
	}
	if (!failed_rewrite_md5.is_empty()) {
		sections["failed_rewrite_md5"] = Dictionary();
		Dictionary failed_rewrite_md5_dict = sections["failed_rewrite_md5"];
		add_to_dict(failed_rewrite_md5_dict, failed_rewrite_md5);
	}
	if (!rewrote_metadata.is_empty()) {
		sections["rewrote_metadata"] = Dictionary();
		Dictionary rewrote_metadata_dict = sections["rewrote_metadata"];
		add_to_dict(rewrote_metadata_dict, rewrote_metadata);
	}

	if (!downloaded_plugins.is_empty()) {
		sections["downloaded_plugins"] = Dictionary();
		Dictionary downloaded_plugins_dict = sections["downloaded_plugins"];
		for (int i = 0; i < downloaded_plugins.size(); i++) {
			downloaded_plugins_dict[downloaded_plugins[i]["plugin_name"]] = downloaded_plugins[i];
		}
	}

	sections["success"] = Dictionary();
	Dictionary success_dict = sections["success"];
	add_to_dict(success_dict, success);
	sections["decompiled_scripts"] = Dictionary();
	Dictionary decompiled_scripts_dict = sections["decompiled_scripts"];
	for (int i = 0; i < decompiled_scripts.size(); i++) {
		decompiled_scripts_dict[decompiled_scripts[i]] = decompiled_scripts[i];
	}
	return sections;
}

String get_to_string(const Vector<Ref<ExportReport>> &vec) {
	String str = "";
	for (auto &info : vec) {
		str += info->get_path() + " to " + info->get_new_source_path() + String("\n");
	}
	return str;
}

String get_failed_section_string(const Vector<Ref<ExportReport>> &vec) {
	String str = "";
	for (int i = 0; i < vec.size(); i++) {
		str += vec[i]->get_path() + String("\n");
	}
	return str;
}

String ImportExporterReport::get_report_string() {
	String report;
	report += get_totals_string();
	report += "-------------\n" + String("\n");
	if (!opt_lossy) {
		auto lossy_imports = _get_lossy_imports();
		report += "\nThe following files were not converted from a lossy import." + String("\n");
		report += get_failed_section_string(lossy_imports);
	}
	if (failed_plugin_cfg_create.size() > 0) {
		report += "------\n";
		report += "\nThe following plugins failed to have their plugin.cfg regenerated:" + String("\n");
		for (int i = 0; i < failed_plugin_cfg_create.size(); i++) {
			report += failed_plugin_cfg_create[i] + String("\n");
		}
	}

	if (failed_gdnative_copy.size() > 0) {
		report += "------\n";
		report += "\nThe following native plugins failed to have their libraries copied:" + String("\n");
		for (int i = 0; i < failed_gdnative_copy.size(); i++) {
			report += failed_gdnative_copy[i] + String("\n");
		}
	}
	// we skip this for version 2 because we have to rewrite the metadata for nearly all the converted resources
	// if (rewrote_metadata.size() > 0 && ver->get_major() != 2) {
	// 	report += "------\n";
	// 	report += "\nThe following files had their import data rewritten:" + String("\n");
	// 	report += get_to_string(rewrote_metadata);
	// }
	auto failed_rewrite_md = _get_failed_rewrite_md();
	if (failed_rewrite_md.size() > 0) {
		report += "------\n";
		report += "\nThe following files were converted and saved to a non-original path, but did not have their import data rewritten." + String("\n");
		report += "These files will not be re-imported when loading the project." + String("\n");
		report += get_to_string(failed_rewrite_md);
	}
	if (not_converted.size() > 0) {
		report += "------\n";
		report += "\nThe following files were not converted because support has not been implemented yet:" + String("\n");
		for (auto &info : not_converted) {
			String unsupported_format_type = info->get_unsupported_format_type();
			if (unsupported_format_type.is_empty()) {
				unsupported_format_type = info->get_path().get_extension();
			}
			report += info->get_path() + " ( importer: " + info->get_import_info()->get_importer() + ", type: " + info->get_import_info()->get_type() + ", format: " + unsupported_format_type + ") to " + info->get_new_source_path().get_file() + String("\n");
		}
	}
	if (failed.size() > 0) {
		String failed_report = "------\n";
		failed_report += "\nFailed conversions:" + String("\n");
		int count = 0;
		for (auto &fail : failed) {
			if (fail->get_exporter() == GDExtensionExporter::EXPORTER_NAME) {
				continue;
			}
			count++;
			failed_report += vformat("* %s\n", fail->get_source_path());
			auto splits = fail->get_message().split("\n");
			for (int i = 0; i < splits.size(); i++) {
				auto split = splits[i].strip_edges();
				if (split.is_empty()) {
					continue;
				}
				failed_report += "  * " + split + String("\n");
			}
			for (auto &msg : fail->get_message_detail()) {
				failed_report += "  * " + msg.strip_edges() + String("\n");
			}
			auto err_messages = fail->get_error_messages();
			if (!err_messages.is_empty()) {
				failed_report += "  * Errors:" + String("\n");
				for (auto &err : err_messages) {
					failed_report += "    " + err.replace("\n", " ").replace("\t", "  ") + String("\n");
				}
			}
			failed_report += "\n";
		}
		if (count != 0) {
			report += failed_report;
		}
	}
	return report;
}
String ImportExporterReport::get_editor_message_string() {
	String report = "";
	String version_text = ver->as_text();
	if (godotsteam_detected) {
		if (mono_detected) {
			version_text += " (Steam Mono edition)";
		} else {
			version_text += " (Steam edition)";
		}
	} else if (mono_detected) {
		version_text += " (Mono)";
	}
	report += "Use Godot editor version " + version_text + " to edit the project." + String("\n");
	if (godotsteam_detected) {
		report += "GodotSteam can be found here: https://github.com/CoaguCo-Industries/GodotSteam/releases \n";
	}
	report += "Note: the project may be using a custom version of Godot. Detection for this has not been implemented yet." + String("\n");
	report += "If you find that you have many non-import errors upon opening the project " + String("\n");
	report += "(i.e. scripts or shaders have many errors), use the original game's binary as the export template." + String("\n");

	return report;
}
String ImportExporterReport::get_detected_unsupported_resource_string() {
	String str = "";
	for (auto type : unsupported_types) {
		Vector<String> spl = type.split("%");
		str += vformat("Resource Type: %-20s Format: %-10s Importer: %s\n", spl[0], spl[1], spl[2]);
	}
	return str;
}

String ImportExporterReport::get_session_notes_string() {
	String report = "";
	Dictionary notes = get_session_notes();
	auto keys = notes.keys();
	if (keys.size() == 0) {
		return report;
	}
	report += String("\n");
	for (int i = 0; i < keys.size(); i++) {
		Dictionary note = notes[keys[i]];
		if (i > 0) {
			report += String("------\n");
		}
		String title = note["title"];
		String message = note["message"];
		report += title + ":" + String("\n");
		report += message + String("\n");
		PackedStringArray details = note["details"];
		for (int j = 0; j < details.size(); j++) {
			report += " - " + details[j] + String("\n");
		}
		report += String("\n");
	}
	return report;
}

String ImportExporterReport::get_log_file_location() {
	return log_file_location;
}

Vector<String> ImportExporterReport::get_decompiled_scripts() {
	return decompiled_scripts;
}

Vector<String> ImportExporterReport::get_failed_scripts() {
	return failed_scripts;
}

TypedArray<ImportInfo> iinfo_vector_to_typedarray(const Vector<Ref<ImportInfo>> &vec) {
	TypedArray<ImportInfo> arr;
	arr.resize(vec.size());
	for (int i = 0; i < vec.size(); i++) {
		arr.set(i, vec[i]);
	}
	return arr;
}

TypedArray<ExportReport> ImportExporterReport::get_successes() const {
	return vector_to_typed_array(success);
}

TypedArray<ExportReport> ImportExporterReport::get_failed() const {
	return vector_to_typed_array(failed);
}

TypedArray<ExportReport> ImportExporterReport::get_not_converted() const {
	return vector_to_typed_array(not_converted);
}

Vector<Ref<ExportReport>> ImportExporterReport::_get_lossy_imports() const {
	Vector<Ref<ExportReport>> vec;
	for (auto &report : success) {
		if (report->get_loss_type() != ImportInfo::LossType::LOSSLESS) {
			vec.push_back(report);
		}
	}
	return vec;
}

Vector<Ref<ExportReport>> ImportExporterReport::_get_rewrote_metadata() const {
	Vector<Ref<ExportReport>> vec;
	for (auto &report : success) {
		if (report->get_rewrote_metadata() == ExportReport::REWRITTEN) {
			vec.push_back(report);
		}
	}
	return vec;
}

Vector<Ref<ExportReport>> ImportExporterReport::_get_failed_rewrite_md() const {
	Vector<Ref<ExportReport>> vec;
	for (auto &report : success) {
		auto metadata_status = report->get_rewrote_metadata();
		if ((metadata_status == ExportReport::NOT_IMPORTABLE && report->get_import_info()->is_import()) || metadata_status == ExportReport::FAILED) {
			vec.push_back(report);
		}
	}
	return vec;
}

Vector<Ref<ExportReport>> ImportExporterReport::_get_failed_rewrite_md5() const {
	Vector<Ref<ExportReport>> vec;
	for (auto &report : success) {
		if (report->get_rewrote_metadata() == ExportReport::MD5_FAILED) {
			vec.push_back(report);
		}
	}
	return vec;
}

TypedArray<ExportReport> ImportExporterReport::get_lossy_imports() const {
	return vector_to_typed_array(_get_lossy_imports());
}

TypedArray<ExportReport> ImportExporterReport::get_rewrote_metadata() const {
	return vector_to_typed_array(_get_rewrote_metadata());
}

TypedArray<ExportReport> ImportExporterReport::get_failed_rewrite_md() const {
	return vector_to_typed_array(_get_failed_rewrite_md());
}

TypedArray<ExportReport> ImportExporterReport::get_failed_rewrite_md5() const {
	return vector_to_typed_array(_get_failed_rewrite_md5());
}

TypedArray<Dictionary> ImportExporterReport::get_downloaded_plugins() const {
	return vector_to_typed_array(downloaded_plugins);
}

Vector<String> ImportExporterReport::get_failed_plugin_cfg_create() const {
	return failed_plugin_cfg_create;
}

Vector<String> ImportExporterReport::get_failed_gdnative_copy() const {
	return failed_gdnative_copy;
}

bool ImportExporterReport::is_steam_detected() const {
	return godotsteam_detected;
}

bool ImportExporterReport::is_mono_detected() const {
	return mono_detected;
}

void ImportExporterReport::print_report() {
	print_line("\n\n********************************EXPORT REPORT********************************" + String("\n"));
	print_line(get_report_string());
	String notes = get_session_notes_string();
	if (!notes.is_empty()) {
		print_line("\n\n---------------------------------IMPORTANT NOTES----------------------------------" + String("\n"));
		print_line(notes);
	}
	print_line("\n------------------------------------------------------------------------------------" + String("\n"));
	print_line(get_editor_message_string());
	print_line("*******************************************************************************\n");
}

String ImportExporterReport::get_gdre_version() const {
	return gdre_version;
}

ImportExporterReport::ImportExporterReport() {
	set_ver("0.0.0");
	gdre_version = GDRESettings::get_gdre_version();
}

ImportExporterReport::ImportExporterReport(const String &p_ver, const String &p_game_name) {
	set_ver(p_ver);
	gdre_version = GDRESettings::get_gdre_version();
	game_name = p_game_name;
}

Dictionary ImportExporterReport::to_json() const {
	auto vec_to_json_array = [](const Vector<Ref<ExportReport>> &vec) -> Array {
		Array arr;
		for (auto &info : vec) {
			arr.append(info->to_json());
		}
		return arr;
	};
	Dictionary json;

	json["report_version"] = REPORT_VERSION;
	json["gdre_version"] = gdre_version;
	json["game_name"] = game_name;
	json["output_dir"] = output_dir;
	json["ver"] = ver->as_text();
	json["had_encryption_error"] = had_encryption_error;
	json["godotsteam_detected"] = godotsteam_detected;
	json["mono_detected"] = mono_detected;
	json["exported_scenes"] = exported_scenes;
	json["show_headless_warning"] = show_headless_warning;
	json["session_files_total"] = session_files_total;
	json["log_file_location"] = log_file_location;
	json["decompiled_scripts"] = decompiled_scripts;
	json["failed_scripts"] = failed_scripts;
	json["translation_export_message"] = translation_export_message;
	json["failed"] = vec_to_json_array(failed);
	json["success"] = vec_to_json_array(success);
	json["not_converted"] = vec_to_json_array(not_converted);
	json["failed_plugin_cfg_create"] = failed_plugin_cfg_create;
	json["failed_gdnative_copy"] = failed_gdnative_copy;
	json["unsupported_types"] = unsupported_types;
	json["downloaded_plugins"] = vector_to_typed_array(downloaded_plugins);
	return json;
}

String ImportExporterReport::_to_string() {
	return JSON::stringify(to_json(), "", false, true);
}

Ref<ImportExporterReport> ImportExporterReport::from_json(const Dictionary &p_json) {
	Ref<ImportExporterReport> report = memnew(ImportExporterReport);
	auto array_to_vec = [](const Array &arr) -> Vector<Ref<ExportReport>> {
		Vector<Ref<ExportReport>> vec;
		for (auto &info : arr) {
			if (info.get_type() != Variant::DICTIONARY) {
				continue;
			}
			Ref<ExportReport> report = ExportReport::from_json(info);
			if (report.is_valid()) {
				vec.push_back(report);
			}
		}
		return vec;
	};
	report->ver = GodotVer::parse(p_json.get("ver", "0.0.0"));
	report->game_name = p_json.get("game_name", "");
	report->gdre_version = p_json.get("gdre_version", "");
	report->godotsteam_detected = p_json.get("godotsteam_detected", false);
	report->mono_detected = p_json.get("mono_detected", false);
	report->exported_scenes = p_json.get("exported_scenes", false);
	report->show_headless_warning = p_json.get("show_headless_warning", false);
	report->session_files_total = p_json.get("session_files_total", 0);
	report->log_file_location = p_json.get("log_file_location", "");
	report->output_dir = p_json.get("output_dir", "");
	report->decompiled_scripts = p_json.get("decompiled_scripts", Vector<String>());
	report->failed_scripts = p_json.get("failed_scripts", Vector<String>());
	report->translation_export_message = p_json.get("translation_export_message", "");
	report->failed = array_to_vec(p_json.get("failed", Array()));
	report->success = array_to_vec(p_json.get("success", Array()));
	report->not_converted = array_to_vec(p_json.get("not_converted", Array()));
	report->failed_plugin_cfg_create = p_json.get("failed_plugin_cfg_create", Vector<String>());
	report->failed_gdnative_copy = p_json.get("failed_gdnative_copy", Vector<String>());
	report->unsupported_types = p_json.get("unsupported_types", Vector<String>());
	report->downloaded_plugins = array_to_vector<Dictionary>(p_json.get("downloaded_plugins", Array()));
	return report;
}

bool ImportExporterReport::is_equal_to(const Ref<ImportExporterReport> &p_import_exporter_report) const {
	if (p_import_exporter_report.is_null()) {
		return false;
	}
	if (gdre_version != p_import_exporter_report->gdre_version) {
		return false;
	}

	if (ver->as_text() != p_import_exporter_report->ver->as_text()) {
		return false;
	}
	auto export_report_vec_is_equal = [](const Vector<Ref<ExportReport>> &a, const Vector<Ref<ExportReport>> &b) -> bool {
		if (a.size() != b.size()) {
			return false;
		}
		for (int i = 0; i < a.size(); i++) {
			if (!a[i]->is_equal_to(b[i])) {
				return false;
			}
		}
		return true;
	};
	if (godotsteam_detected != p_import_exporter_report->godotsteam_detected) {
		return false;
	}
	if (mono_detected != p_import_exporter_report->mono_detected) {
		return false;
	}
	if (exported_scenes != p_import_exporter_report->exported_scenes) {
		return false;
	}
	if (show_headless_warning != p_import_exporter_report->show_headless_warning) {
		return false;
	}
	if (session_files_total != p_import_exporter_report->session_files_total) {
		return false;
	}
	if (log_file_location != p_import_exporter_report->log_file_location) {
		return false;
	}
	if (game_name != p_import_exporter_report->game_name) {
		return false;
	}
	if (output_dir != p_import_exporter_report->output_dir) {
		return false;
	}
	if (decompiled_scripts != p_import_exporter_report->decompiled_scripts) {
		return false;
	}
	if (failed_scripts != p_import_exporter_report->failed_scripts) {
		return false;
	}
	if (translation_export_message != p_import_exporter_report->translation_export_message) {
		return false;
	}
	if (!export_report_vec_is_equal(failed, p_import_exporter_report->failed)) {
		return false;
	}
	if (!export_report_vec_is_equal(success, p_import_exporter_report->success)) {
		return false;
	}
	if (!export_report_vec_is_equal(not_converted, p_import_exporter_report->not_converted)) {
		return false;
	}
	if (failed_plugin_cfg_create != p_import_exporter_report->failed_plugin_cfg_create) {
		return false;
	}
	if (failed_gdnative_copy != p_import_exporter_report->failed_gdnative_copy) {
		return false;
	}
	if (unsupported_types != p_import_exporter_report->unsupported_types) {
		return false;
	}
	if (downloaded_plugins != p_import_exporter_report->downloaded_plugins) {
		return false;
	}
	return true;
}

void ImportExporterReport::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_totals"), &ImportExporterReport::get_totals);
	ClassDB::bind_method(D_METHOD("get_unsupported_types"), &ImportExporterReport::get_unsupported_types);
	ClassDB::bind_method(D_METHOD("get_session_notes"), &ImportExporterReport::get_session_notes);
	ClassDB::bind_method(D_METHOD("get_totals_string"), &ImportExporterReport::get_totals_string);
	ClassDB::bind_method(D_METHOD("get_report_string"), &ImportExporterReport::get_report_string);
	ClassDB::bind_method(D_METHOD("get_detected_unsupported_resource_string"), &ImportExporterReport::get_detected_unsupported_resource_string);
	ClassDB::bind_method(D_METHOD("get_session_notes_string"), &ImportExporterReport::get_session_notes_string);
	ClassDB::bind_method(D_METHOD("get_editor_message_string"), &ImportExporterReport::get_editor_message_string);
	ClassDB::bind_method(D_METHOD("get_log_file_location"), &ImportExporterReport::get_log_file_location);
	ClassDB::bind_method(D_METHOD("get_decompiled_scripts"), &ImportExporterReport::get_decompiled_scripts);
	ClassDB::bind_method(D_METHOD("get_failed_scripts"), &ImportExporterReport::get_failed_scripts);
	ClassDB::bind_method(D_METHOD("get_successes"), &ImportExporterReport::get_successes);
	ClassDB::bind_method(D_METHOD("get_failed"), &ImportExporterReport::get_failed);
	ClassDB::bind_method(D_METHOD("get_not_converted"), &ImportExporterReport::get_not_converted);
	ClassDB::bind_method(D_METHOD("get_lossy_imports"), &ImportExporterReport::get_lossy_imports);
	ClassDB::bind_method(D_METHOD("get_rewrote_metadata"), &ImportExporterReport::get_rewrote_metadata);
	ClassDB::bind_method(D_METHOD("get_failed_rewrite_md"), &ImportExporterReport::get_failed_rewrite_md);
	ClassDB::bind_method(D_METHOD("get_failed_rewrite_md5"), &ImportExporterReport::get_failed_rewrite_md5);
	ClassDB::bind_method(D_METHOD("get_failed_plugin_cfg_create"), &ImportExporterReport::get_failed_plugin_cfg_create);
	ClassDB::bind_method(D_METHOD("get_failed_gdnative_copy"), &ImportExporterReport::get_failed_gdnative_copy);
	ClassDB::bind_method(D_METHOD("get_report_sections"), &ImportExporterReport::get_report_sections);
	ClassDB::bind_method(D_METHOD("get_section_labels"), &ImportExporterReport::get_section_labels);
	ClassDB::bind_method(D_METHOD("print_report"), &ImportExporterReport::print_report);
	ClassDB::bind_method(D_METHOD("set_ver", "ver"), &ImportExporterReport::set_ver);
	ClassDB::bind_method(D_METHOD("get_ver"), &ImportExporterReport::get_ver);
	ClassDB::bind_method(D_METHOD("is_steam_detected"), &ImportExporterReport::is_steam_detected);
	ClassDB::bind_method(D_METHOD("is_mono_detected"), &ImportExporterReport::is_mono_detected);
}
