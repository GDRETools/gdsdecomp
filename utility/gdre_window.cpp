#include "gdre_window.h"
#include "utility/gdre_progress.h"

namespace internal {

static void callback_func(Window *p_this_box, const String &p_signal_name, const Callable &p_this_callback) {
	p_this_box->disconnect(p_signal_name, p_this_callback);
	p_this_callback.call();
}
} //namespace internal

void GDREWindow::popup_box(Node *p_parent, Window *p_box, const String &p_message, const String &p_title, const Callable &p_confirm_callback, const Callable &p_cancel_callback) {
	if (p_parent && p_box->get_parent() != p_parent) {
		if (p_box->get_parent()) {
			p_box->reparent(p_parent);
		} else {
			p_parent->add_child(p_box);
		}
	}
	ConfirmationDialog *confirmation_dialog = Object::cast_to<ConfirmationDialog>(p_box);
	if (confirmation_dialog) {
		confirmation_dialog->set_text(p_message);
		confirmation_dialog->reset_size();
	} else {
		GDREAcceptDialogBase *gdre_accept_dialog = Object::cast_to<GDREAcceptDialogBase>(p_box);
		if (gdre_accept_dialog) {
			gdre_accept_dialog->set_text(p_message);
			gdre_accept_dialog->reset_size();
		} else {
			AcceptDialog *accept_dialog = Object::cast_to<AcceptDialog>(p_box);
			if (accept_dialog) {
				accept_dialog->set_text(p_message);
				accept_dialog->reset_size();
			} else {
				ERR_FAIL_MSG("Unknown dialog type");
			}
		}
	}
	p_box->set_title(p_title);
	if (!p_confirm_callback.is_null()) {
		auto act_confirmed_callback = callable_mp_static(&internal::callback_func).bind(p_box, "confirmed", p_confirm_callback);
		p_box->connect("confirmed", act_confirmed_callback);
	}
	if (!p_cancel_callback.is_null()) {
		auto act_cancelled_callback = callable_mp_static(&internal::callback_func).bind(p_box, "cancelled", p_cancel_callback);
		p_box->connect("cancelled", act_cancelled_callback);
	}
	// p_parent->add_child(p_box);
	p_box->popup_centered();
}

GDREWindow::GDREWindow() {
	if (GDREProgressDialog::get_singleton()) {
		GDREProgressDialog::get_singleton()->add_host_window(this);
	}
	confirmation_dialog = memnew(ConfirmationDialog);
	error_dialog = memnew(AcceptDialog);
	add_child(confirmation_dialog);
	add_child(error_dialog);
}

GDREWindow::~GDREWindow() {
	if (GDREProgressDialog::get_singleton()) {
		GDREProgressDialog::get_singleton()->remove_host_window(this);
	}
}

void GDREWindow::_notification(int p_what) {
	if (!next_process_calls.is_empty()) {
		auto calls = next_process_calls;
		next_process_calls.clear();
		for (const auto &callable : calls) {
			callable.call();
		}
	}
}

void GDREWindow::call_on_next_process(const Callable &p_callable) {
	next_process_calls.push_back(p_callable);
	if (!is_processing()) {
		set_process(true);
	}
}

void GDREWindow::popup_confirm_box(const String &p_message, const String &p_title, const Callable &p_confirm_callback, const Callable &p_cancel_callback) {
	popup_box(this, confirmation_dialog, p_message, p_title, p_confirm_callback, p_cancel_callback);
}

void GDREWindow::popup_error_box(const String &p_message, const String &p_title, const Callable &p_callback) {
	popup_box(this, error_dialog, p_message, p_title, p_callback);
}

GDREAcceptDialogBase::GDREAcceptDialogBase() {
	if (GDREProgressDialog::get_singleton()) {
		GDREProgressDialog::get_singleton()->add_host_window(this);
	}
	confirmation_dialog = memnew(ConfirmationDialog);
	error_dialog = memnew(AcceptDialog);
	add_child(confirmation_dialog);
	add_child(error_dialog);
}

GDREAcceptDialogBase::~GDREAcceptDialogBase() {
	if (GDREProgressDialog::get_singleton()) {
		GDREProgressDialog::get_singleton()->remove_host_window(this);
	}
}

void GDREAcceptDialogBase::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS) {
		if (!next_process_calls.is_empty()) {
			auto calls = next_process_calls;
			next_process_calls.clear();
			for (const auto &callable : calls) {
				callable.call();
			}
		}
	}
}

void GDREAcceptDialogBase::call_on_next_process(const Callable &p_callable) {
	next_process_calls.push_back(p_callable);
	if (!is_processing()) {
		set_process(true);
	}
}

void GDREAcceptDialogBase::popup_confirm_box(const String &p_message, const String &p_title, const Callable &p_confirm_callback, const Callable &p_cancel_callback) {
	GDREWindow::popup_box(this, confirmation_dialog, p_message, p_title, p_confirm_callback, p_cancel_callback);
}

void GDREAcceptDialogBase::popup_error_box(const String &p_message, const String &p_title, const Callable &p_callback) {
	GDREWindow::popup_box(this, error_dialog, p_message, p_title, p_callback, p_callback);
}

void GDREWindow::_bind_methods() {
	ClassDB::bind_method(D_METHOD("call_on_next_process", "p_callable"), &GDREWindow::call_on_next_process);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("popup_box", "p_parent", "p_box", "p_message", "p_title", "p_confirm_callback", "p_cancel_callback"), &GDREWindow::popup_box, DEFVAL(Callable()), DEFVAL(Callable()));
	ClassDB::bind_method(D_METHOD("popup_confirm_box", "p_message", "p_title", "p_confirm_callback", "p_cancel_callback"), &GDREWindow::popup_confirm_box, DEFVAL(Callable()), DEFVAL(Callable()));
	ClassDB::bind_method(D_METHOD("popup_error_box", "p_message", "p_title", "p_callback"), &GDREWindow::popup_error_box, DEFVAL("Error"), DEFVAL(Callable()));
}

void GDREAcceptDialogBase::_bind_methods() {
	ClassDB::bind_method(D_METHOD("call_on_next_process", "p_callable"), &GDREAcceptDialogBase::call_on_next_process);
	ClassDB::bind_method(D_METHOD("popup_confirm_box", "p_message", "p_title", "p_confirm_callback", "p_cancel_callback"), &GDREAcceptDialogBase::popup_confirm_box, DEFVAL(Callable()), DEFVAL(Callable()));
	ClassDB::bind_method(D_METHOD("popup_error_box", "p_message", "p_title", "p_callback"), &GDREAcceptDialogBase::popup_error_box, DEFVAL("Error"), DEFVAL(Callable()));
}
