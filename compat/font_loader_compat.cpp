#include "font_loader_compat.h"

#include "scene/resources/font.h"
#include "utility/gdre_settings.h"

namespace {
// 3.x and below loaded dynamic font files directly, 4.x requires importing the font file into a FontFile resource and has no way to load them directly as a resource.
// Therefore, we should only enable this loader for 3.x and below.
static inline bool is_font_loader_enabled() {
	int major = GDRESettings::get_singleton()->get_ver_major();
	if (major == 0) {
		return GDRESettings::get_singleton()->get_ver_minor() != 0;
	}
	return major <= 3;
}

static HashSet<String> allowed_types = { "Font", "FontFile", "DynamicFont", "DynamicFontData" };
static HashSet<String> allowed_extensions = { "ttf", "otf", "woff", "woff2" };
} //namespace

Ref<Resource> ResourceFormatLoaderCompatFont::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	return custom_load(p_path, p_original_path, get_default_real_load(), r_error, p_use_sub_threads, p_cache_mode);
}

Ref<Resource> ResourceFormatLoaderCompatFont::custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceFormatLoader::CacheMode p_cache_mode) {
	ERR_FAIL_COND_V_MSG(!is_font_loader_enabled(), Ref<Resource>(), "Font loader is not enabled");
	Ref<FontFile> font_file = memnew(FontFile);
	Error err = font_file->load_dynamic_font(p_path);
	if (r_error) {
		*r_error = err;
	}
	ERR_FAIL_COND_V_MSG(err != OK, Ref<Resource>(), "Failed to load font file: " + p_path);
	return Ref<Resource>(font_file);
}

void ResourceFormatLoaderCompatFont::get_recognized_extensions(List<String> *p_extensions) const {
	if (!is_font_loader_enabled()) {
		return;
	}
	for (const String &extension : allowed_extensions) {
		p_extensions->push_back(extension);
	}
}

bool ResourceFormatLoaderCompatFont::handles_type(const String &p_type) const {
	if (!is_font_loader_enabled()) {
		return false;
	}
	return allowed_types.has(p_type);
}

String ResourceFormatLoaderCompatFont::get_resource_type(const String &p_path) const {
	if (!is_font_loader_enabled() || !allowed_extensions.has(p_path.get_extension().to_lower())) {
		return "";
	}
	return "FontFile";
}
Ref<ResourceInfo> ResourceFormatLoaderCompatFont::get_resource_info(const String &p_path, Error *r_error) const {
	ERR_FAIL_COND_V_MSG(!is_font_loader_enabled(), Ref<ResourceInfo>(), "Font loader is not enabled");
	Ref<ResourceInfo> info;
	info.instantiate();
	info->ver_format = 0;
	info->ver_major = GDRESettings::get_singleton()->get_ver_major();
	info->ver_minor = GDRESettings::get_singleton()->get_ver_minor();
	if (allowed_extensions.has(p_path.get_extension().to_lower())) {
		info->resource_format = "FontFile";
		info->type = "FontFile";
		info->original_path = p_path;
	}
	return info;
}
