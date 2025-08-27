extends GDREAcceptDialogBase

enum ActionOption {
	OUTPUT_TO_DIR = 0,
	CREATE_PATCH_PCK = 1,
	PATCH_PCK_DIRECTLY = 2
}

const ONLY_DIR = false

const ACTION_LABELS = {
	ActionOption.OUTPUT_TO_DIR: "Output to Directory",
	ActionOption.CREATE_PATCH_PCK: "Create New Patch PCK",
	ActionOption.PATCH_PCK_DIRECTLY: "Patch PCK Directly"
}

signal do_patch_pck(dest_pck: String, file_map: Dictionary[String, String], should_embed: bool);

var cached_csv_map: Dictionary[String, PackedStringArray] = {}
var cached_translation_info: Dictionary[String, int] = {}
var cached_translation_import_info: Dictionary[String, ImportInfo] = {}
func clear():
	var opt: OptionButton = %ActionOptionButton
	var opt2: OptionButton = %SelectTranslationButton
	%CSVField.text = ""
	%ProjectField.text = ""
	opt2.clear()
	cached_csv_map.clear()
	cached_translation_info.clear()
	cached_translation_import_info.clear()
	for child in %CheckBoxGroup.get_children():
		%CheckBoxGroup.remove_child(child)
		child.queue_free()
	opt.select(ActionOption.OUTPUT_TO_DIR)
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()

func show_win():
	clear()
	var safe_area: Rect2i = DisplayServer.get_display_safe_area()
	var center = (safe_area.position + safe_area.size - self.size) / 2
	self.set_position(center)
	show()

func close():
	clear()
	hide()

func confirm():
	if get_selected_action() != ActionOption.OUTPUT_TO_DIR:
		# change it to a save type rather than an open type
		%SelectOutputDialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
		if get_selected_action() == ActionOption.CREATE_PATCH_PCK:
			%SelectOutputDialog.current_file = GDRESettings.get_pack_path().get_file().get_basename() + "_translation_patch.pck"
		else:
			%SelectOutputDialog.current_file = GDRESettings.get_pack_path().get_file().get_basename() + "_patched.pck"
	else:
		%SelectOutputDialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR

	%SelectOutputDialog.popup_centered()
	pass

func cancelled():
	close()


func load_test():
	_load_project(['/Users/nikita/Library/Application Support/Steam/steamapps/common/Ex-Zodiac/Ex-Zodiac.app/Contents/Resources/Ex-Zodiac.pck'])
	%SelectTranslationButton.select(1)
	_on_select_csv_dialog_file_selected("/Users/nikita/Desktop/missions_patched.csv")


func _ready():
	self.confirmed.connect(self.confirm)
	self.close_requested.connect(self.close)
	self.canceled.connect(self.cancelled)
	clear()
	# load_test()

func get_translations():
	var imports: Array = GDRESettings.get_import_files(false)
	var translations = []
	for import in imports:
		if import.get_importer().contains("translation"):
			translations.append(import)
	return translations


func get_selected_translation():
	var opt: OptionButton = %SelectTranslationButton
	var id = opt.selected
	if id == -1:
		return ""
	var source = opt.get_item_text(id)
	return source

func _set_translation_option_button():
	var opt: OptionButton = %SelectTranslationButton
	opt.clear()
	var translations = get_translations()
	var sources = []
	for translation: ImportInfo in translations:
		if not sources.has(translation.source_file):
			sources.append(translation.source_file)
			cached_translation_info[translation.source_file] = TranslationExporter.count_non_empty_messages_from_info(translation)
			cached_translation_import_info[translation.source_file] = translation
	for source in sources:
		opt.add_item(source)
	opt.select(0)

func _load_project(paths: PackedStringArray):
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()
	var err = GDRESettings.load_project(paths, true)
	if err != OK:
		popup_error_box("Failed to load project:\n" + GDRESettings.get_recent_error_string(), "Error")
		return
	err = GDRESettings.load_import_files()
	if err != OK:
		popup_error_box("Failed to load import files:\n" + GDRESettings.get_recent_error_string(), "Error")
		GDRESettings.unload_project()
		return
	err = GDRESettings.load_project_config()
	if err != OK:
		popup_error_box("Failed to load project config:\n" + GDRESettings.get_recent_error_string(), "Error")
		GDRESettings.unload_project()
		return
	%ProjectField.text = GDRESettings.get_pack_path()
	_set_translation_option_button()
	var pack_infos: Array[PackInfo] = GDRESettings.get_pack_info_list()
	var enable_direct_patch = false
	if (pack_infos.size() == 1 and pack_infos[0].get_type() == PackInfo.PCK):
		enable_direct_patch = true
	_reload_action_option_button(enable_direct_patch)
	_validate()

func get_selected_action() -> ActionOption:
	var opt: OptionButton = %ActionOptionButton
	return opt.selected as ActionOption

func _reload_action_option_button(enable_direct_patch: bool):
	var opt: OptionButton = %ActionOptionButton
	opt.clear()
	for action in ACTION_LABELS.keys():
		if (ONLY_DIR and action != ActionOption.OUTPUT_TO_DIR):
			continue
		if (action == ActionOption.PATCH_PCK_DIRECTLY and not enable_direct_patch):
			continue
		opt.add_item(ACTION_LABELS[action], action)
	opt.select(ActionOption.OUTPUT_TO_DIR)

func _reload_csv_option_buttons(keys: Dictionary[String, PackedStringArray]):
	var container: VBoxContainer = %CheckBoxGroup
	for child in container.get_children():
		container.remove_child(child)
		child.queue_free()
	for key in keys.keys():
		if key == "key":
			continue
		var button: CheckBox = CheckBox.new()
		button.text = key
		container.add_child(button)

func get_selected_locales():
	var container: VBoxContainer = %CheckBoxGroup
	var selected = PackedStringArray()
	for child in container.get_children():
		if not child is CheckBox:
			continue
		var box: CheckBox = child
		if box.button_pressed:
			selected.push_back(box.text)
	print(str(selected))
	return selected

func _validate():
	var disable_ok = false
	if not GDRESettings.is_pack_loaded():
		disable_ok = true
	if %SelectTranslationButton.selected == -1:
		disable_ok = true
	if %CSVField.text == "":
		disable_ok = true

	if disable_ok:
		get_ok_button().disabled = true
	else:
		get_ok_button().disabled = false



func _on_select_project_dialog_files_selected(paths: PackedStringArray) -> void:
	call_on_next_process(call_on_next_process.bind(_load_project.bind(paths)))

func _on_select_project_dialog_file_selected(path: String) -> void:
	_on_select_project_dialog_files_selected([path])
	pass # Replace with function body.


func _on_select_project_dialog_dir_selected(dir: String) -> void:
	_on_select_project_dialog_files_selected([dir])
	pass # Replace with function body.



func _on_select_csv_button_pressed() -> void:
	%SelectCSVDialog.popup_centered()
	pass


func _on_select_csv_dialog_file_selected(path: String) -> void:
	%CSVField.text = path
	var ret_info: Dictionary = {}
	cached_csv_map = TranslationExporter.get_csv_messages(path, ret_info)
	if cached_csv_map.size() == 0:
		popup_error_box("Invalid CSV file:\n" + GDRESettings.get_recent_error_string(), "Error")
		%CSVField.text = ""
		return
	_reload_csv_option_buttons(cached_csv_map)
	_validate()
	var selected = get_selected_translation()
	if (not selected.is_empty() and ret_info.get("new_non_empty_count", 0) != cached_translation_info.get(selected, 0)):
		var msg = "The CSV file has a different number of messages than the translation file.\n"
		msg += "CSV file keys = " + str(ret_info.get("new_non_empty_count", 0))
		msg += "Translation messages = " + str(cached_translation_info.get(selected, 0))
		if ret_info.get("missing_keys", 0) > 0:
			msg += "The resulting patched translation file may be corrupted\n"
		popup_error_box(msg + "Please ensure that the correct translation is selected and the CSV file is up to date.", "Warning")

	pass # Replace with function body.

func do_it(output: String):
	GDRESettings.get_recent_error_string()
	var output_dir: String = output
	if get_selected_action() != ActionOption.OUTPUT_TO_DIR:
		var ext = output.get_extension()
		var dot_ext = "." + ext
		if (output.ends_with(dot_ext + dot_ext)):
			output = output.trim_suffix(dot_ext + dot_ext) + dot_ext
		#GDRESettings::get_gdre_user_path
		output_dir = GDRESettings.get_gdre_user_path().path_join(".tmp_translations")
		if DirAccess.dir_exists_absolute(output_dir):
			GDRECommon.rimraf(output_dir)
	GDRECommon.ensure_dir(output_dir)

	var selected = get_selected_translation()
	if selected.is_empty():
		popup_error_box("Please select a translation file", "Error")
		return
	var r_file_map: Dictionary[String, String] = {}
	var err = TranslationExporter.patch_translations(output_dir, %CSVField.text, ImportInfo.copy(cached_translation_import_info[selected]), get_selected_locales(), r_file_map)
	if err != OK:
		popup_error_box("Failed to patch translation:\n" + GDRESettings.get_recent_error_string(), "Error")
		return
	err = TranslationExporter.patch_project_config(output_dir, r_file_map)
	if err != OK:
		popup_error_box("Failed to patch project config:\n" + GDRESettings.get_recent_error_string(), "Error")
		return
	if get_selected_action() == ActionOption.OUTPUT_TO_DIR:
		popup_error_box("Translation patched successfully", "Success", self.close)
	elif get_selected_action() == ActionOption.CREATE_PATCH_PCK:
		var engine_version: GodotVer = GodotVer.parse_godotver(GDRESettings.get_version_string())
		var pck_creator = PckCreator.new()
		var version = 0
		if engine_version.major == 4:
			version = 2
		elif engine_version.major == 3:
			version = 1
		else:
			version = 0

		pck_creator.start_pck(output,
								version,
								engine_version.major,
								engine_version.minor,
								engine_version.patch,
								false,
								false,
								"")
		err = pck_creator.add_files(r_file_map)
		if err != OK:
			popup_error_box("Failed to add files to patch PCK:\n" + GDRESettings.get_recent_error_string(), "Error")
			return
		err = pck_creator.finish_pck()
		if err != OK:
			popup_error_box("Failed to finish patch PCK:\n" + GDRESettings.get_recent_error_string(), "Error")
			return
		popup_error_box("Patch PCK created successfully", "Success", self.close)
	elif get_selected_action() == ActionOption.PATCH_PCK_DIRECTLY:
		var pck_files = GDRESettings.get_file_list()
		var new_map: Dictionary[String, String] = {}
		for file in pck_files:
			new_map[file] = file
		for file in r_file_map.keys():
			new_map[file] = r_file_map[file]
		do_patch_pck.emit(output, new_map, false)
		self.hide()




func _on_select_output_dialog_dir_selected(dir: String) -> void:
	call_on_next_process(call_on_next_process.bind(do_it.bind(dir)))


func _on_select_output_dialog_file_selected(path: String) -> void:
	call_on_next_process(call_on_next_process.bind(do_it.bind(path)))


func _on_select_project_button_pressed() -> void:
	%SelectProjectDialog.popup_centered()
