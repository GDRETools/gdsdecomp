#include "scene_exporter.h"

#include "compat/resource_loader_compat.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "exporters/export_report.h"
#include "exporters/obj_exporter.h"
#include "external/tinygltf/tiny_gltf.h"
#include "modules/gltf/gltf_document.h"
#include "modules/gltf/structures/gltf_node.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/texture.h"
#include "utility/common.h"
#include "utility/gdre_config.h"
#include "utility/gdre_logger.h"
#include "utility/gdre_settings.h"

#include "core/crypto/crypto_core.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/json.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/packed_scene.h"
struct dep_info {
	ResourceUID::ID uid = ResourceUID::INVALID_ID;
	String dep;
	String remap;
	String orig_remap;
	String type;
	bool exists = true;
	bool uid_in_uid_cache = false;
	bool uid_in_uid_cache_matches_dep = true;
	bool uid_remap_path_exists = true;
};
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
				String uid_text = splits[0];
				info.uid = splits[0].is_empty() ? ResourceUID::INVALID_ID : ResourceUID::get_singleton()->text_to_id(uid_text);
				info.type = splits[1];
				info.dep = splits[2];
				info.uid_in_uid_cache = info.uid != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->has_id(info.uid);
				auto uid_path = info.uid_in_uid_cache ? ResourceUID::get_singleton()->get_id_path(info.uid) : "";
				info.orig_remap = GDRESettings::get_singleton()->get_mapped_path(info.dep);
				if (info.uid_in_uid_cache && uid_path != info.dep) {
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

Error _serialize_file(Ref<GLTFState> p_state, const String p_path, bool p_force_single_precision) {
	Error err = FAILED;
	if (p_path.to_lower().ends_with("glb")) {
		err = _encode_buffer_glb(p_state, p_path);
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
		err = _encode_buffer_bins(p_state, p_path);
		ERR_FAIL_COND_V(err != OK, err);
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V(file.is_null(), FAILED);

		file->create(FileAccess::ACCESS_RESOURCES);
		String json = stringify_json(Variant(p_state->get_json()), indent, true, p_force_single_precision);
		file->store_string(json);
	}
	return err;
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
	bool had_images = false;
	Vector<CompressedTexture2D::DataFormat> image_formats;
	const bool after_4_1 = (iinfo.is_null() ? false : (iinfo->get_ver_major() > 4 || (iinfo->get_ver_major() == 4 && iinfo->get_ver_minor() > 1)));
	const bool after_4_3 = (iinfo.is_null() ? false : (iinfo->get_ver_major() > 4 || (iinfo->get_ver_major() == 4 && iinfo->get_ver_minor() > 3)));
	const bool after_4_4 = (iinfo.is_null() ? false : (iinfo->get_ver_major() > 4 || (iinfo->get_ver_major() == 4 && iinfo->get_ver_minor() > 4)));

	String game_name = GDRESettings::get_singleton()->get_game_name();
	String copyright_string = vformat("The Creators of '%s'", game_name.is_empty() ? p_dest_path.get_file().get_basename() : game_name);

	bool set_all_externals = false;
	List<String> get_deps;
	// We need to preload any Texture resources that are used by the scene with our own loader
	HashMap<String, dep_info> get_deps_map;
	HashSet<String> need_to_be_updated;
	HashSet<String> external_deps_updated;
	Vector<String> error_messages;
	Vector<String> image_extensions;
	HashSet<NodePath> external_animation_nodepaths = { NodePath("AnimationPlayer") };
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

	const auto set_path_options = [after_4_4](Dictionary &options, const String &path, const String &prefix = "save_to_file") {
		if (after_4_4) {
			ResourceUID::ID uid = path.is_empty() ? ResourceUID::INVALID_ID : GDRESettings::get_singleton()->get_uid_for_path(path);
			if (uid != ResourceUID::INVALID_ID) {
				options[prefix + "/path"] = ResourceUID::get_singleton()->id_to_text(uid);
			} else {
				options[prefix + "/path"] = path;
			}
			options[prefix + "/fallback_path"] = path;
		} else {
			options[prefix + "/path"] = path;
		}
	};

	const auto get_path_options = [after_4_4](const Dictionary &options) -> String {
		if (after_4_4) {
			return options.get("save_to_file/fallback_path", options.get("save_to_file/path", ""));
		}
		return options.get("save_to_file/path", "");
	};

	bool no_threaded_load = false;
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
					String ext = info.dep.get_extension().to_upper();
					if (ext == "JPG") {
						ext = "JPEG";
					}
					image_extensions.append(ext);
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
		auto set_cache_res = [&](const dep_info &info, const Ref<Resource> &texture, bool force_replace) {
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
				if (!info.uid_in_uid_cache) {
					ResourceUID::get_singleton()->add_id(info.uid, info.remap);
					texture_uids.push_back(info.uid);
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

		Vector<String> id_to_texture_path;
		Dictionary image_path_to_data_hash;
		Vector<Pair<String, String>> id_to_material_path;
		// Vector<Pair<String, String>> id_to_meshes_path;
		Vector<ObjExporter::MeshInfo> id_to_mesh_info;
		HashMap<String, String> animation_map;
		HashMap<String, ObjExporter::MeshInfo> mesh_info_map;
		HashMap<String, Dictionary> animation_options;

		// export/import settings
		bool has_reset_track = false;
		bool has_skinned_meshes = false;
		bool has_non_skeleton_transforms = false;
		bool has_physics_nodes = false;
		String root_type;
		String root_name;
		String export_image_format = image_extensions.is_empty() ? "PNG" : gdre::get_most_popular_value(image_extensions);
		bool lossy = false;
		if (export_image_format == "WEBP") {
			// Only 3.4 and above supports lossless WebP
			if (iinfo.is_valid() && (iinfo->get_ver_major() > 3 || (iinfo->get_ver_major() == 3 && iinfo->get_ver_minor() >= 4))) {
				export_image_format = "Lossless WebP";
			} else {
				if (GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/force_lossless_images", false)) {
					export_image_format = "PNG";
				} else {
					export_image_format = "Lossy WebP";
					lossy = true;
				}
			}
			// TODO: add setting to force PNG?
		} else if (export_image_format == "JPEG") {
			if (GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/force_lossless_images", false)) {
				export_image_format = "PNG";
			} else {
				lossy = true;
			}
		} else {
			// the GLTF exporter doesn't support anything other than PNG, JPEG, and WEBP
			export_image_format = "PNG";
		}
		if (lossy && p_report.is_valid()) {
			p_report->set_loss_type(ImportInfo::STORED_LOSSY);
		}

		auto get_resource_path = [&](const Ref<Resource> &res) {
			String path = res->get_path();
			if (path.is_empty()) {
				Ref<ResourceInfo> compat = ResourceInfo::get_info_from_resource(res);
				if (compat.is_valid()) {
					path = compat->original_path;
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
			Node *root = scene->instantiate();
			TypedArray<Node> animation_player_nodes = root->find_children("*", "AnimationPlayer");
			TypedArray<Node> mesh_instances = root->find_children("*", "MeshInstance3D");
			HashSet<Node *> skinned_mesh_instances;
			for (auto &E : mesh_instances) {
				MeshInstance3D *mesh_instance = cast_to<MeshInstance3D>(E);
				ERR_CONTINUE(!mesh_instance);
				auto skin = mesh_instance->get_skin();
				if (skin.is_valid()) {
					has_skinned_meshes = true;
					skinned_mesh_instances.insert(mesh_instance);
				}
			}
			for (int32_t node_i = 0; node_i < animation_player_nodes.size(); node_i++) {
				// Force re-compute animation tracks.
				Vector<Ref<AnimationLibrary>> anim_libs;
				AnimationPlayer *player = cast_to<AnimationPlayer>(animation_player_nodes[node_i]);
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
							}
						}
						if (ver_major == 4) {
							int i = 1;
							while (animation_options.has(name)) {
								// append _001, _002, etc.
								name = vformat("%s_%03d", anim_name, i);
								i++;
							}
							animation_options[name] = Dictionary();
							auto &options = animation_options[name];
							options["settings/loop_mode"] = (int)anim->get_loop_mode();
							if (!(path.is_empty() || path.get_file().contains("::"))) {
								options["save_to_file/enabled"] = true;
								set_path_options(options, path);
								options["save_to_file/keep_custom_tracks"] = true;
								// TODO: slices??
							} else {
								options["save_to_file/enabled"] = false;
								set_path_options(options, "");
								options["save_to_file/keep_custom_tracks"] = false;
							}
							options["slices/amount"] = 0;
						}
					}
				}
			}

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
				return path.trim_prefix(scene_name + "_");
			};

			{
				List<String> deps;
				Ref<GLTFDocument> doc;
				doc.instantiate();
				Ref<GLTFState> state;
				state.instantiate();
				state->set_scene_name(scene_name);
				state->set_copyright(copyright_string);
				doc->set_image_format(export_image_format);
				doc->set_lossy_quality(1.0f);

				if (has_non_skeleton_transforms && has_skinned_meshes) {
					// WARN_PRINT("Skinned meshes have non-skeleton transforms, exporting as non-single-root.");
					doc->set_root_node_mode(GLTFDocument::RootNodeMode::ROOT_NODE_MODE_MULTI_ROOT);
					TypedArray<Node> physics_nodes = root->find_children("*", "CollisionObject3D");
					TypedArray<Node> physics_shapes = root->find_children("*", "CollisionShape3D");
					has_physics_nodes = physics_nodes.size() > 0 || physics_shapes.size() > 0;
					if (has_physics_nodes) {
						WARN_PRINT("Skinned meshes have physics nodes, but still exporting as non-single-root.");
					}
				}
				int32_t flags = 0;
				auto exts = doc->get_supported_gltf_extensions();
				flags |= 16; // EditorSceneFormatImporter::IMPORT_USE_NAMED_SKIN_BINDS;
				// root = scene->instantiate();
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

				auto get_name_res = [](const Dictionary &dict, const Ref<Resource> &res, int64_t idx) {
					String name = dict.get("name", String());
					if (name.is_empty()) {
						name = res->get_name();
						if (name.is_empty()) {
							Ref<ResourceInfo> info = ResourceInfo::get_info_from_resource(res);
							if (info.is_valid() && !info->resource_name.is_empty()) {
								name = info->resource_name;
							} else {
								name = res->get_class() + "_" + String::num_int64(idx);
							}
						}
					}
					return name;
				};

				auto get_path_res = [](const Ref<Resource> &res) {
					String path = res->get_path();
					if (path.is_empty()) {
						Ref<ResourceInfo> info = ResourceInfo::get_info_from_resource(res);
						if (info.is_valid() && !info->original_path.is_empty()) {
							path = info->original_path;
						}
					}
					return path;
				};

				{
					auto json = state->get_json();
					auto materials = state->get_materials();
					Array images = state->get_images();
					Array json_images = json.has("images") ? (Array)json["images"] : Array();
					HashMap<String, Vector<int>> image_map;
					bool has_duped_images = false;
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
						String name;
						if (path.is_empty()) {
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
					HashMap<int, int> removal_to_replacement;
					Vector<int> to_remove;
					if (has_duped_images) {
						for (auto &E : image_map) {
							auto &indices = E.value;
							if (indices.size() <= 1) {
								continue;
							}
							int replacement = indices[0];
							for (int i = 1; i < indices.size(); i++) {
								Dictionary image_dict = json_images[indices[i]];
								Ref<Texture2D> image = images[indices[i]];
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
						}
						json["textures"] = json_textures;
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
						for (int i = 0; i < gltf_meshes.size(); i++) {
							Ref<GLTFMesh> gltf_mesh = gltf_meshes[i];
							auto mesh = gltf_mesh->get_mesh();
							auto original_name = gltf_mesh->get_original_name();
							Dictionary mesh_dict = json_meshes[i];
							ObjExporter::MeshInfo mesh_info;
							if (mesh.is_null()) {
								id_to_mesh_info.push_back(mesh_info);
								continue;
							}
							String path = get_path_res(mesh);
							String name;
							bool is_internal = path.is_empty() || path.get_file().contains("::");
							if (is_internal) {
								name = get_name_res(mesh_dict, mesh, i);
							} else {
								name = path.get_file().get_basename();
							}
							if (!name.is_empty()) {
								mesh_dict["name"] = demangle_name(name);
							} else if (!original_name.is_empty()) {
								mesh_dict["name"] = original_name;
							}
							if (original_name.is_empty()) {
								gltf_mesh->set_original_name(name);
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
							if (!name.is_empty()) {
								material_dict["name"] = demangle_name(name);
							}
							id_to_material_path.push_back({ name, path });
						}
					}
					auto gltf_nodes = state->get_nodes();
					// rename animation player nodes to avoid name clashes when reimporting
					if (gltf_nodes.size() > 0) {
						Array json_nodes = json["nodes"];
						Array json_scenes = json["scenes"];
						Vector<GLTFNodeIndex> nodes_to_remove;
						for (int node_idx = gltf_nodes.size() - 1; node_idx >= 0; node_idx--) {
							Ref<GLTFNode> node = gltf_nodes[node_idx];
							Dictionary json_node = json_nodes[node_idx];
							if (node.is_valid()) {
								auto original_name = node->get_original_name();
								if (!original_name.is_empty() && original_name.contains("AnimationPlayer")) {
									if (node_idx == json_nodes.size() - 1 && (json_node.size() == 0 || (json_node.size() == 1 && json_node.has("name")))) {
										// useless node, remove it
										auto parent = node->get_parent();
										if (parent != -1) {
											Dictionary parent_node = json_nodes[parent];
											Array children = parent_node.get("children", Array());
											if (children.has(node_idx)) {
												children.erase(node_idx);
												parent_node["children"] = children;
											}
										}
										for (int j = 0; j < json_scenes.size(); j++) {
											Dictionary scene_json = json_scenes[j];
											Array scene_nodes = scene_json.get("nodes", Array());
											if (scene_nodes.has(node_idx)) {
												scene_nodes.erase(node_idx);
												scene_json["nodes"] = scene_nodes;
												json_scenes[j] = scene_json;
											}
										}
										json_nodes.remove_at(node_idx);
										nodes_to_remove.push_back(node_idx);
										continue;
									} else {
										json_node["name"] = original_name + "_ORIG_" + String::num_int64(node_idx);
									}
								}
							}
						}
						// nodes_to_remove.sort();
						// nodes_to_remove.reverse();
						json["nodes"] = json_nodes;
						json["scenes"] = json_scenes;
					}

					Dictionary gltf_asset = json["asset"];
#if DEBUG_ENABLED
					// less file churn when testing
					gltf_asset["generator"] = "GDRE Tools";
#else
					gltf_asset["generator"] = "GDRE Tools v" + GDRESettings::get_singleton()->get_gdre_version();
#endif

					json["asset"] = gltf_asset;
				}
#if DEBUG_ENABLED
				if (p_dest_path.get_extension() == "glb") {
					// save a gltf copy for debugging
					auto gltf_path = p_dest_path.get_base_dir().path_join("GLTF/" + p_dest_path.get_file().get_basename() + ".gltf");
					gdre::ensure_dir(gltf_path.get_base_dir());
					_serialize_file(state, gltf_path, !GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/use_double_precision", false));
				}
#endif
				p_err = _serialize_file(state, p_dest_path, !GDREConfig::get_singleton()->get_setting("Exporter/Scene/GLTF/use_double_precision", false));
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
		if (iinfo.is_valid() && iinfo->get_ver_major() >= 4) {
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
			global_mesh_info.has_tangents = global_has_tangents.count(true) > global_has_tangents.size() / 2;
			global_mesh_info.has_lods = global_has_lods.count(true) > global_has_lods.size() / 2;
			global_mesh_info.has_shadow_meshes = global_has_shadow_meshes.count(true) > global_has_shadow_meshes.size() / 2;
			global_mesh_info.has_lightmap_uv2 = global_has_lightmap_uv2.count(true) > global_has_lightmap_uv2.size() / 2;
			global_mesh_info.lightmap_uv2_texel_size = gdre::get_most_popular_value(global_lightmap_uv2_texel_size);
			global_mesh_info.bake_mode = gdre::get_most_popular_value(global_bake_mode);

			int image_handling_val = GLTFState::HANDLE_BINARY_EXTRACT_TEXTURES;
			if (had_images) {
				if (has_external_images) {
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
			iinfo->set_param("meshes/light_baking", global_mesh_info.has_lightmap_uv2 ? 2 : global_mesh_info.bake_mode);
			iinfo->set_param("meshes/lightmap_texel_size", global_mesh_info.lightmap_uv2_texel_size);
			iinfo->set_param("meshes/force_disable_compression", !global_mesh_info.compression_enabled);
			iinfo->set_param("skins/use_named_skins", true);
			iinfo->set_param("animation/import", true);
			iinfo->set_param("animation/fps", 30);
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
					String path = get_path_options(E.value);
					if (!(path.is_empty() || path.get_file().contains("::"))) {
						external_deps_updated.insert(path);
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
						ERR_CONTINUE(name.is_empty());
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
					subres["generate/lightmap_uv"] = get_default_mesh_opt(global_mesh_info.has_lightmap_uv2, E.has_lightmap_uv2);
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

			iinfo->set_param("_subresources", _subresources_dict);
			Dictionary extra_info;
			extra_info["image_path_to_data_hash"] = image_path_to_data_hash;
			p_report->set_extra_info(extra_info);
		}
		textures.clear();
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

			if (message.begins_with("at:") ||
					message.begins_with("GDScript backtrace") ||
					message.begins_with("WARNING:")) {
				continue;
			}
			if (message.contains("glTF:")) {
				if (message.contains("Cannot export empty property. No property was specified in the NodePath:") || message.contains("Cannot get node for animated track")) {
					NodePath path = message.substr(message.find("ath:") + 4).strip_edges();
					if (!path.is_empty() && external_animation_nodepaths.has(path)) {
						continue;
					}
				}
			}
			err = ERR_PRINTER_ON_FIRE;
			break;
		}
	}
	if (!set_all_externals) {
		error_messages.append("Failed to set all externals in import info. Some resources may not be exported.");
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
	Vector<Ref<Mesh>> meshes;
	Vector<Ref<Mesh>> meshes_to_remove;

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
	bool non_gltf = ext != "glb" && ext != "gltf";
	// GLTF export can result in inaccurate models
	// save it under .assets, which won't be picked up for import by the godot editor
	if (!to_text && !to_obj) {
		new_path = new_path.replace("res://", "res://.assets/");
		// we only export glbs
		if (non_gltf) {
			new_path = new_path.get_basename() + ".glb";
		}
	}
	iinfo->set_export_dest(new_path);
	String dest_path = output_dir.path_join(new_path.replace("res://", ""));

#if ENABLE_3_X_SCENE_LOADING
	constexpr int minimum_ver = 3;
#else
	constexpr int minimum_ver = 4;
#endif
	if (!to_text && iinfo->get_ver_major() < minimum_ver) {
		err = ERR_UNAVAILABLE;
		report->set_message("Scene export for engine version " + itos(iinfo->get_ver_major()) + " is not currently supported.");
		report->set_unsupported_format_type(itos(iinfo->get_ver_major()) + ".x PackedScene");
	} else {
		report->set_new_source_path(new_path);
		err = _export_file(dest_path, iinfo->get_path(), report);
	}
	if (err == ERR_PRINTER_ON_FIRE) {
		report->set_saved_path(dest_path);
		// save the import info anyway
		auto import_dest = output_dir.path_join(iinfo->get_import_md_path().trim_prefix("res://"));
		iinfo->save_to(import_dest);
		report->set_rewrote_metadata(ExportReport::NOT_IMPORTABLE);
	} else if (err == OK && !to_text && !to_obj && !non_gltf && iinfo->get_ver_major() >= 4) {
		// TODO: Turn this on when we feel confident that we can tell that are exporting correctly
		// TODO: do real GLTF validation
		// TODO: fix errors where some models aren't being textured?
		// move the file to the correct location
		auto new_dest = output_dir.path_join(orignal_export_dest.replace("res://", ""));
		auto da = DirAccess::create_for_path(new_dest.get_base_dir());
		if (da.is_valid() && da->rename(dest_path, new_dest) == OK) {
			iinfo->set_export_dest(orignal_export_dest);
			report->set_new_source_path(orignal_export_dest);
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

void SceneExporter::get_handled_types(List<String> *out) const {
	out->push_back("PackedScene");
}

void SceneExporter::get_handled_importers(List<String> *out) const {
	out->push_back("scene");
}

String SceneExporter::get_name() const {
	return "PackedScene";
}
