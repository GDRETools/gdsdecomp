#include "obj_exporter.h"
#include "compat/fake_mesh.h"
#include "compat/resource_loader_compat.h"
#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/templates/hashfuncs.h"
#include "exporters/export_report.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "utility/common.h"
#include "utility/gdre_logger.h"
#include "utility/gdre_settings.h"
#include "utility/import_info.h"

#include <filesystem>

namespace {
struct Triplet {
	Vector3 v;
	Vector2 vt;
	Vector3 vn;
	Color vc;
	bool has_uv = false;
	bool has_normal = false;
	bool has_color = false;

	bool operator==(const Triplet &other) const {
		return v == other.v && (!has_uv || vt == other.vt) && (!has_normal || vn == other.vn);
	}

	bool operator<(const Triplet &other) const {
		if (v != other.v) {
			return v < other.v;
		}
		if (has_uv != other.has_uv) {
			return has_uv < other.has_uv;
		}
		if (has_uv && vt != other.vt) {
			return vt < other.vt;
		}
		if (has_normal != other.has_normal) {
			return has_normal < other.has_normal;
		}
		if (has_normal && vn != other.vn) {
			return vn < other.vn;
		}
		return false;
	}
};

struct TripletHasher {
	static uint32_t hash(const Triplet &t) {
		return HashMapHasherDefault::hash(t.v) ^ HashMapHasherDefault::hash(t.vt) ^ HashMapHasherDefault::hash(t.vn) ^ HashMapHasherDefault::hash(t.vc);
	}
};
} //namespace

namespace {

inline bool has_shadow_mesh(const Ref<Mesh> &p_mesh) {
	Ref<ArrayMesh> arr_mesh = p_mesh;
	if (arr_mesh.is_valid()) {
		return arr_mesh->get_shadow_mesh().is_valid();
	}
	Ref<FakeMesh> imp_mesh = p_mesh;
	if (imp_mesh.is_valid()) {
		return imp_mesh->get_shadow_mesh().is_valid();
	}
	return false;
}

inline Array surface_get_arrays(const Ref<Mesh> &p_mesh, int p_surf_idx) {
	return p_mesh->surface_get_arrays(p_surf_idx);
}

inline String get_surface_name(const Ref<Mesh> &p_mesh, int p_surf_idx) {
	Ref<ArrayMesh> arr_mesh = p_mesh;
	if (arr_mesh.is_valid()) {
		return arr_mesh->surface_get_name(p_surf_idx);
	}
	Ref<FakeMesh> imp_mesh = p_mesh;
	if (imp_mesh.is_valid()) {
		return imp_mesh->surface_get_name(p_surf_idx);
	}
	return String();
}

inline int get_surface_count(const Ref<Mesh> &p_mesh) {
	Ref<Mesh> arr_mesh = p_mesh;
	if (arr_mesh.is_valid()) {
		return arr_mesh->get_surface_count();
	}
	return 0;
}

inline BitField<Mesh::ArrayFormat> get_surface_format(const Ref<Mesh> &p_mesh, int p_surf_idx) {
	Ref<Mesh> arr_mesh = p_mesh;
	if (arr_mesh.is_valid()) {
		return arr_mesh->surface_get_format(p_surf_idx);
	}
	return (BitField<Mesh::ArrayFormat>)0;
}

inline bool surface_has_lods(const Ref<Mesh> &p_mesh, int p_surf_idx) {
	Ref<ArrayMesh> arr_mesh = p_mesh;
	if (arr_mesh.is_valid()) {
		return !arr_mesh->surface_get_lods(p_surf_idx).is_empty();
	}
	Ref<FakeMesh> imp_mesh = p_mesh;
	if (imp_mesh.is_valid()) {
		return !imp_mesh->surface_get_lods(p_surf_idx).is_empty();
	}
	return false;
}

inline Ref<Material> surface_get_material(const Ref<Mesh> &p_mesh, int p_surf_idx) {
	Ref<Mesh> arr_mesh = p_mesh;
	if (arr_mesh.is_valid()) {
		return arr_mesh->surface_get_material(p_surf_idx);
	}
	return Ref<Material>();
}

template <typename... VarArgs>
static String format_obj_line(bool p_force_single_precision, const String &p_text, VarArgs... p_args) {
	Variant args[sizeof...(p_args) + 1] = { p_args..., Variant() }; // +1 makes sure zero sized arrays are also supported.

	for (int i = 0; i < sizeof...(p_args); i++) {
		Variant &arg = args[i];

		if (arg.get_type() == Variant::Type::FLOAT) {
			String num_str;
			if (p_force_single_precision || arg.operator float() == arg.operator double()) {
				num_str = String::num_scientific(arg.operator float());
			} else {
				num_str = String::num_scientific(arg.operator double());
			}
			if (gdre::base10_float_string_needs_trailing_zero(num_str)) {
				num_str += ".0";
			}
			args[i] = num_str;
		}
	}
	bool error = false;
	String fmt = p_text.replace("%.6f", "%s").sprintf(Span(args, sizeof...(p_args)), &error);

	ERR_FAIL_COND_V_MSG(error, String(), String("Formatting error in string \"") + p_text + "\": " + fmt + ".");

	return fmt;
}

} //namespace

Error ObjExporter::_write_meshes_to_obj(const Vector<Ref<Mesh>> &p_meshes, const String &p_path, const String &p_output_dir, MeshInfo &r_mesh_info) {
	ERR_FAIL_COND_V_MSG(p_meshes.is_empty(), ERR_INVALID_PARAMETER, "No meshes to export");
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot open file for writing: " + p_path);

	// Write header
	f->store_line("# Exported from Godot Engine");

	// Global list of unique triplets and mapping
	HashMap<String, Ref<Material>> materials;

	// For each surface, store the sequence of triplet indices for each face
	bool wrote_mtl_line = false;
	String mtl_path = p_path.get_basename() + ".mtl";

	bool force_single_precision = true;
	if (GDRESettings::get_singleton()->is_pack_loaded()) {
		force_single_precision = !GDRESettings::get_singleton()->requires_double_precision();
	} else {
		auto res_info = ResourceInfo::get_info_from_resource(p_meshes[0]);
		force_single_precision = !(res_info.is_valid() && res_info->using_real_t_double);
	}

	// Build global triplet list and face indices
	const Color default_color = Color(1.0, 1.0, 1.0);
	for (auto p_mesh : p_meshes) {
		ERR_CONTINUE_MSG(p_mesh.is_null(), "Mesh is null");
		Vector<Triplet> global_triplets;
		HashMap<Triplet, int, TripletHasher> triplet_to_index;
		Vector<String> surface_material_names;
		Vector<Vector<int>> surface_face_triplet_indices;
		Vector<String> groups;
		r_mesh_info.has_shadow_meshes = r_mesh_info.has_shadow_meshes || has_shadow_mesh(p_mesh);
		auto surface_count = get_surface_count(p_mesh);
		for (int surf_idx = 0; surf_idx < surface_count; surf_idx++) {
			Array arrays = surface_get_arrays(p_mesh, surf_idx);
			Vector<Vector3> surface_vertices = arrays[Mesh::ARRAY_VERTEX];
			Vector<Vector2> surface_uvs = arrays[Mesh::ARRAY_TEX_UV];
			Vector<Vector3> surface_normals = arrays[Mesh::ARRAY_NORMAL];
			Vector<Color> surface_colors = arrays[Mesh::ARRAY_COLOR];
			Vector<int> indices = arrays[Mesh::ARRAY_INDEX];
			auto format = get_surface_format(p_mesh, surf_idx);
			r_mesh_info.has_tangents = r_mesh_info.has_tangents || ((format & Mesh::ARRAY_FORMAT_TANGENT) != 0);
			r_mesh_info.has_lods = r_mesh_info.has_lods || surface_has_lods(p_mesh, surf_idx);
			r_mesh_info.has_lightmap_uv2 = r_mesh_info.has_lightmap_uv2 || ((format & Mesh::ARRAY_FORMAT_TEX_UV2) != 0);
			r_mesh_info.compression_enabled = r_mesh_info.compression_enabled || ((format & Mesh::ARRAY_FLAG_COMPRESS_ATTRIBUTES) != 0);

			// TODO: This?
			// r_mesh_info.lightmap_uv2_texel_size = p_mesh->surface_get_lightmap_uv2_texel_size(surf_idx);

			bool has_uv = !surface_uvs.is_empty();
			bool has_normal = !surface_normals.is_empty();
			bool has_vertex_colors = !surface_colors.is_empty();

			// Material
			Ref<Material> mat = surface_get_material(p_mesh, surf_idx);
			String mat_name;
			String surface_name = get_surface_name(p_mesh, surf_idx);
			if (mat.is_valid()) {
				mat_name = mat->get_name();
				if (mat_name.is_empty()) {
					if (!surface_name.is_empty()) {
						mat_name = surface_name;
					} else {
						mat_name = vformat("Material.%03d", materials.size() + 1);
					}
				}
				materials[mat_name] = mat;
			}
			surface_material_names.push_back(mat_name);

			Vector<int> face_triplet_indices;
			auto make_triplet = [&](int vi) {
				Triplet t;
				t.v = surface_vertices[vi];
				if (has_uv) {
					t.vt = surface_uvs[vi];
					t.has_uv = true;
				}
				if (has_normal) {
					t.vn = surface_normals[vi];
					t.has_normal = true;
				}
				if (has_vertex_colors && surface_colors[vi] != default_color) {
					t.vc = surface_colors[vi];
					t.has_color = true;
				}
				if (!triplet_to_index.has(t)) {
					triplet_to_index[t] = global_triplets.size();
					global_triplets.push_back(t);
				}
				face_triplet_indices.push_back(triplet_to_index[t]);
			};
			if (!indices.is_empty()) {
				for (int i = 0; i < indices.size(); i++) {
					make_triplet(indices[i]);
				}
			} else {
				for (int vi = 0; vi < surface_vertices.size(); vi++) {
					make_triplet(vi);
				}
			}
			surface_face_triplet_indices.push_back(face_triplet_indices);
		}

		if (!materials.is_empty() && !wrote_mtl_line) {
			f->store_line("mtllib " + mtl_path.get_file());
			wrote_mtl_line = true;
		}
		String mesh_name = p_mesh->get_name();
		if (mesh_name.is_empty()) {
			mesh_name = p_path.get_file().get_basename();
		}
		f->store_line("o " + mesh_name);

		// Write v, vt, vn in triplet order
		bool any_uv = false;
		bool any_normal = false;
		for (int i = 0; i < global_triplets.size(); i++) {
			const Triplet &t = global_triplets[i];
			const Vector3 &v = t.v;
			String v_line = format_obj_line(force_single_precision, "v %.6f %.6f %.6f", v.x, v.y, v.z);
			if (t.has_color) {
				v_line += format_obj_line(force_single_precision, " %.6f %.6f %.6f", t.vc.r, t.vc.g, t.vc.b);
				if (t.vc.a != 1.0f) {
					v_line += format_obj_line(force_single_precision, " %.6f", t.vc.a);
				}
			}
			f->store_line(v_line);
			if (t.has_uv) {
				const Vector2 &uv = t.vt;
				f->store_line(format_obj_line(force_single_precision, "vt %.6f %.6f", uv.x, 1.0 - uv.y));
				any_uv = true;
			}
			if (t.has_normal) {
				const Vector3 &n = t.vn;
				f->store_line(format_obj_line(force_single_precision, "vn %.6f %.6f %.6f", n.x, n.y, n.z));
				any_normal = true;
			}
		}

		// Write faces per surface
		for (int surf_idx = 0; surf_idx < surface_face_triplet_indices.size(); surf_idx++) {
			const Vector<int> &face_triplet_indices = surface_face_triplet_indices[surf_idx];
			const String &mat_name = surface_material_names[surf_idx];
			if (!mat_name.is_empty()) {
				f->store_line("usemtl " + mat_name);
			}
			// Write faces (assume triangles)
			for (int i = 0; i < face_triplet_indices.size(); i += 3) {
				String face_line = "f";
				for (int k = 2; k >= 0; k--) {
					ERR_CONTINUE_MSG(i + k >= face_triplet_indices.size(), "Face triplet index out of bounds in mesh " + p_path.get_file());
					int idx = face_triplet_indices[i + k] + 1; // OBJ indices start at 1
					face_line += " ";
					face_line += itos(idx);
					if (any_uv || any_normal) {
						face_line += "/";
						if (any_uv) {
							face_line += itos(idx);
						}
						if (any_normal) {
							face_line += "/" + itos(idx);
						}
					}
				}
				f->store_line(face_line);
			}
		}
	}
	if (!materials.is_empty()) {
		err = write_materials_to_mtl(materials, mtl_path, p_output_dir, force_single_precision);
		if (err != OK) {
			return err;
		}
	}

	return OK;
}

static String get_relative_path(const String &p_path, const String &p_base_path) {
	std::filesystem::path path = p_path.utf8().get_data();
	std::filesystem::path base_path = p_base_path.utf8().get_data();
	std::filesystem::path relative_path = std::filesystem::relative(path, base_path);
	return String::utf8(relative_path.string().c_str()).simplify_path();
}

Error ObjExporter::write_materials_to_mtl(const HashMap<String, Ref<Material>> &p_materials, const String &p_path, const String &p_output_dir, bool force_single_precision) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot open MTL file for writing: " + p_path);

	f->store_line("# Exported from Godot Engine");
	auto base_dir = p_path.get_base_dir();
	auto filebasename = p_path.get_file().get_basename();
	String relative_dir = get_relative_path(base_dir, p_output_dir);

	auto check_and_save_texture = [&](Ref<Texture2D> tex, const String &suffix) {
		if (tex.is_valid()) {
			String path = tex->get_path();
			bool save_texture = false;
			// if no output_dir or no path, we need to save it to disk
			if (p_output_dir.is_empty() || path.is_empty() || !path.begins_with("res://")) {
				// we need to save it to disk
				if (!relative_dir.is_empty() && !path.is_empty()) {
					path = "res://" + relative_dir.path_join(path.get_file());
					if (FileAccess::exists(path)) {
						save_texture = false;
					} else {
						save_texture = true;
					}
				} else {
					save_texture = true;
				}
			} else {
				// if we have an output_dir, check if the path is relative to the output_dir
				String local_dir = p_output_dir.path_join(path.trim_prefix("res://"));
				String relative_path = get_relative_path(local_dir, base_dir);
				if (relative_path.is_empty()) {
					save_texture = true;
				} else {
					path = relative_path;
				}
			}
			if (save_texture) {
				String name = tex->get_name();
				if (name.is_empty()) {
					name = filebasename + "_" + suffix + ".png";
				}
				gdre::ensure_dir(base_dir);
				Ref<Image> img = tex->get_image();
				ERR_FAIL_COND_V_MSG(img.is_null(), String(), "Image is null for texture " + tex->get_path());
				img->save_png(base_dir.path_join(name));
				path = name;
			}
			return path;
		}
		return String();
	};

	for (const KeyValue<String, Ref<Material>> &E : p_materials) {
		const String &name = E.key;
		Ref<StandardMaterial3D> mat = E.value;

		if (mat.is_valid()) {
			f->store_line("\nnewmtl " + name);

			Color albedo = mat->get_albedo();
			// if (mat->get_flag(StandardMaterial3D::FLAG_SRGB_VERTEX_COLOR)) {
			// 	albedo = albedo.linear_to_srgb();
			// }
			f->store_line(format_obj_line(force_single_precision, "Kd %.6f %.6f %.6f", albedo.r, albedo.g, albedo.b));

			float metallic = mat->get_metallic();
			f->store_line(format_obj_line(force_single_precision, "Ks %.6f %.6f %.6f", metallic, metallic, metallic));

			// float roughness = mat->get_roughness();
			// f->store_line(format_obj_line(force_single_precision, "Ns %.6f", (1.0 - roughness) * 1000.0));

			float alpha = mat->get_albedo().a;
			if (alpha < 1.0) {
				f->store_line(format_obj_line(force_single_precision, "d %.6f", alpha));
			}

			//sharpness
			float sharpness = 1.0 - mat->get_roughness();
			if (sharpness != 0.0) {
				f->store_line(format_obj_line(force_single_precision, "sharpness %d", (int)(sharpness * 1000)));
			}

			// Handle textures if present
			Ref<Texture2D> tex = mat->get_texture(StandardMaterial3D::TEXTURE_ALBEDO);
			String path = check_and_save_texture(tex, "albedo");
			if (!path.is_empty()) {
				f->store_line("map_Kd " + path);
			}
			Ref<Texture2D> met_tex = mat->get_texture(StandardMaterial3D::TEXTURE_METALLIC);
			path = check_and_save_texture(met_tex, "metallic");
			if (!path.is_empty()) {
				f->store_line("map_Ks " + path);
			}
			Ref<Texture2D> rough_tex = mat->get_texture(StandardMaterial3D::TEXTURE_ROUGHNESS);
			path = check_and_save_texture(rough_tex, "roughness");
			if (!path.is_empty()) {
				f->store_line("map_Ns " + path);
			}
			Ref<Texture2D> norm_tex = mat->get_texture(StandardMaterial3D::TEXTURE_NORMAL);
			path = check_and_save_texture(norm_tex, "normal");
			if (!path.is_empty()) {
				f->store_line("map_bump " + path);
			}
		}
	}

	return OK;
}

Error ObjExporter::write_meshes_to_obj(const Vector<Ref<Mesh>> &p_meshes, const String &p_path) {
	MeshInfo mesh_info;
	return _write_meshes_to_obj(p_meshes, p_path, "", mesh_info);
}

Ref<Resource> real_load(const String &p_path, Error *r_error) {
	if (!ResourceCompatLoader::is_globally_available() || ResourceCompatLoader::get_default_load_type() != ResourceInfo::LoadType::GLTF_LOAD) {
		return ResourceCompatLoader::custom_load(p_path, "", ResourceInfo::LoadType::GLTF_LOAD, r_error, false, ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
	} else {
		return ResourceCompatLoader::load_with_real_resource_loader(p_path, "", r_error, false, ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
	}
}

Vector<Ref<Mesh>> ObjExporter::load_meshes_as_fake_meshes(const HashSet<String> &p_paths) {
	Vector<Ref<Mesh>> meshes;
	for (auto &path : p_paths) {
		Error err = OK;
		Ref<Mesh> res;
		if (ResourceCompatLoader::get_resource_type(path) == "ArrayMesh") {
			res = FakeMesh::load_from_array_mesh(path);
		} else {
			res = real_load(path, &err);
		}
		ERR_FAIL_COND_V_MSG(err != OK || res.is_null(), Vector<Ref<Mesh>>(), "Failed to load mesh: " + path);
		meshes.push_back(res);
	}
	return meshes;
}

Vector<Ref<Mesh>> ObjExporter::load_meshes(const HashSet<String> &p_paths, int ver_major, bool p_for_scene_export) {
	Vector<Ref<Mesh>> meshes;
	bool use_fake_meshes = !p_for_scene_export || ver_major <= 3;
	if (use_fake_meshes) {
		meshes = load_meshes_as_fake_meshes(p_paths);
	} else {
		Error err = OK;
		for (auto &path : p_paths) {
			Ref<Mesh> mesh = real_load(path, &err);
			if (mesh.is_null()) {
				ERR_FAIL_V_MSG(Vector<Ref<Mesh>>(), "Not a valid mesh resource: " + path);
			}
			meshes.push_back(mesh);
		}
	}
	if (use_fake_meshes && p_for_scene_export) {
		for (int i = 0; i < meshes.size(); i++) {
			meshes.write[i] = FakeMesh::mesh_to_array_mesh(meshes[i]);
			if (meshes[i].is_null()) {
				ERR_FAIL_V_MSG(Vector<Ref<Mesh>>(), "Failed to convert mesh to array mesh: " + *p_paths.begin());
			}
		}
	}
	return meshes;
}

Error ObjExporter::export_file(const String &p_out_path, const String &p_source_path) {
	return ObjExporter::export_file_with_options(p_out_path, p_source_path, {});
}

Error ObjExporter::export_file_with_options(const String &p_out_path, const String &p_source_path, const Dictionary &p_options) {
	ERR_FAIL_COND_V_MSG(!p_out_path.has_extension("obj"), ERR_INVALID_PARAMETER, "Output path must have .obj extension");
	Vector<Ref<Mesh>> meshes = load_meshes({ p_source_path }, 0, false);
	ERR_FAIL_COND_V_MSG(meshes.is_empty(), ERR_FILE_UNRECOGNIZED, "Error loading mesh: " + p_source_path);
	return write_meshes_to_obj(meshes, p_out_path);
}

void ObjExporter::get_handled_types(List<String> *r_types) const {
	r_types->push_back("ArrayMesh");
	r_types->push_back("Mesh");
}

void ObjExporter::get_handled_importers(List<String> *r_importers) const {
	r_importers->push_back("wavefront_obj");
	r_importers->push_back("mesh");
}

void ObjExporter::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("export_file_with_options", "out_path", "source_path", "options"), &ObjExporter::export_file_with_options);
}

void ObjExporter::rewrite_import_params(Ref<ImportInfo> p_import_info, const MeshInfo &p_mesh_info) {
	auto ver_major = p_import_info->get_ver_major();
	auto ver_minor = p_import_info->get_ver_minor();
	if (ver_major == 4) {
		// 4.x removes the import params, so we need to rewrite all of them
		p_import_info->set_param("generate_tangents", p_mesh_info.has_tangents);
		if (ver_minor >= 4) {
			p_import_info->set_param("generate_lods", p_mesh_info.has_lods);
			p_import_info->set_param("generate_shadow_mesh", p_mesh_info.has_shadow_meshes);
			p_import_info->set_param("generate_lightmap_uv2", p_mesh_info.has_lightmap_uv2);
			p_import_info->set_param("generate_lightmap_uv2_texel_size", p_mesh_info.lightmap_uv2_texel_size);
		}
		p_import_info->set_param("scale_mesh", p_mesh_info.scale_mesh);
		p_import_info->set_param("offset_mesh", p_mesh_info.offset_mesh);
		if (ver_minor < 4) {
			// Does literally nothing (which is why it was removed), but it's true by default
			p_import_info->set_param("optimize_mesh", true);
		}
		if (ver_minor >= 2) {
			p_import_info->set_param("force_disable_mesh_compression", !p_mesh_info.compression_enabled);
		}
	} else if (ver_major == 3) {
		// reset the params for scale_mesh and offset_mesh to the defaults to prevent this from getting scaled and offset again upon re-import
		if (ver_minor >= 1) {
			p_import_info->set_param("scale_mesh", p_mesh_info.scale_mesh);
		}
		if (ver_minor >= 2) {
			p_import_info->set_param("offset_mesh", p_mesh_info.offset_mesh);
		}
	}
	// 2.x doesn't require this
}

Ref<ExportReport> ObjExporter::export_resource(const String &p_output_dir, Ref<ImportInfo> p_import_info) {
	// TODO: This may fail on Godot 2.x objs due to "blend_shapes" property being named "morph_targets" in 2.x,
	// but I can't find any to test...
	String src_path = p_import_info->get_path();
	String dst_path = p_output_dir.path_join(p_import_info->get_export_dest().replace("res://", ""));

	// Create the export report
	Ref<ExportReport> report = memnew(ExportReport(p_import_info, get_name()));
	report->set_resources_used({ p_import_info->get_path() });

	// TODO: Godot 2.x obj export isn't working at all right now, shapes are broken, even though 2.x mesh loading is ostensibly supported by ArrayMesh.
	if (p_import_info->get_ver_major() <= 2) {
		report->set_error(ERR_UNAVAILABLE);
		report->set_unsupported_format_type("2.x meshes");
		report->set_message("Godot 2.x obj export is not supported yet");
		return report;
	}

	// Load the mesh
	Error err;
	Vector<Ref<Mesh>> meshes;
	// deduplicate
	auto dest_files = gdre::vector_to_hashset(p_import_info->get_dest_files());
	// always force this if we're using multithreading, or if we're <= 3.x
	// if we support multithreading, loading real ArrayMesh objects on the task thread will often cause segfaults
	// if we're recovering a mesh from 3.x or lower, they supported meshes with > 256 surfaces, loading those will fail
	meshes = load_meshes(dest_files, p_import_info->get_ver_major(), false);
	if (meshes.is_empty()) {
#if DEBUG_ENABLED
		// save it to a text resource so that we can see the resource errors
		for (auto &path : dest_files) {
			ResourceCompatLoader::to_text(path, p_output_dir.path_join((path.get_basename() + ".tres").trim_prefix("res://")), 0);
		}
#endif
		report->set_error(ERR_INVALID_DATA);
		report->set_message("Errors loading mesh resources");
		return report;
	}

	// Ensure the output directory exists
	err = gdre::ensure_dir(dst_path.get_base_dir());
	if (err != OK) {
		report->set_error(err);
		report->set_message("Failed to create output directory for: " + dst_path);
		return report;
	}

	// Export the mesh
	MeshInfo mesh_info;
	err = _write_meshes_to_obj(meshes, dst_path, p_output_dir, mesh_info);
	// If we generated an MTL file, add it to the report
	String mtl_path = dst_path.get_basename() + ".mtl";
	if (FileAccess::exists(mtl_path)) {
		Dictionary extra_info;
		extra_info["mtl_path"] = mtl_path;
		report->set_extra_info(extra_info);
	}
	meshes.clear();

	if (err != OK) {
		report->set_error(err);
		report->set_message("Failed to export mesh: " + src_path);
		return report;
	}

	rewrite_import_params(p_import_info, mesh_info);

	// Set success information
	report->set_saved_path(dst_path);
	print_verbose("Converted " + src_path + " to " + dst_path);

	return report;
}

bool ObjExporter::supports_multithread() const {
	// Note: For some reason the dummy render server is deadlocking when calling mesh->surface_get_arrays from multiple threads simultaneously.
	// Also getting crashes on forward_plus.
	// So, if we're using multithreading, we use the fake mesh loader to load meshes without loading them into the render server.
	return true;
}

String ObjExporter::get_name() const {
	return EXPORTER_NAME;
}

String ObjExporter::get_default_export_extension(const String &res_path) const {
	return "obj";
}

Vector<String> ObjExporter::get_export_extensions(const String &res_path) const {
	return { "obj" };
}
