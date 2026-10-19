#include "shader_material_converter.h"

#include "modules/regex/regex.h"
#include "scene/main/node.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/resources/texture.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/shader_converter.h"
#include "servers/rendering/shader_preprocessor.h"
#include "servers/rendering/shader_types.h"

#include "utility/common.h"
#include "utility/gdre_settings.h"
#include "utility/task_manager.h"

using SL = ShaderLanguage;

namespace {
HashMap<String, ShaderLanguage::ShaderNode::Uniform::Hint> _initialize_hint_map() {
	HashMap<String, ShaderMaterialConverter::UniformInfo::Hint> hint_map = {
		// intialized with v3 hint names
		{ "hint_albedo", ShaderMaterialConverter::UniformInfo::Hint::HINT_SOURCE_COLOR },
		{ "hint_aniso", ShaderMaterialConverter::UniformInfo::Hint::HINT_ANISOTROPY },
		{ "hint_black", ShaderMaterialConverter::UniformInfo::Hint::HINT_DEFAULT_BLACK },
		{ "hint_black_albedo", ShaderMaterialConverter::UniformInfo::Hint::HINT_DEFAULT_BLACK },
		{ "hint_color", ShaderMaterialConverter::UniformInfo::Hint::HINT_SOURCE_COLOR },
		{ "hint_transparent", ShaderMaterialConverter::UniformInfo::Hint::HINT_DEFAULT_TRANSPARENT },
		{ "hint_white", ShaderMaterialConverter::UniformInfo::Hint::HINT_DEFAULT_WHITE },
	};
	for (int i = 0; i < SL::ShaderNode::Uniform::HINT_MAX; i++) {
		hint_map[SL::get_uniform_hint_name(SL::ShaderNode::Uniform::Hint(i))] = SL::ShaderNode::Uniform::Hint(i);
	}
	return hint_map;
}

HashMap<String, ShaderLanguage::DataType> _initialize_data_type_map() {
	HashMap<String, ShaderLanguage::DataType> map;
	for (int i = 0; i < SL::DataType::TYPE_MAX; i++) {
		map[SL::get_datatype_name(SL::DataType(i))] = SL::DataType(i);
	}
	return map;
}
} //namespace

const HashMap<String, ShaderLanguage::ShaderNode::Uniform::Hint> ShaderMaterialConverter::UniformInfo::hint_name_to_hint = _initialize_hint_map();
const HashMap<String, ShaderLanguage::DataType> ShaderMaterialConverter::UniformInfo::data_type_name_to_data_type = _initialize_data_type_map();

Vector<ShaderLanguage::ShaderNode::Uniform::Hint> ShaderMaterialConverter::UniformInfo::parse_hints(const String &p_hint) {
	Vector<String> hint_strings = p_hint.strip_edges().trim_suffix(";").strip_edges().split(",");
	Vector<ShaderMaterialConverter::UniformInfo::Hint> hints;
	for (auto &E : hint_strings) {
		E = E.strip_edges();
		if (E.contains("(")) {
			E = E.split("(")[0].strip_edges();
		}
		if (hint_name_to_hint.has(E)) {
			hints.push_back(hint_name_to_hint.get(E));
		}
	}
	return hints;
}

ShaderLanguage::DataType ShaderMaterialConverter::UniformInfo::parse_data_type(const String &p_type) {
	return data_type_name_to_data_type.has(p_type) ? data_type_name_to_data_type.get(p_type) : DataType::TYPE_MAX;
}

static const HashSet<String> shader_param_names = {
	"albedo",
	"specular",
	"metallic",
	"roughness",
	"emission",
	"emission_energy",
	"normal_scale",
	"rim",
	"rim_tint",
	"clearcoat",
	"clearcoat_roughness",
	"anisotropy",
	"heightmap_scale",
	"subsurface_scattering_strength",
	"transmittance_color",
	"transmittance_depth",
	"transmittance_boost",
	"backlight",
	"refraction",
	"point_size",
	"uv1_scale",
	"uv1_offset",
	"uv2_scale",
	"uv2_offset",
	"particles_anim_h_frames",
	"particles_anim_v_frames",
	"particles_anim_loop",
	"heightmap_min_layers",
	"heightmap_max_layers",
	"heightmap_flip",
	"uv1_blend_sharpness",
	"uv2_blend_sharpness",
	"grow",
	"proximity_fade_distance",
	"msdf_pixel_range",
	"msdf_outline_size",
	"distance_fade_min",
	"distance_fade_max",
	"ao_light_affect",
	"metallic_texture_channel",
	"ao_texture_channel",
	"clearcoat_texture_channel",
	"rim_texture_channel",
	"heightmap_texture_channel",
	"refraction_texture_channel",
	"alpha_scissor_threshold",
	"alpha_hash_scale",
	"alpha_antialiasing_edge",
	"albedo_texture_size",
	"z_clip_scale",
	"fov_override",
};

bool set_param_not_in_property_list(Ref<BaseMaterial3D> p_base_material, const String &p_param_name, const Variant &p_value) {
	// if the param is not in the property list, but it does have a setter, we can set it
	String setter_name = vformat("set_%s", p_param_name);
	bool is_valid = false;
	int arg_count = p_base_material->get_method_argument_count(setter_name, &is_valid);
	if (is_valid && arg_count == 1) {
		String getter_name = vformat("get_%s", p_param_name);
		if (p_base_material->get_method_argument_count(getter_name, &is_valid) == 0 && is_valid) {
			// check the method signature
			Variant base_material_val = p_base_material->call(getter_name);
			if (base_material_val.get_type() != p_value.get_type()) {
				return false;
			}
		}
		p_base_material->call(vformat("set_%s", p_param_name), p_value);
		return true;
	} else if (p_param_name == "metallness" && p_value.get_type() == Variant::FLOAT) {
		p_base_material->set_metallic(p_value);
		return true;
	} else if (p_param_name.contains("vertex_color") && p_param_name.contains("use") && p_value.get_type() == Variant::BOOL) {
		p_base_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, p_value);
		return true;
	}
	return false;
}

Vector<StringName> fallback_get_render_modes(Ref<Shader> shader) {
	constexpr const char *render_mode_re_str = R"(\brender_mode\s+(?:\s*\/\/.*$)*(\w+(?:\s*(?:\s*\/\/.*$)*,(?:\s*\/\/.*$)*\s*\w+)*))";
	// Ref<RegEx> render_mode_re = RegEx::create_from_string(R"(\brender_mode\s+(\w+(?:\s*,\s*\w+)*))");
	Ref<RegEx> render_mode_re = RegEx::create_from_string(render_mode_re_str);
	String code = shader->get_code();
	HashSet<StringName> render_modes;
	auto render_mode_matches = render_mode_re->search_all(code);
	for (auto &E : render_mode_matches) {
		Ref<RegExMatch> match = E;
		String render_mode_str = match->get_string(1);
		Vector<String> render_mode_parts = render_mode_str.split(",");
		for (auto &E : render_mode_parts) {
			E = E.strip_edges();
			if (E.is_empty()) {
				continue;
			}
			render_modes.insert(E);
		}
	}
	return gdre::hashset_to_vector(render_modes);
}

HashMap<String, ShaderMaterialConverter::UniformInfo> fallback_parse_shader_uniforms(const Ref<Shader> &shader) {
	List<PropertyInfo> uniform_list;
	shader->get_shader_uniform_list(&uniform_list, false);
	HashMap<String, PropertyInfo> property_infos;
	for (auto &E : uniform_list) {
		property_infos[E.name] = E;
	}
	HashMap<String, ShaderMaterialConverter::UniformInfo> uniform_infos;
	String shader_code = shader->get_code();
	if (shader_code.is_empty()) {
		return uniform_infos;
	}
	// Ref<RegEx> re = RegEx::create_from_string(R"((global|instance)?\s*uniform\s+(?:(?:lowp|mediump|highp)\s+)?(\w+)\s+(\w+)\s*(?:\:((?:\s*\w+\s*,)*\s*\w+))?)");
	Ref<RegEx> re = RegEx::create_from_string(R"((?P<scope>global|instance\s+)?uniform\s+(?:(?P<precision>lowp|mediump|highp)\s+)?(?P<type>\w+)(?:\s*\[(?P<array_size>\d+)\])?\s+(?P<name>\w+)(?:\s*\[(?P<array_size2>\d+)\])?\s*(?:\:\s*(?P<hint_string>(?:\s*\w+(?:\s*\(.*\))?\s*,)*\s*\w+(?:\s*\(.*\))?))?)");
	// Don't match "normal" or "form"
	auto matches = re->search_all(shader_code);

	for (auto &E : matches) {
		Ref<RegExMatch> match = E;
		ShaderMaterialConverter::UniformInfo info;
		String param_name = match->get_string(3);
		info.name = match->get_string("name");
		info.scope = ShaderMaterialConverter::UniformInfo::parse_scope(match->get_string("scope"));
		info.type = ShaderMaterialConverter::UniformInfo::parse_data_type(match->get_string("type"));
		String array_size_str = match->get_string("array_size");
		if (array_size_str.is_empty()) {
			array_size_str = match->get_string("array_size2");
		}
		if (!array_size_str.is_empty()) {
			info.array_size = array_size_str.to_int();
		}
		info.hints = ShaderMaterialConverter::UniformInfo::parse_hints(match->get_string("hint_string").strip_edges());
		if (property_infos.has(info.name)) {
			info.property_info = property_infos.get(info.name);
		}
		if (info.is_texture()) {
			info.default_value = shader->get_default_texture_parameter(info.name);
		}
		if (info.default_value.get_type() == Variant::NIL) {
			info.default_value = RenderingServer::get_singleton()->shader_get_parameter_default(shader->get_rid(), info.name);
		}
		uniform_infos[info.name] = info;
	}
	return uniform_infos;
}

ShaderMaterialConverter::ShaderInfo fallback_parse_shader_info(const Ref<Shader> &p_shader) {
	ShaderMaterialConverter::ShaderInfo info;
	info.render_modes = fallback_get_render_modes(p_shader);
	info.uniforms = fallback_parse_shader_uniforms(p_shader);
	return info;
}

static ShaderLanguage::DataType _get_global_shader_uniform_type(const StringName &p_name) {
	RSE::GlobalShaderParameterType gvt = RenderingServerGlobals::material_storage->global_shader_parameter_get_type(p_name);
	return (ShaderLanguage::DataType)RS::global_shader_uniform_type_get_shader_datatype(gvt);
}

ShaderLanguage::ShaderNode *get_shader_node(ShaderLanguage &sl, Ref<Shader> shader) {
	String code = shader->get_code();
	ERR_FAIL_COND_V_MSG(code.is_empty(), nullptr, "Shader code is empty");

	ShaderPreprocessor preprocessor;
	String preprocessed_code;
	String error_text;
	List<ShaderPreprocessor::FilePosition> error_positions;
	Error result = preprocessor.preprocess(code, shader->get_path(), preprocessed_code, &error_text, &error_positions, nullptr, nullptr);
	ERR_FAIL_COND_V_MSG(result != OK, nullptr, vformat("Failed to preprocess shader %s (line %d): %s", shader->get_path(), error_positions.front() ? error_positions.front()->get().line : 0, error_text));

	ShaderLanguage::ShaderCompileInfo info;
	auto mode = shader->get_mode();
	info.functions = ShaderTypes::get_singleton()->get_functions(RSE::ShaderMode(mode));
	info.render_modes = ShaderTypes::get_singleton()->get_modes(RSE::ShaderMode(mode));
	info.stencil_modes = ShaderTypes::get_singleton()->get_stencil_modes(RSE::ShaderMode(mode));
	info.shader_types = ShaderTypes::get_singleton()->get_types();
	info.global_shader_uniform_type_func = _get_global_shader_uniform_type;
	Error err = sl.compile(preprocessed_code, info);
	if (err) {
		ShaderDeprecatedConverter sdc;
		if (sdc.is_code_deprecated(preprocessed_code)) {
			ERR_FAIL_COND_V_MSG(!sdc.convert_code(preprocessed_code), nullptr, vformat("Shader conversion failed (line %d): %s", sdc.get_error_line(), sdc.get_error_text()));
			code = sdc.emit_code();
			preprocessed_code = code;
			err = sl.compile(preprocessed_code, info);
		} else if (sdc.get_error_text() != "") { // Preprocessing failed.
			ERR_FAIL_V_MSG(nullptr, vformat("Shader conversion failed (line %d): %s", sdc.get_error_line(), sdc.get_error_text()));
		} // If the code is reported as not deprecated, let it fall through to the compile step after this if block so that we get the full compile error.
	}
	ERR_FAIL_COND_V_MSG(err != OK, nullptr, vformat("Failed to compile shader %s (line %d): %s", shader->get_path(), sl.get_error_line(), sl.get_error_text()));

	return sl.get_shader();
}

Variant get_default_parameter(const SL::ShaderNode::Uniform &uniform) {
	if (uniform.default_value.is_empty()) {
		return ShaderLanguage::get_default_datatype_value(uniform.type, uniform.array_size, uniform.hint);
	}
	return ShaderLanguage::constant_value_to_variant(uniform.default_value, uniform.type, uniform.array_size, uniform.hint);
}

ShaderMaterialConverter::ShaderInfo ShaderMaterialConverter::parse_shader_info(const Ref<Shader> &shader) {
	ShaderLanguage sl;
	auto shader_node = get_shader_node(sl, shader);
	if (!shader_node) {
		return fallback_parse_shader_info(shader);
	}
	ShaderMaterialConverter::ShaderInfo shader_info;

	shader_info.render_modes = shader_node->render_modes;
	for (auto &E : shader_node->uniforms) {
		ShaderMaterialConverter::UniformInfo info;
		info.name = E.key;
		info.type = E.value.type;
		info.array_size = E.value.array_size;
		info.hints = { E.value.hint };
		if (info.hints.size() == 1 && info.hints[0] == ShaderLanguage::ShaderNode::Uniform::HINT_NONE && E.value.use_color) {
			info.hints = { ShaderLanguage::ShaderNode::Uniform::HINT_SOURCE_COLOR };
		}
		if (info.is_texture()) {
			info.default_value = shader->get_default_texture_parameter(info.name);
		}
		if (info.default_value.get_type() == Variant::NIL) {
			info.default_value = get_default_parameter(E.value);
		}
		info.scope = E.value.scope;
		info.property_info = ShaderLanguage::uniform_to_property_info(E.value);
		shader_info.uniforms[info.name] = info;
	}
	return shader_info;
}

Ref<Texture2D> get_texture_2d(const Variant &val, const ShaderMaterialConverter::UniformInfo &info) {
	Ref<Texture> tex = val;
	if (!tex.is_valid()) {
		if (info.is_texture() && info.is_array()) {
			Array val_array = val;
			for (auto &E : val_array) {
				tex = E;
				if (tex.is_valid()) {
					break;
				}
			}
		}
	}
	if (!tex.is_valid()) {
		return Ref<Texture2D>();
	}

	Ref<Texture2D> tex2d = tex;
	if (!tex2d.is_valid() && info.is_texture()) {
		Ref<Image> img;
		if (Ref<TextureLayered> tex_layered = tex; tex_layered.is_valid()) {
			// use the first one that is valid
			for (int i = 0; i < tex_layered->get_layers(); i++) {
				img = tex_layered->get_layer_data(0);
				if (img.is_valid()) {
					break;
				}
			}
		} else if (Ref<Texture3D> tex_3d = tex; tex_3d.is_valid()) {
			// use the first one that is valid
			auto images = tex_3d->get_data();
			for (int i = 0; i < images.size(); i++) {
				img = images[0];
				if (img.is_valid()) {
					break;
				}
			}
		}
		if (img.is_valid()) {
			Ref<ImageTexture> img_tex = memnew(ImageTexture);
			img_tex->create_from_image(img);
			img_tex->set_name(tex->get_name());
			img_tex->set_path_cache(tex->get_path());
			img_tex->set_local_to_scene(tex->is_local_to_scene());
			img_tex->merge_meta_from(tex.ptr());
			tex2d = img_tex;
		}
	}

	return tex2d;
}

BaseMaterial3D::BlendMode blend_mode_from_string(const String &p_string) {
	if (p_string == "blend_mix") {
		return BaseMaterial3D::BLEND_MODE_MIX;
	} else if (p_string == "blend_add") {
		return BaseMaterial3D::BLEND_MODE_ADD;
	} else if (p_string == "blend_sub") {
		return BaseMaterial3D::BLEND_MODE_SUB;
	} else if (p_string == "blend_mul") {
		return BaseMaterial3D::BLEND_MODE_MUL;
	} else if (p_string == "blend_premul_alpha") {
		return BaseMaterial3D::BLEND_MODE_PREMULT_ALPHA;
	}
	// default
	return BaseMaterial3D::BLEND_MODE_MIX;
}

BaseMaterial3D::DepthDrawMode depth_draw_mode_from_string(const String &p_string) {
	if (p_string == "depth_draw_opaque") {
		return BaseMaterial3D::DEPTH_DRAW_OPAQUE_ONLY;
	} else if (p_string == "depth_draw_always") {
		return BaseMaterial3D::DEPTH_DRAW_ALWAYS;
	} else if (p_string == "depth_draw_never") {
		return BaseMaterial3D::DEPTH_DRAW_DISABLED;
	}
	// default
	return BaseMaterial3D::DEPTH_DRAW_OPAQUE_ONLY;
}

BaseMaterial3D::DepthTest depth_test_from_string(const String &p_string) {
	if (p_string == "depth_test_default") {
		return BaseMaterial3D::DEPTH_TEST_DEFAULT;
	} else if (p_string == "depth_test_inverted") {
		return BaseMaterial3D::DEPTH_TEST_INVERTED;
	}
	// default
	return BaseMaterial3D::DEPTH_TEST_DEFAULT;
}

BaseMaterial3D::CullMode cull_mode_from_string(const String &p_string) {
	if (p_string == "cull_back") {
		return BaseMaterial3D::CULL_BACK;
	} else if (p_string == "cull_front") {
		return BaseMaterial3D::CULL_FRONT;
	} else if (p_string == "cull_disabled") {
		return BaseMaterial3D::CULL_DISABLED;
	}
	// default
	return BaseMaterial3D::CULL_BACK;
}

BaseMaterial3D::SpecularMode specular_mode_from_string(const String &p_string) {
	if (p_string == "specular_schlick_ggx") {
		return BaseMaterial3D::SPECULAR_SCHLICK_GGX;
	} else if (p_string == "specular_toon") {
		return BaseMaterial3D::SPECULAR_TOON;
	} else if (p_string == "specular_disabled") {
		return BaseMaterial3D::SPECULAR_DISABLED;
	}
	// default
	return BaseMaterial3D::SPECULAR_SCHLICK_GGX;
}

BaseMaterial3D::DiffuseMode diffuse_mode_from_string(const String &p_string) {
	if (p_string == "diffuse_lambert") {
		return BaseMaterial3D::DIFFUSE_LAMBERT;
	} else if (p_string == "diffuse_lambert_wrap") {
		return BaseMaterial3D::DIFFUSE_LAMBERT_WRAP;
	} else if (p_string == "diffuse_burley") {
		return BaseMaterial3D::DIFFUSE_BURLEY;
	}
	// default
	return BaseMaterial3D::DIFFUSE_BURLEY;
}

HashMap<String, Variant> set_params_from_render_mode(const Vector<StringName> &render_modes, const Ref<BaseMaterial3D> &base_material) {
	HashMap<String, Variant> params;
	for (String E : render_modes) {
		if (E.begins_with("blend_")) {
			auto blend_mode = blend_mode_from_string(E);
			base_material->set_blend_mode(blend_mode);
			params["blend_mode"] = blend_mode;
		} else if (E.begins_with("depth_draw_")) {
			auto depth_draw_mode = depth_draw_mode_from_string(E);
			base_material->set_depth_draw_mode(depth_draw_mode);
			params["depth_draw_mode"] = depth_draw_mode;
		} else if (E.begins_with("cull_")) {
			auto cull_mode = cull_mode_from_string(E);
			base_material->set_cull_mode(cull_mode);
			params["cull_mode"] = cull_mode;
		} else if (E.begins_with("diffuse_")) {
			auto diffuse_mode = diffuse_mode_from_string(E);
			base_material->set_diffuse_mode(diffuse_mode);
			params["diffuse_mode"] = diffuse_mode;
		} else if (E.begins_with("specular_")) {
			auto specular_mode = specular_mode_from_string(E);
			base_material->set_specular_mode(specular_mode);
			params["specular_mode"] = specular_mode;
		} else if (E.begins_with("depth_test_")) {
			auto depth_test = depth_test_from_string(E);
			base_material->set_depth_test(depth_test);
			params["depth_test"] = depth_test;
		} else if (E == "unshaded") {
			base_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
			params["shading_mode"] = BaseMaterial3D::SHADING_MODE_UNSHADED;
		} else if (E == "sss_mode_skin") {
			base_material->set_feature(BaseMaterial3D::FEATURE_SUBSURFACE_SCATTERING, true);
			params["subsurf_scatter_enabled"] = true;
		} else if (E == "shadows_disabled") {
			base_material->set_flag(BaseMaterial3D::FLAG_DONT_RECEIVE_SHADOWS, true);
			params["shadows_disabled"] = true;
		} else if (E == "ambient_light_disabled") {
			base_material->set_flag(BaseMaterial3D::FLAG_DISABLE_AMBIENT_LIGHT, true);
			params["ambient_light_disabled"] = true;
		} else if (E == "shadow_to_opacity") {
			base_material->set_flag(BaseMaterial3D::FLAG_USE_SHADOW_TO_OPACITY, true);
			params["shadow_to_opacity"] = true;
		} else if (E == "vertex_lighting") {
			base_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_PER_VERTEX);
			params["shading_mode"] = BaseMaterial3D::SHADING_MODE_PER_VERTEX;
		} else if (E == "particle_trails") {
			base_material->set_flag(BaseMaterial3D::FLAG_PARTICLE_TRAILS_MODE, true);
			params["particle_trails"] = true;
		} else if (E == "alpha_to_coverage") {
			base_material->set_alpha_antialiasing(BaseMaterial3D::ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE);
			params["alpha_antialiasing_mode"] = BaseMaterial3D::ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE;
		} else if (E == "alpha_to_coverage_and_one") {
			base_material->set_alpha_antialiasing(BaseMaterial3D::ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE_AND_TO_ONE);
			params["alpha_antialiasing_mode"] = BaseMaterial3D::ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE_AND_TO_ONE;
		} else if (E == "fog_disabled") {
			base_material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
			params["fog_disabled"] = true;
		} else if (E == "specular_occlusion_disabled") {
			base_material->set_flag(BaseMaterial3D::FLAG_DISABLE_SPECULAR_OCCLUSION, true);
			params["specular_occlusion_disabled"] = true;
		} else if (E == "depth_prepass_alpha") {
			base_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_DEPTH_PRE_PASS);
			params["transparency"] = BaseMaterial3D::TRANSPARENCY_ALPHA_DEPTH_PRE_PASS;
		}
		// UNUSED RENDER MODES:
		// "light_only", "collision_use_scale", "disable_force", "disable_velocity", "debug_shadow_splits"
		// "skip_vertex_transform", "world_vertex_coords", "ensure_correct_normals", "wireframe",
		// "keep_data", "use_half_res_pass",
		// "use_quarter_res_pass", "use_debanding"
	}
	return params;
}

static inline void copy_resource_props(const Ref<ShaderMaterial> &p_shader_material, Ref<BaseMaterial3D> &p_base_material) {
	p_base_material->set_name(p_shader_material->get_name());
	p_base_material->set_local_to_scene(p_shader_material->is_local_to_scene());
	p_base_material->set_path_cache(p_shader_material->get_path());
	p_base_material->merge_meta_from(p_shader_material.ptr());
}

Pair<Ref<BaseMaterial3D>, Pair<bool, bool>> ShaderMaterialConverter::convert_shader_material_to_base_material(Ref<ShaderMaterial> p_shader_material, Node *p_parent) {
	// we need to manually create a BaseMaterial3D from the shader material
	// We do this by getting the shader uniforms and then mapping them to the BaseMaterial3D properties
	// We also need to handle the texture parameters and features
	using Hint = ShaderLanguage::ShaderNode::Uniform::Hint;
	ERR_FAIL_COND_V_MSG(p_shader_material.is_null(), {}, "ShaderMaterial is NULL");
	List<StringName> list;
	Ref<Shader> shader = p_shader_material->get_shader();
	if (shader.is_null()) {
#ifdef DEBUG_ENABLED
		WARN_PRINT("Shader on ShaderMaterial is NULL: " + p_shader_material->get_path());
#endif
		Ref<BaseMaterial3D> base_material = memnew(BaseMaterial3D(false));
		copy_resource_props(p_shader_material, base_material);
		return { base_material, { false, false } };
	}

	Ref<ResourceInfo> resource_info = ResourceInfo::get_info_from_resource(p_shader_material);
	bool is_deprecated = resource_info.is_valid() && resource_info->get_ver_major() < 4;

	List<PropertyInfo> prop_list;
	HashSet<String> global_uniforms;
	HashSet<String> instance_uniforms;
	//(global|instance)?\s*uniform\s+(\w+)\s+(\w+)\s*(?:\:\s+(\w+))?

	// Don't match "normal" or "form"
	Ref<RegEx> orm_re = RegEx::create_from_string("(^ORM|[^NF]ORM[a-z_]|[a-z_]ORM|^orm|_orm|[^NnFf]orm_|Orm).*");
	auto is_orm = [&](const String &p_param_name) {
		return p_param_name == "orm" || orm_re->search(p_param_name).is_valid();
	};
	bool has_orm = false;
	HashSet<String> shader_uniforms;
	ShaderMaterialConverter::ShaderInfo shader_info = parse_shader_info(shader);

	std::function<void()> ensure_shader_params = [&]() {
		for (auto &E : shader_info.uniforms) {
			auto &info = E.value;
			if (p_shader_material->get_shader_parameter(info.name).get_type() == Variant::NIL) {
				p_shader_material->set_shader_parameter(info.name, info.default_value);
			}
		}
	};
	// run this on the main thread to avoid race conditions with the shader material's `get_property_list()`, which sets the shader parameter cache
	TaskManager::get_singleton()->dispatch_to_main_thread(ensure_shader_params);

	for (auto &E : shader_info.uniforms) {
		auto &info = E.value;
		shader_uniforms.insert(info.name);
		if (info.scope == UniformInfo::Scope::SCOPE_GLOBAL) {
			global_uniforms.insert(info.name);
		} else if (info.scope == UniformInfo::Scope::SCOPE_INSTANCE) {
			instance_uniforms.insert(info.name);
		}
		if (info.is_texture()) {
			if (is_orm(info.name)) {
				has_orm = true;
			}
		}
	}

	p_shader_material->get_property_list(&prop_list, false);
	for (auto &E : prop_list) {
		if (E.usage == PROPERTY_USAGE_CATEGORY || E.usage == PROPERTY_USAGE_GROUP || E.usage == PROPERTY_USAGE_SUBGROUP) {
			continue;
		}
		if (E.name.begins_with("shader_parameter/")) {
			shader_uniforms.insert(E.name.trim_prefix("shader_parameter/"));
		}
	}

	Ref<BaseMaterial3D> base_material = memnew(BaseMaterial3D(has_orm));

	// this is initialized with the 3.x params that are still valid in 4.x (`_set()` handles the remapping) but won't show up in the property list
	static const HashMap<String, Variant::Type> v3_material_param_to_type_map = {
		{ "flags_use_shadow_to_opacity", Variant::Type::BOOL },
		{ "flags_no_depth_test", Variant::Type::BOOL },
		{ "flags_use_point_size", Variant::Type::BOOL },
		{ "flags_fixed_size", Variant::Type::BOOL },
		{ "flags_albedo_tex_force_srgb", Variant::Type::BOOL },
		{ "flags_do_not_receive_shadows", Variant::Type::BOOL },
		{ "flags_disable_ambient_light", Variant::Type::BOOL },
		{ "params_diffuse_mode", Variant::Type::INT },
		{ "params_specular_mode", Variant::Type::INT },
		{ "params_blend_mode", Variant::Type::INT },
		{ "params_cull_mode", Variant::Type::INT },
		{ "params_depth_draw_mode", Variant::Type::INT },
		{ "params_point_size", Variant::Type::FLOAT },
		{ "params_billboard_mode", Variant::Type::INT },
		{ "params_billboard_keep_scale", Variant::Type::BOOL },
		{ "params_grow", Variant::Type::BOOL },
		{ "params_grow_amount", Variant::Type::FLOAT },
		{ "params_alpha_scissor_threshold", Variant::Type::FLOAT },
		{ "params_alpha_hash_scale", Variant::Type::FLOAT },
		{ "params_alpha_antialiasing_edge", Variant::Type::INT },
		{ "depth_scale", Variant::Type::FLOAT },
		{ "depth_deep_parallax", Variant::Type::FLOAT },
		{ "depth_min_layers", Variant::Type::INT },
		{ "depth_max_layers", Variant::Type::INT },
		{ "depth_flip_tangent", Variant::Type::BOOL },
		{ "depth_flip_binormal", Variant::Type::BOOL },
		{ "depth_texture", Variant::Type::FLOAT },
		{ "emission_energy", Variant::Type::FLOAT },
		{ "flags_transparent", Variant::Type::BOOL },
		{ "flags_unshaded", Variant::Type::BOOL },
		{ "flags_vertex_lighting", Variant::Type::BOOL },
		{ "params_use_alpha_scissor", Variant::Type::BOOL },
		{ "params_use_alpha_hash", Variant::Type::BOOL },
		{ "depth_enabled", Variant::Type::BOOL },
	};
	HashSet<String> base_material_params;
	for (auto &E : v3_material_param_to_type_map) {
		base_material_params.insert(E.key);
	}
	List<PropertyInfo> base_material_prop_list;
	base_material->get_property_list(&base_material_prop_list);
	bool hit_base_material_group = false;
	for (auto &E : base_material_prop_list) {
		if (E.usage == PROPERTY_USAGE_CATEGORY && E.name == "BaseMaterial3D") {
			hit_base_material_group = true;
		}
		if (!hit_base_material_group) {
			continue;
		}
		if (E.usage == PROPERTY_USAGE_CATEGORY || E.usage == PROPERTY_USAGE_GROUP || E.usage == PROPERTY_USAGE_SUBGROUP) {
			if (E.name != "StandardMaterial3D") {
				continue;
			}
			break;
		}
		base_material_params.insert(E.name);
	}

	// base shader switches the texture names in the shader
	static const HashMap<String, String> shader_texture_name_to_base_material_texture_param = {
		{ "texture_albedo", "albedo_texture" },
		{ "texture_orm", "orm_texture" },
		{ "texture_metallic", "metallic_texture" },
		{ "texture_roughness", "roughness_texture" },
		{ "texture_emission", "emission_texture" },
		{ "texture_normal", "normal_texture" },
		{ "texture_bent_normal", "bent_normal_texture" },
		{ "texture_rim", "rim_texture" },
		{ "texture_clearcoat", "clearcoat_texture" },
		{ "texture_flowmap", "anisotropy_flowmap" },
		{ "texture_ambient_occlusion", "ao_texture" },
		{ "texture_heightmap", "heightmap_texture" },
		{ "texture_subsurface_scattering", "subsurf_scatter_texture" },
		{ "texture_subsurface_transmittance", "subsurf_scatter_transmittance_texture" },
		{ "texture_backlight", "backlight_texture" },
		{ "texture_refraction", "refraction_texture" },
		{ "texture_detail_mask", "detail_mask" },
		{ "texture_detail_albedo", "detail_albedo" },
		{ "texture_detail_normal", "detail_normal" },
		{ "texture_depth", "depth_texture" },
		{ "texture_orm", "orm_texture" },
	};

	static const HashMap<String, BaseMaterial3D::TextureParam> texture_param_map = {
		{ "albedo_texture", BaseMaterial3D::TEXTURE_ALBEDO },
		{ "orm_texture", BaseMaterial3D::TEXTURE_ORM },
		{ "metallic_texture", BaseMaterial3D::TEXTURE_METALLIC },
		{ "roughness_texture", BaseMaterial3D::TEXTURE_ROUGHNESS },
		{ "emission_texture", BaseMaterial3D::TEXTURE_EMISSION },
		{ "normal_texture", BaseMaterial3D::TEXTURE_NORMAL },
		{ "bent_normal_texture", BaseMaterial3D::TEXTURE_BENT_NORMAL },
		{ "rim_texture", BaseMaterial3D::TEXTURE_RIM },
		{ "clearcoat_texture", BaseMaterial3D::TEXTURE_CLEARCOAT },
		{ "anisotropy_flowmap", BaseMaterial3D::TEXTURE_FLOWMAP },
		{ "ao_texture", BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION },
		{ "heightmap_texture", BaseMaterial3D::TEXTURE_HEIGHTMAP },
		{ "subsurf_scatter_texture", BaseMaterial3D::TEXTURE_SUBSURFACE_SCATTERING },
		{ "subsurf_scatter_transmittance_texture", BaseMaterial3D::TEXTURE_SUBSURFACE_TRANSMITTANCE },
		{ "backlight_texture", BaseMaterial3D::TEXTURE_BACKLIGHT },
		{ "refraction_texture", BaseMaterial3D::TEXTURE_REFRACTION },
		{ "detail_mask", BaseMaterial3D::TEXTURE_DETAIL_MASK },
		{ "detail_albedo", BaseMaterial3D::TEXTURE_DETAIL_ALBEDO },
		{ "detail_normal", BaseMaterial3D::TEXTURE_DETAIL_NORMAL },
		{ "depth_texture", BaseMaterial3D::TEXTURE_HEIGHTMAP }, // old 3.x name for heightmap texture
		{ "orm_texture", BaseMaterial3D::TEXTURE_ORM },
	};

	const HashMap<BaseMaterial3D::TextureParam, String> texture_param_val_to_name_map = [&]() {
		HashMap<BaseMaterial3D::TextureParam, String> map;
		for (auto &E : texture_param_map) {
			map.insert(E.value, E.key);
		}
		return map;
	}();

	static const HashMap<BaseMaterial3D::TextureParam, BaseMaterial3D::Feature> feature_to_enable_map = {
		{ BaseMaterial3D::TEXTURE_NORMAL, BaseMaterial3D::FEATURE_NORMAL_MAPPING },
		{ BaseMaterial3D::TEXTURE_BENT_NORMAL, BaseMaterial3D::FEATURE_BENT_NORMAL_MAPPING },
		{ BaseMaterial3D::TEXTURE_EMISSION, BaseMaterial3D::FEATURE_EMISSION },
		{ BaseMaterial3D::TEXTURE_RIM, BaseMaterial3D::FEATURE_RIM },
		{ BaseMaterial3D::TEXTURE_CLEARCOAT, BaseMaterial3D::FEATURE_CLEARCOAT },
		{ BaseMaterial3D::TEXTURE_FLOWMAP, BaseMaterial3D::FEATURE_ANISOTROPY },
		{ BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION },
		{ BaseMaterial3D::TEXTURE_HEIGHTMAP, BaseMaterial3D::FEATURE_HEIGHT_MAPPING },
		{ BaseMaterial3D::TEXTURE_SUBSURFACE_SCATTERING, BaseMaterial3D::FEATURE_SUBSURFACE_SCATTERING },
		{ BaseMaterial3D::TEXTURE_SUBSURFACE_TRANSMITTANCE, BaseMaterial3D::FEATURE_SUBSURFACE_TRANSMITTANCE },
		{ BaseMaterial3D::TEXTURE_BACKLIGHT, BaseMaterial3D::FEATURE_BACKLIGHT },
		{ BaseMaterial3D::TEXTURE_REFRACTION, BaseMaterial3D::FEATURE_REFRACTION },
		{ BaseMaterial3D::TEXTURE_DETAIL_MASK, BaseMaterial3D::FEATURE_DETAIL },
		{ BaseMaterial3D::TEXTURE_DETAIL_ALBEDO, BaseMaterial3D::FEATURE_DETAIL },
		{ BaseMaterial3D::TEXTURE_DETAIL_NORMAL, BaseMaterial3D::FEATURE_DETAIL },
	};
	using TextureCandidate = Pair<UniformInfo, Ref<Texture>>;

	bool set_texture = false;
	HashMap<BaseMaterial3D::TextureParam, Vector<TextureCandidate>> base_material_texture_candidates;
	for (int i = 0; i < BaseMaterial3D::TEXTURE_MAX; i++) {
		base_material_texture_candidates[BaseMaterial3D::TextureParam(i)] = Vector<TextureCandidate>();
	}
	HashSet<String> set_param_material_names;
	HashMap<String, Pair<String, Variant>> set_params;
	HashMap<String, Pair<String, Variant>> non_set_params;
	auto add_orm_texture_candidate = [&](const UniformInfo &info, const Ref<Texture> &tex) {
		String lparam_name = info.name.to_lower();
		bool did_set = true;
		if (lparam_name.contains("metallic") || lparam_name.contains("metalness")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_METALLIC].push_back({ info, tex });
		} else if (lparam_name.contains("roughness") || lparam_name.contains("rough")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ROUGHNESS].push_back({ info, tex });
		} else if (lparam_name.contains("ao") || lparam_name.contains("occlusion") || lparam_name.contains("ambient_occlusion")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION].push_back({ info, tex });
		} else if (is_orm(info.name)) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ORM].push_back({ info, tex });
		} else {
			did_set = false;
		}
		return did_set;
	};

	auto third_pass_texture_candidate = [&](const UniformInfo &info, const Ref<Texture> &tex) {
		if (info.hints.has(Hint::HINT_ROUGHNESS_NORMAL) && info.name.to_lower().contains("normal") && base_material_texture_candidates[BaseMaterial3D::TEXTURE_NORMAL].is_empty()) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_NORMAL].push_back({ info, tex });
			return true;
		} else if (info.hints.has(Hint::HINT_NONE)) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ALBEDO].push_back({ info, tex });
			return true;
		}
		return false;
	};

	auto second_pass_texture_candidate = [&](const UniformInfo &info, const Ref<Texture> &tex) {
		const String &param_name = info.name;
		String lparam_name = param_name.to_lower();
		if (lparam_name.contains("albedo")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ALBEDO].push_back({ info, tex });
		} else if (lparam_name.contains("normal") && !info.hints.has(Hint::HINT_ROUGHNESS_NORMAL)) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_NORMAL].push_back({ info, tex });
		} else if (lparam_name.contains("emissive") || lparam_name.contains("emission")) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back({ info, tex });
		} else {
			return add_orm_texture_candidate(info, tex);
		}
		return true;
	};

	auto first_pass_texture_candidates = [&](const UniformInfo &info, const Ref<Texture> &tex) {
		if (info.hints.has(Hint::HINT_SOURCE_COLOR)) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_ALBEDO].push_back({ info, tex });
		} else if (info.hints.has(Hint::HINT_DEFAULT_WHITE)) {
			return add_orm_texture_candidate(info, tex);
		} else if (info.hints.has(Hint::HINT_NORMAL)) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_NORMAL].push_back({ info, tex });
		} else if (info.hints.has(Hint::HINT_DEFAULT_BLACK)) {
			base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back({ info, tex });
		} else {
			return false;
		}
		return true;
	};

	auto filter_texture_candidates = [&](const Vector<TextureCandidate> &candidates, bool ignore_non_2d) {
		Vector<TextureCandidate> filtered_candidates;
		HashSet<int64_t> added;
		for (int i = 0; i < candidates.size(); i++) {
			auto &E = candidates[i];
			if ((!ignore_non_2d || E.first.is_texture_2d()) && first_pass_texture_candidates(E.first, E.second)) {
				added.insert(i);
			}
		}
		for (int i = 0; i < candidates.size(); i++) {
			auto &E = candidates[i];
			if (!added.has(i)) {
				if ((!ignore_non_2d || E.first.is_texture_2d()) && second_pass_texture_candidate(E.first, E.second)) {
					added.insert(i);
				}
			}
		}
		for (int i = 0; i < candidates.size(); i++) {
			auto &E = candidates[i];
			if (!added.has(i)) {
				if ((!ignore_non_2d || E.first.is_texture_2d()) && third_pass_texture_candidate(E.first, E.second)) {
					added.insert(i);
				}
			}
		}

		for (int i = 0; i < candidates.size(); i++) {
			if (!added.has(i)) {
				filtered_candidates.push_back(candidates[i]);
			}
		}
		return filtered_candidates;
	};

	auto remap_param_name = [&](const String &param_name, Variant &val) -> String {
		if (shader_texture_name_to_base_material_texture_param.has(param_name)) {
			return shader_texture_name_to_base_material_texture_param.get(param_name);
		}
		if (!base_material_params.has(param_name)) {
			if (base_material_params.has("params_" + param_name)) {
				return "params_" + param_name;
			}
			if (base_material_params.has("flags_" + param_name)) {
				return "flags_" + param_name;
			}
		}
		if (param_name.contains("use") && param_name.contains("vertex_color") && p_shader_material->get_shader_parameter(param_name).get_type() == Variant::BOOL) {
			return "vertex_color_use_as_albedo";
		}
		if (is_deprecated) {
			if (param_name == "emission_intensity" && val.get_type() == Variant::FLOAT && val.operator double() < 1000.0f) {
				return "emission_energy_multiplier";
			} else if (param_name == "clearcoat_gloss" && val.get_type() == Variant::FLOAT) {
				val = 1.0 - val.operator double();
				return "clearcoat_roughness";
			}
		}

		return param_name;
	};

	Vector<TextureCandidate> unknown_texture_candidates;
	for (auto &E : shader_info.uniforms) {
		auto &info = E.value;
		String real_param_name = E.key;
		String param_name = real_param_name;

		Variant val = p_shader_material->get_shader_parameter(info.name);
		if (val.get_type() == Variant::NIL) {
			val = info.default_value;
		}
		bool did_set = false;
		if (info.is_global()) {
			Variant global_val = GDRESettings::get_singleton()->get_shader_global(real_param_name);
			if (global_val.get_type() != Variant::NIL) {
				val = global_val;
			}
		} else if (info.is_instance() && p_parent) {
			bool valid = false;
			Variant instance_val = p_parent->get("instance_shader_parameters/" + real_param_name, &valid);
			if (valid) {
				val = instance_val;
			}
		}
		param_name = remap_param_name(param_name, val);

		if (base_material_params.has(param_name)) {
			Variant base_material_val = base_material->get(param_name);
			Variant::Type base_material_val_type = base_material_val.get_type();
			if (v3_material_param_to_type_map.has(param_name)) {
				base_material_val_type = v3_material_param_to_type_map.get(param_name);
			}

			if (base_material_val_type == val.get_type()) {
				base_material->set(param_name, val);
				did_set = true;
				set_params[real_param_name] = { param_name, val };
				set_param_material_names.insert(param_name);
			}
		} else if (set_param_not_in_property_list(base_material, param_name, val)) {
			did_set = true;
			set_params[real_param_name] = { param_name, val };
			set_param_material_names.insert(param_name);
		}
		if (!did_set) {
			non_set_params[real_param_name] = { param_name, val };
		}

		Ref<Texture2D> tex = get_texture_2d(val, info);
		if (!tex.is_valid()) {
			continue;
		}
		if (did_set) {
			set_texture = true;
			auto param = texture_param_map.has(param_name) ? texture_param_map.get(param_name) : BaseMaterial3D::TEXTURE_MAX;

			if (param != BaseMaterial3D::TEXTURE_MAX) {
				base_material_texture_candidates[param].push_back({ info, tex });
				continue;
			} else {
				WARN_PRINT(vformat("Unknown texture parameter: %s", param_name));
			}
		}

		unknown_texture_candidates.push_back({ info, tex });
	}
	unknown_texture_candidates = filter_texture_candidates(unknown_texture_candidates, true);
	unknown_texture_candidates = filter_texture_candidates(unknown_texture_candidates, false);
	for (int i = 0; i < BaseMaterial3D::TEXTURE_MAX; i++) {
		auto &candidates = base_material_texture_candidates[BaseMaterial3D::TextureParam(i)];
		auto existing_tex = base_material->get_texture(BaseMaterial3D::TextureParam(i));

		if ((!candidates.is_empty() || existing_tex.is_valid()) && feature_to_enable_map.has(BaseMaterial3D::TextureParam(i))) {
			base_material->set_feature(feature_to_enable_map[BaseMaterial3D::TextureParam(i)], true);
		}

		if (existing_tex.is_valid() || candidates.is_empty()) {
			continue;
		}
		if (candidates.size() == 1) {
			base_material->set_texture(BaseMaterial3D::TextureParam(i), candidates[0].second);
			set_texture = true;
			set_params[candidates[0].first.name] = { texture_param_val_to_name_map.get(BaseMaterial3D::TextureParam(i)), candidates[0].second };
			set_param_material_names.insert(texture_param_val_to_name_map.get(BaseMaterial3D::TextureParam(i)));
			continue;
		}
		bool set_this_texture = false;
		if (i == BaseMaterial3D::TEXTURE_ALBEDO) {
			for (auto &E : candidates) {
				if (E.first.name.contains("albedo") || E.first.name.contains("color")) {
					base_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, E.second);
					set_texture = true;
					set_this_texture = true;
					set_params[E.first.name] = { "albedo_texture", E.second };
					set_param_material_names.insert("albedo_texture");
				} else if (E.first.name.contains("emissi") && set_this_texture) {
					// push this back to the emission candidates
					base_material_texture_candidates[BaseMaterial3D::TEXTURE_EMISSION].push_back(E);
				}
			}
		} else if (i == BaseMaterial3D::TEXTURE_NORMAL) {
			for (auto &E : candidates) {
				if (E.first.name.contains("normal")) {
					base_material->set_texture(BaseMaterial3D::TEXTURE_NORMAL, E.second);
					set_texture = true;
					set_this_texture = true;
					set_params[E.first.name] = { "normal_texture", E.second };
					set_param_material_names.insert("normal_texture");
				}
			}
		}
		if (!set_this_texture) {
			// just use the first one
			base_material->set_texture(BaseMaterial3D::TextureParam(i), candidates[0].second);
			set_texture = true;
			set_params[candidates[0].first.name] = { texture_param_val_to_name_map.get(BaseMaterial3D::TextureParam(i)), candidates[0].second };
			set_param_material_names.insert(texture_param_val_to_name_map.get(BaseMaterial3D::TextureParam(i)));
		}
	}
	auto render_mode_params = set_params_from_render_mode(shader_info.render_modes, base_material);
	for (auto &E : render_mode_params) {
		set_params[E.key] = { E.key, E.value };
		set_param_material_names.insert(E.key);
	}
	// These params modify the transparency
	static const Vector<String> transparency_params = { "params_use_alpha_hash", "params_use_alpha_scissor", "transparency" };

	bool transparency_set = gdre::hashset_intersects_vector(set_param_material_names, transparency_params) || base_material->get_transparency() != BaseMaterial3D::TRANSPARENCY_DISABLED;
	if (set_param_material_names.has("alpha_scissor_threshold") && !transparency_set) {
		base_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
		transparency_set = true;
	}
	if (shader_uniforms.has("brightness") && p_shader_material->get_shader_parameter("brightness").get_type() == Variant::FLOAT) {
		float brightness = p_shader_material->get_shader_parameter("brightness");
		Color get_emission = base_material->get_emission();
		if (get_emission == Color()) {
			get_emission = base_material->get_albedo();
		}
		base_material->set_emission(get_emission * brightness);
	}
	if (!transparency_set && shader_uniforms.has("alpha")) {
		Variant alpha_val = p_shader_material->get_shader_parameter("alpha");
		if (alpha_val.get_type() == Variant::FLOAT) {
			base_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			base_material->set_alpha_antialiasing(BaseMaterial3D::ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE);
			Color albedo = base_material->get_albedo();
			albedo.a = alpha_val;
			base_material->set_albedo(albedo);
			transparency_set = true;
		}
	}
	// set the resource properties
	copy_resource_props(p_shader_material, base_material);

	bool set_instance_uniforms = false;
	for (auto &E : set_params) {
		if (instance_uniforms.has(E.key)) {
			set_instance_uniforms = true;
			break;
		}
	}

	if (!transparency_set) {
		auto albedo_tex = base_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
		if (albedo_tex.is_valid()) {
			Ref<Image> albedo_image = albedo_tex->get_image();
			if (albedo_image.is_valid()) {
				Image::AlphaMode alpha_mode = albedo_image->detect_alpha();
				if (alpha_mode == Image::ALPHA_BLEND) {
					base_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
				} else if (alpha_mode == Image::ALPHA_BIT) {
					base_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
				}
			}
		}
	}
	return { base_material, { set_texture, set_instance_uniforms } };
}
