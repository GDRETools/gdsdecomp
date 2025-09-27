
#include "script_loader.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/os/os.h"
#include "core/variant/variant_parser.h"

#include "bytecode/bytecode_base.h"
#include "compat/fake_csharp_script.h"
#include "compat/fake_gdscript.h"
#include "compat/fake_script.h"
#include "utility/gdre_settings.h"

Ref<Resource> ResourceFormatGDScriptLoader::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	return custom_load(p_path, p_original_path, ResourceCompatLoader::get_default_load_type(), r_error, p_use_sub_threads, p_cache_mode);
}

void ResourceFormatGDScriptLoader::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("gd");
	p_extensions->push_back("gdc");
	p_extensions->push_back("gde");
	p_extensions->push_back("cs");
}

bool ResourceFormatGDScriptLoader::handles_type(const String &p_type) const {
	return (p_type == "Script" || p_type == "GDScript" || p_type == "CSharpScript");
}

String ResourceFormatGDScriptLoader::get_resource_type(const String &p_path) const {
	return _get_resource_type(p_path);
}

String ResourceFormatGDScriptLoader::_get_resource_type(const String &p_path) {
	String extension = p_path.get_extension().to_lower();
	if (extension == "gd" || extension == "gdc" || extension == "gde") {
		return "GDScript";
	}
	if (extension == "cs") {
		return "CSharpScript";
	}
	return "";
}

Ref<Resource> ResourceFormatGDScriptLoader::custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceFormatLoader::CacheMode p_cache_mode) {
	String load_path = p_original_path.is_empty() ? p_path : p_original_path;
	Ref<FakeScript> fake_script;
	if (p_path.get_extension().to_lower() == "cs") {
		if (GDRESettings::get_singleton()->has_loaded_dotnet_assembly()) {
			Ref<FakeCSharpScript> csharp_script;
			csharp_script.instantiate();
			csharp_script->set_original_class("CSharpScript");
			fake_script = csharp_script;
			Error err;
			if (!r_error) {
				r_error = &err;
			}
			fake_script->set_load_type(p_type);
			*r_error = csharp_script->load_source_code(load_path);
			ERR_FAIL_COND_V_MSG(*r_error != OK, Ref<Resource>(), "Error loading script: " + load_path + " (CSharpScript): " + csharp_script->get_error_message());
		} else {
			fake_script.instantiate();
			fake_script->set_original_class("CSharpScript");
			fake_script->set_load_type(p_type);
		}
	} else {
		Ref<FakeGDScript> fake_gd_script;
		fake_gd_script.instantiate();
		fake_script = fake_gd_script;
		Error err;
		if (!r_error) {
			r_error = &err;
		}
		fake_script->set_load_type(p_type);
		*r_error = fake_gd_script->load_source_code(load_path);
		ERR_FAIL_COND_V_MSG(*r_error != OK, Ref<Resource>(), "Error loading script: " + load_path);
	}

	bool is_real_load = p_type == ResourceInfo::LoadType::REAL_LOAD || p_type == ResourceInfo::LoadType::GLTF_LOAD;
	if (is_real_load && p_cache_mode != ResourceFormatLoader::CACHE_MODE_IGNORE && p_cache_mode != ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP) {
		fake_script->set_path(load_path, p_cache_mode == ResourceFormatLoader::CACHE_MODE_REPLACE || p_cache_mode == ResourceFormatLoader::CACHE_MODE_REPLACE_DEEP);
	} else {
		fake_script->set_path_cache(load_path);
	}
	return fake_script;
}

Ref<ResourceInfo> ResourceFormatGDScriptLoader::get_resource_info(const String &p_path, Error *r_error) const {
	Ref<ResourceInfo> info;
	info.instantiate();
	_set_resource_info(info, p_path);
	info->original_path = p_path;
	return info;
}

//	void _set_resource_info(Ref<ResourceInfo> &info, const String &p_path) const;

Error ResourceFormatGDScriptLoader::_set_resource_info(Ref<ResourceInfo> &info, const String &p_path) {
	info->type = _get_resource_type(p_path);
	if (info->type == "") {
		return ERR_FILE_UNRECOGNIZED;
	}
	String extension = p_path.get_extension().to_lower();
	auto rev = GDRESettings::get_singleton()->get_bytecode_revision();
	if (rev) {
		auto decomp = GDScriptDecomp::create_decomp_for_commit(rev);
		if (decomp.is_valid()) {
			Ref<GodotVer> ver = decomp->get_godot_ver();
			if (ver.is_valid() && ver->is_valid_semver()) {
				info->ver_major = ver->get_major();
				info->ver_minor = ver->get_minor();
			}
			info->ver_format = decomp->get_bytecode_version();
		}
	}
	if (extension == "gd") {
		info->resource_format = "GDScriptText";
	} else if (extension == "gdc" || extension == "gde") {
		info->resource_format = "GDScriptBytecode";
	} else if (extension == "cs") {
		info->resource_format = "CSharpScriptText";
	} else {
		info->resource_format = "GDScriptText";
	}
	return OK;
}

// for embedded scripts
Ref<Resource> FakeScriptConverterCompat::convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error) {
	Ref<FakeScript> fake_script;
	auto resource_info = ResourceInfo::get_info_from_resource(res);
	if (res->get_original_class() != "GDScript" && res->get_original_class() != "CSharpScript") {
		fake_script.instantiate();
		fake_script->set_original_class(res->get_original_class());
		fake_script->set_load_type(p_type);
		set_real_from_missing_resource(res, fake_script, p_type);
	} else {
		if (res->get_original_class() == "GDScript") {
			Ref<FakeGDScript> fake_gd_script;
			fake_gd_script.instantiate();
			fake_script = fake_gd_script;
		} else if (res->get_original_class() == "CSharpScript") {
			Ref<FakeCSharpScript> fake_csharp_script;
			fake_csharp_script.instantiate();
			fake_script = fake_csharp_script;
		}
		fake_script->set_load_type(p_type);
		String path = res->get_path();
		if (path.is_empty()) {
			if (resource_info.is_valid()) {
				path = resource_info->original_path;
			}
		}
		fake_script->set_original_class(res->get_original_class());
		if (is_external_resource(res) && p_type != ResourceInfo::LoadType::FAKE_LOAD && p_type != ResourceInfo::LoadType::NON_GLOBAL_LOAD) {
			fake_script->load_source_code(path);
		} else {
			set_real_from_missing_resource(res, fake_script, p_type);
		}
	}
	// ResourceFormatGDScriptLoader::_set_resource_info(resource_info, path);
	resource_info->set_on_resource(fake_script);
	return fake_script;
}

bool FakeScriptConverterCompat::handles_type(const String &p_type, int ver_major) const {
	return p_type == "Script" || p_type == "GDScript" || p_type == "CSharpScript";
}
