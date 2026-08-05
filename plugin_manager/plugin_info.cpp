#include "plugin_info.h"
#include "utility/common.h"

namespace {
void set_if_not_empty(Dictionary &d, const String &key, const String &value) {
	if (!value.is_empty()) {
		d[key] = value;
	}
}

void set_if_not_empty(Dictionary &d, const String &key, const Array &value) {
	if (!value.is_empty()) {
		d[key] = value;
	}
}

void set_if_not_empty(Dictionary &d, const String &key, const Vector<String> &value) {
	if (!value.is_empty()) {
		d[key] = value;
	}
}

} //namespace

Dictionary PluginBin::to_json() const {
	Dictionary d;
	d["name"] = name;
	set_if_not_empty(d, "sha256", sha256);
	set_if_not_empty(d, "verbatim_sha256", verbatim_sha256);
	d["exists"] = exists;
	set_if_not_empty(d, "bin_ver", bin_ver);
	set_if_not_empty(d, "tags", tags);
	return d;
}

PluginBin PluginBin::from_json(Dictionary d) {
	PluginBin bin;
	bin.name = d.get("name", "");
	bin.sha256 = d.get("sha256", "");
	bin.verbatim_sha256 = d.get("verbatim_sha256", "");
	bin.exists = d.get("exists", false);
	bin.bin_ver = d.get("bin_ver", "");
	bin.tags = d.get("tags", Vector<String>());
	return bin;
}

Dictionary GDExtInfo::to_json() const {
	Dictionary d;
	set_if_not_empty(d, "relative_path", relative_path);
	set_if_not_empty(d, "min_godot_version", min_godot_version);
	set_if_not_empty(d, "max_godot_version", max_godot_version);
	Array bins_arr;
	for (const auto &bin : bins) {
		bins_arr.push_back(bin.to_json());
	}
	set_if_not_empty(d, "bins", bins_arr);
	Array deps_arr;
	for (const auto &dep : dependencies) {
		deps_arr.push_back(dep.to_json());
	}
	set_if_not_empty(d, "dependencies", deps_arr);
	return d;
}

GDExtInfo GDExtInfo::from_json(Dictionary d) {
	GDExtInfo info;
	info.relative_path = d.get("relative_path", "");
	info.min_godot_version = d.get("min_godot_version", "");
	info.max_godot_version = d.get("max_godot_version", "");
	Array bins_arr = d.get("bins", Array());
	for (int i = 0; i < bins_arr.size(); i++) {
		info.bins.push_back(PluginBin::from_json(bins_arr[i]));
	}
	Array deps_arr = d.get("dependencies", Array());
	for (int i = 0; i < deps_arr.size(); i++) {
		info.dependencies.push_back(PluginBin::from_json(deps_arr[i]));
	}
	return info;
}

bool ReleaseInfo::is_valid() const {
	return !plugin_source.is_empty();
}

Dictionary ReleaseInfo::to_json() const {
	// all other keys should be non-empty
	Dictionary d;
	d["plugin_source"] = plugin_source;
	d["primary_id"] = primary_id;
	d["secondary_id"] = secondary_id;
	d["version"] = version;
	set_if_not_empty(d, "release_date", release_date);
	d["download_url"] = download_url;
	d["repository_url"] = repository_url;
	set_if_not_empty(d, "sha256_sum", sha256_sum);
	return d;
}

ReleaseInfo ReleaseInfo::from_json(Dictionary d) {
	ReleaseInfo info;
	info.plugin_source = d.get("plugin_source", "");
	info.primary_id = d.get("primary_id", 0);
	info.secondary_id = d.get("secondary_id", 0);
	info.version = d.get("version", "");
	info.release_date = d.get("release_date", "");
	info.download_url = d.get("download_url", "");
	info.repository_url = d.get("repository_url", "");
	info.sha256_sum = d.get("sha256_sum", "");
	return info;
}

bool ReleaseInfo::operator==(const ReleaseInfo &other) const {
	return plugin_source == other.plugin_source &&
			primary_id == other.primary_id &&
			secondary_id == other.secondary_id &&
			version == other.version &&
			release_date == other.release_date &&
			download_url == other.download_url &&
			repository_url == other.repository_url &&
			sha256_sum == other.sha256_sum;
}

bool ReleaseInfo::operator!=(const ReleaseInfo &other) const {
	return !(*this == other);
}

String ReleaseInfo::get_cache_key() const {
	return plugin_source + "-" + itos(primary_id) + "-" + itos(secondary_id);
}

String ReleaseInfo::get_download_file() const {
	return gdre::remove_url_query_params(download_url).get_file();
}

PluginVersion PluginVersion::invalid() {
	PluginVersion version;
	version.cache_version = -1;
	return version;
}

bool PluginVersion::is_valid() const {
	return cache_version == CACHE_VERSION && !plugin_name.is_empty();
}

bool PluginVersion::is_compatible(const Ref<GodotVer> &ver) const {
	Ref<GodotVer> min_ver;
	if (!min_godot_version.is_empty()) {
		min_ver = GodotVer::parse(min_godot_version);
	} else if (gdexts.size() > 0) {
		if (gdexts[0].relative_path.has_extension("gdnlib")) {
			min_ver = GodotVer::parse("3.0");
		} else if (gdexts[0].relative_path.has_extension("gdextension")) {
			min_ver = GodotVer::parse("4.0");
		}
	}
	if (min_ver.is_null()) {
		return true;
	}
	if (ver->get_major() != min_ver->get_major()) {
		return false;
	}
	if (!max_godot_version.is_empty()) {
		return (ver->get_minor() >= min_ver->get_minor() && ver->get_minor() <= GodotVer::parse(max_godot_version)->get_minor());
	}
	return ver->get_minor() >= min_ver->get_minor();
}

bool PluginVersion::is_compatible(const String &godot_version) const {
	auto ver = GodotVer::parse(godot_version);
	return is_compatible(ver);
}

bool PluginVersion::bin_hashes_match(const Vector<String> &hashes) const {
	for (const auto &gdext : gdexts) {
		for (const auto &bin : gdext.bins) {
			for (const auto &hash : hashes) {
				if (bin.sha256 == hash) {
					return true;
				}
			}
		}
	}
	return false;
}

Dictionary PluginVersion::to_json() const {
	Dictionary d;
	d["cache_version"] = cache_version;
	d["plugin_name"] = plugin_name;
	d["release_info"] = release_info.to_json();
	set_if_not_empty(d, "min_godot_version", min_godot_version);
	set_if_not_empty(d, "max_godot_version", max_godot_version);
	set_if_not_empty(d, "base_folder", base_folder);
	d["size"] = size;
	set_if_not_empty(d, "archive_sha256", archive_sha256);
	Array gdexts_arr;
	for (const auto &gdext : gdexts) {
		gdexts_arr.push_back(gdext.to_json());
	}
	set_if_not_empty(d, "gdexts", gdexts_arr);
	return d;
}

PluginVersion PluginVersion::from_json(Dictionary d) {
	PluginVersion version;
	version.cache_version = d.get("cache_version", -1);
	version.plugin_name = d.get("plugin_name", "");
	version.release_info = ReleaseInfo::from_json(d.get("release_info", Dictionary()));
	version.min_godot_version = d.get("min_godot_version", "");
	version.max_godot_version = d.get("max_godot_version", "");
	version.base_folder = d.get("base_folder", "");
	version.size = d.get("size", 0);
	version.archive_sha256 = d.get("archive_sha256", "");
	Array gdexts_arr = d.get("gdexts", Array());
	for (int i = 0; i < gdexts_arr.size(); i++) {
		version.gdexts.push_back(GDExtInfo::from_json(gdexts_arr[i]));
	}
	return version;
}
