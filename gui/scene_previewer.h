/**************************************************************************/
/*  mesh_editor_plugin.h                                                  */
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

#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/packed_scene.h"

class SubViewport;
class Button;

class ScenePreviewer3D : public SubViewportContainer {
	GDCLASS(ScenePreviewer3D, SubViewportContainer);

	float rot_x;
	float rot_y;
	Vector3 center;
	Vector3 scale = Vector3(1.0, 1.0, 1.0);

	Node3D *root = nullptr;
	SubViewport *viewport = nullptr;
	Node3D *rotation = nullptr;
	DirectionalLight3D *light1 = nullptr;
	DirectionalLight3D *light2 = nullptr;
	Camera3D *camera = nullptr;
	Ref<CameraAttributesPractical> camera_attributes;

	Button *light_1_switch = nullptr;
	Button *light_2_switch = nullptr;

	struct ThemeCache {
		Ref<Texture2D> light_1_icon;
		Ref<Texture2D> light_2_icon;
	} theme_cache;

	void _on_light_1_switch_pressed();
	void _on_light_2_switch_pressed();
	void _update_rotation();

protected:
	virtual void _update_theme_item_cache() override;
	void _notification(int p_what);
	void gui_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods();
	void setup_3d();
	void setup_2d();
	// void edit_3d(Node3D *root);

public:
	void edit(Node3D *root);
	void reset();
	ScenePreviewer3D();
};

class ScenePreviewer2D : public SubViewportContainer {
	GDCLASS(ScenePreviewer2D, SubViewportContainer);

	Node *root = nullptr;
	SubViewport *viewport = nullptr;

protected:
	static void _bind_methods();

public:
	void edit(Node *root);
	void reset();
	ScenePreviewer2D();
};

class ScenePreviewer : public MarginContainer {
	GDCLASS(ScenePreviewer, MarginContainer);

	Ref<PackedScene> scene;
	ScenePreviewer3D *previewer_3d = nullptr;
	ScenePreviewer2D *previewer_2d = nullptr;

protected:
	static void _bind_methods();

public:
	void edit(Ref<PackedScene> p_scene);
	String get_edited_resource_path() const;
	void reset();
	ScenePreviewer();
};
