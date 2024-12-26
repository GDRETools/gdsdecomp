
#include "script_loader.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/os/os.h"
#include "core/variant/variant_parser.h"
#include "bytecode/bytecode_base.h"
#include "utility/gdre_settings.h"

Ref<Resource> ScriptLoader::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	return custom_load(p_path, p_original_path, ResourceCompatLoader::get_default_load_type(), r_error, p_use_sub_threads, p_cache_mode);
}

void ScriptLoader::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("gd");
	p_extensions->push_back("gdc");
	p_extensions->push_back("gde");
}

bool ScriptLoader::handles_type(const String &p_type) const {
	return (p_type == "Script" || p_type == "GDScript");
}

String ScriptLoader::get_resource_type(const String &p_path) const {
	String extension = p_path.get_extension().to_lower();
	if (extension == "gd" || extension == "gdc" || extension == "gde") {
		return "GDScript";
	}
	return "";
}

Ref<Resource> ScriptLoader::custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceFormatLoader::CacheMode p_cache_mode) {
	Ref<FakeGDScript> script;
	script.instantiate();
	Error err;
	if (!r_error) {
		r_error = &err;
	}
	*r_error = script->load_source_code(p_path);
	ERR_FAIL_COND_V_MSG(*r_error != OK, Ref<Resource>(), "Error loading script: " + p_path);
	return script;
}

ResourceInfo ScriptLoader::get_resource_info(const String &p_path, Error *r_error) const {
	ResourceInfo info;
	info.type = get_resource_type(p_path);
	if (info.type == "") {
		if (r_error) {
			*r_error = ERR_FILE_UNRECOGNIZED;
		}
		return info;
	}
	auto rev = GDRESettings::get_singleton()->get_bytecode_revision();
	if (rev) {
		auto decomp = GDScriptDecomp::create_decomp_for_commit(rev);
		if (decomp.is_valid()) {
			String extension = p_path.get_extension().to_lower();

			Ref<GodotVer> ver = decomp->get_godot_ver();
			if (ver.is_valid() && ver->is_valid_semver()) {
				info.ver_major = ver->get_major();
				info.ver_minor = ver->get_minor();
			}
			info.ver_format = decomp->get_bytecode_version();
			if (extension == "gd") {
				info.resource_format = "GDScriptText";
			} else if (extension == "gdc" || extension == "gde") {
				info.resource_format = "GDScriptBytecode";
			}
		}
	}
	return info;
}