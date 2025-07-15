/*************************************************************************/
/*  gdre_npck_dlg.cpp                                                     */
/*************************************************************************/
#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"

#include "gdre_npck_dlg.h"

#include "core/version.h"

#include "utility/gdre_version.gen.h"

NewPackDialog::NewPackDialog() {
	set_title(RTR("Create new PCK..."));
	set_flag(Window::Flags::FLAG_RESIZE_DISABLED, false);

	VBoxContainer *script_vb = memnew(VBoxContainer);
	script_vb->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	ver_base = memnew(SpinBox);
	ver_base->set_min(0);
	ver_base->set_max(2);
	ver_base->set_step(1);
	ver_base->set_value(2);
	ver_base->connect("value_changed", callable_mp(this, &NewPackDialog::_val_change));
	script_vb->add_margin_child(RTR("PCK version (\"0\" - Godot 2.x; \"1\" - Godot 3.x; \"2\" - Godot 4.x):"), ver_base);

	HBoxContainer *dir_hbc = memnew(HBoxContainer);

	ver_major = memnew(SpinBox);
	ver_major->set_min(0);
	ver_major->set_max(99999);
	ver_major->set_step(1);
	ver_major->set_value(GODOT_VERSION_MAJOR);
	ver_major->connect("value_changed", callable_mp(this, &NewPackDialog::_val_change));
	dir_hbc->add_child(ver_major);

	ver_minor = memnew(SpinBox);
	ver_minor->set_min(0);
	ver_minor->set_max(99999);
	ver_minor->set_step(1);
	ver_minor->set_value(GODOT_VERSION_MINOR);
	ver_minor->connect("value_changed", callable_mp(this, &NewPackDialog::_val_change));
	dir_hbc->add_child(ver_minor);

	ver_rev = memnew(SpinBox);
	ver_rev->set_min(0);
	ver_rev->set_max(99999);
	ver_rev->set_step(1);
	ver_rev->set_value(0);
	ver_rev->connect("value_changed", callable_mp(this, &NewPackDialog::_val_change));
	dir_hbc->add_child(ver_rev);

	HBoxContainer *enc_hbc = memnew(HBoxContainer);
	enc_dir_chk = memnew(CheckBox);
	enc_dir_chk->set_text("Encrypt directory");
	enc_hbc->add_child(enc_dir_chk);

	Label *enc_lbl_in = memnew(Label);
	enc_lbl_in->set_text("Include filters:");
	enc_hbc->add_child(enc_lbl_in);

	enc_filters_in = memnew(LineEdit);
	enc_filters_in->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	enc_hbc->add_child(enc_filters_in);

	Label *enc_lbl_ex = memnew(Label);
	enc_lbl_ex->set_text("Exclude filters:");
	enc_hbc->add_child(enc_lbl_ex);

	enc_filters_ex = memnew(LineEdit);
	enc_filters_ex->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	enc_hbc->add_child(enc_filters_ex);

	script_vb->add_margin_child(RTR("Encryption:"), enc_hbc);

	_val_change();

	script_vb->add_margin_child(RTR("Godot engine version (\"Major\".\"Minor\".\"Patch\", \"Patch\" value should be \"0\" for all pre 3.2 versions):"), dir_hbc);

	emb_selection = memnew(FileDialog);
	emb_selection->set_access(FileDialog::ACCESS_FILESYSTEM);
	emb_selection->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
	emb_selection->add_filter("*.exe,*.bin,*.32,*.64;Executable files");
	emb_selection->connect("file_selected", callable_mp(this, &NewPackDialog::_exe_select_request));
	emb_selection->set_show_hidden_files(true);
	add_child(emb_selection);

	HBoxContainer *emb_hbc = memnew(HBoxContainer);
	emb_chk = memnew(CheckBox);
	emb_chk->set_text("Embed PCK");
	emb_hbc->add_child(emb_chk);

	emb_name = memnew(LineEdit);
	emb_name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	emb_hbc->add_child(emb_name);

	emb_button = memnew(Button);
	emb_button->set_text("...");
	emb_button->connect("pressed", callable_mp(this, &NewPackDialog::_exe_select_pressed));
	emb_hbc->add_child(emb_button);

	script_vb->add_margin_child(RTR("Embed PCK:"), emb_hbc);

	wmark = memnew(LineEdit);
	wmark->set_text(String("Created with Godot RE tools, ") + String(GDRE_VERSION));
	script_vb->add_margin_child(RTR("Extra tag:"), wmark);

	add_child(script_vb);

	get_ok_button()->set_text(RTR("Save..."));
	add_cancel_button(RTR("Cancel"));
}

void NewPackDialog::_val_change(double p_val) {
#ifdef TOOLS_ENABLED
	Color error_color = (EditorNode::get_singleton()) ? EditorNode::get_singleton()->get_gui_base()->get_theme_color("error_color", "Editor") : Color(1, 0, 0);
#else
	Color error_color = Color(1, 0, 0);
#endif
	Color def_color = ver_major->get_line_edit()->get_theme_color("font_color");
	if (ver_base->get_value() < 2) {
		if ((ver_major->get_value() <= 2 && ver_base->get_value() != 0) || (ver_major->get_value() > 2 && ver_base->get_value() != 1)) {
			ver_base->get_line_edit()->set("custom_colors/font_color", error_color);
		} else {
			ver_base->get_line_edit()->set("custom_colors/font_color", def_color);
		}

		if ((ver_major->get_value() < 3 || (ver_major->get_value() == 3 && ver_minor->get_value() < 2)) && (ver_rev->get_value() != 0)) {
			ver_rev->get_line_edit()->set("custom_colors/font_color", error_color);
		} else {
			ver_rev->get_line_edit()->set("custom_colors/font_color", def_color);
		}
	} else if (ver_base->get_value() == 2) {
		if (ver_major->get_value() < 4) {
			ver_base->get_line_edit()->set("custom_colors/font_color", error_color);
		} else {
			ver_base->get_line_edit()->set("custom_colors/font_color", def_color);
		}
	}
	enc_dir_chk->set_disabled(ver_base->get_value() < 2);
	enc_filters_in->set_editable(ver_base->get_value() == 2);
	enc_filters_ex->set_editable(ver_base->get_value() == 2);
}

void NewPackDialog::_exe_select_pressed() {
	emb_selection->popup_centered(Size2(600, 400));
}

void NewPackDialog::_exe_select_request(const String &p_path) {
	emb_name->set_text(p_path);
}

NewPackDialog::~NewPackDialog() {
	//NOP
}

bool NewPackDialog::get_is_emb() const {
	return emb_chk->is_pressed();
}

String NewPackDialog::get_emb_source() const {
	return emb_name->get_text();
}

bool NewPackDialog::get_enc_dir() const {
	return enc_dir_chk->is_pressed();
}

String NewPackDialog::get_enc_filters_in() const {
	return enc_filters_in->get_text();
}

String NewPackDialog::get_enc_filters_ex() const {
	return enc_filters_ex->get_text();
}

int NewPackDialog::get_version_pack() const {
	return ver_base->get_value();
}

int NewPackDialog::get_version_major() const {
	return ver_major->get_value();
}

int NewPackDialog::get_version_minor() const {
	return ver_minor->get_value();
}

int NewPackDialog::get_version_rev() const {
	return ver_rev->get_value();
}

String NewPackDialog::get_watermark() const {
	return wmark->get_text();
}

void NewPackDialog::_notification(int p_notification) {
	//NOP
}

void NewPackDialog::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_exe_select_pressed"), &NewPackDialog::_exe_select_pressed);
	ClassDB::bind_method(D_METHOD("_exe_select_request", "path"), &NewPackDialog::_exe_select_request);
	ClassDB::bind_method(D_METHOD("_val_change", "val"), &NewPackDialog::_val_change);
}
#endif
