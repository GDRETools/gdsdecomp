class_name GDREPatchPCK
extends GDREChildDialog

const file_icon: Texture2D = preload("res://gdre_icons/gdre_File.svg")
const file_ok: Texture2D = preload("res://gdre_icons/gdre_FileOk.svg")
const file_broken: Texture2D = preload("res://gdre_icons/gdre_FileBroken.svg")
const gdre_export_report = preload("res://gdre_export_report.tscn")
const gdre_file_tree = preload("res://gdre_file_tree.gd")
var arrow_right: Texture2D = get_theme_icon("arrow_collapsed", "Tree")
var select_button: Texture2D = get_theme_icon("select_option", "Tree")
var delete_button: Texture2D = get_theme_icon("indeterminate", "Tree")

var FILE_TREE : GDREFileTree = null
var RECOVER_WINDOW :Window = null
var VERSION_TEXT: Label = null
var SELECTED_PCK: LineEdit = null
var REPORT_DIALOG = null
var SELECT_PCK_DIALOG: FileDialog = null
var FILTER: LineEdit = null
var FILES_TEXT: Label = null
var PATCH_FILE_TREE: GDREFileTree = null
var PATCH_FILE_DIALOG: FileDialog = null
var PATCH_FILE_MAPPING_DIALOG: FileDialog = null
var PATCH_FOLDER_MAPPING_DIALOG: FileDialog = null
var MAP_SELECTED_ITEMS_BUTTON: Button = null

var DROP_FOLDERS_CONFIRMATION_DIALOG: Window = null
var DROP_FOLDERS_LIST_A: Tree = null
var DROP_FOLDERS_LIST_B: Tree = null
var DROP_FOLDERS_LABEL_A: Label = null
var DROP_FOLDERS_LABEL_B: Label = null
var button_clicked_item: TreeItem = null
# var isHiDPI = DisplayServer.screen_get_dpi() >= 240
var root: TreeItem = null
var userroot: TreeItem = null
var num_files:int = 0
var num_broken:int = 0
var num_malformed:int = 0
var _is_test:bool = false

signal patch_pck_done()
# file_map is from -> to, i.e. /path/to/file -> res://file
signal do_patch_pck(dest_pck: String, file_map: Dictionary[String, String]);

enum PatchTreeButton{
	SELECT,
	DELETE
}



func _on_patch_tree_item_edited():
	_validate()
	pass

func _on_patch_tree_button_clicked(item: TreeItem, _column: int, id: int, mouse_button_index: int):
	if (mouse_button_index != MOUSE_BUTTON_LEFT):
		return
	match id:
		PatchTreeButton.SELECT:
			if (not GDRESettings.is_pack_loaded()):
				popup_error_box("Load a pack first!", "Error")
				return
			button_clicked_item = item
			PATCH_FILE_MAPPING_DIALOG.current_file = item.get_text(0).get_file()
			PATCH_FILE_MAPPING_DIALOG.popup_centered()
		PatchTreeButton.DELETE:
			if (button_clicked_item == item):
				button_clicked_item = null
			PATCH_FILE_TREE.get_root().remove_child(item)
			_validate()
			
			pass
	pass

func _on_files_dropped(files: PackedStringArray):
	# get the current global mouse position
	var viewport = get_viewport()
	var mouse_pos = viewport.get_mouse_position()
	# check if it is inside the bounding box of the FileTree or the PatchFileTree
	if FILE_TREE.get_global_rect().has_point(mouse_pos):
		# convert the global mouse position to local position
		if (files.size() > 1):
			popup_error_box("You can only patch one PCK at a time", "Error")
			return
		_on_select_pck_dialog_file_selected(files[0])
	elif PATCH_FILE_TREE.get_global_rect().has_point(mouse_pos):
		_on_select_patch_files_dialog_files_selected(files)

func register_dropped_files():
	pass
	var window = get_viewport()
	var err = window.files_dropped.connect(self._on_files_dropped)
	if err != OK:
		print("Error: failed to connect window to files_dropped signal")
		print("Type: " + self.get_class())
		print("name: " + str(self.get_name()))



func _on_select_patch_folder_mapping_dialog_canceled() -> void:
	_tmp_selected_files.clear()

func _on_select_patch_folder_mapping_dialog_dir_selected(dir: String) -> void:
	# dir is the absolute path to the selected directory
	# _tmp_selected_files is a list of files that were dropped
	for item in _tmp_selected_files:
		item.set_text(1, dir.path_join(item.get_text(0).get_file()))
	_tmp_selected_files.clear()

var _tmp_selected_files: Array = []


func map_all_to_folder(selected_items: Array):
	if (not GDRESettings.is_pack_loaded()):
		popup_error_box("Load a pack first!", "Error")
		return
	if (selected_items.is_empty()):
		popup_error_box("Select items to map!", "Error")
		return
	_tmp_selected_files.clear()
	_tmp_selected_files = selected_items
	PATCH_FOLDER_MAPPING_DIALOG.popup_centered()

# MUST CALL set_root_window() first!!!
# Called when the node enters the scene tree for the first time.
func _ready():
	FILE_TREE =      $Control/FileTree
	VERSION_TEXT =   $Control/VersionText
	SELECTED_PCK = $Control/SelectedPck
	SELECT_PCK_DIALOG = $SelectPckDialog
	FILTER = $Control/Filter
	FILES_TEXT = $Control/FilesText
	PATCH_FILE_TREE = $Control/PatchFileTree
	PATCH_FILE_DIALOG = $SelectPatchFilesDialog
	PATCH_FILE_MAPPING_DIALOG = $SelectPatchMappingDialog
	PATCH_FOLDER_MAPPING_DIALOG = $SelectPatchFolderMappingDialog
	MAP_SELECTED_ITEMS_BUTTON = $Control/PatchButtonHBox/MapButton
	DROP_FOLDERS_CONFIRMATION_DIALOG = $DropFoldersConfirmation
	DROP_FOLDERS_LIST_A = $DropFoldersConfirmation/Control/ItemListA
	DROP_FOLDERS_LIST_B = $DropFoldersConfirmation/Control/ItemListB
	DROP_FOLDERS_LABEL_A = $DropFoldersConfirmation/Control/LabelA
	DROP_FOLDERS_LABEL_B = $DropFoldersConfirmation/Control/LabelB

	DROP_FOLDERS_LIST_A.set_column_title(0, "File")
	DROP_FOLDERS_LIST_A.set_column_title(1, "Mapping")
	DROP_FOLDERS_LIST_B.set_column_title(0, "File")
	DROP_FOLDERS_LIST_B.set_column_title(1, "Mapping")

	if isHiDPI:
		# get_viewport().size *= 2.0
		# get_viewport().content_scale_factor = 2.0
		#ThemeDB.fallback_base_scale = 2.0
		self.content_scale_factor = 2.0
		self.size *= 2.0
	# This is a hack to get around not being able to open multiple scenes
	# unless they're attached to windows
	# The children are not already in the window for ease of GUI creation
	clear()
	PATCH_FILE_TREE.connect("item_edited", self._on_patch_tree_item_edited)
	PATCH_FILE_TREE.connect("button_clicked", self._on_patch_tree_button_clicked)
	PATCH_FILE_TREE.set_column_title(0, "File")
	PATCH_FILE_TREE.set_column_title(1, "Mapping")
	PATCH_FILE_TREE.set_column_custom_minimum_width(1, 0)
	PATCH_FILE_TREE.set_column_expand(1, true)
	PATCH_FILE_TREE.add_custom_right_click_item("Map to Folder...", self.map_all_to_folder)
	register_dropped_files()
	_validate()
	# TODO: remove this
	# var test_bin = "/Users/nikita/Workspace/godot-ws/godot-test-bins/demo_platformer.pck"
	# _on_select_pck_dialog_file_selected(test_bin)


func add_project(paths: PackedStringArray) -> int:
	if (GDRESettings.is_pack_loaded()):
		GDRESettings.unload_project()
		FILE_TREE._clear()
	var err = GDRESettings.load_project(paths, true)
	if (err != OK):
		popup_error_box("Error: failed to open " + str(paths), "Error")
		return err
	var type = GDRESettings.get_pack_type()
	if (type != PackInfo.PCK and type != PackInfo.EXE):
		GDRESettings.unload_project()
		clear_selected_pack()
		popup_error_box("Error: You can only use this to patch PCKs or embedded PCKs", "Error")
		return err
	# need to do this to force it to get a new DirAccess
	PATCH_FILE_MAPPING_DIALOG.set_access(FileDialog.ACCESS_FILESYSTEM)
	PATCH_FILE_MAPPING_DIALOG.current_dir = ""
	PATCH_FILE_MAPPING_DIALOG.set_access(FileDialog.ACCESS_RESOURCES)
	PATCH_FILE_MAPPING_DIALOG.current_dir = "res://"
	PATCH_FOLDER_MAPPING_DIALOG.set_access(FileDialog.ACCESS_FILESYSTEM)
	PATCH_FOLDER_MAPPING_DIALOG.current_dir = ""
	PATCH_FOLDER_MAPPING_DIALOG.set_access(FileDialog.ACCESS_RESOURCES)
	PATCH_FOLDER_MAPPING_DIALOG.current_dir = "res://"
	VERSION_TEXT.text = GDRESettings.get_version_string()
	var arr: Array = GDRESettings.get_file_info_array()
	FILES_TEXT.text = str(arr.size())
	FILE_TREE.add_files_from_packed_infos(arr, true)
	return OK

func clear_patch_files():
	PATCH_FILE_TREE._clear()

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta):
	pass

func _exit_tree():
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()

func clear_selected_pack():
	SELECTED_PCK.text = ""
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()



func add_patch_files(map: Dictionary[String, String]) -> void:
	if (not PATCH_FILE_TREE.get_root()):
		PATCH_FILE_TREE.create_item()
	var root = PATCH_FILE_TREE.get_root()
	for key in map.keys():
		if not map[key].is_empty() and map[key].is_relative_path():
			map[key] = "res://" + map[key]
		var item = PATCH_FILE_TREE.create_file_item(root, key, key, file_icon, -1, "", map[key])
		item.set_cell_mode(0, TreeItem.CELL_MODE_STRING)
		item.set_cell_mode(1, TreeItem.CELL_MODE_STRING)
		item.set_text_direction(0, Control.TextDirection.TEXT_DIRECTION_RTL)
		item.set_structured_text_bidi_override(0, TextServer.STRUCTURED_TEXT_FILE)
		item.set_text(0, key)
		item.set_text(1, map[key])
		item.set_icon(0, file_icon)
		item.set_icon(1, file_broken)
		item.add_button(1, select_button, PatchTreeButton.SELECT)
		item.add_button(1, delete_button, PatchTreeButton.DELETE)
		item.set_editable(0, false)
		item.set_editable(1, true)
	_validate()

func _on_select_pck_button_pressed() -> void:
	SELECT_PCK_DIALOG.popup_centered()

func _on_select_pck_dialog_file_selected(path: String) -> void:
	SELECTED_PCK.text = path
	add_project([path])
	_validate()

func _on_filter_text_changed(new_text: String) -> void:
	FILE_TREE.filter(new_text)
	pass 

func _on_check_all_pressed() -> void:
	FILE_TREE.check_all_shown(true)


func _on_uncheck_all_pressed() -> void:
	FILE_TREE.check_all_shown(false)

func _on_select_patch_files_dialog_files_selected(paths: PackedStringArray) -> void:
	var map: Dictionary[String, String] = {}
	var folders: PackedStringArray = []
	var files: PackedStringArray = []
	for path in paths:
		if DirAccess.dir_exists_absolute(path):
			folders.append(path)
		else:
			files.append(path)
		map[path] = "res://" + path.get_file()
	if folders.size() > 0:
		_on_patch_folders_dropped(folders, files)
	else:
		add_patch_files(map)

func _on_select_patch_files_pressed() -> void:
	PATCH_FILE_DIALOG.popup_centered()

func _on_select_patch_mapping_dialog_file_selected(path: String) -> void:
	if button_clicked_item:
		button_clicked_item.set_text(1, path)
		button_clicked_item = null
		_validate()



func _on_patch_clear_button_pressed() -> void:
	clear_patch_files()
	_validate()

func add_error_to_item(item: TreeItem, message) -> void:
	item.set_icon(1, file_broken)
	var err_msg = item.get_tooltip_text(1)
	if not err_msg.is_empty():
		if err_msg.contains(message):
			return
		err_msg += "\nERROR: " + err_msg
	else:
		err_msg = "ERROR: " + message
	item.set_tooltip_text(1, err_msg)

func clear_error_from_item(item: TreeItem) -> void:
	item.set_icon(1, file_ok)
	item.set_tooltip_text(1, String())

func _validate_map_button():
	var pack_loaded = GDRESettings.is_pack_loaded()
	for item in PATCH_FILE_TREE.get_root().get_children():
		item.set_button_disabled(1, PatchTreeButton.SELECT, not pack_loaded)
	if (!pack_loaded or PATCH_FILE_TREE.get_highlighted_items().is_empty()):
		MAP_SELECTED_ITEMS_BUTTON.disabled = true
	else:
		MAP_SELECTED_ITEMS_BUTTON.disabled = false

func _validate():
	# if (not PATCH_FILE_TREE.get_root() or PATCH_FILE_TREE.get_root().get_children().size() == 0):
	# 	self.get_ok_button().disabled = true
	# 	return false
	var reverse_map: Dictionary[String, Array] = {}
	var item = PATCH_FILE_TREE.get_root().get_first_child()
	var error_messages:PackedStringArray = []
	var pack_loaded = GDRESettings.is_pack_loaded()
	while (item):
		var messages: PackedStringArray = []
		if (item.get_text(1).is_empty()):
			messages.append("Item has empty mapping")
		elif pack_loaded and DirAccess.dir_exists_absolute(item.get_text(1)):
			messages.append("Item is mapped to a directory in the source pack")
		if reverse_map.has(item.get_text(1)):
			messages.append("Item has duplicate mapping")
			for dupe_item in reverse_map[item.get_text(1)]:
				add_error_to_item(dupe_item, "Item has duplicate mapping")
			reverse_map[item.get_text(1)].append(item)
		else:
			reverse_map[item.get_text(1)] = [item]
		if not messages.is_empty():
			add_error_to_item(item, "\n".join(messages))
			for messsage in messages:
				if not error_messages.has(messsage):
					error_messages.append(messsage)
		else:
			clear_error_from_item(item)
		item = item.get_next()

	_validate_map_button()

	if (!pack_loaded or not error_messages.is_empty()):
		self.get_ok_button().disabled = true
		return false

	self.get_ok_button().disabled = false
	return true

# override GDREChildDialog

func show_win():
	clear()
	super.show_win()

func clear():
	SELECTED_PCK.clear()
	FILE_TREE._clear()
	clear_patch_files()

func close():
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()
	hide_win()

func cancelled():
	close()

func confirm():
	$SavePckDialog.popup_centered()
	
func _on_save_pck_dialog_file_selected(path: String) -> void:
	$SavePckDialog.hide()
	if (not _validate()):
		print("Validation failed")
		close()
	var file_map: Dictionary[String, String] = {}
	var pck_files = FILE_TREE.get_checked_files()

	var patch_files = PATCH_FILE_TREE.get_root().get_children()
	var reverse_map = {}
	for item in patch_files:
		file_map[item.get_text(0)] = item.get_text(1)
		reverse_map[item.get_text(1)] = true
	for file in pck_files:
		if not (reverse_map.has(file) or reverse_map.has(file.trim_prefix("res://"))):
			file_map[file] = file
	emit_signal("do_patch_pck", path, file_map)


func add_item_to_drop_list(list: Tree, file: String, file_path: String, mapping: String) -> void:
	var item = list.create_item()
	item.set_text(0, file)
	item.set_icon(0, file_icon)
	item.set_text(1, mapping)
	item.set_metadata(0, file_path)
	item.set_editable(0, false)
	item.set_editable(1, false)

func _on_patch_folders_dropped(folder_paths: PackedStringArray, other_file_paths: PackedStringArray):
	DROP_FOLDERS_LIST_A.clear()
	DROP_FOLDERS_LIST_B.clear()
	# invisible roots for the lists
	DROP_FOLDERS_LIST_A.create_item()
	DROP_FOLDERS_LIST_B.create_item()
	
	var error_messages: PackedStringArray = []
	var empty_folders: PackedStringArray = []
	var added_files = false
	for path in folder_paths:
		var files = Glob.rglob(path.path_join("**/*"), true)
		if (files.is_empty()):
			empty_folders.append(path)
			continue
		# patch list A is for relative to; i.e. /path/to/folder/file.png -> res://file.png
		# patch list B is for relative from; i.e. /path/to/folder/file.png -> res://folder/file.png
		var folder = path.get_file()
		for file_path in files:
			if GDREGlobals.banned_files.has(file_path.get_file()):
				continue
			added_files = true
			var file = file_path.trim_prefix(path).trim_prefix("/")
			var path_a = "res://" + file
			var path_b = "res://" + folder.path_join(file)
			add_item_to_drop_list(DROP_FOLDERS_LIST_A, file, file_path, path_a)
			add_item_to_drop_list(DROP_FOLDERS_LIST_B, file, file_path, path_b)

	for path in other_file_paths:
		if GDREGlobals.banned_files.has(path.get_file()):
			continue
		added_files = true
		var file = path.get_file()
		var mapping = "res://" + file
		add_item_to_drop_list(DROP_FOLDERS_LIST_A, file, path, mapping)
		add_item_to_drop_list(DROP_FOLDERS_LIST_B, file, path, mapping)
	

	# show the confirmation dialog
	if added_files:
		DROP_FOLDERS_CONFIRMATION_DIALOG.popup_centered()
		DROP_FOLDERS_CONFIRMATION_DIALOG.grab_focus()
	if not empty_folders.is_empty():
		for path in empty_folders:
			error_messages.append(path + ": Folder is empty")
		popup_error_box("\n".join(error_messages), "Error")

func _on_drop_folders_confirmation_close_requested() -> void:
	DROP_FOLDERS_CONFIRMATION_DIALOG.hide()

func get_drop_folders_list_map(list: Tree) -> Dictionary[String, String]:
	var item = list.get_root().get_first_child()
	var map: Dictionary[String, String] = {}
	while (item):
		map[item.get_metadata(0)] = item.get_text(1)
		item = item.get_next()
	return map

func _on_select_a_pressed() -> void:
	DROP_FOLDERS_CONFIRMATION_DIALOG.hide()
	# get all the items from the list
	add_patch_files(get_drop_folders_list_map(DROP_FOLDERS_LIST_A))

func _on_select_b_pressed() -> void:
	DROP_FOLDERS_CONFIRMATION_DIALOG.hide()
	add_patch_files(get_drop_folders_list_map(DROP_FOLDERS_LIST_B))


func _on_map_button_pressed() -> void:
	map_all_to_folder(PATCH_FILE_TREE.get_highlighted_items())


func _on_patch_file_tree_cell_selected() -> void:
	_validate_map_button()


func _on_patch_file_tree_item_selected() -> void:
	_validate_map_button() 


func _on_patch_file_tree_nothing_selected() -> void:
	_validate_map_button() 


func _on_patch_file_tree_item_edited() -> void:
	_validate()
