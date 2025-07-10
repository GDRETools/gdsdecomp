#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "utility/asset_library_source.h"
#include "utility/github_source.h"
#include "utility/gitlab_source.h"
#include "utility/plugin_info.h"
#include "utility/plugin_source.h"

class PluginManager : public Object {
	GDCLASS(PluginManager, Object)

private:
	static PluginManager *singleton;
	static constexpr const char *PLUGIN_CACHE_ENV_VAR = "GDRE_PLUGIN_CACHE_DIR";
	static constexpr const char *STATIC_PLUGIN_CACHE_PATH = "res://gdre_static_plugin_cache.json";
	static constexpr const int MAX_SOURCES = 64;
	static Ref<PluginSource> sources[MAX_SOURCES];
	static int source_count;
	static bool prepopping;
	static HashMap<String, PluginVersion> plugin_version_cache;
	static Mutex plugin_version_cache_mutex;

	// Source management
	static Ref<PluginSource> get_source(const String &plugin_name);
	static void load_plugin_version_cache_file(const String &cache_file);
	static void load_plugin_version_cache();
	static void save_plugin_version_cache();
	static Error populate_plugin_version_hashes(PluginVersion &plugin_version);
	static PluginVersion get_plugin_version_for_key(const String &plugin_name, const String &version);

protected:
	static void _bind_methods();

public:
	static void _init();
	// Main interface methods
	static String get_plugin_cache_path();
	static Dictionary get_plugin_info(const String &plugin_name, const Vector<String> &hashes);
	static void load_cache();
	static void save_cache();
	static void prepop_cache(const Vector<String> &plugin_names, bool multithread = true);
	static bool is_prepopping();
	// PluginVersion cache management
	static PluginVersion get_cached_plugin_version(const String &cache_key);
	static void cache_plugin_version(const String &cache_key, const PluginVersion &version);
	static String get_cache_key(const String &plugin_source, uint64_t primary_id, uint64_t secondary_id);
	static PluginVersion populate_plugin_version_from_release(const ReleaseInfo &release_info);
	// Source management
	static void register_source(Ref<PluginSource> source, bool at_front = false);
	static void unregister_source(Ref<PluginSource> source);
};

#endif // PLUGIN_MANAGER_H
