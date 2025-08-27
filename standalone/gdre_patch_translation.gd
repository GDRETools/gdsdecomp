extends GDREAcceptDialogBase

enum ActionOption {
	OUTPUT_TO_DIR = 0,
	CREATE_PATCH_PCK = 1,
	PATCH_PCK_DIRECTLY = 2
}

enum TranslationTreeButton{
	EDIT = 0,
	DELETE = 1
}

const ONLY_DIR = false

const ACTION_LABELS = {
	ActionOption.OUTPUT_TO_DIR: "Output to Directory",
	ActionOption.CREATE_PATCH_PCK: "Create New Patch PCK",
	ActionOption.PATCH_PCK_DIRECTLY: "Patch PCK Directly"
}
@onready var edit_button: Texture2D = get_theme_icon("select_option", "Tree")
@onready var delete_button: Texture2D = get_theme_icon("indeterminate", "Tree")

signal do_patch_pck(dest_pck: String, file_map: Dictionary[String, String], should_embed: bool);

var cached_csv_map: Dictionary[String, PackedStringArray] = {}
var cached_translation_info: Dictionary[String, int] = {}
var cached_translation_import_info: Dictionary[String, ImportInfo] = {}
var cached_translation_locales: Dictionary[String, Dictionary] = {}
# Multi-translation support
var translation_entries: Dictionary[String, TranslationEntry] = {}


var current_csv_messages: Dictionary[String, PackedStringArray] = {}
var current_csv_info: Dictionary = {}

class TranslationEntry:
	var translation_source: String
	var csv_path: String
	var selected_locales: PackedStringArray
	var import_info: ImportInfo
	var non_empty_count: int
	var missing_keys: int

	func _init(source: String, csv: String, locales: PackedStringArray, info: ImportInfo, tr_info: Dictionary):
		translation_source = source
		csv_path = csv
		selected_locales = locales
		import_info = info
		non_empty_count = tr_info.get("new_non_empty_count", 0)
		missing_keys = tr_info.get("missing_keys", 0)

func clear():
	var opt: OptionButton = %ActionOptionButton
	%ProjectField.text = ""
	cached_csv_map.clear()
	cached_translation_info.clear()
	cached_translation_import_info.clear()
	cached_translation_locales.clear()
	translation_entries.clear()
	_clear_translation_tree()
	opt.select(ActionOption.OUTPUT_TO_DIR)
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()

func _clear_translation_tree():
	var tree: Tree = %GDREFileTree
	tree.clear()

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
	_repopulate_select_translation_button()
	%SelectTranslationButton.select(1)
	_on_select_csv_dialog_file_selected("/Users/nikita/Desktop/missions_patched.csv")
	var container: VBoxContainer = %CheckBoxGroup
	# Clear existing checkboxes
	var children = container.get_children()
	for i in range(1, min(3, container.get_child_count())):
		var child = container.get_child(i)
		if child is CheckBox:
			child.button_pressed = true

	_on_add_translation_dialog_confirmed()

func _on_patch_tree_button_clicked(item: TreeItem, _column: int, id: int, mouse_button_index: int):
	if (mouse_button_index != MOUSE_BUTTON_LEFT):
		return
	var src = item.get_metadata(0)
	match id:
		TranslationTreeButton.EDIT:
			_on_add_translation_button_pressed() # pops up the dialog and populates the fields
			# reopen the add translation dialog with the current entry
			var entry = translation_entries[src]
			# erase the entry from the list
			translation_entries.erase(src)
			for i in range(0, %SelectTranslationButton.get_item_count()):
				if %SelectTranslationButton.get_item_text(i) == entry.translation_source:
					%SelectTranslationButton.select(i)
					break
			_on_select_csv_dialog_file_selected(entry.csv_path)

			for i in range(0, %CheckBoxGroup.get_child_count()):
				var child = %CheckBoxGroup.get_child(i)
				if entry.selected_locales.has(child.text):
					child.button_pressed = true
			_update_translation_entries_display()
		TranslationTreeButton.DELETE:
			translation_entries.erase(src)
			_update_translation_entries_display()
			pass
	pass

func _ready():
	self.confirmed.connect(self.confirm)
	self.close_requested.connect(self.close)
	self.canceled.connect(self.cancelled)
	var file_tree: Tree = %GDREFileTree
	file_tree.columns = 3
	%GDREFileTree.connect("button_clicked", self._on_patch_tree_button_clicked)
	clear()
	# load_test()

func get_translations():
	var imports: Array = GDRESettings.get_import_files(false)
	var translations = []
	for import in imports:
		if import.get_importer().contains("translation"):
			translations.append(import)
	return translations

func _populate_translation_tree():
	var translations = get_translations()
	var sources = []
	for translation: ImportInfo in translations:
		if not sources.has(translation.source_file):
			sources.append(translation.source_file)
			cached_translation_info[translation.source_file] = TranslationExporter.count_non_empty_messages_from_info(translation)
			cached_translation_import_info[translation.source_file] = translation
			cached_translation_locales[translation.source_file] = TranslationExporter.get_messages_from_translation(translation)

	# Update the display
	_update_translation_entries_display()

func _load_project(paths: PackedStringArray):
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()
	var err = GDRESettings.load_project(paths, true)
	if err != OK:
		popup_error_box("Failed to load project:\n" + GDRESettings.get_recent_error_string(), "Error")
		return
	err = GDRESettings.post_load_patch_translation()
	if err != OK:
		popup_error_box("Failed to load import files:\n" + GDRESettings.get_recent_error_string(), "Error")
		GDRESettings.unload_project()
		return
	%ProjectField.text = GDRESettings.get_pack_path()
	_populate_translation_tree()
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

func _reload_add_dialog_csv_option_buttons(keys: Dictionary[String, PackedStringArray]):
	var dialog = %AddTranslationDialog
	var container: VBoxContainer = %CheckBoxGroup

	# Clear existing checkboxes
	for child in container.get_children():
		if child is CheckBox:
			container.remove_child(child)
			child.queue_free()

	# Add new checkboxes for each locale
	for key in keys.keys():
		if key == "key":
			continue
		var button: CheckBox = CheckBox.new()
		button.text = key
		container.add_child(button)

func _validate():
	var disable_ok = false
	if not GDRESettings.is_pack_loaded():
		disable_ok = true
	if translation_entries.size() == 0:
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
	# This is called from the AddTranslationDialog
	var dialog = %AddTranslationDialog
	var csv_field: LineEdit = %CSVField
	var opt: OptionButton = %SelectTranslationButton

	csv_field.text = path
	current_csv_info.clear()
	current_csv_messages = TranslationExporter.get_csv_messages(path, current_csv_info)
	if current_csv_messages.size() == 0:
		popup_error_box("Invalid CSV file:\n" + GDRESettings.get_recent_error_string(), "Error")
		csv_field.text = ""
		return

	# Update checkboxes in the dialog
	_reload_add_dialog_csv_option_buttons(current_csv_messages)

	# Validate against selected translation
	if opt.selected != -1:
		var selected_source = opt.get_item_text(opt.selected)
		if (not selected_source.is_empty() and current_csv_info.get("new_non_empty_count", 0) != cached_translation_info.get(selected_source, 0)):
			var msg = "The CSV file has a different number of messages than the translation file.\n"
			msg += "CSV file keys = %d\n" % current_csv_info.get("new_non_empty_count", 0)
			msg += "Translation messages = %d\n" % cached_translation_info.get(selected_source, 0)
			if current_csv_info.get("missing_keys", 0) > 0:
				msg += "The resulting patched translation file may be corrupted!\n"
			popup_error_box(msg + "Please ensure that the correct translation is selected and the CSV file is up to date.", "Warning")

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

	if translation_entries.size() == 0:
		popup_error_box("Please add at least one translation", "Error")
		return

	var r_file_map: Dictionary[String, String] = {}

	# Process each translation entry
	var err: int
	for entry in translation_entries.values():
		err = TranslationExporter.patch_translations(output_dir, entry.csv_path, ImportInfo.copy(entry.import_info), entry.selected_locales, r_file_map)
		if err != OK:
			popup_error_box("Failed to patch translation " + entry.translation_source + ":\n" + GDRESettings.get_recent_error_string(), "Error")
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

func _repopulate_select_translation_button():
	var opt: OptionButton = %SelectTranslationButton
	opt.clear()
	var translations = get_translations()
	var sources = []
	for translation: ImportInfo in translations:
		if not sources.has(translation.source_file):
			sources.append(translation.source_file)
	for source in sources:
		opt.add_item(source)
	if sources.size() > 0:
		opt.select(0)

func _on_add_translation_button_pressed() -> void:
	if not GDRESettings.is_pack_loaded():
		popup_error_box("Please load a project first", "Error")
		return

	# Reset the AddTranslationDialog
	var dialog = %AddTranslationDialog
	_repopulate_select_translation_button()

	# Clear CSV field and checkboxes
	%CSVField.text = ""
	_clear_add_dialog_checkboxes()

	dialog.popup_centered()

func _clear_add_dialog_checkboxes():
	var container: VBoxContainer = %AddTranslationDialog.get_node("VBoxContainer/ScrollContainer/CheckBoxGroup")
	for child in container.get_children():
		if child is CheckBox:
			child.queue_free()

func _on_add_translation_dialog_confirmed() -> void:
	var dialog = %AddTranslationDialog
	var opt: OptionButton = %SelectTranslationButton
	var csv_field: LineEdit = %CSVField

	if opt.selected == -1:
		popup_error_box("Please select a translation", "Error")
		return

	if csv_field.text.is_empty():
		popup_error_box("Please select a CSV file", "Error")
		return

	var selected_source = opt.get_item_text(opt.selected)
	var selected_locales = _get_add_dialog_selected_locales()

	if selected_locales.size() == 0:
		popup_error_box("Please select at least one locale", "Error")
		return

	# Create translation entry
	var entry = TranslationEntry.new(
		selected_source,
		csv_field.text,
		selected_locales,
		cached_translation_import_info[selected_source],
		current_csv_info
	)

	translation_entries[entry.translation_source] = entry
	_update_translation_entries_display()
	dialog.hide()
	_validate()

func _get_add_dialog_selected_locales() -> PackedStringArray:
	var dialog = %AddTranslationDialog
	var container: VBoxContainer = dialog.get_node("VBoxContainer/ScrollContainer/CheckBoxGroup")
	var selected = PackedStringArray()
	for child in container.get_children():
		if child is CheckBox and child.button_pressed:
			selected.push_back(child.text)
	return selected

func _update_translation_entries_display():
	# Update the tree to show only added translations
	var tree = %GDREFileTree
	tree.clear()
	tree.set_column_title(1, "CSV Path")
	tree.set_column_custom_minimum_width(2, 300)

	# Only show translations that have been added
	for entry in translation_entries.values():
		var icon = tree.file_ok
		var message_count = cached_translation_info[entry.translation_source]
		if message_count != entry.non_empty_count and entry.missing_keys > 0:
			icon = tree.file_warning
		var item: TreeItem = tree.add_file_tree_item(entry.translation_source, icon, -1, "", ", ".join(entry.selected_locales))
		item.set_editable(0, false)
		item.set_editable(1, false)
		item.set_editable(2, false)
		item.set_metadata(0, entry.translation_source)
		item.set_cell_mode(1, TreeItem.CELL_MODE_STRING)
		item.set_text_direction(1, Control.TextDirection.TEXT_DIRECTION_RTL)
		item.set_structured_text_bidi_override(1, TextServer.STRUCTURED_TEXT_FILE)
		item.set_text(1, "" + entry.csv_path)
		item.add_button(2, edit_button, TranslationTreeButton.EDIT)
		item.add_button(2, delete_button, TranslationTreeButton.DELETE)

	_validate()

func _on_add_translation_dialog_canceled() -> void:
	%AddTranslationDialog.hide()
