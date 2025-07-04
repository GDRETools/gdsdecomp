#include "gdextension_exporter.h"
#include "core/os/os.h"
#include "core/os/shared_object.h"
#include "exporters/export_report.h"
#include "utility/common.h"
#include "utility/gdre_config.h"
#include "utility/gdre_settings.h"
#include "utility/glob.h"
#include "utility/import_info.h"
#include "utility/plugin_manager.h"
#include "utility/task_manager.h"

Error GDExtensionExporter::export_file(const String &p_dest_path, const String &p_src_path) {
	auto report = export_resource(p_dest_path.get_base_dir(), ImportInfo::load_from_file(p_src_path));
	return report->get_error();
}

#define GDExt_ERR_FAIL_COND_V_MSG(err, ret, msg) \
	{                                            \
		if (err) {                               \
			ret->set_error(err);                 \
			ret->set_message(msg);               \
			ERR_FAIL_V_MSG(ret, msg);            \
		}                                        \
	}

Error find_libs(const Vector<SharedObject> &libs, HashMap<String, SharedObject> &lib_paths) {
	if (libs.size() == 0) {
		return OK;
	}
	HashSet<String> libs_to_find;
	String parent_dir = GDRESettings::get_singleton()->get_pack_path().get_base_dir();
	Error err;
	for (const SharedObject &so : libs) {
		libs_to_find.insert(so.path.get_file());
	}
	{
		Ref<DirAccess> da = DirAccess::open(parent_dir, &err);
		ERR_FAIL_COND_V_MSG(err, ERR_FILE_CANT_OPEN, "Failed to open directory " + parent_dir);
		da->list_dir_begin();
		String f = da->get_next();
		while (!f.is_empty()) {
			if (f != "." && f != "..") {
				for (const SharedObject &so : libs) {
					if (so.path.get_file().to_lower() == f.to_lower()) {
						lib_paths[parent_dir.path_join(f)] = so;
					}
				}
			}
			f = da->get_next();
		}
	}
	if (lib_paths.size() == 0) {
		// if we're on MacOS, try one path up
		if (parent_dir.get_file() == "Resources") {
			parent_dir = parent_dir.get_base_dir();
			Vector<String> globs;
			for (String lib : libs_to_find) {
				globs.push_back(parent_dir.path_join("**").path_join(lib));
			}
			auto paths = Glob::rglob_list(globs, true);
			for (String path : paths) {
				for (const SharedObject &so : libs) {
					if (so.path.get_file().to_lower() == path.get_file().to_lower()) {
						lib_paths[path] = so;
					}
				}
			}
		}
	}
	if (lib_paths.size() == 0) {
		return ERR_BUG;
	}

	return OK;
}

Error copy_libs(const String &output_dir, const HashMap<String, SharedObject> &lib_paths) {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	for (const auto &E : lib_paths) {
		const auto &so = E.value;
		String dest_path = output_dir.path_join(so.path.replace("res://", ""));
		Error err = gdre::ensure_dir(dest_path.get_base_dir());
		ERR_FAIL_COND_V_MSG(err, ERR_FILE_CANT_WRITE, "Failed to create directory for library " + dest_path);
		if (da->file_exists(E.key)) {
			err = da->copy(E.key, dest_path);
		} else {
			err = da->copy_dir(E.key, dest_path);
		}
		ERR_FAIL_COND_V_MSG(err, ERR_FILE_CANT_WRITE, "Failed to copy library " + E.key + " to " + dest_path);
	}
	return OK;
}

String get_plugin_name(const Ref<ImportInfo> import_infos) {
	String rel_path = import_infos->get_import_md_path().simplify_path().replace("res://", "");
	String plugin_name;
	if (rel_path.begins_with("addons/")) {
		plugin_name = rel_path.replace("addons/", "").split("/")[0];
	} else {
		plugin_name = rel_path.get_file().get_basename();
	}
	return plugin_name;
}

bool libs_has_platform(const Vector<SharedObject> &libs, const String &platform) {
	for (const SharedObject &so : libs) {
		if (so.tags.has(platform)) {
			return true;
		}
	}
	return false;
}

Ref<ExportReport> GDExtensionExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	// get the first name of the plugin after res://addons
	String plugin_name = get_plugin_name(import_infos);
	Ref<ExportReport> report = memnew(ExportReport(import_infos));
	Ref<ImportInfoGDExt> iinfo = import_infos;
	String platform = OS::get_singleton()->get_name().to_lower();
	String parent_dir = GDRESettings::get_singleton()->get_pack_path().get_base_dir();

	auto libs = iinfo->get_libaries();
	HashMap<String, SharedObject> lib_paths;
	Error err = OK;
	err = find_libs(libs, lib_paths);
	GDExt_ERR_FAIL_COND_V_MSG(err, report, "Failed to find gdextension libraries for plugin " + import_infos->get_import_md_path());
	auto deps = iinfo->get_dependencies();
	HashMap<String, SharedObject> dep_paths;
	bool found_deps = true;
	err = find_libs(iinfo->get_dependencies(), dep_paths);
	if (err) {
		found_deps = false;
		err = OK;
	}
	bool downloaded_plugin = false;
	if (GDREConfig::get_singleton()->get_setting("download_plugins")) {
		Vector<String> hashes;
		for (const auto &E : lib_paths) {
			// TODO: come up with a way of consistently hashing signed macos binaries
			if (E.value.tags.has("macos")) {
				continue;
			}
			auto md5 = gdre::get_md5(E.key, true);
			if (!md5.is_empty()) {
				hashes.push_back(md5);
			}
		}
		if (!hashes.is_empty()) {
			String url = PluginManager::get_plugin_download_url(plugin_name, hashes);
			if (TaskManager::get_singleton()->is_current_group_task_canceled()) {
				report->set_error(ERR_SKIP);
				return report;
			}
			if (!url.is_empty()) {
				String zip_path = output_dir.path_join(".tmp").path_join(plugin_name + ".zip");
				auto task_id = TaskManager::get_singleton()->add_download_task(url, zip_path);
				report->set_download_task_id(task_id);
				report->set_saved_path(zip_path);
				return report;
			}
		}
	}

	if (!downloaded_plugin) {
		err = copy_libs(output_dir, lib_paths);
		GDExt_ERR_FAIL_COND_V_MSG(err, report, "Failed to copy gdextension libraries for plugin " + import_infos->get_import_md_path());
		err = copy_libs(output_dir, dep_paths);
		GDExt_ERR_FAIL_COND_V_MSG(err, report, "Failed to copy gdextension dependencies for plugin " + import_infos->get_import_md_path());
		bool found_our_platform = false;
		for (const auto &E : lib_paths) {
			const auto &so = E.value;
			if (so.tags.has(platform)) {
				found_our_platform = true;
				break;
			}
		}
		if (!found_our_platform || (!found_deps && libs_has_platform(deps, platform))) {
			report->set_message(plugin_name);
		}
	}
	return report;
}

void GDExtensionExporter::get_handled_types(List<String> *out) const {
}

void GDExtensionExporter::get_handled_importers(List<String> *out) const {
	out->push_back("gdextension");
	out->push_back("gdnative");
}

String GDExtensionExporter::get_name() const {
	return "GDExtension";
}

String GDExtensionExporter::get_default_export_extension(const String &res_path) const {
	return "gdext";
}
