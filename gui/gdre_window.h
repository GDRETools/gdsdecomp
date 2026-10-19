#pragma once

#include "scene/gui/dialogs.h"
#include "scene/gui/file_dialog.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

class GDREWindow : public Window {
	GDCLASS(GDREWindow, Window);

	ConfirmationDialog *confirmation_dialog;
	AcceptDialog *error_dialog;
	static const Callable void_func_callable;

protected:
	static void _bind_methods();

	void _notification(int p_what);

public:
	static Size2i get_safe_window_size(Size2i p_size);
	static Rect2i get_safe_window_size_and_position(Size2i p_size, Vector2i p_position);
	static void ensure_safe_size_and_position(Window *p_window);
	static void set_window_autoscaling(Window *p_window, Size2i p_min_size);
	static void popup_box(Node *p_parent, Window *p_box, const String &p_message, const String &p_title, const Callable &p_confirm_callback = {}, const Callable &p_cancel_callback = {}, const String &p_ok_button_text = "OK", const String &p_cancel_button_text = "Cancel");

	void popup_confirm_box(const String &p_message, const String &p_title, const Callable &p_confirm_callback = Callable(), const Callable &p_cancel_callback = Callable(), const String &p_ok_button_text = "OK", const String &p_cancel_button_text = "Cancel");
	void popup_error_box(const String &p_message, const String &p_title = "Error", const Callable &p_callback = Callable());

	GDREWindow();
	~GDREWindow();
};

class GDREAcceptDialogBase : public AcceptDialog {
	GDCLASS(GDREAcceptDialogBase, AcceptDialog);
	ConfirmationDialog *confirmation_dialog;
	AcceptDialog *error_dialog;

protected:
	static void _bind_methods();

	void _notification(int p_what);

public:
	void popup_confirm_box(const String &p_message, const String &p_title, const Callable &p_confirm_callback = Callable(), const Callable &p_cancel_callback = Callable(), const String &p_ok_button_text = "OK", const String &p_cancel_button_text = "Cancel");
	void popup_error_box(const String &p_message, const String &p_title = "Error", const Callable &p_callback = Callable());

	GDREAcceptDialogBase();
	~GDREAcceptDialogBase();
};

class GDREConfirmationDialogBase : public ConfirmationDialog {
	GDCLASS(GDREConfirmationDialogBase, ConfirmationDialog);

	ConfirmationDialog *confirmation_dialog;
	AcceptDialog *error_dialog;

protected:
	static void _bind_methods();

	void _notification(int p_what);

public:
	void popup_confirm_box(const String &p_message, const String &p_title, const Callable &p_confirm_callback = Callable(), const Callable &p_cancel_callback = Callable(), const String &p_ok_button_text = "OK", const String &p_cancel_button_text = "Cancel");
	void popup_error_box(const String &p_message, const String &p_title = "Error", const Callable &p_callback = Callable());

	GDREConfirmationDialogBase();
	~GDREConfirmationDialogBase();
};

class GDREFileDialog : public FileDialog {
	GDCLASS(GDREFileDialog, FileDialog);

protected:
	void _notification(int p_what);
};
