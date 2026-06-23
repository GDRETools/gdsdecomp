#include "scene_exporter.h"

#include "compat/resource_loader_compat.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/property_info.h"
#include "core/os/thread_safe.h"
#include "core/variant/variant.h"
#include "core/version_generated.gen.h"
#include "exporters/export_report.h"
#include "exporters/obj_exporter.h"
#include "external/tinygltf/tiny_gltf.h"
#include "modules/gltf/extensions/physics/gltf_document_extension_physics.h"
#include "modules/gltf/gltf_document.h"
#include "modules/gltf/structures/gltf_node.h"
#include "modules/regex/regex.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/navigation/navigation_region_3d.h"
#include "scene/3d/occluder_instance_3d.h"
#include "scene/3d/physics/area_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/surface_tool.h"
#include "scene/resources/texture.h"
#include "utility/common.h"
#include "utility/gdre_config.h"
#include "utility/gdre_logger.h"
#include "utility/gdre_settings.h"
#include "utility/shader_material_converter.h"

#include "core/crypto/crypto_core.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "main/gdre_main_loop.h"
#include "main/main.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/packed_scene.h"
#include "utility/task_manager.h"

#ifndef PRINT_PERF_CSV
#define PRINT_PERF_CSV 0
#endif

struct dep_info {
	ResourceUID::ID uid = ResourceUID::INVALID_ID;
	String dep;
	String remap;
	String orig_remap;
	String type;
	String real_type;
	int depth = 0;
	bool exists = true;
	bool uid_in_uid_cache = false;
	bool uid_in_uid_cache_matches_dep = true;
	bool uid_remap_path_exists = true;
	bool parent_is_script_or_shader = false;
};

Ref<GLTFDocumentExtensionPhysics> get_physics_extension() {
	Ref<GLTFDocumentExtensionPhysics> physics_ext;
	for (auto &ext : GLTFDocument::get_all_gltf_document_extensions()) {
		physics_ext = ext;
		if (physics_ext.is_valid()) {
			break;
		}
	}
	return physics_ext;
}

Ref<GLTFDocumentExtensionPhysicsRemover> get_physics_remover_extension() {
	Ref<GLTFDocumentExtensionPhysicsRemover> physics_remover_ext;
	for (auto &ext : GLTFDocument::get_all_gltf_document_extensions()) {
		physics_remover_ext = ext;
		if (physics_remover_ext.is_valid()) {
			break;
		}
	}
	return physics_remover_ext;
}

void unregister_physics_extension() {
	auto physics_ext = get_physics_extension();
	if (physics_ext.is_valid()) {
		GLTFDocument::unregister_gltf_document_extension(physics_ext);
	}
	auto physics_remover_ext = get_physics_remover_extension();
	if (!physics_remover_ext.is_valid()) {
		GLTFDocument::register_gltf_document_extension(memnew(GLTFDocumentExtensionPhysicsRemover));
	}
}

void register_physics_extension() {
	Ref<GLTFDocumentExtensionPhysics> physics_ext = get_physics_extension();
	if (!physics_ext.is_valid()) {
		GLTFDocument::register_gltf_document_extension(memnew(GLTFDocumentExtensionPhysics), true);
	}
	Ref<GLTFDocumentExtensionPhysicsRemover> physics_remover_ext = get_physics_remover_extension();
	if (physics_remover_ext.is_valid()) {
		GLTFDocument::unregister_gltf_document_extension(physics_remover_ext);
	}
}

void _add_indent(String &r_result, const String &p_indent, int p_size) {
	if (p_indent.is_empty()) {
		return;
	}
	for (int i = 0; i < p_size; i++) {
		r_result += p_indent;
	}
}

void _stringify_json(String &r_result, const Variant &p_var, const String &p_indent, int p_cur_indent, bool p_sort_keys, bool force_single_precision, HashSet<const void *> &p_markers) {
	if (p_cur_indent > Variant::MAX_RECURSION_DEPTH) {
		r_result += "...";
		ERR_FAIL_MSG("JSON structure is too deep. Bailing.");
	}

	const char *colon = p_indent.is_empty() ? ":" : ": ";
	const char *end_statement = p_indent.is_empty() ? "" : "\n";

	switch (p_var.get_type()) {
		case Variant::NIL:
			r_result += "null";
			return;
		case Variant::BOOL:
			r_result += p_var.operator bool() ? "true" : "false";
			return;
		case Variant::INT:
			r_result += itos(p_var);
			return;
		case Variant::FLOAT: {
			double num = p_var;

			// Only for exactly 0. If we have approximately 0 let the user decide how much
			// precision they want.
			if (num == double(0)) {
				r_result += "0.0";
				return;
			}

			// No NaN in JSON.
			if (Math::is_nan(num)) {
				r_result += "null";
				return;
			}

			// No Infinity in JSON; use a value that will be parsed as Infinity/-Infinity.
			if (std::isinf(num)) {
				if (num < 0.0) {
					r_result += "-1.0e+511";
				} else {
					r_result += "1.0e+511";
				}
				return;
			}

			String num_str;
			if (force_single_precision || (double)(float)num == num) {
				num_str = String::num_scientific((float)num);
			} else {
				num_str = String::num_scientific(num);
			}
			r_result += num_str;
			if (!(num_str.contains_char('.') || num_str.contains_char('e'))) {
				r_result += ".0";
			}
			return;
		}
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::ARRAY: {
			Array a = p_var;
			if (p_markers.has(a.id())) {
				r_result += "\"[...]\"";
				ERR_FAIL_MSG("Converting circular structure to JSON.");
			}

			if (a.is_empty()) {
				r_result += "[]";
				return;
			}

			r_result += '[';
			r_result += end_statement;

			p_markers.insert(a.id());

			bool first = true;
			for (const Variant &var : a) {
				if (first) {
					first = false;
				} else {
					r_result += ',';
					r_result += end_statement;
				}
				_add_indent(r_result, p_indent, p_cur_indent + 1);
				_stringify_json(r_result, var, p_indent, p_cur_indent + 1, p_sort_keys, force_single_precision, p_markers);
			}
			r_result += end_statement;
			_add_indent(r_result, p_indent, p_cur_indent);
			r_result += ']';
			p_markers.erase(a.id());
			return;
		}
		case Variant::DICTIONARY: {
			Dictionary d = p_var;
			if (p_markers.has(d.id())) {
				r_result += "\"{...}\"";
				ERR_FAIL_MSG("Converting circular structure to JSON.");
			}

			r_result += '{';
			r_result += end_statement;
			p_markers.insert(d.id());

			LocalVector<Variant> keys = d.get_key_list();

			if (p_sort_keys) {
				keys.sort_custom<StringLikeVariantOrder>();
			}

			bool first_key = true;
			for (const Variant &key : keys) {
				if (first_key) {
					first_key = false;
				} else {
					r_result += ',';
					r_result += end_statement;
				}
				_add_indent(r_result, p_indent, p_cur_indent + 1);
				_stringify_json(r_result, String(key), p_indent, p_cur_indent + 1, p_sort_keys, force_single_precision, p_markers);
				r_result += colon;
				_stringify_json(r_result, d[key], p_indent, p_cur_indent + 1, p_sort_keys, force_single_precision, p_markers);
			}

			r_result += end_statement;
			_add_indent(r_result, p_indent, p_cur_indent);
			r_result += '}';
			p_markers.erase(d.id());
			return;
		}
		default:
			r_result += '"';
			r_result += String(p_var).json_escape();
			r_result += '"';
			return;
	}
}

String stringify_json(const Variant &p_var, const String &p_indent, bool p_sort_keys, bool force_single_precision) {
	String result;
	HashSet<const void *> markers;
	_stringify_json(result, p_var, p_indent, 0, p_sort_keys, force_single_precision, markers);
	return result;
}

#define MAX_DEPTH 256
void get_deps_recursive(const String &p_path, HashMap<String, dep_info> &r_deps, bool parent_is_script_or_shader = false, int depth = 0) {
	if (depth > MAX_DEPTH) {
		ERR_PRINT("Dependency recursion depth exceeded.");
		return;
	}
	List<String> deps;
	ResourceCompatLoader::get_dependencies(p_path, &deps, true);
	Vector<String> deferred_script_or_shader_deps;
	for (const String &dep : deps) {
		if (!r_deps.has(dep)) {
			r_deps[dep] = dep_info{};
			dep_info &info = r_deps[dep];
			info.depth = depth;
			info.parent_is_script_or_shader = parent_is_script_or_shader;
			auto splits = dep.split("::");
			if (splits.size() == 3) {
				// If it has a UID, UID is first, followed by type, then fallback path
				String uid_text = splits[0];
				info.uid = splits[0].is_empty() ? ResourceUID::INVALID_ID : ResourceUID::get_singleton()->text_to_id(uid_text);
				info.type = splits[1];
				info.dep = splits[2];
				info.uid_in_uid_cache = info.uid != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->has_id(info.uid);
				auto uid_path = info.uid_in_uid_cache ? ResourceUID::get_singleton()->get_id_path(info.uid) : "";
				info.orig_remap = GDRESettings::get_singleton()->get_mapped_path(info.dep);
				if (info.uid_in_uid_cache && uid_path != info.dep && uid_path != info.orig_remap) {
					info.uid_in_uid_cache_matches_dep = false;
					info.remap = GDRESettings::get_singleton()->get_mapped_path(uid_path);
					if (!FileAccess::exists(info.remap)) {
						info.uid_remap_path_exists = false;
						info.remap = "";
					}
				}
				if (info.remap.is_empty()) {
					info.remap = info.orig_remap;
				}
				auto thingy = GDRESettings::get_singleton()->get_mapped_path(splits[0]);
				if (!FileAccess::exists(info.remap)) {
					if (FileAccess::exists(info.dep)) {
						info.remap = info.dep;
					} else {
						info.exists = false;
						continue;
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
				info.real_type = ResourceCompatLoader::get_resource_type(info.remap);
				if (info.real_type == "Unknown") {
					auto ext = info.remap.get_extension().to_lower();
					if (ext == "gd" || ext == "gdc") {
						info.real_type = "GDScript";
					} else if (ext == "glsl" || ext == "glslv" || ext == "glslh" || ext == "glslc") {
						info.real_type = "GLSLShader";
					} else if (ext == "gdshader" || ext == "shader") {
						info.real_type = "Shader";
					} else if (ext == "cs") {
						info.real_type = "CSharpScript";
					} else {
						// just use the type
						info.real_type = info.type;
					}
				}
				if (!(info.real_type.contains("Script") || info.real_type.contains("Shader"))) {
					get_deps_recursive(info.remap, r_deps, false, depth + 1);
				} else {
					// defer to after the script/shader deps are processed so that if a non-script/shader has a dependency on the same dep(s) as a script/shader,
					// the non-script/shader will be processed first and have parent_is_script_or_shader set to false
					deferred_script_or_shader_deps.push_back(info.remap);
				}
			} else {
				info.exists = false;
			}
		}
	}
	for (const String &dep : deferred_script_or_shader_deps) {
		get_deps_recursive(dep, r_deps, true, depth + 1);
	}
}

bool GLBExporterInstance::using_threaded_load() const {
	// If the scenes are being exported using the worker task pool, we can't use threaded load
	return true;
}

Error load_model(const String &p_filename, tinygltf::Model &model, String &r_error) {
	tinygltf::TinyGLTF loader;
	loader.SetImagesAsIs(true);
	std::string filename = p_filename.utf8().get_data();
	std::string error;
	std::string warning;
	bool is_binary = p_filename.get_extension().to_lower() == "glb";
	bool state = is_binary ? loader.LoadBinaryFromFile(&model, &error, &warning, filename) : loader.LoadASCIIFromFile(&model, &error, &warning, filename);
	if (error.size() > 0) { // validation errors, ignore for right now
		r_error.append_utf8(error.c_str());
	}
	if (!state) {
		return ERR_FILE_CANT_READ;
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

inline bool _all_buffers_empty(const Vector<Vector<uint8_t>> &p_buffers, int start_idx = 0) {
	for (int i = start_idx; i < p_buffers.size(); i++) {
		if (!p_buffers[i].is_empty()) {
			return false;
		}
	}
	return true;
}

Error _encode_buffer_glb(Ref<GLTFState> p_state, const String &p_path, Vector<String> &r_buffer_paths) {
	auto state_buffers = p_state->get_buffers();
	print_verbose("glTF: Total buffers: " + itos(state_buffers.size()));

	if (state_buffers.is_empty() || _all_buffers_empty(state_buffers)) {
		ERR_FAIL_COND_V_MSG(!p_state->get_buffer_views().is_empty(), ERR_INVALID_DATA, "glTF: Buffer views are present, but buffers are empty.");
		return OK;
	}
	Array buffers;
	Dictionary gltf_buffer;

	gltf_buffer["byteLength"] = state_buffers[0].size();
	buffers.push_back(gltf_buffer);

	for (GLTFBufferIndex i = 1; i < state_buffers.size() - 1; i++) {
		Vector<uint8_t> buffer_data = state_buffers[i];
		if (buffer_data.is_empty()) {
			if (i < state_buffers.size() - 1 && !_all_buffers_empty(state_buffers, i + 1)) {
				// have to push back a dummy buffer to avoid changing the buffer index
				WARN_PRINT("glTF: Buffer " + itos(i) + " is empty, but there are non-empty subsequent buffers.");
				gltf_buffer["byteLength"] = 0;
				buffers.push_back(gltf_buffer);
			}
			continue;
		}
		Dictionary gltf_buffer;
		String filename = p_path.get_basename().get_file() + itos(i) + ".bin";
		String path = p_path.get_base_dir() + "/" + filename;
		Error err;
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
		if (file.is_null()) {
			return err;
		}
		r_buffer_paths.push_back(path);
		file->create(FileAccess::ACCESS_RESOURCES);
		file->store_buffer(buffer_data.ptr(), buffer_data.size());
		gltf_buffer["uri"] = filename;
		gltf_buffer["byteLength"] = buffer_data.size();
		buffers.push_back(gltf_buffer);
	}
	p_state->get_json()["buffers"] = buffers;

	return OK;
}

Error _encode_buffer_bins(Ref<GLTFState> p_state, const String &p_path, Vector<String> &r_buffer_paths) {
	auto state_buffers = p_state->get_buffers();
	print_verbose("glTF: Total buffers: " + itos(state_buffers.size()));

	if (state_buffers.is_empty() || _all_buffers_empty(state_buffers)) {
		ERR_FAIL_COND_V_MSG(!p_state->get_buffer_views().is_empty(), ERR_INVALID_DATA, "glTF: Buffer views are present, but buffers are empty.");
		return OK;
	}
	Array buffers;

	for (GLTFBufferIndex i = 0; i < state_buffers.size(); i++) {
		Vector<uint8_t> buffer_data = state_buffers[i];
		Dictionary gltf_buffer;
		if (buffer_data.is_empty()) {
			if (i < state_buffers.size() - 1 && !_all_buffers_empty(state_buffers, i + 1)) {
				// have to push back a dummy buffer to avoid changing the buffer index
				WARN_PRINT("glTF: Buffer " + itos(i) + " is empty, but there are non-empty subsequent buffers.");
				gltf_buffer["byteLength"] = 0;
				buffers.push_back(gltf_buffer);
			}
			continue;
		}
		String filename = p_path.get_basename().get_file() + itos(i) + ".bin";
		String path = p_path.get_base_dir() + "/" + filename;
		Error err;
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
		if (file.is_null()) {
			return err;
		}
		r_buffer_paths.push_back(path);
		file->create(FileAccess::ACCESS_RESOURCES);
		file->store_buffer(buffer_data.ptr(), buffer_data.size());
		gltf_buffer["uri"] = filename;
		gltf_buffer["byteLength"] = buffer_data.size();
		buffers.push_back(gltf_buffer);
	}
	if (!buffers.is_empty()) {
		p_state->get_json()["buffers"] = buffers;
	}

	return OK;
}

Error _serialize_file(Ref<GLTFState> p_state, const String p_path, Vector<String> &r_buffer_paths, bool p_force_single_precision) {
	Error err = FAILED;
	if (p_path.to_lower().ends_with("glb")) {
		err = _encode_buffer_glb(p_state, p_path, r_buffer_paths);
		ERR_FAIL_COND_V(err != OK, err);
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V(file.is_null(), FAILED);

		String json = stringify_json(p_state->get_json(), "", true, p_force_single_precision);

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
		err = _encode_buffer_bins(p_state, p_path, r_buffer_paths);
		ERR_FAIL_COND_V(err != OK, err);
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V(file.is_null(), FAILED);

		file->create(FileAccess::ACCESS_RESOURCES);
		String json = stringify_json(Variant(p_state->get_json()), indent, true, p_force_single_precision);
		file->store_string(json);
	}
	return err;
}

int GLBExporterInstance::get_ver_major(const String &res_path) {
	Error err;
	auto info = ResourceCompatLoader::get_resource_info(res_path, "", &err);
	ERR_FAIL_COND_V_MSG(err != OK, 0, "Failed to get resource info for " + res_path);
	return info->ver_major; // Placeholder return value
}

String GLBExporterInstance::add_errors_to_report(Error p_err, const String &err_msg) {
	String step;
	switch (p_err) {
		case ERR_FILE_MISSING_DEPENDENCIES:
			step = "dependency resolution";
			break;
		case ERR_FILE_CANT_OPEN:
			step = "scene resource load";
			break;
		case ERR_CANT_ACQUIRE_RESOURCE:
			step = "instancing scene resource";
			break;
		case ERR_COMPILATION_FAILED:
			step = "appending to GLTF document";
			break;
		case ERR_FILE_CANT_WRITE:
			step = "serializing GLTF document";
			break;
		case ERR_FILE_CORRUPT:
			step = "GLTF is empty or corrupt";
			break;
		case ERR_BUG:
			step = "GLTF conversion";
			break;
		case ERR_PRINTER_ON_FIRE:
			step = "rewriting import settings";
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
	if (p_err == ERR_SKIP) {
		err_message = "Export was cancelled";
	} else if (p_err == ERR_TIMEOUT) {
		err_message = "Export timed out";
	} else if (p_err == OK) {
		err_message = "";
	}
	if (!err_msg.is_empty()) {
		err_message += ":\n  " + err_msg;
	}
	if (!desc.is_empty()) {
		err_message += "\n  Scene had " + desc;
	}
	error_statement = err_message;
	Vector<String> errors;
	if (scene_loading_error_messages.size() > 0) {
		errors.append("** Errors during scene loading:");
		errors.append_array(scene_loading_error_messages);
	}
	if (scene_instantiation_error_messages.size() > 0) {
		errors.append("** Errors during scene instantiation:");
		errors.append_array(scene_instantiation_error_messages);
	}
	if (gltf_serialization_error_messages.size() > 0) {
		errors.append("** Errors during GLTF conversion:");
		errors.append_array(gltf_serialization_error_messages);
	}
	if (import_param_error_messages.size() > 0) {
		errors.append("** Errors during import parameter setting:");
		errors.append_array(import_param_error_messages);
	}
	other_error_messages.append_array(_get_logged_error_messages());
	if (other_error_messages.size() > 0) {
		errors.append("** Other errors:");
		errors.append_array(other_error_messages);
	}
	for (const auto &E : get_deps_map) {
		dependency_resolution_list.append(vformat("  %s -> %s, exists: %s", E.key, E.value.remap, E.value.exists ? "yes" : "no"));
	}
	bool validation_error = p_err == ERR_PRINTER_ON_FIRE || p_err == ERR_BUG;

	if (report.is_valid()) {
		if (validation_error) {
			if (!project_recovery) {
				// Only relevant for project recovery mode
				p_err = OK;
			} else {
				// TODO: make the metadata rewriter in import exporter not require ERR_PRINTER_ON_FIRE to be set to write dirty metadata when there are errors
				p_err = ERR_PRINTER_ON_FIRE;
			}
		}
		report->set_message(error_statement + "\n");
		Dictionary extra_info = report->get_extra_info();
		extra_info["dependencies"] = dependency_resolution_list;
		report->set_extra_info(extra_info);
		report->set_error(p_err);
		report->append_error_messages(errors);
	}
	String printed_error_message = (!validation_error ? vformat("Failed to export scene %s:\n  %s", source_path, err_message) : vformat("GLTF validation failed for scene %s:\n  %s", source_path, err_message));

	return printed_error_message;
}

void GLBExporterInstance::set_path_options(Dictionary &import_opts, const String &path, const String &prefix) {
	if (after_4_4) {
		ResourceUID::ID uid = path.is_empty() ? ResourceUID::INVALID_ID : GDRESettings::get_singleton()->get_uid_for_path(path);
		if (uid != ResourceUID::INVALID_ID) {
			import_opts[prefix + "/path"] = ResourceUID::get_singleton()->id_to_text(uid);
		} else {
			import_opts[prefix + "/path"] = path;
		}
		import_opts[prefix + "/fallback_path"] = path;
	} else {
		import_opts[prefix + "/path"] = path;
	}
}

String GLBExporterInstance::get_path_options(const Dictionary &import_opts) {
	if (after_4_4) {
		return import_opts.get("save_to_file/fallback_path", import_opts.get("save_to_file/path", ""));
	}
	return import_opts.get("save_to_file/path", "");
}

void GLBExporterInstance::set_cache_res(const dep_info &info, const Ref<Resource> &texture, bool force_replace) {
	if (texture.is_null()) {
		loaded_deps.push_back(texture);
		return;
	}
	if (!force_replace) {
		if (auto existing = ResourceCache::get_ref(info.dep); existing.is_valid()) {
			loaded_deps.push_back(existing);
			return;
		}
	}
#ifdef TOOLS_ENABLED
	texture->set_import_path(info.remap);
#endif
	// reset the path cache, then set the path so it loads it into cache.
	texture->set_path_cache("");
	texture->set_path(info.dep, true);
	loaded_deps.push_back(texture);
}

bool GLBExporterInstance::check_cached_res(const dep_info &info) {
	if (auto existing = ResourceCache::get_ref(info.dep); existing.is_valid()) {
		loaded_deps.push_back(existing);
		return true;
	}
	return false;
}

String GLBExporterInstance::get_name_res(const Dictionary &dict, const Ref<Resource> &res, int64_t idx) {
	String name;
	name = dict.get("name", String());
	if (name.is_empty()) {
		name = res->get_name();
		if (name.is_empty()) {
			Ref<ResourceInfo> info = ResourceInfo::get_info_from_resource(res);
			if (info.is_valid() && !info->resource_name.is_empty()) {
				name = info->resource_name;
			}
		}
	}
	return name;
}

String GLBExporterInstance::get_path_res(const Ref<Resource> &res) {
	String path = res->get_path();
	if (path.is_empty()) {
		Ref<ResourceInfo> info = ResourceInfo::get_info_from_resource(res);
		if (info.is_valid() && !info->original_path.is_empty()) {
			path = info->original_path;
		}
	}
	return path;
}

inline bool get_count_majority(const Vector<bool> &p_values) {
	int64_t count = p_values.count(true);
	return count >= p_values.size() - count;
}

ObjExporter::MeshInfo GLBExporterInstance::_get_mesh_options_for_import_params() {
	ObjExporter::MeshInfo global_mesh_info;
	Vector<bool> global_has_tangents;
	Vector<bool> global_has_lods;
	Vector<bool> global_has_shadow_meshes;
	Vector<bool> global_has_lightmap_uv2;
	Vector<float> global_lightmap_uv2_texel_size;
	Vector<int> global_bake_mode;
	// push them back
	for (auto &E : id_to_mesh_info) {
		global_has_tangents.push_back(E.has_tangents);
		global_has_lods.push_back(E.has_lods);
		global_has_shadow_meshes.push_back(E.has_shadow_meshes);
		global_has_lightmap_uv2.push_back(E.has_lightmap_uv2);
		global_lightmap_uv2_texel_size.push_back(E.lightmap_uv2_texel_size);
		global_bake_mode.push_back(E.bake_mode);
		// compression enabled is used for forcing disabling, so if ANY of them have it on, we need to set it on
		global_mesh_info.compression_enabled = global_mesh_info.compression_enabled || E.compression_enabled;
	}
	global_mesh_info.has_tangents = get_count_majority(global_has_tangents);
	global_mesh_info.has_lods = get_count_majority(global_has_lods);
	global_mesh_info.has_shadow_meshes = get_count_majority(global_has_shadow_meshes);
	global_mesh_info.has_lightmap_uv2 = get_count_majority(global_has_lightmap_uv2);
	global_mesh_info.lightmap_uv2_texel_size = gdre::get_most_popular_value(global_lightmap_uv2_texel_size);
	global_mesh_info.bake_mode = gdre::get_most_popular_value(global_bake_mode);

	return global_mesh_info;
}

String GLBExporterInstance::get_resource_path(const Ref<Resource> &res) {
	String path = res->get_path();
	if (path.is_empty()) {
		Ref<ResourceInfo> compat = ResourceInfo::get_info_from_resource(res);
		if (compat.is_valid()) {
			path = compat->original_path;
		}
	}
	return path;
}

#define GDRE_SCN_EXP_FAIL_V_MSG(err, msg)                            \
	{                                                                \
		[[maybe_unused]] auto _err = add_errors_to_report(err, msg); \
		return err;                                                  \
	}

#define GDRE_SCN_EXP_FAIL_COND_V_MSG(cond, err, msg) \
	if (unlikely(cond)) {                            \
		GDRE_SCN_EXP_FAIL_V_MSG(err, msg);           \
	}

void GLBExporterInstance::_initial_set(const String &p_src_path, Ref<ExportReport> p_report) {
	report = p_report;
	iinfo = p_report.is_valid() ? p_report->get_import_info() : nullptr;
	res_info = ResourceCompatLoader::get_resource_info(p_src_path, "");
	if (iinfo.is_valid()) {
		ver_major = iinfo->get_ver_major();
		ver_minor = iinfo->get_ver_minor();
	} else {
		if (res_info.is_valid() && res_info->get_resource_format() != "text") {
			ver_major = res_info->ver_major;
			ver_minor = res_info->ver_minor;
		} else {
			ver_major = GDRESettings::get_singleton()->get_ver_major();
			ver_minor = GDRESettings::get_singleton()->get_ver_minor();
		}
	}
	source_path = p_src_path;
	after_4_1 = (ver_major > 4 || (ver_major == 4 && ver_minor > 1));
	after_4_3 = (ver_major > 4 || (ver_major == 4 && ver_minor > 3));
	after_4_4 = (ver_major > 4 || (ver_major == 4 && ver_minor > 4));
	updating_import_info = !force_no_update_import_params && iinfo.is_valid() && iinfo->get_ver_major() >= 4;

	if (iinfo.is_valid()) {
		scene_name = iinfo->get_source_file().get_file().get_basename();
	} else {
		if (res_info.is_valid()) {
			scene_name = res_info->resource_name;
		}
		if (scene_name.is_empty()) {
			scene_name = source_path.get_file().get_basename();
		}
	}
}

Error GLBExporterInstance::_load_deps() {
	other_error_messages.append_array(_get_logged_error_messages());
	get_deps_recursive(source_path, get_deps_map);

	for (auto &E : get_deps_map) {
		dep_info &info = E.value;
		if (info.type == "Script") {
			has_script = true;
		} else if (info.type == "Shader") {
			has_shader = true;
		} else {
			if (info.type == "Animation" || info.type == "AnimationLibrary") {
				animation_deps_needed.insert(info.dep);
				if (info.depth == 0) {
					need_to_be_updated.insert(info.dep);
				}
			} else if (info.type.contains("Material")) {
				if (info.real_type == "ShaderMaterial") {
					has_shader = true;
				}
				if (info.depth == 0) {
					need_to_be_updated.insert(info.dep);
				}
			} else if (info.type.contains("Texture")) {
				image_deps_needed.insert(info.dep);
				String ext = info.dep.get_extension().to_upper();
				if (ext == "JPG") {
					ext = "JPEG";
				}
				image_extensions.append(ext);
				if (info.depth == 0) {
					need_to_be_updated.insert(info.dep);
				}
			} else if (info.type.contains("Mesh")) {
				if (info.depth == 0) {
					need_to_be_updated.insert(info.dep);
				}
			}
		}
	}

	if (report.is_valid()) {
		Dictionary extra_info = report->get_extra_info();
		extra_info["has_script"] = has_script;
		extra_info["has_shader"] = has_shader;
		report->set_extra_info(extra_info);
	}

	// Don't need this right now, we just instance shader to a missing resource
	// If GLTF exporter somehow starts making use of them, we'll have to do this
	// bool is_default_gltf_load = ResourceCompatLoader::is_default_gltf_load();
	// if (has_shader) {
	// 	print_line("This scene has shaders, which may not be compatible with the exporter.");
	// 	// if it has a shader, we have to set gltf_load to false and do a real load on the textures, otherwise shaders will not be applied to the textures
	// 	ResourceCompatLoader::set_default_gltf_load(false);
	// }
	for (auto &E : get_deps_map) {
		dep_info &info = E.value;
		// Never set a Shader, they're not used by the GLTF writer and cause errors
		if ((info.type == "Script" && info.dep.get_extension().to_lower() == "cs" && !GDRESettings::get_singleton()->has_loaded_dotnet_assembly()) || (info.type == "Shader" && !replace_shader_materials)) {
			auto texture = CompatFormatLoader::create_missing_external_resource(info.dep, info.type, info.uid, "");
			if (info.type == "Script") {
				Ref<FakeScript> script = texture;
				script->set_instance_recording_properties(true);
				script->set_load_type(ResourceCompatLoader::get_default_load_type());
			}
			set_cache_res(info, texture, false);
			continue;
		}
		// old visual
		// if (ver_major < GODOT_VERSION_MAJOR && info.real_type == "VisualShader" && replace_shader_materials) {
		// 	if (check_cached_res(info)) {
		// 		continue;
		// 	}
		// 	auto visual_shader = ResourceCompatLoader::custom_load(
		// 			info.remap, "",
		// 			ResourceInfo::FAKE_LOAD,
		// 			&err,
		// 			using_threaded_load(),
		// 			ResourceFormatLoader::CACHE_MODE_IGNORE); // not ignore deep, we want to reuse dependencies if they exist
		// 	if (err != OK || visual_shader.is_null()) {
		// 		if (ignore_missing_dependencies) {
		// 			missing_dependencies.push_back(info.dep);
		// 			WARN_PRINT(vformat("%s: Dependency %s -> %s failed to load.", source_path, info.dep, info.remap));
		// 			continue;
		// 		}
		// 		GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_MISSING_DEPENDENCIES, vformat("Dependency %s -> %s failed to load.", info.dep, info.remap));
		// 	}
		// 	bool got = false;
		// 	String code = visual_shader->get("code", &got);
		// 	ERR_CONTINUE_MSG(!got || code.is_empty(), vformat("Failed to get code from VisualShader %s", info.dep));
		// 	Ref<Shader> shader;
		// 	std::function<void(const String &)> make_new_shader = [&](const String &p_code) -> void {
		// 		shader = memnew(Shader());
		// 		shader->set_code(p_code);
		// 		set_cache_res(info, shader, false);
		// 	};
		// 	if (!TaskManager::get_singleton()->dispatch_to_main_thread(make_new_shader, code).has_value()) {
		// 		return ERR_SKIP;
		// 	}
		// 	continue;
		// }
		if (!FileAccess::exists(info.remap) && !FileAccess::exists(info.dep)) {
			if (ignore_missing_dependencies) {
				missing_dependencies.push_back(info.dep);
				WARN_PRINT(vformat("%s: Dependency %s -> %s does not exist.", source_path, info.dep, info.remap));
				continue;
			}
			GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_MISSING_DEPENDENCIES,
					vformat("Dependency %s -> %s does not exist.", info.dep, info.remap));
		} else if (info.uid != ResourceUID::INVALID_ID) {
			if (!info.uid_in_uid_cache) {
				ResourceUID::get_singleton()->add_id(info.uid, info.remap);
				loaded_dep_uids.push_back(info.uid);
			} else if (!info.uid_in_uid_cache_matches_dep) {
				if (info.uid_remap_path_exists) {
					WARN_PRINT(vformat("Dependency %s:%s is not mapped to the same path: %s (%s)", info.dep, info.remap, info.orig_remap, ResourceUID::get_singleton()->id_to_text(info.uid)));
					ResourceUID::get_singleton()->set_id(info.uid, info.remap);
				}
			}
			if (info.uid_remap_path_exists) {
				continue;
				// else fall through
			}
		}

		if (info.dep != info.remap) {
			String our_path = GDRESettings::get_singleton()->get_mapped_path(info.dep);
			if (our_path != info.remap) {
				WARN_PRINT(vformat("Dependency %s:%s is not mapped to the same path: %s", info.dep, info.remap, our_path));
				if (check_cached_res(info)) {
					continue;
				}
				auto result = TaskManager::get_singleton()->dispatch_to_main_thread((std::function<Ref<Resource>()>)[&]() -> Ref<Resource> {
					auto res = ResourceCompatLoader::custom_load(
							info.remap, "",
							ResourceCompatLoader::get_default_load_type(),
							&err,
							using_threaded_load(),
							ResourceFormatLoader::CACHE_MODE_IGNORE); // not ignore deep, we want to reuse dependencies if they exist
					set_cache_res(info, res, false);
					return res;
				});
				if (!result.has_value()) {
					return ERR_SKIP; // cancelled
				}
				auto texture = result.value();
				if (err || texture.is_null()) {
					if (ignore_missing_dependencies) {
						missing_dependencies.push_back(info.dep);
						WARN_PRINT(vformat("%s: Dependency %s:%s failed to load.", source_path, info.dep, info.remap));
						continue;
					}
					GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_MISSING_DEPENDENCIES,
							vformat("Dependency %s:%s failed to load.", info.dep, info.remap));
				}
			} else { // if mapped_path logic changes, we have to set this to true
				// no_threaded_load = true;
			}
		}
	}

	// export/import settings
	export_image_format = image_extensions.is_empty() ? "PNG" : gdre::get_most_popular_value(image_extensions);
	has_lossy_images = false;
	if (export_image_format == "WEBP") {
		// Only 3.4 and above supports lossless WebP
		if (ver_major > 3 || (ver_major == 3 && ver_minor >= 4)) {
			export_image_format = "Lossless WebP";
		} else {
			if (force_lossless_images) {
				export_image_format = "PNG";
			} else {
				export_image_format = "Lossy WebP";
				has_lossy_images = true;
			}
		}
		// TODO: add setting to force PNG?
	} else if (export_image_format == "JPEG") {
		if (force_lossless_images) {
			export_image_format = "PNG";
		} else {
			has_lossy_images = true;
		}
	} else {
		// the GLTF exporter doesn't support anything other than PNG, JPEG, and WEBP
		export_image_format = "PNG";
	}
	if (has_lossy_images && report.is_valid()) {
		report->set_loss_type(ImportInfo::STORED_LOSSY);
	}

	scene_loading_error_messages.append_array(_get_logged_error_messages());
	return OK;
}

namespace SceneExporterEnums {
enum LightBakeMode {
	LIGHT_BAKE_DISABLED,
	LIGHT_BAKE_STATIC,
	LIGHT_BAKE_STATIC_LIGHTMAPS,
	LIGHT_BAKE_DYNAMIC,
};

enum MeshPhysicsMode {
	MESH_PHYSICS_DISABLED,
	MESH_PHYSICS_MESH_AND_STATIC_COLLIDER,
	MESH_PHYSICS_RIGID_BODY_AND_MESH,
	MESH_PHYSICS_STATIC_COLLIDER_ONLY,
	MESH_PHYSICS_AREA_ONLY,
};

enum NavMeshMode {
	NAVMESH_DISABLED,
	NAVMESH_MESH_AND_NAVMESH,
	NAVMESH_NAVMESH_ONLY,
};

enum OccluderMode {
	OCCLUDER_DISABLED,
	OCCLUDER_MESH_AND_OCCLUDER,
	OCCLUDER_OCCLUDER_ONLY,
};

enum MeshOverride {
	MESH_OVERRIDE_DEFAULT,
	MESH_OVERRIDE_ENABLE,
	MESH_OVERRIDE_DISABLE,
};

enum BodyType {
	BODY_TYPE_STATIC,
	BODY_TYPE_DYNAMIC,
	BODY_TYPE_AREA
};

enum ShapeType {
	SHAPE_TYPE_DECOMPOSE_CONVEX,
	SHAPE_TYPE_SIMPLE_CONVEX,
	SHAPE_TYPE_TRIMESH,
	SHAPE_TYPE_BOX,
	SHAPE_TYPE_SPHERE,
	SHAPE_TYPE_CYLINDER,
	SHAPE_TYPE_CAPSULE,
	SHAPE_TYPE_AUTOMATIC,
};
} //namespace SceneExporterEnums

Dictionary get_default_node_options() {
	Dictionary dict;
	// INTERNAL_IMPORT_CATEGORY_NODE
	dict["node/node_type"] = "";
	dict["node/script"] = Variant();
	dict["import/skip_import"] = false;

	// INTERNAL_IMPORT_CATEGORY_MESH_3D_NODE
	dict["generate/physics"] = false;
	dict["generate/navmesh"] = SceneExporterEnums::NAVMESH_DISABLED;
	dict["physics/body_type"] = SceneExporterEnums::BODY_TYPE_STATIC;
	dict["physics/shape_type"] = SceneExporterEnums::SHAPE_TYPE_AUTOMATIC;
	dict["physics/physics_material_override"] = Variant();
	dict["physics/layer"] = 1;
	dict["physics/mask"] = 1;

	dict["mesh_instance/layers"] = 1;
	dict["mesh_instance/visibility_range_begin"] = 0.0f;
	dict["mesh_instance/visibility_range_begin_margin"] = 0.0f;
	dict["mesh_instance/visibility_range_end"] = 0.0f;
	dict["mesh_instance/visibility_range_end_margin"] = 0.0f;
	dict["mesh_instance/visibility_range_fade_mode"] = GeometryInstance3D::VISIBILITY_RANGE_FADE_DISABLED;
	dict["mesh_instance/cast_shadow"] = GeometryInstance3D::SHADOW_CASTING_SETTING_ON;

	// Decomposition
	Ref<MeshConvexDecompositionSettings> decomposition_default = Ref<MeshConvexDecompositionSettings>();
	decomposition_default.instantiate();
	dict["decomposition/advanced"] = false;
	dict["decomposition/precision"] = 5;
	dict["decomposition/max_concavity"] = decomposition_default->get_max_concavity();
	dict["decomposition/symmetry_planes_clipping_bias"] = decomposition_default->get_symmetry_planes_clipping_bias();
	dict["decomposition/revolution_axes_clipping_bias"] = decomposition_default->get_revolution_axes_clipping_bias();
	dict["decomposition/min_volume_per_convex_hull"] = decomposition_default->get_min_volume_per_convex_hull();
	dict["decomposition/resolution"] = decomposition_default->get_resolution();
	dict["decomposition/max_num_vertices_per_convex_hull"] = decomposition_default->get_max_num_vertices_per_convex_hull();
	dict["decomposition/plane_downsampling"] = decomposition_default->get_plane_downsampling();
	dict["decomposition/convexhull_downsampling"] = decomposition_default->get_convex_hull_downsampling();
	dict["decomposition/normalize_mesh"] = decomposition_default->get_normalize_mesh();
	dict["decomposition/mode"] = static_cast<int>(decomposition_default->get_mode());
	dict["decomposition/convexhull_approximation"] = decomposition_default->get_convex_hull_approximation();
	dict["decomposition/max_convex_hulls"] = decomposition_default->get_max_convex_hulls();
	dict["decomposition/project_hull_vertices"] = decomposition_default->get_project_hull_vertices();

	// Primitives: Box, Sphere, Cylinder, Capsule.
	dict["primitive/size"] = Vector3(2.0, 2.0, 2.0);
	dict["primitive/height"] = 1.0;
	dict["primitive/radius"] = 1.0;
	dict["primitive/position"] = Vector3();
	dict["primitive/rotation"] = Vector3();

	dict["generate/occluder"] = SceneExporterEnums::OCCLUDER_DISABLED;
	dict["occluder/simplification_distance"] = 0.1f;

	// animation node
	dict["optimizer/enabled"] = true;
	dict["optimizer/max_velocity_error"] = 0.01;
	dict["optimizer/max_angular_error"] = 0.01;
	dict["optimizer/max_precision_error"] = 3;
	dict["compression/enabled"] = false;
	dict["compression/page_size"] = 8;
	dict["import_tracks/position"] = 1;
	dict["import_tracks/rotation"] = 1;
	dict["import_tracks/scale"] = 1;

	// skeleton 3d node
	dict["rest_pose/load_pose"] = 0;
	dict["rest_pose/external_animation_library"] = Variant();
	dict["rest_pose/selected_animation"] = "";
	dict["rest_pose/selected_timestamp"] = 0.0f;

	return dict;
}

bool is_auto_generated_node(Node *p_node) {
	ERR_FAIL_NULL_V(p_node, false);
	RigidBody3D *parent_rigid_body = Object::cast_to<RigidBody3D>(p_node);
	if (parent_rigid_body) {
		for (auto &child : p_node->get_children()) {
			if (Object::cast_to<MeshInstance3D>(child)) {
				return true;
			}
		}
	}
	Node *parent = p_node->get_parent();
	Node *parent_parent = parent ? parent->get_parent() : nullptr;
	if (parent && p_node->get_owner() != parent->get_owner()) {
		parent_parent = nullptr;
	}
	bool parent_is_mesh_instance = parent ? !!Object::cast_to<MeshInstance3D>(parent) : false;
	bool parent_is_root_and_node3d = parent && parent_parent == nullptr && parent->get_class() == "Node3D";
	if (Object::cast_to<StaticBody3D>(p_node) && (!parent || parent_is_root_and_node3d || parent_is_mesh_instance || Object::cast_to<Area3D>(parent))) {
		return true;
	}
	if (!parent || parent_is_mesh_instance || parent_is_root_and_node3d) {
		if (Object::cast_to<NavigationRegion3D>(p_node)) {
			return true;
		}
		if (Object::cast_to<OccluderInstance3D>(p_node)) {
			return true;
		}
	}
	if (parent && Object::cast_to<CollisionShape3D>(p_node)) {
		if (Object::cast_to<Area3D>(parent)) {
			return true;
		}
		return is_auto_generated_node(parent);
	}
	return false;
}

Dictionary get_node_options(Node *p_node, Node *original_node = nullptr) {
	ERR_FAIL_NULL_V(p_node, Dictionary());
	Dictionary node_options_dict = Dictionary();

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node);
	AnimationPlayer *animation_player = Object::cast_to<AnimationPlayer>(p_node);
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_node);
	Ref<Script> script = p_node->get_script();
	if (!mesh_instance && !animation_player && !skeleton && !is_auto_generated_node(p_node)) {
		String type_name;
		if (script.is_valid()) {
			type_name = script->get_global_name();
		}
		if (type_name.is_empty()) {
			type_name = p_node->get_class();
		}
		if (!type_name.is_empty() && type_name != "Node3D") {
			node_options_dict["node/node_type"] = type_name;
		}
	}
	if (script.is_valid()) {
		node_options_dict["node/script"] = script;
	}
	if (mesh_instance) {
		// node_options_dict["import/skip_import"] = false;
		// only used internally
		//SHAPE_TYPE_SIMPLE_CONVEX = ConvexPolygonShape3D
		//SHAPE_TYPE_TRIMESH = ConcavePolygonShape3D
		//SHAPE_TYPE_BOX = BoxShape3D
		//SHAPE_TYPE_SPHERE = SphereShape3D
		//SHAPE_TYPE_CYLINDER = CylinderShape3D
		//SHAPE_TYPE_CAPSULE = CapsuleShape3D
		//SHAPE_TYPE_AUTOMATIC = AutomaticShape3D
		SceneExporterEnums::MeshPhysicsMode mesh_physics_mode = SceneExporterEnums::MESH_PHYSICS_DISABLED;
		SceneExporterEnums::BodyType body_type = SceneExporterEnums::BODY_TYPE_STATIC;
		SceneExporterEnums::OccluderMode occluder_mode = SceneExporterEnums::OCCLUDER_DISABLED;
		SceneExporterEnums::NavMeshMode navmesh_mode = SceneExporterEnums::NAVMESH_DISABLED;
		SceneExporterEnums::ShapeType shape_type = SceneExporterEnums::SHAPE_TYPE_AUTOMATIC;
		Ref<PhysicsMaterial> physics_material_override;
		uint32_t physics_layer_bits = 1;
		uint32_t physics_mask_bits = 1;
		PhysicsBody3D *physics_body_node = nullptr;
		RigidBody3D *parent_rigid_body = Object::cast_to<RigidBody3D>(p_node->get_parent());
		float occlusion_simplification_distance = 0.1f;
		if (parent_rigid_body) {
			physics_body_node = parent_rigid_body;
			physics_material_override = parent_rigid_body->get_physics_material_override();
			mesh_physics_mode = SceneExporterEnums::MESH_PHYSICS_RIGID_BODY_AND_MESH;
			body_type = SceneExporterEnums::BODY_TYPE_DYNAMIC;
		}
		for (auto &child : p_node->get_children()) {
			if (auto static_body = Object::cast_to<StaticBody3D>(child); static_body) {
				physics_body_node = static_body;
				physics_material_override = static_body->get_physics_material_override();
				body_type = SceneExporterEnums::BODY_TYPE_STATIC;
				mesh_physics_mode = SceneExporterEnums::MESH_PHYSICS_MESH_AND_STATIC_COLLIDER;
			}
			// navmesh
			if (auto navmesh = Object::cast_to<NavigationRegion3D>(child); navmesh) {
				navmesh_mode = SceneExporterEnums::NAVMESH_MESH_AND_NAVMESH;
			}
			// occluder
			if (auto occluder_instance = Object::cast_to<OccluderInstance3D>(child); occluder_instance) {
				occlusion_simplification_distance = occluder_instance->get_bake_simplification_distance();
				occluder_mode = SceneExporterEnums::OCCLUDER_MESH_AND_OCCLUDER;
			}
		}

		if (original_node) {
			if (auto area_3d = Object::cast_to<Area3D>(original_node); area_3d) {
				mesh_physics_mode = SceneExporterEnums::MESH_PHYSICS_AREA_ONLY;
				body_type = SceneExporterEnums::BODY_TYPE_AREA;
			} else {
				if (auto navigation_region = Object::cast_to<NavigationRegion3D>(original_node); navigation_region) {
					navmesh_mode = SceneExporterEnums::NAVMESH_NAVMESH_ONLY;
				} else if (auto occluder_instance = Object::cast_to<OccluderInstance3D>(original_node); occluder_instance) {
					occluder_mode = SceneExporterEnums::OCCLUDER_OCCLUDER_ONLY;
				}
				if (mesh_physics_mode == SceneExporterEnums::MESH_PHYSICS_MESH_AND_STATIC_COLLIDER) {
					mesh_physics_mode = SceneExporterEnums::MESH_PHYSICS_STATIC_COLLIDER_ONLY;
				}
			}
		}

		node_options_dict["generate/physics"] = mesh_physics_mode != SceneExporterEnums::MESH_PHYSICS_DISABLED;
		node_options_dict["generate/navmesh"] = navmesh_mode;
		Dictionary primtive_options_dict = Dictionary();

		if (physics_body_node) {
			physics_layer_bits = physics_body_node->get_collision_layer();
			physics_mask_bits = physics_body_node->get_collision_mask();
			TypedArray<Node> physics_nodes = physics_body_node->find_children("*", "CollisionShape3D", true);
			if (physics_nodes.size() > 1) {
				// easy, it's decomposing convex
				shape_type = SceneExporterEnums::SHAPE_TYPE_DECOMPOSE_CONVEX;
			} else if (physics_nodes.size() == 1) {
				CollisionShape3D *physics_node = Object::cast_to<CollisionShape3D>(physics_nodes[0]);
				if (physics_node) {
					auto shape = physics_node->get_shape();
					if (Ref<ConvexPolygonShape3D> convex_shape = shape; convex_shape.is_valid()) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_SIMPLE_CONVEX;
					} else if (Ref<ConcavePolygonShape3D> concave_shape = shape; concave_shape.is_valid()) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_TRIMESH;
					} else if (Ref<BoxShape3D> box_shape = shape; box_shape.is_valid()) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_BOX;
						primtive_options_dict["primitive/size"] = box_shape->get_size();
						primtive_options_dict["primitive/position"] = physics_node->get_position();
						primtive_options_dict["primitive/rotation"] = physics_node->get_rotation();
					} else if (Ref<SphereShape3D> sphere_shape = shape; sphere_shape.is_valid()) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_SPHERE;
						primtive_options_dict["primitive/radius"] = sphere_shape->get_radius();
						primtive_options_dict["primitive/position"] = physics_node->get_position();
						primtive_options_dict["primitive/rotation"] = physics_node->get_rotation();
					} else if (Ref<CylinderShape3D> cylinder_shape = shape; cylinder_shape.is_valid()) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_CYLINDER;
						primtive_options_dict["primitive/height"] = cylinder_shape->get_height();
						primtive_options_dict["primitive/radius"] = cylinder_shape->get_radius();
						primtive_options_dict["primitive/position"] = physics_node->get_position();
						primtive_options_dict["primitive/rotation"] = physics_node->get_rotation();
					} else if (Ref<CapsuleShape3D> capsule_shape = shape; capsule_shape.is_valid()) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_CAPSULE;
						primtive_options_dict["primitive/height"] = capsule_shape->get_height();
						primtive_options_dict["primitive/radius"] = capsule_shape->get_radius();
						primtive_options_dict["primitive/position"] = physics_node->get_position();
						primtive_options_dict["primitive/rotation"] = physics_node->get_rotation();
					} else {
						shape_type = SceneExporterEnums::SHAPE_TYPE_AUTOMATIC;
					}
				}
			}
			const auto auto_shape_type = body_type == SceneExporterEnums::BODY_TYPE_DYNAMIC ? SceneExporterEnums::SHAPE_TYPE_DECOMPOSE_CONVEX : SceneExporterEnums::SHAPE_TYPE_TRIMESH;
			if (shape_type == auto_shape_type) {
				shape_type = SceneExporterEnums::SHAPE_TYPE_AUTOMATIC;
			}

			node_options_dict["physics/body_type"] = body_type;
			node_options_dict["physics/shape_type"] = shape_type;
			if (physics_material_override.is_valid()) {
				node_options_dict["physics/physics_material_override"] = physics_material_override;
			} else {
				node_options_dict["physics/physics_material_override"] = Variant();
			}
			node_options_dict["physics/layer"] = physics_layer_bits;
			node_options_dict["physics/mask"] = physics_mask_bits;
			// TODO: Decomposition options in the case of shape_type == SHAPE_TYPE_DECOMPOSE_CONVEX;
			// as far as I can tell, it's nearly impossible to recover these settings from the DecomposeConvexShape3D node
		}

		node_options_dict["mesh_instance/layers"] = mesh_instance->get_layer_mask();
		node_options_dict["mesh_instance/visibility_range_begin"] = mesh_instance->get_visibility_range_begin();
		node_options_dict["mesh_instance/visibility_range_begin_margin"] = mesh_instance->get_visibility_range_begin_margin();
		node_options_dict["mesh_instance/visibility_range_end"] = mesh_instance->get_visibility_range_end();
		node_options_dict["mesh_instance/visibility_range_end_margin"] = mesh_instance->get_visibility_range_end_margin();
		node_options_dict["mesh_instance/visibility_range_fade_mode"] = mesh_instance->get_visibility_range_fade_mode();
		node_options_dict["mesh_instance/cast_shadow"] = mesh_instance->get_cast_shadows_setting();

		for (auto &E : primtive_options_dict) {
			node_options_dict[E.key] = E.value;
		}
		node_options_dict["generate/occluder"] = occluder_mode;
		if (occluder_mode != SceneExporterEnums::OCCLUDER_DISABLED) {
			node_options_dict["occluder/simplification_distance"] = occlusion_simplification_distance;
		}
	}
	// AnimationPlayer options modify the imported animations, they all have to do with optimizing and culling tracks,
	// so we want the default options.
	// Skeleton3D options modify the imported skeleton and it will be exported according to those modifications
	// e.g. a retargeted skeleton will be exported with the retargeted bones, so we want the default options so as to leave it as is.

	return node_options_dict;
}

NodePath get_node_path(Node *p_node) {
	const Node *n = p_node;

	Vector<StringName> path;

	while (n) {
		path.push_back(n->get_name());
		n = n->get_parent();
	}

	path.reverse();
	if (path.is_empty()) {
		return NodePath(".");
	}

	return NodePath(path, true);
}

// disabled in debug builds, so we have to copy-and-paste it here.
Ref<ArrayMesh> get_nav_array_debug_mesh(const Ref<NavigationMesh> navmesh) {
	Ref<ArrayMesh> debug_mesh;
	debug_mesh.instantiate();
	Vector<Vector3> vertices = navmesh->get_vertices();

	if (vertices.is_empty()) {
		return debug_mesh;
	}

	int polygon_count = navmesh->get_polygon_count();

	if (polygon_count < 1) {
		// no face, no play
		return debug_mesh;
	}

	// build geometry face surface
	Vector<Vector3> face_vertex_array;
	face_vertex_array.resize(polygon_count * 3);

	for (int i = 0; i < polygon_count; i++) {
		Vector<int> polygon = navmesh->get_polygon(i);

		face_vertex_array.push_back(vertices[polygon[0]]);
		face_vertex_array.push_back(vertices[polygon[1]]);
		face_vertex_array.push_back(vertices[polygon[2]]);
	}

	Array face_mesh_array;
	face_mesh_array.resize(Mesh::ARRAY_MAX);
	face_mesh_array[Mesh::ARRAY_VERTEX] = face_vertex_array;

	// if enabled add vertex colors to colorize each face individually
	static constexpr bool enabled_geometry_face_random_color = false;
	static const Color debug_navigation_geometry_face_color = Color(0.5, 1.0, 1.0, 0.4);
	if (enabled_geometry_face_random_color) {
		Color polygon_color = debug_navigation_geometry_face_color;

		Vector<Color> face_color_array;
		face_color_array.resize(polygon_count * 3);

		for (int i = 0; i < polygon_count; i++) {
			polygon_color = debug_navigation_geometry_face_color * (Color(Math::randf(), Math::randf(), Math::randf()));

			face_color_array.push_back(polygon_color);
			face_color_array.push_back(polygon_color);
			face_color_array.push_back(polygon_color);
		}
		face_mesh_array[Mesh::ARRAY_COLOR] = face_color_array;
	}

	debug_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, face_mesh_array);
	Ref<StandardMaterial3D> face_material;
	face_material = Ref<StandardMaterial3D>(memnew(StandardMaterial3D));
	face_material->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
	face_material->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
	face_material->set_albedo(debug_navigation_geometry_face_color);
	face_material->set_cull_mode(StandardMaterial3D::CULL_DISABLED);
	face_material->set_flag(StandardMaterial3D::FLAG_DISABLE_FOG, true);
	if (enabled_geometry_face_random_color) {
		face_material->set_flag(StandardMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
		face_material->set_flag(StandardMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	}
	debug_mesh->surface_set_material(0, face_material);

	return debug_mesh;
}

void GLBExporterInstance::recompute_animation_tracks_for_library(AnimationPlayer *p_player, const Ref<AnimationLibrary> &p_anim_lib, const LocalVector<StringName> &p_anim_names) {
	if (ver_major > 3 || p_anim_names.is_empty()) {
		return;
	}
	auto current_animation = p_player->get_current_animation();
	auto current_pos = current_animation.is_empty() ? 0 : p_player->get_current_animation_position();

	Ref<RegEx> mesh_surface_re = RegEx::create_from_string(":mesh:surface_(\\d+)");
	// Force re-compute animation tracks.
	for (auto &anim_name : p_anim_names) {
		Ref<Animation> anim = p_anim_lib->get_animation(anim_name);
		auto info = ResourceInfo::get_info_from_resource(anim);
		ERR_CONTINUE(!info.is_valid());
		constexpr const char *converted_paths_from_3_x = "converted_paths_from_3.x";
		if (info->extra.get(converted_paths_from_3_x, false)) {
			continue;
		}

		size_t num_tracks = anim->get_track_count();
		for (size_t i = 0; i < num_tracks; i++) {
			String str_path = String(anim->track_get_path(i));
			if (str_path.contains(":mesh:surface_")) {
				// Surface properties are 1-indexed in 3.x, but 0-indexed in 4.x.
				Ref<RegExMatch> match = mesh_surface_re->search(str_path);
				if (match.is_valid()) {
					int surface_index = match->get_string(1).to_int();
					surface_index--;
					str_path = mesh_surface_re->sub(str_path, ":mesh:surface_" + String::num_int64(surface_index));
				}
			}
			if (str_path.contains(":material/")) {
				str_path = str_path.replace(":material/", ":surface_material_override/");
			}
			if (str_path.contains(":shader_param/")) {
				str_path = str_path.replace(":shader_param/", ":shader_parameter/");
			}
			anim->track_set_path(i, str_path);
		}
		info->extra.set(converted_paths_from_3_x, true);
	}

	p_player->set_current_animation(*p_anim_names.begin());
	p_player->advance(0);
	p_player->set_current_animation(current_animation);
	if (!current_animation.is_empty()) {
		p_player->seek(current_pos);
	}
}

void GLBExporterInstance::convert_animation_tracks_to_v4_for_player(AnimationPlayer *p_player) {
	LocalVector<StringName> anim_lib_names;
	p_player->get_animation_library_list(&anim_lib_names);
	for (auto &anim_lib_name : anim_lib_names) {
		Ref<AnimationLibrary> anim_lib = p_player->get_animation_library(anim_lib_name);
		if (anim_lib.is_valid()) {
			LocalVector<StringName> anim_names;
			anim_lib->get_animation_list(&anim_names);
			recompute_animation_tracks_for_library(p_player, anim_lib, anim_names);
		}
	}
}

Node *GLBExporterInstance::_set_stuff_from_instanced_scene(Node *root) {
	bool current_thread_safe_for_nodes = is_current_thread_safe_for_nodes();
	set_current_thread_safe_for_nodes(true);
	root_type = root->get_class();
	root_name = root->get_name();

	TypedArray<Node> animation_player_nodes = root->find_children("*", "AnimationPlayer");
	TypedArray<Node> mesh_instances = root->find_children("*", "MeshInstance3D");
	HashSet<Node *> skinned_mesh_instances;
	HashSet<Node *> generated_mesh_instance_parents;
	HashMap<Ref<ShaderMaterial>, Ref<BaseMaterial3D>> shader_material_to_base_material_map;

	HashSet<Ref<Mesh>> meshes_in_mesh_instances;
	auto cache_material = [&](const Ref<ShaderMaterial> &shader_material, const Ref<BaseMaterial3D> &base_material, Pair<bool, bool> used_textures_and_is_instance) {
		// We don't want to cache it if it has instance uniforms that were actually used,
		// since they will need to be created per-instance
		if (base_material.is_valid() && !used_textures_and_is_instance.second) {
			shader_material_to_base_material_map[shader_material] = base_material;
		}
		// Keeping the shader material around until the end to avoid errors caused when a deferred call that uses it is executed after it's freed
		if (!loaded_deps.has(shader_material)) {
			loaded_deps.push_back(shader_material);
		}
	};

	auto process_mesh_instance = [&](MeshInstance3D *mesh_instance) {
		auto skin = mesh_instance->get_skin();
		if (skin.is_valid()) {
			has_skinned_meshes = true;
			skinned_mesh_instances.insert(mesh_instance);
		}
		auto mesh = mesh_instance->get_mesh();
		if (mesh.is_valid()) {
			meshes_in_mesh_instances.insert(mesh);
			String path = mesh->get_path(); // external and internal paths
			if (!path.is_empty()) {
				mesh_path_to_instance_map[path] = mesh_instance;
			}
		}
		if (replace_shader_materials && mesh.is_valid()) {
			if (Ref<ShaderMaterial> shader_mat_override = mesh_instance->get_material_override(); shader_mat_override.is_valid()) {
				auto [base_material, used_textures_and_is_instance] = ShaderMaterialConverter::convert_shader_material_to_base_material(shader_mat_override, mesh_instance);
				if (base_material.is_valid()) {
					mesh_instance->set_material_override(base_material);
				}
				cache_material(shader_mat_override, base_material, used_textures_and_is_instance);
			}
			if (Ref<ShaderMaterial> shader_mat_overlay = mesh_instance->get_material_overlay(); shader_mat_overlay.is_valid()) {
				auto [base_material, used_textures_and_is_instance] = ShaderMaterialConverter::convert_shader_material_to_base_material(shader_mat_overlay, mesh_instance);
				if (base_material.is_valid()) {
					mesh_instance->set_material_overlay(base_material);
				}
				cache_material(shader_mat_overlay, base_material, used_textures_and_is_instance);
			}
			for (int surface_i = 0; surface_i < mesh->get_surface_count(); surface_i++) {
				Ref<ShaderMaterial> shader_material;
				Ref<Material> active_surface_material = mesh_instance->get_active_material(surface_i);
				Ref<Material> surface_material = mesh->surface_get_material(surface_i);
				Ref<BaseMaterial3D> base_material;
				shader_material = active_surface_material;
				if (shader_material.is_valid()) {
					if (shader_material_to_base_material_map.has(shader_material)) {
						base_material = shader_material_to_base_material_map[shader_material];
					} else {
						Pair<Ref<BaseMaterial3D>, Pair<bool, bool>> base_material_pair = ShaderMaterialConverter::convert_shader_material_to_base_material(shader_material, mesh_instance);
						if (!base_material_pair.second.first) {
							base_material = surface_material;
							if (!base_material.is_valid()) {
								Ref<ShaderMaterial> surface_shader_material = surface_material;
								if (surface_shader_material.is_valid() && surface_shader_material != shader_material) {
									auto new_material_pair = ShaderMaterialConverter::convert_shader_material_to_base_material(surface_shader_material);
									if (new_material_pair.second.first) {
										shader_material = surface_shader_material;
										base_material_pair = new_material_pair;
									}
								}
							}
						}
						if (!base_material.is_valid()) {
							base_material = base_material_pair.first;
						}
						cache_material(shader_material, base_material, base_material_pair.second);
					}
					if (base_material.is_valid()) {
						mesh_instance->set_surface_override_material(surface_i, base_material);
					}
				}
			}
		}
	};
	other_error_messages.append_array(_get_logged_error_messages());
	for (auto &E : mesh_instances) {
		MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(E);
		ERR_CONTINUE(!mesh_instance);
		process_mesh_instance(mesh_instance);
	}
	other_error_messages.append_array(_get_logged_error_messages());
	// Needed for warning about scene with physics nodes being exported as multi-root
	{
		TypedArray<Node> physics_nodes = root->find_children("*", "CollisionObject3D");
		TypedArray<Node> physics_shapes = root->find_children("*", "CollisionShape3D");
		has_physics_nodes = physics_nodes.size() > 0 || physics_shapes.size() > 0;
	}

	TypedArray<Node> nodes = { root };
	nodes.append_array(root->get_children());
	int i = 0;
	auto generate_mesh_instance = [&](Node *node, const Ref<Mesh> &mesh, Node *original_node = nullptr) {
		auto mesh_instance = memnew(MeshInstance3D());
		mesh_instance->set_mesh(mesh);
		String name = mesh->get_name();
		String path = mesh->get_path();

		if (name.is_empty()) {
			name = demangle_name(path.get_file().get_basename());
			if (name.is_empty() || path.contains("::")) {
				if (original_node) {
					name = original_node->get_name();
				} else {
					name = ("Mesh_" + String::num_int64(++i));
				}
			}
			mesh->set_name(name);
		}
		if (!path.is_empty()) {
			mesh_path_to_instance_map[path] = mesh_instance;
		}
		mesh_instance->set_name(name);
		generated_mesh_instance_parents.insert(node);
		meshes_in_mesh_instances.insert(mesh);
		return mesh_instance;
	};
	std::function<bool(Node *)> process_node = [&](Node *node) -> bool {
		ScriptInstance *si = node->get_script_instance();
		List<PropertyInfo> properties;
		HashSet<MeshInstance3D *> mis_generated_from_scripts;
		// generate mesh instances from script properties
		if (si) {
			si->get_property_list(&properties);
			HashSet<Variant> other_values;
			HashSet<Ref<Skin>> skins;
			HashSet<NodePath> skeleton_paths;
			for (auto &prop : properties) {
				Variant value;
				if (si->get(prop.name, value)) {
					// check if it's a mesh instance
					Ref<Mesh> mesh = value;
					if (mesh.is_valid() && !meshes_in_mesh_instances.has(mesh)) {
						// create a new mesh instance
						auto mesh_instance = generate_mesh_instance(node, mesh);
						mis_generated_from_scripts.insert(mesh_instance);
					}
				}
			}
			if (!mis_generated_from_scripts.is_empty()) {
				std::function<void()> add_mesh_instances = [&]() {
					for (auto &mesh_instance : mis_generated_from_scripts) {
						node->add_child(mesh_instance);
						bool set_skin = false;
						bool set_skeleton = false;
						for (auto &prop : properties) {
							Variant value;
							if (si->get(prop.name, value) && !skins.has(value) && !skeleton_paths.has(value)) {
								Ref<Skin> skin = value;
								if (skin.is_valid() && !set_skin) {
									mesh_instance->set_skin(skin);
									set_skin = true;
									skins.insert(skin);
								} else if (!set_skeleton) {
									NodePath skeleton_path = value;
									if (!skeleton_path.is_empty()) {
										// we need to check to see if this is actually a skeleton
										Node *skeleton = node->get_node(skeleton_path);
										Skeleton3D *skeleton3d = skeleton ? Object::cast_to<Skeleton3D>(skeleton) : nullptr;
										if (skeleton3d) {
											NodePath actual_path = skeleton_path;
											if (!skeleton_path.is_absolute()) {
												actual_path = mesh_instance->get_path_to(skeleton3d);
											}
											mesh_instance->set_skeleton_path(actual_path);
											set_skeleton = true;
											skeleton_paths.insert(skeleton_path);
										}
									}
								}
							}
							if (set_skin && set_skeleton) {
								break;
							}
						}
						process_mesh_instance(mesh_instance);
						if (updating_import_info) {
							node_options[get_node_path(mesh_instance).operator String()] = { { "import/skip_import", true } };
						}
					}
				};
				if (!TaskManager::get_singleton()->dispatch_to_main_thread(add_mesh_instances).has_value()) {
					return false; // cancelled
				}
			}
		}
		Node *original_node = nullptr;
		std::function<void(Ref<ArrayMesh>)> replace_with_mi = [&](Ref<ArrayMesh> mesh) {
			auto mesh_instance = generate_mesh_instance(node, mesh, node);
			mesh_instance->set_name(node->get_name());
			mesh_instance->set_scene_file_path(node->get_scene_file_path());
			process_mesh_instance(mesh_instance);
			replaced_node_names.push_back(get_node_path(node).operator String() + ":" + node->get_class());
			auto node3d = Object::cast_to<Node3D>(node);
			auto transform = node3d ? node3d->get_transform() : Transform3D();
			auto script = node->get_script();
			original_node = node;
			String original_class = node->get_class();
			List<PropertyInfo> properties;
			node->get_property_list(&properties);
			//VisualInstance3D, CollisionObject3D, NavigationRegion3D
			//E.usage == PROPERTY_USAGE_CATEGORY && E.name == "BaseMaterial3D"
			const HashSet<StringName> original_classes = { "VisualInstance3D", "CollisionObject3D", "NavigationRegion3D" };
			bool hit_node_3d_category = false;
			for (auto &E : properties) {
				// setting any of the Node or Object properties will screw this up
				if ((E.usage & PROPERTY_USAGE_CATEGORY) && E.name == "Node3D") {
					hit_node_3d_category = true;
				}
				if (!hit_node_3d_category) {
					continue;
				}
				if ((E.usage & PROPERTY_USAGE_CATEGORY) && !mesh_instance->is_class(E.name)) {
					break;
				}
				if (E.usage & PROPERTY_USAGE_STORAGE) {
					mesh_instance->set(E.name, node->get(E.name));
				}
			}
			node->replace_by(mesh_instance);
			mesh_instance->set_transform(transform);
			mesh_instance->set_script(script);

			node = mesh_instance;
			if (original_node == root) {
				root = node;
				root_type = node->get_class();
				root_name = node->get_name();
			}
			auto parent = original_node->get_parent();
			Vector<CollisionShape3D *> shapes;
			if (!parent || !Object::cast_to<RigidBody3D>(parent)) {
				for (auto &E : mesh_instance->get_children()) {
					auto shape = Object::cast_to<CollisionShape3D>(E.operator Object *());
					if (shape) {
						shapes.push_back(shape);
					}
				}
			}
			if (!shapes.is_empty()) {
				StaticBody3D *static_body = memnew(StaticBody3D());
				static_body->set_name(mesh_instance->get_name().operator String() + "_StaticBody3D");
				mesh_instance->add_child(static_body);
				for (auto &shape : shapes) {
					shape->set_owner(nullptr);
					shape->reparent(static_body, false);
				}
			}
		};
		// replace NavMesh/Occluder/Area3D-only nodes with mesh instances
		// e.g. the original mesh will have been replaced by a NavMesh/Occluder/Area3D node by the importer, so we have to put it back.
		if (!is_auto_generated_node(node) && node != root) {
			Ref<ArrayMesh> replacement_mesh;
			if (auto navigation_region = Object::cast_to<NavigationRegion3D>(node); navigation_region) {
				if (Ref<NavigationMesh> navmesh = navigation_region->get_navigation_mesh(); navmesh.is_valid()) {
					replacement_mesh = get_nav_array_debug_mesh(navmesh);
				}
			} else if (auto occluder_instance = Object::cast_to<OccluderInstance3D>(node); occluder_instance) {
				if (Ref<Occluder3D> occluder = occluder_instance->get_occluder(); occluder.is_valid()) {
					replacement_mesh = occluder->get_debug_mesh();
				}
			} else if (auto area_3d = Object::cast_to<Area3D>(node); area_3d) {
				Vector<Ref<ArrayMesh>> meshes;
				for (auto &E : area_3d->get_children()) {
					if (auto collision_shape = Object::cast_to<CollisionShape3D>(E.operator Object *()); collision_shape && collision_shape->get_shape().is_valid()) {
						meshes.push_back(collision_shape->get_shape()->get_debug_mesh());
					}
				}
				if (meshes.size() > 1) {
					SurfaceTool surface_tool;
					for (size_t i = 0; i < meshes.size(); i++) {
						for (size_t j = 0; j < meshes[i]->get_surface_count(); j++) {
							if (i == 0 && j == 0) {
								surface_tool.create_from(meshes[i], j);
							} else {
								surface_tool.append_from(meshes[i], j, Transform3D());
							}
						}
					}
					replacement_mesh = surface_tool.commit();
				} else if (meshes.size() == 1) {
					replacement_mesh = meshes[0];
				}
			}
			if (replacement_mesh.is_valid()) {
				if (!TaskManager::get_singleton()->dispatch_to_main_thread(replace_with_mi, replacement_mesh).has_value()) {
					return false; // cancelled
				}
			}
		}
		if (updating_import_info && node != root) {
			node_options[get_node_path(node).operator String()] = get_node_options(node, original_node);
		}

		if (original_node) {
			if (SceneTree::get_singleton()) {
				original_node->queue_free();
			} else {
				memdelete(original_node);
			}
		}

		for (auto &E : node->get_children()) {
			auto child = Object::cast_to<Node>(E.operator Object *());
			if (!mis_generated_from_scripts.has(Object::cast_to<MeshInstance3D>(child))) {
				if (!process_node(child)) {
					return false; // cancelled
				}
			}
		}
		return true;
	};
	if (!process_node(root)) {
		return nullptr; // cancelled
	}
	if (replaced_node_names.size() > 0) {
		auto thingy = String(", ").join(replaced_node_names);
		print_line(vformat("%s: replaced nodes with mesh instances: %s", scene_name, thingy));
		if (GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/debug_copies", false)) {
			auto new_dest = "res://.tscn_manip/" + report->get_import_info()->get_export_dest().trim_prefix("res://.assets/").trim_prefix("res://").get_basename() + ".tscn";
			auto new_dest_path = output_dir.path_join(new_dest.replace_first("res://", ""));
			Ref<PackedScene> scene = memnew(PackedScene);
			scene->pack(root);
			ResourceCompatLoader::save_custom(scene, new_dest_path, GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR);
		}
	}

	Vector<int64_t> fps_values;

	if (ver_major <= 3) {
		std::function<void()> convert_all_players_to_v4 = [&]() {
			for (int32_t node_i = 0; node_i < animation_player_nodes.size(); node_i++) {
				AnimationPlayer *player = Object::cast_to<AnimationPlayer>(animation_player_nodes[node_i]);
				ERR_CONTINUE(!player);
				convert_animation_tracks_to_v4_for_player(player);
			}
		};
		if (!TaskManager::get_singleton()->dispatch_to_main_thread(convert_all_players_to_v4).has_value()) {
			return nullptr; // cancelled
		}
	}

	for (int32_t node_i = 0; node_i < animation_player_nodes.size(); node_i++) {
		AnimationPlayer *player = Object::cast_to<AnimationPlayer>(animation_player_nodes[node_i]);
		ERR_CONTINUE(!player);
		bool any_compressed = false;
		// Force re-compute animation tracks.
		Vector<Ref<AnimationLibrary>> anim_libs;
		LocalVector<StringName> anim_lib_names;
		player->get_animation_library_list(&anim_lib_names);
		for (auto &lib_name : anim_lib_names) {
			Ref<AnimationLibrary> lib = player->get_animation_library(lib_name);
			if (lib.is_valid()) {
				anim_libs.push_back(lib);
			}
		}
		int64_t max_fps = -1;
		for (auto &anim_lib : anim_libs) {
			LocalVector<StringName> anim_names;
			anim_lib->get_animation_list(&anim_names);
			for (auto &anim_name : anim_names) {
				double shortest_frame_duration = 1000.0;

				Ref<Animation> anim = anim_lib->get_animation(anim_name);
				auto path = get_resource_path(anim);
				String name = anim_name;
				if (name == "RESET") {
					has_reset_track = true;
				}
				size_t num_tracks = anim->get_track_count();
				// check for a transform that affects a non-skeleton node
				for (size_t i = 0; i < num_tracks; i++) {
					if (anim->track_get_type(i) == Animation::TYPE_SCALE_3D || anim->track_get_type(i) == Animation::TYPE_ROTATION_3D || anim->track_get_type(i) == Animation::TYPE_POSITION_3D) {
						if (anim->track_get_path(i).get_subname_count() == 0) {
							has_non_skeleton_transforms = true;
						}
					}
					if (!anim->track_is_imported(i)) {
						external_animation_nodepaths.insert(anim->track_get_path(i));
					} else if (updating_import_info) {
						any_compressed = any_compressed || anim->track_is_compressed(i);
						auto key_count = anim->track_get_key_count(i);
						double last_key_frame = key_count > 0 ? anim->track_get_key_time(i, 0) : 0;
						// Ignore the very last frame, it's usually an inserted fast frame to ensure the animation loops.
						auto max_key_count = key_count - 1;
						// check the key frames for the shortest frame duration
						for (size_t j = 1; j < max_key_count; j++) {
							auto key_frame = anim->track_get_key_time(i, j);
							double duration = key_frame - last_key_frame;
							if (duration > 0.0) {
								shortest_frame_duration = MIN(shortest_frame_duration, key_frame - last_key_frame);
							}
							last_key_frame = key_frame;
						}
					}
				}
				int64_t fps = 0;
				if (shortest_frame_duration > 0.0) {
					fps = round(1.0 / shortest_frame_duration);
				}
				if (fps > 0) {
					fps_values.push_back(fps);
					max_fps = MAX(max_fps, fps);
				}
				if (updating_import_info) {
					int i = 1;
					while (animation_options.has(name)) {
						// append _001, _002, etc.
						name = vformat("%s_%03d", anim_name, i);
						i++;
					}
					animation_options[name] = Dictionary();
					auto &anim_options = animation_options[name];
					anim_options["settings/loop_mode"] = (int)anim->get_loop_mode();
					if (!(path.is_empty() || path.get_file().contains("::"))) {
						anim_options["save_to_file/enabled"] = true;
						set_path_options(anim_options, path);
						anim_options["save_to_file/keep_custom_tracks"] = true;
						// TODO: slices??
					} else {
						anim_options["save_to_file/enabled"] = false;
						set_path_options(anim_options, "");
						anim_options["save_to_file/keep_custom_tracks"] = false;
					}
					anim_options["slices/amount"] = 0;
				}
			}
		}
		if (updating_import_info) {
			auto path = get_node_path(player).operator String();
			node_options[path] = get_node_options(player);
			if (any_compressed) {
				node_options[path]["compression/enabled"] = true;
				// The rest of the options modify the animation tracks.
				// These will already be reflected in the saved resource, so we don't set them.
			}
		}
		if (max_fps > 0) {
			baked_fps = MIN(max_fps, 120);
		}
	}
	set_current_thread_safe_for_nodes(current_thread_safe_for_nodes);
	return root;
}

bool GLBExporterInstance::_is_logger_silencing_errors() const {
	if (supports_multithread()) {
		return GDRELogger::is_thread_local_silencing_errors();
	}
	return GDRELogger::is_silencing_errors();
}

void GLBExporterInstance::_silence_errors(bool p_silence) {
	if (supports_multithread()) {
		GDRELogger::set_thread_local_silent_errors(p_silence);
	} else {
		GDRELogger::set_silent_errors(p_silence);
	}
}

#define GDRE_SCN_EXP_CHECK_CANCEL()                                                                                \
	{                                                                                                              \
		if (unlikely(TaskManager::get_singleton()->is_current_task_canceled() || canceled)) {                      \
			Error cancel_err = TaskManager::get_singleton()->is_current_task_timed_out() ? ERR_TIMEOUT : ERR_SKIP; \
			return cancel_err;                                                                                     \
		}                                                                                                          \
	}

// TODO: handle Godot version <= 4.2 image naming scheme?
String GLBExporterInstance::demangle_name(const String &name) {
	return name.trim_prefix(scene_name + "_");
}

bool check_children_for_non_physics_nodes(Node *p_scene_node);

bool check_children_for_non_physics_nodes(Node *p_scene_node) {
	bool has_non_physics_children = false;
	auto children = p_scene_node->get_children();
	for (auto child : children) {
		Node *child_node = Object::cast_to<Node>(child);
		ERR_CONTINUE(child_node == nullptr);
		if (!Object::cast_to<CollisionShape3D>(child_node) && !Object::cast_to<CollisionObject3D>(child_node)) {
			has_non_physics_children = true;
			break;
		} else {
			if (check_children_for_non_physics_nodes(child_node)) {
				has_non_physics_children = true;
				break;
			}
		}
	}
	return has_non_physics_children;
}

void GLTFDocumentExtensionPhysicsRemover::convert_scene_node(Ref<GLTFState> p_state, Ref<GLTFNode> p_gltf_node, Node *p_scene_node) {
	if (Object::cast_to<CollisionShape3D>(p_scene_node) || Object::cast_to<CollisionObject3D>(p_scene_node)) {
		if (!check_children_for_non_physics_nodes(p_scene_node)) {
			p_gltf_node->set_parent(-2);
		}
	}
}

Ref<Image> GLBExporterInstance::_parse_image_bytes_into_image(const Ref<GLTFState> &p_state, const Vector<uint8_t> &p_bytes, const String &p_mime_type, int p_index, String &r_file_extension) {
	Ref<Image> r_image;
	r_image.instantiate();
	// Check if any GLTFDocumentExtensions want to import this data as an image.
	for (auto &ext : GLTFDocument::get_all_gltf_document_extensions()) {
		ERR_CONTINUE(ext.is_null());
		Error err = ext->parse_image_data(p_state, p_bytes, p_mime_type, r_image);
		ERR_CONTINUE_MSG(err != OK, "glTF: Encountered error " + itos(err) + " when parsing image " + itos(p_index) + " in file " + p_state->get_filename() + ". Continuing.");
		if (!r_image->is_empty()) {
			r_file_extension = ext->get_image_file_extension();
			return r_image;
		}
	}
	// If no extension wanted to import this data as an image, try to load a PNG or JPEG.
	// First we honor the mime types if they were defined.
	if (p_mime_type == "image/png") { // Load buffer as PNG.
		r_image->load_png_from_buffer(p_bytes);
		r_file_extension = ".png";
	} else if (p_mime_type == "image/jpeg") { // Loader buffer as JPEG.
		r_image->load_jpg_from_buffer(p_bytes);
		r_file_extension = ".jpg";
	}
	// If we didn't pass the above tests, we attempt loading as PNG and then JPEG directly.
	// This covers URIs with base64-encoded data with application/* type but
	// no optional mimeType property, or bufferViews with a bogus mimeType
	// (e.g. `image/jpeg` but the data is actually PNG).
	// That's not *exactly* what the spec mandates but this lets us be
	// lenient with bogus glb files which do exist in production.
	if (r_image->is_empty()) { // Try PNG first.
		r_image->load_png_from_buffer(p_bytes);
	}
	if (r_image->is_empty()) { // And then JPEG.
		r_image->load_jpg_from_buffer(p_bytes);
	}
	// If it still can't be loaded, give up and insert an empty image as placeholder.
	if (r_image->is_empty()) {
		ERR_PRINT(vformat("glTF: Couldn't load image index '%d' with its given mimetype: %s.", p_index, p_mime_type));
	}
	return r_image;
}

String GLBExporterInstance::get_gltf_image_hash(const Ref<GLTFState> &p_state, const Dictionary &dict, int p_index) {
	String image_hash;
	String mime_type;
	if (dict.has("mimeType")) { // Should be "image/png", "image/jpeg", or something handled by an extension.
		mime_type = dict["mimeType"];
	}

	String resource_uri;
	// We only need the image hash if it's embedded in the glTF file, so if it's not a bufferView, we can return early.
	Vector<uint8_t> data;
	if (dict.has("bufferView")) {
		// Handles the third bullet point from the spec (bufferView).
		ERR_FAIL_COND_V_MSG(mime_type.is_empty(), "", vformat("glTF: Image index '%d' specifies 'bufferView' but no 'mimeType', which is invalid.", p_index));
		const GLTFBufferViewIndex bvi = dict["bufferView"];
		auto buffer_views = p_state->get_buffer_views();
		ERR_FAIL_INDEX_V(bvi, buffer_views.size(), "");
		Ref<GLTFBufferView> bv = buffer_views[bvi];
		const GLTFBufferIndex bi = bv->get_buffer();
		auto buffers = p_state->get_buffers();
		ERR_FAIL_INDEX_V(bi, buffers.size(), "");
		ERR_FAIL_COND_V(bv->get_byte_offset() + bv->get_byte_length() > buffers[bi].size(), "");
		const PackedByteArray &buffer = buffers[bi];
		data = buffer.slice(bv->get_byte_offset(), bv->get_byte_offset() + bv->get_byte_length());
	}
	if (data.is_empty()) {
		return image_hash;
	}
	String ext;
	Ref<Image> img = _parse_image_bytes_into_image(p_state, data, mime_type, p_index, ext);
	if (img.is_valid()) {
		auto img_data = img->get_data();
		unsigned char md5_hash[16];
		CryptoCore::md5(img_data.ptr(), img_data.size(), md5_hash);
		image_hash = String::hex_encode_buffer(md5_hash, 16);
	}
	return image_hash;
}

Error GLBExporterInstance::_export_instanced_scene(Node *root, const String &p_dest_path) {
	{
		GDRE_SCN_EXP_CHECK_CANCEL();
		String game_name = GDRESettings::get_singleton()->get_game_name();
		String copyright_string = vformat(COPYRIGHT_STRING_FORMAT, game_name.is_empty() ? p_dest_path.get_file().get_basename() : game_name);
		List<String> deps;
		Ref<GLTFDocument> doc;
		doc.instantiate();
		Ref<GLTFState> state;
		state.instantiate();
		state->set_scene_name(scene_name);
		state->set_copyright(copyright_string);
		doc->set_image_format(export_image_format);
		doc->set_lossy_quality(1.0f);

		GDRE_SCN_EXP_CHECK_CANCEL();
		if (force_export_multi_root || (has_non_skeleton_transforms && has_skinned_meshes)) {
			// WARN_PRINT("Skinned meshes have non-skeleton transforms, exporting as non-single-root.");
			doc->set_root_node_mode(GLTFDocument::RootNodeMode::ROOT_NODE_MODE_MULTI_ROOT);
			if (has_physics_nodes) {
				WARN_PRINT("Skinned meshes have physics nodes, but still exporting as non-single-root.");
			}
		}
		if (force_require_KHR_node_visibility) {
			doc->set_visibility_mode(GLTFDocument::VisibilityMode::VISIBILITY_MODE_INCLUDE_REQUIRED);
		} else {
			doc->set_visibility_mode(GLTFDocument::VisibilityMode::VISIBILITY_MODE_INCLUDE_OPTIONAL);
		}
		int32_t flags = 0;
		auto exts = doc->get_supported_gltf_extensions();
		flags |= 16; // EditorSceneFormatImporter::IMPORT_USE_NAMED_SKIN_BINDS;
		bool was_silenced = _is_logger_silencing_errors();
		_silence_errors(true);
		other_error_messages.append_array(_get_logged_error_messages());
		auto errors_before_append = _get_error_count();
		err = doc->append_from_scene(root, state, flags);
		_silence_errors(was_silenced);
		auto errors_after_append = _get_error_count();
		if (err) {
			gltf_serialization_error_messages.append_array(_get_logged_error_messages());
			GDRE_SCN_EXP_FAIL_V_MSG(ERR_COMPILATION_FAILED, "Failed to append scene " + source_path + " to glTF document");
		}
		GDRE_SCN_EXP_CHECK_CANCEL();

		_silence_errors(true);
		auto errors_before_serialize = _get_error_count();
		err = doc->_serialize(state);
		auto errors_after_serialize = _get_error_count();
		_silence_errors(was_silenced);
		GDRE_SCN_EXP_CHECK_CANCEL();

		if (errors_after_serialize > errors_before_serialize || errors_after_append > errors_before_append) {
			gltf_serialization_error_messages.append_array(_get_logged_error_messages());
		}
		if (err) {
			GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_CANT_WRITE, "Failed to serialize glTF document");
		}

		if (GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/debug_copies", false)) {
			// save a gltf copy for debugging
			Dictionary gltf_asset = state->get_json().get("asset", Dictionary());
			gltf_asset["generator"] = "GDRE Tools";
			state->get_json()["asset"] = gltf_asset;
			auto rel_path = p_dest_path.begins_with(output_dir) ? p_dest_path.trim_prefix(output_dir).simplify_path().trim_prefix("/") : p_dest_path.get_file();
			auto gltf_path = output_dir.path_join(".untouched_gltf_copy").path_join(rel_path.trim_prefix(".assets/").get_basename() + ".gltf");
			gdre::ensure_dir(gltf_path.get_base_dir());
			Vector<String> buffer_paths;
			_serialize_file(state, gltf_path, buffer_paths, !use_double_precision);
		}
		GDRE_SCN_EXP_CHECK_CANCEL();
		auto check_unique = [&](String &name, HashSet<String> &image_map) {
			if (name.is_empty()) {
				return;
			}
			if (!image_map.has(name)) {
				image_map.insert(name);
			} else {
				name = String(name) + vformat("_%03d", image_map.size() - 1);
			}
		};

		// rename objects in the state so that they will be imported correctly, and gather import params info
		{
			auto json = state->get_json();
			auto materials = state->get_materials();
			auto images = state->get_images();
			Array json_images = json.has("images") ? (Array)json["images"] : Array();
			HashSet<String> image_map;
			static const HashMap<String, Vector<BaseMaterial3D::TextureParam>> generated_tex_suffixes = {
				{ "emission", { BaseMaterial3D::TEXTURE_EMISSION } },
				{ "normal", { BaseMaterial3D::TEXTURE_NORMAL } },
				// These are imported into the same texture, and the materials use that same texture for each of these params.
				{ "orm", { BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, BaseMaterial3D::TEXTURE_ROUGHNESS, BaseMaterial3D::TEXTURE_METALLIC } },
				{ "albedo", { BaseMaterial3D::TEXTURE_ALBEDO } }
			};
			HashMap<String, String> image_name_to_path;
			for (auto E : image_deps_needed) {
				image_name_to_path[E.get_file().get_basename()] = E;
			}

			for (int i = 0; i < json_images.size(); i++) {
				Dictionary image_dict = json_images[i];
				Ref<Texture2D> image = images[i];
				auto path = get_path_res(image);
				String name = image->get_name();
				if (path.is_empty() && !name.is_empty()) {
					if (image_name_to_path.has(name)) {
						path = image_name_to_path[name];
					} else if (name[name.length() - 1] <= '9' && name[name.length() - 1] >= '0') {
						name = name.substr(0, name.length() - 1);
						if (image_name_to_path.has(name) && !image_map.has(name)) {
							path = image_name_to_path[name];
						}
					}
				}
				if (path.is_empty() && !name.is_empty()) {
					auto parts = name.rsplit("_", false, 1);
					String material_name = parts.size() > 0 ? parts[0] : String();
					String suffix;
					Vector<BaseMaterial3D::TextureParam> params;
					if (parts.size() > 1 && generated_tex_suffixes.has(parts[1])) {
						suffix = parts[1];
						params = generated_tex_suffixes[suffix];
					}
					if (!suffix.is_empty()) {
						for (auto E : materials) {
							Ref<Material> material = E;
							if (!material.is_valid()) {
								continue;
							}

							String mat_name = material->get_name();
							if (material_name != mat_name && material_name != mat_name.replace(".", "_")) {
								continue;
							}
							Ref<BaseMaterial3D> base_material = material;
							if (base_material.is_valid()) {
								for (auto param : params) {
									auto tex = base_material->get_texture(param);
									if (tex.is_valid()) {
										path = tex->get_path();
										break;
									}
								}
							}
						}
					}
				}
				bool is_internal = path.is_empty() || path.get_file().contains("::");
				if (is_internal) {
					name = get_name_res(image_dict, image, i);
				} else {
					name = path.get_file().get_basename();
					external_deps_updated.insert(path);
					if (updating_import_info && is_batch_export) {
						image_path_to_data_hash[path] = get_gltf_image_hash(state, image_dict, i);
					}
				}
				check_unique(name, image_map);

				if (!name.is_empty()) {
					image_dict["name"] = demangle_name(name);
				}
				Ref<ResourceInfo> info = ResourceInfo::get_info_from_resource(image);
				Dictionary extras = info.is_valid() ? info->extra : Dictionary();
				had_images = true;
				if (extras.has("data_format")) {
					image_formats.push_back(CompressedTexture2D::DataFormat(int(extras["data_format"])));
				}
			}
			if (json_images.size() > 0) {
				json["images"] = json_images;
			}

			if (json.has("meshes")) {
				auto default_light_map_size = Vector2i(0, 0);
				HashSet<String> mesh_names;
				Vector<Pair<Ref<ArrayMesh>, MeshInstance3D *>> mesh_to_instance;
				Vector<bool> mesh_is_shadow;
				auto gltf_meshes = state->get_meshes();
				Array json_meshes = json.has("meshes") ? (Array)json["meshes"] : Array();
				HashSet<Ref<ImporterMesh>> shadow_meshes;
				for (int i = 0; i < gltf_meshes.size(); i++) {
					Ref<GLTFMesh> gltf_mesh = gltf_meshes[i];
					auto mesh = gltf_mesh->get_mesh();
					if (mesh.is_valid()) {
						auto shadow_mesh = mesh->get_shadow_mesh();
						if (shadow_mesh.is_valid()) {
							shadow_meshes.insert(shadow_mesh);
						}
					}
				}
				for (int i = 0; i < gltf_meshes.size(); i++) {
					Ref<GLTFMesh> gltf_mesh = gltf_meshes[i];
					auto imesh = gltf_mesh->get_mesh();
					auto original_name = gltf_mesh->get_original_name();
					Dictionary mesh_dict = json_meshes[i];
					ObjExporter::MeshInfo mesh_info;
					if (imesh.is_null()) {
						continue;
					}
					String path = get_path_res(imesh);
					String name = original_name;
					if (name.is_empty()) {
						bool is_internal = path.is_empty() || path.get_file().contains("::");
						if (is_internal) {
							name = get_name_res(mesh_dict, imesh, i);
						} else {
							name = path.get_file().get_basename();
						}
					}
					check_unique(name, mesh_names);
					if (!name.is_empty()) {
						mesh_dict["name"] = demangle_name(name);
						if (original_name.is_empty()) {
							gltf_mesh->set_original_name(name);
						}
					}
					if (!updating_import_info || shadow_meshes.has(imesh) || (path.is_empty() && name.is_empty())) {
						// mesh that won't be imported, skip
						continue;
					}
					// Set the mesh info so that we can use it to rewrite the import params
					mesh_info.path = path;
					mesh_info.name = name;
					mesh_info.has_shadow_meshes = imesh->get_shadow_mesh().is_valid();
					mesh_info.has_lightmap_uv2 = imesh->get_lightmap_size_hint() != default_light_map_size;
					mesh_info.bake_mode = mesh_info.has_lightmap_uv2 ? 2 : 1;
					if (mesh_path_to_instance_map.has(path)) {
						Ref<Mesh> instance_mesh = mesh_path_to_instance_map[path]->get_mesh();
						Ref<ArrayMesh> arr_mesh = instance_mesh;
						mesh_info.has_shadow_meshes = arr_mesh.is_valid() ? arr_mesh->get_shadow_mesh().is_valid() : mesh_info.has_shadow_meshes;

						auto gi_mode = mesh_path_to_instance_map[path]->get_gi_mode();
						if (gi_mode == GeometryInstance3D::GI_MODE_DISABLED) {
							mesh_info.bake_mode = 0; // DISABLED
						} else if (gi_mode == GeometryInstance3D::GI_MODE_DYNAMIC) {
							mesh_info.bake_mode = 3; // DYNAMIC
						} else if (gi_mode == GeometryInstance3D::GI_MODE_STATIC) {
							if (mesh_info.has_lightmap_uv2) {
								mesh_info.bake_mode = 2; // STATIC_LIGHTMAPS
							} else {
								mesh_info.bake_mode = 1; // STATIC
							}
						}
					}
					auto surface_count = imesh->get_surface_count();
					for (int surf_idx = 0; surf_idx < surface_count; surf_idx++) {
						auto format = imesh->get_surface_format(surf_idx);
						mesh_info.has_tangents = mesh_info.has_tangents || ((format & Mesh::ARRAY_FORMAT_TANGENT) != 0);
						mesh_info.has_lods = mesh_info.has_lods || imesh->get_surface_lod_count(surf_idx) > 0;
						mesh_info.compression_enabled = mesh_info.compression_enabled || ((format & Mesh::ARRAY_FLAG_COMPRESS_ATTRIBUTES) != 0);
						// TODO: add lightmap_uv2_texel_size
						// r_mesh_info.lightmap_uv2_texel_size = p_mesh->surface_get_lightmap_uv2_texel_size(surf_idx);
					}
					id_to_mesh_info.push_back(mesh_info);
				}
				json["meshes"] = json_meshes;
			}

			if (json.has("materials")) {
				HashSet<String> material_names;
				Array json_materials = json["materials"];
				for (int i = 0; i < materials.size(); i++) {
					Dictionary material_dict = json_materials[i];
					Ref<Material> material = materials[i];
					auto path = get_path_res(material);
					String name = material->get_name();
					bool is_internal = path.is_empty() || path.get_file().contains("::");
					if (name.is_empty()) {
						if (is_internal) {
							name = get_name_res(material_dict, material, i);
						} else {
							name = path.get_file().get_basename();
						}
					}
					check_unique(name, material_names);
					// the name in the options import is the name in the gltf file, unlike for meshes
					if (!name.is_empty()) {
						material_dict["name"] = name;
					}
					id_to_material_path.push_back({ name, path });
				}
			}
			Dictionary gltf_asset = json["asset"];
#if DEBUG_ENABLED
			// less file churn when testing
			gltf_asset["generator"] = "GDRE Tools";
#else
			gltf_asset["generator"] = "GDRE Tools v" + GDRESettings::get_gdre_version();
#endif

			json["asset"] = gltf_asset;
		}
		GDRE_SCN_EXP_CHECK_CANCEL();
		if (p_dest_path.get_extension() == "glb" && GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/debug_copies", false)) {
			// save a gltf copy for debugging
			auto rel_path = p_dest_path.begins_with(output_dir) ? p_dest_path.trim_prefix(output_dir).simplify_path().trim_prefix("/") : p_dest_path.get_file();
			if (iinfo.is_valid()) {
				String ext = iinfo->get_source_file().get_extension().to_lower();
				// make sure it doesn't already end with two extensions
				if (rel_path.get_extension().get_extension().is_empty()) {
					rel_path = rel_path.get_basename() + "." + ext + ".gltf";
				}
			}
			auto gltf_path = output_dir.path_join(".gltf_copy").path_join(rel_path.trim_prefix(".assets/").get_basename() + ".gltf");
			gdre::ensure_dir(gltf_path.get_base_dir());
			Vector<String> buffer_paths;
			_serialize_file(state, gltf_path, buffer_paths, !use_double_precision);
		}
		GDRE_SCN_EXP_CHECK_CANCEL();
		Vector<String> buffer_paths;
		err = _serialize_file(state, p_dest_path, buffer_paths, !use_double_precision);
		if (report.is_valid() && buffer_paths.size() > 0) {
			Dictionary extra_info = report->get_extra_info();
			extra_info["external_buffer_paths"] = buffer_paths;
			report->set_extra_info(extra_info);
		}
	}
	GDRE_SCN_EXP_FAIL_COND_V_MSG(err, ERR_FILE_CANT_WRITE, "Failed to write glTF document to " + p_dest_path);
	return OK;
}

Error GLBExporterInstance::_check_model_can_load(const String &p_dest_path) {
	tinygltf::Model model;
	String error_string;
	Error load_err = load_model(p_dest_path, model, error_string);
	if (load_err != OK) {
		gltf_serialization_error_messages.append(error_string);
		return ERR_FILE_CORRUPT;
	}
	return OK;
}

Dictionary GLBExporterInstance::_get_default_subresource_options() {
	Dictionary dict = {
		{ "save_to_file/enabled", false },
		{ "save_to_file/path", "" },
		{ "save_to_file/fallback_path", "" },
		{ "generate/shadow_meshes", 0 },
		{ "generate/lightmap_uv", 0 },
		{ "generate/lods", 0 },
		{ "lods/normal_merge_angle", 20.0f },
		{ "use_external/enabled", false },
		{ "use_external/path", "" },
		{ "use_external/fallback_path", "" },
		{ "settings/loop_mode", 0 },
		{ "save_to_file/keep_custom_tracks", false },
		{ "slices/amount", 0 },
	};
	if (!after_4_3) {
		dict["lods/normal_split_angle"] = 25.0f;
	}
	if (!after_4_4) {
		dict["lods/normal_merge_angle"] = 60.0f;
	}
	for (int i = 0; i < 256; i++) {
		dict["slice_" + itos(i + 1) + "/name"] = "";
		dict["slice_" + itos(i + 1) + "/start_frame"] = 0;
		dict["slice_" + itos(i + 1) + "/end_frame"] = 0;
		dict["slice_" + itos(i + 1) + "/loop_mode"] = 0;
		dict["slice_" + itos(i + 1) + "/save_to_file/enabled"] = false;
		dict["slice_" + itos(i + 1) + "/save_to_file/path"] = "";
		dict["slice_" + itos(i + 1) + "/save_to_file/fallback_path"] = "";
		dict["slice_" + itos(i + 1) + "/save_to_file/keep_custom_tracks"] = false;
	}
	return dict;
	// switch (p_category) {
	// 	case SceneExporterEnums::INTERNAL_IMPORT_CATEGORY_MESH: {
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "save_to_file/enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), false));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "save_to_file/path", PROPERTY_HINT_SAVE_FILE, "*.res,*.tres"), ""));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "save_to_file/fallback_path", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), ""));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "generate/shadow_meshes", PROPERTY_HINT_ENUM, "Default,Enable,Disable"), 0));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "generate/lightmap_uv", PROPERTY_HINT_ENUM, "Default,Enable,Disable"), 0));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "generate/lods", PROPERTY_HINT_ENUM, "Default,Enable,Disable"), 0));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::FLOAT, "lods/normal_merge_angle", PROPERTY_HINT_RANGE, "0,180,1,degrees"), 20.0f));
	// 	} break;
	// 	case SceneExporterEnums::INTERNAL_IMPORT_CATEGORY_MATERIAL: {
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "use_external/enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), false));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "use_external/path", PROPERTY_HINT_FILE, "*.material,*.res,*.tres"), ""));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "use_external/fallback_path", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), ""));
	// 	} break;
	// 	case SceneExporterEnums::INTERNAL_IMPORT_CATEGORY_ANIMATION: {
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "settings/loop_mode", PROPERTY_HINT_ENUM, "None,Linear,Pingpong"), 0));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "save_to_file/enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), false));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "save_to_file/path", PROPERTY_HINT_SAVE_FILE, "*.res,*.anim,*.tres"), ""));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "save_to_file/fallback_path", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), ""));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "save_to_file/keep_custom_tracks"), ""));
	// 		r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "slices/amount", PROPERTY_HINT_RANGE, "0,256,1", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), 0));

	// 		for (int i = 0; i < 256; i++) {
	// 			r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "slice_" + itos(i + 1) + "/name"), ""));
	// 			r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "slice_" + itos(i + 1) + "/start_frame"), 0));
	// 			r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "slice_" + itos(i + 1) + "/end_frame"), 0));
	// 			r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "slice_" + itos(i + 1) + "/loop_mode", PROPERTY_HINT_ENUM, "None,Linear,Pingpong"), 0));
	// 			r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "slice_" + itos(i + 1) + "/save_to_file/enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), false));
	// 			r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "slice_" + itos(i + 1) + "/save_to_file/path", PROPERTY_HINT_SAVE_FILE, "*.res,*.anim,*.tres"), ""));
	// 			r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "slice_" + itos(i + 1) + "/save_to_file/fallback_path", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), ""));
	// 			r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "slice_" + itos(i + 1) + "/save_to_file/keep_custom_tracks"), false));
	// 		}
	// 	} break;
	// }
}

void GLBExporterInstance::_update_import_params(const String &p_dest_path) {
	ObjExporter::MeshInfo global_mesh_info = _get_mesh_options_for_import_params();

	int image_handling_val = GLTFState::HANDLE_BINARY_EXTRACT_TEXTURES;
	if (had_images) {
		if (image_deps_needed.size() > 0) {
			image_handling_val = GLTFState::HANDLE_BINARY_EXTRACT_TEXTURES;
		} else {
			auto most_common_format = gdre::get_most_popular_value(image_formats);
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
	iinfo->set_param("meshes/light_baking", global_mesh_info.bake_mode);
	iinfo->set_param("meshes/lightmap_texel_size", global_mesh_info.lightmap_uv2_texel_size);
	iinfo->set_param("meshes/force_disable_compression", !global_mesh_info.compression_enabled);
	iinfo->set_param("skins/use_named_skins", true);
	iinfo->set_param("animation/import", true);
	iinfo->set_param("animation/fps", baked_fps);
	iinfo->set_param("animation/trimming", p_dest_path.get_extension().to_lower() == "fbx");
	iinfo->set_param("animation/remove_immutable_tracks", true);
	iinfo->set_param("animation/import_rest_as_RESET", has_reset_track);
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
		iinfo->set_param("gltf/naming_version", after_4_4 ? 2 : 1);
	}

	iinfo->set_param("gltf/embedded_image_handling", image_handling_val);

	Dictionary default_subresource_options = _get_default_subresource_options();
	auto check_subresource_dict = [&](Dictionary &p_dict) {
		bool differs = false;
		for (auto &E : p_dict) {
			if (!default_subresource_options.has(E.key)) {
				ERR_CONTINUE(!default_subresource_options.has(E.key));
			}
			if (E.value != default_subresource_options.get(E.key, Variant())) {
				differs = true;
				break;
			}
		}
		// the importer puts all the default options in if one of them differs; if none do, we don't have to include it
		return differs;
	};
	if (animation_options.size() > 0) {
		Dictionary animations_dict = _subresources_dict.get("animations", Dictionary());
		for (auto &E : animation_options) {
			if (check_subresource_dict(E.value)) {
				animations_dict[E.key] = E.value;
			}
			String path = get_path_options(E.value);
			if (!(path.is_empty() || path.get_file().contains("::"))) {
				external_deps_updated.insert(path);
				animation_deps_updated.insert(path);
			}
		}
		if (!animations_dict.is_empty()) {
			_subresources_dict["animations"] = animations_dict;
		}
	}
	auto get_default_mesh_opt = [](bool global_opt, bool local_opt) {
		if (global_opt == local_opt) {
			return 0;
		}
		if (local_opt) {
			return 1;
		}
		return 2;
	};
	if (id_to_mesh_info.size() > 0) {
		Dictionary mesh_Dict = _subresources_dict.get("meshes", Dictionary());
		for (auto &E : id_to_mesh_info) {
			auto name = E.name;
			auto path = E.path;
			if (name.is_empty() || mesh_Dict.has(name)) {
				continue;
			}
			// "save_to_file/enabled": true,
			// "save_to_file/path": "res://models/Enemies/cultist-shoot-anim.res",
			Dictionary subres;
			if (path.is_empty() || path.get_file().contains("::")) {
				subres["save_to_file/enabled"] = false;
				set_path_options(subres, "");
			} else {
				subres["save_to_file/enabled"] = true;
				set_path_options(subres, path);
				external_deps_updated.insert(path);
			}
			subres["generate/shadow_meshes"] = get_default_mesh_opt(global_mesh_info.has_shadow_meshes, E.has_shadow_meshes);
			subres["generate/lightmap_uv"] = get_default_mesh_opt(global_mesh_info.bake_mode == 2, E.has_lightmap_uv2);
			subres["generate/lods"] = get_default_mesh_opt(global_mesh_info.has_lods, E.has_lods);
			// TODO: get these somehow??
			if (!after_4_3) {
				subres["lods/normal_split_angle"] = default_subresource_options.get("lods/normal_split_angle", 25.0f);
			}
			subres["lods/normal_merge_angle"] = default_subresource_options.get("lods/normal_merge_angle", 60.0f);
			// Doesn't look like this ever made it in?
			// if (!after_4_3) {
			// 	subres["lods/raycast_normals"] = false;
			// }
			if (check_subresource_dict(subres)) {
				mesh_Dict[name] = subres;
			}
		}
		if (!mesh_Dict.is_empty()) {
			_subresources_dict["meshes"] = mesh_Dict;
		}
	}
	if (id_to_material_path.size() > 0) {
		Dictionary mat_Dict = _subresources_dict.get("materials", Dictionary());
		for (auto &E : id_to_material_path) {
			auto name = E.first;
			auto path = E.second;
			if (name.is_empty() || mat_Dict.has(name)) {
				continue;
			}
			Dictionary subres;
			if (path.is_empty() || path.get_file().contains("::")) {
				subres["use_external/enabled"] = false;
				set_path_options(subres, "", "use_external");
			} else {
				subres["use_external/enabled"] = true;
				set_path_options(subres, path, "use_external");
				external_deps_updated.insert(path);
			}
			if (check_subresource_dict(subres)) {
				mat_Dict[name] = subres;
			}
		}
		if (!mat_Dict.is_empty()) {
			_subresources_dict["materials"] = mat_Dict;
		}
	}
	if (node_options.size() > 0) {
		const Dictionary default_node_options = get_default_node_options();
		if (!_subresources_dict.has("nodes")) {
			_subresources_dict["nodes"] = Dictionary();
		}
		Dictionary node_Dict = _subresources_dict["nodes"];
		for (auto &E : node_options) {
			// If we're not removing physics bodies, we don't want the editor to re-generate them when re-importing.
			if (!remove_physics_bodies && E.value.get("generate/physics", false)) {
				SceneExporterEnums::BodyType body_type = (SceneExporterEnums::BodyType)E.value.get("physics/body_type", (int)SceneExporterEnums::BODY_TYPE_STATIC);
				if (body_type != SceneExporterEnums::BODY_TYPE_AREA) {
					E.value["generate/physics"] = false;
				}
			}
			if (E.value.has("physics/physics_material_override")) {
				Ref<Resource> physics_material_override = E.value["physics/physics_material_override"];
				if (physics_material_override.is_valid() && !physics_material_override->is_built_in()) {
					external_deps_updated.insert(physics_material_override->get_path());
				}
			}
			for (auto &key : E.value.keys()) {
				if (default_node_options.has(key) && E.value.get(key, Variant()) == default_node_options.get(key, Variant())) {
					E.value.erase(key);
				}
			}
			if (E.value.is_empty()) {
				continue;
			}
			String path_key = "PATH:" + E.key.trim_prefix("/" + root_name).trim_prefix("/");
			node_Dict[path_key] = E.value;
		}
		if (node_Dict.is_empty()) {
			_subresources_dict.erase("nodes");
		}
	}

	iinfo->set_param("_subresources", _subresources_dict);
	Dictionary extra_info = report->get_extra_info();
	if (!image_path_to_data_hash.is_empty()) {
		extra_info["image_path_to_data_hash"] = image_path_to_data_hash;
	}
	report->set_extra_info(extra_info);
}

void GLBExporterInstance::_unload_deps() {
	loaded_deps.clear();

	// remove the UIDs that we added that didn't exist before
	for (uint64_t id : loaded_dep_uids) {
		ResourceUID::get_singleton()->remove_id(id);
	}
	loaded_dep_uids.clear();
}

Error SceneExporter::export_file_to_non_glb(const String &p_src_path, const String &p_dest_path, Ref<ImportInfo> iinfo) {
	String dest_ext = p_dest_path.get_extension().to_lower();
	if (dest_ext == "escn" || dest_ext == "tscn") {
		return ResourceCompatLoader::to_text(p_src_path, p_dest_path);
	} else if (dest_ext == "obj") {
		ObjExporter::MeshInfo mesh_info;
		return export_file_to_obj(p_dest_path, p_src_path, iinfo);
	}
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "You called the wrong function you idiot.");
}

Node *GLBExporterInstance::_instantiate_scene(Ref<PackedScene> scene) {
	other_error_messages.append_array(_get_logged_error_messages());
	// Instantiation of older scenes will spam warnings about deprecated features (this doesn't affect the error count or retrieving the logged error messages)
#ifndef DEBUG_ENABLED
	if (ver_major <= 3) {
		_silence_errors(true);
	}
#endif
	auto result = TaskManager::get_singleton()->dispatch_to_main_thread((std::function<Node *()>)[&scene]() -> Node * {
		return scene->instantiate();
	});
	if (!result.has_value()) {
		return nullptr; // cancelled
	}
	Node *root = result.value();
#ifndef DEBUG_ENABLED
	if (ver_major <= 3) {
		_silence_errors(false);
	}
#endif
	// this isn't an explcit error by itself, but it's context in case we experience further errors during the export
	scene_instantiation_error_messages.append_array(_get_logged_error_messages());
	if (root == nullptr) {
		err = ERR_CANT_ACQUIRE_RESOURCE;
		ERR_PRINT(add_errors_to_report(ERR_CANT_ACQUIRE_RESOURCE, "Failed to instantiate scene " + source_path));
		_get_logged_error_messages();
	}
	return root;
}

Error GLBExporterInstance::_load_scene_and_deps(Ref<Resource> &r_scene) {
	MeshInstance3D::upgrading_skeleton_compat = true;
	err = _load_deps();
	if (err != OK) {
		return err;
	}
	return _load_scene(r_scene);
}

Error GLBExporterInstance::_load_scene(Ref<Resource> &r_scene) {
	auto mode_type = ResourceCompatLoader::get_default_load_type();
	// loading older scenes will spam warnings about deprecated features
#ifndef DEBUG_ENABLED
	if (ver_major <= 3) {
		_silence_errors(true);
	}
#endif
	std::optional<Ref<Resource>> result;
	// For some reason, scenes with meshes fail to load without the load done by ResourceLoader::load, possibly due to notification shenanigans.
	if (ResourceCompatLoader::is_globally_available()) {
		result = TaskManager::get_singleton()->dispatch_to_main_thread((std::function<Ref<Resource>()>)[&]() -> Ref<Resource> {
			return ResourceLoader::load(source_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &err);
		});
	} else {
		result = TaskManager::get_singleton()->dispatch_to_main_thread((std::function<Ref<Resource>()>)[&]() -> Ref<Resource> {
			return ResourceCompatLoader::custom_load(source_path, "", mode_type, &err, using_threaded_load(), ResourceFormatLoader::CACHE_MODE_REUSE);
		});
	}
	if (!result.has_value()) {
		return ERR_SKIP;
	}
	r_scene = result.value();
#ifndef DEBUG_ENABLED
	if (ver_major <= 3) {
		_silence_errors(false);
	}
#endif
	scene_loading_error_messages.append_array(_get_logged_error_messages());
	if (err || !r_scene.is_valid()) {
		r_scene = nullptr;
		_unload_deps();
		GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_CANT_READ, "Failed to load scene " + source_path);
	}
	return OK;
}

bool GLBExporterInstance::supports_multithread() const {
	return !Thread::is_main_thread();
}

void GLBExporterInstance::cancel() {
	canceled = true;
}

Error GLBExporterInstance::_get_return_error() {
	bool set_all_externals = !updating_import_info;
	if (updating_import_info) {
		set_all_externals = true;
		for (auto &E : need_to_be_updated) {
			if (!external_deps_updated.has(E)) {
				set_all_externals = false;
				break;
			}
		}
	}
	// GLTFDocument has issues with custom animations and throws errors;
	// if we've set all the external resources (including custom animations),
	// then this isn't an error.
	bool removed_all_errors = true;
	static constexpr const char *const ANIMATION_ERROR_PATTERN = R"((?:glTF: Cannot export empty property\. No property was specified in the NodePath: |glTF: Cannot get node(?: index)? for animated track using path: )(.*))";
	Ref<RegEx> animation_error_re = RegEx::create_from_string(ANIMATION_ERROR_PATTERN);

	static const Vector<String> errors_to_ignore = {
		"A node was animated, but it wasn't found in the GLTFState", // animation track not found because it's not exported
		"Parameter \"p_target_object\" is null.", // object property animated by track not found because it's not exportable by the exporter
		"Image width specified (0 pixels) must be greater than 0 pixels", // texture export failure caused by zero-sized source images
		"Image width must be greater than 0", // texture export failure caused by zero-sized source images
		"Cannot generate mipmaps with width or height equal to 0", // texture export failure caused by zero-sized source images
		"Condition \"p_texture->get_image().is_null()\" is true. Returning: -1", // texture export failure caused by zero-sized source images
	};
	String last_error_message;
	if (gltf_serialization_error_messages.size() > 0) {
		Vector<int64_t> error_messages_to_remove;
		bool removed_last_error = false;
		for (int64_t i = 0; i < gltf_serialization_error_messages.size(); i++) {
			auto message = gltf_serialization_error_messages[i].strip_edges();

			if ((message.begins_with("at:") || message.begins_with("GDScript backtrace"))) {
				if (removed_last_error) {
					error_messages_to_remove.push_back(i);
				}
				continue;
			}
			removed_last_error = false;

			// If the exact same error message is emitted twice in a row, we can ignore the second one
			if (message == last_error_message) {
				error_messages_to_remove.push_back(i);
				removed_last_error = true;
				continue;
			}
			last_error_message = message;
			if (message.contains("but the track in the Godot AnimationPlayer is using a different interpolation.")) {
				// lots of spurious warnings from this one
				error_messages_to_remove.push_back(i);
				removed_last_error = true;
				continue;
			}

			if (message.begins_with("WARNING:")) {
				// don't count it, but don't remove it either
				continue;
			}
			if (message.contains("glTF:") && (!updating_import_info || animation_deps_updated.size() == animation_deps_needed.size())) {
				if (auto match = animation_error_re->search(message); match.is_valid()) {
					NodePath path = match->get_string(1).strip_edges();
					if (!updating_import_info || (!path.is_empty() && external_animation_nodepaths.has(path))) {
						error_messages_to_remove.push_back(i);
						removed_last_error = true;
						continue;
					}
				}
				// The previous error message is always emitted right after this one (and this one doesn't contain a path), so we just ignore it.
				if (message.contains("A node was animated, but it wasn't found in the GLTFState")) {
					error_messages_to_remove.push_back(i);
					removed_last_error = true;
					continue;
				}
			}

			for (auto &e : errors_to_ignore) {
				if (message.contains(e)) {
					error_messages_to_remove.push_back(i);
					removed_last_error = true;
					break;
				}
			}
			if (removed_last_error) {
				continue;
			}

			// Otherwise, we haven't removed all the errors
			removed_all_errors = false;
		}
		if (removed_all_errors) {
			gltf_serialization_error_messages.clear();
		} else {
			error_messages_to_remove.sort();
			for (int64_t i = error_messages_to_remove.size() - 1; i >= 0; i--) {
				gltf_serialization_error_messages.remove_at(error_messages_to_remove[i]);
			}
		}
	}
	bool had_gltf_serialization_errors = gltf_serialization_error_messages.size() > 0;

	if (!set_all_externals) {
		import_param_error_messages.append("Dependencies that were not set:");
		for (auto &E : need_to_be_updated) {
			if (external_deps_updated.has(E)) {
				continue;
			}
			import_param_error_messages.append("\t" + E);
		}
	}

	if (had_gltf_serialization_errors) {
		String _ = add_errors_to_report(ERR_BUG, "");
	} else if ((!set_all_externals || missing_dependencies.size() > 0) && err == OK) {
		String _ = add_errors_to_report(ERR_PRINTER_ON_FIRE, "Failed to set all external dependencies in GLTF export and/or import info. This scene may not be imported correctly upon re-import.");
	} else if (err == OK) {
		String _ = add_errors_to_report(OK, "");
	} else {
		GDRE_SCN_EXP_FAIL_COND_V_MSG(err, err, "");
	}

	if (project_recovery && (had_gltf_serialization_errors || !set_all_externals)) {
		err = ERR_PRINTER_ON_FIRE;
	}

	return err;
}

Error SceneExporter::export_file(const String &p_dest_path, const String &p_src_path) {
	auto report = export_file_with_options(p_dest_path, p_src_path, {});
	return report.is_valid() ? report->get_error() : ERR_BUG;
}

Error SceneExporter::export_file_to_obj(const String &p_dest_path, const String &p_src_path, Ref<ImportInfo> iinfo) {
	Error err;
	Ref<PackedScene> scene;
	int ver_major = iinfo.is_valid() ? iinfo->get_ver_major() : get_ver_major(p_src_path);
	if (ver_major < MINIMUM_GODOT_VER_SUPPORTED) {
		return ERR_UNAVAILABLE;
	}
	// For some reason, scenes with meshes fail to load without the load done by ResourceLoader::load, possibly due to notification shenanigans.
	std::optional<Ref<PackedScene>> result;
	if (ResourceCompatLoader::is_globally_available()) {
		result = TaskManager::get_singleton()->dispatch_to_main_thread((std::function<Ref<PackedScene>()>)[&]() -> Ref<PackedScene> {
			return ResourceLoader::load(p_src_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_REUSE, &err);
		});
	} else {
		result = TaskManager::get_singleton()->dispatch_to_main_thread((std::function<Ref<PackedScene>()>)[&]() -> Ref<PackedScene> {
			return ResourceCompatLoader::custom_load(p_src_path, "PackedScene", ResourceCompatLoader::get_default_load_type(), &err, true, ResourceFormatLoader::CACHE_MODE_REUSE);
		});
	}
	if (!result.has_value()) {
		return ERR_SKIP; // cancelled
	}
	ERR_FAIL_COND_V_MSG(err, ERR_FILE_CANT_READ, "Failed to load scene " + p_src_path);
	return export_scene_to_obj(scene, p_dest_path, iinfo, ver_major);
}

Vector<Ref<Mesh>> get_meshes(const Ref<PackedScene> &scene, int ver_major, bool exclude_shadow_meshes = true) {
	Vector<Ref<Mesh>> meshes;
	Vector<Ref<Mesh>> meshes_to_remove;

	auto resources = _find_resources(scene, true, ver_major);
	for (auto &E : resources) {
		Ref<Mesh> mesh = E;
		if (mesh.is_valid()) {
			meshes.push_back(mesh);
			if (exclude_shadow_meshes) {
				Ref<ArrayMesh> array_mesh = mesh;
				if (array_mesh.is_valid()) {
					auto shadow_mesh = array_mesh->get_shadow_mesh();
					if (shadow_mesh.is_valid()) {
						meshes_to_remove.push_back(shadow_mesh);
					}
				}
			}
		}
	}
	for (auto &mesh : meshes_to_remove) {
		meshes.erase(mesh);
	}
	return meshes;
}

size_t get_total_surface_count(const Vector<Ref<Mesh>> &meshes) {
	size_t total_surface_count = 0;
	for (auto &mesh : meshes) {
		total_surface_count += mesh->get_surface_count();
	}
	return total_surface_count;
}

Error SceneExporter::export_scene_to_obj(const Ref<PackedScene> &scene, const String &p_dest_path, Ref<ImportInfo> iinfo, int ver_major) {
	return export_meshes_to_obj(get_meshes(scene, ver_major, true), p_dest_path, iinfo);
}

Error SceneExporter::export_meshes_to_obj(const Vector<Ref<Mesh>> &meshes, const String &p_dest_path, Ref<ImportInfo> iinfo) {
	ObjExporter::MeshInfo r_mesh_info;
	Error err = ObjExporter::_write_meshes_to_obj(meshes, p_dest_path, p_dest_path.get_base_dir(), r_mesh_info);
	if (err != OK) {
		return err;
	}
	if (iinfo.is_valid()) {
		ObjExporter::rewrite_import_params(iinfo, r_mesh_info);
	}
	return OK;
}

GLBExporterInstance::GLBExporterInstance(String p_output_dir, Dictionary curr_options, bool p_project_recovery) {
	project_recovery = p_project_recovery;
	output_dir = p_output_dir;
	set_options(curr_options);
}

void GLBExporterInstance::set_options(const Dictionary &curr_options) {
	Dictionary options = curr_options;
	if (!options.has("Exporter/Scene/GLTF/force_lossless_images")) {
		options["Exporter/Scene/GLTF/force_lossless_images"] = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/force_lossless_images", false);
	}
	if (!options.has("Exporter/Scene/GLTF/use_double_precision")) {
		options["Exporter/Scene/GLTF/use_double_precision"] = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/use_double_precision", false);
	}
	if (!options.has("Exporter/Scene/GLTF/force_export_multi_root")) {
		options["Exporter/Scene/GLTF/force_export_multi_root"] = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/force_export_multi_root", false);
	}
	if (!options.has("Exporter/Scene/GLTF/replace_shader_materials")) {
		options["Exporter/Scene/GLTF/replace_shader_materials"] = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/replace_shader_materials", false);
	}
	if (!options.has("Exporter/Scene/GLTF/force_require_KHR_node_visibility")) {
		options["Exporter/Scene/GLTF/force_require_KHR_node_visibility"] = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/force_require_KHR_node_visibility", false);
	}
	if (!options.has("Exporter/Scene/GLTF/ignore_missing_dependencies")) {
		options["Exporter/Scene/GLTF/ignore_missing_dependencies"] = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/ignore_missing_dependencies", false);
	}
	if (!options.has("Exporter/Scene/GLTF/remove_physics_bodies")) {
		options["Exporter/Scene/GLTF/remove_physics_bodies"] = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/remove_physics_bodies", false);
	}
	replace_shader_materials = options.get("Exporter/Scene/GLTF/replace_shader_materials", false);
	force_lossless_images = options.get("Exporter/Scene/GLTF/force_lossless_images", false);
	force_export_multi_root = options.get("Exporter/Scene/GLTF/force_export_multi_root", false);
	force_require_KHR_node_visibility = options.get("Exporter/Scene/GLTF/force_require_KHR_node_visibility", false);
	use_double_precision = options.get("Exporter/Scene/GLTF/use_double_precision", false);
	ignore_missing_dependencies = options.get("Exporter/Scene/GLTF/ignore_missing_dependencies", false);
	remove_physics_bodies = options.get("Exporter/Scene/GLTF/remove_physics_bodies", false);
}

constexpr bool _check_unsupported(int ver_major, bool is_text_output) {
	return ver_major < SceneExporter::MINIMUM_GODOT_VER_SUPPORTED && !is_text_output;
}

inline void _set_unsupported(Ref<ExportReport> report, int ver_major, bool is_obj_output) {
	report->set_error(ERR_UNAVAILABLE);
	report->set_unsupported_format_type(vformat("v%d.x scene", ver_major));
	report->set_message(vformat("Scene export for engine version %d with %s output is not currently supported\nTry saving to .tscn instead.", ver_major, is_obj_output ? "obj" : "GLTF/GLB"));
}

Error _check_cancelled() {
	if (TaskManager::get_singleton()->is_current_task_canceled()) {
		if (TaskManager::get_singleton()->is_current_task_timed_out()) {
			return ERR_TIMEOUT;
		}
		return ERR_SKIP;
	}
	return OK;
}

struct BatchExportToken : public TaskRunnerStruct {
	static std::atomic<int64_t> in_progress;
	GLBExporterInstance instance;
	Ref<ExportReport> report;
	Ref<PackedScene> _scene;
	Node *root = nullptr;
	String p_src_path;
	String p_dest_path;
	String original_export_dest;
	String output_dir;
	bool preload_done = false;
	bool finished = false;
	bool export_done = false;
	bool no_rename = false;
	int ver_major = 0;
	Error err = OK;
	size_t scene_size = 0;
	int64_t export_start_time = 0;
	int64_t export_end_time = 0;
	size_t surface_count = 0;

	BatchExportToken(const String &p_output_dir, const Ref<ImportInfo> &p_iinfo, Dictionary p_options = {}, bool p_is_batch_export = false) :
			instance(p_output_dir, p_options, true) {
		ERR_FAIL_COND_MSG(!p_iinfo.is_valid(), "Import info is invalid");
		report = memnew(ExportReport(p_iinfo, SceneExporter::EXPORTER_NAME));
		original_export_dest = p_iinfo->get_export_dest();
		instance.set_batch_export(p_is_batch_export);
		String new_path = original_export_dest;
		String ext = new_path.get_extension().to_lower();
		bool to_text = ext == "escn" || ext == "tscn";
		bool to_obj = ext == "obj";
		bool non_gltf = ext != "glb" && ext != "gltf";
		// Non-original path, save it under .assets, which won't be picked up for import by the godot editor
		if (!to_text && !to_obj && non_gltf) {
			new_path = new_path.replace("res://", "res://.assets/").get_basename() + ".glb";
		}
		ver_major = p_iinfo->get_ver_major();
		scene_size = FileAccess::get_size(p_iinfo->get_path());
		output_dir = p_output_dir;
		p_src_path = p_iinfo->get_path();
		set_export_dest(new_path);
	}

	void set_export_dest(const String &p_export_dest) {
		report->get_import_info()->set_export_dest(p_export_dest);
		p_dest_path = output_dir.path_join(p_export_dest.replace("res://", ""));
	}
	String get_export_dest() const {
		return report->get_import_info()->get_export_dest();
	}

	bool is_text_output() const {
		String ext = get_export_dest().get_extension().to_lower();
		return ext == "escn" || ext == "tscn";
	}

	bool is_obj_output() const {
		return get_export_dest().get_extension().to_lower() == "obj";
	}

	bool is_glb_output_with_non_gltf_ext() const {
		String ext = original_export_dest.get_extension().to_lower();
		bool to_text = ext == "escn" || ext == "tscn";
		bool to_obj = ext == "obj";

		return !to_text && !to_obj && ext != "glb" && ext != "gltf";
	}

	void append_original_ext_to_export_dest() {
		String original_ext = original_export_dest.get_extension().to_lower();
		set_export_dest(get_export_dest().get_basename() + "." + original_ext + ".glb");
	}
	String get_original_export_dest() const {
		return original_export_dest;
	}

	void move_output_to_dot_assets() {
		String new_export_dest = get_export_dest();
		if (!FileAccess::exists(p_dest_path)) {
			return;
		}
		report->set_saved_path(p_dest_path);
		if (new_export_dest.begins_with("res://.assets/")) {
			// already in .assets
			return;
		}
		new_export_dest = new_export_dest.replace_first("res://", "res://.assets/");
		report->get_import_info()->set_export_dest(new_export_dest);
		auto new_dest = output_dir.path_join(new_export_dest.trim_prefix("res://"));
		auto new_dest_base_dir = new_dest.get_base_dir();
		gdre::ensure_dir(new_dest_base_dir);
		auto da = DirAccess::create_for_path(new_dest_base_dir);
		if (da.is_valid() && da->rename(p_dest_path, new_dest) == OK) {
			report->get_import_info()->set_export_dest(new_export_dest);
			report->set_saved_path(new_dest);
			Dictionary extra_info = report->get_extra_info();
			if (extra_info.has("external_buffer_paths")) {
				for (auto &E : extra_info["external_buffer_paths"].operator PackedStringArray()) {
					auto buffer_path = new_dest_base_dir.path_join(E.get_file());
					da->rename(E, buffer_path);
				}
			}
		}
	}

	void _clear_scene(void *p_userdata = nullptr) {
		if (root) {
			memdelete(root);
			root = nullptr;
		}
		_scene = nullptr;
		instance._unload_deps();
	}

	void clear_scene() {
		// Ensure that the scene is cleared on the main thread to avoid rare segfaults caused by race conditions in TextureStorage
		if (!TaskManager::get_singleton()->dispatch_to_main_thread(this, &BatchExportToken::_clear_scene, nullptr).has_value()) {
			err = ERR_SKIP;
		}
	}

	bool check_unsupported() {
		return _check_unsupported(ver_major, is_text_output());
	}

	Error create_packed_scene_from_mesh(const Ref<Mesh> &mesh, Ref<PackedScene> &scene) {
		ERR_FAIL_COND_V_MSG(mesh.is_null(), ERR_INVALID_PARAMETER, "Mesh is null");
		return TaskManager::get_singleton()->dispatch_to_main_thread((std::function<Error()>)[&]() -> Error {
											   MeshInstance3D *root = memnew(MeshInstance3D);
											   root->set_mesh(mesh);
											   scene = Ref<PackedScene>(memnew(PackedScene));
											   scene->pack(root);
											   return OK;
										   })
				.value_or(ERR_SKIP);
	}

	// scene loading and scene instancing has to be done on the main thread to avoid deadlocks and crashes
	bool batch_preload() {
		GDRELogger::clear_error_queues();
		if (check_unsupported()) {
			err = ERR_UNAVAILABLE;
			_set_unsupported(report, ver_major, is_obj_output());
			after_preload();
			return false;
		}

		if (is_text_output()) {
			after_preload();
			return false;
		}
		err = _check_cancelled();
		if (err != OK) {
			report->set_error(err);
			after_preload();
			return false;
		}
		instance._initial_set(p_src_path, report);

		err = gdre::ensure_dir(p_dest_path.get_base_dir());
		report->set_error(err);
		if (err) {
			after_preload();
			ERR_FAIL_V_MSG(false, "Failed to ensure directory " + p_dest_path.get_base_dir());
		}
		{
			String resource_type = report->get_import_info()->get_type();
			bool is_mesh = false;
			if (resource_type != "PackedScene") {
				if (resource_type != "Mesh" && !ClassDB::is_parent_class(resource_type, "Mesh")) {
					after_preload();
					ERR_FAIL_V_MSG(false, "Unsupported resource type: " + resource_type);
				}
				is_mesh = true;
			}
			Ref<Resource> resource;
			err = instance._load_scene_and_deps(resource);
			if (resource.is_null() && err == OK) {
				err = ERR_CANT_ACQUIRE_RESOURCE;
			}

			Ref<PackedScene> scene = resource;
			if (err == OK && scene.is_null() && is_mesh) {
				err = create_packed_scene_from_mesh(resource, scene);
			}

			if (err != OK) {
				report->set_error(err);
				after_preload();
				return false;
			}
			_scene = scene;
			if (!is_obj_output()) {
				root = instance._instantiate_scene(scene);
				if (root) {
					root = instance._set_stuff_from_instanced_scene(root);
				}
				if (!root) {
					clear_scene();
					err = _check_cancelled();
					if (err == OK) {
						err = ERR_CANT_ACQUIRE_RESOURCE;
					}
					report->set_error(err);
				}
			}
		}

		// print_line("Preloaded scene " + p_src_path);
		after_preload();
		return false;
	}

	void after_preload() {
		if (_scene.is_valid()) {
			// We have to flush the message queue after the scene is loaded;
			// Certain resources like NoiseTexture2D can queue up deferred calls that will cause crashes if not flushed before the scene is manipulated or freed
			if (!TaskManager::get_singleton()->dispatch_to_main_thread((std::function<void()>)[]() {
												 GDREMainLoop::iteration(true);
											 })
							.has_value()) {
				err = ERR_SKIP;
			}
		}
		preload_done = true;
	}

	void batch_export_instanced_scene() {
		while (!preload_done && _check_cancelled() == OK) {
			OS::get_singleton()->delay_usec(10000);
		}
		if (err == OK) {
			err = _check_cancelled();
		}
		if (err != OK) {
			clear_scene();
			report->set_error(err);
			export_done = true;
			return;
		}
		in_progress++;
		// print_line("Exporting scene " + p_src_path);
		export_start_time = OS::get_singleton()->get_ticks_msec();

		if (is_text_output() || is_obj_output()) {
			if (is_obj_output()) {
				err = SceneExporter::export_scene_to_obj(_scene, p_dest_path, report->get_import_info(), ver_major);
			} else {
				err = ResourceCompatLoader::to_text(p_src_path, p_dest_path);
			}
			if (err != OK) {
				report->append_error_messages(GDRELogger::get_thread_errors());
			} else {
				report->set_saved_path(p_dest_path);
			}
		} else {
			err = instance._batch_export_instanced_scene(root, p_dest_path);
			if (err == OK) {
				report->set_saved_path(p_dest_path);
			}
			if (GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/debug_copies", false)) {
				auto new_dest = "res://.tscn_copy/" + report->get_import_info()->get_export_dest().trim_prefix("res://.assets/").trim_prefix("res://").get_basename() + ".tscn";
				auto new_dest_path = output_dir.path_join(new_dest.replace_first("res://", ""));
				ResourceCompatLoader::to_text(p_src_path, new_dest_path);
			}
		}
		clear_scene();
		// print_line("Finished exporting scene " + p_src_path);
		report->set_error(err);
		finished = true;
		export_done = true;
		export_end_time = OS::get_singleton()->get_ticks_msec();
		in_progress--;
	}

	void post_export(Error p_skip_type = ERR_SKIP) {
		// GLTF export can result in inaccurate models
		// save it under .assets, which won't be picked up for import by the godot editor
		if (err == OK && !finished) {
			err = p_skip_type;
		}
		report->set_resources_used({ report->get_import_info()->get_dest_files() });
		report->set_error(err);
		if (err == ERR_SKIP) {
			report->set_message("Export cancelled.");
		} else if (err == ERR_TIMEOUT) {
			report->set_message("Export timed out.");
		}
		if (is_text_output() || is_obj_output()) {
			if (err == OK) {
				report->set_saved_path(p_dest_path);
			}
		} else {
			if (instance.had_script() && err == OK) {
				report->set_message("Script has scripts, not saving to original path.");
				if (!no_rename) {
					move_output_to_dot_assets();
				}
			} else if (err == OK) {
				report->set_saved_path(p_dest_path);
			} else if (err && !no_rename) {
				move_output_to_dot_assets();
			}
		}
	}
	// TaskRunnerStruct functions
	virtual int get_current_task_step_value() override {
		return 0;
	}
	virtual String get_current_task_step_description() override {
		return "Exporting scene " + p_src_path;
	}
	virtual void cancel() override {
		err = ERR_SKIP;
		instance.cancel();
	}
	virtual void run(void *p_userdata) override {
		batch_export_instanced_scene();
	}

	virtual ~BatchExportToken() = default;
};

std::atomic<int64_t> BatchExportToken::in_progress = 0;

Ref<ExportReport> SceneExporter::export_file_with_options(const String &out_path, const String &res_path, const Dictionary &options) {
	Ref<ImportInfo> iinfo;
	if (GDRESettings::get_singleton()->is_pack_loaded()) {
		auto found = GDRESettings::get_singleton()->get_import_info_by_dest(res_path);
		if (found.is_valid()) {
			iinfo = ImportInfo::copy(found);
		}
	}
	if (!iinfo.is_valid()) {
		bool is_resource = ResourceCompatLoader::handles_resource(res_path);
		if (is_resource) {
			// NOTE: If we start supporting non-resource scenes, we need to update this.
			iinfo = ImportInfo::load_from_file(res_path);
		}
		if (!iinfo.is_valid()) {
			Ref<ExportReport> report = memnew(ExportReport(nullptr, EXPORTER_NAME));
			if (!is_resource) {
				report->set_message(res_path + " is not a valid resource.");
				report->set_error(ERR_INVALID_PARAMETER);
			} else {
				report->set_message("Failed to load resource " + res_path + " for export");
				report->set_error(ERR_FILE_CANT_READ);
			}
			ERR_FAIL_V_MSG(report, report->get_message());
		}
	}
	String ext = out_path.get_extension().to_lower();
	bool to_text = ext == "escn" || ext == "tscn";
	bool to_obj = ext == "obj";
	bool non_gltf = ext != "glb" && ext != "gltf";
	int ver_major = iinfo->get_ver_major();
	if (_check_unsupported(ver_major, to_text)) {
		Ref<ExportReport> rep = memnew(ExportReport(iinfo, EXPORTER_NAME));
		_set_unsupported(rep, ver_major, to_obj);
		return rep;
	}
	String opath = out_path;
	if (to_text || to_obj) {
		Ref<ExportReport> obj_rep = memnew(ExportReport(iinfo, EXPORTER_NAME));
		Error err = export_file_to_non_glb(res_path, out_path, nullptr);
		obj_rep->set_error(err);
		if (err != OK) {
			obj_rep->append_error_messages(GDRELogger::get_errors());
		} else {
			obj_rep->set_saved_path(out_path);
		}
		return obj_rep;
	} else if (non_gltf) {
		opath = out_path.get_basename() + ".glb";
	}
	bool remove_physics_bodies = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/remove_physics_bodies", false);
	if (remove_physics_bodies) {
		unregister_physics_extension();
	}
	auto token = std::make_shared<BatchExportToken>(out_path.get_base_dir(), iinfo, options);
	token->set_export_dest(token->get_export_dest().get_basename() + "." + opath.get_extension());
	token->p_dest_path = opath;
	if (non_gltf) {
		token->report->append_message_detail({ "Attempting to export to non-GLTF format, saving to " + opath });
	}
	token->no_rename = true;
	token->instance.set_force_no_update_import_params(true);
	token->batch_preload();
	if (token->err != OK) {
		token->post_export(token->err);
		if (remove_physics_bodies) {
			register_physics_extension();
		}
		return token->report;
	}
	Error err;
	if (Thread::is_main_thread()) {
		err = TaskManager::get_singleton()->run_task(token, nullptr, "Exporting scene " + res_path, -1, true, true, true);
	} else { // we're on a task thread, run it here
		token->batch_export_instanced_scene();
		err = token->err;
	}
	token->post_export(err);
	if (remove_physics_bodies) {
		register_physics_extension();
	}
	return token->report;
}

void SceneExporter::prebatch_export() {
	bool remove_physics_bodies = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/remove_physics_bodies", false);
	if (remove_physics_bodies) {
		unregister_physics_extension();
	}
}

void SceneExporter::postbatch_export() {
	bool remove_physics_bodies = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/remove_physics_bodies", false);
	if (remove_physics_bodies) {
		register_physics_extension();
	}
}

Ref<ExportReport> SceneExporter::export_resource(const String &output_dir, Ref<ImportInfo> iinfo) {
	BatchExportToken token(output_dir, iinfo, {}, true);
	token.batch_preload();
	token.batch_export_instanced_scene();
	token.post_export();
	return token.report;
}

void SceneExporter::get_handled_types(List<String> *out) const {
	out->push_back("PackedScene");
}

void SceneExporter::get_handled_importers(List<String> *out) const {
	out->push_back("scene");
}

String SceneExporter::get_name() const {
	return EXPORTER_NAME;
}

String SceneExporter::get_default_export_extension(const String &res_path) const {
	return "glb";
}

Vector<String> SceneExporter::get_export_extensions(const String &res_path) const {
	return { "glb", "gltf", "obj", "escn", "tscn" };
}

void SceneExporter::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("export_file_with_options", "out_path", "res_path", "options"), &SceneExporter::export_file_with_options);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_minimum_godot_ver_supported"), &SceneExporter::get_minimum_godot_ver_supported);
}

uint64_t GLBExporterInstance::_get_error_count() {
	return supports_multithread() ? GDRELogger::get_thread_error_count() : GDRELogger::get_error_count();
}

Vector<String> GLBExporterInstance::_get_logged_error_messages() {
	auto errors = supports_multithread() ? GDRELogger::get_thread_errors() : GDRELogger::get_errors();
	Vector<String> ret;
	for (auto &err : errors) {
		String lstripped = err.strip_edges(true, false);
		if (!lstripped.begins_with("GDScript backtrace")) {
			ret.push_back(err.strip_edges(false, true));
		}
	}
	return ret;
}

SceneExporter *SceneExporter::singleton = nullptr;

SceneExporter *SceneExporter::get_singleton() {
	return singleton;
}

SceneExporter::SceneExporter() {
	singleton = this;
}

SceneExporter::~SceneExporter() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

Error GLBExporterInstance::_batch_export_instanced_scene(Node *root, const String &p_dest_path) {
	{
		err = _export_instanced_scene(root, p_dest_path);
	}

	// _export_instanced_scene should have already set the error report
	if (err != OK) {
		return err;
	}

	if (updating_import_info) {
		_update_import_params(p_dest_path);
	}
	// Check if the model can be loaded; minimum validation to ensure the model is valid
	err = _check_model_can_load(p_dest_path);
	if (err) {
		GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_CORRUPT, "");
	}
	err = _get_return_error();

	return err;
}
