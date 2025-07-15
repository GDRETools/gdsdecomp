/*************************************************************************/
/*  gdre_cmp_dlg.cpp                                                     */
/*************************************************************************/
/*************************************************************************/
#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#endif
/*************************************************************************/

#include "gdre_cmp_dlg.h"

#include "bytecode/bytecode_versions.h"
#include "scene/gui/option_button.h"

ScriptCompDialog::ScriptCompDialog() {
	set_title(RTR("Compile GDScript"));
	set_flag(Window::Flags::FLAG_RESIZE_DISABLED, false);

	target_folder_selection = memnew(FileDialog);
	target_folder_selection->set_access(FileDialog::ACCESS_FILESYSTEM);
	target_folder_selection->set_file_mode(FileDialog::FILE_MODE_OPEN_DIR);
	target_folder_selection->connect("dir_selected", callable_mp(this, &ScriptCompDialog::_dir_select_request));
	target_folder_selection->set_show_hidden_files(true);
	add_child(target_folder_selection);

	file_selection = memnew(FileDialog);
	file_selection->set_access(FileDialog::ACCESS_FILESYSTEM);
	file_selection->set_file_mode(FileDialog::FILE_MODE_OPEN_FILES);
	file_selection->add_filter("*.gd;GDScript text files");
	file_selection->connect("files_selected", callable_mp(this, &ScriptCompDialog::_add_files_request));
	file_selection->set_show_hidden_files(true);
	add_child(file_selection);

	VBoxContainer *script_vb = memnew(VBoxContainer);
	script_vb->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	//File list
	file_list = memnew(ItemList);
	file_list->set_custom_minimum_size(Size2(400, 100) * EDSCALE);
	file_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	file_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	HBoxContainer *file_list_hbc = memnew(HBoxContainer);
	add_file = memnew(Button);
	add_file->set_text(RTR("Add files..."));
	add_file->connect("pressed", callable_mp(this, &ScriptCompDialog::_add_files_pressed));
	file_list_hbc->add_child(add_file);

	remove_file = memnew(Button);
	remove_file->set_text(RTR("Remove files"));
	remove_file->connect("pressed", callable_mp(this, &ScriptCompDialog::_remove_file_pressed));
	file_list_hbc->add_child(remove_file);

	clear_files = memnew(Button);
	clear_files->set_text(RTR("Clear files"));
	clear_files->connect("pressed", callable_mp(this, &ScriptCompDialog::_clear_pressed));
	file_list_hbc->add_child(clear_files);

	script_vb->add_margin_child(RTR("Script files:"), file_list, true);
	script_vb->add_child(file_list_hbc);

	//Script version
	scrver = memnew(OptionButton);

	for (int i = 0; decomp_versions[i].commit != 0; i++) {
		scrver->add_item(decomp_versions[i].name, decomp_versions[i].commit);
	}

	script_vb->add_margin_child(RTR("Script bytecode version:"), scrver);
	scrver->connect("item_selected", callable_mp(this, &ScriptCompDialog::_bytcode_changed));

	//Target directory
	HBoxContainer *dir_hbc = memnew(HBoxContainer);
	target_dir = memnew(LineEdit);
	target_dir->set_editable(false);
	target_dir->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	dir_hbc->add_child(target_dir);

	select_dir = memnew(Button);
	select_dir->set_text("...");
	select_dir->connect("pressed", callable_mp(this, &ScriptCompDialog::_dir_select_pressed));
	dir_hbc->add_child(select_dir);

	script_vb->add_margin_child(RTR("Destination folder:"), dir_hbc);

	script_key_error = memnew(Label);
	script_vb->add_child(script_key_error);

	add_child(script_vb);

	get_ok_button()->set_text(RTR("Compile"));
	_validate_input();

	add_cancel_button(RTR("Cancel"));
}

void ScriptCompDialog::_bytcode_changed(int p_id) {
	_validate_input();
}

int ScriptCompDialog::get_bytecode_version() const {
	return scrver->get_selected_id();
}

ScriptCompDialog::~ScriptCompDialog() {
	//NOP
}

Vector<String> ScriptCompDialog::get_file_list() const {
	Vector<String> ret;
	for (int i = 0; i < file_list->get_item_count(); i++) {
		ret.push_back(file_list->get_item_text(i));
	}
	return ret;
}

String ScriptCompDialog::get_target_dir() const {
	return target_dir->get_text();
}

void ScriptCompDialog::_add_files_pressed() {
	file_selection->popup_centered(Size2(600, 400));
}

void ScriptCompDialog::_add_files_request(const Vector<String> &p_files) {
	for (int i = 0; i < p_files.size(); i++) {
		file_list->add_item(p_files[i]);
	}
	_validate_input();
}

void ScriptCompDialog::_clear_pressed() {
	file_list->clear();
	_validate_input();
}

void ScriptCompDialog::_remove_file_pressed() {
	Vector<int> items = file_list->get_selected_items();
	for (int i = items.size() - 1; i >= 0; i--) {
		file_list->remove_item(items[i]);
	}
	_validate_input();
}

void ScriptCompDialog::_validate_input() {
	bool ok = true;
	String error_message;

#ifdef TOOLS_ENABLED
	Color error_color = (EditorNode::get_singleton()) ? EditorNode::get_singleton()->get_gui_base()->get_theme_color("error_color", "Editor") : Color(1, 0, 0);
#else
	Color error_color = Color(1, 0, 0);
#endif

	if (target_dir->get_text().is_empty()) {
		error_message += RTR("No destination folder selected") + "\n";
		script_key_error->add_theme_color_override("font_color", error_color);
		ok = false;
	}
	if (file_list->get_item_count() == 0) {
		error_message += RTR("No files selected") + "\n";
		script_key_error->add_theme_color_override("font_color", error_color);
		ok = false;
	}
	if (scrver->get_selected_id() == 0xfffffff || scrver->get_selected_id() == 0) {
		error_message += RTR("No bytecode version selected") + "\n";
		script_key_error->add_theme_color_override("font_color", error_color);
		ok = false;
	}

	script_key_error->set_text(error_message);

	get_ok_button()->set_disabled(!ok);
}

void ScriptCompDialog::_dir_select_pressed() {
	target_folder_selection->popup_centered(Size2(600, 400));
}

void ScriptCompDialog::_dir_select_request(const String &p_path) {
	target_dir->set_text(p_path);
	_validate_input();
}

void ScriptCompDialog::_notification(int p_notification) {
	//NOP
}

void ScriptCompDialog::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_file_list"), &ScriptCompDialog::get_file_list);
	ClassDB::bind_method(D_METHOD("get_target_dir"), &ScriptCompDialog::get_target_dir);

	ClassDB::bind_method(D_METHOD("_add_files_pressed"), &ScriptCompDialog::_add_files_pressed);
	ClassDB::bind_method(D_METHOD("_add_files_request", "files"), &ScriptCompDialog::_add_files_request);
	ClassDB::bind_method(D_METHOD("_remove_file_pressed"), &ScriptCompDialog::_remove_file_pressed);
	ClassDB::bind_method(D_METHOD("_clear_pressed"), &ScriptCompDialog::_clear_pressed);
	ClassDB::bind_method(D_METHOD("_dir_select_pressed"), &ScriptCompDialog::_dir_select_pressed);
	ClassDB::bind_method(D_METHOD("_dir_select_request", "path"), &ScriptCompDialog::_dir_select_request);
}
