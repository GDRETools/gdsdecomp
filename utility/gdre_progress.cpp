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
#include "scene/gui/file_dialog.h"
#include "servers/display_server.h"
#include "utility/gdre_logger.h"
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

void GDREProgressDialog::Task::set_indeterminate(bool p_indeterminate) {
	indeterminate = p_indeterminate;
	force_next_redraw = true;
}

void GDREProgressDialog::Task::set_length(int p_new_amount) {
	steps = p_new_amount;
	force_next_redraw = true;
}

bool GDREProgressDialog::Task::update() {
	ERR_FAIL_COND_V(!initialized, false);
	if (!vb) {
		return false;
	}
	auto curr_time_us = OS::get_singleton()->get_ticks_usec();
	if (!should_redraw(curr_time_us)) {
		if (!(curr_time_us - last_progress_tick >= 50000 &&
					(progress->get_value() != current_step.step ||
							state->get_text() != current_step.state ||
							indeterminate != progress->is_indeterminate() ||
							steps != progress->get_max()))) {
			return false;
		}
	}
	force_next_redraw = false;

	if (indeterminate != progress->is_indeterminate()) {
		progress->set_indeterminate(indeterminate);
	}
	if (steps != progress->get_max()) {
		progress->set_max(steps);
	}
	if (!indeterminate) {
		if (progress->get_value() != current_step.step) {
			progress->set_value(current_step.step);
		}
	}
	if (state->get_text() != current_step.state) {
		state->set_text(current_step.state);
	}
	last_progress_tick = OS::get_singleton()->get_ticks_usec();
	return true;
}

void GDREProgressDialog::_popup() {
	// 	if (GodotREEditorStandalone::get_singleton()) {
	// 		GodotREEditorStandalone::get_singleton()->set_process_input(true);
	// 	}
	// #ifdef TOOLS_ENABLED
	// 	else if (EditorNode::get_singleton()) {
	// 		EditorNode::get_singleton()->set_process_input(true);
	// 	}
	// #endif

	// 	// Disable all other windows to prevent interaction with them.
	// 	for (Window *w : host_windows) {
	// 		w->set_process_mode(PROCESS_MODE_DISABLED);
	// 	}

	if (is_ready()) {
		_reparent_and_show();
	} else {
		callable_mp(this, &GDREProgressDialog::_reparent_and_show).call_deferred();
	}
}

void GDREProgressDialog::_parent_visible_changed(Window *p_window) {
	if (!Thread::is_main_thread()) {
		callable_mp(this, &GDREProgressDialog::_parent_visible_changed).call_deferred(p_window);
	} else {
		if (p_window) {
			p_window->disconnect(SceneStringName(visibility_changed), callable_mp(this, &GDREProgressDialog::_parent_visible_changed).bind(p_window));
		}
		if (is_visible()) {
			_popup();
		}
	}
}

void GDREProgressDialog::_reparent_and_show() {
	Window *current_window = SceneTree::get_singleton()->get_root()->get_last_exclusive_window();
	ERR_FAIL_NULL(current_window);

	FileDialog *file_dialog = Object::cast_to<FileDialog>(current_window);
	if (file_dialog && file_dialog->get_use_native_dialog()) {
		WARN_PRINT("File dialog is using native dialog, so we can't show progress dialog, attempting to show it again...");
		callable_mp(this, &GDREProgressDialog::_reparent_and_show).call_deferred();
	}

	Size2 ms = main->get_combined_minimum_size();
	ms.width = MAX(500 * GDRESettings::get_singleton()->get_auto_display_scale(), ms.width);

	Ref<StyleBox> style = main->get_theme_stylebox(SceneStringName(panel), SNAME("PopupMenu"));
	ms += style->get_minimum_size();

	main->set_offset(SIDE_LEFT, style->get_margin(SIDE_LEFT));
	main->set_offset(SIDE_RIGHT, -style->get_margin(SIDE_RIGHT));
	main->set_offset(SIDE_TOP, style->get_margin(SIDE_TOP));
	main->set_offset(SIDE_BOTTOM, -style->get_margin(SIDE_BOTTOM));

	if (is_inside_tree()) {
		if (this != current_window && get_parent() != current_window) {
			reparent(current_window);
		}
	}

	if (!is_inside_tree()) {
		popup_exclusive_centered(current_window, ms);
	} else {
		Rect2i adjust = _popup_adjust_rect();
		if (adjust != Rect2i()) {
			set_position(adjust.position);
			set_size(adjust.size);
		}
		popup_centered(ms);
	}
	auto callable = callable_mp(this, &GDREProgressDialog::_parent_visible_changed).bind(current_window);
	if (!current_window->is_connected(SceneStringName(visibility_changed), callable)) {
		current_window->connect(SceneStringName(visibility_changed), callable);
	}

	// if (!is_inside_tree()) {
	// 	callable_mp(this, &GDREProgressDialog::_reparent_and_show).call_deferred();
	// 	return;
	// }
	// Ensures that events are properly released before the dialog blocks input.
	// bool window_is_input_disabled = current_window->is_input_disabled();
	// current_window->set_disable_input(!window_is_input_disabled);
	// current_window->set_disable_input(window_is_input_disabled);

	show();
}

void GDREProgressDialog::_hide() {
	hide();
	// 	if (GodotREEditorStandalone::get_singleton()) {
	// 		GodotREEditorStandalone::get_singleton()->set_process_input(false);
	// 	}
	// #ifdef TOOLS_ENABLED
	// 	else if (EditorNode::get_singleton()) {
	// 		EditorNode::get_singleton()->set_process_input(false);
	// 	}
	// #endif
	// 	for (Window *w : host_windows) {
	// 		w->set_process_mode(PROCESS_MODE_INHERIT);
	// 	}
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
	bool do_update = p_force_redraw;
	tasks.modify_if(p_task, [&](TaskMap::value_type &t) {
		t.second.set_step(p_state, p_step, p_force_redraw);
		if (is_main_thread) {
			do_update = do_update || t.second.should_redraw(OS::get_singleton()->get_ticks_usec());
		}
	});
	if (do_update && is_main_thread) {
		main_thread_update();
	}

	return canceled;
}

void GDREProgressDialog::task_set_length(const String &p_task, bool p_indeterminate, int p_new_amount) {
	ERR_FAIL_COND(!tasks.contains(p_task));
	tasks.modify_if(p_task, [&](TaskMap::value_type &t) {
		t.second.set_indeterminate(p_indeterminate);
		if (p_new_amount != -1) {
			t.second.set_length(p_new_amount);
		}
	});
	if (is_safe_to_redraw()) {
		main_thread_update();
	}
}

bool GDREProgressDialog::_process_removals() {
	String p_task;
	bool has_deletions = false;
	while (queued_removals.try_pop(p_task)) {
		bool has = tasks.modify_if(p_task, [](TaskMap::value_type &t) {
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
	ERR_FAIL_COND_MSG(!is_safe_to_redraw(), "Cannot update progress dialog from non-main thread or while flushing messages.");
	// This prevents recursive calls to main_thread_update caused by main iteration calling `process()`
	if (is_updating) {
		return;
	}
	is_updating = true;
	bool removed = _process_removals();
	bool should_update = removed;
	bool p_can_cancel = false;
	bool task_initialized = false;
	bool should_force_redraw = false;
	uint64_t task_count = 0;
	uint64_t last_tick = OS::get_singleton()->get_ticks_usec();
	// if it's been more than 500ms since the last update_ui happened, force a redraw (if we have any tasks to redraw).
	if (last_tick - last_tick_updated > 500000) {
		should_force_redraw = true;
	}
	tasks.for_each_m([&](TaskMap::value_type &E) {
		Task &t = E.second;
		if (!t.initialized) {
			task_initialized = true;
			t.init(main);
		}
		t.force_next_redraw = t.force_next_redraw || should_force_redraw;
		if (t.update()) {
			should_update = true;
		}
		p_can_cancel = p_can_cancel || t.can_cancel;
		task_count++;
	});
	if (should_update || task_initialized) {
		last_tick_updated = last_tick;
		if (task_count == 0) {
			_hide();
		} else if (task_initialized || removed) {
			_post_add_task(p_can_cancel);
		} else {
			_update_ui();
		}
	}
	is_updating = false;
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
	// set_process(true);
}

GDREProgressDialog::~GDREProgressDialog() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

String EditorProgressGDDC::get_task() {
	return task;
}

Ref<EditorProgressGDDC> EditorProgressGDDC::create(Node *p_parent, const String &p_task, const String &p_label, int p_amount, bool p_can_cancel) {
	return memnew(EditorProgressGDDC(nullptr, p_task, p_label, p_amount, p_can_cancel));
}

bool StdOutProgress::step(int p_step, bool p_force_refresh) {
	const int prev_step = current_step;
	if (p_step == -1) {
		current_step++;
	} else {
		current_step = p_step;
	}
	auto current_tick = OS::get_singleton()->get_ticks_usec();
	float progress = MAX(0, (float)current_step / (float)amount);
	size_t progress_percent = MIN(static_cast<size_t>(progress * 100), static_cast<size_t>(100));
	size_t prev_progress_percent = MIN(static_cast<size_t>((static_cast<float>(prev_step) / static_cast<float>(amount)) * 100), static_cast<size_t>(100));
	bool should_redraw = progress_percent != prev_progress_percent;
	float indeterminate_progress = -1;
	if (is_indeterminate) {
		uint16_t prev_width = indeterminate_width;
		indeterminate_width = (indeterminate_width + ((current_tick - last_progress_tick) / 100000)) % 30;
		indeterminate_progress = (float)indeterminate_width / (float)width;
		should_redraw = should_redraw || prev_width != indeterminate_width;
	}
	// We only print a new status bar if the progress has changed or if it's been more than 500ms since the last update;
	// We don't use p_force_refresh here because print_status_bar does not iterate the main loop,
	// and it's not really needed if there's nothing to update (and printing involves locking, so it can cause contention).
	constexpr uint64_t PROGRESS_TICK_THRESHOLD = 500000;
	constexpr uint64_t ITERATION_TICK_THRESHOLD = 10000;
	if (should_redraw || (current_tick - last_progress_tick) > PROGRESS_TICK_THRESHOLD) {
		GDRELogger::print_status_bar(label, progress, indeterminate_progress);
		last_progress_tick = current_tick;
	}
	if ((p_force_refresh || current_tick - last_iteration_tick > ITERATION_TICK_THRESHOLD) && GDREProgressDialog::is_safe_to_redraw()) {
		// force the main loop to iterate; this is needed to allow for input events to be processed and the command queue to be flushed.
		Main::iteration();
		last_iteration_tick = current_tick;
	}
	return false;
}

void StdOutProgress::end() {
	GDRELogger::print_status_bar(label, 1.0f);
	GDRELogger::stdout_print("\n");
}

void EditorProgressGDDC::_bind_methods() {
	ClassDB::bind_method(D_METHOD("step", "state", "step", "force_refresh"), &EditorProgressGDDC::step, DEFVAL(-1), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_task"), &EditorProgressGDDC::get_task);
	ClassDB::bind_method(D_METHOD("set_progress_length", "indeterminate", "new_amount"), &EditorProgressGDDC::set_progress_length, DEFVAL(-1));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("create", "parent", "task", "label", "amount", "can_cancel"), &EditorProgressGDDC::create, DEFVAL(false));
}

bool EditorProgressGDDC::step(const String &p_state, int p_step, bool p_force_refresh) {
	if (GDRESettings::get_singleton() && GDRESettings::get_singleton()->is_headless()) {
		return stdout_progress.step(p_step, p_force_refresh);
	} else {
		if (p_step == -1) {
			stdout_progress.current_step++;
		} else {
			stdout_progress.current_step = p_step;
		}
	}
	if (GDREProgressDialog::get_singleton()) {
		return GDREProgressDialog::get_singleton()->task_step(task, p_state, p_step, p_force_refresh);
	} else {
#ifdef TOOLS_ENABLED
		if (EditorNode::get_singleton()) {
			if (Thread::is_main_thread()) {
				return EditorNode::progress_task_step(task, p_state, p_step, p_force_refresh);
			} else {
				EditorNode::progress_task_step_bg(task, p_step);
				return false;
			}
		}
#endif
	}
	return false;
}

void EditorProgressGDDC::set_progress_length(bool p_indeterminate, int p_new_amount) {
	stdout_progress.is_indeterminate = p_indeterminate;
	if (p_new_amount != -1) {
		stdout_progress.amount = p_new_amount;
	}
	if (GDRESettings::get_singleton() && GDRESettings::get_singleton()->is_headless()) {
		stdout_progress.step(stdout_progress.current_step, true);
	} else if (GDREProgressDialog::get_singleton()) {
		GDREProgressDialog::get_singleton()->task_set_length(task, p_indeterminate, p_new_amount);
	}
}

EditorProgressGDDC::EditorProgressGDDC() {}
EditorProgressGDDC::EditorProgressGDDC(const String &p_task, const String &p_label, int p_amount, bool p_can_cancel) :
		EditorProgressGDDC(nullptr, p_task, p_label, p_amount, p_can_cancel) {}
EditorProgressGDDC::EditorProgressGDDC(Node *p_parent, const String &p_task, const String &p_label, int p_amount, bool p_can_cancel) {
	task = p_task;
	stdout_progress = { p_label, p_amount, 0, p_amount == -1 };
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
		if (EditorNode::get_singleton()) {
			if (Thread::is_main_thread()) {
				EditorNode::progress_add_task(p_task, p_label, p_amount, p_can_cancel);
			} else {
				EditorNode::progress_add_task_bg(p_task, p_label, p_amount);
			}
		}
#endif
	}
}

EditorProgressGDDC::~EditorProgressGDDC() {
	// if no EditorNode...
	if (GDRESettings::get_singleton() && GDRESettings::get_singleton()->is_headless()) {
		stdout_progress.end();
	} else if (GDREProgressDialog::get_singleton()) {
		GDREProgressDialog::get_singleton()->end_task(task);
	} else {
#ifdef TOOLS_ENABLED
		if (EditorNode::get_singleton()) {
			if (Thread::is_main_thread()) {
				EditorNode::progress_end_task(task);
			} else {
				EditorNode::progress_end_task_bg(task);
			}
		}
#endif
	}
}
