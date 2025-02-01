class_name GDRERecoverDialog
extends GDREChildDialog


const gdre_export_report = preload("res://gdre_export_report.tscn")
const gdre_file_tree = preload("res://gdre_file_tree.gd")
var FILE_TREE : GDREFileTree = null
var EXTRACT_ONLY : CheckBox = null
var RECOVER : CheckBox = null
var RECOVER_WINDOW :Window = null
var VERSION_TEXT: Label = null
var INFO_TEXT : Label = null
var REPORT_DIALOG = null
var DIRECTORY: LineEdit = null
var RESOURCE_PREVIEW: Control = null
var HSPLIT_CONTAINER: HSplitContainer = null
var SHOW_PREVIEW_BUTTON: Button = null
var DESKTOP_DIR = OS.get_system_dir(OS.SystemDir.SYSTEM_DIR_DESKTOP)


# var isHiDPI = DisplayServer.screen_get_dpi() >= 240
var root: TreeItem = null
var userroot: TreeItem = null
var num_files:int = 0
var num_broken:int = 0
var num_malformed:int = 0
var _is_test:bool = false
var _file_dialog: FileDialog = null

signal recovery_done()

func _propagate_check(item: TreeItem, checked: bool):
	item.set_checked(0, checked)
	var it: TreeItem = item.get_first_child()
	while (it):
		_propagate_check(it, checked)
		it = it.get_next()

func _on_item_edited():
	var item = FILE_TREE.get_edited()
	var checked = item.is_checked(0)
	_propagate_check(item, checked)

func show_win():
	# get the screen size
	var safe_area: Rect2i = DisplayServer.get_display_safe_area()
	var new_size = Vector2(safe_area.size.x - 100, safe_area.size.y - 100)
	self.size = new_size
	var center = (safe_area.position + self.size / 2)
	self.set_position(center)
	SHOW_PREVIEW_BUTTON.set_pressed_no_signal(true)
	SHOW_PREVIEW_BUTTON.toggled.emit(true)
	self.popup_centered()

# MUST CALL set_root_window() first!!!
# Called when the node enters the scene tree for the first time.
func _ready():
	FILE_TREE =      $HSplitContainer/Control/FileTree
	EXTRACT_ONLY =   $HSplitContainer/Control/RadioButtons/ExtractOnly
	RECOVER =        $HSplitContainer/Control/RadioButtons/FullRecovery
	VERSION_TEXT =   $HSplitContainer/Control/VersionText
	INFO_TEXT =      $HSplitContainer/Control/InfoText
	RECOVER_WINDOW = self #$Control/RecoverWindow
	DIRECTORY = $HSplitContainer/Control/Directory
	RESOURCE_PREVIEW = $HSplitContainer/GdreResourcePreview
	HSPLIT_CONTAINER = $HSplitContainer
	SHOW_PREVIEW_BUTTON = $HSplitContainer/Control/ShowResourcePreview
	#if !_is_test:
		#assert(POPUP_PARENT_WINDOW)
	#else:
	POPUP_PARENT_WINDOW = get_window()
	var thing: Variant = 2
	var int_thing: int = int(thing)
	if isHiDPI:
		# get_viewport().size *= 2.0
		# get_viewport().content_scale_factor = 2.0
		#ThemeDB.fallback_base_scale = 2.0
		RECOVER_WINDOW.content_scale_factor = 2.0
		RECOVER_WINDOW.size *= 2.0

	clear()
	SHOW_PREVIEW_BUTTON.set_pressed_no_signal(true)
	SHOW_PREVIEW_BUTTON.toggled.emit(true)

	var file_list: Tree = FILE_TREE
	file_list.connect("item_edited", self._on_item_edited)
	setup_extract_dir_dialog()
	DIRECTORY.text = DESKTOP_DIR



	# load_test() 

func add_project(paths: PackedStringArray) -> int:
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()
	clear()
	var err = GDRESettings.load_project(paths)
	if (err != OK):
		var errors = (GDRESettings.get_errors())
		var error_msg = ""
		for error in errors:
			error_msg += error.strip_edges() + "\n"
		if error_msg.to_lower().contains("encrypt"):
			error_msg = "Incorrect encryption key. Please set the correct key and try again."
		popup_error_box("Error: failed to open " + str(paths) + ":\n" + error_msg, "Error")
		return err
	var pckdump = PckDumper.new()
	var skipped = false
	err = pckdump.check_md5_all_files()
	if ERR_SKIP == err:
		skipped = true
		err = OK
	VERSION_TEXT.text = GDRESettings.get_version_string()
	var arr: Array = GDRESettings.get_file_info_array()
	FILE_TREE.add_files_from_packed_infos(arr, skipped)
	INFO_TEXT.text = "Total files: " + String.num_int64(FILE_TREE.num_files)# + 
	if FILE_TREE.num_broken > 0 or FILE_TREE.num_malformed > 0:
		INFO_TEXT.text += "   Broken files: " + String.num_int64(FILE_TREE.num_broken) + "    Malformed paths: " + String.num_int64(FILE_TREE.num_malformed)
	DIRECTORY.text = DESKTOP_DIR.path_join(paths[0].get_file().get_basename())
	return OK

func load_test():
	#const path = "/Users/nikita/Workspace/godot-ws/godot-test-bins/satryn.apk"
	const path = '/Users/nikita/Library/Application Support/CrossOver/Bottles/Steam/drive_c/Program Files (x86)/Steam/steamapps/common/CRUEL/Cruel.pck'
	add_project([path])
	show_win()
	


func extract_and_recover(output_dir: String):
	GDRESettings.open_log_file(output_dir)
	var log_path = GDRESettings.get_log_file_path()
	var report_str = "Log file written to " + log_path
	report_str += "\nPlease include this file when reporting an issue!\n\n"
	var pck_dumper = PckDumper.new()
	var files_to_extract = FILE_TREE.get_checked_files()
	var err = pck_dumper.pck_dump_to_dir(output_dir, files_to_extract)
	if (err):
		popup_error_box("Error: Could not extract files!!", "Error")
		return
	# check if ExtractOnly is pressed
	if (EXTRACT_ONLY.is_pressed()):
		report_str += "Total files extracted: " + String.num(files_to_extract.size()) + "\n"
		popup_error_box(report_str, "Info")
		# GDRESettings.unload_pack()
		return
	# otherwise, continue to recover
	var import_exporter = ImportExporter.new()
	import_exporter.export_imports(output_dir, files_to_extract)
	var report = import_exporter.get_report()
	GDRESettings.close_log_file()
	REPORT_DIALOG = gdre_export_report.instantiate()
	REPORT_DIALOG.set_root_window(POPUP_PARENT_WINDOW)
	POPUP_PARENT_WINDOW.add_child(REPORT_DIALOG)
	#POPUP_PARENT_WINDOW.move_child(REPORT_DIALOG, self.get_index() -1)
	REPORT_DIALOG.add_report(report)
	REPORT_DIALOG.connect("report_done", self._report_done)
	#hide_win()
	REPORT_DIALOG.show_win()

func _report_done():
	if REPORT_DIALOG:
		REPORT_DIALOG.disconnect("report_done", self._report_done)
		REPORT_DIALOG.hide_win()
		POPUP_PARENT_WINDOW.remove_child(REPORT_DIALOG)
		REPORT_DIALOG = null
	else:
		print("REPORT DONE WITHOUT REPORT_DIALOG?!?!")
	close()

func cancel_extract():
	pass



var _last_path: String = ""
func _on_dialog_close():
	extract_and_recover(_last_path)

func open_extract_dir_dialog(path:String = ""):
	#var pck_path = path if !path.is_empty() else GDRESettings.get_pack_path().get_base_dir()
	
	_file_dialog.set_current_dir(DIRECTORY.text.get_base_dir())
	open_subwindow(_file_dialog)

func _dir_selected(path: String):
	DIRECTORY.text = path

func setup_extract_dir_dialog():
	_file_dialog = $ExtractDirDialog
	#_file_dialog = FileDialog.new()
	#_file_dialog.use_native_dialog = true
	#_file_dialog.set_access(FileDialog.ACCESS_FILESYSTEM)
	#_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	#_file_dialog.set_title("Select a directory to extract to")
	#POPUP_PARENT_WINDOW.add_child(_file_dialog)
	_file_dialog.connect("dir_selected", self._dir_selected)


func _on_filter_text_changed(new_text: String) -> void:
	FILE_TREE.filter(new_text)
	pass # Replace with function body.

func _on_check_all_pressed() -> void:
	FILE_TREE.check_all_shown(true)


func _on_uncheck_all_pressed() -> void:
	FILE_TREE.check_all_shown(false)


func close():
	var err = OK
	if GDRESettings.is_pack_loaded():
		err = GDRESettings.unload_project()
	RESOURCE_PREVIEW.reset()
	hide_win()
	emit_signal("recovery_done")

func clear():
	FILE_TREE._clear()

func confirm():
	RESOURCE_PREVIEW.reset()
	extract_and_recover(DIRECTORY.text)
	# pop open a file dialog to pick a directory
	

func cancelled():
	close()

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta):
	pass


func _on_directory_button_pressed() -> void:
	open_extract_dir_dialog() # Replace with function body.



func _on_file_tree_item_selected() -> void:
	if not RESOURCE_PREVIEW.is_visible_in_tree():
		return
	var item = FILE_TREE.get_selected()
	if item:
		var path = item.get_metadata(0)
		if not path.is_empty():
			RESOURCE_PREVIEW.load_resource(path)

# No need for this
# var _multi_selected_timer = null
# var _on_multi_selected_cooldown = false
# var _on_multi_selected_cooldown_time: float = 0.5
# func _on_multi_select_timeout():
# 	_on_multi_selected_cooldown = false
# 	_multi_selected_timer = null
# func _on_multi_selected(item, column, selected):
# 	if item and selected and not _on_multi_selected_cooldown and RESOURCE_PREVIEW.is_visible_in_tree() :
# 		var path = item.get_metadata(0)
# 		if not path.is_empty():
# 			RESOURCE_PREVIEW.load_resource(path)
# 			_multi_selected_timer = get_tree().create_timer(_on_multi_selected_cooldown_time)
# 			_multi_selected_timer.connect("timeout", self._on_multi_select_timeout)



func _on_show_resource_preview_toggled(toggled_on: bool) -> void:
	if toggled_on:
		RESOURCE_PREVIEW.visible = true
		# get the current size of the window
		# set the split offset to 66% of the window size
		HSPLIT_CONTAINER.set_split_offset(self.size.x * 0.50)
		SHOW_PREVIEW_BUTTON.text = "Hide Resource Preview"
		_on_file_tree_item_selected()
	else:
		RESOURCE_PREVIEW.visible = false
		HSPLIT_CONTAINER.set_split_offset(0)
		SHOW_PREVIEW_BUTTON.text = "Show Resource Preview..."
		RESOURCE_PREVIEW.reset()
