#include "plugin_manager.h"

#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "plugin_manager/plugin_info.h"
#include "plugin_source.h"
#include "utility/common.h"
#include "utility/gdre_settings.h"
#include "utility/glob.h"
#include "utility/http_requester.h"
#include "utility/task_manager.h"

Ref<PluginSource> PluginManager::sources[MAX_SOURCES];
int PluginManager::source_count = 0;
bool PluginManager::prepopping = false;
HashMap<String, PluginVersion> PluginManager::plugin_version_cache;
Mutex PluginManager::plugin_version_cache_mutex;
// known bad plugin versions and their replacements; these had better be prepopped.
HashMap<String, Pair<String, String>> PluginManager::known_bad_plugin_versions = {
	{ "godotsteam", Pair<String, String>("v4.4.1-gde", "v4.4.2-gde") },
};

String PluginManager::get_plugin_download_cache_path() {
	return GDRESettings::get_gdre_user_path().path_join("download_cache");
}

String PluginManager::get_plugin_cache_path() { // check if OS has the environment variable "GDRE_PLUGIN_CACHE_DIR" set
	// if it is set, use that as the cache folder
	// This is a hack to help prepopulate the cache for releases
	if (OS::get_singleton()->has_environment(PLUGIN_CACHE_ENV_VAR)) {
		return OS::get_singleton()->get_environment(PLUGIN_CACHE_ENV_VAR);
	}
	return GDRESettings::get_gdre_user_path().path_join("plugin_cache").path_join("v" + itos(CACHE_VERSION));
}

String PluginManager::get_download_path_for_release(const ReleaseInfo &p_release_info) {
	String zip_name = p_release_info.get_download_file();
	if (zip_name.is_empty()) {
		String ext = "zip";
		if (auto pos = p_release_info.download_url.find(".tar"); pos != -1) {
			ext = p_release_info.download_url.substr(pos);
		}
		zip_name = vformat("plugin.%s", ext);
	}
	String filename = p_release_info.get_cache_key() + "-" + zip_name;
	String save_path = get_plugin_download_cache_path().path_join(filename);
	return save_path;
}

TaskManager::DownloadTaskID PluginManager::start_download_plugin_to_cache(const ReleaseInfo &p_release_info, const String &p_sha256_sum, String &r_save_path, Error &r_error) {
	String save_path = get_download_path_for_release(p_release_info);
	if (FileAccess::exists(save_path)) {
		String reference_sha256 = p_sha256_sum;
		if (reference_sha256.is_empty() && FileAccess::exists(save_path + ".sha256")) {
			reference_sha256 = FileAccess::get_file_as_string(save_path + ".sha256").strip_edges();
		}
		if (reference_sha256 == gdre::get_sha256(save_path)) {
			r_error = OK;
			r_save_path = save_path;
			if (is_prepopping()) {
				print_line("Plugin already cached, skipping download: " + save_path.get_file());
			}
			return TaskManager::get_singleton()->add_fake_download_task(p_release_info.download_url, save_path);
		}
	}
	r_error = gdre::ensure_dir(save_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(r_error != OK, -1, "Failed to ensure directory exists: " + save_path.get_base_dir());
	r_save_path = save_path;
	if (is_prepopping()) {
		print_line("Downloading plugin to populate cache: " + p_release_info.download_url);
		r_error = HTTPRequester::download_file_sync(p_release_info.download_url, save_path);
		ERR_FAIL_COND_V_MSG(r_error != OK, -1, "Failed to download plugin to cache: " + p_release_info.download_url);
		if (auto fa = FileAccess::open(save_path + ".sha256", FileAccess::WRITE); fa.is_valid()) {
			fa->store_string(gdre::get_sha256(save_path));
		}
		return TaskManager::get_singleton()->add_fake_download_task(p_release_info.download_url, save_path);
	}
	return TaskManager::get_singleton()->add_download_task(p_release_info.download_url, save_path, false, true);
}

TaskManager::DownloadTaskID PluginManager::download_plugin(const PluginVersion &p_plugin_version, String &r_save_path, Error &r_error) {
	return start_download_plugin_to_cache(p_plugin_version.release_info, p_plugin_version.archive_sha256, r_save_path, r_error);
}

Ref<PluginSource> PluginManager::get_source(const String &plugin_name) {
	// Check if we already have a source for this plugin
	Ref<PluginSource> default_source;
	for (int i = 0; i < source_count; ++i) {
		if (sources[i]->handles_plugin(plugin_name)) {
			if (sources[i]->is_default()) {
				default_source = sources[i];
				continue;
			}
			return sources[i];
		}
	}
	return default_source;
}

void PluginManager::register_source(Ref<PluginSource> source, bool at_front) {
	if (source_count < MAX_SOURCES) {
		if (at_front) {
			for (int i = source_count; i > 0; --i) {
				sources[i] = sources[i - 1];
			}
			sources[0] = source;
		} else {
			sources[source_count] = source;
		}
		source_count++;
	}
}

void PluginManager::unregister_source(Ref<PluginSource> source) {
	// Find source
	int i = 0;
	for (; i < source_count; ++i) {
		if (sources[i] == source) {
			break;
		}
	}
	ERR_FAIL_COND(i >= source_count); // Not found
	for (int j = i; j < source_count - 1; ++j) {
		sources[j] = sources[j + 1];
	}
	sources[source_count - 1].unref();
	--source_count;
}

PluginVersion PluginManager::get_plugin_version_for_key(const String &plugin_name, int64_t primary_id, int64_t secondary_id, Error &r_error) {
	Ref<PluginSource> source = get_source(plugin_name);
	ERR_FAIL_COND_V_MSG(source.is_null(), PluginVersion::invalid(), "No source found for plugin: " + plugin_name);

	r_error = OK;
	// Get ReleaseInfo from the source
	ReleaseInfo release_info = source->get_release_info(plugin_name, primary_id, secondary_id, r_error);
	ERR_FAIL_COND_V_MSG(r_error != OK, PluginVersion::invalid(), vformat("Failed to get release info for plugin %s primary id %d secondary id %d", plugin_name, primary_id, secondary_id));
	if (!release_info.is_valid()) {
		return PluginVersion::invalid(); // No release info available
	}

	return _get_plugin_version_for_current_release_info(release_info, r_error);
}

PluginVersion PluginManager::_get_plugin_version_for_current_release_info(const ReleaseInfo &release_info, Error &r_error) {
	// Generate cache key
	r_error = OK;
	String cache_key = release_info.get_cache_key();

	{
		MutexLock lock(plugin_version_cache_mutex);
		if (plugin_version_cache.has(cache_key) && plugin_version_cache[cache_key].release_info == release_info) {
			return plugin_version_cache[cache_key];
		}
	}

	// Populate PluginVersion from ReleaseInfo
	PluginVersion plugin_version = populate_plugin_version_from_release(release_info, r_error);
	if (!plugin_version.is_valid()) {
		return PluginVersion::invalid(); // Return empty version on error
	}

	// Cache the result
	cache_plugin_version(cache_key, plugin_version);

	return plugin_version;
}

PluginVersion PluginManager::get_plugin_info(const String &plugin_name, const Vector<String> &hashes) {
	Ref<PluginSource> source = get_source(plugin_name);
	ERR_FAIL_COND_V_MSG(source.is_null(), PluginVersion::invalid(), "No source found for plugin: " + plugin_name);

	PluginVersion found_version;
	// First, check all cached PluginVersions for this plugin
	{
		MutexLock lock(plugin_version_cache_mutex);
		String first_version;
		String replacement_version;
		for (auto &E : plugin_version_cache) {
			const PluginVersion &cached_version = E.value;

			// Check if this cached version is for the requested plugin
			if (cached_version.is_valid() && cached_version.plugin_name == plugin_name) {
				// Check if any of the hashes match
				if (cached_version.bin_hashes_match(hashes)) {
					print_line("Found matching plugin in cache: " + plugin_name + ", version: " + cached_version.release_info.version + ", download url: " + cached_version.release_info.download_url);
					if (known_bad_plugin_versions.has(plugin_name) && known_bad_plugin_versions[plugin_name].first == cached_version.release_info.version) {
						first_version = cached_version.release_info.version;
						replacement_version = known_bad_plugin_versions[plugin_name].second;
						break;
					}
					found_version = cached_version;
					break;
				}
			}
		}
		if (!replacement_version.is_empty()) {
			for (auto &E : plugin_version_cache) {
				const PluginVersion &cached_version = E.value;
				if (cached_version.is_valid() && cached_version.plugin_name == plugin_name && cached_version.release_info.version == replacement_version) {
					print_line("Found known bad plugin version: " + plugin_name + ", version: " + first_version + ", replacing with: " + replacement_version);
					found_version = cached_version;
				}
			}
			ERR_FAIL_COND_V_MSG(!found_version.is_valid(), PluginVersion::invalid(), "!!!!!!!!!\nNO REPLACEMENT FOUND\n!!!!!!!!!!!");
		}
	}
	if (found_version.is_valid()) {
		// get the release info from the source
		if (source->get_plugin_name() == found_version.release_info.plugin_source) {
			Error err = OK;
			auto current_info = source->get_release_info(plugin_name, found_version.release_info.primary_id, found_version.release_info.secondary_id, err);
			ERR_FAIL_COND_V_MSG(err != OK, found_version, vformat("Failed to verify release info for plugin %s primary id %d secondary id %d", plugin_name, found_version.release_info.primary_id, found_version.release_info.secondary_id));
			if (current_info.is_valid() && current_info == found_version.release_info) {
				return found_version;
			}
		}
		// otherwise, we need to find the correct release info
		print_line(vformat("Cache for plugin %s, version %s does not match current release info, recaching...", plugin_name, found_version.release_info.version));
		Error err = OK;
		auto release_infos = source->find_release_infos_by_tag(plugin_name, found_version.release_info.version, err);
		ERR_FAIL_COND_V_MSG(err != OK, found_version, vformat("Failed to find release infos for plugin %s tag %s", plugin_name, found_version.release_info.version));
		if (!release_infos.is_empty()) {
			for (auto &current_info : release_infos) {
				PluginVersion plugin_version = _get_plugin_version_for_current_release_info(current_info, err);
				ERR_CONTINUE_MSG(err != OK, vformat("Failed to populate plugin version for plugin %s primary id %d secondary id %d", plugin_name, current_info.primary_id, current_info.secondary_id));
				if (plugin_version.is_valid() && plugin_version.bin_hashes_match(hashes)) {
					String previous_cache_key = found_version.release_info.get_cache_key();
					String cache_key = plugin_version.release_info.get_cache_key();
					if (previous_cache_key != cache_key) {
						erase_cached_plugin_version(previous_cache_key);
					}
					cache_plugin_version(cache_key, plugin_version);
					return plugin_version;
				}
			}
		}
	}
	Error err = OK;
	// If no cached versions match, get all release info and populate PluginVersions
	auto version_keys = source->get_plugin_version_numbers(plugin_name, err);
	ERR_FAIL_COND_V_MSG(err != OK, PluginVersion::invalid(), "Failed to get plugin version numbers for plugin " + plugin_name);

	for (auto &version_key : version_keys) {
		if (TaskManager::get_singleton()->is_current_task_canceled()) {
			break;
		}
		Error err = OK;
		ReleaseInfo release_info = source->get_release_info(plugin_name, version_key.first, version_key.second, err);
		ERR_CONTINUE_MSG(err != OK, vformat("Failed to get release info for plugin %s primary id %d secondary id %d", plugin_name, version_key.first, version_key.second));
		if (!release_info.is_valid()) {
			continue; // Skip if no release info available
		}
		auto plugin_version = _get_plugin_version_for_current_release_info(release_info, err);
		ERR_CONTINUE_MSG(err != OK, vformat("Failed to populate plugin version for plugin %s primary id %d secondary id %d", plugin_name, version_key.first, version_key.second));
		if (plugin_version.is_valid() && plugin_version.bin_hashes_match(hashes)) {
			print_line("Found matching plugin after population: " + plugin_name + ", version: " + plugin_version.release_info.version + ", download url: " + plugin_version.release_info.download_url);
			return plugin_version;
		}
	}
	print_line(vformat("No plugin download url found for plugin %s, hashes: %s", plugin_name, String(", ").join(hashes)));

	return PluginVersion::invalid();
}

Vector<PluginVersion> PluginManager::get_possibles_from_deps(const String &plugin_name, const Ref<GodotVer> engine_version, const Vector<PluginBin> &deps) {
	Vector<PluginVersion> possibles;
	{
		MutexLock lock(plugin_version_cache_mutex);
		for (auto &E : plugin_version_cache) {
			const PluginVersion &cached_version = E.value;
			if (cached_version.is_valid() && cached_version.plugin_name == plugin_name) {
				possibles.push_back(cached_version);
			}
		}
	}
	Vector<int> to_remove;
	for (int i = 0; i < possibles.size(); i++) {
		int matching_count = 0;
		const PluginVersion &cached_version = possibles[i];
		if (!cached_version.is_compatible(engine_version)) {
			to_remove.push_back(i);
			continue;
		}
		for (const auto &gdext : cached_version.gdexts) {
			for (const auto &bin : gdext.dependencies) {
				for (const auto &dep : deps) {
					if (bin.name == dep.name && bin.sha256 == dep.sha256) {
						matching_count++;
						break;
					}
				}
			}
		}
		if (matching_count != deps.size()) {
			to_remove.push_back(i);
		}
	}
	for (int i = to_remove.size() - 1; i >= 0; i--) {
		possibles.remove_at(to_remove[i]);
	}
	return possibles;
}

void PluginManager::load_cache() {
	auto start_time = OS::get_singleton()->get_ticks_msec();
	Dictionary d;
	if (FileAccess::exists(STATIC_PLUGIN_CACHE_PATH)) {
		load_plugin_version_cache_file(STATIC_PLUGIN_CACHE_PATH, true);
	}
	for (int i = 0; i < source_count; ++i) {
		sources[i]->load_cache();
	}

	// Load PluginVersion cache
	load_plugin_version_cache();
	auto end_time = OS::get_singleton()->get_ticks_msec();
	print_verbose(vformat("Loaded plugin cache in %dms", end_time - start_time));
}

void PluginManager::save_cache() {
	// Save PluginVersion cache
	save_plugin_version_cache();
	for (int i = 0; i < source_count; ++i) {
		sources[i]->save_cache();
	}
}

namespace {
struct PrePopToken {
	String plugin_name;
	Ref<PluginSource> source;
	Pair<int64_t, int64_t> version;
	Error error = OK;
};

struct PrePopTask {
	String describe_task(uint32_t index, PrePopToken *tokens) {
		return vformat("%s version: %d %d", tokens[index].plugin_name, tokens[index].version.first, tokens[index].version.second);
	}

	void do_task(uint32_t index, PrePopToken *tokens) {
		Error &err = tokens[index].error;
		auto &token = tokens[index];
		// Use the new workflow: get ReleaseInfo and populate PluginVersion
		ReleaseInfo release_info = token.source->get_release_info(token.plugin_name, token.version.first, token.version.second, err);
		ERR_FAIL_COND_MSG(err != OK, vformat("Failed to get release info for plugin %s primary id %d secondary id %d", token.plugin_name, token.version.first, token.version.second));
		if (release_info.is_valid()) {
			String cache_key = release_info.get_cache_key();
			PluginVersion cached_version = PluginManager::get_cached_plugin_version(cache_key);
			bool valid = cached_version.is_valid();
			if (valid && cached_version.release_info != release_info) {
				print_line("Cached version does not match release info for plugin " + token.plugin_name + " version: " + release_info.version);
				valid = false;
			}
			if (!valid) {
				PluginVersion plugin_version = PluginManager::populate_plugin_version_from_release(release_info, err);
				ERR_FAIL_COND_MSG(err != OK, vformat("Failed to populate plugin version for plugin %s primary id %d secondary id %d", token.plugin_name, token.version.first, token.version.second));
				if (plugin_version.is_valid()) {
					PluginManager::cache_plugin_version(cache_key, plugin_version);
				}
			}
		}
	}
};
} //namespace

Error PluginManager::prepop_cache(const Vector<String> &plugin_names, const String &output_path) {
	bool multithread = !GDREConfig::get_singleton()->get_setting("force_single_threaded", false);
	prepopping = true;
	String plugin_names_str = String(", ").join(plugin_names);
	print_line("Prepopulating cache for " + plugin_names_str);
	print_line("Plugin dir: " + get_plugin_cache_path());
	Vector<PrePopToken> tokens;
	for (int i = 0; i < plugin_names.size(); i++) {
		auto &plugin_name = plugin_names[i];
		auto source = get_source(plugin_name);
		if (source.is_null()) {
			continue;
		}
		Error err = OK;
		auto versions = source->get_plugin_version_numbers(plugin_name, err);
		ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Failed to get plugin version numbers for plugin %s", plugin_name));
		for (auto &version : versions) {
			PrePopToken token;
			token.plugin_name = plugin_name;
			token.source = source;
			token.version = version;
			tokens.push_back(token);
		}
	}
	PrePopTask task;
	Error err = OK;
	if (multithread) {
		gdre::shuffle_vector(tokens);
		err = TaskManager::get_singleton()->run_multithreaded_group_task(
				&task,
				&PrePopTask::do_task,
				tokens.ptrw(),
				tokens.size(),
				&PrePopTask::describe_task,
				"GDRESettings::prepop_plugin_cache_gh",
				"Prepopulating plugin cache",
				true,
				-1,
				true);
	} else {
		for (int i = 0; i < tokens.size(); i++) {
			task.do_task(i, tokens.ptrw());
			if (tokens[i].error != OK) {
				break;
			}
		}
	}
	save_plugin_version_static_cache(output_path);
	if (err != OK) {
		return err;
	}
	for (int i = 0; i < tokens.size(); i++) {
		if (tokens[i].error != OK) {
			return tokens[i].error;
		}
	}

	print_plugin_cache();

	return OK;
}

bool PluginManager::is_prepopping() {
	return prepopping;
}

PluginVersion PluginManager::get_cached_plugin_version(const String &cache_key) {
	MutexLock lock(plugin_version_cache_mutex);
	if (plugin_version_cache.has(cache_key)) {
		return plugin_version_cache[cache_key];
	}
	return PluginVersion::invalid();
}

void PluginManager::erase_cached_plugin_version(const String &cache_key) {
	MutexLock lock(plugin_version_cache_mutex);
	plugin_version_cache.erase(cache_key);
}

void PluginManager::cache_plugin_version(const String &cache_key, const PluginVersion &version) {
	MutexLock lock(plugin_version_cache_mutex);
	plugin_version_cache[cache_key] = version;
}

PluginVersion PluginManager::populate_plugin_version_from_release(const ReleaseInfo &release_info, Error &r_error) {
	PluginVersion version;
	version.release_info = release_info;
	version.plugin_name = ""; // Will be populated during analysis
	version.min_godot_version = "";
	version.max_godot_version = "";
	version.base_folder = "";
	version.archive_sha256 = release_info.sha256_sum;

	// Download and analyze the plugin using the new method
	r_error = populate_plugin_version_hashes(version);
	if (r_error != OK) {
		return PluginVersion::invalid(); // Return empty version on error
	}

	return version;
}

Error PluginManager::populate_plugin_version_hashes(PluginVersion &plugin_version) {
	auto temp_folder = GDRESettings::get_singleton()->get_gdre_tmp_path();
	String url = plugin_version.release_info.download_url;
	String new_temp_foldr = temp_folder.path_join(plugin_version.release_info.get_cache_key());
	String zip_path;
	if (!is_prepopping()) {
		print_line("Downloading plugin to populate cache: " + url);
	}

	Error err = OK;
	auto task_id = start_download_plugin_to_cache(plugin_version.release_info, plugin_version.archive_sha256, zip_path, err);
	ERR_FAIL_COND_V_MSG(err || task_id == -1, err, "Failed to start download task for plugin " + plugin_version.release_info.download_url);
	err = TaskManager::get_singleton()->wait_for_download_task_completion(task_id);

	if (err) {
		if (err == ERR_FILE_NOT_FOUND) {
			ERR_FAIL_V_MSG(err, "Failed plugin download (404): " + url);
		} else if (err == ERR_UNAUTHORIZED) {
			ERR_FAIL_V_MSG(err, "Failed plugin download (401): " + url);
		}
		return err;
	}

	plugin_version.size = FileAccess::get_size(zip_path);
	ERR_FAIL_COND_V_MSG(!gdre::is_path_archive(zip_path), ERR_FILE_CORRUPT, "File is not an archive: " + zip_path);

	auto close_and_remove_zip = [&]() {
		if (zip_path.is_empty() && zip_path.begins_with(get_plugin_download_cache_path())) {
			gdre::rimraf(zip_path);
		}
		gdre::rimraf(new_temp_foldr);
	};

	// just unzip the files to the folder
	String unzupped_path = new_temp_foldr.path_join("unzipped");
	err = gdre::unzip_file_to_dir(zip_path, unzupped_path);
	if (err) {
		close_and_remove_zip();
		return err;
	}

	String archive_sha256 = gdre::get_sha256(zip_path);
	if (!plugin_version.archive_sha256.is_empty() && archive_sha256 != plugin_version.archive_sha256) {
		WARN_PRINT("Archive SHA-256 mismatch: " + plugin_version.archive_sha256 + " != " + archive_sha256);
	}
	plugin_version.archive_sha256 = archive_sha256;

	auto files = gdre::get_recursive_dir_list(unzupped_path, {}, false, true);
	for (int64_t i = files.size() - 1; i >= 0; i--) {
		if (files[i].simplify_path().begins_with("__MACOSX")) {
			files.remove_at(i);
		}
	}
	String gd_ext_file = "";
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);

	// get all the gdexts
	HashMap<String, GDExtInfo> gdexts;
	String backup_plugin_name = "";
	for (int i = 0; i < files.size(); i++) {
		auto ipath = files[i];
		auto idx = ipath.to_lower().find("addons");
		if (backup_plugin_name.is_empty() && idx != -1 && !ipath.ends_with("/") && ipath.get_file() != "addons") {
			backup_plugin_name = ipath.substr(idx).simplify_path().get_slice("/", 1);
		}
		auto ext = files[i].get_extension().to_lower();
		if (ext == "gdextension" || ext == "gdnlib") {
			// get the path relative to the "addons" folder
			if (idx == -1) {
				// just get one path up from the file
				idx = ipath.get_base_dir().rfind("/") + 1;
				if (plugin_version.plugin_name.is_empty()) {
					plugin_version.plugin_name = ipath.get_file().get_basename();
				}
			} else {
				if (plugin_version.plugin_name.is_empty()) {
					plugin_version.plugin_name = ipath.substr(idx).simplify_path().get_slice("/", 1);
				}
			}
			auto rel_path = ipath.substr(idx);
			GDExtInfo gdext_info;
			gdext_info.relative_path = rel_path;
			if (plugin_version.base_folder.is_empty() && idx > 0) {
				plugin_version.base_folder = ipath.substr(0, idx);
			}
			gdexts[files[i]] = gdext_info;
		}
	}

	if (gdexts.size() == 0) {
		print_line("No gdexts found in plugin, skipping: " + url);
		if (plugin_version.plugin_name.is_empty()) {
			if (backup_plugin_name.is_empty()) {
				String zip_name = url.ends_with(".zip") ? url.get_file() : "plugin.zip";
				if (zip_name != "plugin.zip") {
					plugin_version.plugin_name = zip_name.get_basename();
				} else {
					// github.com/godotsteam/godotsteam/releases/download/thing.zip -> godotsteam
					plugin_version.plugin_name = url.trim_prefix("https://").trim_prefix("http://").get_slice("/", 2);
				}
			} else {
				plugin_version.plugin_name = backup_plugin_name;
			}
		}
		close_and_remove_zip();
		return OK;
	}

	print_line("Populating plugin version hashes for " + plugin_version.plugin_name + " version: " + plugin_version.release_info.version);

	bool first_min = true;
	bool first_max = true;
	for (auto &E : gdexts) {
		GDExtInfo &gdext_info = E.value;
		auto &gdext_path = E.key;
		String gdext_str = FileAccess::get_file_as_string(unzupped_path.path_join(gdext_path));
		ERR_FAIL_COND_V_MSG(gdext_str.is_empty(), ERR_FILE_CORRUPT, "Failed to get gdext string for " + gdext_path);
		Ref<ImportInfoGDExt> cf = memnew(ImportInfoGDExt);
		cf->load_from_string("res://" + gdext_info.relative_path, gdext_str);

		if (!cf->get_compatibility_minimum().is_empty()) {
			auto min = cf->get_compatibility_minimum();
			if (first_min || plugin_version.min_godot_version < min) {
				plugin_version.min_godot_version = min;
				first_min = false;
			}
			gdext_info.min_godot_version = min;
		}

		if (!cf->get_compatibility_maximum().is_empty()) {
			auto max = cf->get_compatibility_maximum();
			if (first_max || plugin_version.max_godot_version > max) {
				plugin_version.max_godot_version = max;
				first_max = false;
			}
			gdext_info.max_godot_version = max;
		}

		auto parse_bins = [unzupped_path](Vector<SharedObject> bins) {
			Vector<PluginBin> plugin_bins;
			HashSet<String> seen_paths;
			for (auto &elem : bins) {
				auto &lib = elem.path;
				String lib_search_path = lib.trim_prefix("res://").simplify_path();
				Vector<String> paths;

				while (paths.is_empty() && !lib_search_path.is_empty()) {
					paths = Glob::rglob(unzupped_path.path_join("**").path_join(lib_search_path), true);
					// pop off the LEFT part of the path, e.g. "addons/godotsteam/x11/libgodotsteam.so" -> "godotsteam/x11/libgodotsteam.so
					auto parts = lib_search_path.split("/", false, 1);
					if (parts.size() <= 1) {
						break;
					}
					lib_search_path = parts[1];
				}
				String real_path;
				for (auto &p : paths) {
					if (p.ends_with(lib.replace_first("res://", ""))) {
						real_path = p;
						break;
					}
				}

				if (real_path.is_empty()) {
					// just use the first path
					if (paths.size() > 0) {
						for (auto &p : paths) {
							if (!seen_paths.has(p)) {
								real_path = p;
								break;
							}
						}
						// just use the first path
						if (real_path.is_empty()) {
							real_path = paths[0];
						}
					}
				}
				seen_paths.insert(real_path);
				auto plugin_bin = PluginSource::get_plugin_bin(real_path, elem);
				plugin_bins.push_back(plugin_bin);
			}
			return plugin_bins;
		};

		gdext_info.bins = parse_bins(cf->get_libaries(false));
		gdext_info.dependencies = parse_bins(cf->get_dependencies(false));
		plugin_version.gdexts.push_back(gdext_info);
	}

	close_and_remove_zip();
	gdre::rimraf(new_temp_foldr);
	return OK;
}

void PluginManager::load_plugin_version_cache() {
	String cache_dir = get_plugin_cache_path().path_join("plugin_versions");
	if (!DirAccess::exists(cache_dir)) {
		return;
	}
	auto files = Glob::rglob(cache_dir.path_join("**/*.json"), true);
	for (auto &file : files) {
		load_plugin_version_cache_file(file, false);
	}
}

void PluginManager::load_plugin_version_cache_file(const String &cache_file, bool p_static_cached) {
	// this works with both the static and dynamic cache files
	auto file = FileAccess::open(cache_file, FileAccess::READ);
	if (file.is_null()) {
		ERR_PRINT("Failed to open plugin version cache file!");
		return;
	}

	String json = file->get_as_text();
	Dictionary data = JSON::parse_string(json);

	MutexLock lock(plugin_version_cache_mutex);
	for (auto &E : data.keys()) {
		Dictionary version_data = data[E];
		PluginVersion version = PluginVersion::from_json(version_data);
		if (version.is_valid()) {
			version.is_static_cached = p_static_cached;
			String cache_key = version.release_info.get_cache_key();
			plugin_version_cache[cache_key] = version;
		}
	}
}

void PluginManager::save_plugin_version_cache() {
	String cache_dir = get_plugin_cache_path().path_join("plugin_versions");
	gdre::ensure_dir(cache_dir);
	HashMap<String, HashMap<String, Dictionary>> data;
	{
		MutexLock lock(plugin_version_cache_mutex);
		for (auto &E : plugin_version_cache) {
			const PluginVersion &version = E.value;
			if (version.is_valid() && !version.is_static_cached) {
				Dictionary version_json = version.to_json();
				String source = version.release_info.plugin_source;
				String primary_id = itos(version.release_info.primary_id);
				String secondary_id = itos(version.release_info.secondary_id);
				if (!data.has(source)) {
					data[source] = HashMap<String, Dictionary>();
				}
				if (!data[source].has(primary_id)) {
					data[source][primary_id] = Dictionary();
				}
				data[source][primary_id][secondary_id] = version_json;
			}
		}
	}

	for (auto &source : data) {
		for (auto &primary_id : source.value) {
			// source/primary_id.json - with secondary_id as the key
			String file_name = source.key + "/" + primary_id.key + ".json";
			String path = cache_dir.path_join(file_name);
			gdre::ensure_dir(path.get_base_dir());
			auto file = FileAccess::open(path, FileAccess::WRITE);
			if (file.is_null()) {
				ERR_PRINT("Failed to open plugin version cache file for writing: " + file_name);
				continue;
			}
			String json = JSON::stringify(primary_id.value, " ", false, true);
			file->store_string(json);
			file->flush();
			file->close();
		}
	}
	data.clear();
}

Error PluginManager::save_plugin_version_static_cache(const String &output_path) {
	Vector<Pair<String, Dictionary>> vec;
	Dictionary data;
	{
		MutexLock lock(plugin_version_cache_mutex);
		for (auto &E : plugin_version_cache) {
			vec.push_back(Pair<String, Dictionary>(E.key, E.value.to_json()));
		}
	}
	struct _VecSort {
		bool operator()(const Pair<String, Dictionary> &a, const Pair<String, Dictionary> &b) const {
			return a.first < b.first;
		}
	};
	vec.sort_custom<_VecSort>();
	for (auto &E : vec) {
		data[E.first] = E.second;
	}
	gdre::ensure_dir(output_path.get_base_dir());
	auto file = FileAccess::open(output_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(file.is_null(), ERR_FILE_CANT_WRITE, "Failed to open plugin version static cache file for writing: " + output_path);
	// No indenting to keep the file size small
	String json = JSON::stringify(data, "", false, true);
	if (!file->store_string(json)) {
		return ERR_FILE_CANT_WRITE;
	}
	file->flush();
	file->close();
	return OK;
}

Dictionary PluginManager::_get_plugin_info(const String &plugin_name, const Vector<String> &hashes) {
	auto val = get_plugin_info(plugin_name, hashes);
	if (!val.is_valid()) {
		return Dictionary();
	}
	return val.to_json();
}

void PluginManager::clear_plugin_cache(bool clear_static_cache) {
	{
		MutexLock lock(plugin_version_cache_mutex);
		plugin_version_cache.clear();
		if (!clear_static_cache) {
			if (FileAccess::exists(STATIC_PLUGIN_CACHE_PATH)) {
				load_plugin_version_cache_file(STATIC_PLUGIN_CACHE_PATH, true);
			}
		}
	}
	for (int i = 0; i < source_count; ++i) {
		sources[i]->clear_cache();
	}
}

void PluginManager::clear_download_cache() {
	gdre::rimraf(get_plugin_download_cache_path());
	gdre::ensure_dir(get_plugin_download_cache_path());
}

void PluginManager::_bind_methods() {
	// ClassDB::bind_method(D_METHOD("get_plugin_version", "plugin_name", "version"), &PluginManager::get_plugin_version);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_plugin_info", "plugin_name", "hashes"), &PluginManager::_get_plugin_info);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("load_cache"), &PluginManager::load_cache);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_cache"), &PluginManager::save_cache);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("prepop_cache", "plugin_names", "output_path"), &PluginManager::prepop_cache);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("register_source", "name", "source"), &PluginManager::register_source);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("unregister_source", "name"), &PluginManager::unregister_source);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("print_plugin_cache"), &PluginManager::print_plugin_cache);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("clear_plugin_cache", "clear_static_cache"), &PluginManager::clear_plugin_cache, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("clear_download_cache"), &PluginManager::clear_download_cache);
}

namespace {
struct _PluginVersionSort {
	bool operator()(const PluginVersion &a, const PluginVersion &b) const {
		if (a.plugin_name != b.plugin_name) {
			return a.plugin_name < b.plugin_name;
		}
		return a.release_info.version < b.release_info.version;
	}
};
} //namespace

void PluginManager::print_plugin_cache() {
	Vector<PluginVersion> plugin_versions;
	{
		MutexLock lock(plugin_version_cache_mutex);
		for (auto &E : plugin_version_cache) {
			plugin_versions.push_back(E.value);
		}
	}
	plugin_versions.sort_custom<_PluginVersionSort>();
	for (auto &plugin_version : plugin_versions) {
		print_line(plugin_version.plugin_name + "," + plugin_version.release_info.version + "," + plugin_version.release_info.download_url);
	}
}
