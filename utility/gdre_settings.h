#ifndef GDRE_SETTINGS_H
#define GDRE_SETTINGS_H
#include "core/object/class_db.h"
#include "gd_parallel_hashmap.h"
#include "import_info.h"
#include "packed_file_info.h"
#include "pcfg_loader.h"
#include "utility/godotver.h"

#include "core/config/project_settings.h"
#include "core/object/object.h"
#include "core/os/thread_safe.h"

class GDREPackSettings : public ProjectSettings {
	GDCLASS(GDREPackSettings, ProjectSettings);

public:
	static void do_set_resource_path(GDREPackSettings *settings, const String &p_path) {
		settings->resource_path = p_path;
	}
};

class GDRELogger;
class GDREPackedData;
class GDRESettings : public Object {
	GDCLASS(GDRESettings, Object);
	_THREAD_SAFE_CLASS_
public:
	class PackInfo : public RefCounted {
		GDCLASS(PackInfo, RefCounted);

		friend class GDRESettings;

	public:
		enum PackType {
			PCK,
			APK,
			ZIP,
			DIR,
			EXE,
			UNKNOWN
		};

	private:
		String pack_file = "";
		Ref<GodotVer> version;
		uint32_t fmt_version = 0;
		uint32_t pack_flags = 0;
		uint64_t file_base = 0;
		uint32_t file_count = 0;
		PackType type = PCK;
		Ref<ProjectConfigLoader> pcfg;
		bool encrypted = false;
		bool suspect_version = false;

	public:
		void init(
				String f, Ref<GodotVer> godot_ver, uint32_t fver, uint32_t flags, uint64_t base, uint32_t count, PackType tp, bool p_encrypted = false, bool p_suspect_version = false) {
			pack_file = f;
			// copy the version, or set it to null if it's invalid
			if (godot_ver.is_valid() && godot_ver->is_valid_semver()) {
				version = GodotVer::create(godot_ver->get_major(), godot_ver->get_minor(), godot_ver->get_patch(), godot_ver->get_prerelease(), godot_ver->get_build_metadata());
			}
			fmt_version = fver;
			pack_flags = flags;
			file_base = base;
			file_count = count;
			type = tp;
			pcfg.instantiate();
			encrypted = p_encrypted;
			suspect_version = p_suspect_version;
		}
		bool has_unknown_version() {
			return !version.is_valid() || !version->is_valid_semver();
		}
		void set_project_config() {
		}
		PackInfo() {
			version.instantiate();
			pcfg.instantiate();
		}

		String get_pack_file() const { return pack_file; }
		Ref<GodotVer> get_version() const { return GodotVer::create(version->get_major(), version->get_minor(), version->get_patch(), version->get_prerelease(), version->get_build_metadata()); }
		uint32_t get_fmt_version() const { return fmt_version; }
		uint32_t get_pack_flags() const { return pack_flags; }
		uint64_t get_file_base() const { return file_base; }
		uint32_t get_file_count() const { return file_count; }
		PackType get_type() const { return type; }
		bool is_encrypted() const { return encrypted; }
		bool has_suspect_version() const { return suspect_version; }

	protected:
		static void _bind_methods() {
			ClassDB::bind_method(D_METHOD("get_pack_file"), &PackInfo::get_pack_file);
			ClassDB::bind_method(D_METHOD("get_version"), &PackInfo::get_version);
			ClassDB::bind_method(D_METHOD("get_fmt_version"), &PackInfo::get_fmt_version);
			ClassDB::bind_method(D_METHOD("get_pack_flags"), &PackInfo::get_pack_flags);
			ClassDB::bind_method(D_METHOD("get_file_base"), &PackInfo::get_file_base);
			ClassDB::bind_method(D_METHOD("get_file_count"), &PackInfo::get_file_count);
			ClassDB::bind_method(D_METHOD("get_type"), &PackInfo::get_type);
			ClassDB::bind_method(D_METHOD("is_encrypted"), &PackInfo::is_encrypted);
			ClassDB::bind_method(D_METHOD("has_suspect_version"), &PackInfo::has_suspect_version);
			BIND_ENUM_CONSTANT(PCK);
			BIND_ENUM_CONSTANT(APK);
			BIND_ENUM_CONSTANT(ZIP);
			BIND_ENUM_CONSTANT(DIR);
			BIND_ENUM_CONSTANT(EXE);
			BIND_ENUM_CONSTANT(UNKNOWN);
		}
	};

	class ProjectInfo : public RefCounted {
		GDCLASS(ProjectInfo, RefCounted);

	public:
		Ref<GodotVer> version;
		Ref<ProjectConfigLoader> pcfg;
		HashSet<String> resource_strings; // For translation key recovery
		PackInfo::PackType type = PackInfo::PCK;
		String pack_file;
		int bytecode_revision = 0;
		bool suspect_version = false;
		ProjectInfo() {
			pcfg.instantiate();
		}
		// String project_path;
	};

private:
	Vector<Ref<PackInfo>> packs;
	Ref<ProjectInfo> current_project;
	GDREPackedData *gdre_packeddata_singleton = nullptr;
	GDRELogger *logger;
	Array import_files;
	HashMap<String, Ref<ImportInfoRemap>> remap_iinfo;
	String gdre_user_path = "";
	String gdre_resource_path = "";
	Vector<Ref<PackedFileInfo>> dir_files;

	struct UID_Cache {
		CharString cs;
		bool saved_to_cache = false;
	};
	struct IInfoToken {
		String path;
		Ref<ImportInfo> info;
		int ver_major = 0;
		int ver_minor = 0;
		Error err = OK;
	};

	struct StringLoadToken {
		String engine_version;
		String path;
		Vector<String> strings;
		Error err = OK;
	};

	void _do_import_load(uint32_t i, IInfoToken *tokens);
	String get_IInfoToken_description(uint32_t i, IInfoToken *p_userdata);
	void _do_string_load(uint32_t i, StringLoadToken *tokens);
	String get_string_load_token_description(uint32_t i, StringLoadToken *p_userdata);
	HashMap<ResourceUID::ID, UID_Cache> unique_ids; //unique IDs and utf8 paths (less memory used)
	ParallelFlatHashMap<String, ResourceUID::ID> path_to_uid;
	HashMap<String, Dictionary> script_cache;

	uint8_t old_key[32] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	bool set_key = false;
	Vector<uint8_t> enc_key;
	String enc_key_str = "";
	bool in_editor = false;
	bool first_load = true;
	bool error_encryption = false;
	String project_path = "";
	static GDRESettings *singleton;
	static String exec_dir;
	bool headless = false;
	bool download_plugins = false;
	Ref<ConfigFile> config;

	void remove_current_pack();
	String _get_res_path(const String &p_path, const String &resource_dir, const bool suppress_errors);
	void add_logger();

	static String _get_cwd();
	Error get_version_from_bin_resources();
	bool check_if_dir_is_v4();
	bool check_if_dir_is_v3();
	bool check_if_dir_is_v2();
	int get_ver_major_from_dir();
	Error load_dir(const String &p_path);
	Error unload_dir();
	bool has_valid_version() const;

	String get_loaded_pack_data_dir();
	Error load_pack_uid_cache(bool p_reset = false);
	Error reset_uid_cache();

	Error load_pack_gdscript_cache(bool p_reset = false);
	Error reset_gdscript_cache();

	Error detect_bytecode_revision(bool p_no_valid_version);

	static constexpr bool need_correct_patch(int ver_major, int ver_minor);
	void _do_prepop(uint32_t i, const String *plugins);
	String sanitize_home_in_path(const String &p_path);
	void log_sysinfo();

	static ResourceUID::ID _get_uid_for_path(const String &p_path, bool _generate = false);

protected:
	static void _bind_methods();

public:
	Error load_project(const Vector<String> &p_paths, bool cmd_line_extract = false);
	Error load_pck(const String &p_path);

	Error unload_project();
	String get_gdre_resource_path() const;
	String get_gdre_user_path() const;

	Vector<uint8_t> get_encryption_key();
	String get_encryption_key_string();
	bool is_pack_loaded() const;

	bool had_encryption_error() const;
	void _set_error_encryption(bool is_encryption_error);
	Error set_encryption_key(Vector<uint8_t> key);
	Error set_encryption_key_string(const String &key);
	void reset_encryption_key();
	void add_pack_info(Ref<PackInfo> packinfo);

	StringName get_cached_script_class(const String &p_path);
	StringName get_cached_script_base(const String &p_path);

	Vector<String> get_file_list(const Vector<String> &filters = Vector<String>());
	Array get_file_info_array(const Vector<String> &filters = Vector<String>());
	Vector<Ref<PackedFileInfo>> get_file_info_list(const Vector<String> &filters = Vector<String>());
	TypedArray<PackInfo> get_pack_info_list() const;
	PackInfo::PackType get_pack_type() const;
	String get_pack_path() const;
	String get_version_string() const;
	uint32_t get_ver_major() const;
	uint32_t get_ver_minor() const;
	uint32_t get_ver_rev() const;
	uint32_t get_file_count() const;
	void set_ver_rev(uint32_t p_rev);
	String globalize_path(const String &p_path, const String &resource_path = "") const;
	String localize_path(const String &p_path, const String &resource_path = "") const;
	void set_project_path(const String &p_path);
	String get_project_path() const;
	String get_res_path(const String &p_path, const String &resource_dir = "");
	bool has_res_path(const String &p_path, const String &resource_dir = "");
	Error open_log_file(const String &output_dir);
	String get_log_file_path();
	bool is_fs_path(const String &p_path) const;
	Error close_log_file();
	Dictionary get_remaps(bool include_imports = true) const;
	bool has_any_remaps() const;
	bool has_remap(const String &src, const String &dst) const;
	Error add_remap(const String &src, const String &dst);
	// This only gets explicit remaps, not imports
	String get_remap(const String &src) const;
	String get_mapped_path(const String &src) const;
	Error remove_remap(const String &src, const String &dst, const String &output_dir = "");
	Variant get_project_setting(const String &p_setting);
	bool has_project_setting(const String &p_setting);
	String get_project_config_path();
	String get_cwd();
	Array get_import_files(bool copy = false);
	bool has_file(const String &p_path);
	Error load_import_files();
	Error load_import_file(const String &p_path);
	Ref<ImportInfo> get_import_info_by_dest(const String &p_path) const;
	Ref<ImportInfo> get_import_info_by_source(const String &p_path);
	Vector<String> get_code_files();
	String get_exec_dir();
	// Only for testing, don't use this
	void set_exec_dir(const String &p_cwd);
	bool are_imports_loaded() const;
	bool is_project_config_loaded() const;
	bool is_headless() const;
	String get_sys_info_string() const;
	Error load_project_config();
	Error save_project_config(const String &p_out_dir);
	bool pack_has_project_config();
	float get_auto_display_scale() const;
	String get_gdre_version() const;
	String get_disclaimer_text() const;
	static String get_disclaimer_body();
	bool loaded_resource_strings() const;
	void load_all_resource_strings();
	void get_resource_strings(HashSet<String> &r_strings) const;
	int get_bytecode_revision() const;
	void prepop_plugin_cache(const Vector<String> &plugins);
	void load_config();
	void save_config();
	void set_setting(const String &p_setting, const Variant &p_value);
	bool has_setting(const String &string) const;
	static String get_section_from_key(const String &p_setting);
	Variant get_setting(const String &p_setting, const Variant &p_default_value = Variant()) const;
	String get_home_dir();
	ResourceUID::ID get_uid_for_path(const String &p_path) const;
	String get_game_name() const;

	Vector<String> get_errors();

	static GDRESettings *get_singleton();
	GDRESettings();
	~GDRESettings();
};

VARIANT_ENUM_CAST(GDRESettings::PackInfo::PackType);
#endif
