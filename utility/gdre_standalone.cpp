#include "gdre_standalone.h"
#include "gdre_version.gen.h"
#include "utility/gdre_audio_stream_preview.h"

GodotREEditorStandalone *GodotREEditorStandalone::singleton = nullptr;

void GodotREEditorStandalone::_write_log_message(String p_message) {
	emit_signal("write_log_message", p_message);
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
}

// TODO: move this to common
void GodotREEditorStandalone::tree_set_edit_checkbox_cell_only_when_checkbox_is_pressed(Tree *p_tree, bool enabled) {
	p_tree->set_edit_checkbox_cell_only_when_checkbox_is_pressed(enabled);
}

void GodotREEditorStandalone::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_write_log_message"), &GodotREEditorStandalone::_write_log_message);
	ClassDB::bind_method(D_METHOD("pck_select_request", "path"), &GodotREEditorStandalone::pck_select_request);
	ADD_SIGNAL(MethodInfo("write_log_message", PropertyInfo(Variant::STRING, "message")));
	ClassDB::bind_method(D_METHOD("get_version"), &GodotREEditorStandalone::get_version);
	ClassDB::bind_method(D_METHOD("show_about_dialog"), &GodotREEditorStandalone::show_about_dialog);
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
