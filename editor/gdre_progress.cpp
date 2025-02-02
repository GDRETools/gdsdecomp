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
#include "gdre_editor.h"
#include "main/main.h"
#include "servers/display_server.h"

#include <utility/gdre_settings.h>
#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
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

void GDREProgressDialog::add_task(const String &p_task, const String &p_label, int p_steps, bool p_can_cancel) {
	if (MessageQueue::get_singleton()->is_flushing()) {
		ERR_PRINT("Do not use progress dialog (task) while flushing the message queue or using call_deferred()!");
		return;
	}

	ERR_FAIL_COND_MSG(tasks.has(p_task), "Task '" + p_task + "' already exists.");
	GDREProgressDialog::Task t;
	t.vb = memnew(VBoxContainer);
	VBoxContainer *vb2 = memnew(VBoxContainer);
	t.vb->add_margin_child(p_label, vb2);
	t.progress = memnew(ProgressBar);
	t.progress->set_max(p_steps);
	t.progress->set_value(p_steps);
	vb2->add_child(t.progress);
	t.state = memnew(Label);
	t.state->set_clip_text(true);
	vb2->add_child(t.state);
	main->add_child(t.vb);

	tasks[p_task] = t;
	if (p_can_cancel) {
		cancel_hb->show();
	} else {
		cancel_hb->hide();
	}
	cancel_hb->move_to_front();
	canceled = false;
	_popup();
	if (p_can_cancel) {
		cancel->grab_focus();
	}
	_update_ui();
}

bool GDREProgressDialog::task_step(const String &p_task, const String &p_state, int p_step, bool p_force_redraw) {
	ERR_FAIL_COND_V(!tasks.has(p_task), canceled);

	Task &t = tasks[p_task];
	if (!p_force_redraw) {
		uint64_t tus = OS::get_singleton()->get_ticks_usec();
		if (tus - t.last_progress_tick < 200000) { //200ms
			return canceled;
		}
	}
	if (p_step < 0) {
		t.progress->set_value(t.progress->get_value() + 1);
	} else {
		t.progress->set_value(p_step);
	}

	t.state->set_text(p_state);
	t.last_progress_tick = OS::get_singleton()->get_ticks_usec();
	_update_ui();

	return canceled;
}

void GDREProgressDialog::end_task(const String &p_task) {
	ERR_FAIL_COND(!tasks.has(p_task));
	Task &t = tasks[p_task];

	memdelete(t.vb);
	tasks.erase(p_task);

	if (tasks.is_empty()) {
		hide();
	} else {
		_popup();
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
	cancel->set_text(TTR("Cancel"));
	cancel_hb->add_spacer();
	cancel->connect(SceneStringName(pressed), callable_mp(this, &GDREProgressDialog::_cancel_pressed));
}

bool EditorProgressGDDC::step(const String &p_state, int p_step, bool p_force_refresh) {
	if (progress_dialog) {
		return progress_dialog->task_step(task, p_state, p_step, p_force_refresh);
	}
	return false;
}

EditorProgressGDDC::EditorProgressGDDC(Node *p_parent, const String &p_task, const String &p_label, int p_amount, bool p_can_cancel) {
	progress_dialog = GDREProgressDialog::get_singleton();
	if (progress_dialog) {
		if (p_parent) {
			progress_dialog->add_host_window(p_parent->get_window());
		}
		progress_dialog->add_task(p_task, p_label, p_amount, p_can_cancel);
	}
	task = p_task;
}

EditorProgressGDDC::~EditorProgressGDDC() {
	// if no EditorNode...
	if (progress_dialog) {
		progress_dialog->end_task(task);
	}
}