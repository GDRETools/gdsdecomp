#include "gdre_standalone.h"
#include "core/object/callable_method_pointer.h"
#include "gdre_version.gen.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/rich_text_label.h"
#include "scene/main/node.h"
#include "utility/gdre_audio_stream_preview.h"
#include "utility/gdre_logger.h"
#include "utility/gdre_window.h"
#include "utility/task_manager.h"

GodotREEditorStandalone *GodotREEditorStandalone::singleton = nullptr;

void GodotREEditorStandalone::write_log_message(const String &p_message) {
	// find a child with the name "log_window"
	auto current_time = OS::get_singleton()->get_ticks_msec();
	// if it's been less than 200ms since the last log message, add it to the buffer
	if (current_time - last_log_message_time < 200) {
		log_message_buffer.append(p_message);
		return;
	}
	last_log_message_time = current_time;

	Node *n = get_node(NodePath("log_window"));
	if (!n) {
		// Can't use error macros because they will end up calling this function again
		static bool warning_shown1 = false;
		if (!warning_shown1) {
			GDRELogger::stdout_print("Failed to find log_window");
			warning_shown1 = true;
		}
		return;
	}
	RichTextLabel *log_window = Object::cast_to<RichTextLabel>(n);
	if (!log_window) {
		static bool warning_shown2 = false;
		if (!warning_shown2) {
			GDRELogger::stdout_print("Failed to cast log_window to RichTextLabel");
			warning_shown2 = true;
		}
		return;
	}
	String text = log_window->get_text() + String("").join(log_message_buffer) + p_message;
	log_message_buffer.clear();
	int line_count = text.count("\n");
	if (line_count > 1000) {
		int new_lines_to_remove = line_count - 1000;
		auto found = -1;
		while (new_lines_to_remove > 0) {
			auto new_found = text.find("\n", found + 1);
			if (new_found == String::npos) {
				break;
			}
			found = new_found;
			new_lines_to_remove--;
		}
		text = text.substr(found + 1);
	}
	last_log_message_time = OS::get_singleton()->get_ticks_msec();
	log_window->set_text(text);
	// this forces it to scroll to vscroll max
	log_window->scroll_to_paragraph(INT_MAX);
}

String GodotREEditorStandalone::get_version() {
	return String(GDRE_VERSION);
}

void GodotREEditorStandalone::pck_select_request(const Vector<String> &p_path) {
}

void GodotREEditorStandalone::show_about_dialog() {
}

void GodotREEditorStandalone::progress_add_task(const String &p_task, const String &p_label, int p_steps, bool p_can_cancel) {
	if (!singleton) {
		return;
		// } else if (singleton->cmdline_export_mode) {
		// 	print_line(p_task + ": begin: " + p_label + " steps: " + itos(p_steps));
	} else if (singleton->progress_dialog) {
		singleton->progress_dialog->add_task(p_task, p_label, p_steps, p_can_cancel);
	}
}

bool GodotREEditorStandalone::progress_task_step(const String &p_task, const String &p_state, int p_step, bool p_force_refresh) {
	if (!singleton) {
		return false;
		// } else if (singleton->cmdline_export_mode) {
		// 	print_line("\t" + p_task + ": step " + itos(p_step) + ": " + p_state);
		// 	return false;
	} else if (singleton->progress_dialog) {
		return singleton->progress_dialog->task_step(p_task, p_state, p_step, p_force_refresh);
	} else {
		return false;
	}
}

void GodotREEditorStandalone::progress_end_task(const String &p_task) {
	if (!singleton) {
		return;
		// } else if (singleton->cmdline_export_mode) {
		// 	print_line(p_task + ": end");
	} else if (singleton->progress_dialog) {
		singleton->progress_dialog->end_task(p_task);
	}
}

void GodotREEditorStandalone::_notification(int p_notification) {
	if (p_notification == NOTIFICATION_ENTER_TREE) {
		auto parent_window = get_parent_window();
		if (parent_window) {
			// progress_dialog->reparent(parent_window);
			parent_window->set_theme(get_theme());
			callable_mp((Node *)progress_dialog, &Node::reparent).call_deferred(parent_window, false);
		}
	}
	if (p_notification == NOTIFICATION_READY) {
		if (get_parent_window()) {
			get_parent_window()->call_deferred("add_child", error_dialog);
			get_parent_window()->call_deferred("add_child", confirmation_dialog);
		}
	}
	if (p_notification == NOTIFICATION_PROCESS) {
		if (log_message_buffer.size() > 0 && OS::get_singleton()->get_ticks_msec() - last_log_message_time > 200) {
			write_log_message("");
		}
		TaskManager::get_singleton()->update_progress_bg(false, true);
	}
}

// TODO: move this to common
void GodotREEditorStandalone::tree_set_edit_checkbox_cell_only_when_checkbox_is_pressed(Tree *p_tree, bool enabled) {
	p_tree->set_edit_checkbox_cell_only_when_checkbox_is_pressed(enabled);
}

void GodotREEditorStandalone::popup_error_box(const String &p_message, const String &p_title, const Callable &p_callback) {
	GDREWindow::popup_box(get_parent_window(), error_dialog, p_message, p_title, p_callback, p_callback);
}

void GodotREEditorStandalone::popup_confirm_box(const String &p_message, const String &p_title, const Callable &p_confirm_callback, const Callable &p_cancel_callback) {
	GDREWindow::popup_box(get_parent_window(), confirmation_dialog, p_message, p_title, p_confirm_callback, p_cancel_callback);
}
void GodotREEditorStandalone::_bind_methods() {
	ClassDB::bind_method(D_METHOD("write_log_message", "message"), &GodotREEditorStandalone::write_log_message);
	ClassDB::bind_method(D_METHOD("pck_select_request", "path"), &GodotREEditorStandalone::pck_select_request);
	ClassDB::bind_method(D_METHOD("get_version"), &GodotREEditorStandalone::get_version);
	ClassDB::bind_method(D_METHOD("show_about_dialog"), &GodotREEditorStandalone::show_about_dialog);
	ClassDB::bind_method(D_METHOD("popup_error_box", "message", "title", "callback"), &GodotREEditorStandalone::popup_error_box, DEFVAL("Error"), DEFVAL(Callable()));
	ClassDB::bind_method(D_METHOD("popup_confirm_box", "message", "title", "confirm_callback", "cancel_callback"), &GodotREEditorStandalone::popup_confirm_box, DEFVAL(Callable()), DEFVAL(Callable()));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("progress_add_task", "task", "label", "steps", "can_cancel"), &GodotREEditorStandalone::progress_add_task);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("progress_task_step", "task", "state", "step", "force_refresh"), &GodotREEditorStandalone::progress_task_step);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("progress_end_task", "task"), &GodotREEditorStandalone::progress_end_task);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("tree_set_edit_checkbox_cell_only_when_checkbox_is_pressed", "tree", "enabled"), &GodotREEditorStandalone::tree_set_edit_checkbox_cell_only_when_checkbox_is_pressed);
}

GodotREEditorStandalone::GodotREEditorStandalone() {
	singleton = this;
	progress_dialog = memnew(GDREProgressDialog);
	audio_stream_preview_generator_node = memnew(GDREAudioStreamPreviewGeneratorNode);
	add_child(progress_dialog);
	add_child(audio_stream_preview_generator_node);
	confirmation_dialog = memnew(ConfirmationDialog);
	error_dialog = memnew(AcceptDialog);
	set_process(true);
}

GodotREEditorStandalone::~GodotREEditorStandalone() {
	if (progress_dialog) {
		progress_dialog->queue_free();
	}
	if (audio_stream_preview_generator_node) {
		audio_stream_preview_generator_node->queue_free();
	}
	singleton = nullptr;
}
