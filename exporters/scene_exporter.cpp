#include "scene_exporter.h"

#include "compat/resource_loader_compat.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "exporters/export_report.h"
#include "modules/gltf/gltf_document.h"
#include "utility/common.h"
#include "utility/gdre_logger.h"
#include "utility/gdre_settings.h"

#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "scene/resources/packed_scene.h"
#include "utility/resource_info.h"

struct dep_info {
	ResourceUID::ID uid = ResourceUID::INVALID_ID;
	String dep;
	String remap;
	String type;
};

#define MAX_DEPTH 256
void get_deps_recursive(const String &p_path, HashMap<String, dep_info> &r_deps, int depth = 0) {
	if (depth > MAX_DEPTH) {
		ERR_PRINT("Dependency recursion depth exceeded.");
		return;
	}
	List<String> deps;
	ResourceCompatLoader::get_dependencies(p_path, &deps, true);
	for (const String &dep : deps) {
		if (!r_deps.has(dep)) {
			r_deps[dep] = dep_info{};
			dep_info &info = r_deps[dep];
			auto splits = dep.split("::");
			if (splits.size() == 3) {
				// If it has a UID, UID is first, followed by type, then fallback path
				info.uid = splits[0].is_empty() ? ResourceUID::INVALID_ID : ResourceUID::get_singleton()->text_to_id(splits[0]);
				info.type = splits[1];
				info.dep = splits[2];
				info.remap = GDRESettings::get_singleton()->get_mapped_path(info.dep);
				auto thingy = GDRESettings::get_singleton()->get_mapped_path(splits[0]);
				if (!FileAccess::exists(info.remap)) {
					if (FileAccess::exists(info.dep)) {
						info.remap = info.dep;
					} else {
						// If the remap doesn't exist, try to find the remap in the UID system
						auto mapped_path = info.uid != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->has_id(info.uid) ? ResourceUID::get_singleton()->get_id_path(info.uid) : "";
						if (mapped_path.is_empty() || !FileAccess::exists(mapped_path)) {
							mapped_path = GDRESettings::get_singleton()->get_mapped_path(mapped_path);
							if (mapped_path.is_empty() || !FileAccess::exists(mapped_path)) {
								ERR_PRINT(vformat("Failed to export scene %s: Dependency %s:%s does not exist.", p_path, info.dep, info.remap));
								continue;
							}
						}
						info.remap = mapped_path;
					}
				}

			} else {
				// otherwise, it's path followed by type
				info.dep = splits[0];
				info.type = splits[1];
			}
			if (info.remap.is_empty()) {
				info.remap = GDRESettings::get_singleton()->get_mapped_path(info.dep);
			}
			get_deps_recursive(info.remap, r_deps, depth + 1);
		}
	}
}

bool SceneExporter::using_threaded_load() const {
	// If the scenes are being exported using the worker task pool, we can't use threaded load
	return !supports_multithread();
}

Error SceneExporter::_export_file(const String &p_dest_path, const String &p_src_path) {
	String dest_ext = p_dest_path.get_extension().to_lower();
	if (dest_ext == "escn" || dest_ext == "tscn") {
		return ResourceCompatLoader::to_text(p_src_path, p_dest_path);
	} else if (dest_ext != "glb") {
		ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "Only .escn, .tscn, and .glb formats are supported for export.");
	}
	Vector<uint64_t> texture_uids;
	Error err = OK;
	bool has_script = false;
	bool has_shader = false;
	{
		List<String> get_deps;
		// We need to preload any Texture resources that are used by the scene with our own loader
		HashMap<String, dep_info> get_deps_map;
		get_deps_recursive(p_src_path, get_deps_map);

		Vector<Ref<Resource>> textures;

		for (auto &E : get_deps_map) {
			dep_info &info = E.value;
			if (info.type == "Script") {
				has_script = true;
			} else if (info.dep.get_extension().to_lower().contains("shader")) {
				has_shader = true;
			}
		}
		// Don't need this right now, we just instance shader to a missing resource
		// If GLTF exporter somehow starts making use of them, we'll have to do this
		// bool is_default_gltf_load = ResourceCompatLoader::is_default_gltf_load();
		// if (has_shader) {
		// 	print_line("This scene has shaders, which may not be compatible with the exporter.");
		// 	// if it has a shader, we have to set gltf_load to false and do a real load on the textures, otherwise shaders will not be applied to the textures
		// 	ResourceCompatLoader::set_default_gltf_load(false);
		// }
		auto set_cache_res = [&](const dep_info &info, Ref<Resource> texture, bool force_replace) {
			if (texture.is_null() || (!force_replace && ResourceCache::get_ref(info.dep).is_valid())) {
				return;
			}
#ifdef TOOLS_ENABLED
			texture->set_import_path(info.remap);
#endif
			// reset the path cache, then set the path so it loads it into cache.
			texture->set_path_cache("");
			texture->set_path(info.dep, true);
			textures.push_back(texture);
		};
		for (auto &E : get_deps_map) {
			dep_info &info = E.value;
			// Never set Script or Shader, they're not used by the GLTF writer and cause errors
			if (info.type == "Script" || info.type == "Shader") {
				auto texture = CompatFormatLoader::create_missing_external_resource(info.dep, info.type, info.uid, "");
				set_cache_res(info, texture, false);
				continue;
			}
			if (!FileAccess::exists(info.remap) && !FileAccess::exists(info.dep)) {
				ERR_FAIL_V_MSG(ERR_FILE_MISSING_DEPENDENCIES, vformat("Failed to export scene %s: Dependency %s:%s does not exist.", p_src_path, info.dep, info.remap));
			} else if (info.uid != ResourceUID::INVALID_ID) {
				if (!ResourceUID::get_singleton()->has_id(info.uid)) {
					ResourceUID::get_singleton()->add_id(info.uid, info.remap);
					texture_uids.push_back(info.uid);
				}
				continue;
			}

			if (info.dep != info.remap) {
				auto texture = ResourceCompatLoader::custom_load(
						info.remap, "",
						ResourceCompatLoader::get_default_load_type(),
						&err,
						using_threaded_load(),
						ResourceFormatLoader::CACHE_MODE_REUSE);
				if (err || texture.is_null()) {
					ERR_FAIL_V_MSG(ERR_FILE_MISSING_DEPENDENCIES, vformat("Failed to export scene %s: Dependency %s:%s failed to load.", p_src_path, info.dep, info.remap));
				}
				set_cache_res(info, texture, false);
			}
		}
		auto errors_before = using_threaded_load() ? GDRELogger::get_error_count() : GDRELogger::get_thread_error_count();
		err = _export_scene(p_dest_path, p_src_path, using_threaded_load());
		auto errors_after = using_threaded_load() ? GDRELogger::get_error_count() : GDRELogger::get_thread_error_count();
		if (err == OK && errors_after > errors_before) {
			err = ERR_PRINTER_ON_FIRE;
		}
	}
	// if (has_shader) {
	// 	ResourceCompatLoader::set_default_gltf_load(true);
	// }
	// remove the UIDs that we added that didn't exist before
	for (uint64_t id : texture_uids) {
		ResourceUID::get_singleton()->remove_id(id);
	}
	if (err != ERR_PRINTER_ON_FIRE) {
		ERR_FAIL_COND_V_MSG(err, err, "Failed to export scene " + p_src_path);
	} else {
		WARN_PRINT("Exported scene had errors: " + p_src_path);
	}
	return err;
}

Error SceneExporter::_export_scene(const String &p_dest_path, const String &p_src_path, bool use_subthreads) {
	Error err;
	auto mode_type = ResourceCompatLoader::get_default_load_type();
	Ref<PackedScene> scene;
	// For some reason, scenes with meshes fail to load without the load done by ResourceLoader::load, possibly due to notification shenanigans.
	if (ResourceCompatLoader::is_globally_available() && use_subthreads) {
		scene = ResourceLoader::load(p_src_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_REUSE, &err);
	} else {
		scene = ResourceCompatLoader::custom_load(p_src_path, "PackedScene", mode_type, &err, use_subthreads, ResourceFormatLoader::CACHE_MODE_REUSE);
	}
	ERR_FAIL_COND_V_MSG(err, err, "Failed to load scene " + p_src_path);
	// GLTF export can result in inaccurate models
	// save it under .assets, which won't be picked up for import by the godot editor
	// we only export glbs
	err = gdre::ensure_dir(p_dest_path.get_base_dir());
	Node *root;
	{
		List<String> deps;
		Ref<GLTFDocument> doc;
		doc.instantiate();
		Ref<GLTFState> state;
		state.instantiate();
		int32_t flags = 0;
		flags |= 16; // EditorSceneFormatImporter::IMPORT_USE_NAMED_SKIN_BINDS;
		root = scene->instantiate();
		err = doc->append_from_scene(root, state, flags);
		if (err) {
			memdelete(root);
			ERR_FAIL_COND_V_MSG(err, err, "Failed to append scene " + p_src_path + " to glTF document");
		}
		err = doc->write_to_filesystem(state, p_dest_path);
	}
	memdelete(root);
	ERR_FAIL_COND_V_MSG(err, err, "Failed to write glTF document to " + p_dest_path);
	return OK;
}
Error SceneExporter::export_file(const String &p_dest_path, const String &p_src_path) {
	String ext = p_dest_path.get_extension().to_lower();
	if (ext != "escn" && ext != "tscn") {
		int ver_major = get_ver_major(p_src_path);
		ERR_FAIL_COND_V_MSG(ver_major != 4, ERR_UNAVAILABLE, "Scene export for engine version " + itos(ver_major) + " is not currently supported.");
	}
	return _export_file(p_dest_path, p_src_path);
}
Ref<ExportReport> SceneExporter::export_resource(const String &output_dir, Ref<ImportInfo> iinfo) {
	Ref<ExportReport> report = memnew(ExportReport(iinfo));

	Error err;
	Vector<uint64_t> texture_uids;
	String new_path = iinfo->get_export_dest();
	String ext = new_path.get_extension().to_lower();
	bool to_text = ext == "escn" || ext == "tscn";
	if (!to_text) {
		new_path = new_path.replace("res://", "res://.assets/");
		if (ext != "glb") {
			new_path = new_path.get_basename() + ".glb";
		}
	}
	iinfo->set_export_dest(new_path);
	String dest_path = output_dir.path_join(new_path.replace("res://", ""));
	if (!to_text && iinfo->get_ver_major() != 4) {
		err = ERR_UNAVAILABLE;
		report->set_message("Scene export for engine version " + itos(iinfo->get_ver_major()) + " is not currently supported.");
		report->set_unsupported_format_type(itos(iinfo->get_ver_major()) + ".x PackedScene");
	} else {
		report->set_new_source_path(new_path);
		err = _export_file(dest_path, iinfo->get_path());
	}
	if (err == OK) {
		report->set_saved_path(dest_path);
	} else if (err == ERR_PRINTER_ON_FIRE) {
		report->set_message("Scene export had errors.");
	}
#if DEBUG_ENABLED
	if (err) {
		// save it as a text scene so we can see what went wrong
		auto new_dest = dest_path.get_basename() + ".tscn";
		ResourceCompatLoader::to_text(iinfo->get_path(), new_dest);
	}
#endif
	report->set_error(err);
	return report; // We always save to an unoriginal path
}

bool SceneExporter::handles_import(const String &importer, const String &resource_type) const {
	return importer == "scene" || resource_type == "PackedScene";
}

void SceneExporter::get_handled_types(List<String> *out) const {
	out->push_back("PackedScene");
}

void SceneExporter::get_handled_importers(List<String> *out) const {
	out->push_back("scene");
}