/*************************************************************************/
/*  gdre_enc_key.cpp                                                     */
/*************************************************************************/
/*************************************************************************/
#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
/*************************************************************************/

#include "gdre_enc_key.h"
#include "utility/gdre_settings.h"

EncKeyDialog::EncKeyDialog() {
	set_title(RTR("Set encryption key"));
	set_flag(Window::Flags::FLAG_RESIZE_DISABLED, false);

	VBoxContainer *script_vb = memnew(VBoxContainer);

	//Encryption key
	script_key = memnew(LineEdit);
	script_key->connect("text_changed", callable_mp(this, &EncKeyDialog::_script_encryption_key_changed));
	script_vb->add_margin_child(RTR("Script encryption key (64 character hex string):"), script_key);

	script_key_error = memnew(Label);
	script_vb->add_child(script_key_error);

	add_child(script_vb);

	get_ok_button()->set_text(RTR("Set"));
	_validate_input();

	add_cancel_button(RTR("Cancel"));
}

EncKeyDialog::~EncKeyDialog() {
	//NOP
}

Vector<uint8_t> EncKeyDialog::get_key() const {
	Vector<uint8_t> key;

	if (script_key->get_text().is_empty() || !script_key->get_text().is_valid_hex_number(false) || script_key->get_text().length() != 64) {
		return key;
	}

	key.resize(32);
	for (int i = 0; i < 32; i++) {
		int v = 0;
		if (i * 2 < script_key->get_text().length()) {
			char32_t ct = script_key->get_text().to_lower()[i * 2];
			if (ct >= '0' && ct <= '9')
				ct = ct - '0';
			else if (ct >= 'a' && ct <= 'f')
				ct = 10 + ct - 'a';
			v |= ct << 4;
		}

		if (i * 2 + 1 < script_key->get_text().length()) {
			char32_t ct = script_key->get_text().to_lower()[i * 2 + 1];
			if (ct >= '0' && ct <= '9')
				ct = ct - '0';
			else if (ct >= 'a' && ct <= 'f')
				ct = 10 + ct - 'a';
			v |= ct;
		}
		key.write[i] = v;
	}
	return key;
}

void EncKeyDialog::_validate_input() {
	bool ok = true;
	String error_message;

#ifdef TOOLS_ENABLED
	Color error_color = (EditorNode::get_singleton()) ? EditorNode::get_singleton()->get_gui_base()->get_theme_color("error_color", "Editor") : Color(1, 0, 0);
	Color warn_color = (EditorNode::get_singleton()) ? EditorNode::get_singleton()->get_gui_base()->get_theme_color("warning_color", "Editor") : Color(1, 1, 0);
#else
	Color error_color = Color(1, 0, 0);
	Color warn_color = Color(1, 1, 0);
#endif

	if (script_key->get_text().is_empty()) {
		error_message += RTR("No encryption key") + "\n";
		script_key_error->add_theme_color_override("font_color", warn_color);
	} else if (!script_key->get_text().is_valid_hex_number(false) || script_key->get_text().length() != 64) {
		error_message += RTR("Invalid encryption key (must be 64 characters long hex)") + "\n";
		script_key_error->add_theme_color_override("font_color", error_color);
		ok = false;
	}

	script_key_error->set_text(error_message);

	get_ok_button()->set_disabled(!ok);
}

void EncKeyDialog::_script_encryption_key_changed(const String &p_key) {
	_validate_input();
}

void EncKeyDialog::_notification(int p_notification) {
	//NOP
}

void EncKeyDialog::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_key"), &EncKeyDialog::get_key);
	ClassDB::bind_method(D_METHOD("_script_encryption_key_changed", "key"), &EncKeyDialog::_script_encryption_key_changed);
}
#endif // TOOLS_ENABLED
