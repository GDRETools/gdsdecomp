class_name GDREDecompile
extends GDRECompDecomp

	# "4.5-dev.6 (2e216b5 / 2025-06-10 / Bytecode version: 101) - content header size changed"
	# "	4.5-dev.5 (ee121ef / 2025-06-09 / Bytecode version: 100) - Added `PERIOD_PERIOD_PERIOD` token."
	# "	4.5-dev.4 (b59d6be / 2025-05-18 / Bytecode version: 100) - Added `ABSTRACT` token."


func get_bytecode_parts(idx = -1) -> PackedStringArray:
	if idx == -1:
		idx = %BytecodeSelector.get_selected_id()
	var bytecode_str = %BytecodeSelector.get_item_text(idx).strip_edges()
	if bytecode_str.is_empty() or bytecode_str.begins_with("---"):
		return []
	var parts = bytecode_str.split(" ", false, 1)
	if parts.size() == 1:
		parts.append("")
	else:
		var info_str: String = parts[1].split(")", true, 1)[0].strip_edges().trim_prefix("(")
		var rev_str = info_str.get_slice("/", 0).strip_edges()
		var date_str = info_str.get_slice("/", 1).strip_edges()
		var bytecode_version_str = info_str.get_slice("/", 2).strip_edges()
		parts[1] = rev_str
		parts.append(date_str)
		parts.append(bytecode_version_str)
	return parts

func get_engine_version(idx = -1) -> String:
	var parts = get_bytecode_parts(idx)
	if parts.is_empty():
		return ""
	var engine_version = parts[0].strip_edges()
	return engine_version

func get_bytecode_revision(idx = -1) -> int:
	var parts = get_bytecode_parts(idx)
	if parts.is_empty():
		return 0
	var revision_str = parts[1].strip_edges()
	return revision_str.hex_to_int()

func get_bytecode_ver_num(idx = -1) -> int:
	var parts = get_bytecode_parts(idx)
	if parts.is_empty():
		return 0
	var bytecode_version_str = parts[3].strip_edges().split(":", true, 1)[1].strip_edges()
	return bytecode_version_str.to_int()

func get_bytecode_revision_and_version(idx = -1) -> Array[int]:
	var parts = get_bytecode_parts(idx)
	if parts.is_empty():
		return []
	var revision_str = parts[1].strip_edges()
	var bytecode_version_str = parts[3].strip_edges().split(":", true, 1)[1].strip_edges()
	return [revision_str.hex_to_int(), bytecode_version_str.to_int()]

func confirm():
	var engine_version = get_engine_version()
	var bytecode_revision = get_bytecode_revision()
	var decomp = GDScriptDecomp.create_decomp_for_commit(bytecode_revision)
	if decomp == null:
		popup_error_box("Failed to create decompiler for version: " + engine_version + " (revision: " + String.num_int64(bytecode_revision, 16) + ")", "Decompiler Error")
		return
	var files = get_file_list()
	if files.size() == 0:
		popup_error_box("No files selected", "Error")
		return
	var dest_folder = %DestinationFolder.text
	if dest_folder == "":
		popup_error_box("No destination folder selected", "Error")
		return
	for file in files:
		var err = decomp.decompile_byte_code(file)
		if err != OK:
			popup_error_box("Failed to decompile file: " + file, "Decompile Error")
			return
		var decompiled = decomp.get_script_text()
		var dest_file = dest_folder.path_join(file.get_file().get_basename() + ".gd")
		var f = FileAccess.open(dest_file, FileAccess.WRITE)
		if f == null:
			popup_error_box("Failed to open file for writing: " + dest_file, "File Error")
			return
		f.store_string(decompiled)
		f.close()
	popup_confirm_box("The following files were successfully decompiled: " + "\n".join(files), "Decompile Complete", self.close, self.close)


func _get_bytecode_version_for_file(buf: PackedByteArray) -> int:
	if buf.size() < 4:
		return 0
	var text = buf.slice(0, 4)
	# check for 'G' 'D' 'S' 'C'
	if not (String.chr(text[0]) == 'G' and String.chr(text[1]) == 'D' and String.chr(text[2]) == 'S' and String.chr(text[3]) == 'C'):
		return 0
	# well, this may not work on big endian files
	var version = buf[4] | buf[5] << 8 | buf[6] << 16 | buf[7] << 24
	return version


func get_bytecode_buffer_for_file(file: String) -> PackedByteArray:
	if file.get_extension().to_lower() == "gde":
		var buf = GDScriptDecomp.get_buffer_encrypted(file, 3, GDRESettings.get_encryption_key())
		if buf.size() == 0:
			process_error("ERROR: Failed to get bytecode buffer for encrypted file (did you set the encryption key?)")
			return PackedByteArray()
		return buf
	var fa = FileAccess.open(file, FileAccess.READ)
	if fa == null:
		process_error("ERROR: Failed to open file: " + file)
		return PackedByteArray()
	return fa.get_buffer(fa.get_length())


func reset_bytecode_selector() -> void:
	repopulate_bytecode_selector(%GDRETextEditor.is_visible_in_tree())

func process_error(error: String) -> void:
	var err_label_color = Color.RED
	if error.begins_with("WARNING:"):
		err_label_color = Color.YELLOW
	%ErrorLabel.text = error
	%ErrorLabel.add_theme_color_override("font_color", err_label_color)


func _test_bytecode_for_file(file_bufs: Dictionary[String, PackedByteArray], item_bytecode_revision: int) -> bool:
	var decomp = GDScriptDecomp.create_decomp_for_commit(item_bytecode_revision)
	if decomp == null:
		process_error("WARNING: Failed to create decompiler for revision: " + String.num_int64(item_bytecode_revision, 16))
		return false
	for file: String in file_bufs.keys():
		var test_result = decomp.test_bytecode(file_bufs[file])
		if test_result != GDScriptDecomp.BYTECODE_TEST_PASS:
			var err_str = decomp.get_error_message()
			process_error("WARNING: bytecode test failed for file: " + file.get_file() + "\n" + err_str)
			return false
	return true


func _update_bytecode_selector() -> void:
	# get all the files in the file list
	var bytecode_versions: Dictionary[int, bool] = {}
	if %FileList.get_item_count() == 0:
		reset_bytecode_selector()
		return
	var file_bufs: Dictionary[String, PackedByteArray] = {}
	for i in %FileList.get_item_count():
		var file = %FileList.get_item_text(i).strip_edges()
		if not file_bufs.has(file):
			file_bufs[file] = get_bytecode_buffer_for_file(file)
		if file_bufs[file].size() == 0:
			# error set by get_bytecode_buffer_for_file
			reset_bytecode_selector()
			return
		var version = _get_bytecode_version_for_file(file_bufs[file])
		if version != 0:
			bytecode_versions[version] = true
		else:
			process_error("Invalid GDScript bytecode file: " + file)
			reset_bytecode_selector()
			return
	if bytecode_versions.size() == 0:
		process_error("No bytecode versions found")
		reset_bytecode_selector()
		return
	if bytecode_versions.size() > 1:
		process_error("Multiple bytecode versions found: " + ", ".join(bytecode_versions.keys()))
		reset_bytecode_selector()
		return

	var bytecode_version = bytecode_versions.keys()[0]
	var current_idx = %BytecodeSelector.get_selected_id()
	# filter out the items from the bytecode selector that are not the selected bytecode version
	var idx = %BytecodeSelector.get_item_count() - 1
	while idx >= 0:
		var parts = get_bytecode_revision_and_version(idx)
		# don't disable the "--- Please select bytecode version ---" item
		if parts.is_empty():
			idx -= 1
			continue
		var item_bytecode_revision = parts[0]
		var item_bytecode_version = parts[1]
		if not %ForceEnableBox.is_pressed() and bytecode_version != item_bytecode_version:
			%BytecodeSelector.set_item_disabled(idx, true)
			if idx == current_idx:
				%BytecodeSelector.select(0)
		else:
			%BytecodeSelector.set_item_disabled(idx, false)
			if idx == current_idx:
				_test_bytecode_for_file(file_bufs, item_bytecode_revision)
		idx -= 1

func _refresh_decomp_preview() -> void:
	if not %GDRETextEditor.is_visible_in_tree():
		return
	var items = %FileList.get_selected_items()
	var revision = get_bytecode_revision()
	if revision == 0 and items.size() == 0:
		%GDRETextEditor.load_text_string("Select a file and bytecode version to see the decompiled code")
		return
	elif revision == 0:
		%GDRETextEditor.load_text_string("Select a bytecode version to see the decompiled code")
		return
	elif items.size() == 0:
		%GDRETextEditor.load_text_string("Select a file to see the decompiled code")
		return
	# single selection mode, so there will only be one item selected
	var path = %FileList.get_item_text(items[0]).strip_edges()
	if not path.is_empty():
		%GDRETextEditor.load_code(path, revision)

var dont_refresh = false
# overrides GDRECompDecomp.refresh()
func refresh() -> void:
	if dont_refresh:
		return
	%ErrorLabel.text = ""
	dont_refresh = true
	_update_bytecode_selector()
	_refresh_decomp_preview()
	dont_refresh = false

# overrides GDRECompDecomp.clear()
func clear():
	dont_refresh = true
	super.clear()
	%ErrorLabel.text = ""
	%GDRETextEditor.reset()
	reset_bytecode_selector()
	dont_refresh = false

func _on_file_list_item_selected(_idx: int) -> void:
	refresh()

func _on_show_resource_preview_toggled(toggled_on: bool) -> void:
	if toggled_on:
		%BytecodeSelector.fit_to_longest_item = false
		%GDRETextEditor.visible = true
		%MainSplit.set_split_offset(-(self.size.x / 2.0))
		%PreviewButton.text = "Hide Preview"
	else:
		%BytecodeSelector.fit_to_longest_item = true
		%GDRETextEditor.visible = false
		%MainSplit.set_split_offset(0)
		%PreviewButton.text = "Show Preview..."
		%GDRETextEditor.reset()
	reset_bytecode_selector()
	refresh()


func _on_check_box_toggled(toggled_on:  bool) -> void:
	refresh()
