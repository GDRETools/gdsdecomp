#include "scene_exporter.h"

#include "compat/resource_loader_compat.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/variant/variant.h"
#include "exporters/export_report.h"
#include "exporters/obj_exporter.h"
#include "external/tinygltf/tiny_gltf.h"
#include "modules/gltf/extensions/physics/gltf_document_extension_physics.h"
#include "modules/gltf/gltf_document.h"
#include "modules/gltf/structures/gltf_node.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/occluder_instance_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/texture.h"
#include "utility/common.h"
#include "utility/gdre_config.h"
#include "utility/gdre_logger.h"
#include "utility/gdre_settings.h"

#include "core/crypto/crypto_core.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "main/main.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/packed_scene.h"
#include "utility/task_manager.h"

#ifndef MAKE_GLTF_COPY
#define MAKE_GLTF_COPY 1
#endif
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
				r_result += String::num_scientific((float)num);
			} else {
				r_result += String::num_scientific(num);
			}
			r_result += num_str;
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

inline bool _all_buffers_empty(const TypedArray<Vector<uint8_t>> &p_buffers, int start_idx = 0) {
	for (int i = start_idx; i < p_buffers.size(); i++) {
		if (!p_buffers[i].operator PackedByteArray().is_empty()) {
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

	gltf_buffer["byteLength"] = state_buffers[0].operator PackedByteArray().size();
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
	}
	if (!err_msg.is_empty()) {
		err_message += ":\n  " + err_msg;
	}
	if (!desc.is_empty()) {
		err_message += "\n  Scene had " + desc;
	}
	error_statement = err_message;
	Vector<String> errors;
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
		report->append_message_detail({ "Dependencies:" });
		report->append_message_detail(dependency_resolution_list);
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
	if (texture.is_null() || (!force_replace && ResourceCache::get_ref(info.dep).is_valid())) {
		return;
	}
#ifdef TOOLS_ENABLED
	texture->set_import_path(info.remap);
#endif
	// reset the path cache, then set the path so it loads it into cache.
	texture->set_path_cache("");
	texture->set_path(info.dep, true);
	loaded_deps.push_back(texture);
}

String GLBExporterInstance::get_name_res(const Dictionary &dict, const Ref<Resource> &res, int64_t idx) {
	String name = dict.get("name", String());
	if (name.is_empty()) {
		name = res->get_name();
		if (name.is_empty()) {
			Ref<ResourceInfo> info = ResourceInfo::get_info_from_resource(res);
			if (info.is_valid() && !info->resource_name.is_empty()) {
				name = info->resource_name;
			} else {
				// name = res->get_class() + "_" + String::num_int64(idx);
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
	get_deps_recursive(source_path, get_deps_map);

	for (auto &E : get_deps_map) {
		dep_info &info = E.value;
		if (info.type == "Script") {
			has_script = true;
		} else if (info.dep.get_extension().to_lower().contains("shader")) {
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
		if (!FileAccess::exists(info.remap) && !FileAccess::exists(info.dep)) {
			if (ignore_missing_dependencies) {
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
				if (!ResourceCache::has(info.dep)) {
					auto texture = ResourceCompatLoader::custom_load(
							info.remap, "",
							ResourceCompatLoader::get_default_load_type(),
							&err,
							using_threaded_load(),
							ResourceFormatLoader::CACHE_MODE_IGNORE); // not ignore deep, we want to reuse dependencies if they exist
					if (err || texture.is_null()) {
						if (ignore_missing_dependencies) {
							WARN_PRINT(vformat("%s: Dependency %s:%s failed to load.", source_path, info.dep, info.remap));
							continue;
						}
						GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_MISSING_DEPENDENCIES,
								vformat("Dependency %s:%s failed to load.", info.dep, info.remap));
					}
					if (!ResourceCache::has(info.dep)) {
						set_cache_res(info, texture, false);
					}
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
	dict["generate/navmesh"] = 0;
	dict["physics/body_type"] = 0;
	dict["physics/shape_type"] = 7;
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

	dict["generate/occluder"] = 0;
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

Dictionary get_node_options(Node *p_node) {
	Dictionary node_options_dict = Dictionary();

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node);
	AnimationPlayer *animation_player = Object::cast_to<AnimationPlayer>(p_node);
	Ref<Script> script = p_node->get_script();
	if (!mesh_instance && !animation_player) {
		String type_name;
		if (script.is_valid()) {
			type_name = script->get_global_name();
			// TODO: fix FakeScript to not put the path in the global name if it doesn't have a class_name
			if (type_name.begins_with("res://")) {
				type_name = "";
			}
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
			if (Object::cast_to<StaticBody3D>(child)) {
				auto static_body = Object::cast_to<StaticBody3D>(child);
				physics_body_node = static_body;
				physics_material_override = static_body->get_physics_material_override();
				body_type = SceneExporterEnums::BODY_TYPE_STATIC;
				mesh_physics_mode = SceneExporterEnums::MESH_PHYSICS_MESH_AND_STATIC_COLLIDER;
			}
			// navmesh
			if (Object::cast_to<NavigationMesh>(child)) {
				navmesh_mode = SceneExporterEnums::NAVMESH_MESH_AND_NAVMESH;
			}
			// occluder
			if (auto occluder_instance = Object::cast_to<OccluderInstance3D>(child); occluder_instance) {
				occlusion_simplification_distance = occluder_instance->get_bake_simplification_distance();
				occluder_mode = SceneExporterEnums::OCCLUDER_MESH_AND_OCCLUDER;
			}
		}

		node_options_dict["generate/physics"] = mesh_physics_mode != SceneExporterEnums::MESH_PHYSICS_DISABLED;
		node_options_dict["generate/navmesh"] = navmesh_mode;
		Dictionary primtive_options_dict = Dictionary();

		if (physics_body_node) {
			physics_layer_bits = physics_body_node->get_collision_layer();
			physics_mask_bits = physics_body_node->get_collision_mask();
			TypedArray<Node> physics_nodes = p_node->find_children("*", "CollisionObject3D");
			if (physics_nodes.size() > 1) {
				// easy, it's decomposing convex
				shape_type = SceneExporterEnums::SHAPE_TYPE_DECOMPOSE_CONVEX;
			} else if (physics_nodes.size() == 1) {
				CollisionObject3D *physics_node = Object::cast_to<CollisionObject3D>(physics_nodes[0]);
				if (physics_node) {
					auto shape = physics_node->get("shape");
					if (auto convex_shape = Object::cast_to<ConvexPolygonShape3D>(shape); convex_shape) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_SIMPLE_CONVEX;
					} else if (auto concave_shape = Object::cast_to<ConcavePolygonShape3D>(shape); concave_shape) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_TRIMESH;
					} else if (auto box_shape = Object::cast_to<BoxShape3D>(shape); box_shape) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_BOX;
						primtive_options_dict["primitive/size"] = box_shape->get_size();
						primtive_options_dict["primitive/position"] = physics_node->get_position();
						primtive_options_dict["primitive/rotation"] = physics_node->get_rotation();
					} else if (auto sphere_shape = Object::cast_to<SphereShape3D>(shape); sphere_shape) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_SPHERE;
						primtive_options_dict["primitive/radius"] = sphere_shape->get_radius();
						primtive_options_dict["primitive/position"] = physics_node->get_position();
						primtive_options_dict["primitive/rotation"] = physics_node->get_rotation();
					} else if (auto cylinder_shape = Object::cast_to<CylinderShape3D>(shape); cylinder_shape) {
						shape_type = SceneExporterEnums::SHAPE_TYPE_CYLINDER;
						primtive_options_dict["primitive/height"] = cylinder_shape->get_height();
						primtive_options_dict["primitive/radius"] = cylinder_shape->get_radius();
						primtive_options_dict["primitive/position"] = physics_node->get_position();
						primtive_options_dict["primitive/rotation"] = physics_node->get_rotation();
					} else if (auto capsule_shape = Object::cast_to<CapsuleShape3D>(shape); capsule_shape) {
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
			node_options_dict["physics/physics_material_override"] = physics_material_override.is_valid() ? (Variant)physics_material_override : Variant();
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

	return NodePath(path, true);
}

void GLBExporterInstance::_set_stuff_from_instanced_scene(Node *root) {
	root_type = root->get_class();
	root_name = root->get_name();

	TypedArray<Node> animation_player_nodes = root->find_children("*", "AnimationPlayer");
	TypedArray<Node> mesh_instances = root->find_children("*", "MeshInstance3D");
	HashSet<Node *> skinned_mesh_instances;
	HashSet<Ref<Mesh>> meshes_in_mesh_instances;
	for (auto &E : mesh_instances) {
		MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(E);
		ERR_CONTINUE(!mesh_instance);
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
		if (updating_import_info) {
			auto node_path = get_node_path(mesh_instance);
			if (!node_path.is_empty()) {
				node_options[node_path.operator String()] = get_node_options(mesh_instance);
			}
		}
	}
	TypedArray<Node> physics_nodes = root->find_children("*", "CollisionObject3D");
	TypedArray<Node> physics_shapes = root->find_children("*", "CollisionShape3D");
	has_physics_nodes = physics_nodes.size() > 0 || physics_shapes.size() > 0;

	if (has_script) {
		TypedArray<Node> nodes = { root };
		nodes.append_array(root->get_children());
		for (int i = 0; i < nodes.size(); i++) {
			auto &E = nodes[i];
			Node *node = static_cast<Node *>(E.operator Object *());
			ScriptInstance *si = node->get_script_instance();
			List<PropertyInfo> properties;
			if (si) {
				if (updating_import_info) {
					node_options[get_node_path(node).operator String()] = get_node_options(node);
				}
				si->get_property_list(&properties);
				HashSet<Variant> other_values;
				Vector<MeshInstance3D *> mesh_instances;
				HashSet<Ref<Skin>> skins;
				HashSet<NodePath> skeleton_paths;
				for (auto &prop : properties) {
					Variant value;
					if (si->get(prop.name, value)) {
						// check if it's a mesh instance
						Ref<Mesh> mesh = value;
						if (mesh.is_valid() && !meshes_in_mesh_instances.has(mesh)) {
							// create a new mesh instance
							auto mesh_instance = memnew(MeshInstance3D());
							mesh_instance->set_mesh(mesh);
							String name = mesh->get_name();
							String path = mesh->get_path();

							if (name.is_empty()) {
								name = demangle_name(path.get_file().get_basename());
								if (name.is_empty() || path.contains("::")) {
									name = ("Mesh_" + String::num_int64(i));
								}
								mesh->set_name(name);
							}
							if (!path.is_empty()) {
								mesh_path_to_instance_map[path] = mesh_instance;
							}
							mesh_instance->set_name(name);
							node->add_child(mesh_instance);
							mesh_instances.push_back(mesh_instance);
							// meshes_in_mesh_instances.insert(mesh);
						}
					}
				}
				for (auto &mesh_instance : mesh_instances) {
					bool set_skin = false;
					bool set_skeleton = false;
					for (auto &prop : properties) {
						Variant value;
						if (si->get(prop.name, value) && !skins.has(value) && !skeleton_paths.has(value)) {
							Ref<Skin> skin = value;
							if (skin.is_valid() && !set_skin) {
								has_skinned_meshes = true;
								skinned_mesh_instances.insert(mesh_instance);
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
											actual_path = node->get_path_to(skeleton3d);
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
				}
			}
		}
	}
	Vector<int64_t> fps_values;
	for (int32_t node_i = 0; node_i < animation_player_nodes.size(); node_i++) {
		bool any_compressed = false;
		// Force re-compute animation tracks.
		Vector<Ref<AnimationLibrary>> anim_libs;
		AnimationPlayer *player = Object::cast_to<AnimationPlayer>(animation_player_nodes[node_i]);
		List<StringName> anim_lib_names;
		player->get_animation_library_list(&anim_lib_names);
		for (auto &lib_name : anim_lib_names) {
			Ref<AnimationLibrary> lib = player->get_animation_library(lib_name);
			if (lib.is_valid()) {
				anim_libs.push_back(lib);
			}
		}
		ERR_CONTINUE(!player);
		auto current_anmation = player->get_current_animation();
		auto current_pos = current_anmation.is_empty() ? 0 : player->get_current_animation_position();
		int64_t max_fps = -1;
		for (auto &anim_lib : anim_libs) {
			List<StringName> anim_names;
			anim_lib->get_animation_list(&anim_names);
			if (ver_major <= 3 && anim_names.size() > 0) {
				// force re-compute animation tracks.
				player->set_current_animation(anim_names.front()->get());
				player->advance(0);
				player->set_current_animation(current_anmation);
				if (!current_anmation.is_empty()) {
					player->seek(current_pos);
				}
			}
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

static const HashSet<String> shader_param_names = {
	"albedo",
	"specular",
	"metallic",
	"roughness",
	"emission",
	"emission_energy",
	"normal_scale",
	"rim",
	"rim_tint",
	"clearcoat",
	"clearcoat_roughness",
	"anisotropy",
	"heightmap_scale",
	"subsurface_scattering_strength",
	"transmittance_color",
	"transmittance_depth",
	"transmittance_boost",
	"backlight",
	"refraction",
	"point_size",
	"uv1_scale",
	"uv1_offset",
	"uv2_scale",
	"uv2_offset",
	"particles_anim_h_frames",
	"particles_anim_v_frames",
	"particles_anim_loop",
	"heightmap_min_layers",
	"heightmap_max_layers",
	"heightmap_flip",
	"uv1_blend_sharpness",
	"uv2_blend_sharpness",
	"grow",
	"proximity_fade_distance",
	"msdf_pixel_range",
	"msdf_outline_size",
	"distance_fade_min",
	"distance_fade_max",
	"ao_light_affect",
	"metallic_texture_channel",
	"ao_texture_channel",
	"clearcoat_texture_channel",
	"rim_texture_channel",
	"heightmap_texture_channel",
	"refraction_texture_channel",
	"alpha_scissor_threshold",
	"alpha_hash_scale",
	"alpha_antialiasing_edge",
	"albedo_texture_size",
	"z_clip_scale",
	"fov_override",
};

bool set_param_not_in_property_list(Ref<BaseMaterial3D> p_base_material, const String &p_param_name, const Variant &p_value) {
	// if the param is not in the property list, but it does have a setter, we can set it
	if (p_base_material->has_method(vformat("set_%s", p_param_name))) {
		if (p_base_material->has_method(vformat("get_%s", p_param_name))) {
			Variant base_material_val = p_base_material->call(vformat("get_%s", p_param_name));
			if (base_material_val.get_type() != p_value.get_type()) {
				return false;
			}
		}
		p_base_material->call(vformat("set_%s", p_param_name), p_value);
		return true;
	} else if (p_param_name == "metallness" && p_value.get_type() == Variant::FLOAT) {
		p_base_material->set_metallic(p_value);
		return true;
	}
	return false;
}
struct UniformInfo {
	String scope;
	String type;
	String name;
	String hint;
};

Pair<Ref<BaseMaterial3D>, bool> convert_shader_material_to_base_material(Ref<ShaderMaterial> p_shader_material, Node *p_parent = nullptr) {
	// we need to manually create a BaseMaterial3D from the shader material
	// We do this by getting the shader uniforms and then mapping them to the BaseMaterial3D properties
	// We also need to handle the texture parameters and features
	List<StringName> list;
	Ref<Shader> shader = p_shader_material->get_shader();
	List<PropertyInfo> prop_list;
	List<PropertyInfo> uniform_list;
	HashSet<String> shader_uniforms;
	shader->get_shader_uniform_list(&uniform_list, false);
	for (auto &E : uniform_list) {
		shader_uniforms.insert(E.name);
	}
	HashSet<String> global_uniforms;
	HashSet<String> instance_uniforms;
	//(global|instance)?\s*uniform\s+(\w+)\s+(\w+)\s*(?:\:\s+(\w+))?

	String shader_code = shader->get_code();
	Ref<RegEx> re = RegEx::create_from_string(R"((global|instance)?\s*uniform\s+(?:(?:lowp|mediump|highp)\s+)?(\w+)\s+(\w+)\s*(?:\:((?:\s*\w+\s*,)*\s*\w+))?)");
	// Don't match "normal" or "form"
	Ref<RegEx> orm_re = RegEx::create_from_string("(^ORM|[^NF]ORM[a-z_]|[a-z_]ORM|^orm|_orm|[^NnFf]orm_|Orm).*");
	auto is_orm = [&](const String &p_param_name) {
		return p_param_name == "orm" || orm_re->search(p_param_name).is_valid();
	};
	auto matches = re->search_all(shader_code);
	bool has_orm = false;
	HashMap<String, UniformInfo> uniform_infos;
	for (auto &E : matches) {
		Ref<RegExMatch> match = E;
		UniformInfo info;
		String param_name = match->get_string(3);

		info.scope = match->get_string(1);
		info.type = match->get_string(2);
		info.name = match->get_string(3);
		info.hint = match->get_string(4).strip_edges();
		uniform_infos[info.name] = info;
		shader_uniforms.insert(param_name);
		if (info.scope == "global") {
			global_uniforms.insert(param_name);
		} else if (info.scope == "instance") {
			instance_uniforms.insert(param_name);
		}
		if (info.scope == "sampler2D") {
			if (is_orm(param_name)) {
				has_orm = true;
			}
		}
	}

	p_shader_material->get_property_list(&prop_list, false);
	for (auto &E : prop_list) {
		if (E.usage == PROPERTY_USAGE_CATEGORY || E.usage == PROPERTY_USAGE_GROUP || E.usage == PROPERTY_USAGE_SUBGROUP) {
			continue;
		}
		if (E.name.begins_with("shader_parameter/")) {
			shader_uniforms.insert(E.name.trim_prefix("shader_parameter/"));
		}
	}

	Ref<BaseMaterial3D> base_material = memnew(BaseMaterial3D(has_orm));

	// this is initialized with the 3.x params that are still valid in 4.x (`_set()` handles the remapping) but won't show up in the property list
	HashSet<String> base_material_params = {
		"flags_use_shadow_to_opacity",
		"flags_use_shadow_to_opacity",
		"flags_no_depth_test",
		"flags_use_point_size",
		"flags_fixed_size",
		"flags_albedo_tex_force_srgb",
		"flags_do_not_receive_shadows",
		"flags_disable_ambient_light",
		"params_diffuse_mode",
		"params_specular_mode",
		"params_blend_mode",
		"params_cull_mode",
		"params_depth_draw_mode",
		"params_point_size",
		"params_billboard_mode",
		"params_billboard_keep_scale",
		"params_grow",
		"params_grow_amount",
		"params_alpha_scissor_threshold",
		"params_alpha_hash_scale",
		"params_alpha_antialiasing_edge",
		"depth_scale",
		"depth_deep_parallax",
		"depth_min_layers",
		"depth_max_layers",
		"depth_flip_tangent",
		"depth_flip_binormal",
		"depth_texture",
		"emission_energy",
		"flags_transparent",
		"flags_unshaded",
		"flags_vertex_lighting",
		"params_use_alpha_scissor",
		"params_use_alpha_hash",
		"depth_enabled",
	};
	List<PropertyInfo> base_material_prop_list;
	base_material->get_property_list(&base_material_prop_list);
	bool hit_base_material_group = false;
	for (auto &E : base_material_prop_list) {
		if (E.usage == PROPERTY_USAGE_CATEGORY && E.name == "BaseMaterial3D") {
			hit_base_material_group = true;
		}
		if (!hit_base_material_group) {
			continue;
		}
		if (E.usage == PROPERTY_USAGE_CATEGORY || E.usage == PROPERTY_USAGE_GROUP || E.usage == PROPERTY_USAGE_SUBGROUP) {
			if (E.name != "StandardMaterial3D") {
				continue;
			}
			break;
		}
		base_material_params.insert(E.name);
	}

	static const HashMap<String, BaseMaterial3D::TextureParam> texture_param_map = {
		{ "albedo_texture", BaseMaterial3D::TEXTURE_ALBEDO },
		{ "orm_texture", BaseMaterial3D::TEXTURE_ORM },
		{ "metallic_texture", BaseMaterial3D::TEXTURE_METALLIC },
		{ "roughness_texture", BaseMaterial3D::TEXTURE_ROUGHNESS },
		{ "emission_texture", BaseMaterial3D::TEXTURE_EMISSION },
		{ "normal_texture", BaseMaterial3D::TEXTURE_NORMAL },
		{ "bent_normal_texture", BaseMaterial3D::TEXTURE_BENT_NORMAL },
		{ "rim_texture", BaseMaterial3D::TEXTURE_RIM },
		{ "clearcoat_texture", BaseMaterial3D::TEXTURE_CLEARCOAT },
		{ "anisotropy_flowmap", BaseMaterial3D::TEXTURE_FLOWMAP },
		{ "ao_texture", BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION },
		{ "heightmap_texture", BaseMaterial3D::TEXTURE_HEIGHTMAP },
		{ "subsurf_scatter_texture", BaseMaterial3D::TEXTURE_SUBSURFACE_SCATTERING },
		{ "subsurf_scatter_transmittance_texture", BaseMaterial3D::TEXTURE_SUBSURFACE_TRANSMITTANCE },
		{ "backlight_texture", BaseMaterial3D::TEXTURE_BACKLIGHT },
		{ "refraction_texture", BaseMaterial3D::TEXTURE_REFRACTION },
		{ "detail_mask", BaseMaterial3D::TEXTURE_DETAIL_MASK },
		{ "detail_albedo", BaseMaterial3D::TEXTURE_DETAIL_ALBEDO },
		{ "detail_normal", BaseMaterial3D::TEXTURE_DETAIL_NORMAL },
		{ "depth_texture", BaseMaterial3D::TEXTURE_HEIGHTMAP }, // old 3.x name for heightmap texture
		{ "orm_texture", BaseMaterial3D::TEXTURE_ORM },
	};

	static const HashMap<BaseMaterial3D::TextureParam, BaseMaterial3D::Feature> feature_to_enable_map = {
		{ BaseMaterial3D::TEXTURE_NORMAL, BaseMaterial3D::FEATURE_NORMAL_MAPPING },
		{ BaseMaterial3D::TEXTURE_BENT_NORMAL, BaseMaterial3D::FEATURE_BENT_NORMAL_MAPPING },
		{ BaseMaterial3D::TEXTURE_EMISSION, BaseMaterial3D::FEATURE_EMISSION },
		{ BaseMaterial3D::TEXTURE_RIM, BaseMaterial3D::FEATURE_RIM },
		{ BaseMaterial3D::TEXTURE_CLEARCOAT, BaseMaterial3D::FEATURE_CLEARCOAT },
		{ BaseMaterial3D::TEXTURE_FLOWMAP, BaseMaterial3D::FEATURE_ANISOTROPY },
		{ BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION },
		{ BaseMaterial3D::TEXTURE_HEIGHTMAP, BaseMaterial3D::FEATURE_HEIGHT_MAPPING },
		{ BaseMaterial3D::TEXTURE_SUBSURFACE_SCATTERING, BaseMaterial3D::FEATURE_SUBSURFACE_SCATTERING },
		{ BaseMaterial3D::TEXTURE_SUBSURFACE_TRANSMITTANCE, BaseMaterial3D::FEATURE_SUBSURFACE_TRANSMITTANCE },
		{ BaseMaterial3D::TEXTURE_BACKLIGHT, BaseMaterial3D::FEATURE_BACKLIGHT },
		{ BaseMaterial3D::TEXTURE_REFRACTION, BaseMaterial3D::FEATURE_REFRACTION },
		{ BaseMaterial3D::TEXTURE_DETAIL_MASK, BaseMaterial3D::FEATURE_DETAIL },
		{ BaseMaterial3D::TEXTURE_DETAIL_ALBEDO, BaseMaterial3D::FEATURE_DETAIL },
		{ BaseMaterial3D::TEXTURE_DETAIL_NORMAL, BaseMaterial3D::FEATURE_DETAIL },
	};

	Vector<String> lines = shader_code.split("\n");
	bool set_texture = false;
	HashMap<BaseMaterial3D::TextureParam, Vector<Pair<String, Ref<Texture>>>> base_material_texture_candidates;
	for (int i = 0; i < BaseMaterial3D::TEXTURE_MAX; i++) {
		base_material_texture_candidates[BaseMaterial3D::TextureParam(i)] = Vector<Pair<String, Ref<Texture>>>();
	}
	HashMap<String, Variant> set_params;
	HashMap<String, Variant> non_set_params;
	auto add_orm_texture_candidate = [&](const String &param_name, const Ref<Texture> &tex) {
		String lparam_name = param_name.to_lower();
		bool did_set = true;
		if (lparam_name.contains("metallic") || lparam_name.contains("metalness")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_METALLIC].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (lparam_name.contains("roughness") || lparam_name.contains("rough")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ROUGHNESS].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (lparam_name.contains("ao") || lparam_name.contains("occlusion") || lparam_name.contains("ambient_occlusion")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (is_orm(param_name)) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ORM].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else {
			did_set = false;
		}
		return did_set;
	};
	auto add_texture_candidate = [&](const String &param_name, const String &hint, const Ref<Texture> &tex) {
		String lparam_name = param_name.to_lower();
		if (lparam_name.contains("albedo")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ALBEDO].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (lparam_name.contains("normal") && !hint.contains("roughness_normal")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_NORMAL].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (lparam_name.contains("emissive") || lparam_name.contains("emission")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else {
			return add_orm_texture_candidate(param_name, tex);
		}
		return true;
	};
	Vector<Pair<Pair<String, String>, Ref<Texture>>> unknown_texture_candidates;
	for (auto &E : shader_uniforms) {
		String param_name = E.trim_prefix("shader_parameter/");
		Variant val = p_shader_material->get_shader_parameter(param_name);

		bool did_set = false;
		if (base_material_params.has(param_name)) {
			Variant base_material_val;
			if (global_uniforms.has(param_name)) {
				base_material_val = ProjectSettings::get_singleton()->get_setting("shader_globals/" + param_name);
			} else if (instance_uniforms.has(param_name) && p_parent) {
				bool valid = false;
				Variant parent_val = p_parent->get(param_name, &valid);
				if (valid) {
					base_material_val = parent_val;
				}
			} else {
				base_material_val = base_material->get(param_name);
			}

			if (base_material_val.get_type() == val.get_type()) {
				base_material->set(param_name, val);
				did_set = true;
				set_params[param_name] = val;
			}
		} else if (set_param_not_in_property_list(base_material, param_name, val)) {
			did_set = true;
			set_params[param_name] = val;
		}
		if (!did_set) {
			non_set_params[param_name] = val;
		}

		Ref<Texture> tex = val;
		if (!tex.is_valid()) {
			continue;
		}
		if (did_set) {
			set_texture = true;
			auto param = texture_param_map.has(param_name) ? texture_param_map.get(param_name) : BaseMaterial3D::TEXTURE_MAX;
			if (param != BaseMaterial3D::TEXTURE_MAX) {
				base_material_texture_candidates[param].push_back(Pair<String, Ref<Texture>>(param_name, tex));
			} else {
				WARN_PRINT(vformat("Unknown texture parameter: %s", param_name));
			}
			continue;
		}

		String hint;
		// trying to capture "uniform sampler2D emissive_texture : hint_black;"
		if (uniform_infos.has(param_name)) {
			hint = uniform_infos[param_name].hint;
		}

		// renames from 3.x to 4.x
		// { "hint_albedo", "source_color" },
		// { "hint_aniso", "hint_anisotropy" },
		// { "hint_black", "hint_default_black" },
		// { "hint_black_albedo", "hint_default_black" },
		// { "hint_color", "source_color" },
		// { "hint_white", "hint_default_white" },
		auto lparam_name = param_name.to_lower();

		if (hint.contains("hint_albedo") || hint.contains("source_color")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ALBEDO].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (hint.contains("hint_white") || hint.contains("hint_default_white")) {
			add_orm_texture_candidate(param_name, tex);
		} else if (hint.contains("hint_normal")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_NORMAL].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else if (hint.contains("hint_black") || hint.contains("hint_default_black")) {
			// if (param_name.contains("emissive") || param_name.contains("emission")) {
			// 	base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back(Pair<String, Ref<Texture>>(param_name, tex));
			// } else {
			// 	default_textures[param_name.trim_prefix("shader_parameter/")] = tex;
			// }
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back(Pair<String, Ref<Texture>>(param_name, tex));
		} else {
			unknown_texture_candidates.push_back({ { param_name, hint }, tex });
		}
	}
	for (auto &E : unknown_texture_candidates) {
		add_texture_candidate(E.first.first, E.first.second, E.second);
	}
	for (int i = 0; i < BaseMaterial3D::TEXTURE_MAX; i++) {
		auto &candidates = base_material_texture_candidates[BaseMaterial3D::TextureParam(i)];
		auto existing_tex = base_material->get_texture(BaseMaterial3D::TextureParam(i));

		bool should_not_set = candidates.is_empty() && !existing_tex.is_valid();
		if (should_not_set) {
			continue;
		}

		if (feature_to_enable_map.has(BaseMaterial3D::TextureParam(i))) {
			base_material->set_feature(feature_to_enable_map[BaseMaterial3D::TextureParam(i)], true);
		}

		if (existing_tex.is_valid()) {
			continue;
		}
		if (candidates.size() == 1) {
			base_material->set_texture(BaseMaterial3D::TextureParam(i), candidates[0].second);
			set_texture = true;
			continue;
		}
		bool set_this_texture = false;
		if (i == BaseMaterial3D::TEXTURE_ALBEDO) {
			for (auto &E : candidates) {
				if (E.first.contains("albedo")) {
					base_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, E.second);
					set_texture = true;
					set_this_texture = true;
				} else if (E.first.contains("emissi") && set_this_texture) {
					// push this back to the emission candidates
					base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back(E);
				}
			}
		} else if (i == BaseMaterial3D::TEXTURE_NORMAL) {
			for (auto &E : candidates) {
				if (E.first.contains("normal")) {
					base_material->set_texture(BaseMaterial3D::TEXTURE_NORMAL, E.second);
					set_texture = true;
					set_this_texture = true;
				}
			}
		}
		if (!set_this_texture) {
			// just use the first one
			base_material->set_texture(BaseMaterial3D::TextureParam(i), candidates[0].second);
			set_texture = true;
		}
	}
	if (shader_uniforms.has("brightness") && p_shader_material->get_shader_parameter("brightness").get_type() == Variant::FLOAT) {
		float brightness = p_shader_material->get_shader_parameter("brightness");
		Color get_emission = base_material->get_emission();
		if (get_emission == Color()) {
			get_emission = base_material->get_albedo();
		}
		base_material->set_emission(get_emission * brightness);
	}
	if (shader_uniforms.has("alpha")) {
		Variant alpha_val = p_shader_material->get_shader_parameter("alpha");
		if (alpha_val.get_type() == Variant::FLOAT) {
			base_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			base_material->set_alpha_antialiasing(BaseMaterial3D::ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE);
			Color albedo = base_material->get_albedo();
			albedo.a = alpha_val;
			base_material->set_albedo(albedo);
		}
	}
	base_material->set_name(p_shader_material->get_name());
	// set the path to the shader material's path so that we can add it to the external deps found if necessary
	base_material->set_path_cache(p_shader_material->get_path());

	return { base_material, set_texture };
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
		if (after_4_4 || force_require_KHR_node_visibility) {
			doc->set_visibility_mode(GLTFDocument::VisibilityMode::VISIBILITY_MODE_INCLUDE_REQUIRED);
		} else {
			doc->set_visibility_mode(GLTFDocument::VisibilityMode::VISIBILITY_MODE_INCLUDE_OPTIONAL);
		}
		int32_t flags = 0;
		auto exts = doc->get_supported_gltf_extensions();
		flags |= 16; // EditorSceneFormatImporter::IMPORT_USE_NAMED_SKIN_BINDS;
		bool was_silenced = _is_logger_silencing_errors();
		_silence_errors(true);
		other_error_messages = _get_logged_error_messages();
		auto errors_before = _get_error_count();
		err = doc->append_from_scene(root, state, flags);
		_silence_errors(was_silenced);
		if (err) {
			gltf_serialization_error_messages.append_array(_get_logged_error_messages());
			GDRE_SCN_EXP_FAIL_V_MSG(ERR_COMPILATION_FAILED, "Failed to append scene " + source_path + " to glTF document");
		}
		GDRE_SCN_EXP_CHECK_CANCEL();

		// remove shader materials from meshes in the state before serializing
		if (replace_shader_materials) {
			for (auto &E : state->get_meshes()) {
				Ref<GLTFMesh> mesh = E;
				if (mesh.is_valid()) {
					Ref<ImporterMesh> im = mesh->get_mesh();
					ERR_CONTINUE_MSG(im.is_null(), "ImporterMesh is null");
					auto instance_materials = mesh->get_instance_materials();

					String path = get_path_res(im);
					MeshInstance3D *instance = nullptr;
					if (!path.is_empty() && mesh_path_to_instance_map.has(path)) {
						instance = mesh_path_to_instance_map[path];
					}
					for (int surface_i = 0; surface_i < im->get_surface_count(); surface_i++) {
						Ref<ShaderMaterial> shader_material;
						Ref<BaseMaterial3D> base_material;
						Pair<Ref<BaseMaterial3D>, bool> base_material_pair;
						if (surface_i < instance_materials.size()) {
							shader_material = instance_materials[surface_i];
						} else {
							shader_material = im->get_surface_material(surface_i);
						}
						if (shader_material.is_valid()) {
							base_material_pair = convert_shader_material_to_base_material(shader_material, instance);
							if (!base_material_pair.second) {
								base_material = im->get_surface_material(surface_i);
								if (!base_material.is_valid()) {
									Ref<ShaderMaterial> surface_shader_material = im->get_surface_material(surface_i);
									if (surface_shader_material.is_valid() && surface_shader_material != shader_material) {
										auto new_material_pair = convert_shader_material_to_base_material(surface_shader_material);
										if (new_material_pair.second) {
											shader_material = surface_shader_material;
											base_material_pair = new_material_pair;
										}
									}
								}
							}
							if (!base_material.is_valid()) {
								base_material = base_material_pair.first;
							}
							if (base_material.is_valid()) {
								if (surface_i < instance_materials.size()) {
									instance_materials[surface_i] = base_material;
								}
								im->set_surface_material(surface_i, base_material);
							}
						}
					}
					// This shouldn't happen??
					if (instance_materials.size() > im->get_surface_count()) {
						WARN_PRINT("Instance materials size is greater than the mesh's surface count????");
						for (int i = im->get_surface_count(); i < instance_materials.size(); i++) {
							Ref<ShaderMaterial> shader_material = instance_materials[i];
							if (shader_material.is_valid()) {
								auto new_material_pair = convert_shader_material_to_base_material(shader_material);
								if (new_material_pair.second) {
									instance_materials[i] = new_material_pair.first;
								}
							}
						}
					}
					mesh->set_instance_materials(instance_materials);
				}
			}
		}

		_silence_errors(true);
		err = doc->_serialize(state);
		_silence_errors(was_silenced);
		GDRE_SCN_EXP_CHECK_CANCEL();

		auto errors_after = _get_error_count();
		if (errors_after > errors_before) {
			gltf_serialization_error_messages.append_array(_get_logged_error_messages());
		}
		if (err) {
			GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_CANT_WRITE, "Failed to serialize glTF document");
		}

#if MAKE_GLTF_COPY
		{
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
#endif
		// rename objects in the state so that they will be imported correctly, and gather import params info
		{
			auto json = state->get_json();
			auto materials = state->get_materials();
			Array images = state->get_images();
			Array json_images = json.has("images") ? (Array)json["images"] : Array();
			HashMap<String, Vector<int>> image_map;
			auto insert_image_map = [&](String &name, int i) {
				if (!image_map.has(name)) {
					image_map[name] = Vector<int>();
					image_map[name].push_back(i);
				} else {
					image_map[name].push_back(i);
					name = String(name) + vformat("_%03d", image_map[name].size() - 1);
				}
			};
			static const HashMap<String, Vector<BaseMaterial3D::TextureParam>> generated_tex_suffixes = {
				{ "emission", { BaseMaterial3D::TEXTURE_EMISSION } },
				{ "normal", { BaseMaterial3D::TEXTURE_NORMAL } },
				// These are imported into the same texture, and the materials use that same texture for each of these params.
				{ "orm", { BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, BaseMaterial3D::TEXTURE_ROUGHNESS, BaseMaterial3D::TEXTURE_METALLIC } },
				{ "albedo", { BaseMaterial3D::TEXTURE_ALBEDO } }
			};
			for (int i = 0; i < json_images.size(); i++) {
				Dictionary image_dict = json_images[i];
				Ref<Texture2D> image = images[i];
				auto path = get_path_res(image);
				String name = image_dict.get("name", String());
				if (path.is_empty() && !name.is_empty()) {
					for (auto E : image_deps_needed) {
						if (E.get_file().get_basename() == name) {
							path = E;
							break;
						}
					}
				}
				if (path.is_empty() && !get_name_res(image_dict, image, i).is_empty()) {
					name = get_name_res(image_dict, image, i);
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
					insert_image_map(name, i);
				} else {
					name = path.get_file().get_basename();
					external_deps_updated.insert(path);
					insert_image_map(name, i);
					unsigned char md5_hash[16];
					Ref<Image> img = image->get_image();
					auto img_data = img->get_data();
					CryptoCore::md5(img_data.ptr(), img_data.size(), md5_hash);
					String new_md5 = String::hex_encode_buffer(md5_hash, 16);
					image_path_to_data_hash[path] = new_md5;
				}
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
				Vector<String> mesh_names;
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
					String name;
					bool is_internal = path.is_empty() || path.get_file().contains("::");
					if (is_internal) {
						name = get_name_res(mesh_dict, imesh, i);
					} else {
						name = path.get_file().get_basename();
					}
					if (!name.is_empty()) {
						mesh_dict["name"] = demangle_name(name);
						if (original_name.is_empty()) {
							gltf_mesh->set_original_name(name);
						}
					} else if (!original_name.is_empty()) {
						mesh_dict["name"] = original_name;
						name = original_name;
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
						mesh_info.has_lightmap_uv2 = instance_mesh->get_lightmap_size_hint() != default_light_map_size;

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
				Array json_materials = json["materials"];
				for (int i = 0; i < materials.size(); i++) {
					Dictionary material_dict = json_materials[i];
					Ref<Material> material = materials[i];
					auto path = get_path_res(material);
					String name;
					bool is_internal = path.is_empty() || path.get_file().contains("::");
					if (is_internal) {
						name = get_name_res(material_dict, material, i);
					} else {
						name = path.get_file().get_basename();
					}
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
#if MAKE_GLTF_COPY
		GDRE_SCN_EXP_CHECK_CANCEL();
		if (p_dest_path.get_extension() == "glb") {
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
#endif
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

	if (animation_options.size() > 0) {
		if (!_subresources_dict.has("animations")) {
			_subresources_dict["animations"] = Dictionary();
		}
		Dictionary animations_dict = _subresources_dict["animations"];
		for (auto &E : animation_options) {
			animations_dict[E.key] = E.value;
			String path = get_path_options(E.value);
			if (!(path.is_empty() || path.get_file().contains("::"))) {
				external_deps_updated.insert(path);
				animation_deps_updated.insert(path);
			}
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
		if (!_subresources_dict.has("meshes")) {
			_subresources_dict["meshes"] = Dictionary();
		}

		Dictionary mesh_Dict = _subresources_dict["meshes"];
		for (auto &E : id_to_mesh_info) {
			auto name = E.name;
			auto path = E.path;
			if (name.is_empty() || mesh_Dict.has(name)) {
				continue;
			}
			// "save_to_file/enabled": true,
			// "save_to_file/path": "res://models/Enemies/cultist-shoot-anim.res",
			mesh_Dict[name] = Dictionary();
			Dictionary subres = mesh_Dict[name];
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
				subres["lods/normal_split_angle"] = 25.0f;
			}
			subres["lods/normal_merge_angle"] = 60.0f;
			// Doesn't look like this ever made it in?
			// if (!after_4_3) {
			// 	subres["lods/raycast_normals"] = false;
			// }
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
				set_path_options(subres, "", "use_external");
			} else {
				subres["use_external/enabled"] = true;
				set_path_options(subres, path, "use_external");
				external_deps_updated.insert(path);
			}
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
				E.value["generate/physics"] = false;
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
	extra_info["image_path_to_data_hash"] = image_path_to_data_hash;
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
	auto errors_before = _get_error_count();
	Node *root = scene->instantiate();
	auto errors_after = _get_error_count();
	// this isn't an explcit error by itself, but it's context in case we experience further errors during the export
	if (errors_after > errors_before) {
		scene_instantiation_error_messages.append_array(_get_logged_error_messages());
	}
	if (root == nullptr) {
		err = ERR_CANT_ACQUIRE_RESOURCE;
		ERR_PRINT(add_errors_to_report(ERR_CANT_ACQUIRE_RESOURCE, "Failed to instantiate scene " + source_path));
		_get_logged_error_messages();
	}
	return root;
}

Error GLBExporterInstance::_load_scene_and_deps(Ref<PackedScene> &r_scene) {
	err = _load_deps();
	if (err != OK) {
		return err;
	}
	return _load_scene(r_scene);
}

Error GLBExporterInstance::_load_scene(Ref<PackedScene> &r_scene) {
	auto mode_type = ResourceCompatLoader::get_default_load_type();
	// For some reason, scenes with meshes fail to load without the load done by ResourceLoader::load, possibly due to notification shenanigans.
	if (ResourceCompatLoader::is_globally_available() && using_threaded_load()) {
		r_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_REUSE, &err);
	} else {
		r_scene = ResourceCompatLoader::custom_load(source_path, "PackedScene", mode_type, &err, using_threaded_load(), ResourceFormatLoader::CACHE_MODE_REUSE);
	}
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

void GLBExporterInstance::_do_export_instanced_scene(void *p_pair_of_root_node_and_dest_path) {
	auto pair = (Pair<Node *, String> *)p_pair_of_root_node_and_dest_path;
	Node *root = pair->first;
	String dest_path = pair->second;
	err = _export_instanced_scene(root, dest_path);
}

void GLBExporterInstance::cancel() {
	canceled = true;
}

Error GLBExporterInstance::export_file(const String &p_dest_path, const String &p_src_path, Ref<ExportReport> p_report) {
	_initial_set(p_src_path, p_report);

	if (ver_major < SceneExporter::MINIMUM_GODOT_VER_SUPPORTED) {
		return ERR_UNAVAILABLE;
	}

	String dest_ext = p_dest_path.get_extension().to_lower();
	if (dest_ext != "glb" && dest_ext != "gltf") {
		ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "Only .glb, and .gltf formats are supported for export.");
	}

	err = gdre::ensure_dir(p_dest_path.get_base_dir());
	GDRE_SCN_EXP_FAIL_COND_V_MSG(err, err, "Failed to ensure directory " + p_dest_path.get_base_dir());

	{
		Ref<PackedScene> scene;
		err = _load_scene_and_deps(scene);
		if (err != OK) {
			return err;
		}

		Node *root = _instantiate_scene(scene);
		if (!root) {
			scene = nullptr;
			_unload_deps();
			return err;
		}

		_set_stuff_from_instanced_scene(root);
		err = _export_instanced_scene(root, p_dest_path);
		if (err != OK) {
			return err;
		}

		memdelete(root);
	}
	_unload_deps();

	// _export_instanced_scene should have already set the error report
	if (err != OK) {
		return err;
	}

	// Check if the model can be loaded; minimum validation to ensure the model is valid
	err = _check_model_can_load(p_dest_path);
	GDRE_SCN_EXP_FAIL_COND_V_MSG(err, ERR_FILE_CORRUPT, "");

	if (updating_import_info) {
		_update_import_params(p_dest_path);
	}

	return _get_return_error();
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
	if (gltf_serialization_error_messages.size() > 0 && animation_deps_needed.size() > 0 && (!updating_import_info || animation_deps_updated.size() == animation_deps_needed.size())) {
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
			if (message.begins_with("WARNING:")) {
				// don't count it, but don't remove it either
				continue;
			}
			if (message.contains("glTF:")) {
				if (message.contains("Cannot export empty property. No property was specified in the NodePath:") || message.contains("animated track using path")) {
					NodePath path = message.substr(message.find("ath:") + 4).strip_edges();
					if (!updating_import_info || (!path.is_empty() && external_animation_nodepaths.has(path))) {
						// pop off the error message and the stack traces
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
	} else if (!set_all_externals && err == OK) {
		String _ = add_errors_to_report(ERR_PRINTER_ON_FIRE, "Failed to set all external dependencies in GLTF export and/or import info. This scene may not be imported correctly upon re-import.");
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
	bool using_threaded_load = !SceneExporter::can_multithread;
	int ver_major = iinfo.is_valid() ? iinfo->get_ver_major() : get_ver_major(p_src_path);
	if (ver_major < MINIMUM_GODOT_VER_SUPPORTED) {
		return ERR_UNAVAILABLE;
	}
	// For some reason, scenes with meshes fail to load without the load done by ResourceLoader::load, possibly due to notification shenanigans.
	if (ResourceCompatLoader::is_globally_available() && using_threaded_load) {
		scene = ResourceLoader::load(p_src_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_REUSE, &err);
	} else {
		scene = ResourceCompatLoader::custom_load(p_src_path, "PackedScene", ResourceCompatLoader::get_default_load_type(), &err, !using_threaded_load, ResourceFormatLoader::CACHE_MODE_REUSE);
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
	bool do_on_main_thread = false;
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

	void clear_scene() {
		if (root) {
			memdelete(root);
			root = nullptr;
		}
		_scene = nullptr;
	}

	bool check_unsupported() {
		return _check_unsupported(ver_major, is_text_output());
	}

	// scene loading and scene instancing has to be done on the main thread to avoid deadlocks and crashes
	bool batch_preload() {
		if (check_unsupported()) {
			err = ERR_UNAVAILABLE;
			_set_unsupported(report, ver_major, is_obj_output());
			preload_done = true;
			return false;
		}

		if (is_text_output()) {
			preload_done = true;
			return false;
		}
		err = _check_cancelled();
		if (err != OK) {
			report->set_error(err);
			preload_done = true;
			return false;
		}
		instance._initial_set(p_src_path, report);

		err = gdre::ensure_dir(p_dest_path.get_base_dir());
		report->set_error(err);
		ERR_FAIL_COND_V_MSG(err, false, "Failed to ensure directory " + p_dest_path.get_base_dir());
		{
			Ref<PackedScene> scene;
			err = instance._load_scene_and_deps(scene);
			if (scene.is_null() && err == OK) {
				err = ERR_CANT_ACQUIRE_RESOURCE;
			}
			if (err != OK) {
				report->set_error(err);
				preload_done = true;
				return false;
			}
			_scene = scene;
			if (!is_obj_output()) {
				root = instance._instantiate_scene(scene);
				if (!root) {
					_scene = nullptr;
					err = ERR_CANT_ACQUIRE_RESOURCE;
					report->set_error(err);
					preload_done = true;
					return false;
				}
				instance._set_stuff_from_instanced_scene(root);
			}
		}

		constexpr uint64_t MAX_MESHES_ON_WORKER_THREAD = 15;
		if (instance.is_batch_export) {
			auto meshes = get_meshes(_scene, ver_major, true);
			if (meshes.size() > MAX_MESHES_ON_WORKER_THREAD) {
				// disabling for now; can't figure out what the x factor is for why certain scenes take forever
				// do_on_main_thread = true;
			}
			surface_count = get_total_surface_count(meshes);
		}
		// print_line("Preloaded scene " + p_src_path);
		preload_done = true;
		return do_on_main_thread;
	}

	void batch_export_instanced_scene() {
		while (!preload_done && _check_cancelled() == OK) {
			OS::get_singleton()->delay_usec(10000);
		}
		if (do_on_main_thread && !Thread::is_main_thread()) {
			return;
		}
		if (err == OK) {
			err = _check_cancelled();
		}
		if (err != OK) {
			if (root) {
				memdelete(root);
				root = nullptr;
			}
			_scene = nullptr;
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
			if (root) {
				memdelete(root);
				root = nullptr;
			}
			if (err == OK) {
				report->set_saved_path(p_dest_path);
			}
		}
		_scene = nullptr;
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
			if (is_obj_output()) {
				instance._unload_deps();
			}
			if (err == OK) {
				report->set_saved_path(p_dest_path);
			}
		} else {
#ifdef DEBUG_ENABLED // export a text copy so we can see what went wrong
			if (err != OK) {
				auto new_dest = "res://.tscn_copy/" + report->get_import_info()->get_export_dest().trim_prefix("res://").get_basename() + ".tscn";
				auto new_dest_path = output_dir.path_join(new_dest.replace_first("res://", ""));
				ResourceCompatLoader::to_text(p_src_path, new_dest_path);
			}
#endif
			instance._unload_deps();
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
	Error err = TaskManager::get_singleton()->run_task(token, nullptr, "Exporting scene " + res_path, -1, true, true, true);
	token->post_export(err);
	if (remove_physics_bodies) {
		register_physics_extension();
	}
	return token->report;
}

Ref<ExportReport> SceneExporter::export_resource(const String &output_dir, Ref<ImportInfo> iinfo) {
	BatchExportToken token(output_dir, iinfo);
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

void SceneExporter::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("export_file_with_options", "out_path", "res_path", "options"), &SceneExporter::export_file_with_options);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_minimum_godot_ver_supported"), &SceneExporter::get_minimum_godot_ver_supported);
}

uint64_t GLBExporterInstance::_get_error_count() {
	return supports_multithread() ? GDRELogger::get_thread_error_count() : GDRELogger::get_error_count();
}

Vector<String> GLBExporterInstance::_get_logged_error_messages() {
	return supports_multithread() ? GDRELogger::get_thread_errors() : GDRELogger::get_errors();
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

	// Check if the model can be loaded; minimum validation to ensure the model is valid
	err = _check_model_can_load(p_dest_path);
	if (err) {
		GDRE_SCN_EXP_FAIL_V_MSG(ERR_FILE_CORRUPT, "");
	}
	if (updating_import_info) {
		_update_import_params(p_dest_path);
	}
	err = _get_return_error();

	return err;
}
// void do_batch_export_instanced_scene(int i, BatchExportToken *tokens);
void SceneExporter::do_batch_export_instanced_scene(int i, std::shared_ptr<BatchExportToken> *tokens) {
	std::shared_ptr<BatchExportToken> token = tokens[i];
	token->batch_export_instanced_scene();
}

void SceneExporter::do_single_threaded_batch_export_instanced_scene(int i, std::shared_ptr<BatchExportToken> *tokens) {
	std::shared_ptr<BatchExportToken> token = tokens[i];
	token->batch_preload();
	token->batch_export_instanced_scene();
}

String SceneExporter::get_batch_export_description(int i, std::shared_ptr<BatchExportToken> *tokens) const {
	return "Exporting scene " + tokens[i]->p_src_path;
}

struct BatchExportTokenSort {
	bool operator()(const std::shared_ptr<BatchExportToken> &a, const std::shared_ptr<BatchExportToken> &b) const {
		return a->export_end_time - a->export_start_time > b->export_end_time - b->export_start_time;
	}
};

size_t SceneExporter::get_vram_usage() {
	List<RS::MeshInfo> mesh_info;
	RS::get_singleton()->mesh_debug_usage(&mesh_info);
	List<RS::TextureInfo> tinfo;
	RS::get_singleton()->texture_debug_usage(&tinfo);

	size_t total_vmem = 0;

	for (const RS::MeshInfo &E : mesh_info) {
		total_vmem += E.vertex_buffer_size + E.attribute_buffer_size + E.skin_buffer_size + E.index_buffer_size + E.blend_shape_buffer_size + E.lod_index_buffers_size;
	}
	for (const RS::TextureInfo &E : tinfo) {
		total_vmem += E.bytes;
	}
	return total_vmem;
}

inline uint64_t get_average_delta(const Vector<uint64_t> &deltas) {
	uint64_t total_delta = 0;
	for (auto &delta : deltas) {
		total_delta += delta;
	}
	return total_delta / (deltas.size() > 0 ? deltas.size() : 1);
}

#ifdef DEBUG_ENABLED
#define perf_print(...) print_line(__VA_ARGS__)
#define perf_print_verbose(...) print_verbose(__VA_ARGS__)
#else
#define perf_print(...) print_verbose(__VA_ARGS__)
#define perf_print_verbose(...) (void)(0)
#endif

Vector<Ref<ExportReport>> SceneExporter::batch_export_files(const String &output_dir, const Vector<Ref<ImportInfo>> &scenes) {
	Vector<std::shared_ptr<BatchExportToken>> tokens;
	HashMap<String, int> export_dest_to_iinfo;
	Vector<Ref<ExportReport>> reports;
	for (auto &scene : scenes) {
		Ref<ExportReport> report = ResourceExporter::_check_for_existing_resources(scene);
		if (report.is_valid()) {
			// No extant resources to export
			report->set_exporter(get_name());
			reports.push_back(report);
			continue;
		}
		String ext = scene->get_export_dest().get_extension().to_lower();
		if (_check_unsupported(scene->get_ver_major(), ext == "escn" || ext == "tscn")) {
			// Unsupported version
			report = memnew(ExportReport(scene, get_name()));
			_set_unsupported(report, scene->get_ver_major(), ext == "obj");
			reports.push_back(report);
			continue;
		}

		tokens.push_back(std::make_shared<BatchExportToken>(output_dir, scene, Dictionary(), true));
		int idx = tokens.size() - 1;
		auto &token = tokens[idx];
		token->instance.image_path_to_data_hash = Dictionary();
		String export_dest = token->get_export_dest();
		if (export_dest_to_iinfo.has(export_dest)) {
			int other_i = export_dest_to_iinfo[export_dest];
			auto &other_token = tokens.write[other_i];
			if (other_token->original_export_dest.get_file() != other_token->get_export_dest().get_file()) {
				other_token->append_original_ext_to_export_dest();
				export_dest_to_iinfo.erase(export_dest);
			}
		}
		if (export_dest_to_iinfo.has(export_dest)) {
			if (token->original_export_dest.get_file() != token->get_export_dest().get_file()) {
				token->append_original_ext_to_export_dest();
			} else {
				token->set_export_dest(export_dest.get_basename() + "_" + itos(idx) + "." + export_dest.get_extension());
			}
		} else {
			export_dest_to_iinfo[export_dest] = idx;
		}
	}

	if (tokens.is_empty()) {
		return reports;
	}

	bool remove_physics_bodies = GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/remove_physics_bodies", false);
	if (remove_physics_bodies) {
		unregister_physics_extension();
	}

	static constexpr int64_t ONE_MB = 1024LL * 1024LL;
	static constexpr int64_t ONE_GB = 1024LL * ONE_MB;
	static constexpr int64_t EIGHT_GB = 8LL * ONE_GB;

	BatchExportToken::in_progress = 0;
	Dictionary mem_info = OS::get_singleton()->get_memory_info();
	int64_t current_memory_usage = OS::get_singleton()->get_static_memory_usage();
	// available memory does not include disk cache, so we should take the larger of this or peak memory usage (i.e. the most we've allocated)
	int64_t total_mem_available = MAX((int64_t)mem_info["available"], (int64_t)OS::get_singleton()->get_static_memory_peak_usage() - current_memory_usage);
	int64_t max_usage = TaskManager::maximum_memory_usage;
	size_t current_vram_usage = get_vram_usage();
	size_t peak_vram_usage = current_vram_usage;
	// TODO: get the real available VRAM; right now we assume 4GB
	const int64_t MAX_VRAM = EIGHT_GB / 2;
	// 75% of the default thread pool size; we can't saturate the thread pool because some loaders may make use of them and we'll cause a deadlock.
	const int64_t default_num_threads = OS::get_singleton()->get_default_thread_pool_size() * 0.75;
	// The smaller of either the above value, or the amount of memory available to us, divided by 256MB (conservative estimate of 256MB per scene)
	const int64_t number_of_threads = MIN(default_num_threads, max_usage / (ONE_GB / 4));

	print_line("\nExporting scenes...");
	perf_print(vformat("Current memory usage: %.02fMB, Total memory available: %.02fMB", (double)current_memory_usage / (double)ONE_MB, (double)total_mem_available / (double)ONE_MB));
	perf_print(vformat("VRAM usage: %.02fMB", (double)current_vram_usage / (double)ONE_MB));
	perf_print(vformat("Default thread pool size: %d", OS::get_singleton()->get_default_thread_pool_size()));
	perf_print(vformat("Number of threads to use for scene export: %d\n", (int64_t)number_of_threads));

	Vector<uint64_t> deltas;

	// auto last_print_time = OS::get_singleton()->get_ticks_usec();
	// auto last_warn_time = OS::get_singleton()->get_ticks_usec();
	auto _get_vram_usage = [&]() {
#if 0 // RS::get_singleton()->texture_debug_usage() is causing segfaults if we have the export thread pool running, so disabling for now
		auto start_tick = OS::get_singleton()->get_ticks_usec();
		current_vram_usage = SceneExporter::get_vram_usage();
		peak_vram_usage = MAX(peak_vram_usage, current_vram_usage);
		auto current_tick = OS::get_singleton()->get_ticks_usec();
		if (current_tick - last_print_time > 1000000) {
			perf_print_verbose(vformat("VRAM usage: %.02fMB", (double)current_vram_usage / (double)ONE_MB));
			last_print_time = current_tick;
		}
		auto delta = current_tick - start_tick;
		deltas.push_back(delta);
		if (delta > 10000 && current_tick - last_warn_time > 1000000) {
			perf_print("Took " + itos(delta / 1000) + "ms to get VRAM usage!!");
			last_warn_time = current_tick;
		}
		if (deltas.size() > 10000) {
			auto avg = get_average_delta(deltas);
			deltas.clear();
			deltas.resize(100);
			deltas.fill(avg);
		}
#endif
		return current_vram_usage;
	};

	Error err = OK;
	auto export_start_time = OS::get_singleton()->get_ticks_msec();
	if (number_of_threads <= 1 || GDREConfig::get_singleton()->get_setting("force_single_threaded", false)) {
		err = TaskManager::get_singleton()->run_group_task_on_current_thread(
				this,
				&SceneExporter::do_single_threaded_batch_export_instanced_scene,
				tokens.ptrw(),
				tokens.size(),
				&SceneExporter::get_batch_export_description,
				"Exporting scenes",
				"Exporting scenes",
				true);
	} else {
		auto task_id = TaskManager::get_singleton()->add_group_task(
				this,
				&SceneExporter::do_batch_export_instanced_scene,
				tokens.ptrw(),
				tokens.size(),
				&SceneExporter::get_batch_export_description,
				"Exporting scenes",
				"Exporting scenes",
				true,
				number_of_threads,
				true);

		for (int64_t i = 0; i < tokens.size(); i++) {
			auto &token = tokens[i];
			if (token->batch_preload()) {
				// we need to do the export on the main thread
				token->batch_export_instanced_scene();
			}
			// Don't load more than the current number of tasks being processed
			_get_vram_usage();
			while (BatchExportToken::in_progress >= number_of_threads ||
					(BatchExportToken::in_progress > 0 &&
							((int64_t)OS::get_singleton()->get_static_memory_usage() > max_usage ||
									current_vram_usage > MAX_VRAM))) {
				auto start_tick = OS::get_singleton()->get_ticks_usec();
				if (TaskManager::get_singleton()->update_progress_bg(true)) {
					break;
				}
				_get_vram_usage();
				auto cur_tick = OS::get_singleton()->get_ticks_usec();
				auto delta = cur_tick - start_tick;
				if (delta < 10000 && delta > 1000) {
					OS::get_singleton()->delay_usec(10000 - delta);
				}
			}
			// calling update_progress_bg serves three purposes:
			// 1) updating the progress bar
			// 2) checking if the task was cancelled
			// 3) allowing the main loop to iterate so that the command queue is flushed
			// Without flushing the command queue, GLTFDocument::append_from_scene will hang
			if (TaskManager::get_singleton()->update_progress_bg(true)) {
				break;
			}
		}
		while (!TaskManager::get_singleton()->is_current_task_completed(task_id)) {
			auto start_tick = OS::get_singleton()->get_ticks_usec();
			_get_vram_usage();
			if (TaskManager::get_singleton()->update_progress_bg(true)) {
				break;
			}
			auto cur_tick = OS::get_singleton()->get_ticks_usec();
			auto delta = cur_tick - start_tick;
			if (delta < 10000 && delta > 1000) {
				OS::get_singleton()->delay_usec(delta);
			}
		}
		err = TaskManager::get_singleton()->wait_for_task_completion(task_id);

		// get the average delta
		uint64_t average_delta = get_average_delta(deltas);
		perf_print(vformat("Average time to get VRAM usage: %.02fms", (double)average_delta / 1000.0));
		perf_print(vformat("Peak VRAM usage: %.02fMB", (double)peak_vram_usage / (double)ONE_MB));
	}
	perf_print("\n");
	auto export_end_time = OS::get_singleton()->get_ticks_msec();
	tokens.sort_custom<BatchExportTokenSort>();
#if PRINT_PERF_CSV
	perf_print("scene,time,surface_count");
	for (auto &token : tokens) {
		perf_print(vformat("%s,%.02f,%d", token->get_export_dest(), (double)(token->export_end_time - token->export_start_time) / 1000.0, (int64_t)token->surface_count));
	}
#endif
	for (auto &token : tokens) {
		token->post_export(err);
		reports.push_back(token->report);
	}

	print_line(vformat("*** Exporting %d scenes took %.02fs\n", tokens.size(), (double)(export_end_time - export_start_time) / 1000.0));
	if (remove_physics_bodies) {
		register_physics_extension();
	}
	return reports;
}
