#ifndef OBJ_EXPORTER_H
#define OBJ_EXPORTER_H

#include "resource_exporter.h"
#include "scene/resources/material.h"

class ObjExporter : public ResourceExporter {
	GDCLASS(ObjExporter, ResourceExporter);

private:
	static Error write_materials_to_mtl(const HashMap<String, Ref<Material>> &p_materials, const String &p_path, const String &p_output_dir);

protected:
	static void _bind_methods();

public:
	struct MeshInfo {
		bool has_tangents = false;
		bool has_lods = false;
		bool has_shadow_meshes = false;
		bool has_lightmap_uv2 = false;
		float lightmap_uv2_texel_size = 0.2;
		int bake_mode = 1;
		bool compression_enabled = false;
		Vector3 scale_mesh = Vector3(1, 1, 1); // these are constant
		Vector3 offset_mesh = Vector3(0, 0, 0);
		String name;
		String path;
	};
	virtual String get_name() const override;
	static Error _write_meshes_to_obj(const Vector<Ref<Mesh>> &p_meshes, const String &p_path, const String &p_output_dir, MeshInfo &r_mesh_info);
	static Error write_meshes_to_obj(const Vector<Ref<Mesh>> &p_meshes, const String &p_path);
	static void rewrite_import_params(Ref<ImportInfo> p_import_info, const MeshInfo &p_mesh_info);
	virtual Error export_file(const String &p_out_path, const String &p_source_path) override;
	virtual void get_handled_types(List<String> *r_types) const override;
	virtual void get_handled_importers(List<String> *r_importers) const override;
	virtual Ref<ExportReport> export_resource(const String &p_output_dir, Ref<ImportInfo> p_import_info) override;
	virtual bool supports_multithread() const override;
	virtual String get_default_export_extension(const String &res_path) const override;
};

#endif // OBJ_EXPORTER_H
