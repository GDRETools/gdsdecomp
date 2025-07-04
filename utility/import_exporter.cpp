
#include "import_exporter.h"

#include "bytecode/bytecode_base.h"
#include "compat/oggstr_loader_compat.h"
#include "compat/resource_loader_compat.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"
#include "exporters/export_report.h"
#include "exporters/resource_exporter.h"
#include "gdre_logger.h"
#include "utility/common.h"
#include "utility/gdre_config.h"
#include "utility/gdre_settings.h"
#include "utility/glob.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "thirdparty/minimp3/minimp3_ex.h"
#include "utility/import_info.h"

#include <compat/script_loader.h>
#include <utility/gdre_standalone.h>

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

/**
Sort the scenes so that they are exported last
 */
namespace {
static FileNoCaseComparator file_no_case_comparator;
struct ReportComparator {
	bool operator()(const Ref<ExportReport> &a, const Ref<ExportReport> &b) const {
		return file_no_case_comparator(a->get_import_info()->get_source_file(), b->get_import_info()->get_source_file());
	}
};
} //namespace

// Error remove_remap(const String &src, const String &dst, const String &output_dir);
Error ImportExporter::handle_auto_converted_file(const String &autoconverted_file) {
	String prefix = autoconverted_file.replace_first("res://", "");
	if (!prefix.begins_with(".")) {
		String old_path = output_dir.path_join(prefix);
		if (FileAccess::exists(old_path)) {
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

void _save_filesystem_cache(const Vector<Ref<ExportReport>> &reports, Ref<FileAccess> p_file) {
	String current_dir = "";
	auto curr_time = OS::get_singleton()->get_unix_time();
	bool is_v4_4_or_newer = get_ver_major() > 4 || (get_ver_major() == 4 && get_ver_minor() >= 4);
	for (auto &report : reports) {
		String source_file = report->get_import_info()->get_source_file();
		String base_dir = source_file.get_base_dir();
		if (base_dir != "res://") {
			base_dir += "/";
		}
		if (base_dir != current_dir) {
			current_dir = base_dir;
			p_file->store_line("::" + base_dir + "::" + String::num_int64(curr_time));
		}

		// const EditorFileSystemDirectory::FileInfo *file_info = p_dir->files[i];
		// if (!file_info->import_group_file.is_empty()) {
		// 	group_file_cache.insert(file_info->import_group_file);
		// }

		String type = report->actual_type;
		if (!report->script_class.is_empty()) {
			type += "/" + String(report->script_class);
		}

		PackedStringArray cache_string;
		cache_string.append(source_file.get_file());
		cache_string.append(type);
		cache_string.append(itos(ResourceUID::get_singleton()->text_to_id(report->get_import_info()->get_uid())));
		cache_string.append(itos(report->modified_time));
		cache_string.append(itos(report->import_modified_time));
		cache_string.append(itos(1)); // TODO?
		cache_string.append("");
		Vector<String> parts = {
			"",
			"",
			"",
		};
		if (is_v4_4_or_newer) {
			parts = { "", "", "", itos(0), itos(0), report->import_md5, String("<*>").join(report->get_import_info()->get_dest_files()) };
		}
		cache_string.append(String("<>").join(parts));
		cache_string.append(String("<>").join(report->dependencies));

		p_file->store_line(String("::").join(cache_string));
	}
}

void save_filesystem_cache(const Vector<Ref<ExportReport>> &reports, String output_dir, bool is_partial_export) {
	if (get_ver_major() <= 3) {
		return;
	}
	String cache_path = get_ver_minor() < 4 ? "filesystem_cache8" : "filesystem_cache10";
	String editor_dir = output_dir.path_join(".godot").path_join("editor");
	gdre::ensure_dir(editor_dir);
	String cache_file = editor_dir.path_join(cache_path);
	if (is_partial_export && FileAccess::exists(cache_file)) {
		return;
	}
	Ref<FileAccess> f = FileAccess::open(cache_file, FileAccess::WRITE);
	//write an md5 hash of all 0s
	f->store_line(String("00000000000000000000000000000000"));
	_save_filesystem_cache(reports, f);
}

void ImportExporter::rewrite_metadata(ExportToken &token) {
	auto &report = token.report;
	ERR_FAIL_COND_MSG(report.is_null(), "Cannot rewrite metadata for null report");
	Error err = report->get_error();
	auto iinfo = report->get_import_info();
	auto if_err_func = [&]() {
		if (err != OK) {
			report->set_rewrote_metadata(ExportReport::FAILED);
		} else {
			report->set_rewrote_metadata(ExportReport::REWRITTEN);
		}
	};
	String new_md_path = output_dir.path_join(iinfo->get_import_md_path().replace("res://", ""));

	if (report->get_rewrote_metadata() == ExportReport::NOT_IMPORTABLE || !iinfo->is_import()) {
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
	bool export_matches_source = report->get_source_path() == report->get_new_source_path();
	if (err == OK && (not_in_res_tree || !export_matches_source)) {
		if (iinfo->get_ver_major() <= 2 && opt_rewrite_imd_v2) {
			// TODO: handle v2 imports with more than one source, like atlas textures
			err = rewrite_import_source(report->get_new_source_path(), iinfo);
			if_err_func();
		} else if (not_in_res_tree && iinfo->get_ver_major() >= 3 && opt_rewrite_imd_v3 && (iinfo->get_source_file().find(report->get_new_source_path().replace("res://", "")) != -1)) {
			// Currently, we only rewrite the import data for v3 if the source file was somehow recorded as an absolute file path,
			// But is still in the project structure
			err = rewrite_import_source(report->get_new_source_path(), iinfo);
			if_err_func();
		} else if (iinfo->is_dirty()) {
			err = iinfo->save_to(new_md_path);
			if (err != OK) {
				report->set_rewrote_metadata(ExportReport::FAILED);
			} else if (!export_matches_source) {
				report->set_rewrote_metadata(ExportReport::NOT_IMPORTABLE);
			}
		}
	} else if (iinfo->is_dirty()) {
		if (err == OK) {
			err = iinfo->save_to(new_md_path);
			if_err_func();
		} else {
			report->set_rewrote_metadata(ExportReport::NOT_IMPORTABLE);
		}
	} else {
		report->set_rewrote_metadata(ExportReport::NOT_DIRTY);
	}
	auto mdat = report->get_rewrote_metadata();
	if (mdat == ExportReport::FAILED || mdat == ExportReport::NOT_IMPORTABLE) {
		return;
	}
	if (opt_write_md5_files && err == OK && iinfo->get_ver_major() > 2) {
		err = ERR_LINK_FAILED;
		Ref<ImportInfoModern> modern_iinfo = iinfo;
		if (modern_iinfo.is_valid()) {
			err = modern_iinfo->save_md5_file(output_dir);
		}
		if (err) {
			report->set_rewrote_metadata(ExportReport::MD5_FAILED);
		}
	}
	if (!err && iinfo->get_ver_major() >= 4 && export_matches_source && report->get_rewrote_metadata() != ExportReport::NOT_IMPORTABLE) {
		String resource_script_class;
		List<String> deps;
		auto path = iinfo->get_path();
		auto res_info = ResourceCompatLoader::get_resource_info(path);
		report->actual_type = res_info.is_valid() ? res_info->type : iinfo->get_type();
		report->script_class = res_info.is_valid() ? res_info->script_class : "";
		ResourceCompatLoader::get_dependencies(path, &deps, false);
		for (auto &dep : deps) {
			report->dependencies.push_back(dep);
		}
		report->import_md5 = FileAccess::get_md5(new_md_path);
		report->import_modified_time = FileAccess::get_modified_time(new_md_path);
		report->modified_time = FileAccess::get_modified_time(report->get_saved_path());
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
	Error err = da->copy_dir(tmp_dir, output);
	ERR_FAIL_COND_V_MSG(err != OK, ERR_FILE_CANT_WRITE, "Failed to copy GodotSteam files to " + output_dir);
	gdre::rimraf(parent_tmp_dir);
	da->remove(zip_path);
	return OK;
}

void ImportExporter::_do_export(uint32_t i, ExportToken *tokens) {
	// Taken care of in the main thread
	if (unlikely(cancelled)) {
		return;
	}
	auto &token = tokens[i];
	bool has_file = false;
	auto dest_files = token.iinfo->get_dest_files();
	for (const String &dest : dest_files) {
		if (GDRESettings::get_singleton()->has_path_loaded(dest)) {
			has_file = true;
			break;
		}
	}
	if (!has_file) {
		token.report.instantiate(token.iinfo);
		token.report->set_error(ERR_FILE_NOT_FOUND);
		token.report->set_message("No existing resources found for this import");
		token.report->append_message_detail({ "Possibles:" });
		token.report->append_message_detail(dest_files);
		last_completed++;
		return;
	}

	tokens[i].report = Exporter::export_resource(output_dir, tokens[i].iinfo);
	rewrite_metadata(tokens[i]);
	if (tokens[i].supports_multithread) {
		tokens[i].report->append_error_messages(GDRELogger::get_thread_errors());
	} else {
		tokens[i].report->append_error_messages(GDRELogger::get_errors());
	}
	last_completed++;
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
		err = TaskManager::get_singleton()->run_task_on_current_thread(
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
				f->store_string(ResourceUID::get_singleton()->id_to_text(uid));
			}
		}
	}
}

// export all the imported resources
Error ImportExporter::export_imports(const String &p_out_dir, const Vector<String> &_files_to_export) {
	reset_log();
	ResourceCompatLoader::make_globally_available();
	ResourceCompatLoader::set_default_gltf_load(false);
	report = Ref<ImportExporterReport>(memnew(ImportExporterReport(get_settings()->get_version_string())));
	report->log_file_location = get_settings()->get_log_file_path();
	ERR_FAIL_COND_V_MSG(!get_settings()->is_pack_loaded(), ERR_DOES_NOT_EXIST, "pack/dir not loaded!");
	output_dir = !p_out_dir.is_empty() ? p_out_dir : get_settings()->get_project_path();
	Error err = OK;
	// TODO: make this use "copy"
	Array _files = get_settings()->get_import_files();
	if (_files.size() == 0) {
		WARN_PRINT("No import files found!");
		return OK;
	}
	bool partial_export = (_files_to_export.size() > 0 && _files_to_export.size() != get_settings()->get_file_count());
	size_t export_files_count = partial_export ? _files_to_export.size() : _files.size();
	const Vector<String> files_to_export = partial_export ? _files_to_export : get_settings()->get_file_list();
	Ref<EditorProgressGDDC> pr = memnew(EditorProgressGDDC("export_imports", "Exporting resources...", export_files_count, true));

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

	Ref<DirAccess> dir = DirAccess::open(output_dir);
	Vector<String> addon_first_level_dirs = Glob::glob("res://addons/*", true);
	if (addon_first_level_dirs.size() > 0) {
		if (partial_export) {
			addon_first_level_dirs = Glob::dirs_in_names(files_to_export, addon_first_level_dirs);
		}
		recreate_plugin_configs(addon_first_level_dirs);
	}

	if (pr->step("Exporting resources...", 0, true)) {
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
	Vector<ExportToken> tokens;
	Vector<ExportToken> non_multithreaded_tokens;
	HashSet<String> files_to_export_set = vector_to_hashset(files_to_export);
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
		bool supports_multithreading = !GDREConfig::get_singleton()->get_setting("force_single_threaded", false);
		bool is_high_priority = importer == "gdextension" || importer == "gdnative";
		if (exporter_map.has(importer)) {
			if (!exporter_map.get(importer)->supports_multithread()) {
				supports_multithreading = false;
			}
		} else {
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
				tokens.push_back({ iinfo, nullptr, supports_multithreading });
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
		last_completed = -1;
		cancelled = false;
		Error err = TaskManager::get_singleton()->run_multithreaded_group_task(
				this,
				&ImportExporter::_do_export,
				tokens.ptrw(),
				tokens.size(),
				&ImportExporter::get_export_token_description,
				"ImportExporter::export_imports",
				"Exporting resources...",
				true, -1, true, pr, 0);
		if (err != OK) {
			print_line("Export cancelled!");
			return err;
		}
	}
	GDRELogger::clear_error_queues();
	err = TaskManager::get_singleton()->run_task_on_current_thread(
			this,
			&ImportExporter::_do_export,
			non_multithreaded_tokens.ptrw(),
			non_multithreaded_tokens.size(),
			&ImportExporter::get_export_token_description,
			"ImportExporter::export_imports",
			"Exporting resources...",
			true, pr, num_multithreaded_tokens);

	if (err != OK) {
		print_line("Export cancelled!");
		return err;
	}
	// err = _reexport_translations(non_multithreaded_tokens, tokens.size(), pr);
	// if (err != OK) {
	// 	print_line("Export cancelled!");
	// 	return err;
	// }
	tokens.append_array(non_multithreaded_tokens);
	pr->step("Finalizing...", tokens.size() - 1, true);
	report->session_files_total = tokens.size();
	// add to report
	bool has_remaps = GDRESettings::get_singleton()->has_any_remaps();
	HashSet<String> success_paths;
	for (int i = 0; i < tokens.size(); i++) {
		const ExportToken &token = tokens[i];
		Ref<ImportInfo> iinfo = token.iinfo;
		String src_ext = iinfo->get_source_file().get_extension().to_lower();
		Ref<ExportReport> ret = token.report;
		if (ret.is_null()) {
			ERR_PRINT("Exporter returned null report for " + iinfo->get_path());
			report->failed.push_back(ret);
			continue;
		}
		err = ret->get_error();
		if (err == ERR_SKIP) {
			report->not_converted.push_back(ret);
			continue;
		} else if (err == ERR_UNAVAILABLE) {
			String type = iinfo->get_type();
			String format_type = src_ext;
			if (ret->get_unsupported_format_type() != "") {
				format_type = ret->get_unsupported_format_type();
			}
			report_unsupported_resource(type, format_type, iinfo->get_path());
			report->not_converted.push_back(ret);
			continue;
		} else if (err != OK) {
			if (iinfo->get_importer() == "script_bytecode") {
				report->failed_scripts.push_back(iinfo->get_path());
				if (err == ERR_UNAUTHORIZED) {
					report->had_encryption_error = true;
				}
			}
			report->failed.push_back(ret);
			print_verbose("Failed to convert " + iinfo->get_type() + " resource " + iinfo->get_path());
			continue;
		}
		if (iinfo->get_importer() == "scene" && src_ext != "escn" && src_ext != "tscn") {
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
		} else if (iinfo->get_importer() == "csv_translation" || iinfo->get_importer() == "translation_csv" || iinfo->get_importer() == "translation") {
			report->translation_export_message += ret->get_message();
		} else if (iinfo->get_importer() == "script_bytecode") {
			report->decompiled_scripts.push_back(iinfo->get_path());
			// 4.4 and higher have uid files for scripts that we have to recreate
			if ((get_ver_major() == 4 && get_ver_minor() >= 4) || get_ver_major() > 4) {
				recreate_uid_file(iinfo->get_source_file(), true, files_to_export_set);
			}
		}
		// ***** Record export result *****
		auto metadata_status = ret->get_rewrote_metadata();
		// the following are successful exports, but we failed to rewrite metadata or write md5 files
		if (metadata_status == ExportReport::REWRITTEN) {
			report->rewrote_metadata.push_back(ret);
		} else if (metadata_status == ExportReport::NOT_IMPORTABLE || metadata_status == ExportReport::FAILED) {
			// necessary to rewrite import metadata but failed to do so
			report->failed_rewrite_md.push_back(ret);
		} else if (metadata_status == ExportReport::MD5_FAILED) {
			report->failed_rewrite_md5.push_back(ret);
		}
		if (ret->get_loss_type() != ImportInfo::LOSSLESS) {
			report->lossy_imports.push_back(ret);
		}
		if (iinfo->get_importer() == "gdextension" || iinfo->get_importer() == "gdnative") {
			if (!ret->get_message().is_empty()) {
				report->failed_gdnative_copy.push_back(ret->get_message());
			} else if (!ret->get_saved_path().is_empty() && ret->get_download_task_id() != -1) {
				Ref<ImportInfoGDExt> iinfo_gdext = iinfo;
				Error err = TaskManager::get_singleton()->wait_for_download_task_completion(ret->get_download_task_id());
				if (err != OK) {
					report->failed_gdnative_copy.push_back(ret->get_saved_path());
					continue;
				}
				if (!iinfo_gdext.is_valid()) {
					// wtf?
					ERR_PRINT("Invalid ImportInfoGDExt");
					report->failed_gdnative_copy.push_back(ret->get_saved_path());
					continue;
				}
				err = unzip_and_copy_addon(iinfo, ret->get_saved_path());
				if (err != OK) {
					report->failed_gdnative_copy.push_back(ret->get_saved_path());
					continue;
				}
			}
		}
		report->success.push_back(ret);
		success_paths.insert(iinfo->get_export_dest());
	}

	// remove remaps
	if (has_remaps) {
		for (auto &token : tokens) {
			auto &iinfo = token.iinfo;
			auto err = token.report.is_null() ? ERR_BUG : token.report->get_error();
			if (iinfo.is_valid() && !err) {
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
		auto non_custom_uid_files = get_settings()->get_file_list({ "*.gd", "*.gdshader", "*.shader", "*.cs" });
		for (int i = 0; i < non_custom_uid_files.size(); i++) {
			recreate_uid_file(non_custom_uid_files[i], false, files_to_export_set);
		}
	}

	if (get_settings()->is_project_config_loaded()) { // some pcks do not have project configs
		if (get_settings()->save_project_config(output_dir) != OK) {
			print_line("ERROR: Failed to save project config!");
		} else {
			print_line("Saved project config.");
			// Remove binary project config, as editors will load from it instead of the text one
			dir->remove(get_settings()->get_project_config_path().get_file());
		}
	}
	pr = nullptr;
	report->print_report();
	ResourceCompatLoader::set_default_gltf_load(false);
	ResourceCompatLoader::unmake_globally_available();
	// check if the .tmp directory is empty
	if (gdre::dir_is_empty(output_dir.path_join(".tmp"))) {
		dir->remove(output_dir.path_join(".tmp"));
	}
	// 4.1 and higher have a filesystem cache
	if (get_ver_major() >= 4 && !(get_ver_minor() < 1 && get_ver_major() == 4)) {
		Vector<Ref<ExportReport>> reports;
		for (auto &token : tokens) {
			if (token.report->modified_time > 0) {
				reports.push_back(token.report);
			}
		}
		reports.sort_custom<ReportComparator>();
		save_filesystem_cache(reports, output_dir, partial_export);
	}
	return OK;
}

Error ImportExporter::recreate_plugin_config(const String &plugin_dir) {
	Error err;
	if (GDRESettings::get_singleton()->get_bytecode_revision() == 0) {
		return ERR_UNCONFIGURED;
	}
	static const Vector<String> wildcards = { "*.gdc", "*.gde", "*.gd" };
	String rel_plugin_path = String("addons").path_join(plugin_dir);
	auto gd_scripts = gdre::get_recursive_dir_list(String("res://").path_join(rel_plugin_path), wildcards, false);
	String main_script;

	if (gd_scripts.is_empty()) {
		return OK;
	}

	bool tool_scripts_found = false;
	bool cant_decompile = false;

	for (int j = 0; j < gd_scripts.size(); j++) {
		auto ext = gd_scripts[j].get_extension().to_lower();
		if ((ext == "gde" || ext == "gdc") && GDRESettings::get_singleton()->get_bytecode_revision() == 0) {
			cant_decompile = true;
			continue;
		}
		String gd_script_abs_path = String("res://").path_join(rel_plugin_path).path_join(gd_scripts[j]);
		Ref<FakeGDScript> gd_script = ResourceCompatLoader::non_global_load(gd_script_abs_path, "", &err);
		if (gd_script.is_valid()) {
			if (gd_script->get_instance_base_type() == "EditorPlugin") {
				main_script = gd_scripts[j].get_basename() + ".gd";
				break;
			}
			if (gd_script->is_tool()) {
				tool_scripts_found = true;
			}
		}
	}
	if (main_script == "") {
		// No tool scripts found, this is not a plugin
		if (!tool_scripts_found && !cant_decompile) {
			return OK;
		}
		return ERR_UNAVAILABLE;
	}
	String plugin_cfg_text = String("[plugin]\n\n") +
			"name=\"" + plugin_dir.replace("_", " ").replace(".", " ") + "\"\n" +
			"description=\"" + plugin_dir.replace("_", " ").replace(".", " ") + " plugin\"\n" +
			"author=\"Unknown\"\n" +
			"version=\"1.0\"\n" +
			"script=\"" + main_script + "\"";
	String output_plugin_path = output_dir.path_join(rel_plugin_path);
	gdre::ensure_dir(output_plugin_path);
	Ref<FileAccess> f = FileAccess::open(output_plugin_path.path_join("plugin.cfg"), FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err || f.is_null(), ERR_FILE_CANT_WRITE, "can't open plugin.cfg for writing");
	ERR_FAIL_COND_V_MSG(!f->store_string(plugin_cfg_text), ERR_FILE_CANT_WRITE, "can't write plugin.cfg");
	print_verbose("Recreated plugin config for " + plugin_dir);
	return OK;
}

// Recreates the "plugin.cfg" files for each plugin to avoid loading errors.
Error ImportExporter::recreate_plugin_configs(const Vector<String> &plugin_dirs) {
	Error err;
	if (!DirAccess::exists("res://addons")) {
		return OK;
	}
	print_line("Recreating plugin configs...");
	Vector<String> dirs;
	if (plugin_dirs.is_empty()) {
		dirs = Glob::glob("res://addons/*", true);
	} else {
		dirs = plugin_dirs;
		for (int i = 0; i < dirs.size(); i++) {
			if (!dirs[i].is_absolute_path()) {
				dirs.write[i] = String("res://addons/").path_join(dirs[i]);
			}
		}
	}
	for (int i = 0; i < dirs.size(); i++) {
		String path = dirs[i];
		if (!DirAccess::dir_exists_absolute(path)) {
			continue;
		}
		String dir = dirs[i].get_file();
		err = recreate_plugin_config(dir);
		if (err) {
			WARN_PRINT("Failed to recreate plugin.cfg for " + dir);
			report->failed_plugin_cfg_create.push_back(dir);
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
			new_import->set_params(iinfo->get_params());
			String new_import_file = output_dir.path_join(dest_files[i].replace("res://", ""));
			new_import->set_source_and_md5(new_import_file, source_md5);
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

void ImportExporter::report_unsupported_resource(const String &type, const String &format_name, const String &import_path) {
	String type_format_str = type + "%" + format_name.to_lower();
	if (report->unsupported_types.find(type_format_str) == -1) {
		WARN_PRINT("Conversion for Resource of type " + type + " and format " + format_name + " not implemented for " + import_path);
		report->unsupported_types.push_back(type_format_str);
	}
	print_verbose("Did not convert " + type + " resource " + import_path);
}

void ImportExporter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("export_imports", "p_out_dir", "files_to_export"), &ImportExporter::export_imports, DEFVAL(""), DEFVAL(PackedStringArray()));
	ClassDB::bind_method(D_METHOD("get_report"), &ImportExporter::get_report);
	ClassDB::bind_method(D_METHOD("reset"), &ImportExporter::reset);
}

void ImportExporter::reset_log() {
	report = Ref<ImportExporterReport>(memnew(ImportExporterReport));
}

void ImportExporter::reset() {
	opt_bin2text = true;
	opt_export_textures = true;
	opt_export_samples = true;
	opt_export_ogg = true;
	opt_export_mp3 = true;
	opt_lossy = true;
	opt_export_jpg = true;
	opt_export_webp = true;
	opt_rewrite_imd_v2 = true;
	opt_rewrite_imd_v3 = true;
	opt_decompile = true;
	opt_only_decompile = false;
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
	String unsup = get_detected_unsupported_resource_string();
	if (!unsup.is_empty()) {
		Dictionary unsupported;
		unsupported["title"] = "Unsupported Resources Detected";
		String message = "The following resource types were detected in the project that conversion is not implemented for yet.\n";
		message += "See Export Report to see which resources were not exported.\n";
		message += "You will still be able to edit the project in the editor regardless.";
		unsupported["message"] = message;
		PackedStringArray list;
		for (int i = 0; i < unsupported_types.size(); i++) {
			auto split = unsupported_types[i].split("%");
			auto str = "Resource Type: " + split[0] + ", Format: " + split[1];
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
	return notes;
}

String ImportExporterReport::get_totals_string() {
	String report = "";
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
		dict[vec[i]->get_path()] = vec[i]->get_new_source_path();
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
	return labels;
}

Dictionary ImportExporterReport::get_report_sections() {
	Dictionary sections;
	// sections["totals"] = get_totals();
	// sections["unsupported_types"] = get_unsupported_types();
	// sections["session_notes"] = get_session_notes();

	if (!failed.is_empty()) {
		sections["failed"] = Dictionary();
		Dictionary failed_dict = sections["failed"];
		add_to_dict(failed_dict, failed);
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
	if (lossy_imports.size() > 0) {
		if (!opt_lossy) {
			report += "\nThe following files were not converted from a lossy import." + String("\n");
			report += get_failed_section_string(lossy_imports);
		}
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
			report += info->get_path() + " ( importer: " + info->get_import_info()->get_importer() + ", type: " + info->get_import_info()->get_type() + ", format: " + info->get_unsupported_format_type() + ") to " + info->get_new_source_path().get_file() + String("\n");
		}
	}
	if (failed.size() > 0) {
		report += "------\n";
		report += "\nFailed conversions:" + String("\n");
		for (auto &fail : failed) {
			report += vformat("* %s\n", fail->get_source_path());
			auto splits = fail->get_message().split("\n");
			for (int i = 0; i < splits.size(); i++) {
				auto split = splits[i].strip_edges();
				if (split.is_empty()) {
					continue;
				}
				report += "  * " + split + String("\n");
			}
			for (auto &msg : fail->get_message_detail()) {
				report += "  * " + msg.strip_edges() + String("\n");
			}
			auto err_messages = fail->get_error_messages();
			if (!err_messages.is_empty()) {
				report += "  * Errors:" + String("\n");
				for (auto &err : err_messages) {
					err = err.strip_edges();
					report += "\t\t-\t" + err.replace("\n", " ").replace("\t", "  ") + String("\n");
				}
			}
			report += "\n";
		}
	}
	return report;
}
String ImportExporterReport::get_editor_message_string() {
	String report = "";
	report += "Use Godot editor version " + ver->as_text() + String(godotsteam_detected ? " (steam version)" : "") + " to edit the project." + String("\n");
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
		str += vformat("Type: %-20s", spl[0]) + "\tFormat: " + spl[1] + "\n";
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

TypedArray<ImportInfo> ImportExporterReport::get_successes() const {
	return vector_to_typed_array(success);
}

TypedArray<ImportInfo> ImportExporterReport::get_failed() const {
	return vector_to_typed_array(failed);
}

TypedArray<ImportInfo> ImportExporterReport::get_not_converted() const {
	return vector_to_typed_array(not_converted);
}

TypedArray<ImportInfo> ImportExporterReport::get_lossy_imports() const {
	return vector_to_typed_array(lossy_imports);
}

TypedArray<ImportInfo> ImportExporterReport::get_rewrote_metadata() const {
	return vector_to_typed_array(rewrote_metadata);
}

TypedArray<ImportInfo> ImportExporterReport::get_failed_rewrite_md() const {
	return vector_to_typed_array(failed_rewrite_md);
}

TypedArray<ImportInfo> ImportExporterReport::get_failed_rewrite_md5() const {
	return vector_to_typed_array(failed_rewrite_md5);
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
}
