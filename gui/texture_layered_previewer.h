/**************************************************************************/
/*  texture_layered_editor_plugin.h                                       */
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

#pragma once

#include "gui/gdre_previewer.h"

#include "scene/gui/spin_box.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/resources/texture.h"

class GDREColorChannelSelector;

class TextureLayeredPreviewer : public GDREPreviewer {
	GDCLASS(TextureLayeredPreviewer, GDREPreviewer);

	struct ThemeCache {
		Color outline_color;
	} theme_cache;

	SpinBox *layer = nullptr;
	Label *info = nullptr;
	Ref<Texture> texture;

	static constexpr const int MATERIAL_COUNT = 4;

	static inline Ref<Shader> shaders[MATERIAL_COUNT];
	Ref<ShaderMaterial> materials[MATERIAL_COUNT];

	float x_rot = 0;
	float y_rot = 0;
	Control *texture_rect = nullptr;

	bool setting = false;
	Vector2 original_mouse_pos;
	bool use_rotation = false;

	GDREColorChannelSelector *channel_selector = nullptr;

	void _draw_outline();

	void _make_materials();
	void _update_material(bool p_texture_changed);

	void _layer_changed(double) {
		if (!setting) {
			_update_material(false);
		}
	}

	void _texture_changed();

	void _texture_rect_update_area();
	void _texture_rect_draw();

	void _update_gui();

	void on_selected_channels_changed();

	Pair<int, int> get_texture_size() const;

	int get_material_index() const;

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;

public:
	static void init_shaders();
	static void finish_shaders();

	virtual String get_previewer_name() const override;
	virtual Error edit(Ref<Resource> p_texture) override;
	virtual bool can_edit(const String &p_resource_path, const String &p_resource_type) const override;
	virtual String get_edited_resource_path() const override;
	virtual void reset() override;
	virtual ResourceInfo::LoadType get_load_type() const override;

	TextureLayeredPreviewer();
};
