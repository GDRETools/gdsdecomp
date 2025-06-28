class_name GDRECompDecomp
extends GDREChildDialog



func confirm():
	assert(false, "Not implemented")

func clear():
	%FileList.clear()

func close():
	clear()
	hide_win()

func cancelled():
	clear()
	hide_win()

func get_file_list():
	var ret: PackedStringArray = []
	for i in range(%FileList.get_item_count()):
		ret.append(%FileList.get_item_text(i))
	return ret

func refresh() -> void:
	pass

func _validate():
	if %FileList.get_item_count() == 0:
		self.get_ok_button().disabled = true
	elif %BytecodeSelector.get_selected_id() == 0:
		self.get_ok_button().disabled = true
	elif %DestinationFolder.text == "":
		self.get_ok_button().disabled = true
	else:
		self.get_ok_button().disabled = false

func _on_add_files_button_pressed() -> void:
	%AddFilesDialog.popup_centered()

func _on_remove_files_button_pressed() -> void:
	# get all the selected items in the list, then remove them
	var items: PackedInt32Array = %FileList.get_selected_items()
	items.reverse()
	for item in items:
		%FileList.remove_item(item)
	_validate()
	refresh()


func _on_clear_files_button_pressed() -> void:
	%FileList.clear()
	_validate()
	refresh()


func _on_destination_folder_button_pressed() -> void:
	%DestinationFolderDialog.popup_centered()

func _on_destination_folder_dialog_dir_selected(dir: String) -> void:
	%DestinationFolder.text = dir
	_validate()
	refresh()

func _on_add_files_dialog_files_selected(paths: PackedStringArray) -> void:
	for path in paths:
		%FileList.add_item(path)
	_validate()
	refresh()

func _on_bytecode_selector_item_selected(_index: int) -> void:
	_validate()
	refresh()

func _on_destination_folder_text_changed(_new_text: String) -> void:
	_validate()
	refresh()

func repopulate_bytecode_selector(split_long_versions: bool = false) -> void:
	var versions = GDScriptDecomp.get_bytecode_versions()
	_repopulate_bytecode_selector(versions, split_long_versions)

const MAX_LONG_LINE_LENGTH: int = 60

func _repopulate_bytecode_selector(versions: PackedStringArray, split_long_versions: bool = false) -> void:
	var prev_selected_id = %BytecodeSelector.get_selected_id()
	%BytecodeSelector.clear()
	%BytecodeSelector.add_item("--- Please select bytecode version ---")
	for version in versions:
		if split_long_versions and version.length() > MAX_LONG_LINE_LENGTH:
			var lines: PackedStringArray = []
			# we want the first split to be the at the dash right before the description
			var dash_index = version.find("- ")
			var split_index = dash_index + 2
			lines.append(version.left(split_index))
			lines.append(version.right(version.length() - split_index))
			while lines[-1].length() > MAX_LONG_LINE_LENGTH:
				var last_line = lines[-1]
				var last_line_space_index = last_line.find(" ", MAX_LONG_LINE_LENGTH)
				if last_line_space_index == -1:
					# just break, we can deal with a tiny extra
					break

				var left = last_line.left(last_line_space_index)
				var right = last_line.right(last_line.length() - last_line_space_index)
				lines[-1] = left
				lines.append(right)
			version = "\t\n".join(lines)
		%BytecodeSelector.add_item(version)
	if prev_selected_id != -1:
		%BytecodeSelector.select(prev_selected_id)
	else:
		%BytecodeSelector.select(0)


func _ready() -> void:
	self.add_cancel_button("Cancel")
	var versions = GDScriptDecomp.get_bytecode_versions()
	if %BytecodeSelector.get_item_count() != versions.size() + 1:
		_repopulate_bytecode_selector(versions)
	self.connect("files_dropped", self._on_add_files_dialog_files_selected)

	%AddFilesButton.connect("pressed", self._on_add_files_button_pressed)
	%RemoveFilesButton.connect("pressed", self._on_remove_files_button_pressed)
	%ClearFilesButton.connect("pressed", self._on_clear_files_button_pressed)
	%DestinationFolderButton.connect("pressed", self._on_destination_folder_button_pressed)
	%DestinationFolder.connect("text_changed", self._on_destination_folder_text_changed)
	%BytecodeSelector.connect("item_selected", self._on_bytecode_selector_item_selected)
	%AddFilesDialog.connect("files_selected", self._on_add_files_dialog_files_selected)
	%DestinationFolderDialog.connect("dir_selected", self._on_destination_folder_dialog_dir_selected)
	_validate()
