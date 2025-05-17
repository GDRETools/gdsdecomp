#include "scene_exporter.h"

#include "compat/resource_loader_compat.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "exporters/export_report.h"
#include "exporters/obj_exporter.h"
#include "external/tinygltf/tiny_gltf.h"
#include "modules/gltf/gltf_document.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/texture.h"
#include "utility/common.h"
#include "utility/gdre_logger.h"
#include "utility/gdre_settings.h"

#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/json.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/packed_scene.h"
struct dep_info {
	ResourceUID::ID uid = ResourceUID::INVALID_ID;
	String dep;
	String remap;
	String type;
	bool exists = true;
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
								info.exists = false;
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
			if (FileAccess::exists(info.remap)) {
				get_deps_recursive(info.remap, r_deps, depth + 1);
			} else {
				info.exists = false;
			}
		}
	}
}

bool SceneExporter::using_threaded_load() const {
	// If the scenes are being exported using the worker task pool, we can't use threaded load
	return !supports_multithread();
}

Error load_model(const String &p_filename, tinygltf::Model &model, String &r_error) {
	tinygltf::TinyGLTF loader;
	loader.SetImagesAsIs(true);
	std::string filename = p_filename.utf8().get_data();
	std::string error;
	std::string warning;
	bool is_binary = p_filename.get_extension().to_lower() == "glb";
	bool state = is_binary ? loader.LoadBinaryFromFile(&model, &error, &warning, filename) : loader.LoadASCIIFromFile(&model, &error, &warning, filename);
	ERR_FAIL_COND_V_MSG(!state, ERR_FILE_CANT_READ, vformat("Failed to load GLTF file: %s", error.c_str()));
	if (error.size() > 0) { // validation errors, ignore for right now
		r_error.append_utf8(error.c_str());
	}
	return OK;
}

Error save_model(const String &p_filename, const tinygltf::Model &model) {
	tinygltf::TinyGLTF loader;
	loader.SetImagesAsIs(true);
	std::string filename = p_filename.utf8().get_data();
	gdre::ensure_dir(p_filename.get_base_dir());
	bool is_binary = p_filename.get_extension().to_lower() == "glb";
	bool state = loader.WriteGltfSceneToFile(&model, filename, is_binary, is_binary, !is_binary, is_binary);
	ERR_FAIL_COND_V_MSG(!state, ERR_FILE_CANT_WRITE, vformat("Failed to save GLTF file!"));
	return OK;
}

inline void _merge_resources(HashSet<Ref<Resource>> &merged, const HashSet<Ref<Resource>> &p_resources) {
	for (const auto &E : p_resources) {
		merged.insert(E);
	}
}

void SceneExporter::rewrite_global_mesh_import_params(Ref<ImportInfo> p_import_info, const ObjExporter::MeshInfo &p_mesh_info) {
	auto ver_major = p_import_info->get_ver_major();
	auto ver_minor = p_import_info->get_ver_minor();
	if (ver_major == 4) {
		// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "meshes/ensure_tangents"), true));
		// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "meshes/generate_lods"), true));
		// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "meshes/create_shadow_meshes"), true));
		// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "meshes/light_baking", PROPERTY_HINT_ENUM, "Disabled,Static,Static Lightmaps,Dynamic", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), 1));
		// r_options->push_back(ImportOption(PropertyInfo(Variant::FLOAT, "meshes/lightmap_texel_size", PROPERTY_HINT_RANGE, "0.001,100,0.001"), 0.2));
		// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "meshes/force_disable_compression"), false));

		// 4.x removes the import params, so we need to rewrite all of them
		p_import_info->set_param("meshes/ensure_tangents", p_mesh_info.has_tangents);
		p_import_info->set_param("meshes/generate_lods", p_mesh_info.has_lods);
		p_import_info->set_param("meshes/create_shadow_meshes", p_mesh_info.has_shadow_meshes);
		p_import_info->set_param("meshes/light_baking", p_mesh_info.has_lightmap_uv2 ? 2 : 1);
		p_import_info->set_param("meshes/lightmap_texel_size", p_mesh_info.lightmap_uv2_texel_size);
		if (ver_minor >= 2) {
			p_import_info->set_param("meshes/force_disable_compression", !p_mesh_info.compression_enabled);
		}
	}
	// 2.x doesn't require this
}
HashSet<Ref<Resource>> _find_resources(const Variant &p_variant, bool p_main, int ver_major) {
	HashSet<Ref<Resource>> resources;
	switch (p_variant.get_type()) {
		case Variant::OBJECT: {
			Ref<Resource> res = p_variant;

			if (res.is_null() || !CompatFormatLoader::resource_is_resource(res, ver_major) || resources.has(res)) {
				return resources;
			}

			if (!p_main) {
				resources.insert(res);
			}

			List<PropertyInfo> property_list;

			res->get_property_list(&property_list);
			property_list.sort();

			List<PropertyInfo>::Element *I = property_list.front();

			while (I) {
				PropertyInfo pi = I->get();

				if (pi.usage & PROPERTY_USAGE_STORAGE) {
					Variant v = res->get(I->get().name);

					if (pi.usage & PROPERTY_USAGE_RESOURCE_NOT_PERSISTENT) {
						Ref<Resource> sres = v;
						if (sres.is_valid() && !resources.has(sres)) {
							resources.insert(sres);
							_merge_resources(resources, _find_resources(sres, false, ver_major));
						}
					} else {
						_merge_resources(resources, _find_resources(v, false, ver_major));
					}
				}

				I = I->next();
			}

			// COMPAT: get the missing resources too
			Dictionary missing_resources = res->get_meta(META_MISSING_RESOURCES, Dictionary());
			if (missing_resources.size()) {
				LocalVector<Variant> keys = missing_resources.get_key_list();
				for (const Variant &E : keys) {
					_merge_resources(resources, _find_resources(missing_resources[E], false, ver_major));
				}
			}

			resources.insert(res); // Saved after, so the children it needs are available when loaded
		} break;
		case Variant::ARRAY: {
			Array varray = p_variant;
			_merge_resources(resources, _find_resources(varray.get_typed_script(), false, ver_major));
			for (const Variant &var : varray) {
				_merge_resources(resources, _find_resources(var, false, ver_major));
			}

		} break;
		case Variant::DICTIONARY: {
			Dictionary d = p_variant;
			_merge_resources(resources, _find_resources(d.get_typed_key_script(), false, ver_major));
			_merge_resources(resources, _find_resources(d.get_typed_value_script(), false, ver_major));
			LocalVector<Variant> keys = d.get_key_list();
			for (const Variant &E : keys) {
				// Of course keys should also be cached, after all we can't prevent users from using resources as keys, right?
				// See also ResourceFormatSaverBinaryInstance::_find_resources (when p_variant is of type Variant::DICTIONARY)
				_merge_resources(resources, _find_resources(E, false, ver_major));
				Variant v = d[E];
				_merge_resources(resources, _find_resources(v, false, ver_major));
			}
		} break;
		default: {
		}
	}
	return resources;
}

Error _encode_buffer_glb(Ref<GLTFState> p_state, const String &p_path) {
	auto state_buffers = p_state->get_buffers();
	print_verbose("glTF: Total buffers: " + itos(state_buffers.size()));

	if (state_buffers.is_empty()) {
		return OK;
	}
	Array buffers;
	if (!state_buffers.is_empty()) {
		Vector<uint8_t> buffer_data = state_buffers[0];
		Dictionary gltf_buffer;

		gltf_buffer["byteLength"] = buffer_data.size();
		buffers.push_back(gltf_buffer);
	}

	for (GLTFBufferIndex i = 1; i < state_buffers.size() - 1; i++) {
		Vector<uint8_t> buffer_data = state_buffers[i];
		Dictionary gltf_buffer;
		String filename = p_path.get_basename().get_file() + itos(i) + ".bin";
		String path = p_path.get_base_dir() + "/" + filename;
		Error err;
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
		if (file.is_null()) {
			return err;
		}
		if (buffer_data.is_empty()) {
			return OK;
		}
		file->create(FileAccess::ACCESS_RESOURCES);
		file->store_buffer(buffer_data.ptr(), buffer_data.size());
		gltf_buffer["uri"] = filename;
		gltf_buffer["byteLength"] = buffer_data.size();
		buffers.push_back(gltf_buffer);
	}
	p_state->get_json()["buffers"] = buffers;

	return OK;
}

Error _encode_buffer_bins(Ref<GLTFState> p_state, const String &p_path) {
	auto state_buffers = p_state->get_buffers();
	print_verbose("glTF: Total buffers: " + itos(state_buffers.size()));

	if (state_buffers.is_empty()) {
		return OK;
	}
	Array buffers;

	for (GLTFBufferIndex i = 0; i < state_buffers.size(); i++) {
		Vector<uint8_t> buffer_data = state_buffers[i];
		Dictionary gltf_buffer;
		String filename = p_path.get_basename().get_file() + itos(i) + ".bin";
		String path = p_path.get_base_dir() + "/" + filename;
		Error err;
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
		if (file.is_null()) {
			return err;
		}
		if (buffer_data.is_empty()) {
			return OK;
		}
		file->create(FileAccess::ACCESS_RESOURCES);
		file->store_buffer(buffer_data.ptr(), buffer_data.size());
		gltf_buffer["uri"] = filename;
		gltf_buffer["byteLength"] = buffer_data.size();
		buffers.push_back(gltf_buffer);
	}
	p_state->get_json()["buffers"] = buffers;

	return OK;
}

Error _serialize_file(Ref<GLTFState> p_state, const String p_path) {
	Error err = FAILED;
	if (p_path.to_lower().ends_with("glb")) {
		err = _encode_buffer_glb(p_state, p_path);
		ERR_FAIL_COND_V(err != OK, err);
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V(file.is_null(), FAILED);

		String json = Variant(p_state->get_json()).to_json_string();

		const uint32_t magic = 0x46546C67; // GLTF
		const int32_t header_size = 12;
		const int32_t chunk_header_size = 8;
		CharString cs = json.utf8();
		const uint32_t text_data_length = cs.length();
		const uint32_t text_chunk_length = ((text_data_length + 3) & (~3));
		const uint32_t text_chunk_type = 0x4E4F534A; //JSON

		uint32_t binary_data_length = 0;
		auto state_buffers = p_state->get_buffers();
		if (state_buffers.size() > 0) {
			binary_data_length = ((PackedByteArray)state_buffers[0]).size();
		}
		const uint32_t binary_chunk_length = ((binary_data_length + 3) & (~3));
		const uint32_t binary_chunk_type = 0x004E4942; //BIN

		file->create(FileAccess::ACCESS_RESOURCES);
		file->store_32(magic);
		file->store_32(p_state->get_major_version()); // version
		uint32_t total_length = header_size + chunk_header_size + text_chunk_length;
		if (binary_chunk_length) {
			total_length += chunk_header_size + binary_chunk_length;
		}
		file->store_32(total_length);

		// Write the JSON text chunk.
		file->store_32(text_chunk_length);
		file->store_32(text_chunk_type);
		file->store_buffer((uint8_t *)&cs[0], cs.length());
		for (uint32_t pad_i = text_data_length; pad_i < text_chunk_length; pad_i++) {
			file->store_8(' ');
		}

		// Write a single binary chunk.
		if (binary_chunk_length) {
			file->store_32(binary_chunk_length);
			file->store_32(binary_chunk_type);
			file->store_buffer(((PackedByteArray)state_buffers[0]).ptr(), binary_data_length);
			for (uint32_t pad_i = binary_data_length; pad_i < binary_chunk_length; pad_i++) {
				file->store_8(0);
			}
		}
	} else {
		String indent = "";
#if DEBUG_ENABLED
		indent = "  ";
#endif
		err = _encode_buffer_bins(p_state, p_path);
		ERR_FAIL_COND_V(err != OK, err);
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V(file.is_null(), FAILED);

		file->create(FileAccess::ACCESS_RESOURCES);
		String json = JSON::stringify(Variant(p_state->get_json()), indent);
		file->store_string(json);
	}
	return err;
}

template <typename T>
T get_most_popular_value(const Vector<T> &p_values) {
	HashMap<T, int64_t> dict;
	for (int i = 0; i < p_values.size(); i++) {
		size_t current_count = dict.has(p_values[i]) ? dict.get(p_values[i]) : 0;
		dict[p_values[i]] = current_count + 1;
	}
	int64_t max_count = 0;
	T most_popular_value;
	for (auto &E : dict) {
		if (E.value > max_count) {
			max_count = E.value;
			most_popular_value = E.key;
		}
	}
	return most_popular_value;
}

Error SceneExporter::_export_file(const String &p_dest_path, const String &p_src_path, Ref<ExportReport> p_report) {
	String dest_ext = p_dest_path.get_extension().to_lower();
	Ref<ImportInfo> iinfo = p_report.is_valid() ? p_report->get_import_info() : nullptr;
	int ver_major = iinfo.is_valid() ? iinfo->get_ver_major() : get_ver_major(p_src_path);
	if (dest_ext == "escn" || dest_ext == "tscn") {
		return ResourceCompatLoader::to_text(p_src_path, p_dest_path);
	} else if (dest_ext == "obj") {
		ObjExporter::MeshInfo mesh_info;
		Error err = export_file_to_obj(p_dest_path, p_src_path, ver_major, mesh_info);
		if (err != OK) {
			return err;
		}
		if (iinfo.is_valid()) {
			ObjExporter::rewrite_import_params(iinfo, mesh_info);
		}
		return OK;
	} else if (dest_ext != "glb" && dest_ext != "gltf") {
		ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "Only .escn, .tscn, .obj, .glb, and .gltf formats are supported for export.");
	}
	ObjExporter::MeshInfo r_mesh_info;
	Vector<uint64_t> texture_uids;
	Error err = OK;
	bool has_script = false;
	bool has_shader = false;
	bool has_external_animation = false;
	bool has_external_materials = false;
	bool has_external_images = false;
	bool has_external_meshes = false;
	Vector<CompressedTexture2D::DataFormat> image_formats;

	bool set_all_externals = false;
	List<String> get_deps;
	// We need to preload any Texture resources that are used by the scene with our own loader
	HashMap<String, dep_info> get_deps_map;
	HashSet<String> need_to_be_updated;
	HashSet<String> external_deps_updated;
	Vector<String> error_messages;
	auto append_error_messages = [&](Error p_err, const String &err_msg = "") {
		String step;
		switch (p_err) {
			case ERR_FILE_MISSING_DEPENDENCIES:
				step = "dependency resolution";
				break;
			case ERR_FILE_CANT_OPEN:
				step = "load";
				break;
			case ERR_COMPILATION_FAILED:
				step = "appending to GLTF document";
				break;
			case ERR_FILE_CANT_WRITE:
				step = "serializing GLTF document";
				break;
			case ERR_PRINTER_ON_FIRE:
				step = "GLTF export";
				break;
			case ERR_FILE_CORRUPT:
				step = "GLTF validation";
				break;
			default:
				step = "unknown";
				break;
		}
		String desc;
		if (has_script && has_shader) {
			desc = "scripts and shaders";
		} else if (has_script) {
			desc = "scripts";
		} else if (has_shader) {
			desc = "shaders";
		}
		auto err_message = vformat("Errors during %s", step);
		if (!err_msg.is_empty()) {
			err_message += ":\n  " + err_msg;
		}
		if (!desc.is_empty()) {
			err_message += "\n  Scene had " + desc;
		}

		if (p_report.is_valid()) {
			auto message = err_message + "\n";
			Vector<String> message_detail;
			message_detail.append("Dependencies:");
			for (const auto &E : get_deps_map) {
				message_detail.append(vformat("  %s -> %s, exists: %s", E.key, E.value.remap, E.value.exists ? "yes" : "no"));
			}
			p_report->set_message(message);
			p_report->append_message_detail(message_detail);
			p_report->set_error(p_err);
			error_messages.append_array(supports_multithread() ? GDRELogger::get_thread_errors() : GDRELogger::get_errors());
			p_report->append_error_messages(error_messages);
		}

		return vformat("Failed to export scene %s:\n  %s", p_src_path, err_message);
	};

#define GDRE_SCN_EXP_FAIL_V_MSG(err, msg)                                                    \
	{                                                                                        \
		ERR_PRINT(append_error_messages(err, msg));                                          \
		supports_multithread() ? GDRELogger::get_thread_errors() : GDRELogger::get_errors(); \
		return err;                                                                          \
	}

#define GDRE_SCN_EXP_FAIL_COND_V_MSG(cond, err, msg) \
	if (unlikely(cond)) {                            \
		GDRE_SCN_EXP_FAIL_V_MSG(err, msg);           \
	}

	{
		get_deps_recursive(p_src_path, get_deps_map);

		Vector<Ref<Resource>> textures;

		for (auto &E : get_deps_map) {
			dep_info &info = E.value;
			if (info.type == "Script") {
				has_script = true;
			} else if (info.dep.get_extension().to_lower().contains("shader")) {
				has_shader = true;
			} else {
				if (info.type.contains("Animation")) {
					has_external_animation = true;
					need_to_be_updated.insert(info.dep);
				} else if (info.type.contains("Material")) {
					has_external_materials = true;
					need_to_be_updated.insert(info.dep);
				} else if (info.type.contains("Texture")) {
					has_external_images = true;
					need_to_be_updated.insert(info.dep);
				} else if (info.type.contains("Mesh")) {
					has_external_meshes = true;
					need_to_be_updated.insert(info.dep);
				}
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
				GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_MISSING_DEPENDENCIES,
						vformat("Dependency %s -> %s does not exist.", info.dep, info.remap));
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
					GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_MISSING_DEPENDENCIES,
							vformat("Dependency %s:%s failed to load.", info.dep, info.remap));
				}
				set_cache_res(info, texture, false);
			}
		}
		Vector<String> id_to_texture_path;
		Vector<Pair<String, String>> id_to_material_path;
		// Vector<Pair<String, String>> id_to_meshes_path;
		Vector<ObjExporter::MeshInfo> id_to_mesh_info;
		Vector<Pair<String, String>> id_to_animations_path;
		HashMap<String, String> animation_map;
		HashMap<String, ObjExporter::MeshInfo> mesh_info_map;
		HashMap<String, Dictionary> animation_options;
		String root_type;
		String root_name;

		auto get_resource_path = [&](const Ref<Resource> &res) {
			String path = res->get_path();
			if (path.is_empty()) {
				Dictionary compat = ResourceInfo::get_info_dict_from_resource(res);
				if (compat.size() > 0) {
					path = compat["original_path"];
				}
			}
			return path;
		};

		auto errors_before = using_threaded_load() ? GDRELogger::get_error_count() : GDRELogger::get_thread_error_count();
		auto _export_scene = [&]() {
			Error p_err;
			auto mode_type = ResourceCompatLoader::get_default_load_type();
			Ref<PackedScene> scene;
			// For some reason, scenes with meshes fail to load without the load done by ResourceLoader::load, possibly due to notification shenanigans.
			if (ResourceCompatLoader::is_globally_available() && using_threaded_load()) {
				scene = ResourceLoader::load(p_src_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_REUSE, &p_err);
			} else {
				scene = ResourceCompatLoader::custom_load(p_src_path, "PackedScene", mode_type, &p_err, using_threaded_load(), ResourceFormatLoader::CACHE_MODE_REUSE);
			}
			ERR_FAIL_COND_V_MSG(p_err, ERR_FILE_CANT_READ, "Failed to load scene " + p_src_path);
			p_err = gdre::ensure_dir(p_dest_path.get_base_dir());
			auto deps = _find_resources(scene, true, ver_major);
			Vector<Ref<AnimationLibrary>> animation_libraries;
			if (has_external_animation) {
				for (auto &E : deps) {
					Ref<AnimationLibrary> anim_lib = E;
					if (anim_lib.is_valid()) {
						animation_libraries.push_back(anim_lib);
						List<StringName> anim_names;
						anim_lib->get_animation_list(&anim_names);
						for (auto &anim_name : anim_names) {
							Ref<Animation> anim = anim_lib->get_animation(anim_name);
							auto path = get_resource_path(anim);
							String name = anim_name;
							int i = 1;
							while (animation_options.has(name)) {
								// append _001, _002, etc.
								name = vformat("%s_%03d", anim_name, i);
								i++;
							}
							animation_options[name] = Dictionary();
							auto &options = animation_options[name];
							if (!(path.is_empty() || path.get_file().contains("::"))) {
								options["save_to_file/enabled"] = true;
								options["save_to_file/keep_custom_tracks"] = true;
								options["save_to_file/path"] = path;
							} else {
								options["save_to_file/enabled"] = false;
								options["save_to_file/keep_custom_tracks"] = false;
								options["save_to_file/path"] = "";
							}
							options["settings/loop_mode"] = (int)anim->get_loop_mode();
							options["slices/amount"] = 0;
						}
					}
				}
			}
			Node *root;
			String scene_name;
			if (p_report.is_valid()) {
				scene_name = iinfo->get_source_file().get_file().get_basename();
			} else {
				scene_name = p_src_path.get_file().get_slice(".", 0);
			}
			auto pop_res_path_vec = [](Array arr, Vector<String> &paths) {
				paths.clear();
				paths.resize(arr.size());
				for (int i = 0; i < arr.size(); i++) {
					Ref<Resource> res = arr[i];
					if (res.is_null()) {
						paths.write[i] = "";
					} else {
						paths.write[i] = res->get_path();
					}
				}
			};

			// TODO: handle Godot version <= 4.2 image naming scheme?
			auto demangle_name = [scene_name](const String &path) {
				return path.get_file().get_basename().trim_prefix(scene_name + "_");
			};

			{
				List<String> deps;
				Ref<GLTFDocument> doc;
				doc.instantiate();
				Ref<GLTFState> state;
				state.instantiate();
				int32_t flags = 0;
				auto exts = doc->get_supported_gltf_extensions();
				flags |= 16; // EditorSceneFormatImporter::IMPORT_USE_NAMED_SKIN_BINDS;
				root = scene->instantiate();
				root_type = root->get_class();
				root_name = root->get_name();
				p_err = doc->append_from_scene(root, state, flags);
				if (p_err) {
					memdelete(root);
					ERR_FAIL_COND_V_MSG(p_err, ERR_COMPILATION_FAILED, "Failed to append scene " + p_src_path + " to glTF document");
				}
				Vector<String> original_mesh_names;
				Vector<String> original_image_names;

				p_err = doc->_serialize(state);
				if (p_err) {
					ERR_FAIL_COND_V_MSG(p_err, ERR_FILE_CANT_WRITE, "Failed to serialize glTF document");
				}

				auto get_name_res = [](const Dictionary &dict, const Ref<Resource> &res) {
					String name = dict.get("name", String());
					if (name.is_empty()) {
						name = res->get_name();
						if (name.is_empty()) {
							Dictionary compat = ResourceInfo::get_info_dict_from_resource(res);
							if (compat.size() > 0) {
								name = compat["resource_name"];
							}
						}
					}
					return name;
				};

				auto get_path_res = [](const Ref<Resource> &res) {
					String path = res->get_path();
					if (path.is_empty()) {
						Dictionary compat = ResourceInfo::get_info_dict_from_resource(res);
						if (compat.size() > 0) {
							path = compat["original_path"];
						}
					}
					return path;
				};

				{
					auto json = state->get_json();
					Array images = state->get_images();
					Array json_images = json["images"];
					HashMap<String, Vector<int>> image_map;
					bool has_duped_images = false;
					for (int i = 0; i < json_images.size(); i++) {
						Dictionary image_dict = json_images[i];
						Ref<Texture2D> image = images[i];
						auto path = get_path_res(image);
						String name;
						bool is_internal = path.is_empty() || path.get_file().contains("::");
						if (is_internal) {
							name = get_name_res(image_dict, image);
							if (!image_map.has(name)) {
								image_map[name] = Vector<int>();
							} else {
								has_duped_images = true;
							}
							image_map[name].push_back(i);
						} else {
							name = path.get_file().get_basename();
							external_deps_updated.insert(path);
							if (!image_map.has(name)) {
								image_map[name] = Vector<int>();
							} else {
								has_duped_images = true;
							}
							image_map[name].push_back(i);
						}
						if (!name.is_empty()) {
							image_dict["name"] = demangle_name(name);
						}
						auto compat_dict = ResourceInfo::get_info_dict_from_resource(image);
						Dictionary extras = compat_dict.get("extras", Dictionary());
						if (extras.has("data_format")) {
							image_formats.push_back(CompressedTexture2D::DataFormat(int(compat_dict["data_format"])));
						}
					}
					HashMap<int, int> removal_to_replacement;
					Vector<int> to_remove;
					if (has_duped_images) {
						for (auto &E : image_map) {
							auto &name = E.key;
							auto &indices = E.value;
							if (indices.size() <= 1) {
								continue;
							}
							int replacement = indices[0];
							for (int i = 1; i < indices.size(); i++) {
								Dictionary image_dict = json_images[indices[i]];
								Ref<Texture2D> image = images[indices[i]];
								String name = image_dict.get("name", String());
								to_remove.push_back(indices[i]);
								removal_to_replacement[indices[i]] = replacement;
							}
						}
						Array json_textures = json["textures"];
						for (Dictionary texture_dict : json_textures) {
							if (texture_dict.has("source")) {
								// image idx
								int image_idx = texture_dict["source"];
								if (removal_to_replacement.has(image_idx)) {
									texture_dict["source"] = removal_to_replacement[image_idx];
								}
							}
						}
						to_remove.sort();
						to_remove.reverse();
						for (int i = 0; i < to_remove.size(); i++) {
							json_images.remove_at(to_remove[i]);
							// don't touch the images
						}
						json["textures"] = json_textures;
					}
					json["images"] = json_images;

					{
						auto default_light_map_size = Vector2i(0, 0);
						TypedArray<Node> mesh_instances = root->find_children("*", "MeshInstance3D");
						Vector<String> mesh_names;
						Vector<Pair<Ref<ArrayMesh>, MeshInstance3D *>> mesh_to_instance;
						Vector<bool> mesh_is_shadow;
						auto gltf_meshes = state->get_meshes();
						Array json_meshes = json["meshes"];
						for (int i = 0; i < gltf_meshes.size(); i++) {
							Ref<GLTFMesh> gltf_mesh = gltf_meshes[i];
							auto mesh = gltf_mesh->get_mesh();
							auto original_name = gltf_mesh->get_original_name();
							if (original_name.is_empty()) {
								original_name = mesh->get_name();
							}
							Dictionary mesh_dict = json_meshes[i];
							ObjExporter::MeshInfo mesh_info;
							if (mesh.is_null()) {
								id_to_mesh_info.push_back(mesh_info);
								continue;
							}
							Dictionary compat = ResourceInfo::get_info_dict_from_resource(mesh);
							String path = get_path_res(mesh);
							String name;
							bool is_internal = path.is_empty() || path.get_file().contains("::");
							if (is_internal) {
								name = get_name_res(mesh_dict, mesh);
							} else {
								name = path.get_file().get_basename();
							}
							if (!name.is_empty()) {
								mesh_dict["name"] = demangle_name(name);
							}
							mesh_info.path = path;
							mesh_info.name = name;

							mesh_info.has_shadow_meshes = mesh->get_shadow_mesh().is_valid();
							mesh_info.has_lightmap_uv2 = mesh_info.has_lightmap_uv2 || mesh->get_lightmap_size_hint() != default_light_map_size;
							for (int surf_idx = 0; surf_idx < mesh->get_surface_count(); surf_idx++) {
								auto format = mesh->get_surface_format(surf_idx);
								mesh_info.has_tangents = mesh_info.has_tangents || ((format & Mesh::ARRAY_FORMAT_TANGENT) != 0);
								mesh_info.has_lods = mesh_info.has_lods || mesh->get_surface_lod_count(surf_idx) > 0;
								mesh_info.compression_enabled = mesh_info.compression_enabled || ((format & Mesh::ARRAY_FLAG_COMPRESS_ATTRIBUTES) != 0);
								// r_mesh_info.lightmap_uv2_texel_size = p_mesh->surface_get_lightmap_uv2_texel_size(surf_idx);
							}

							id_to_mesh_info.push_back(mesh_info);
						}
					}
					{
						TypedArray<Node> nodes = root->find_children("*", "MeshInstance3D");
						Array json_materials = json["materials"];
						auto materials = state->get_materials();
						for (int i = 0; i < materials.size(); i++) {
							Dictionary material_dict = json_materials[i];
							Ref<Material> material = materials[i];
							auto path = get_path_res(material);
							String name;
							bool is_internal = path.is_empty() || path.get_file().contains("::");
							if (is_internal) {
								name = get_name_res(material_dict, material);
							} else {
								name = path.get_file().get_basename();
							}
							if (!name.is_empty()) {
								material_dict["name"] = demangle_name(name);
							}
							id_to_material_path.push_back({ name, path });
						}
					}
				}
#if DEBUG_ENABLED
				if (p_dest_path.get_extension() == "glb") {
					// save a gltf copy for debugging
					auto gltf_path = p_dest_path.get_base_dir().path_join("GLTF/" + p_dest_path.get_file().get_basename() + ".gltf");
					gdre::ensure_dir(gltf_path.get_base_dir());
					_serialize_file(state, gltf_path);
				}
#endif
				p_err = _serialize_file(state, p_dest_path);
			}
			memdelete(root);
			ERR_FAIL_COND_V_MSG(p_err, ERR_FILE_CANT_WRITE, "Failed to write glTF document to " + p_dest_path);
			tinygltf::Model model;
			String error_string;
			// load it just to verify that it's valid
			p_err = load_model(p_dest_path, model, error_string);
			ERR_FAIL_COND_V_MSG(p_err, ERR_FILE_CORRUPT, "Failed to load glTF document from " + p_dest_path + ": " + error_string);
			return OK;
		};
		err = _export_scene();
		auto errors_after = using_threaded_load() ? GDRELogger::get_error_count() : GDRELogger::get_thread_error_count();
		if (err == OK && errors_after > errors_before) {
			err = ERR_PRINTER_ON_FIRE;
		}
		auto iinfo = p_report.is_valid() ? p_report->get_import_info() : Ref<ImportInfo>();
		if (iinfo.is_valid()) {
			ObjExporter::MeshInfo global_mesh_info;
			Vector<bool> global_has_tangents;
			Vector<bool> global_has_lods;
			Vector<bool> global_has_shadow_meshes;
			Vector<bool> global_has_lightmap_uv2;
			Vector<float> global_lightmap_uv2_texel_size;
			Vector<int> global_bake_mode;
			Vector<bool> global_compression_enabled;
			// push them back
			for (auto &E : id_to_mesh_info) {
				global_has_tangents.push_back(E.has_tangents);
				global_has_lods.push_back(E.has_lods);
				global_has_shadow_meshes.push_back(E.has_shadow_meshes);
				global_has_lightmap_uv2.push_back(E.has_lightmap_uv2);
				global_lightmap_uv2_texel_size.push_back(E.lightmap_uv2_texel_size);
				global_bake_mode.push_back(E.bake_mode);
				global_compression_enabled.push_back(E.compression_enabled);
			}
			global_mesh_info.has_tangents = global_has_tangents.count(true) > global_has_tangents.size() / 2;
			global_mesh_info.has_lods = global_has_lods.count(true) > global_has_lods.size() / 2;
			global_mesh_info.has_shadow_meshes = global_has_shadow_meshes.count(true) > global_has_shadow_meshes.size() / 2;
			global_mesh_info.has_lightmap_uv2 = global_has_lightmap_uv2.count(true) > global_has_lightmap_uv2.size() / 2;
			global_mesh_info.lightmap_uv2_texel_size = get_most_popular_value(global_lightmap_uv2_texel_size);
			global_mesh_info.bake_mode = get_most_popular_value(global_bake_mode);
			global_mesh_info.compression_enabled = global_compression_enabled.count(true) > global_compression_enabled.size() / 2;

			bool after_4_1 = iinfo->get_ver_major() > 4 || (iinfo->get_ver_major() == 4 && iinfo->get_ver_minor() > 1);
			bool after_4_3 = iinfo->get_ver_major() > 4 || (iinfo->get_ver_major() == 4 && iinfo->get_ver_minor() > 3);
			bool after_4_4 = iinfo->get_ver_major() > 4 || (iinfo->get_ver_major() == 4 && iinfo->get_ver_minor() > 4);
			bool has_any_image = image_formats.size() > 0;
			int image_handling_val = GLTFState::HANDLE_BINARY_EXTRACT_TEXTURES;
			if (has_any_image) {
				if (has_external_images) {
					image_handling_val = GLTFState::HANDLE_BINARY_EXTRACT_TEXTURES;
				} else {
					auto most_common_format = get_most_popular_value(image_formats);
					if (most_common_format == CompressedTexture2D::DATA_FORMAT_BASIS_UNIVERSAL) {
						image_handling_val = GLTFState::HANDLE_BINARY_EMBED_AS_BASISU;
					} else {
						image_handling_val = GLTFState::HANDLE_BINARY_EMBED_AS_UNCOMPRESSED;
					}
				}
			}

			// r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "nodes/root_type", PROPERTY_HINT_TYPE_STRING, "Node"), ""));
			// r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "nodes/root_name"), ""));
			iinfo->set_param("nodes/root_type", root_type);
			iinfo->set_param("nodes/root_name", root_name);

			iinfo->set_param("nodes/apply_root_scale", true);
			iinfo->set_param("nodes/root_scale", 1.0);
			iinfo->set_param("nodes/import_as_skeleton_bones", false);
			if (after_4_4) {
				iinfo->set_param("nodes/use_name_suffixes", true);
			}
			if (after_4_3) {
				iinfo->set_param("nodes/use_node_type_suffixes", true);
			}
			iinfo->set_param("meshes/ensure_tangents", global_mesh_info.has_tangents);
			iinfo->set_param("meshes/generate_lods", global_mesh_info.has_lods);
			iinfo->set_param("meshes/create_shadow_meshes", global_mesh_info.has_shadow_meshes);
			iinfo->set_param("meshes/light_baking", global_mesh_info.has_lightmap_uv2 ? 2 : global_mesh_info.bake_mode);
			iinfo->set_param("meshes/lightmap_texel_size", global_mesh_info.lightmap_uv2_texel_size);
			iinfo->set_param("meshes/force_disable_compression", !global_mesh_info.compression_enabled);
			iinfo->set_param("skins/use_named_skins", true);
			iinfo->set_param("animation/import", true);
			iinfo->set_param("animation/fps", 30);
			iinfo->set_param("animation/trimming", p_dest_path.get_extension().to_lower() == "fbx");
			iinfo->set_param("animation/remove_immutable_tracks", true);
			iinfo->set_param("animation/import_rest_as_RESET", false);
			iinfo->set_param("import_script/path", "");
			// 		r_options->push_back(ResourceImporterScene::ImportOption(PropertyInfo(Variant::INT, "gltf/naming_version", PROPERTY_HINT_ENUM, "Godot 4.1 or 4.0,Godot 4.2 or later"), 1));
			// r_options->push_back(ResourceImporterScene::ImportOption(PropertyInfo(Variant::INT, "gltf/embedded_image_handling", PROPERTY_HINT_ENUM, "Discard All Textures,Extract Textures,Embed as Basis Universal,Embed as Uncompressed", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), GLTFState::HANDLE_BINARY_EXTRACT_TEXTURES));
			// Godot 4.2 and above blow out the import params, so we need to update them to point to the external resources.
			Dictionary _subresources_dict = Dictionary();
			if (iinfo->has_param("_subresources")) {
				_subresources_dict = iinfo->get_param("_subresources");
			} else {
				iinfo->set_param("_subresources", _subresources_dict);
			}

			if (after_4_1) {
				iinfo->set_param("gltf/naming_version", after_4_1 ? 1 : 0);
			}

			iinfo->set_param("gltf/embedded_image_handling", image_handling_val);

			if (animation_options.size() > 0) {
				if (!_subresources_dict.has("animations")) {
					_subresources_dict["animations"] = Dictionary();
				}
				Dictionary animations_dict = _subresources_dict["animations"];
				for (auto &E : animation_options) {
					// "save_to_file/enabled": true,
					// "save_to_file/keep_custom_tracks": true,
					// "save_to_file/path": "res://models/Enemies/cultist-shoot-anim.res",
					animations_dict[E.key] = E.value;
					String path = animations_dict[E.key].operator Dictionary().get("save_to_file/path", String());
					if (!(path.is_empty() || path.get_file().contains("::"))) {
						external_deps_updated.insert(path);
					}
				}
			}
			if (id_to_mesh_info.size() > 0) {
				if (!_subresources_dict.has("meshes")) {
					_subresources_dict["meshes"] = Dictionary();
				}

				Dictionary mesh_Dict = _subresources_dict["meshes"];
				for (auto &E : id_to_mesh_info) {
					auto name = E.name;
					auto path = E.path;
					if (name.is_empty() || mesh_Dict.has(name)) {
						ERR_CONTINUE(name.is_empty() || mesh_Dict.has(name));
						// continue;
					}
					// "save_to_file/enabled": true,
					// "save_to_file/path": "res://models/Enemies/cultist-shoot-anim.res",
					mesh_Dict[name] = Dictionary();
					Dictionary subres = mesh_Dict[name];
					if (path.is_empty() || path.get_file().contains("::")) {
						subres["save_to_file/enabled"] = false;
						subres["save_to_file/path"] = "";
					} else {
						subres["save_to_file/enabled"] = true;
						subres["save_to_file/path"] = path;
						external_deps_updated.insert(path);
					}
					subres["generate/shadow_meshes"] = E.has_shadow_meshes ? 1 : 0;
					subres["generate/lightmap_uv"] = E.has_lightmap_uv2 ? 1 : 0;
					subres["generate/lods"] = E.has_lods ? 1 : 0;
					subres["lods/normal_merge_angle"] = 60.0f; // TODO: get this somehow??
				}
			}
			if (id_to_material_path.size() > 0) {
				if (!_subresources_dict.has("materials")) {
					_subresources_dict["materials"] = Dictionary();
				}

				Dictionary mat_Dict = _subresources_dict["materials"];
				for (auto &E : id_to_material_path) {
					auto name = E.first;
					auto path = E.second;
					if (name.is_empty() || mat_Dict.has(name)) {
						continue;
					}
					mat_Dict[name] = Dictionary();
					Dictionary subres = mat_Dict[name];
					if (path.is_empty() || path.get_file().contains("::")) {
						subres["use_external/enabled"] = false;
						subres["use_external/path"] = "";
					} else {
						subres["use_external/enabled"] = true;
						subres["use_external/path"] = path;
						external_deps_updated.insert(path);
					}
				}
			}

			iinfo->set_param("_subresources", _subresources_dict);
		}
	}

	// remove the UIDs that we added that didn't exist before
	for (uint64_t id : texture_uids) {
		ResourceUID::get_singleton()->remove_id(id);
	}

	set_all_externals = external_deps_updated.size() == need_to_be_updated.size();
	// GLTF Exporter has issues with custom animations and throws errors;
	// if we've set all the external resources (including custom animations),
	// then this isn't an error.
	if (err == ERR_PRINTER_ON_FIRE && has_external_animation && set_all_externals) {
		err = OK;
		error_messages.append_array(supports_multithread() ? GDRELogger::get_thread_errors() : GDRELogger::get_errors());
		for (auto &msg : error_messages) {
			auto message = msg.strip_edges();
			if (message.to_lower().contains("animated track") || message.begins_with("at:") || message.begins_with("GDScript backtrace") || message.begins_with("WARNING:")) {
				continue;
			}
			err = ERR_PRINTER_ON_FIRE;
			break;
		}
	}
	GDRE_SCN_EXP_FAIL_COND_V_MSG(err, err, "");
	if (!set_all_externals || has_script || has_shader) {
		err = ERR_PRINTER_ON_FIRE;
	}
	return OK;
}

Error SceneExporter::export_file(const String &p_dest_path, const String &p_src_path) {
	String ext = p_dest_path.get_extension().to_lower();
	if (ext != "escn" && ext != "tscn") {
		int ver_major = get_ver_major(p_src_path);
		ERR_FAIL_COND_V_MSG(ver_major != 4, ERR_UNAVAILABLE, "Scene export for engine version " + itos(ver_major) + " is not currently supported.");
	}
	return _export_file(p_dest_path, p_src_path, nullptr);
}

Error SceneExporter::export_file_to_obj(const String &p_dest_path, const String &p_src_path, int ver_major, ObjExporter::MeshInfo &r_mesh_info) {
	Error err;
	Ref<PackedScene> scene;
	// For some reason, scenes with meshes fail to load without the load done by ResourceLoader::load, possibly due to notification shenanigans.
	if (ResourceCompatLoader::is_globally_available() && using_threaded_load()) {
		scene = ResourceLoader::load(p_src_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_REUSE, &err);
	} else {
		scene = ResourceCompatLoader::custom_load(p_src_path, "PackedScene", ResourceCompatLoader::get_default_load_type(), &err, using_threaded_load(), ResourceFormatLoader::CACHE_MODE_REUSE);
	}
	ERR_FAIL_COND_V_MSG(err, ERR_FILE_CANT_READ, "Failed to load scene " + p_src_path);
	Vector<Ref<ArrayMesh>> meshes;
	Vector<Ref<ArrayMesh>> meshes_to_remove;

	auto resources = _find_resources(scene, true, ver_major);
	for (auto &E : resources) {
		Ref<ArrayMesh> mesh = E;
		if (mesh.is_valid()) {
			meshes.push_back(mesh);
			auto shadow_mesh = mesh->get_shadow_mesh();
			if (shadow_mesh.is_valid()) {
				meshes_to_remove.push_back(shadow_mesh);
			}
		}
	}
	for (auto &mesh : meshes_to_remove) {
		meshes.erase(mesh);
	}

	return ObjExporter::_write_meshes_to_obj(meshes, p_dest_path, p_dest_path.get_base_dir(), r_mesh_info);
}

Ref<ExportReport> SceneExporter::export_resource(const String &output_dir, Ref<ImportInfo> iinfo) {
	Ref<ExportReport> report = memnew(ExportReport(iinfo));

	Error err;
	Vector<uint64_t> texture_uids;
	String orignal_export_dest = iinfo->get_export_dest();
	String new_path = orignal_export_dest;
	String ext = new_path.get_extension().to_lower();
	bool to_text = ext == "escn" || ext == "tscn";
	bool to_obj = ext == "obj";
	// GLTF export can result in inaccurate models
	// save it under .assets, which won't be picked up for import by the godot editor
	if (!to_text && !to_obj) {
		new_path = new_path.replace("res://", "res://.assets/");
		// we only export glbs
		if (ext != "glb" && ext != "gltf") {
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
		err = _export_file(dest_path, iinfo->get_path(), report);
	}
	if (err == ERR_PRINTER_ON_FIRE) {
		report->set_saved_path(dest_path);
	} else if (err == OK && !to_text && !to_obj) {
		// TODO: Turn this on when we feel confident that we can tell that are exporting correctly
		// TODO: do real GLTF validation
		// TODO: fix errors where some models aren't being textured?
		// move the file to the correct location
		auto new_dest = output_dir.path_join(orignal_export_dest.replace("res://", ""));
		auto da = DirAccess::create_for_path(new_dest.get_base_dir());
		if (da.is_valid() && da->rename(dest_path, new_dest) == OK) {
			iinfo->set_export_dest(orignal_export_dest);
			report->set_new_source_path("");
			report->set_saved_path(new_dest);
		}
	}

#if DEBUG_ENABLED
	if (err && err != ERR_UNAVAILABLE) {
		// save it as a text scene so we can see what went wrong
		auto new_new_path = ".gltf_copy/" + new_path.trim_prefix("res://.assets/").get_basename() + ".tscn";
		auto new_dest = output_dir.path_join(new_new_path);
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
