#include "visual_shader_compat.h"
#include "core/error/error_macros.h"
#include "core/version_generated.gen.h"
#include "modules/gdscript/gdscript.h"
#include "modules/regex/regex.h"
#include "utility/gdre_config.h"
#include "utility/resource_info.h"

void VisualShaderCompat::_update_shader() const {
	// does nothing; we don't want to update the shader code
}

void VisualShaderCompat::reset_state() {
	// does nothing; we don't want to reset the shader state
}

Shader::Mode VisualShaderCompat::get_mode() const {
	return Shader::get_mode();
}

bool VisualShaderCompat::is_text_shader() const {
	return true;
}

Ref<Resource> VisualShaderConverterCompat::convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error) {
	Ref<VisualShaderCompat> visual_shader = memnew(VisualShaderCompat());
	bool got = false;
	static const StringName res_format_type = "text";
	visual_shader->_start_load(res_format_type, 2);
	visual_shader->set_code(res->get("code", &got));
	if (!got || visual_shader->get_code().is_empty()) {
		if (r_error) {
			*r_error = ERR_FILE_CORRUPT;
		}
		ERR_FAIL_V_MSG(nullptr, "Failed to get code from VisualShader");
	}
	visual_shader->_finish_load(res_format_type, 2);
	visual_shader->merge_meta_from(res.ptr());
	return visual_shader;
}

bool VisualShaderConverterCompat::handles_type(const String &p_type, int ver_major) const {
	return p_type == "VisualShader" && ver_major < GODOT_VERSION_MAJOR;
}

Ref<Resource> VisualShaderNodeCustomConverterCompat::convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error) {
	if (p_type != ResourceInfo::REAL_LOAD) {
		// We don't support loading real scripts on anything other than real load.
		return get_real_from_missing_resource(res, p_type);
	}
	if (!GDREConfig::get_singleton()->get_setting("execute_visual_shader_node_scripts")) {
		WARN_PRINT("Skipping VisualShaderNodeCustom script execution due to user setting; loading this shader will probably fail. To enable this, set the 'execute_visual_shader_node_scripts' setting to true in the GDRE Tools configuration.");
		return get_real_from_missing_resource(res, p_type);
	}
	Ref<VisualShaderNodeCustom> visual_shader_node_custom = memnew(VisualShaderNodeCustom());
	List<PropertyInfo> properties;
	res->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (property.usage & PROPERTY_USAGE_STORAGE) {
			if (property.name == "script") {
				Ref<Script> script = res->get("script");
				if (Ref<FakeScript> fake_script = script; fake_script.is_valid()) {
					// TODO: This is not adequate for scripts that derive from another GDScript-defined class, but it'll do for now, we rarely encounter these.
					if (fake_script->get_direct_base_type() != "VisualShaderNodeCustom") {
						WARN_PRINT("THIS WILL PROBABLY FAIL!");
					}
					Ref<Script> real_script = memnew(GDScript);
					String source_code = fake_script->get_source_code();
					// change the class_name (if it exists) to someting unique to avoid conflicts
					Ref<RegEx> regex = RegEx::create_from_string(R"(^(extends\s+\w+\s+)?(class_name\s+)(\w+))");
					String prefix = "_" + String::num_int64(rand());
					auto new_source_code = regex->sub(source_code, "\\1\\2" + prefix + "\\3");
					real_script->set_source_code(new_source_code);
					if (real_script->reload() == OK) {
						script = real_script;
					}
				}
				visual_shader_node_custom->set_script(script);
			} else {
				visual_shader_node_custom->set(property.name, res->get(property.name));
			}
		}
	}
	visual_shader_node_custom->set_local_to_scene(res->is_local_to_scene());
	visual_shader_node_custom->set_scene_unique_id(res->get_scene_unique_id());

	return visual_shader_node_custom;
}

bool VisualShaderNodeCustomConverterCompat::handles_type(const String &p_type, int ver_major) const {
	return p_type == "VisualShaderNodeCustom" && ver_major == GODOT_VERSION_MAJOR;
}
