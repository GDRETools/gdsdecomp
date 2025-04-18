/**************************************************************************/
/*  progress_dialog.cpp                                                   */
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

#include "gdre_progress.h"

#include "core/os/os.h"
#include "main/main.h"
#include "servers/display_server.h"
#include "utility/gdre_standalone.h"

#include <utility/gd_parallel_queue.h>
#include <utility/gdre_settings.h>
#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#endif
void GDREBackgroundProgress::_add_task(const String &p_task, const String &p_label, int p_steps) {
	_THREAD_SAFE_METHOD_
	ERR_FAIL_COND_MSG(tasks.has(p_task), "Task '" + p_task + "' already exists.");
	GDREBackgroundProgress::Task t;
	t.hb = memnew(HBoxContainer);
	Label *l = memnew(Label);
	l->set_text(p_label + " ");
	t.hb->add_child(l);
	t.progress = memnew(ProgressBar);
	t.progress->set_max(p_steps);
	t.progress->set_value(p_steps);
	Control *ec = memnew(Control);
	ec->set_h_size_flags(SIZE_EXPAND_FILL);
	ec->set_v_size_flags(SIZE_EXPAND_FILL);
	t.progress->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	ec->add_child(t.progress);
	ec->set_custom_minimum_size(Size2(80, 5) * GDRESettings::get_singleton()->get_auto_display_scale());
	t.hb->add_child(ec);

	add_child(t.hb);

	tasks[p_task] = t;
}

void GDREBackgroundProgress::_update() {
	_THREAD_SAFE_METHOD_

	for (const KeyValue<String, int> &E : updates) {
		if (tasks.has(E.key)) {
			_task_step(E.key, E.value);
		}
	}

	updates.clear();
}

void GDREBackgroundProgress::_task_step(const String &p_task, int p_step) {
	_THREAD_SAFE_METHOD_

	ERR_FAIL_COND(!tasks.has(p_task));

	Task &t = tasks[p_task];
	if (p_step < 0) {
		t.progress->set_value(t.progress->get_value() + 1);
	} else {
		t.progress->set_value(p_step);
	}
}

void GDREBackgroundProgress::_end_task(const String &p_task) {
	_THREAD_SAFE_METHOD_

	ERR_FAIL_COND(!tasks.has(p_task));
	Task &t = tasks[p_task];

	memdelete(t.hb);
	tasks.erase(p_task);
}

void GDREBackgroundProgress::add_task(const String &p_task, const String &p_label, int p_steps) {
	callable_mp(this, &GDREBackgroundProgress::_add_task).call_deferred(p_task, p_label, p_steps);
}

void GDREBackgroundProgress::task_step(const String &p_task, int p_step) {
	//this code is weird, but it prevents deadlock.
	bool no_updates = true;
	{
		_THREAD_SAFE_METHOD_
		no_updates = updates.is_empty();
	}

	if (no_updates) {
		callable_mp(this, &GDREBackgroundProgress::_update).call_deferred();
	}

	{
		_THREAD_SAFE_METHOD_
		updates[p_task] = p_step;
	}
}

void GDREBackgroundProgress::end_task(const String &p_task) {
	callable_mp(this, &GDREBackgroundProgress::_end_task).call_deferred(p_task);
}

////////////////////////////////////////////////

GDREProgressDialog *GDREProgressDialog::singleton = nullptr;

void GDREProgressDialog::_update_ui() {
	// Run main loop for two frames.
	if (is_inside_tree()) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}
}

void GDREProgressDialog::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS) {
		main_thread_update();
	}
}

void GDREProgressDialog::Task::init(VBoxContainer *main) {
	ERR_FAIL_COND(vb);
	vb = memnew(VBoxContainer);
	VBoxContainer *vb2 = memnew(VBoxContainer);
	vb->add_margin_child(label, vb2);
	progress = memnew(ProgressBar);
	if (indeterminate) {
		steps = 1;
		progress->set_indeterminate(true);
	}
	progress->set_max(steps);
	progress->set_value(steps);
	vb2->add_child(progress);
	state = memnew(Label);
	state->set_clip_text(true);
	vb2->add_child(state);
	main->add_child(vb);
	initialized = true;
}

void GDREProgressDialog::Task::set_step(const String &p_state, int p_step, bool p_force_redraw) {
	current_step.state = p_state;
	if (p_step == -1) {
		current_step.step++;
	} else {
		current_step.step = p_step;
	}
	force_next_redraw = force_next_redraw || p_force_redraw;
}

bool GDREProgressDialog::Task::should_redraw(uint64_t curr_time_us) const {
	return force_next_redraw || curr_time_us - last_progress_tick >= 200000;
}

bool GDREProgressDialog::Task::update() {
	ERR_FAIL_COND_V(!initialized, false);
	if (!should_redraw(OS::get_singleton()->get_ticks_usec())) {
		return false;
	}
	bool was_forced = force_next_redraw;
	force_next_redraw = false;
	if (!vb) {
		return false;
	}
	if (!indeterminate) {
		if (!was_forced && state->get_text() == current_step.state && progress->get_value() == current_step.step) {
			return false;
		}
		progress->set_value(current_step.step);
	}
	state->set_text(current_step.state);
	last_progress_tick = OS::get_singleton()->get_ticks_usec();
	return true;
}

void GDREProgressDialog::_popup() {
	Size2 ms = main->get_combined_minimum_size();
	ms.width = MAX(500 * GDRESettings::get_singleton()->get_auto_display_scale(), ms.width);

	Ref<StyleBox> style = main->get_theme_stylebox(SceneStringName(panel), SNAME("PopupMenu"));
	ms += style->get_minimum_size();

	main->set_offset(SIDE_LEFT, style->get_margin(SIDE_LEFT));
	main->set_offset(SIDE_RIGHT, -style->get_margin(SIDE_RIGHT));
	main->set_offset(SIDE_TOP, style->get_margin(SIDE_TOP));
	main->set_offset(SIDE_BOTTOM, -style->get_margin(SIDE_BOTTOM));
	if (is_inside_tree()) {
		Rect2i adjust = _popup_adjust_rect();
		if (adjust != Rect2i()) {
			set_position(adjust.position);
			set_size(adjust.size);
		}
		popup_centered(ms);
	} else {
		for (Window *window : host_windows) {
			if (window->has_focus()) {
				if (!is_inside_tree()) {
					popup_exclusive_centered(window, ms);
				} else if (get_parent() != window) {
					reparent(window);
					popup_centered(ms);
				}
				return;
			}
		}
		// No host window found, use main window.
		if (GodotREEditorStandalone::get_singleton()) {
			if (!is_inside_tree()) {
				popup_exclusive_centered(GodotREEditorStandalone::get_singleton(), ms);
			} else {
				reparent(GodotREEditorStandalone::get_singleton()->get_parent_window());
				popup_centered(ms);
			}
		} else {
#ifdef TOOLS_ENABLED
			EditorInterface::get_singleton()->popup_dialog_centered(this, ms);
#endif
		}
	}
	set_process_input(true);
	grab_focus();
}

void GDREProgressDialog::_post_add_task(bool p_can_cancel) {
	if (p_can_cancel) {
		cancel_hb->show();
	} else {
		cancel_hb->hide();
	}
	cancel_hb->move_to_front();
	_popup();
	if (p_can_cancel) {
		cancel->grab_focus();
	}
	_update_ui();
}

bool GDREProgressDialog::is_safe_to_redraw() {
	return Thread::is_main_thread() && !MessageQueue::get_singleton()->is_flushing();
}

void GDREProgressDialog::add_task(const String &p_task, const String &p_label, int p_steps, bool p_can_cancel) {
	ERR_FAIL_COND_MSG(tasks.contains(p_task), "Task '" + p_task + "' already exists.");
	Task t = { p_task, p_label, p_steps, p_can_cancel, p_steps == -1 };
	tasks.try_emplace_l(p_task, [=](TaskMap::value_type &v) {}, t);
	canceled = false;
	if (!is_safe_to_redraw()) {
		return;
	}

	main_thread_update();
}

bool GDREProgressDialog::task_step(const String &p_task, const String &p_state, int p_step, bool p_force_redraw) {
	ERR_FAIL_COND_V(!tasks.contains(p_task), canceled);
	bool is_main_thread = is_safe_to_redraw();
	bool do_update = false;
	tasks.if_contains(p_task, [&](TaskMap::value_type &t) {
		t.second.set_step(p_state, p_step, p_force_redraw);
		if (is_main_thread) {
			do_update = t.second.should_redraw(OS::get_singleton()->get_ticks_usec());
		}
	});
	if (do_update) {
		main_thread_update();
	}

	return canceled;
}

bool GDREProgressDialog::_process_removals() {
	String p_task;
	bool has_deletions = false;
	while (queued_removals.try_pop(p_task)) {
		bool has = tasks.if_contains(p_task, [](TaskMap::value_type &t) {
			if (t.second.vb) {
				memdelete(t.second.vb);
				t.second.vb = nullptr;
			}
		});
		if (has) {
			tasks.erase(p_task);
			has_deletions = true;
		}
	}
	return has_deletions;
}

void GDREProgressDialog::end_task(const String &p_task) {
	ERR_FAIL_COND(!tasks.contains(p_task));
	queued_removals.try_push(p_task);
	if (!is_safe_to_redraw()) {
		return;
	}
	main_thread_update();
}

void GDREProgressDialog::main_thread_update() {
	bool should_update = _process_removals();
	bool p_can_cancel = false;
	bool initialized = false;
	uint64_t size = 0;
	tasks.for_each_m([&](TaskMap::value_type &E) {
		Task &t = E.second;
		if (!t.initialized) {
			initialized = true;
			t.init(main);
		}
		if (t.update()) {
			should_update = true;
		}
		p_can_cancel = p_can_cancel || t.can_cancel;
		size++;
	});
	if (should_update || initialized) {
		if (size == 0) {
			hide();
		} else if (initialized) {
			_post_add_task(p_can_cancel);
		} else {
			_update_ui();
		}
	}
}

void GDREProgressDialog::add_host_window(Window *p_window) {
	ERR_FAIL_NULL(p_window);
	if (!host_windows.has(p_window)) {
		host_windows.push_back(p_window);
	}
}

void GDREProgressDialog::remove_host_window(Window *p_window) {
	ERR_FAIL_NULL(p_window);
	if (host_windows.has(p_window)) {
		host_windows.erase(p_window);
	}
}

void GDREProgressDialog::_cancel_pressed() {
	canceled = true;
}

GDREProgressDialog::GDREProgressDialog() {
	main = memnew(VBoxContainer);
	add_child(main);
	main->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	set_exclusive(true);
	set_flag(Window::FLAG_POPUP, false);
	singleton = this;
	cancel_hb = memnew(HBoxContainer);
	main->add_child(cancel_hb);
	cancel_hb->hide();
	cancel = memnew(Button);
	cancel_hb->add_spacer();
	cancel_hb->add_child(cancel);
	cancel->set_text(RTR("Cancel"));
	cancel_hb->add_spacer();
	cancel->connect(SceneStringName(pressed), callable_mp(this, &GDREProgressDialog::_cancel_pressed));
	set_process(true);
}

String EditorProgressGDDC::get_task() {
	return task;
}

Ref<EditorProgressGDDC> EditorProgressGDDC::create(Node *p_parent, const String &p_task, const String &p_label, int p_amount, bool p_can_cancel) {
	return memnew(EditorProgressGDDC(nullptr, p_task, p_label, p_amount, p_can_cancel));
}

void EditorProgressGDDC::_bind_methods() {
	ClassDB::bind_method(D_METHOD("step", "state", "step", "force_refresh"), &EditorProgressGDDC::step, DEFVAL(-1), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_task"), &EditorProgressGDDC::get_task);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("create", "parent", "task", "label", "amount", "can_cancel"), &EditorProgressGDDC::create, DEFVAL(false));
}
bool EditorProgressGDDC::step(const String &p_state, int p_step, bool p_force_refresh) {
	if (GDRESettings::get_singleton() && GDRESettings::get_singleton()->is_headless()) {
		return false;
	}
	if (GDREProgressDialog::get_singleton()) {
		return GDREProgressDialog::get_singleton()->task_step(task, p_state, p_step, p_force_refresh);
	} else {
#ifdef TOOLS_ENABLED
		if (Thread::is_main_thread()) {
			return EditorNode::progress_task_step(task, p_state, p_step, p_force_refresh);
		} else {
			EditorNode::progress_task_step_bg(task, p_step);
			return false;
		}
#endif
	}
	return false;
}
EditorProgressGDDC::EditorProgressGDDC() {}
EditorProgressGDDC::EditorProgressGDDC(const String &p_task, const String &p_label, int p_amount, bool p_can_cancel) :
		EditorProgressGDDC(nullptr, p_task, p_label, p_amount, p_can_cancel) {}
EditorProgressGDDC::EditorProgressGDDC(Node *p_parent, const String &p_task, const String &p_label, int p_amount, bool p_can_cancel) {
	task = p_task;
	if (GDRESettings::get_singleton() && GDRESettings::get_singleton()->is_headless()) {
		return;
	}
	if (GDREProgressDialog::get_singleton()) {
		if (p_parent) {
			GDREProgressDialog::get_singleton()->add_host_window(p_parent->get_window());
		}
		GDREProgressDialog::get_singleton()->add_task(p_task, p_label, p_amount, p_can_cancel);
	} else {
#ifdef TOOLS_ENABLED
		if (Thread::is_main_thread()) {
			EditorNode::progress_add_task(p_task, p_label, p_amount, p_can_cancel);
		} else {
			EditorNode::progress_add_task_bg(p_task, p_label, p_amount);
		}
#endif
	}
}

EditorProgressGDDC::~EditorProgressGDDC() {
	// if no EditorNode...
	if (GDRESettings::get_singleton() && GDRESettings::get_singleton()->is_headless()) {
		// do nothing
	} else if (GDREProgressDialog::get_singleton()) {
		GDREProgressDialog::get_singleton()->end_task(task);
	} else {
#ifdef TOOLS_ENABLED
		if (Thread::is_main_thread()) {
			EditorNode::progress_end_task(task);
		} else {
			EditorNode::progress_end_task_bg(task);
		}
#endif
	}
}