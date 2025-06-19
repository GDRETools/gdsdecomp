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

protected:
	struct GHReleaseCache {
		double retrieved_time = 0;
		struct ReleasePair {
			Dictionary release;
			HashMap<uint64_t, Dictionary> assets;
		};
		HashMap<uint64_t, ReleasePair> releases;
		Dictionary to_json() const {
			Dictionary d;
			d["retrieved_time"] = retrieved_time;
			Dictionary releases_dict;
			for (auto &E : releases) {
				auto &release_id = E.key;
				auto &release_pair = E.value;
				Dictionary release_dict;
				release_dict["release"] = release_pair.release;
				// Don't populate assets; we restore them from the `release` dictionary
				releases_dict[release_id] = release_dict;
			}
			d["releases"] = releases_dict;
			return d;
		}
		static GHReleaseCache from_json(const Dictionary &d) {
			GHReleaseCache cache;
			cache.retrieved_time = d.get("retrieved_time", 0);
			Dictionary releases_dict = d.get("releases", {});
			for (auto &E : releases_dict.keys()) {
				auto &release_id = E;
				Dictionary release_dict = releases_dict[release_id];
				ReleasePair release_pair;
				release_pair.release = release_dict.get("release", {});
				Array assets = release_pair.release.get("assets", {});
				for (int i = 0; i < assets.size(); i++) {
					Dictionary asset = assets[i];
					uint64_t asset_id = asset.get("id", 0);
					release_pair.assets[asset_id] = asset;
				}
				cache.releases[release_id] = release_pair;
			}
			return cache;
		}
	};

	HashMap<String, GHReleaseCache> release_cache;

	bool should_skip_tag(const String &plugin_name, const String &tag);
	bool should_skip_release(const String &plugin_name, const String &release);
	String get_repo_url(const String &plugin_name);

	Vector<Dictionary> get_list_of_releases(const String &plugin_name);

	Dictionary get_release_dict(const String &plugin_name, uint64_t release_id);
	Vector<Pair<uint64_t, uint64_t>> get_gh_asset_pairs(const String &plugin_name);

	virtual bool recache_release_list(const String &plugin_name);

	String _get_release_cache_file_name();
	void _load_release_cache();
	void _save_release_cache();

public:
	GitHubSource();
	~GitHubSource();

	// PluginSource interface implementation
	Vector<String> get_plugin_version_numbers(const String &plugin_name) override;
	ReleaseInfo get_release_info(const String &plugin_name, const String &version_key) override;
	String get_plugin_name() override;
	void load_cache_internal() override;
	void save_cache() override;
	bool handles_plugin(const String &plugin_name) override;
	bool is_default() override { return false; }
	// void load_cache_data(const String &plugin_name, const Dictionary &data) override; // Deprecated
};

#endif // GITHUB_SOURCE_H
