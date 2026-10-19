#pragma once

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"

#include "plugin_info.h"
#include "plugin_source.h"

#include "utility/task_manager.h"

class PluginManager : public Object {
	GDCLASS(PluginManager, Object)

private:
	struct PrePopToken {
		String plugin_name;
		Ref<PluginSource> source;
		Pair<int64_t, int64_t> version;
		Error error = OK;
	};

	static PluginManager *singleton;
	static constexpr const char *PLUGIN_CACHE_ENV_VAR = "GDRE_PLUGIN_CACHE_DIR";
	static constexpr const char *STATIC_PLUGIN_CACHE_PATH = "res://gdre_static_plugin_cache.json";
	static constexpr const int MAX_SOURCES = 64;
	Ref<PluginSource> sources[MAX_SOURCES];
	int source_count;
	bool prepopping;
	WorkerThreadPool::TaskID load_cache_task_id = -1;
	bool loaded_cache = false;
	HashMap<String, PluginVersion> plugin_version_cache;
	Mutex plugin_version_cache_mutex;
	static HashMap<String, Pair<String, String>> known_bad_plugin_versions;

	String get_prepop_task_description(uint32_t index, PrePopToken *tokens);
	void do_prepop_task(uint32_t index, PrePopToken *tokens);

	// Source management
	Ref<PluginSource> get_source(const String &plugin_name);
	void load_plugin_version_cache_file(const String &cache_file, bool prepop = false);
	void load_plugin_version_cache();
	void save_plugin_version_cache();
	Error save_plugin_version_static_cache(const String &output_path);
	Error populate_plugin_version_hashes(PluginVersion &plugin_version);
	String get_download_path_for_release(const ReleaseInfo &p_release_info);
	TaskManager::DownloadTaskID start_download_plugin_to_cache(const ReleaseInfo &p_release_info, const String &p_sha256_sum, String &r_save_path, Error &r_error);

	PluginVersion _get_plugin_version_for_current_release_info(const ReleaseInfo &release_info, Error &r_error);

	void _load_cache_on_this_thread(void *_unused = nullptr);
	bool is_prepopping();
	// PluginVersion cache management
	PluginVersion get_cached_plugin_version(const String &cache_key);
	void erase_cached_plugin_version(const String &cache_key);
	void cache_plugin_version(const String &cache_key, const PluginVersion &version);
	PluginVersion populate_plugin_version_from_release(const ReleaseInfo &release_info, Error &r_error);
	PluginVersion get_plugin_version_for_key(const String &plugin_name, int64_t primary_id, int64_t secondary_id, Error &r_error);

	void _wait_for_load_task();
	void ensure_cache_loaded();

protected:
	static void _bind_methods();

	Dictionary _get_plugin_info(const String &plugin_name, const Vector<String> &hashes);

public:
	static PluginManager *get_singleton();
	// Main interface methods
	static String get_plugin_download_cache_path();
	static String get_plugin_cache_path();
	TaskManager::DownloadTaskID download_plugin(const PluginVersion &p_plugin_version, String &r_save_path, Error &r_error);
	PluginVersion get_plugin_info(const String &plugin_name, const Vector<String> &hashes);
	Vector<PluginVersion> get_possibles_from_deps(const String &plugin_name, const Ref<GodotVer> engine_version, const Vector<PluginBin> &deps);
	Error prepop_cache(const Vector<String> &plugin_names, const String &output_path);
	// Source management
	void register_source(Ref<PluginSource> source, bool at_front = false);
	void unregister_source(Ref<PluginSource> source, bool save_cache = true);
	void print_plugin_cache();
	void clear_plugin_cache(bool clear_static_cache = false);
	void clear_download_cache();

	void reload_cache();
	void save_cache();
	void start_load_cache();

	PluginManager();
	~PluginManager();
};
