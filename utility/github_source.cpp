#include "github_source.h"
#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/string/ustring.h"
#include "modules/zip/zip_reader.h"
#include "utility/common.h"
#include "utility/gdre_settings.h"
#include "utility/glob.h"
#include "utility/plugin_manager.h"
#include "utility/task_manager.h"

const String GitHubSource::github_release_api_url = _github_release_api_url;

namespace {
static const HashMap<String, Vector<String>> tag_masks = {
	{ "godotsteam", { "*gdn*", "*gde*" } },
};

static const HashMap<String, Vector<String>> release_file_masks = {
	{ "limboai", { "*gdextension*" } },
	{ "orchestrator", { "*plugin*" } },
	{ "discord-rpc-gd", { "*RPC*" } },
	{ "discord-sdk-gd", { "*SDK*" } },
	{ "godot-rapier2d", { "godot-rapier-2d*", "godot-rapier2d*" } },
	{ "godot-rapier3d", { "godot-rapier-3d*", "godot-rapier3d*" } },
};

static const HashMap<String, Vector<String>> release_file_exclude_masks = {
	{ "godot-jolt", { "*symbols*" } },
	{ "godot-steam-audio", { "*demo*" } },
	{ "discord-rpc-gd", { "*Demo*" } },
	{ "discord-sdk-gd", { "*Demo*" } },
	{ "epic-online-services-godot", { "*-android-*", "*-ios-*", "*-macos-*", "*-windows-*", "*-linux-*", "*-web*" } },
};

static const HashMap<String, String> plugin_map = {
	{ "epic-online-services-godot", "https://github.com/3ddelano/epic-online-services-godot" },
	{ "godotgif", "https://github.com/BOTLANNER/godot-gif" },
	{ "godot-rapier3d", "https://github.com/appsinacup/godot-rapier-physics" },
	{ "godot-rapier2d", "https://github.com/appsinacup/godot-rapier-physics" },
	{ "native_dialogs", "https://github.com/98teg/NativeDialogs" },
	{ "ffmpeg", "https://github.com/EIRTeam/EIRTeam.FFmpeg" },
	{ "discord-sdk-gd", "https://github.com/vaporvee/discord-rpc-godot" },
	{ "discord-rpc-gd", "https://github.com/vaporvee/discord-rpc-godot" },
	{ "godot-steam-audio", "https://github.com/stechyo/godot-steam-audio" },
	{ "m_terrain", "https://github.com/mohsenph69/Godot-MTerrain-plugin" },
	{ "godotsteam", "https://github.com/GodotSteam/GodotSteam" },
	{ "godot-jolt", "https://github.com/godot-jolt/godot-jolt" },
	{ "orchestrator", "https://github.com/CraterCrash/godot-orchestrator" },
	{ "limboai", "https://github.com/limbonaut/limboai" },
	{ "terrain_3d", "https://github.com/TokisanGames/Terrain3D" },
	{ "fmod", "https://github.com/utopia-rise/fmod-gdextension" },
};
} // namespace
GitHubSource::GitHubSource() {
	// Initialize any necessary resources
}

GitHubSource::~GitHubSource() {
	// Clean up any resources
}

const HashMap<String, String> &GitHubSource::get_plugin_repo_map() {
	return plugin_map;
}

const HashMap<String, Vector<String>> &GitHubSource::get_plugin_tag_masks() {
	return tag_masks;
}

const HashMap<String, Vector<String>> &GitHubSource::get_plugin_release_file_masks() {
	return release_file_masks;
}

const HashMap<String, Vector<String>> &GitHubSource::get_plugin_release_file_exclude_masks() {
	return release_file_exclude_masks;
}

bool GitHubSource::should_skip_tag(const String &plugin_name, const String &tag) {
	if (get_plugin_tag_masks().has(plugin_name)) {
		const auto &suffixes = get_plugin_tag_masks()[plugin_name];
		for (int i = 0; i < suffixes.size(); i++) {
			if (tag.match(suffixes[i])) {
				return false;
			}
		}
		return true;
	}
	return false;
}

bool GitHubSource::should_skip_release(const String &plugin_name, const String &release_url) {
	if (release_url.is_empty()) {
		return true;
	}
	if (get_plugin_release_file_exclude_masks().has(plugin_name)) {
		const auto &masks = get_plugin_release_file_exclude_masks()[plugin_name];
		String file_name = release_url.get_file().to_lower();
		for (int i = 0; i < masks.size(); i++) {
			if (file_name.matchn(masks[i])) {
				return true;
			}
		}
	}
	if (get_plugin_release_file_masks().has(plugin_name)) {
		const auto &masks = get_plugin_release_file_masks()[plugin_name];
		String file_name = release_url.get_file().to_lower();
		for (int i = 0; i < masks.size(); i++) {
			if (file_name.matchn(masks[i])) {
				return false;
			}
		}
		return true;
	}
	return false;
}

String GitHubSource::get_repo_url(const String &plugin_name) {
	return get_plugin_repo_map().has(plugin_name) ? get_plugin_repo_map()[plugin_name] : "";
}

bool GitHubSource::recache_release_list(const String &plugin_name) {
	bool has_cached_releases = false;
	{
		MutexLock lock(cache_mutex);
		if (release_cache.has(plugin_name)) {
			auto &cache = release_cache[plugin_name];
			if (!is_cache_expired(cache.retrieved_time)) {
				return true;
			}
			has_cached_releases = cache.releases.size() > 0;
		}
	}

	String repo_url = get_repo_url(plugin_name);
	if (repo_url.is_empty() || !repo_url.contains("github.com")) {
		return false;
	}

	double now = OS::get_singleton()->get_unix_time();
	// Extract org and repo from URL
	// the url is like this: https://github.com/GodotSteam/GodotSteam
	auto thing = repo_url.replace_first("https://", "");
	// now it's like this: github.com/GodotSteam/GodotSteam
	String org = thing.get_slice("/", 1);
	String repo = thing.get_slice("/", 2);

	Vector<Dictionary> releases;
	int pages = 1000;
	for (int page = 1; page < pages; page++) {
		String request_url = github_release_api_url.replace("{0}", org).replace("{1}", repo).replace("{2}", itos(page));

		Vector<uint8_t> response;
		Error err = gdre::wget_sync(request_url, response, 20);
		if (err) {
			if (err == ERR_UNAUTHORIZED) { // rate limit exceeded
				// use the cached releases if they exist
				print_line(get_plugin_name() + " rate limit exceeded!");
				if (has_cached_releases) {
					print_line(get_plugin_name() + " using cached releases...");
					return true;
				}
				print_line(get_plugin_name() + " no cached releases, failing...");
				return false;
			}
			if (err == ERR_FILE_NOT_FOUND && page > 1) {
				// no more releases
				break;
			}
			if (err != OK) {
				print_line(get_plugin_name() + " failed to get releases: " + itos(err));
				return false;
			}
		}

		String response_str;
		response_str.append_utf8((const char *)response.ptr(), response.size());
		Array response_obj = JSON::parse_string(response_str);
		if (response_obj.is_empty()) {
			break;
		}

		for (int i = 0; i < response_obj.size(); i++) {
			Dictionary release = response_obj[i];
			releases.push_back(release);
		}
		if (response_obj.size() < 100) {
			break;
		}
	}

	GHReleaseCache cache;
	cache.retrieved_time = now;

	for (int i = 0; i < releases.size(); i++) {
		Dictionary release = releases[i];
		uint64_t release_id = uint64_t(release.get("id", 0));
		Array assets_arr = release.get("assets", {});
		HashMap<uint64_t, Dictionary> asset_map;
		// empty out the author field because it takes up way too much space and its not needed
		release["author"] = Dictionary();
		for (int j = 0; j < assets_arr.size(); j++) {
			Dictionary asset = assets_arr[j];
			// same as author
			asset["uploader"] = Dictionary();
			uint64_t asset_id = uint64_t(asset.get("id", 0));
			asset_map[asset_id] = asset;
		}
		cache.releases[release_id] = { release, asset_map };
	}

	{
		MutexLock lock(cache_mutex);
		release_cache[plugin_name] = cache;
	}

	return true;
}
namespace {
bool is_empty_or_null(const String &str) {
	return str.is_empty() || str == "<null>";
}
} //namespace

ReleaseInfo GitHubSource::get_release_info(const String &plugin_name, const String &version_key) {
	auto parts = version_key.split("-");
	if (parts.size() != 2) {
		return ReleaseInfo();
	}
	auto release_id = parts[0].to_int();
	auto asset_id = parts[1].to_int();
	if (release_id == 0 || asset_id == 0) {
		return ReleaseInfo();
	}

	auto release = get_release_dict(plugin_name, release_id);
	if (release.is_empty()) {
		return ReleaseInfo();
	}

	Array assets = release.get("assets", {});
	for (int i = 0; i < assets.size(); i++) {
		Dictionary asset = assets[i];
		if (uint64_t(asset.get("id", 0)) == asset_id) {
			String name = asset.get("name", "");
			if (is_empty_or_null(name)) {
				continue;
			}
			String download_url = asset.get("browser_download_url", "");
			String ext = download_url.get_file().get_extension().to_lower();
			if (ext.is_empty()) {
				ext = name.get_extension().to_lower();
			}
			if (ext == "zip") {
				if (is_empty_or_null(download_url)) {
					continue;
				}
				String tag_name = release.get("tag_name", "");

				ReleaseInfo release_info;
				release_info.plugin_source = get_plugin_name();
				release_info.primary_id = release_id;
				release_info.secondary_id = asset_id;
				release_info.version = tag_name;
				release_info.engine_ver_major = 0; // Will be determined during analysis
				release_info.release_date = asset.get("created_at", "");
				release_info.download_url = download_url;

				return release_info;
			}
		}
	}

	return ReleaseInfo();
}

Vector<Dictionary> GitHubSource::get_list_of_releases(const String &plugin_name) {
	if (!recache_release_list(plugin_name)) {
		return {};
	}
	Vector<Dictionary> releases;
	{
		MutexLock lock(cache_mutex);
		if (release_cache.has(plugin_name)) {
			for (auto &release : release_cache[plugin_name].releases) {
				releases.push_back(release.value.release);
			}
		}
	}
	return releases;
}

Vector<Pair<uint64_t, uint64_t>> GitHubSource::get_gh_asset_pairs(const String &plugin_name) {
	auto thing = get_list_of_releases(plugin_name);
	Vector<Pair<uint64_t, uint64_t>> release_asset_pairs;
	for (auto &release : thing) {
		auto tag = release.get("tag_name", "");
		if (should_skip_tag(plugin_name, tag)) {
			continue;
		}
		uint64_t release_id = release.get("id", 0);
		Array assets = release.get("assets", Array());
		for (auto &asset : assets) {
			if (should_skip_release(plugin_name, ((Dictionary)asset).get("browser_download_url", ""))) {
				continue;
			}
			uint64_t asset_id = ((Dictionary)asset).get("id", 0);
			release_asset_pairs.push_back({ release_id, asset_id });
		}
	}
	return release_asset_pairs;
}

Dictionary GitHubSource::get_release_dict(const String &plugin_name, uint64_t release_id) {
	if (!recache_release_list(plugin_name)) {
		return Dictionary();
	}
	{
		MutexLock lock(cache_mutex);
		if (release_cache.has(plugin_name)) {
			auto &cache = release_cache[plugin_name];
			if (cache.releases.has(release_id)) {
				return cache.releases[release_id].release;
			}
		}
	}
	return Dictionary();
}

Vector<String> GitHubSource::get_plugin_version_numbers(const String &plugin_name) {
	auto pairs = get_gh_asset_pairs(plugin_name);
	Vector<String> versions;
	for (auto &pair : pairs) {
		versions.push_back(itos(pair.first) + "-" + itos(pair.second));
	}
	return versions;
}

void GitHubSource::load_cache_internal() {
	_load_release_cache();
}

String GitHubSource::_get_release_cache_file_name() {
	return PluginManager::get_plugin_cache_path().path_join(get_plugin_name() + "_release_cache.json");
}

// Doing this because GitHub rate limits after only 60 requests per hour
void GitHubSource::_load_release_cache() {
	MutexLock lock(cache_mutex);
	auto file = _get_release_cache_file_name();
	if (!FileAccess::exists(file)) {
		return;
	}
	auto fa = FileAccess::open(file, FileAccess::READ);
	ERR_FAIL_COND_MSG(fa.is_null(), "Failed to open file for reading: " + file);
	String json = fa->get_as_text();
	Dictionary d = JSON::parse_string(json);
	for (auto &E : d.keys()) {
		String plugin_name = E;
		Dictionary plugin_dict = d[plugin_name];
		release_cache[plugin_name] = GHReleaseCache::from_json(plugin_dict);
	}
}

void GitHubSource::_save_release_cache() {
	MutexLock lock(cache_mutex);
	auto file = _get_release_cache_file_name();
	gdre::ensure_dir(file.get_base_dir());
	auto fa = FileAccess::open(file, FileAccess::WRITE);
	ERR_FAIL_COND_MSG(fa.is_null(), "Failed to open file for writing: " + file);
	Dictionary d;
	for (auto &E : release_cache) {
		d[E.key] = E.value.to_json();
	}
	fa->store_string(JSON::stringify(d, " ", false, true));
}

void GitHubSource::save_cache() {
	_save_release_cache();
}

bool GitHubSource::handles_plugin(const String &plugin_name) {
	return get_plugin_repo_map().has(plugin_name);
}

String GitHubSource::get_plugin_name() {
	return "github";
}
