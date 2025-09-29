/**************************************************************************/
/*  mesh_editor_plugin.cpp                                                */
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

#include "scene_previewer.h"

#include "compat/input_event_parser_v2.h"
#include "core/config/project_settings.h"
#include "main/main.h"
#include "scene/2d/camera_2d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/main/viewport.h"

#include "utility/gdre_settings.h"

void ScenePreviewer3D::gui_input(const Ref<InputEvent> &p_event) {
	ERR_FAIL_COND(p_event.is_null());

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		if ((mm->get_button_mask().has_flag(MouseButtonMask::LEFT))) {
			rot_x -= mm->get_relative().y * 0.01;
			rot_y -= mm->get_relative().x * 0.01;

			rot_x = CLAMP(rot_x, -Math::PI / 2, Math::PI / 2);
			_update_rotation();
		}
	}

	Ref<InputEventMagnifyGesture> mg = p_event;
	if (mg.is_valid()) {
		scale *= mg->get_factor();
		_update_rotation();
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::WHEEL_UP) {
		scale *= 1.1;
		_update_rotation();
	}
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::WHEEL_DOWN) {
		scale *= 0.9;
		_update_rotation();
	}
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && (key->get_keycode() == Key::EQUAL || key->get_keycode() == Key::PLUS)) {
		scale *= 1.1;
		_update_rotation();
	} else if (key.is_valid() && key->is_pressed() && key->get_keycode() == Key::MINUS) {
		scale *= 0.9;
		_update_rotation();
	}
}

void ScenePreviewer3D::_update_theme_item_cache() {
	SubViewportContainer::_update_theme_item_cache();
	theme_cache.light_1_icon = get_theme_icon(SNAME("MaterialPreviewLight1"), SNAME("EditorIcons"));
	theme_cache.light_2_icon = get_theme_icon(SNAME("MaterialPreviewLight2"), SNAME("EditorIcons"));
}

void ScenePreviewer3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			light_1_switch->set_button_icon(theme_cache.light_1_icon);
			light_2_switch->set_button_icon(theme_cache.light_2_icon);
		} break;
	}
}

void ScenePreviewer3D::_update_rotation() {
	Transform3D t;
	t.basis.rotate(Vector3(0, 1, 0), -rot_y);
	t.basis.rotate(Vector3(1, 0, 0), -rot_x);
	t.scale(scale);
	rotation->set_transform(t);
	// Transform3D camera_transform = camera->get_transform();
	// camera_transform.origin = center;
	// camera->set_transform(camera_transform);
}

AABB _calculate_aabb_for_scene(Node *p_node, AABB &p_scene_aabb) {
	MeshInstance3D *mesh_node = Object::cast_to<MeshInstance3D>(p_node);
	if (mesh_node && mesh_node->get_mesh().is_valid()) {
		Transform3D accum_xform;
		Node3D *base = mesh_node;
		while (base) {
			accum_xform = base->get_transform() * accum_xform;
			base = Object::cast_to<Node3D>(base->get_parent());
		}

		AABB aabb = accum_xform.xform(mesh_node->get_mesh()->get_aabb());
		p_scene_aabb.merge_with(aabb);
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		p_scene_aabb = _calculate_aabb_for_scene(p_node->get_child(i), p_scene_aabb);
	}

	return p_scene_aabb;
}

void ScenePreviewer3D::edit(Node3D *p_root) {
	// setup_3d();
	root = p_root;

	rot_x = Math::deg_to_rad(-15.0);
	rot_y = Math::deg_to_rad(30.0);

	AABB aabb;
	aabb = _calculate_aabb_for_scene(root, aabb);
	center = aabb.get_center();
	scale = Vector3(1.0, 1.0, 1.0);
	_update_rotation();
	Transform3D root_transform = root->get_transform();
	root_transform.origin = center;
	rotation->add_child(root);
	root->set_transform(root_transform);

	Vector3 ofs = aabb.get_center();
	float m = aabb.get_longest_axis_size();
	if (m != 0) {
		m = 1.0 / m;
		m *= 0.5;
		Transform3D xform;
		xform.basis.scale(Vector3(m, m, m));
		xform.origin = -xform.basis.xform(ofs); //-ofs*m;
		//xform.origin.z -= aabb.get_longest_axis_size() * 2;
		root->set_transform(xform);
	}
}

void ScenePreviewer3D::reset() {
	if (root) {
		rotation->remove_child(root);
		root->queue_free();
		root = nullptr;
	}
	// if (viewport) {
	// 	remove_child(viewport);
	// 	viewport->queue_free();
	// 	viewport = nullptr;
	// }
}

void ScenePreviewer3D::_on_light_1_switch_pressed() {
	light1->set_visible(light_1_switch->is_pressed());
}

void ScenePreviewer3D::_on_light_2_switch_pressed() {
	light2->set_visible(light_2_switch->is_pressed());
}

ScenePreviewer3D::ScenePreviewer3D() {
	setup_3d();
}

void ScenePreviewer3D::setup_3d() {
	viewport = memnew(SubViewport);
	Ref<World3D> world_3d;
	world_3d.instantiate();
	viewport->set_world_3d(world_3d); // Use own world.
	add_child(viewport);
	viewport->set_disable_input(true);
	viewport->set_msaa_3d(Viewport::MSAA_4X);
	set_stretch(true);
	camera = memnew(Camera3D);
	camera->set_transform(Transform3D(Basis(), Vector3(0, 0, 1.1)));
	camera->set_perspective(45, 0.1, 10);
	viewport->add_child(camera);

	if (GLOBAL_GET("rendering/lights_and_shadows/use_physical_light_units")) {
		camera_attributes.instantiate();
		camera->set_attributes(camera_attributes);
	}

	light1 = memnew(DirectionalLight3D);
	light1->set_transform(Transform3D().looking_at(Vector3(-1, -1, -1), Vector3(0, 1, 0)));
	viewport->add_child(light1);

	light2 = memnew(DirectionalLight3D);
	light2->set_transform(Transform3D().looking_at(Vector3(0, 1, 0), Vector3(0, 0, 1)));
	light2->set_color(Color(0.7, 0.7, 0.7));
	viewport->add_child(light2);

	rotation = memnew(Node3D);
	viewport->add_child(rotation);

	set_custom_minimum_size(Size2(1, 150) * GDRESettings::get_singleton()->get_auto_display_scale());

	HBoxContainer *hb = memnew(HBoxContainer);
	add_child(hb);
	hb->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT, Control::PRESET_MODE_MINSIZE, 2);

	hb->add_spacer();

	VBoxContainer *vb_light = memnew(VBoxContainer);
	hb->add_child(vb_light);

	light_1_switch = memnew(Button);
	light_1_switch->set_theme_type_variation("PreviewLightButton");
	light_1_switch->set_toggle_mode(true);
	light_1_switch->set_pressed(true);
	light_1_switch->set_accessibility_name(TTRC("First Light"));
	vb_light->add_child(light_1_switch);
	light_1_switch->connect(SceneStringName(pressed), callable_mp(this, &ScenePreviewer3D::_on_light_1_switch_pressed));

	light_2_switch = memnew(Button);
	light_2_switch->set_theme_type_variation("PreviewLightButton");
	light_2_switch->set_toggle_mode(true);
	light_2_switch->set_pressed(true);
	light_2_switch->set_accessibility_name(TTRC("Second Light"));
	vb_light->add_child(light_2_switch);
	light_2_switch->connect(SceneStringName(pressed), callable_mp(this, &ScenePreviewer3D::_on_light_2_switch_pressed));

	rot_x = 0;
	rot_y = 0;
}

void ScenePreviewer3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("edit", "mesh"), &ScenePreviewer3D::edit);
	ClassDB::bind_method(D_METHOD("reset"), &ScenePreviewer3D::reset);
}

void ScenePreviewer2D::edit(Node *p_root) {
	root = p_root;
	viewport->add_child(root);
}

void ScenePreviewer2D::reset() {
	if (root) {
		viewport->remove_child(root);
		root->queue_free();
		root = nullptr;
	}
}

ScenePreviewer2D::ScenePreviewer2D() {
	viewport = memnew(SubViewport);
	viewport->set_disable_input(true);
	viewport->set_disable_3d(true);
	set_stretch(true);
	add_child(viewport);
	// set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT, Control::PRESET_MODE_MINSIZE, 0);
}

ScenePreviewer::ScenePreviewer() {
	set_theme_type_variation("ScenePreviewer");
	previewer_3d = memnew(ScenePreviewer3D);
	previewer_2d = memnew(ScenePreviewer2D);
	previewer_2d->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	previewer_2d->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	previewer_2d->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	previewer_2d->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(previewer_3d);
	previewer_3d->set_visible(false);
	add_child(previewer_2d);
	previewer_2d->set_visible(false);
}

void ScenePreviewer::edit(Ref<PackedScene> p_scene) {
	reset();
	scene = p_scene;
	Node *root = scene->instantiate();
	root->set_process_mode(Node::PROCESS_MODE_DISABLED);
	root->set_process(false);
	root->set_process_internal(false);
	root->set_physics_process(false);
	root->set_physics_process_internal(false);

	Node3D *root_3d = Object::cast_to<Node3D>(root);
	if (root_3d) {
		previewer_3d->edit(root_3d);
		previewer_3d->set_visible(true);
	} else {
		previewer_2d->edit(root);
		previewer_2d->set_visible(true);
	}
	root->set_process_mode(Node::PROCESS_MODE_DISABLED);
	root->set_process(false);
	root->set_process_internal(false);
	root->set_physics_process(false);
	root->set_physics_process_internal(false);
}

void ScenePreviewer::reset() {
	previewer_3d->reset();
	previewer_2d->reset();
	previewer_3d->set_visible(false);
	previewer_2d->set_visible(false);
	scene = nullptr;
}

void ScenePreviewer2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("edit", "scene"), &ScenePreviewer2D::edit);
	ClassDB::bind_method(D_METHOD("reset"), &ScenePreviewer2D::reset);
}

void ScenePreviewer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("edit", "scene"), &ScenePreviewer::edit);
	ClassDB::bind_method(D_METHOD("reset"), &ScenePreviewer::reset);
	ClassDB::bind_method(D_METHOD("get_edited_resource_path"), &ScenePreviewer::get_edited_resource_path);
}

String ScenePreviewer::get_edited_resource_path() const {
	if (scene.is_valid()) {
		return scene->get_path();
	}
	return "";
}
