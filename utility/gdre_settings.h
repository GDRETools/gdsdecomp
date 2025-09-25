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
class GodotMonoDecompWrapper;
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
		bool loaded_resource_strings = false;
		PackInfo::PackType type = PackInfo::PCK;
		String pack_file;
		int bytecode_revision = 0;
		bool suspect_version = false;
		String assembly_path;
		Ref<GodotMonoDecompWrapper> decompiler;
		String assembly_temp_dir;
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

	// Load import file task function
	void _do_import_load(uint32_t i, IInfoToken *tokens);
	String get_IInfoToken_description(uint32_t i, IInfoToken *p_userdata);
	// String load individual file task function
	void _do_string_load(uint32_t i, StringLoadToken *tokens);
	String get_string_load_token_description(uint32_t i, StringLoadToken *p_userdata);
	HashMap<ResourceUID::ID, UID_Cache> unique_ids; //unique IDs and utf8 paths (less memory used)
	ParallelFlatHashMap<String, ResourceUID::ID> path_to_uid;
	HashMap<String, Dictionary> script_cache;

	Vector<uint8_t> enc_key;

	bool in_editor = false;
	bool first_load = true;
	bool error_encryption = false;
	String project_path = "";
	static GDRESettings *singleton;
	static String exec_dir;
	bool headless = false;
	bool download_plugins = false;
	HashSet<String> translation_key_hints;

	// Unloads the current pack
	void remove_current_pack();
	void add_logger();

	// After Godot starts up, it changes the CWD to the executable directory; we need to get the actual CWD before initalizing is finished for CLI usage
	static String _get_cwd();
	// Gets the version from the binary resources
	Error get_version_from_bin_resources();
	// Checks if the given directory contains files that will only show up in version 4
	bool check_if_dir_is_v4();
	// Checks if the given directory contains files that will only show up in version 3
	bool check_if_dir_is_v3();
	// Checks if the given directory contains files that will NOT show up in version 2
	bool check_if_dir_is_v2();
	// Gets the major version of the engine from the given directory
	int get_ver_major_from_dir();
	// This loads project directories by setting the global resource path to the project directory
	// We have to be very careful about this, this means that any GDRE resources we have loaded
	// could fail to reload if they somehow became unloaded while we were messing with the project.
	Error load_dir(const String &p_path);
	// resets the ProjectSettings resource path to the GDRE resource path
	Error unload_dir();
	// Checks if we have detected a valid engine version for the current project
	bool has_valid_version() const;

	// Returns the path to the hidden project data directory, or the regular project data directory if the hidden directory is not set
	// This is where all the imported resources reside in the project.
	String get_loaded_pack_data_dir();
	// Loads the UID cache from the PCK and populates the global ResourceUID cache.
	// Necessary for being able to load dependent resources by their UID.
	Error load_pack_uid_cache(bool p_reset = false);
	Error reset_uid_cache();

	// Loads the GDScript cache from the PCK
	Error load_pack_gdscript_cache(bool p_reset = false);
	// Resets the GDScript cache
	Error reset_gdscript_cache();
	// Ensures that the script cache has all the scripts in the project.
	// We need this to be complete to ensure that scripts can load their base classes
	// (which is required for scene export for scenes with scripts)
	void _ensure_script_cache_complete();

	// Detects the bytecode revision from the binary resources
	// This is necessary for being able to decompile scripts
	Error detect_bytecode_revision(bool p_no_valid_version);

	static constexpr bool need_correct_patch(int ver_major, int ver_minor);
	void _do_prepop(uint32_t i, const String *plugins);
	// For printing out paths, we want to replace the home directory with ~ to keep PII out of logs
	String sanitize_home_in_path(const String &p_path);
	// Ensures log files contain this information
	void log_sysinfo();

	static ResourceUID::ID _get_uid_for_path(const String &p_path, bool _generate = false);

	// Loads the encryption key from the global settings into `script_encryption_key`
	// so that native classes (e.g. FileAccessPCK) can use it during project recovery
	void load_encryption_key();
	// Unloads the encryption key
	void unload_encryption_key();
	Error reload_dotnet_assembly(const String &p_path);
	// Finds and loads the .NET assembly for the project
	Error load_project_dotnet_assembly();

	// Games can define global shader parameters in the project settings;
	// this function loads them into the rendering server to make them available to any shaders that are loaded (e.g. during scene export)
	void _set_shader_globals();
	// Clears them after the project is unloaded
	void _clear_shader_globals();

	Vector<String> sort_and_validate_pck_files(const Vector<String> &p_paths);

	// Initializes the bytecode revision from the ephemeral settings if `force_bytecode_revision` is set
	bool _init_bytecode_from_ephemeral_settings();

protected:
	static void _bind_methods();
	Error _add_pack(const String &p_path);

	Error _load_embedded_zips();

	Error _project_post_load(bool initial_load = false, const String &csharp_assembly_override = "");

public:
	// Loads the project from the given paths
	// Main entry point for project recovery and extraction
	Error load_project(const Vector<String> &p_paths, bool cmd_line_extract = false, const String &csharp_assembly_override = "");
	// Used by `load_project` to load the given PCK file
	Error load_pck(const String &p_path);

	// Performs steps necessary after loading the project to patch the translation files
	// We do not want to do all the steps that are done in `load_project()`
	// because we don't want to load embedded zips, get the bytecode revision, load .net assemblies, etc.
	Error post_load_patch_translation();
	bool needs_post_load_patch_translation() const;

	// Unloads the project
	Error unload_project(bool p_no_reset_ephemeral = false);
	// Get the path to the GDRE resource directory (dev-only, only used if running in editor)
	String get_gdre_resource_path() const;
	// Get the path to the GDRE user directory (where we store temp files, plugin cache, logs, etc.)
	String get_gdre_user_path() const;
	// Get the path to the GDRE temp directory
	String get_gdre_tmp_path() const;

	// Checks if we have loaded a project
	bool is_pack_loaded() const;

	// Returns whether the project had an encryption error
	bool had_encryption_error() const;

	// Returns the encryption key as bytes
	Vector<uint8_t> get_encryption_key();
	// Returns the encryption key as a string
	String get_encryption_key_string();
	// PackedSource doesn't pass back useful error information when loading packs,
	// this is a hack so that we can tell if it was an encryption error.
	void _set_error_encryption(bool is_encryption_error);
	// Sets the encryption key from a vector of bytes
	Error set_encryption_key(Vector<uint8_t> key);
	// Sets the encryption key from a string
	Error set_encryption_key_string(const String &key);
	// Resets the encryption key
	void reset_encryption_key();
	// Adds a pack info to the list of packs (used by the pack sources in GDREPackedData)
	void add_pack_info(Ref<PackInfo> packinfo);

	// Returns the class for the given script path from the script cache
	StringName get_cached_script_class(const String &p_path);
	// Returns the base class for the given script path from the script cache
	StringName get_cached_script_base(const String &p_path);
	// Returns the path to the script for the given class from the script cache
	String get_path_for_script_class(const StringName &p_class);

	// Returns the list of files in the project, filtered by the given filters
	Vector<String> get_file_list(const Vector<String> &filters = Vector<String>());
	// Same as the below but as an array for GDScript
	Array get_file_info_array(const Vector<String> &filters = Vector<String>());
	// Returns the list of file infos in the project, filtered by the given filters
	Vector<Ref<PackedFileInfo>> get_file_info_list(const Vector<String> &filters = Vector<String>());
	// Returns the list of currently loaded packs
	TypedArray<PackInfo> get_pack_info_list() const;
	// Returns the list of paths to the currently loaded packs
	Vector<String> get_pack_paths() const;
	// Get the type of the current project's main pack
	PackInfo::PackType get_pack_type() const;
	// Get the path to the current project's main pack
	String get_pack_path() const;
	// Get the current project's engine version string
	String get_version_string() const;
	// Get the current project's engine version major
	uint32_t get_ver_major() const;
	// Get the current project's engine version minor
	uint32_t get_ver_minor() const;
	// Get the current project's engine version revision
	uint32_t get_ver_rev() const;
	// Get the total number of files in all loaded packs
	uint32_t get_file_count() const;
	// Converts a local path to a global filesystem path (e.g. "res://icon.png" -> "/path/to/game/icon.png")
	String globalize_path(const String &p_path, const String &resource_path = "") const;
	// Converts a global filesystem path to a local path (e.g. "/path/to/game/icon.png" -> "res://icon.png")
	String localize_path(const String &p_path, const String &resource_path = "") const;
	// The path for projects loaded from directories; used primarily by `localize_path` and `globalize_path`
	void set_project_path(const String &p_path);
	String get_project_path() const;
	// Starts logging to a file for project recovery
	Error open_log_file(const String &output_dir);
	// Returns the path to the currently open log file
	String get_log_file_path();
	Error close_log_file();
	Dictionary get_remaps(bool include_imports = true) const;
	// Checks if the project has any remaps
	bool has_any_remaps() const;
	// Checks if a remap exists for the given source and destination paths
	bool has_remap(const String &src, const String &dst) const;
	Error add_remap(const String &src, const String &dst);
	// Returns the remapped path for the given source path
	// (only remaps defined by `.remap` files or the project config setting, not `.import` files)
	String get_remap(const String &src) const;
	// Returns the remapped path for the given source path
	String get_mapped_path(const String &src) const;
	// Removes a remap from the project config or output directory
	Error remove_remap(const String &src, const String &dst, const String &output_dir = "");
	// Returns a project setting for the currently open project
	Variant get_project_setting(const String &p_setting, const Variant &default_value = Variant()) const;
	// Checks if a project setting exists for the currently open project
	bool has_project_setting(const String &p_setting);
	// Sets a project setting for the currently open project
	void set_project_setting(const String &p_setting, Variant value);
	// Returns the path to the currently open project config file
	String get_project_config_path();
	// Get the current working directory (for CLI usage)
	String get_cwd();
	// Returns the import files from the project
	// If `copy` is true, returns a copy of the import files
	Array get_import_files(bool copy = false);
	// Whether or not the file is located in a loaded pack
	bool has_path_loaded(const String &p_path) const;
	// Loads the import files (.import, .remap, .gdnlib, .gdextension)
	// This is necessary for resource export and mapping paths.
	Error load_import_files();
	Error load_import_file(const String &p_path);
	Ref<ImportInfo> get_import_info_by_dest(const String &p_path) const;
	Ref<ImportInfo> get_import_info_by_source(const String &p_path);
	// Get the path to the current working directory (for CLI usage)
	String get_exec_dir();
	// Set the current working directory (for CLI usage)
	void set_exec_dir(const String &p_cwd);
	// Checks if we have loaded any import files
	bool are_imports_loaded() const;
	// Checks if we loaded a project config from the current project
	bool is_project_config_loaded() const;
	// Returns whether GDRE Tools is running with `--headless`
	bool is_headless() const;
	// Returns a string containing the OS name, version, and rendering adapter name
	// For logging and bug reports
	String get_sys_info_string() const;
	// Loads the project config from the project directory
	Error load_project_config();
	// Saves the project config to a file
	Error save_project_config(const String &p_out_dir);
	// Saves the project config to a binary file
	Error save_project_config_binary(const String &p_out_dir);
	// Checks if the project has a project.godot or project.binary file
	bool pack_has_project_config() const;
	// A copy of `EditorSettings::get_auto_display_scale()`, copied here for non-editor builds
	// Gets the auto display scale for native GUI elements (gdre_progress, etc.)
	float get_auto_display_scale() const;
	String get_gdre_version() const;
	String get_disclaimer_text() const;
	static String get_disclaimer_body();
	bool loaded_resource_strings() const;
	// Loads all the strings from all resources, scripts, and text files in the project for translation key recovery
	void load_all_resource_strings();
	void get_resource_strings(HashSet<String> &r_strings) const;
	// Returns the detected bytecode revision
	int get_bytecode_revision() const;
	// Helper command to prepopulate the plugin version cache for releases
	void prepop_plugin_cache(const Vector<String> &plugins);
	String get_home_dir();
	// ResourceUID does not provide a way to get a UID for a given path, so we have to do it ourselves
	ResourceUID::ID get_uid_for_path(const String &p_path) const;
	String get_game_name() const;
	// the reverse of `get_remap()`; gets the source path for the given destination path
	String get_remapped_source_path(const String &p_dst) const;

	Vector<String> get_errors();

	void set_dotnet_assembly_path(const String &p_path);
	String get_dotnet_assembly_path() const;

	bool has_loaded_dotnet_assembly() const;
	String get_project_dotnet_assembly_name() const;

	bool project_requires_dotnet_assembly() const;

	// In case we had to copy the .mono folder to a temporary directory
	// Used by the GUI to determine if we should display the Assembly picker
	String get_temp_dotnet_assembly_dir() const;
	String find_dotnet_assembly_path(Vector<String> p_search_dirs) const;

	Ref<GodotMonoDecompWrapper> get_dotnet_decompiler() const;

	// Updates .NET assembly and script states if the user changes settings in the "Export options" dialog while the project is loaded
	void update_from_ephemeral_settings();

	String get_recent_error_string(bool p_filter_backtraces = true);

	static GDRESettings *get_singleton();

	// Loads a file containing translation key hints
	// This can either be a CSV file (e.g. the output of the translation exporter),
	// or a "stringdump" file (i.e. a dump of all the strings that were found in the project during `load_all_resource_strings()`)
	Error load_translation_key_hint_file(const String &p_path);

	GDRESettings();
	~GDRESettings();
};

VARIANT_ENUM_CAST(GDRESettings::PackInfo::PackType);
#endif
