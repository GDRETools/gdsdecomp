#pragma once
#include "core/object/object.h"
#include "scene/gui/box_container.h"
#include "scene/gui/control.h"
#include "scene/gui/tree.h"
#include "utility/gdre_progress.h"

class GDREAudioStreamPreviewGeneratorNode;
class AcceptDialog;
class ConfirmationDialog;
class GodotREEditorStandalone : public Control {
	GDCLASS(GodotREEditorStandalone, Control)

	static GodotREEditorStandalone *singleton;
	HBoxContainer *menu_hb = nullptr;
	GDREProgressDialog *progress_dialog = nullptr;
	GDREAudioStreamPreviewGeneratorNode *audio_stream_preview_generator_node = nullptr;
	AcceptDialog *error_dialog = nullptr;
	ConfirmationDialog *confirmation_dialog = nullptr;
	uint64_t last_log_message_time = 0;
	Vector<String> log_message_buffer;

protected:
	void _notification(int p_notification);
	static void _bind_methods();

public:
	void show_about_dialog();
	static void progress_add_task(const String &p_task, const String &p_label, int p_steps, bool p_can_cancel);
	static bool progress_task_step(const String &p_task, const String &p_state, int p_step, bool p_force_refresh);
	static void progress_end_task(const String &p_task);
	static void tree_set_edit_checkbox_cell_only_when_checkbox_is_pressed(Tree *p_tree, bool enabled);
	void popup_error_box(const String &p_message, const String &p_title = "Error", const Callable &p_callback = Callable());
	void popup_confirm_box(const String &p_message, const String &p_title, const Callable &p_confirm_callback = Callable(), const Callable &p_cancel_callback = Callable());

	void pck_select_request(const Vector<String> &p_path);
	void write_log_message(const String &p_message);
	String get_version();
	static GodotREEditorStandalone *get_singleton() { return singleton; }

	GodotREEditorStandalone();
	~GodotREEditorStandalone();
};
