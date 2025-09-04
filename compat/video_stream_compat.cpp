#include "video_stream_compat.h"
#include "utility/gdre_settings.h"

#include "modules/theora/video_stream_theora.h"

Ref<Resource> ResourceFormatLoaderCompatVideo::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	return ResourceFormatLoaderTheora().load(p_path, p_original_path, r_error, p_use_sub_threads, r_progress, p_cache_mode);
}

Ref<Resource> ResourceFormatLoaderCompatVideo::custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceFormatLoader::CacheMode p_cache_mode) {
	return ResourceFormatLoaderTheora().load(p_path, p_original_path, r_error, use_threads, nullptr, p_cache_mode);
}

void ResourceFormatLoaderCompatVideo::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("ogv");
	p_extensions->push_back("ogm");
}

bool ResourceFormatLoaderCompatVideo::handles_type(const String &p_type) const {
	return ClassDB::is_parent_class(p_type, "VideoStream") || p_type == "VideoStreamTheora";
}

String ResourceFormatLoaderCompatVideo::get_resource_type(const String &p_path) const {
	String el = p_path.get_extension().to_lower();
	if (el == "ogv" || el == "ogm") {
		return "VideoStreamTheora";
	}
	return "";
}
Ref<ResourceInfo> ResourceFormatLoaderCompatVideo::get_resource_info(const String &p_path, Error *r_error) const {
	Ref<ResourceInfo> info;
	info.instantiate();
	info->ver_format = 0;
	info->ver_major = GDRESettings::get_singleton()->get_ver_major();
	info->ver_minor = GDRESettings::get_singleton()->get_ver_minor();
	String el = p_path.get_extension().to_lower();
	if (el == "ogv" || el == "ogm") {
		info->resource_format = "OggTheora";
		info->type = "VideoStreamTheora";
		info->original_path = p_path;
	}
	return info;
}
