#include "gdextension_exporter.h"
#include "core/os/os.h"
#include "core/os/shared_object.h"
#include "exporters/export_report.h"
#include "plugin_manager/plugin_manager.h"
#include "utility/common.h"
#include "utility/gdre_config.h"
#include "utility/gdre_settings.h"
#include "utility/glob.h"
#include "utility/import_info.h"
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

struct SharedObjectHasher {
	static uint32_t hash(const SharedObject &so) {
		uint32_t h = hash_murmur3_one_32(so.path.hash());
		for (const auto &tag : so.tags) {
			h = hash_murmur3_one_32(tag.hash(), h);
		}
		h = hash_murmur3_one_32(so.target.hash(), h);
		return hash_fmix32(h);
	}
};

struct SharedObjectEqual {
	static bool compare(const SharedObject &a, const SharedObject &b) {
		return a.path == b.path && a.tags == b.tags && a.target == b.target;
	}
};

using LibMap = HashMap<SharedObject, String, SharedObjectHasher, SharedObjectEqual>;

Error find_libs(const Vector<SharedObject> &libs, LibMap &lib_paths, const HashSet<String> &tags = {}) {
	if (libs.size() == 0) {
		return OK;
	}
	HashSet<String> libs_to_find;
	String parent_dir = GDRESettings::get_singleton()->get_pack_path().get_base_dir();
	Error err;
	for (const SharedObject &so : libs) {
		libs_to_find.insert(so.path.get_file());
	}
	auto find_so = [&](const String &path) {
		bool found = false;
		Vector<SharedObject> backup_sos;
		for (const SharedObject &so : libs) {
			if (so.path.get_file().to_lower() == path.get_file().to_lower()) {
				if (tags.is_empty() || gdre::has_all(tags, so.tags)) {
					lib_paths.insert(so, path);
					found = true;
				} else {
					backup_sos.push_back(so);
				}
			}
		}
		if (!found && !backup_sos.is_empty()) {
			for (const SharedObject &so : backup_sos) {
				lib_paths.insert(so, path);
			}
		}
	};
	{
		Ref<DirAccess> da = DirAccess::open(parent_dir, &err);
		ERR_FAIL_COND_V_MSG(err, ERR_FILE_CANT_OPEN, "Failed to open directory " + parent_dir);
		da->list_dir_begin();
		String f = da->get_next();
		while (!f.is_empty()) {
			if (f != "." && f != "..") {
				find_so(parent_dir.path_join(f));
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
				find_so(path);
			}
		}
	}
	if (lib_paths.size() == 0) {
		return ERR_BUG;
	}

	return OK;
}

Error copy_libs(const String &output_dir, const LibMap &lib_paths) {
	if (lib_paths.is_empty()) {
		return OK;
	}
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	HashMap<String, String> dest_paths;
	for (const auto &E : lib_paths) {
		const auto &so = E.key;
		String dest_path = output_dir.path_join(so.path.replace("res://", ""));
		if (dest_paths.has(dest_path) && dest_paths.get(dest_path) == E.value) {
			continue;
		}
		dest_paths.insert(dest_path, E.value);
		Error err = gdre::ensure_dir(dest_path.get_base_dir());
		ERR_FAIL_COND_V_MSG(err, ERR_FILE_CANT_WRITE, "Failed to create directory for library " + dest_path);
		if (da->file_exists(E.value)) {
			err = da->copy(E.value, dest_path);
		} else {
			err = da->copy_dir(E.value, dest_path);
		}
		ERR_FAIL_COND_V_MSG(err, ERR_FILE_CANT_WRITE, "Failed to copy library " + E.value + " to " + dest_path);
	}
	return OK;
}

String get_plugin_name(const Ref<ImportInfo> import_infos) {
	String rel_path = import_infos->get_import_md_path().simplify_path().replace("res://", "");
	String plugin_name;
	// If it begins with addons/, get the name of the immediate parent directory; otherwise, use the file name.
	// e.g. "addons/FMOD/fmod_gdextension.gdextension" -> "FMOD"
	// e.g. "bin/spine_godot_extension.gdextension" -> "spine_godot_extension"
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

struct SortSharedObject {
	bool operator()(const SharedObject &a, const SharedObject &b) const {
		return a.path < b.path;
	}
};

Ref<ExportReport> GDExtensionExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	// get the first name of the plugin after res://addons
	String plugin_name = get_plugin_name(import_infos);
	Ref<ExportReport> report = memnew(ExportReport(import_infos, get_name()));
	Ref<ImportInfoGDExt> iinfo = import_infos;
	String platform = OS::get_singleton()->get_name().to_lower();
	String parent_dir = GDRESettings::get_singleton()->get_pack_path().get_base_dir();

	auto libs = iinfo->get_libaries();
	// nothing to do if there are no libraries
	if (libs.is_empty()) {
		print_line("No libraries found in plugin " + import_infos->get_import_md_path() + ", skipping...");
		report->set_error(ERR_SKIP);
		report->set_message("No libraries found for plugin " + import_infos->get_import_md_path());
		return report;
	}
	LibMap lib_paths;
	Error err = OK;
	err = find_libs(libs, lib_paths);
	if (err) {
		Vector<String> missing_libs = { "None of the following libraries were found in the PCK directory: " };
		for (const auto &E : libs) {
			missing_libs.push_back(E.path.get_file());
		}
		report->append_message_detail(missing_libs);
	}
	HashSet<String> tags_found;
	for (const auto &E : lib_paths) {
		for (const auto &tag : E.key.tags) {
			tags_found.insert(tag);
		}
	}
	GDExt_ERR_FAIL_COND_V_MSG(err, report, "Failed to find gdextension libraries for plugin " + import_infos->get_import_md_path());
	auto deps = iinfo->get_dependencies();
	LibMap dep_paths;
	bool found_deps = true;
	err = find_libs(deps, dep_paths, tags_found);
	if (err) {
		found_deps = false;
		err = OK;
	}
	bool downloaded_plugin = false;
	if (GDREConfig::get_singleton()->get_setting("download_plugins")) {
		HashSet<String> hashes;
		HashSet<String> paths;
		for (const auto &E : lib_paths) {
			// TODO: come up with a way of consistently hashing signed macos binaries
			if (E.key.tags.has("macos") || paths.has(E.value)) {
				continue;
			}
			paths.insert(E.value);
			auto md5 = gdre::get_md5(E.value, true);
			if (!md5.is_empty()) {
				hashes.insert(md5);
			}
		}
		if (!hashes.is_empty()) {
			Dictionary info = PluginManager::get_plugin_info(plugin_name, gdre::hashset_to_vector(hashes));
			if (TaskManager::get_singleton()->is_current_task_canceled()) {
				report->set_error(ERR_SKIP);
				return report;
			}
			PluginVersion version = PluginVersion::from_json(info);
			if (version.is_valid()) {
				String url = version.release_info.download_url;
				String zip_path = output_dir.path_join(".tmp").path_join(plugin_name + ".zip");
				auto task_id = TaskManager::get_singleton()->add_download_task(url, zip_path);
				report->set_download_task_id(task_id);
				report->set_saved_path(zip_path);
				report->set_extra_info(info);
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
			const auto &so = E.key;
			if (so.tags.has(platform)) {
				found_our_platform = true;
				break;
			}
		}
		if (GDREConfig::get_singleton()->get_setting("Exporter/GDExtension/make_editor_copy")) {
			static const HashSet<String> release_type_tags = { "release", "debug", "dev", "editor", "template_release", "template_debug" };
			LibMap lib_paths_to_add;
			HashSet<String> tags_to_add;
			for (auto &KV : lib_paths) {
				HashSet<String> tags_to_find = gdre::hashset_without(gdre::vector_to_hashset(KV.key.tags), release_type_tags);
				for (auto &so : libs) {
					if (lib_paths.has(so) || !gdre::has_all(so.tags, tags_to_find)) {
						continue;
					}
					gdre::add_all(tags_to_add, so.tags);
					if (so.path == KV.key.path) {
						continue;
					}
					lib_paths_to_add.insert(so, KV.value);
				}
			}
			err = copy_libs(output_dir, lib_paths_to_add);
			GDExt_ERR_FAIL_COND_V_MSG(err, report, "Failed to copy gdextension libraries for plugin " + import_infos->get_import_md_path());
			if (!dep_paths.is_empty()) {
				LibMap dep_paths_to_add;
				auto get_dep_path_basename = [](const String &path) {
					return path.get_file().get_basename().trim_suffix("L").trim_suffix("D").to_lower();
				};
				HashSet<String> found_paths;
				Vector<SharedObject> ordered_deps;
				HashMap<String, int> tags_to_count;
				for (auto &KV : dep_paths) {
					String string_tag = String(".").join(KV.key.tags);
					if (!tags_to_count.has(string_tag)) {
						tags_to_count.insert(string_tag, 0);
					}
					tags_to_count[string_tag]++;
					if (found_paths.has(KV.value)) {
						continue;
					}
					found_paths.insert(KV.value);
					ordered_deps.push_back(KV.key);
				}
				ordered_deps.sort_custom<SortSharedObject>();
				HashMap<String, Vector<SharedObject>> grouped_deps = iinfo->get_grouped_dependencies();
				bool rewrote = false;
				for (auto &KV : grouped_deps) {
					if (KV.value.size() == 0) {
						continue;
					}
					auto normalized_tags = ImportInfoGDExt::normalize_tags(KV.key.split("."));
					if (!gdre::has_all(tags_to_add, normalized_tags)) {
						continue;
					}
					KV.value.sort_custom<SortSharedObject>();
					int idx = -1;
					if (KV.value.size() == ordered_deps.size()) {
						for (idx = 0; idx < KV.value.size(); idx++) {
							if (KV.value[idx].path == ordered_deps[idx].path) {
								continue;
							}
							if (get_dep_path_basename(KV.value[idx].path) != get_dep_path_basename(ordered_deps[idx].path)) {
								dep_paths_to_add.clear();
								break;
							}
							String dep_path = dep_paths.get(ordered_deps[idx]);
							dep_paths_to_add.insert(KV.value[idx], dep_path);
						}
					}
					if (idx < KV.value.size()) {
						WARN_PRINT("Dependencies mismatch! Rewriting gdextension dependencies...");
						auto dict = iinfo->get_dependency_dict();
						int max = 0;
						String to_use = "";
						for (auto &e : dict) {
							String normalized_string = String(".").join(ImportInfoGDExt::normalize_tags(e.key.operator String().split(".")));
							int64_t size = 0;
							if (e.value.get_type() == Variant::PACKED_STRING_ARRAY) {
								size = e.value.operator PackedStringArray().size();
							} else if (e.value.get_type() == Variant::DICTIONARY) {
								size = e.value.operator Dictionary().size();
							} else {
								continue;
							}
							if (tags_to_count.has(normalized_string) && tags_to_count[normalized_string] == size && tags_to_count[normalized_string] > max) {
								to_use = e.key;
							}
						}
						if (to_use.is_empty()) {
							continue;
						}
						dict[KV.key] = dict[to_use];
						iinfo->set_dependency_dict(dict);
						rewrote = true;
					}
				}
				err = copy_libs(output_dir, dep_paths_to_add);
				GDExt_ERR_FAIL_COND_V_MSG(err, report, "Failed to copy gdextension dependencies for plugin " + import_infos->get_import_md_path());
				if (rewrote) {
					auto thingy = ImportInfo::load_from_file(iinfo->get_import_md_path());

					err = iinfo->save_to(output_dir.path_join(import_infos->get_import_md_path().trim_prefix("res://")));
					GDExt_ERR_FAIL_COND_V_MSG(err, report, "Failed to save gdextension dependencies for plugin " + import_infos->get_import_md_path());
					report->set_rewrote_metadata(ExportReport::REWRITTEN);
				}
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
	return EXPORTER_NAME;
}

String GDExtensionExporter::get_default_export_extension(const String &res_path) const {
	return "gdext";
}
