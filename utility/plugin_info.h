#pragma once
#include "core/variant/variant.h"
#include "utility/godotver.h"

static constexpr int CACHE_VERSION = 1;

struct PluginBin {
	String name;
	String md5;
	Vector<String> tags;

	Dictionary to_json() const {
		Dictionary d;
		d["name"] = name;
		d["md5"] = md5;
		d["tags"] = tags;
		return d;
	}

	static PluginBin from_json(Dictionary d) {
		PluginBin bin;
		bin.name = d.get("name", "");
		bin.md5 = d.get("md5", "");
		bin.tags = d.get("tags", Vector<String>());
		return bin;
	}
};

struct GDExtInfo {
	String relative_path;
	String min_godot_version;
	String max_godot_version;
	Vector<PluginBin> bins;
	Vector<PluginBin> dependencies;

	Dictionary to_json() const {
		Dictionary d;
		d["relative_path"] = relative_path;
		d["min_godot_version"] = min_godot_version;
		d["max_godot_version"] = max_godot_version;
		Array bins_arr;
		for (const auto &bin : bins) {
			bins_arr.push_back(bin.to_json());
		}
		d["bins"] = bins_arr;
		Array deps_arr;
		for (const auto &dep : dependencies) {
			deps_arr.push_back(dep.to_json());
		}
		d["dependencies"] = deps_arr;
		return d;
	}

	static GDExtInfo from_json(Dictionary d) {
		GDExtInfo info;
		info.relative_path = d.get("relative_path", "");
		info.min_godot_version = d.get("min_godot_version", "");
		info.max_godot_version = d.get("max_godot_version", "");
		Array bins_arr = d.get("bins", {});
		for (int i = 0; i < bins_arr.size(); i++) {
			info.bins.push_back(PluginBin::from_json(bins_arr[i]));
		}
		Array deps_arr = d.get("dependencies", {});
		for (int i = 0; i < deps_arr.size(); i++) {
			info.dependencies.push_back(PluginBin::from_json(deps_arr[i]));
		}
		return info;
	}
};

// break this up into multiple structs
// COMPLETED:
// - PluginVersion
//   - cache_version
//   - plugin_name
//   - min_godot_version
//   - max_godot_version
//   - base_folder
//   - gdexts
//   - ReleaseInfo
//     - asset_id - renamed to primary_id in cache keys
//     - release_id - renamed to secondary_id in cache keys
//     - changed from_asset_lib to plugin_source
//     - version
//     - engine_ver_major
//     - release_date
//     - download_url
// - Changed CACHE_VERSION from 0 to 1
// - Plugin sources now return ReleaseInfo structs and don't cache PluginVersion structs
// - PluginManager caches PluginVersion structs with keys: plugin_source-primary_id-secondary_id
// - PluginVersion structs are populated from ReleaseInfo structs and analysis

struct ReleaseInfo {
	String plugin_source;
	uint64_t primary_id = 0; // assetlib asset id or github release id
	uint64_t secondary_id = 0; // assetlib edit_id or github asset id
	String version;
	int engine_ver_major = 0;
	String release_date;
	String download_url;

	Dictionary to_json() const {
		Dictionary d;
		d["plugin_source"] = plugin_source;
		d["primary_id"] = primary_id;
		d["secondary_id"] = secondary_id;
		d["version"] = version;
		d["engine_ver_major"] = engine_ver_major;
		d["release_date"] = release_date;
		d["download_url"] = download_url;
		return d;
	}

	static ReleaseInfo from_json(Dictionary d) {
		ReleaseInfo info;
		info.plugin_source = d.get("plugin_source", "");
		info.primary_id = d.get("primary_id", 0);
		info.secondary_id = d.get("secondary_id", 0);
		info.version = d.get("version", "");
		info.engine_ver_major = d.get("engine_ver_major", 0);
		info.release_date = d.get("release_date", "");
		info.download_url = d.get("download_url", "");
		return info;
	}
};

struct PluginVersion {
	int cache_version = CACHE_VERSION;
	String plugin_name;
	ReleaseInfo release_info;
	String min_godot_version;
	String max_godot_version;
	String base_folder;
	Vector<GDExtInfo> gdexts;

	static PluginVersion invalid() {
		PluginVersion version;
		version.cache_version = -1;
		return version;
	}

	bool is_valid() const {
		return cache_version == CACHE_VERSION && !plugin_name.is_empty();
	}

	bool is_compatible(const Ref<GodotVer> &ver) const {
		auto min_ver = GodotVer::parse(min_godot_version);
		if (ver->get_major() != min_ver->get_major()) {
			return false;
		}
		if (!max_godot_version.is_empty()) {
			return (ver->get_minor() >= min_ver->get_minor() && ver->get_minor() <= GodotVer::parse(max_godot_version)->get_minor());
		}
		return ver->get_minor() >= min_ver->get_minor();
	}

	bool is_compatible(const String &godot_version) const {
		auto ver = GodotVer::parse(godot_version);
		return is_compatible(ver);
	}

	Dictionary to_json() const {
		Dictionary d;
		d["cache_version"] = cache_version;
		d["plugin_name"] = plugin_name;
		d["release_info"] = release_info.to_json();
		d["min_godot_version"] = min_godot_version;
		d["max_godot_version"] = max_godot_version;
		d["base_folder"] = base_folder;
		Array gdexts_arr;
		for (const auto &gdext : gdexts) {
			gdexts_arr.push_back(gdext.to_json());
		}
		d["gdexts"] = gdexts_arr;
		return d;
	}

	static PluginVersion from_json(Dictionary d) {
		PluginVersion version;
		version.cache_version = d.get("cache_version", -1);
		version.plugin_name = d.get("plugin_name", "");
		version.release_info = ReleaseInfo::from_json(d.get("release_info", {}));
		version.min_godot_version = d.get("min_godot_version", "");
		version.max_godot_version = d.get("max_godot_version", "");
		version.base_folder = d.get("base_folder", "");
		Array gdexts_arr = d.get("gdexts", {});
		for (int i = 0; i < gdexts_arr.size(); i++) {
			version.gdexts.push_back(GDExtInfo::from_json(gdexts_arr[i]));
		}
		return version;
	}
};
