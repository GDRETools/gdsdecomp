/**************************************************************************/
/*  texture_layered_editor_plugin.cpp                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "texture_layered_previewer.h"

#include "core/input/input.h"
#include "core/object/callable_mp.h"
#include "scene/gui/label.h"

#include "gui/gdre_color_channel_selector.h"

#include "gui/gui_icons.h"

// Shader sources.

constexpr const char *array_2d_shader = R"(
	// TextureLayeredPreviewer preview shader (2D array).

	shader_type canvas_item;

	uniform sampler2DArray tex;
	uniform float layer;
	uniform vec4 u_channel_factors = vec4(1.0);

	vec4 filter_preview_colors(vec4 input_color, vec4 factors) {
		// Filter RGB.
		vec4 output_color = input_color * vec4(factors.rgb, input_color.a);

		// Remove transparency when alpha is not enabled.
		output_color.a = mix(1.0, output_color.a, factors.a);

		// Switch to opaque grayscale when visualizing only one channel.
		float csum = factors.r + factors.g + factors.b + factors.a;
		float single = clamp(2.0 - csum, 0.0, 1.0);
		for (int i = 0; i < 4; i++) {
			float c = input_color[i];
			output_color = mix(output_color, vec4(c, c, c, 1.0), factors[i] * single);
		}

		return output_color;
	}

	void fragment() {
		COLOR = textureLod(tex, vec3(UV, layer), 0.0);
		COLOR = filter_preview_colors(COLOR, u_channel_factors);
	}
)";

constexpr const char *cubemap_shader = R"(
	// TextureLayeredPreviewer preview shader (cubemap).

	shader_type canvas_item;

	uniform samplerCube tex;
	uniform vec3 normal;
	uniform mat3 rot;

	uniform vec4 u_channel_factors = vec4(1.0);

	vec4 filter_preview_colors(vec4 input_color, vec4 factors) {
		// Filter RGB.
		vec4 output_color = input_color * vec4(factors.rgb, input_color.a);

		// Remove transparency when alpha is not enabled.
		output_color.a = mix(1.0, output_color.a, factors.a);

		// Switch to opaque grayscale when visualizing only one channel.
		float csum = factors.r + factors.g + factors.b + factors.a;
		float single = clamp(2.0 - csum, 0.0, 1.0);
		for (int i = 0; i < 4; i++) {
			float c = input_color[i];
			output_color = mix(output_color, vec4(c, c, c, 1.0), factors[i] * single);
		}

		return output_color;
	}

	void fragment() {
		vec3 n = rot * normalize(vec3(normal.xy * (UV * 2.0 - 1.0), normal.z));
		COLOR = textureLod(tex, n, 0.0);
		COLOR = filter_preview_colors(COLOR, u_channel_factors);
	}
)";

constexpr const char *cubemap_array_shader = R"(
	// TextureLayeredPreviewer preview shader (cubemap array).

	shader_type canvas_item;
	uniform samplerCubeArray tex;
	uniform vec3 normal;
	uniform mat3 rot;
	uniform float layer;

	uniform vec4 u_channel_factors = vec4(1.0);

	vec4 filter_preview_colors(vec4 input_color, vec4 factors) {
		// Filter RGB.
		vec4 output_color = input_color * vec4(factors.rgb, input_color.a);

		// Remove transparency when alpha is not enabled.
		output_color.a = mix(1.0, output_color.a, factors.a);

		// Switch to opaque grayscale when visualizing only one channel.
		float csum = factors.r + factors.g + factors.b + factors.a;
		float single = clamp(2.0 - csum, 0.0, 1.0);
		for (int i = 0; i < 4; i++) {
			float c = input_color[i];
			output_color = mix(output_color, vec4(c, c, c, 1.0), factors[i] * single);
		}

		return output_color;
	}

	void fragment() {
		vec3 n = rot * normalize(vec3(normal.xy * (UV * 2.0 - 1.0), normal.z));
		COLOR = textureLod(tex, vec4(n, layer), 0.0);
		COLOR = filter_preview_colors(COLOR, u_channel_factors);
	}
)";

constexpr const char *texture_3d_shader = R"(
	// Texture3DEditor preview shader.

	shader_type canvas_item;

	uniform sampler3D tex;
	uniform float layer;

	uniform vec4 u_channel_factors = vec4(1.0);

	vec4 filter_preview_colors(vec4 input_color, vec4 factors) {
		// Filter RGB.
		vec4 output_color = input_color * vec4(factors.rgb, input_color.a);

		// Remove transparency when alpha is not enabled.
		output_color.a = mix(1.0, output_color.a, factors.a);

		// Switch to opaque grayscale when visualizing only one channel.
		float csum = factors.r + factors.g + factors.b + factors.a;
		float single = clamp(2.0 - csum, 0.0, 1.0);
		for (int i = 0; i < 4; i++) {
			float c = input_color[i];
			output_color = mix(output_color, vec4(c, c, c, 1.0), factors[i] * single);
		}

		return output_color;
	}

	void fragment() {
		COLOR = textureLod(tex, vec3(UV, layer), 0.0);
		COLOR = filter_preview_colors(COLOR, u_channel_factors);
	}
)";

void TextureLayeredPreviewer::gui_input(const Ref<InputEvent> &p_event) {
	ERR_FAIL_COND(p_event.is_null());

	if (!use_rotation) {
		return;
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && mm->get_button_mask().has_flag(MouseButtonMask::RIGHT)) {
		if (Input::get_singleton()->get_mouse_mode() == Input::MouseMode::MOUSE_MODE_VISIBLE) {
			Input::get_singleton()->set_mouse_mode(Input::MouseMode::MOUSE_MODE_CAPTURED);
		}

		y_rot += mm->get_relative().x * 0.01;
		x_rot = CLAMP(x_rot - mm->get_relative().y * 0.01, -Math::PI * 0.5f, Math::PI * 0.5f);

		_update_material(false);
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::RIGHT) {
		if (Input::get_singleton()->get_mouse_mode() == Input::MouseMode::MOUSE_MODE_CAPTURED) {
			Input::get_singleton()->set_mouse_mode(Input::MouseMode::MOUSE_MODE_VISIBLE);
			Input::get_singleton()->warp_mouse(original_mouse_pos);
		} else if (Input::get_singleton()->get_mouse_mode() == Input::MouseMode::MOUSE_MODE_VISIBLE) {
			original_mouse_pos = mb->get_global_position();
		}
	}
}

void TextureLayeredPreviewer::_texture_rect_draw() {
	texture_rect->draw_rect(Rect2(Point2(), texture_rect->get_size()), Color(1, 1, 1, 1));
}

void TextureLayeredPreviewer::_update_gui() {
	if (texture.is_null()) {
		return;
	}

	_texture_rect_update_area();

	Ref<TextureLayered> texture_layered = texture;
	Ref<Texture3D> texture_3d = texture;
	ERR_FAIL_COND(texture_layered.is_null() && texture_3d.is_null());
	_texture_rect_update_area();

	const Image::Format format = texture_layered.is_valid() ? texture_layered->get_format() : texture_3d->get_format();
	const String format_name = Image::get_format_name(format);
	String texture_info;

	bool has_mipmaps = false;
	int mip_count = 0;
	int width = 0;
	int height = 0;
	int layers = 0;
	if (texture_layered.is_valid()) {
		has_mipmaps = texture_layered->has_mipmaps();
		width = texture_layered->get_width();
		height = texture_layered->get_height();
		layers = texture_layered->get_layers();
		switch (texture_layered->get_layered_type()) {
			case TextureLayered::LAYERED_TYPE_2D_ARRAY: {
				layer->set_max(texture_layered->get_layers() - 1);

				texture_info = vformat(String::utf8("%d×%d (×%d) %s\n"),
						texture_layered->get_width(),
						texture_layered->get_height(),
						texture_layered->get_layers(),
						format_name);

			} break;
			case TextureLayered::LAYERED_TYPE_CUBEMAP: {
				layer->hide();

				texture_info = vformat(String::utf8("%d×%d %s\n"),
						texture_layered->get_width(),
						texture_layered->get_height(),
						format_name);

			} break;
			case TextureLayered::LAYERED_TYPE_CUBEMAP_ARRAY: {
				layer->set_max(texture_layered->get_layers() / 6 - 1);

				texture_info = vformat(String::utf8("%d×%d (×%d) %s\n"),
						texture_layered->get_width(),
						texture_layered->get_height(),
						texture_layered->get_layers() / 6,
						format_name);

			} break;

			default: {
			}
		}
	} else { // texture_3d
		has_mipmaps = texture_3d->has_mipmaps();
		width = texture_3d->get_width();
		height = texture_3d->get_height();
		layers = texture_3d->get_depth();
		layer->set_max(texture_3d->get_depth() - 1);

		texture_info = vformat(String::utf8("%d×%d×%d %s\n"),
				texture_3d->get_width(),
				texture_3d->get_height(),
				texture_3d->get_depth(),
				format_name);
	}
	if (has_mipmaps) {
		mip_count = Image::get_image_required_mipmaps(width, height, format);
		const int memory = Image::get_image_data_size(width, height, format, true) * layers;

		texture_info += vformat(RTR("%s Mipmaps") + "\n" + RTR("Memory: %s"),
				mip_count,
				String::humanize_size(memory));

	} else {
		const int memory = Image::get_image_data_size(width, height, format, false) * layers;

		texture_info += vformat(RTR("No Mipmaps") + "\n" + RTR("Memory: %s"),
				String::humanize_size(memory));
	}

	info->set_text(texture_info);

	const uint32_t components_mask = Image::get_format_component_mask(format);
	if (Math::is_power_of_2(components_mask)) {
		// Only one channel available, no point in showing a channel selector.
		channel_selector->hide();
	} else {
		channel_selector->show();
		channel_selector->set_available_channels_mask(components_mask);
	}
}

void TextureLayeredPreviewer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_RESIZED: {
			_texture_rect_update_area();
		} break;

		case NOTIFICATION_DRAW: {
			Ref<Texture2D> checkerboard = GDREGuiIcons::get_icon(SNAME("Checkerboard"), get_theme_default_base_scale());
			draw_texture_rect(checkerboard, texture_rect->get_rect(), true);
			_draw_outline();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			if (info) {
				Ref<Font> metadata_label_font = get_theme_font(SNAME("expression"));
				info->add_theme_font_override(SceneStringName(font), metadata_label_font);
			}
			theme_cache.outline_color = get_theme_color(SNAME("extra_border_color_1"));
		} break;
	}
}

void TextureLayeredPreviewer::_texture_changed() {
	if (!is_visible()) {
		return;
	}

	setting = true;
	_update_gui();
	setting = false;

	_update_material(true);
	queue_redraw();
}

void TextureLayeredPreviewer::_update_material(bool p_texture_changed) {
	if (materials[0].is_null()) {
		WARN_PRINT("materials are null, creating...");
		_make_materials();
	}
	materials[0]->set_shader_parameter("layer", layer->get_value());
	materials[2]->set_shader_parameter("layer", layer->get_value());
	materials[3]->set_shader_parameter("layer", layer->get_value());

	Vector3 v(-1, -1, -1);
	v.normalize();

	Basis b;
	b.rotate(Vector3(1, 0, 0), x_rot);
	b.rotate(Vector3(0, 1, 0), y_rot);

	materials[1]->set_shader_parameter("normal", v);
	materials[1]->set_shader_parameter("rot", b);
	materials[2]->set_shader_parameter("normal", v);
	materials[2]->set_shader_parameter("rot", b);

	if (p_texture_changed) {
		int index = get_material_index();
		use_rotation = (TextureLayered::LayeredType)index == TextureLayered::LAYERED_TYPE_CUBEMAP || (TextureLayered::LayeredType)index == TextureLayered::LAYERED_TYPE_CUBEMAP_ARRAY;
		RID rid = texture.is_valid() ? texture->get_rid() : RID();
		materials[index]->set_shader_parameter("tex", rid);
	}

	const Vector4 channel_factors = channel_selector->get_selected_channel_factors();
	for (unsigned int i = 0; i < 3; ++i) {
		materials[i]->set_shader_parameter("u_channel_factors", channel_factors);
	}
}

void TextureLayeredPreviewer::on_selected_channels_changed() {
	_update_material(false);
}

void TextureLayeredPreviewer::_draw_outline() {
	const float outline_width = Math::round(get_theme_default_base_scale());
	const Rect2 outline_rect = texture_rect->get_rect().grow(outline_width * 0.5);
	draw_rect(outline_rect, theme_cache.outline_color, false, outline_width);
}

void TextureLayeredPreviewer::_make_materials() {
	for (int i = 0; i < MATERIAL_COUNT; i++) {
		materials[i].instantiate();
		materials[i]->set_shader(shaders[i]);
	}
}

Pair<int, int> TextureLayeredPreviewer::get_texture_size() const {
	if (Ref<TextureLayered> texture_layered = texture; texture_layered.is_valid()) {
		return Pair<int, int>(texture_layered->get_width(), texture_layered->get_height());
	} else if (Ref<Texture3D> texture_3d = texture; texture_3d.is_valid()) {
		return Pair<int, int>(texture_3d->get_width(), texture_3d->get_height());
	}
	return Pair<int, int>(0, 0);
}

int TextureLayeredPreviewer::get_material_index() const {
	if (Ref<TextureLayered> texture_layered = texture; texture_layered.is_valid()) {
		return (int)texture_layered->get_layered_type();
	} else if (Ref<Texture3D> texture_3d = texture; texture_3d.is_valid()) {
		return MATERIAL_COUNT - 1;
	}
	return 0;
}

void TextureLayeredPreviewer::_texture_rect_update_area() {
	if (texture.is_null()) {
		return;
	}

	auto [base_width, base_height] = get_texture_size();

	Size2 size = get_size();
	int tex_width = base_width * size.height / base_height;
	int tex_height = size.height;

	if (tex_width > size.width) {
		tex_width = size.width;
		tex_height = base_height * tex_width / base_width;
	}

	// Prevent the texture from being unpreviewable after the rescale, so that we can still see something
	if (tex_height <= 0) {
		tex_height = 1;
	}
	if (tex_width <= 0) {
		tex_width = 1;
	}

	int ofs_x = (size.width - tex_width) / 2;
	int ofs_y = (size.height - tex_height) / 2;

	texture_rect->set_position(Vector2(ofs_x, ofs_y - Math::round(get_theme_default_base_scale())));
	texture_rect->set_size(Vector2(tex_width, tex_height));
}

void TextureLayeredPreviewer::init_shaders() {
	shaders[0].instantiate();
	shaders[0]->set_code(array_2d_shader);

	shaders[1].instantiate();
	shaders[1]->set_code(cubemap_shader);

	shaders[2].instantiate();
	shaders[2]->set_code(cubemap_array_shader);

	shaders[3].instantiate();
	shaders[3]->set_code(texture_3d_shader);
}

void TextureLayeredPreviewer::finish_shaders() {
	shaders[0].unref();
	shaders[1].unref();
	shaders[2].unref();
	shaders[3].unref();
}

String TextureLayeredPreviewer::get_previewer_name() const {
	return "texture_layered";
}

Error TextureLayeredPreviewer::edit(Ref<Resource> p_texture) {
	if (texture.is_valid()) {
		texture->disconnect_changed(callable_mp(this, &TextureLayeredPreviewer::_texture_changed));
	}
	ERR_FAIL_COND_V_MSG(p_texture.is_null(), ERR_INVALID_PARAMETER, "Texture is null");
	ERR_FAIL_COND_V_MSG(Ref<Texture3D>(p_texture).is_null() && Ref<TextureLayered>(p_texture).is_null(), ERR_INVALID_PARAMETER, "Texture is not a TextureLayered or Texture3D");
	texture = p_texture;

	if (texture.is_valid()) {
		if (materials[0].is_null()) {
			_make_materials();
		}

		texture->connect_changed(callable_mp(this, &TextureLayeredPreviewer::_texture_changed));
		texture_rect->set_material(materials[get_material_index()]);

		setting = true;
		layer->set_value(0);
		layer->show();
		_update_gui();
		setting = false;

		x_rot = 0;
		y_rot = 0;

		_update_material(true);
		queue_redraw();

	} else {
		hide();
	}
	return OK;
}

void TextureLayeredPreviewer::reset() {
	if (texture.is_valid()) {
		texture->disconnect_changed(callable_mp(this, &TextureLayeredPreviewer::_texture_changed));
		materials[get_material_index()]->set_shader_parameter("tex", RID());
	}
	texture = nullptr;
	hide();
}

ResourceInfo::LoadType TextureLayeredPreviewer::get_load_type() const {
	return ResourceInfo::LoadType::REAL_LOAD;
}

String TextureLayeredPreviewer::get_edited_resource_path() const {
	if (texture.is_valid()) {
		return texture->get_path();
	}
	return "";
}

TextureLayeredPreviewer::TextureLayeredPreviewer() {
	set_texture_repeat(TextureRepeat::TEXTURE_REPEAT_ENABLED);
	set_custom_minimum_size(Size2(0, 256.0) * get_theme_default_base_scale());

	texture_rect = memnew(Control);
	texture_rect->set_mouse_filter(MOUSE_FILTER_IGNORE);
	texture_rect->connect(SceneStringName(draw), callable_mp(this, &TextureLayeredPreviewer::_texture_rect_draw));

	add_child(texture_rect);

	layer = memnew(SpinBox);
	layer->set_step(1);
	layer->set_max(100);

	layer->set_modulate(Color(1, 1, 1, 0.8));
	layer->set_h_grow_direction(GROW_DIRECTION_BEGIN);
	layer->set_anchor(SIDE_RIGHT, 1);
	layer->set_anchor(SIDE_LEFT, 1);
	layer->connect(SceneStringName(value_changed), callable_mp(this, &TextureLayeredPreviewer::_layer_changed));

	add_child(layer);

	channel_selector = memnew(GDREColorChannelSelector);
	channel_selector->connect("selected_channels_changed", callable_mp(this, &TextureLayeredPreviewer::on_selected_channels_changed));
	channel_selector->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	add_child(channel_selector);

	info = memnew(Label);
	info->set_focus_mode(FOCUS_ACCESSIBILITY);
	info->add_theme_color_override(SceneStringName(font_color), Color(1, 1, 1));
	info->add_theme_color_override("font_shadow_color", Color(0, 0, 0));
	info->add_theme_font_size_override(SceneStringName(font_size), 14 * get_theme_default_base_scale());
	info->add_theme_color_override("font_outline_color", Color(0, 0, 0));
	info->add_theme_constant_override("outline_size", 8 * get_theme_default_base_scale());

	info->set_h_grow_direction(GROW_DIRECTION_BEGIN);
	info->set_v_grow_direction(GROW_DIRECTION_BEGIN);
	info->set_h_size_flags(Control::SIZE_SHRINK_END);
	info->set_v_size_flags(Control::SIZE_SHRINK_END);
	info->set_anchor(SIDE_RIGHT, 1);
	info->set_anchor(SIDE_LEFT, 1);
	info->set_anchor(SIDE_BOTTOM, 1);
	info->set_anchor(SIDE_TOP, 1);

	add_child(info);
}

bool TextureLayeredPreviewer::can_edit(const String &p_resource_path, const String &p_type) const {
	String ext = p_resource_path.get_file().get_extension().to_lower();
	if (ext == "ctexarray" || ext == "ccube" || ext == "ccubearray" || ext == "texarr" || ext == "ctex3d" || ext == "tex3d") {
		return true;
	}

	if (!p_type.is_empty()) {
		return p_type == "ImageLayeredTexture" || p_type == "TextureLayered" || p_type == "ImageTexture3D" || p_type == "StreamTextureArray" || p_type == "CompressedTexture2DArray" || p_type == "CompressedCubemap" || p_type == "CompressedCubemapArray" || p_type == "TextureArray" || p_type == "CompressedTexture3D" || p_type == "StreamTexture3D" || p_type == "Texture3D";
	}

	return false;
}
