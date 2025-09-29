#pragma once

#include "scene/gui/dialogs.h"
#include "scene/main/window.h"

class GDREWindow : public Window {
	GDCLASS(GDREWindow, Window);

	Vector<Callable> next_process_calls;
	ConfirmationDialog *confirmation_dialog;
	AcceptDialog *error_dialog;
	static const Callable void_func_callable;

protected:
	void _notification(int p_what);

	static void _bind_methods();

public:
	static void popup_box(Node *p_parent, Window *p_box, const String &p_message, const String &p_title, const Callable &p_confirm_callback = {}, const Callable &p_cancel_callback = {});
	void call_on_next_process(const Callable &p_callable);

	void popup_confirm_box(const String &p_message, const String &p_title, const Callable &p_confirm_callback = Callable(), const Callable &p_cancel_callback = Callable());
	void popup_error_box(const String &p_message, const String &p_title = "Error", const Callable &p_callback = Callable());

	GDREWindow();
	~GDREWindow();
};

class GDREAcceptDialogBase : public AcceptDialog {
	GDCLASS(GDREAcceptDialogBase, AcceptDialog);
	Vector<Callable> next_process_calls;
	ConfirmationDialog *confirmation_dialog;
	AcceptDialog *error_dialog;

protected:
	void _notification(int p_what);

	static void _bind_methods();

public:
	void call_on_next_process(const Callable &p_callable);

	void popup_confirm_box(const String &p_message, const String &p_title, const Callable &p_confirm_callback = Callable(), const Callable &p_cancel_callback = Callable());
	void popup_error_box(const String &p_message, const String &p_title = "Error", const Callable &p_callback = Callable());

	GDREAcceptDialogBase();
	~GDREAcceptDialogBase();
};
