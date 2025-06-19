#include "gitlab_source.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "utility/common.h"
#include "utility/plugin_manager.h"

const String GitLabSource::gitlab_release_api_url = _gitlab_release_api_url;
namespace {
static const HashMap<String, Vector<String>> tag_masks = {};
static const HashMap<String, Vector<String>> release_file_masks = {
	{ "sg-physics-2d", { "*gdextension*" } },
};
static const HashMap<String, Vector<String>> release_file_exclude_masks = {};
static const HashMap<String, String> plugin_map = {
	{ "sg-physics-2d", "https://gitlab.com/snopek-games/sg-physics-2d" },
};
} // namespace
GitLabSource::GitLabSource() {
	// Initialize any necessary resources
}

GitLabSource::~GitLabSource() {
	// Clean up any resources
}

const HashMap<String, String> &GitLabSource::get_plugin_repo_map() {
	return plugin_map;
}

const HashMap<String, Vector<String>> &GitLabSource::get_plugin_tag_masks() {
	return tag_masks;
}

const HashMap<String, Vector<String>> &GitLabSource::get_plugin_release_file_masks() {
	return release_file_masks;
}

const HashMap<String, Vector<String>> &GitLabSource::get_plugin_release_file_exclude_masks() {
	return release_file_exclude_masks;
}

bool GitLabSource::recache_release_list(const String &plugin_name) {
	{
		MutexLock lock(cache_mutex);
		if (release_cache.has(plugin_name)) {
			auto &cache = release_cache[plugin_name];
			if (!is_cache_expired(cache.retrieved_time)) {
				return true;
			}
		}
	}

	String repo_url = get_repo_url(plugin_name);
	if (repo_url.is_empty() || !repo_url.contains("gitlab.com")) {
		return false;
	}

	double now = OS::get_singleton()->get_unix_time();
	// Extract org and repo from URL
	String org = repo_url.get_slice("/", 3);
	String repo = repo_url.get_slice("/", 4);

	Vector<Dictionary> releases;
	String request_url = gitlab_release_api_url.replace("{0}", org).replace("{1}", repo);

	Vector<uint8_t> response;
	Error err = gdre::wget_sync(request_url, response, 20);
	if (err) {
		return false;
	}

	String response_str;
	response_str.append_utf8((const char *)response.ptr(), response.size());
	Array response_obj = JSON::parse_string(response_str);
	if (response_obj.is_empty()) {
		return false;
	}

	for (int i = 0; i < response_obj.size(); i++) {
		Dictionary release = response_obj[i];
		releases.push_back(release);
	}

	GHReleaseCache cache;
	cache.retrieved_time = now;

	for (int i = 0; i < releases.size(); i++) {
		Dictionary release = releases[i];
		String tag_name = release.get("tag_name", "");
		uint64_t release_id = tag_name.hash(); // GitLab doesn't provide release IDs, so we hash the tag name
		release["id"] = release_id;

		Dictionary assets_obj = release.get("assets", Dictionary());
		Array assets_arr = assets_obj.get("links", Array());
		HashMap<uint64_t, Dictionary> asset_map;

		for (int j = 0; j < assets_arr.size(); j++) {
			Dictionary asset = assets_arr[j];
			// Convert GitLab asset format to match GitHub's
			asset["browser_download_url"] = asset.get("direct_asset_url", "");
			asset["created_at"] = release.get("created_at", "");
			asset["updated_at"] = release.get("released_at", "");

			uint64_t asset_id = uint64_t(asset.get("id", 0));
			asset_map[asset_id] = asset;
			assets_arr[j] = asset;
		}
		release["assets"] = assets_arr;
		cache.releases[release_id] = { release, asset_map };
	}

	{
		MutexLock lock(cache_mutex);
		release_cache[plugin_name] = cache;
	}

	return true;
}

String GitLabSource::get_plugin_name() {
	return "gitlab";
}
