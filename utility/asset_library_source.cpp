#include "asset_library_source.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "utility/common.h"
#include "utility/gdre_settings.h"
#include "utility/glob.h"
#include "utility/plugin_manager.h"
#include "utility/task_manager.h"
HashMap<String, String> AssetLibrarySource::GODOT_VERSION_RELEASE_DATES = {
	{ "2.0", "2016-02-23" },
	{ "2.1", "2016-09-08" },
	{ "3.0", "2018-01-29" },
	{ "3.1", "2019-03-13" },
	{ "3.2", "2020-01-29" },
	{ "3.3", "2021-04-22" },
	{ "3.4", "2021-11-06" },
	{ "3.5", "2022-08-05" },
	{ "3.6", "2024-09-09" },
	{ "4.0", "2023-03-01" },
	{ "4.1", "2023-07-06" },
	{ "4.2", "2023-11-30" },
	{ "4.3", "2024-08-15" },
	{ "4.4", "2025-01-22" },
};

Vector<Dictionary> AssetLibrarySource::search_for_assets(const String &plugin_name, int ver_major) {
	static const Vector<String> _GODOT_VERSIONS = { "2.99", "3.99", "4.99" };
	Vector<String> godot_versions;
	if (ver_major == 0) {
		godot_versions = _GODOT_VERSIONS;
	} else {
		godot_versions.push_back(_GODOT_VERSIONS[ver_major - 2]);
	}

	static const String URL_TEMPLATE = "https://godotengine.org/asset-library/api/asset?type=addon&filter={0}&godot_version={1}&max_results={2}&page={3}";
	Array assets;

	for (const auto &godot_version : godot_versions) {
		int page = 0;
		int pages = 1000;
		for (page = 0; page < pages; page++) {
			String request_url = URL_TEMPLATE.replace("{0}", plugin_name)
										 .replace("{1}", godot_version)
										 .replace("{2}", "500")
										 .replace("{3}", itos(page));
			Vector<uint8_t> response;
			Error err = gdre::wget_sync(request_url, response);
			if (err) {
				break;
			}
			String response_str;
			response_str.append_utf8((const char *)response.ptr(), response.size());
			Dictionary dict = JSON::parse_string(response_str);
			if (dict.is_empty()) {
				break;
			}
			pages = dict.get("pages", {});
			Array page_assets = dict.get("result", {});
			if (page_assets.is_empty()) {
				break;
			}
			assets.append_array(page_assets);
		}
	}
	return gdre::array_to_vector<Dictionary>(assets);
}

Vector<int> AssetLibrarySource::search_for_asset_ids(const String &plugin_name, int ver_major) {
	auto assets = search_for_assets(plugin_name, ver_major);
	Vector<int> asset_ids;
	for (int i = 0; i < assets.size(); i++) {
		Dictionary asset = assets[i];
		if (!asset.has("asset_id")) {
			continue;
		}
		asset_ids.push_back(asset.get("asset_id", 0));
	}
	asset_ids.sort();
	return asset_ids;
}

Vector<Dictionary> AssetLibrarySource::get_list_of_edits(int asset_id) {
	int page = 0;
	int pages = 1000;
	double now = OS::get_singleton()->get_unix_time();
	Vector<Dictionary> edits_vec;

	{
		MutexLock lock(cache_mutex);
		if (edit_list_cache.has(asset_id)) {
			auto &cache = edit_list_cache[asset_id];
			if (!is_cache_expired(cache.retrieved_time)) {
				return cache.edit_list;
			}
		}
	}

	for (page = 0; page < pages; page++) {
		String URL = "https://godotengine.org/asset-library/api/asset/edit?asset={0}&status=accepted&page={1}";
		URL = URL.replace("{0}", itos(asset_id)).replace("{1}", itos(page));

		Vector<uint8_t> response;
		Error err = gdre::wget_sync(URL, response);
		if (err) {
			break;
		}
		String response_str;
		response_str.append_utf8((const char *)response.ptr(), response.size());
		Dictionary response_obj = JSON::parse_string(response_str);
		if (response_obj.is_empty()) {
			break;
		}
		Array edits = response_obj.get("result", {});
		pages = response_obj.get("pages", {});
		if (edits.is_empty()) {
			break;
		}
		for (int i = 0; i < edits.size(); i++) {
			edits_vec.push_back(edits[i]);
		}
	}

	{
		MutexLock lock(cache_mutex);
		edit_list_cache[asset_id] = { now, (uint64_t)asset_id, edits_vec };
	}

	return edits_vec;
}

Dictionary AssetLibrarySource::get_edit(int edit_id) {
	{
		constexpr time_t EDIT_EXPIRY_TIME = 24 * 3600; // 1 day in seconds
		MutexLock lock(cache_mutex);
		if (edit_cache.has(edit_id)) {
			auto &cache = edit_cache[edit_id];
			if (!cache.retrieved_time + EDIT_EXPIRY_TIME <= OS::get_singleton()->get_unix_time()) {
				return cache.edit;
			}
		}
	}

	String URL = "https://godotengine.org/asset-library/api/asset/edit/{0}";
	URL = URL.replace("{0}", itos(edit_id));
	Vector<uint8_t> response;
	Error err = gdre::wget_sync(URL, response);
	if (err || response.size() == 0) {
		return Dictionary();
	}
	String response_str;
	response_str.append_utf8((const char *)response.ptr(), response.size());
	Dictionary response_obj = JSON::parse_string(response_str);
	if (response_obj.is_empty()) {
		return Dictionary();
	}

	{
		MutexLock lock(cache_mutex);
		edit_cache[edit_id] = {
			OS::get_singleton()->get_unix_time(),
			(uint64_t)edit_id,
			response_obj
		};
	}

	return response_obj;
}

namespace {
bool is_empty_or_null(const String &str) {
	return str.is_empty() || str == "<null>";
}
} //namespace

ReleaseInfo AssetLibrarySource::get_release_info(const String &plugin_name, const String &version_key) {
	auto parts = version_key.split("-", true, 1);
	if (parts.size() != 2) {
		return ReleaseInfo();
	}
	auto asset_id = parts[0].to_int();
	auto version_string = parts[1];

	auto edits = get_list_of_edits(asset_id);
	for (int i = 0; i < edits.size(); i++) {
		Dictionary edit = edits[i];
		int edit_id = int(edit.get("edit_id", {}));
		if (edit.get("version_string", "") == version_string) {
			Dictionary edit_data = get_edit(edit_id);
			if (edit_data.is_empty()) {
				continue;
			}

			String godot_version = edit.get("godot_version", "");
			String submit_date = edit.get("submit_date", "");
			String plugin_name_from_edit = edit.get("title", "");
			if (!is_empty_or_null(submit_date)) {
				submit_date = submit_date.split(" ")[0];
			}
			String version = edit_data.get("version_string", "");
			String download_commit = edit_data.get("download_commit", "");
			if (is_empty_or_null(version) || is_empty_or_null(download_commit)) {
				continue;
			}
			if (!download_commit.begins_with("http")) {
				String download_url = edit_data.get("download_url", "");
				if (is_empty_or_null(download_url) || !download_url.begins_with("http")) {
					continue;
				}
				download_commit = download_url;
			}
			if (edit_data.has("godot_version")) {
				String _ver = edit_data.get("godot_version", "");
				if (!is_empty_or_null(_ver)) {
					godot_version = _ver;
				}
			}
			if (is_empty_or_null(godot_version)) {
				WARN_PRINT("Godot version not found for plugin " + plugin_name_from_edit + " version: " + version);
			}
			String release_date = GODOT_VERSION_RELEASE_DATES.has(godot_version) ? GODOT_VERSION_RELEASE_DATES.get(godot_version) : "";
			String current_version = godot_version;
			if (is_empty_or_null(release_date)) {
				print_line("Godot version " + godot_version + " not found in release dates");
			} else {
				if (submit_date < release_date) {
					for (auto &E : GODOT_VERSION_RELEASE_DATES) {
						const String &engine_version = E.key;
						const String &date = E.value;
						if (current_version[0] == engine_version[0] && date <= submit_date && current_version >= engine_version) {
							godot_version = engine_version;
						}
					}
				}
			}

			ReleaseInfo release_info;
			release_info.plugin_source = get_plugin_name();
			release_info.primary_id = asset_id;
			release_info.secondary_id = edit_id;
			release_info.version = version;
			release_info.engine_ver_major = !godot_version.is_empty() ? godot_version.split(".")[0].to_int() : 0;
			release_info.release_date = submit_date;
			release_info.download_url = download_commit;

			return release_info;
		}
	}

	return ReleaseInfo();
}

Vector<String> AssetLibrarySource::get_version_strings_for_asset(int asset_id) {
	Vector<String> versions;
	auto edits = get_list_of_edits(asset_id);
	for (int i = 0; i < edits.size(); i++) {
		String version = edits[i].get("version_string", "");
		if (version.is_empty() || version == "<null>" || versions.has(version)) {
			continue;
		}
		versions.push_back(version);
	}
	return versions;
}

Vector<String> AssetLibrarySource::get_plugin_version_numbers(const String &plugin_name) {
	auto asset_ids = search_for_asset_ids(plugin_name);
	Vector<String> versions;
	for (auto asset_id : asset_ids) {
		auto new_versions = get_version_strings_for_asset(asset_id);
		for (auto &version : new_versions) {
			versions.append(itos(asset_id) + "-" + version);
		}
	}
	return versions;
}

void AssetLibrarySource::load_edit_list_cache() {
	MutexLock lock(cache_mutex);
	String edit_list_cache_file = PluginManager::get_plugin_cache_path().path_join("asset_lib_edit_list_release_cache.json");
	if (!FileAccess::exists(edit_list_cache_file)) {
		return;
	}
	auto file = FileAccess::open(edit_list_cache_file, FileAccess::READ);
	if (file.is_null()) {
		return;
	}
	Dictionary json = JSON::parse_string(file->get_as_text());
	for (auto &E : json) {
		edit_list_cache[E.key] = EditListCache::from_json(E.value);
	}
	file->close();
}

void AssetLibrarySource::load_edit_cache() {
	MutexLock lock(cache_mutex);
	String edit_cache_file = PluginManager::get_plugin_cache_path().path_join("asset_lib_edits_release_cache.json");
	if (!FileAccess::exists(edit_cache_file)) {
		return;
	}
	auto file = FileAccess::open(edit_cache_file, FileAccess::READ);
	if (file.is_null()) {
		return;
	}
	Dictionary json = JSON::parse_string(file->get_as_text());
	for (auto &E : json) {
		edit_cache[E.key] = EditCache::from_json(E.value);
	}
	file->close();
}

void AssetLibrarySource::load_cache_internal() {
	load_edit_list_cache();
	load_edit_cache();
}

void AssetLibrarySource::save_edit_list_cache() {
	MutexLock lock(cache_mutex);
	String edit_list_cache_file = PluginManager::get_plugin_cache_path().path_join("asset_lib_edit_list_release_cache.json");
	auto file = FileAccess::open(edit_list_cache_file, FileAccess::WRITE);
	if (file.is_null()) {
		return;
	}
	Dictionary json;
	for (auto &E : edit_list_cache) {
		json[E.key] = E.value.to_json();
	}
	file->store_string(JSON::stringify(json));
	file->close();
	json.clear();
}

void AssetLibrarySource::save_edit_cache() {
	MutexLock lock(cache_mutex);
	String edit_cache_file = PluginManager::get_plugin_cache_path().path_join("asset_lib_edits_release_cache.json");
	auto file = FileAccess::open(edit_cache_file, FileAccess::WRITE);
	if (file.is_null()) {
		return;
	}

	Dictionary json;
	for (auto &E : edit_cache) {
		json[E.key] = E.value.to_json();
	}
	file->store_string(JSON::stringify(json));
	file->close();
	json.clear();
}

void AssetLibrarySource::save_cache() {
	save_edit_list_cache();
	save_edit_cache();
}

bool AssetLibrarySource::handles_plugin(const String &plugin_name) {
	return true;
}

String AssetLibrarySource::get_plugin_name() {
	return "asset_lib";
}

EditListCache EditListCache::from_json(const Dictionary &json) {
	EditListCache cache;
	cache.retrieved_time = json.get("retrieved_time", 0);
	cache.asset_id = json.get("asset_id", 0);
	cache.edit_list = gdre::array_to_vector<Dictionary>(json.get("edit_list", Array()));
	return cache;
}

Dictionary EditListCache::to_json() const {
	return {
		{ "retrieved_time", retrieved_time },
		{ "asset_id", asset_id },
		{ "edit_list", gdre::vector_to_array(edit_list) }
	};
}

EditCache EditCache::from_json(const Dictionary &json) {
	EditCache cache;
	cache.retrieved_time = json.get("retrieved_time", 0);
	cache.edit_id = json.get("edit_id", 0);
	cache.edit = json.get("edit", Dictionary());
	return cache;
}

Dictionary EditCache::to_json() const {
	return {
		{ "retrieved_time", retrieved_time },
		{ "edit_id", edit_id },
		{ "edit", edit }
	};
}