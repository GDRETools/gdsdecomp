#include "plugin_source.h"
#include "core/error/error_macros.h"
#include "plugin_info.h"
#include "utility/common.h"

PluginBin PluginSource::get_plugin_bin(const String &path, const SharedObject &obj) {
	PluginBin bin;
	bin.name = obj.path;
	bin.md5 = gdre::get_md5(path, true);
	bin.tags = obj.tags;
	return bin;
}

bool PluginSource::handles_plugin(const String &plugin_name) {
	ERR_FAIL_V_MSG(false, "Not implemented");
}

bool PluginSource::is_default() {
	return false;
}

ReleaseInfo PluginSource::get_release_info(const String &plugin_name, const String &version_key) {
	return ReleaseInfo();
}

void PluginSource::load_cache_internal() {
	ERR_FAIL_MSG("Not implemented");
}

void PluginSource::save_cache() {
	ERR_FAIL_MSG("Not implemented");
}

Vector<String> PluginSource::get_plugin_version_numbers(const String &plugin_name) {
	ERR_FAIL_V_MSG(Vector<String>(), "Not implemented");
}

void PluginSource::load_cache() {
	load_cache_internal();
}

String PluginSource::get_plugin_name() {
	ERR_FAIL_V_MSG(String(), "Not implemented");
}

void PluginSource::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_plugin_version_numbers", "plugin_name"), &PluginSource::get_plugin_version_numbers);
	ClassDB::bind_method(D_METHOD("load_cache"), &PluginSource::load_cache);
	ClassDB::bind_method(D_METHOD("save_cache"), &PluginSource::save_cache);
	ClassDB::bind_method(D_METHOD("handles_plugin", "plugin_name"), &PluginSource::handles_plugin);
	ClassDB::bind_method(D_METHOD("is_default"), &PluginSource::is_default);
}
