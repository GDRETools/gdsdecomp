#ifndef GITHUB_SOURCE_H
#define GITHUB_SOURCE_H

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "plugin_source.h"

class GitHubSource : public PluginSource {
	GDCLASS(GitHubSource, PluginSource)

private:
	static constexpr const char *_github_release_api_url = "https://api.github.com/repos/{0}/{1}/releases?per_page=100&page={2}";
	static const String github_release_api_url;
	virtual const HashMap<String, String> &get_plugin_repo_map();
	virtual const HashMap<String, Vector<String>> &get_plugin_tag_masks();
	virtual const HashMap<String, Vector<String>> &get_plugin_release_file_masks();
	virtual const HashMap<String, Vector<String>> &get_plugin_release_file_exclude_masks();
	virtual String get_plugin_cache_path();

protected:
	struct GHReleaseCache {
		double retrieved_time = 0;
		struct ReleasePair {
			Dictionary release;
			HashMap<uint64_t, Dictionary> assets;
		};
		HashMap<uint64_t, ReleasePair> releases;
	};

	HashMap<String, GHReleaseCache> release_cache;
	HashMap<String, HashMap<uint64_t, HashMap<uint64_t, PluginVersion>>> non_asset_lib_cache; // plugin_name -> tag -> asset_id -> PluginVersion

	bool should_skip_tag(const String &plugin_name, const String &tag);
	bool should_skip_release(const String &plugin_name, const String &release);
	String get_repo_url(const String &plugin_name);

	bool _get_cached_version(const String &plugin_name, uint64_t release_id, uint64_t asset_id, PluginVersion &version);
	Vector<Dictionary> get_list_of_releases(const String &plugin_name);

	bool init_plugin_version_from_release(Dictionary release_entry, uint64_t gh_asset_id, PluginVersion &version);
	Dictionary get_release_dict(const String &plugin_name, uint64_t release_id);
	Vector<Pair<uint64_t, uint64_t>> get_gh_asset_pairs(const String &plugin_name);
	PluginVersion get_plugin_version_gh(const String &plugin_name, uint64_t release_id, uint64_t asset_id);

	virtual bool recache_release_list(const String &plugin_name);

public:
	GitHubSource();
	~GitHubSource();

	// PluginSource interface implementation
	PluginVersion get_plugin_version(const String &plugin_name, const String &version) override;
	String get_plugin_download_url(const String &plugin_name, const Vector<String> &hashes) override;
	Vector<String> get_plugin_version_numbers(const String &plugin_name) override;
	String get_plugin_name() override;
	void load_cache_internal() override;
	void save_cache() override;
	void prepop_cache(const Vector<String> &plugin_names, bool multithread = false) override;
	bool handles_plugin(const String &plugin_name) override;
	bool is_default() override { return false; }
	void load_cache_data(const String &plugin_name, const Dictionary &data) override;
};

#endif // GITHUB_SOURCE_H
