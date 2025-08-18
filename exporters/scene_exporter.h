#pragma once
#include "exporters/obj_exporter.h"
#include "exporters/resource_exporter.h"
#include "scene/resources/compressed_texture.h"

struct dep_info;
struct BatchExportToken;

class SceneExporter : public ResourceExporter {
	GDCLASS(SceneExporter, ResourceExporter);
	friend struct BatchExportToken;

	static Error export_file_to_obj(const String &res_path, const String &dest_path, Ref<ImportInfo> iinfo);
	static Error export_scene_to_obj(const Ref<PackedScene> &scene, const String &dest_path, Ref<ImportInfo> iinfo, int ver_major);

	static SceneExporter *singleton;

	void do_batch_export_instanced_scene(int i, std::shared_ptr<BatchExportToken> *tokens);
	void do_single_threaded_batch_export_instanced_scene(int i, std::shared_ptr<BatchExportToken> *tokens);

	String get_batch_export_description(int i, std::shared_ptr<BatchExportToken> *tokens) const;

protected:
	static void _bind_methods();

public:
#if ENABLE_3_X_SCENE_LOADING
	static constexpr int MINIMUM_GODOT_VER_SUPPORTED = 3;
#else
	static constexpr int MINIMUM_GODOT_VER_SUPPORTED = 4;
#endif
	static constexpr const char *const EXPORTER_NAME = "PackedScene";

	static Error export_file_to_non_glb(const String &p_src_path, const String &p_dest_path, Ref<ImportInfo> iinfo);
	static constexpr bool can_multithread = false;

	static SceneExporter *get_singleton();
	SceneExporter();
	~SceneExporter();

	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
	virtual bool supports_multithread() const override { return can_multithread; }
	virtual String get_name() const override;
	virtual bool supports_nonpack_export() const override { return false; }
	virtual String get_default_export_extension(const String &res_path) const override;

	static Ref<ExportReport> export_file_with_options(const String &out_path, const String &res_path, const Dictionary &options);
	Vector<Ref<ExportReport>> batch_export_files(const String &output_dir, const Vector<Ref<ImportInfo>> &scenes);

	static constexpr int get_minimum_godot_ver_supported() {
		return MINIMUM_GODOT_VER_SUPPORTED;
	}
};

class GLBExporterInstance {
	friend struct BatchExportToken;
	friend class SceneExporter;

	bool is_batch_export = false;
	bool force_no_update_import_params = false;
	// options, set during constructor
	bool project_recovery = false;
	bool replace_shader_materials = false;
	bool force_lossless_images = false;
	bool force_export_multi_root = false;
	bool force_require_KHR_node_visibility = false;
	bool use_double_precision = false;
	String output_dir;

	bool exporting_in_thread = false;
	bool canceled = false;

	// set during _initial_set
	int ver_major = 0;
	int ver_minor = 0;
	bool after_4_1 = false;
	bool after_4_3 = false;
	bool after_4_4 = false;
	Error err = OK;
	bool updating_import_info = false;
	Ref<ExportReport> report;
	String source_path;
	Ref<ImportInfo> iinfo;
	Ref<ResourceInfo> res_info;

	// set during _load_deps
	bool has_script = false;
	bool has_shader = false;
	List<String> get_deps;
	HashMap<String, dep_info> get_deps_map;
	HashSet<String> script_or_shader_deps;
	HashSet<String> need_to_be_updated;
	HashSet<String> animation_deps_needed;
	HashSet<String> image_deps_needed;
	String export_image_format;
	Vector<String> image_extensions;
	Vector<Ref<Resource>> loaded_deps;
	Vector<uint64_t> loaded_dep_uids;

	// set during _export_instanced_scene
	bool had_images = false;
	Vector<CompressedTexture2D::DataFormat> image_formats;
	Dictionary image_path_to_data_hash;
	Vector<ObjExporter::MeshInfo> id_to_mesh_info;
	Vector<Pair<String, String>> id_to_material_path;

	// set during _set_stuff_from_instanced_scene
	HashMap<String, Dictionary> animation_options; // used by update_import_params
	bool has_reset_track = false;
	bool has_skinned_meshes = false;
	bool has_non_skeleton_transforms = false;
	bool has_physics_nodes = false;
	String root_type;
	String root_name;
	bool has_lossy_images = false;
	HashSet<NodePath> external_animation_nodepaths = { NodePath("AnimationPlayer") };

	// set during update_import_params
	HashSet<String> external_deps_updated;
	HashSet<String> animation_deps_updated;

	// error tracking
	String error_statement;
	Vector<String> scene_instantiation_error_messages;
	Vector<String> gltf_serialization_error_messages;
	Vector<String> import_param_error_messages;
	Vector<String> dependency_resolution_list;
	Vector<String> other_error_messages;

	constexpr static const char *const COPYRIGHT_STRING_FORMAT = "The Creators of '%s'";

	ObjExporter::MeshInfo _get_mesh_options_for_import_params();

	static String get_resource_path(const Ref<Resource> &res);
	static String get_name_res(const Dictionary &dict, const Ref<Resource> &res, int64_t idx);
	static String get_path_res(const Ref<Resource> &res);
	static int get_ver_major(const String &res_path);

	String add_errors_to_report(Error p_err, const String &err_msg = "");
	void set_cache_res(const dep_info &info, const Ref<Resource> &texture, bool force_replace);

	void insert_image_map(String &name, int i);
	void get_default_mesh_opt(bool global_opt, bool local_opt);

	void _set_stuff_from_instanced_scene(Node *root);
	Error _export_instanced_scene(Node *root, const String &p_dest_path);
	void _update_import_params(const String &p_dest_path);
	Error _check_model_can_load(const String &p_dest_path);
	Error _load_deps();
	Error _load_scene_and_deps(Ref<PackedScene> &r_scene);
	void _unload_deps();
	Error _get_return_error();
	Node *_instantiate_scene(Ref<PackedScene> scene);

	void set_path_options(Dictionary &import_opts, const String &path, const String &prefix = "save_to_file");
	String get_path_options(const Dictionary &import_opts);
	void _initial_set(const String &p_src_path, Ref<ExportReport> p_report);

	uint64_t _get_error_count();
	Vector<String> _get_logged_error_messages();

	bool _is_logger_silencing_errors() const;
	void _silence_errors(bool p_silence);

public:
	void _do_export_instanced_scene(void *p_pair_of_root_node_and_dest_path);

	GLBExporterInstance(String p_output_dir, Dictionary curr_options = {}, bool p_project_recovery = false);

	bool had_script() const { return has_script; }

	void cancel();

	bool using_threaded_load() const;
	bool supports_multithread() const;
	Error export_file(const String &out_path, const String &res_path, Ref<ExportReport> p_report);

	Error _batch_export_instanced_scene(Node *root, const String &p_dest_path);

	void set_batch_export(bool p_batch_export) { is_batch_export = p_batch_export; }

	void set_force_no_update_import_params(bool p_force_no_update_import_params) { force_no_update_import_params = p_force_no_update_import_params; }

	void set_options(const Dictionary &p_options);
};
