#include "github_source.h"
#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/string/ustring.h"
#include "plugin_manager.h"
#include "utility/common.h"
#include "utility/http_requester.h"

const String GitHubSource::github_release_api_url = _github_release_api_url;
namespace {
static const HashMap<String, Vector<String>> tag_masks = {};

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
	{ "fmod", { "*android.zip", "*demo.zip" } },
	{ "godotgif", { "*_convert_binary_*" } }
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
	{ "godot-jolt", "https://github.com/godot-jolt/godot-jolt" },
	{ "orchestrator", "https://github.com/CraterCrash/godot-orchestrator" },
	{ "limboai", "https://github.com/limbonaut/limboai" },
	{ "terrain_3d", "https://github.com/TokisanGames/Terrain3D" },
	{ "FMOD", "https://github.com/GDRETools/fmod-for-godot" }, // All caps FMOD
	{ "fmod", "https://github.com/utopia-rise/fmod-gdextension" }, // lowercase fmod
	{ "spine_godot_extension", "https://github.com/GDRETools/spine-runtimes" },
	{ "spine-godot-extension", "https://github.com/GDRETools/spine-runtimes" }, // this isn't distributed as an addon, so there are multiple potential names for the plugin
	{ "sentry", "https://github.com/getsentry/sentry-godot" },
	{ "sentrysdk", "https://github.com/getsentry/sentry-godot" },
	{ "wwise", "https://github.com/alessandrofama/wwise-godot-integration" },
	{ "Wwise", "https://github.com/alessandrofama/wwise-godot-integration" },
	{ "audio_gate", "https://github.com/Visssarion/audio-gate" },
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

const String &GitHubSource::get_release_api_url() {
	return github_release_api_url;
}

int GitHubSource::get_release_page_limit() {
	return 100;
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

String normalize_release_date(const String &p_datetime) {
	if (p_datetime.ends_with("Z")) {
		return p_datetime;
	}
	String time_splitter = p_datetime.find_char('T') > 0 ? "T" : "";
	if (time_splitter.is_empty()) {
		time_splitter = p_datetime.find_char(' ') > 0 ? " " : "";
	}
	if (time_splitter.is_empty()) {
		return p_datetime;
	}
	auto parts = p_datetime.split(time_splitter, false);
	const String &time = parts[1];
	int64_t index_of_plus = time.rfind_char('+');
	int64_t index_of_minus = time.rfind_char('-');
	String santized_datetime = p_datetime;

	if (index_of_plus > 0) {
		santized_datetime = santized_datetime.substr(0, p_datetime.rfind_char('+'));
	} else if (index_of_minus > 0) {
		santized_datetime = santized_datetime.substr(0, p_datetime.rfind_char('-'));
	}
	int64_t unix_time = Time::get_singleton()->get_unix_time_from_datetime_string(p_datetime);
	if (index_of_plus > 0) {
		String offset = time.get_slice("+", 1);
		int64_t offset_hours = offset.get_slice(":", 0).to_int();
		int64_t offset_minutes = offset.get_slice(":", 1).to_int();
		int64_t offset_seconds = offset.get_slice(":", 2).to_int();
		int64_t offset_total = offset_hours * 3600 + offset_minutes * 60 + offset_seconds;
		unix_time -= offset_total;
	}
	if (index_of_minus > 0) {
		String offset = time.get_slice("-", 1);
		int64_t offset_hours = offset.get_slice(":", 0).to_int();
		int64_t offset_minutes = offset.get_slice(":", 1).to_int();
		int64_t offset_seconds = offset.get_slice(":", 2).to_int();
		int64_t offset_total = offset_hours * 3600 + offset_minutes * 60 + offset_seconds;
		unix_time += offset_total;
	}

	String ret = Time::get_singleton()->get_datetime_string_from_unix_time(unix_time) + "Z";
	return ret;
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

Error GitHubSource::recache_release_list(const String &plugin_name) {
	bool has_cached_releases = false;
	{
		MutexLock lock(cache_mutex);
		if (release_cache.has(plugin_name)) {
			auto &cache = release_cache[plugin_name];
			if (!is_cache_expired(cache.retrieved_time)) {
				return OK;
			}
			has_cached_releases = cache.releases.size() > 0;
		}
	}

	String repo_url = get_repo_url(plugin_name);
	if (repo_url.is_empty()) {
		return ERR_INVALID_PARAMETER;
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
		Vector<String> extra_headers;
		// add the github api key if it's set; this is primarily used for prepopulating the cache to avoid rate limiting
		if (get_plugin_name() == "github") {
			String api_key = OS::get_singleton()->get_environment("GITHUB_API_KEY");
			if (!api_key.is_empty()) {
				extra_headers.push_back("Authorization: Bearer " + api_key);
			}
		}
		String request_url = get_release_api_url().replace("{0}", org).replace("{1}", repo).replace("{2}", itos(page));

		Vector<uint8_t> response;
		Error err = make_request(request_url, extra_headers, response);
		if (err) {
			if (err == ERR_BUSY) { // rate limit exceeded
				// use the cached releases if they exist
				print_line(get_plugin_name() + ": rate limit exceeded!");
				if (has_cached_releases) {
					print_line(get_plugin_name() + ": using cached releases...");
					return OK;
				}
				print_line(get_plugin_name() + ": no cached releases, failing...");
				return ERR_UNAUTHORIZED;
			}
			if (err == ERR_FILE_NOT_FOUND && page > 1) {
				// no more releases
				break;
			}
			if (err != OK) {
				print_line(get_plugin_name() + ": failed to get releases: " + itos(err));
				return err;
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
		if (response_obj.size() < get_release_page_limit()) {
			break;
		}
	}

	GHReleaseCache cache;
	cache.retrieved_time = now;

	for (int i = 0; i < releases.size(); i++) {
		Dictionary release = releases[i];
		int64_t release_id = int64_t(release.get("id", 0));
		Array assets_arr = release.get("assets", {});
		HashMap<int64_t, Dictionary> asset_map;
		// empty out the author field because it takes up way too much space and its not needed
		release["author"] = Dictionary();
		for (int j = 0; j < assets_arr.size(); j++) {
			Dictionary asset = assets_arr[j];
			// same as author
			if (asset.has("uploader")) {
				asset["uploader"] = Dictionary();
			}
			int64_t asset_id = int64_t(asset.get("id", 0));
			asset_map[asset_id] = asset;
		}
		cache.releases[release_id] = {};
		cache.releases[release_id].release = release;
		cache.releases[release_id].assets = asset_map;
	}

	{
		MutexLock lock(cache_mutex);
		release_cache[plugin_name] = cache;
	}

	return OK;
}
namespace {
bool is_empty_or_null(const String &str) {
	return str.is_empty() || str == "<null>";
}
} //namespace

ReleaseInfo GitHubSource::get_release_info(const String &plugin_name, int64_t primary_id, int64_t secondary_id, Error &r_error) {
	auto release_id = primary_id;
	auto asset_id = secondary_id;
	if (release_id <= 0 || asset_id <= 0) {
		return ReleaseInfo();
	}

	auto release = get_release_dict(plugin_name, release_id, r_error);
	if (release.is_empty()) {
		return ReleaseInfo();
	}

	Array assets = release.get("assets", {});
	for (int i = 0; i < assets.size(); i++) {
		Dictionary asset = assets[i];
		if (int64_t(asset.get("id", 0)) == asset_id) {
			String name = asset.get("name", "");
			if (is_empty_or_null(name)) {
				continue;
			}
			String download_url = asset.get("browser_download_url", "");
			String download_file = gdre::remove_url_query_params(download_url).get_file();
			if (download_file.is_empty()) {
				download_file = name.get_file();
			}
			if (gdre::is_path_archive(download_file)) {
				if (is_empty_or_null(download_url)) {
					continue;
				}
				String tag_name = release.get("tag_name", "");

				ReleaseInfo release_info;
				release_info.plugin_source = get_plugin_name();
				release_info.primary_id = release_id;
				release_info.secondary_id = asset_id;
				release_info.version = tag_name;
				release_info.release_date = asset.get("created_at", "");
				if (!release_info.release_date.is_empty()) {
					// parse the date string and rewrite it to the standard format
					release_info.release_date = normalize_release_date(release_info.release_date);
					// Time:: doesn't parse the timezone offset, so we need to parse it manually
				}
				String sha256_sum = asset.get("digest", "");
				if (sha256_sum.begins_with("sha256:")) {
					sha256_sum = sha256_sum.trim_prefix("sha256:");
				} else {
					sha256_sum = "";
				}
				release_info.sha256_sum = sha256_sum;

				release_info.download_url = download_url;
				release_info.repository_url = get_repo_url(plugin_name);

				return release_info;
			}
		}
	}

	return ReleaseInfo();
}

Vector<Dictionary> GitHubSource::get_list_of_releases(const String &plugin_name, Error &r_error) {
	r_error = recache_release_list(plugin_name);
	if (r_error != OK) {
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

Dictionary GitHubSource::get_release_dict(const String &plugin_name, int64_t release_id, Error &r_error) {
	r_error = recache_release_list(plugin_name);
	if (r_error != OK) {
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

Vector<Pair<int64_t, int64_t>> GitHubSource::get_plugin_version_numbers(const String &plugin_name, Error &r_connection_error) {
	auto thing = get_list_of_releases(plugin_name, r_connection_error);
	ERR_FAIL_COND_V_MSG(r_connection_error != OK, {}, "Failed to get list of releases for plugin " + plugin_name);
	Vector<Pair<int64_t, int64_t>> release_asset_pairs;
	for (auto &release : thing) {
		auto tag = release.get("tag_name", "");
		if (should_skip_tag(plugin_name, tag)) {
			continue;
		}
		int64_t release_id = release.get("id", 0);
		Array assets = release.get("assets", Array());
		for (auto &asset : assets) {
			if (should_skip_release(plugin_name, ((Dictionary)asset).get("browser_download_url", ""))) {
				continue;
			}
			int64_t asset_id = ((Dictionary)asset).get("id", 0);
			release_asset_pairs.push_back({ release_id, asset_id });
		}
	}
	return release_asset_pairs;
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
	fa->flush();
	d.clear();
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

Vector<ReleaseInfo> GitHubSource::find_release_infos_by_tag(const String &plugin_name, const String &tag, Error &r_connection_error) {
	Vector<Dictionary> releases = get_list_of_releases(plugin_name, r_connection_error);
	ERR_FAIL_COND_V_MSG(r_connection_error != OK, {}, "Failed to get list of releases for plugin " + plugin_name);
	Vector<ReleaseInfo> release_infos;
	for (auto &release : releases) {
		int64_t release_id = release.get("id", 0);
		if (release_id <= 0) {
			return {};
		}
		String tag_name = release.get("tag_name", "");
		if (tag_name == tag) {
			Array assets = release.get("assets", {});
			for (int i = 0; i < assets.size(); i++) {
				Dictionary asset = assets[i];
				if (should_skip_release(plugin_name, ((Dictionary)asset).get("browser_download_url", ""))) {
					continue;
				}
				int64_t asset_id = ((Dictionary)asset).get("id", 0);
				if (asset_id == 0) {
					continue;
				}
				auto rel_info = get_release_info(plugin_name, release_id, asset_id, r_connection_error);
				ERR_FAIL_COND_V_MSG(r_connection_error, {}, vformat("Failed to get release info for release %d asset %d", release_id, asset_id));
				if (rel_info.is_valid()) {
					release_infos.push_back(rel_info);
				}
			}
		}
	}
	return release_infos;
}

void GitHubSource::clear_cache() {
	MutexLock lock(cache_mutex);
	release_cache.clear();
}

Error GitHubSource::make_request(const String &url, const Vector<String> &extra_headers, Vector<uint8_t> &response) {
	// more retries for github since it's
	Error err = HTTPRequester::wget_sync(url, response, 15, 5, extra_headers);
	if (err == ERR_UNAUTHORIZED) { // github passes back a 401 Unauthorized error if the rate limit is exceeded; we'll treat this as a rate limit error
		err = ERR_BUSY;
	}
	return err;
}
