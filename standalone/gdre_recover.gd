class_name GDRERecoverDialog
extends GDREWindow


var FILE_TREE = null
var EXTRACT_ONLY : CheckBox = null
var RECOVER : CheckBox = null
var RECOVER_WINDOW :Window = null
var VERSION_TEXT: Label = null
var INFO_TEXT : Label = null
var DIRECTORY: LineEdit = null
var RESOURCE_PREVIEW: Control = null
var HSPLIT_CONTAINER: HSplitContainer = null
var SHOW_PREVIEW_BUTTON: Button = null
var DESKTOP_DIR = OS.get_system_dir(OS.SystemDir.SYSTEM_DIR_DESKTOP)

var isHiDPI = false #DisplayServer.screen_get_dpi() >= 240
var root: TreeItem = null
var userroot: TreeItem = null
var num_files:int = 0
var num_broken:int = 0
var num_malformed:int = 0
var _file_dialog: FileDialog = null

signal recovery_done()
signal recovery_confirmed(files_to_extract: PackedStringArray, output_dir: String, extract_only: bool)

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
	set_process_input(true)
	var safe_area: Rect2i = DisplayServer.get_display_safe_area()
	var new_size = Vector2(safe_area.size.x - 100, safe_area.size.y - 100)
	self.size = new_size
	var center = (safe_area.position + self.size / 2)
	self.set_position(center)
	SHOW_PREVIEW_BUTTON.set_pressed_no_signal(true)
	SHOW_PREVIEW_BUTTON.toggled.emit(true)
	set_exclusive(true)
	self.popup_centered()



func extract_file(file: String, output_dir: String, dir_structure: DirStructure, rel_base: String) -> String:
	var bytes = FileAccess.get_file_as_bytes(file)
	if bytes.is_empty():
		return "Failed to read file: " + file
	else:
		var file_path = get_output_file_name(file, output_dir, dir_structure, "", rel_base)
		GDRECommon.ensure_dir(file_path.get_base_dir())
		var f = FileAccess.open(file_path, FileAccess.WRITE)
		if f:
			f.store_buffer(bytes)
			f.close()
		else:
			return "Failed to open file for writing: " + file_path
	return ""


var prev_items: Array = []
var REL_BASE_DIR = "res://"

func _get_all_files(files: PackedStringArray) -> PackedStringArray:
	var new_files: Dictionary = {}
	for file in files:
		if DirAccess.dir_exists_absolute(file):
			var new_arr = GDRECommon.get_recursive_dir_list(file, [], true)
			for new_file in new_arr:
				new_files[new_file] = true
		else:
			new_files[file] = true
	return PackedStringArray(new_files.keys())

const DIR_STRUCTURE_OPTION_NAME = "Directory Structure"
const EXPORT_SCENE_OPTION_NAME = "Export Scenes as"

enum DirStructure {
	FLAT,
	RELATIVE_HIERARCHICAL,
	ABSOLUTE_HIERARCHICAL,
}

enum ExportSceneType {
	AUTO,
	TSCN,
	GLB,
	GLTF
}

const DIR_STRUCTURE_NAMES: PackedStringArray = [
	"Flat",
	"Relative Hierarchical",
	"Absolute Hierarchical",
]

const EXPORT_SCENE_TYPE_NAMES: PackedStringArray = [
	"Auto",
	"tscn",
	"GLB",
	"GLTF",
]

func get_output_file_name(src: String, output_folder: String, dir_structure_option: DirStructure, new_ext: String = "", rel_base: String = "") -> String:
	var new_name = ""
	if dir_structure_option == DirStructure.FLAT:
		new_name = output_folder.path_join(src.get_file())
	elif dir_structure_option == DirStructure.ABSOLUTE_HIERARCHICAL:
		new_name = output_folder.path_join(src.trim_prefix("res://").replace("user://", ".user/"))
	elif dir_structure_option == DirStructure.RELATIVE_HIERARCHICAL:
		new_name = output_folder.path_join(src.trim_prefix(rel_base))
	if !new_ext.is_empty():
		new_name = new_name.get_basename() + "." + new_ext
	return new_name


func _export_scene(file: String, output_dir: String, dir_structure: DirStructure, rel_base: String, export_type: ExportSceneType) -> ExportReport:
	var source_file = file
	var iinfo = GDRESettings.get_import_info_by_dest(file)
	if iinfo:
		source_file = iinfo.source_file

	var ext = source_file.get_extension().to_lower()

	if export_type == ExportSceneType.GLB:
		ext = "glb"
	elif export_type == ExportSceneType.GLTF:
		ext = "gltf"
	elif export_type == ExportSceneType.TSCN:
		ext = "tscn"
	else: # AUTO
		if not is_instance_valid(iinfo):
			ext = "tscn"


	var export_dest = get_output_file_name(source_file, output_dir, dir_structure, ext, rel_base)
	var report = SceneExporter.export_file_with_options(export_dest, file, {
		"Exporter/Scene/GLTF/replace_shader_materials": true,
	})
	#err == ERR_BUG || err == ERR_PRINTER_ON_FIRE || err == ERR_DATABASE_CANT_READ
	if (report.error == ERR_BUG or report.error == ERR_PRINTER_ON_FIRE or report.error == ERR_DATABASE_CANT_READ):
		report.error = OK
	return report


func get_log_error_string(errs: PackedStringArray) -> String:
	return "\n".join(GDRECommon.filter_error_backtraces(errs))

func convert_pcfg_to_text(path: String, output_dir: String) -> Array:
	var loader = ProjectConfigLoader.new()
	var ver_major = GDRESettings.get_ver_major()
	var ver_minor = GDRESettings.get_ver_minor()
	var text_file = "project.godot"
	var err = OK
	if path.get_file() == "engine.cfb":
		text_file = "engine.cfg"
	err = loader.load_cfb(path, ver_major, ver_minor)
	if err != OK:
		return [err, text_file]
	return [loader.save_cfb(output_dir, ver_major, ver_minor), text_file]

func _export_files(files: PackedStringArray, output_dir: String, dir_structure: DirStructure, rel_base: String, export_glb: ExportSceneType) -> PackedStringArray:
	var errs: PackedStringArray = []
	files = _get_all_files(files)

	for file in files:
		if DirAccess.dir_exists_absolute(file):
			continue

		if file.get_file() == "project.binary" || file.get_file() == "engine.cfb":
			var ret = convert_pcfg_to_text(file, output_dir)
			if ret[0] != OK:
				errs.append("Failed to convert project config: " + file + "\n" + GDRESettings.get_recent_error_string())
			continue

		GDRESettings.get_errors()
		var _ret: ImportInfo = GDRESettings.get_import_info_by_dest(file)
		var file_ext = file.get_extension().to_lower()
		if file_ext == "scn" or file_ext == "tscn" or (_ret and _ret.get_compat_type() == "PackedScene"):
			if export_glb != ExportSceneType.GLB and export_glb != ExportSceneType.GLTF and file_ext == "tscn":
				var src = file if not is_instance_valid(_ret) else _ret.source_file
				if src.get_extension().to_lower() == file_ext:
					# just extract the file
						var err = extract_file(file, output_dir, dir_structure, rel_base)
						if not err.is_empty():
							errs.append(err)
						continue
			var report: ExportReport = _export_scene(file, output_dir, dir_structure, rel_base, export_glb)
			if not report:
				errs.append("Failed to export resource: " + file)
			elif report.error != OK and report.error != ERR_PRINTER_ON_FIRE:
				if (report.error == ERR_SKIP):
					errs.append("Exporting cancelled: " + file + "\n" + report.message + "\n" + get_log_error_string(report.get_error_messages()))
					break
				errs.append("Failed to export resource: " + file + "\n" + report.message + "\n" + get_log_error_string(report.get_error_messages()))
		elif _ret:
			var iinfo: ImportInfo = ImportInfo.copy(_ret)
			iinfo.export_dest = get_output_file_name(iinfo.source_file, "res://", dir_structure, iinfo.source_file.get_extension().to_lower(), rel_base)
			var report: ExportReport = Exporter.export_resource(output_dir, iinfo)
			if not report:
				errs.append("Failed to export resource: " + file)
			elif report.error != OK and report.error != ERR_PRINTER_ON_FIRE:
				errs.append("Failed to export resource: " + file + "\n" + report.message + "\n" + get_log_error_string(report.get_error_messages()))
			else:
				var actual_output_path = report.saved_path
				var rel_path = actual_output_path.simplify_path().trim_prefix(output_dir).trim_prefix("/")
				if rel_path.begins_with(".assets"):
					var new_path = output_dir.path_join(rel_path.trim_prefix(".assets"))
					GDRECommon.ensure_dir(new_path.get_base_dir())
					DirAccess.rename_absolute(actual_output_path, new_path)

		elif Exporter.is_exportable_resource(file):
			var ext = Exporter.get_default_export_extension(file)
			var dest_file = get_output_file_name(file, output_dir, dir_structure, ext, rel_base)
			var err = Exporter.export_file(dest_file, file)
			if err:
				errs.append("Failed to export file: " + file + "\n" + GDRESettings.get_recent_error_string())
		elif ResourceFormatLoaderCompatBinary.is_binary_resource(file):
			var new_ext = "tres"
			if file.get_extension().to_lower() == "scn":
				new_ext = "tscn"
			var err = ResourceCompatLoader.to_text(file, get_output_file_name(file, output_dir, dir_structure, new_ext, rel_base))
			if err:
				errs.append("Failed to export file: " + file + "\n" + GDRESettings.get_recent_error_string())
		else:
			# extract the file
			var err = extract_file(file, output_dir, dir_structure, rel_base)
			if not err.is_empty():
				errs.append(err)
	return errs

func _on_export_resources_confirmed(output_dir: String):
	# Export goes very slow if the preview is visible and something like a 3D scene is being rendered;
	# We toggle it off during the export and then turn it back on after it's done
	var export_preview_visible = %GdreResourcePreview.is_main_view_visible()
	if export_preview_visible:
		%GdreResourcePreview.set_main_view_visible(false)
	self.call_on_next_process(self.call_on_next_process.bind(self._do_export.bind(output_dir, export_preview_visible)))


func _show_error_or_success(errs: PackedStringArray, success_message: String, output_dir: String):
	if errs.size() > 0:
		popup_error_box("\n".join(errs), "Error")
	else:
		popup_confirm_box(success_message, "Success", func(): OS.shell_open(GDRECommon.path_to_uri(output_dir)), func(): pass, "Open Folder", "OK")

func _do_export(output_dir: String, export_preview_visible: bool):
	var files: PackedStringArray = []
	var errs: PackedStringArray = []
	for item: TreeItem in prev_items:
		files.append(FILE_TREE._get_path(item))
	prev_items = []
	var rel_base = REL_BASE_DIR
	REL_BASE_DIR = "res://"
	var options = %ExportResDirDialog.get_selected_options()
	var dir_structure = options.get(DIR_STRUCTURE_OPTION_NAME, DirStructure.RELATIVE_HIERARCHICAL)
	var export_glb: ExportSceneType = options.get(EXPORT_SCENE_OPTION_NAME, int(ExportSceneType.AUTO))

	errs = _export_files(files, output_dir, dir_structure, rel_base, export_glb)
	if export_preview_visible:
		%GdreResourcePreview.set_main_view_visible(true)

	self.call_on_next_process(self.call_on_next_process.bind(self._show_error_or_success.bind(errs, "Successfully exported resources", output_dir)))




func _on_extract_resources_dir_selected(path: String):
	self.call_on_next_process(self.call_on_next_process.bind(self._do_extract.bind(path)))

func _do_extract(path: String):
	var options = %ExtractResDirDialog.get_selected_options()
	var dir_structure = options.get(DIR_STRUCTURE_OPTION_NAME, DirStructure.RELATIVE_HIERARCHICAL)
	var files: PackedStringArray = []
	var errs: PackedStringArray = []
	for item in prev_items:
		files.append(FILE_TREE._get_path(item))
	print("FILES: ", files)
	prev_items = []
	var rel_base = REL_BASE_DIR
	REL_BASE_DIR = "res://"
	files = _get_all_files(files)
	for file in files:
		if DirAccess.dir_exists_absolute(file):
			continue
		var err = extract_file(file, path, dir_structure, rel_base)
		if not err.is_empty():
			errs.append(err)
	self.call_on_next_process(self.call_on_next_process.bind(self._show_error_or_success.bind(errs, "Successfully extracted resources", path)))

func _determine_rel_base_dir(selected_items: Array) -> String:
	var base_dirs: Dictionary = {}
	if selected_items.size() == 0:
		return "res://"
	for item in selected_items:
		var path = FILE_TREE._get_path(item)
		var base_dir = path.get_base_dir()
		#short circuit if the path is in the root
		if base_dir == "res://":
			return "res://"
		base_dirs[base_dir] = true
	if base_dirs.size() > 1:
		var keys = base_dirs.keys()
		# get the shortest path
		keys.sort_custom(func(a, b): return a.length() < b.length())
		var shortest_path = keys[0]
		for base_dir in keys:
			if base_dir == shortest_path:
				continue
			if not base_dir.begins_with(shortest_path):
				return "res://"
		return shortest_path
	return base_dirs.keys()[0]


func _determine_default_dir_structure(selected_items: Array) -> Array:
	var base_dirs: Dictionary = {}
	var had_folder = false
	var rel_base_dir = "res://"
	for item in selected_items:
		if FILE_TREE.item_is_folder(item):
			had_folder = true
			base_dirs[FILE_TREE._get_path(item)] = true
		else:
			var path = FILE_TREE._get_path(item)
			var base_dir = path.get_base_dir()
			base_dirs[base_dir] = true
	if base_dirs.size() > 1 or had_folder:
		rel_base_dir = _determine_rel_base_dir(selected_items)
		if rel_base_dir.is_empty() or rel_base_dir == "res://":
			return [DirStructure.ABSOLUTE_HIERARCHICAL, rel_base_dir]
		else:
			return [DirStructure.RELATIVE_HIERARCHICAL, rel_base_dir]
	return [DirStructure.FLAT, ""]


func _on_export_resources_pressed(_selected_items):
	prev_items = _selected_items
	var ret = _determine_default_dir_structure(_selected_items)
	var default_dir_structure = ret[0]
	REL_BASE_DIR = ret[1]
	open_export_resources_dir_dialog(default_dir_structure)

func _on_extract_resources_pressed(_selected_items):
	prev_items = _selected_items
	var ret = _determine_default_dir_structure(_selected_items)
	var default_dir_structure = ret[0]
	REL_BASE_DIR = ret[1]
	open_extract_resources_dir_dialog(default_dir_structure)


func _set_current_dir_if_default(file_dialog: FileDialog, dir: String):
	var cur_dir = file_dialog.get_current_dir();
	if cur_dir.is_empty() || cur_dir == GDRESettings.get_cwd():
		if (!dir.is_empty()):
			file_dialog.set_current_dir(dir)


func _set_file_dialog_options(file_dialog: FileDialog, default_dir_structure: DirStructure, include_scene: bool):
	var options = file_dialog.get_selected_options()
	file_dialog.set_option_count(0)
	file_dialog.add_option(DIR_STRUCTURE_OPTION_NAME, DIR_STRUCTURE_NAMES, int(default_dir_structure))
	if not include_scene:
		return
	var include_glb = GDRESettings.get_ver_major() >= SceneExporter.get_minimum_godot_ver_supported()
	var scene_default = options.get(EXPORT_SCENE_OPTION_NAME, int(ExportSceneType.AUTO))
	#file_dialog.set_option_default(0, int(default_dir_structure))
	var glb_opts = EXPORT_SCENE_TYPE_NAMES.duplicate()
	if not include_glb:
		glb_opts.remove_at(int(ExportSceneType.GLTF))
		glb_opts.remove_at(int(ExportSceneType.GLB))
	file_dialog.add_option(EXPORT_SCENE_OPTION_NAME, glb_opts, scene_default)

func open_export_resources_dir_dialog(default_dir_structure: DirStructure):
	# remove all the current options
	_set_file_dialog_options(%ExportResDirDialog, default_dir_structure, true)
	_set_current_dir_if_default(%ExportResDirDialog, DIRECTORY.text.get_base_dir())
	var _name = %ExportResDirDialog.get_option_name(0)
	open_subwindow(%ExportResDirDialog)

func open_extract_resources_dir_dialog(default_dir_structure: DirStructure):
	var file_dialog: FileDialog = %ExtractResDirDialog
	_set_file_dialog_options(file_dialog, default_dir_structure, false)
	var _name = file_dialog.get_option_name(0)
	_set_current_dir_if_default(file_dialog, DIRECTORY.text.get_base_dir())
	open_subwindow(file_dialog)

func setup_export_resources_dir_dialog():
	%ExportResDirDialog.connect("dir_selected", self._on_export_resources_confirmed)

func setup_extract_resources_dir_dialog():
	%ExtractResDirDialog.connect("dir_selected", self._on_extract_resources_dir_selected)



func _ready():
	FILE_TREE =      %FileTree
	EXTRACT_ONLY =   %ExtractOnly
	RECOVER =        %FullRecovery
	VERSION_TEXT =   %VersionText
	INFO_TEXT =      %InfoText
	RECOVER_WINDOW = self #$Control/RecoverWindow
	DIRECTORY = %Directory
	RESOURCE_PREVIEW = %GdreResourcePreview
	HSPLIT_CONTAINER = %HSplitContainer
	SHOW_PREVIEW_BUTTON = %ShowResourcePreview
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
	setup_export_resources_dir_dialog()
	setup_extract_resources_dir_dialog()
	DIRECTORY.text = DESKTOP_DIR
	FILE_TREE.add_custom_right_click_item("Extract Selected...", self._on_extract_resources_pressed)
	FILE_TREE.add_custom_right_click_item("Export Selected...", self._on_export_resources_pressed)
	# load_test()

func add_project(paths: PackedStringArray) -> int:
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()
	clear()
	var err = GDRESettings.load_project(paths)
	if (err != OK):
		return err
	var pckdump = PckDumper.new()
	var skipped = false
	err = pckdump.check_md5_all_files()
	if ERR_SKIP == err:
		skipped = true
		err = OK
	VERSION_TEXT.text = GDRESettings.get_version_string()
	var arr: Array = GDRESettings.get_file_info_array()
	FILE_TREE.add_files_from_packed_infos(arr, skipped, GDRESettings.had_encryption_error())
	INFO_TEXT.text = "Total files: " + String.num_int64(FILE_TREE.num_files)# +
	if FILE_TREE.num_broken > 0 or FILE_TREE.num_malformed > 0:
		INFO_TEXT.text += "   Broken files: " + String.num_int64(FILE_TREE.num_broken) + "    Malformed paths: " + String.num_int64(FILE_TREE.num_malformed)
	DIRECTORY.text = DESKTOP_DIR.path_join(GDRESettings.get_pack_path().get_file().get_basename())

	if GodotMonoDecompWrapper.is_godot_mono_decomp_enabled() and GDRESettings.project_requires_dotnet_assembly():
		# don't show the assembly picker if the assembly is loaded from a temp directory
		# It's either loaded from an APK or the PCK and it's not pickable by the user
		if (not GDRESettings.get_temp_dotnet_assembly_dir().is_empty() and GDRESettings.has_loaded_dotnet_assembly()):
			%AssemblyPickerHBox.visible = false
		else:
			%Assembly.text = GDRESettings.get_dotnet_assembly_path()
			changed_assembly = false
			set_assembly_good(GDRESettings.has_loaded_dotnet_assembly())
			%AssemblyPickerHBox.visible = true
	else:
		%AssemblyPickerHBox.visible = false
	return OK

func load_test():
	#const path = "/Users/nikita/Workspace/godot-ws/godot-test-bins/satryn.apk"
	const path = '/Users/nikita/Library/Application Support/CrossOver/Bottles/Steam/drive_c/Program Files (x86)/Steam/steamapps/common/Psychopomp Gold/Psychopomp GOLD.exe'
	add_project([path])
	show_win()

func cancel_extract():
	pass


func hide_win():
	self.hide()


func open_subwindow(window: Window):
	window.set_transient(true)
	window.set_exclusive(true)
	window.popup_centered()

func open_extract_dir_dialog():
	_file_dialog.set_current_dir(DIRECTORY.text.get_base_dir())
	open_subwindow(_file_dialog)

func _dir_selected(path: String):
	DIRECTORY.text = path

func setup_extract_dir_dialog():
	_file_dialog = $ExtractDirDialog
	_file_dialog.connect("dir_selected", self._dir_selected)


func _on_filter_text_changed(new_text: String) -> void:
	FILE_TREE.filter(new_text)

func _on_check_all_pressed() -> void:
	FILE_TREE.check_all_shown(true)


func _on_uncheck_all_pressed() -> void:
	FILE_TREE.check_all_shown(false)


func close():
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()
	RESOURCE_PREVIEW.reset()
	hide_win()
	emit_signal("recovery_done")

func clear():
	FILE_TREE._clear()

func _go():
	hide_win()
	emit_signal("recovery_confirmed", FILE_TREE.get_checked_files(), DIRECTORY.text, EXTRACT_ONLY.is_pressed())


func confirm():
	if changed_assembly:
		handle_assembly_change()
	RESOURCE_PREVIEW.reset()
	if (not EXTRACT_ONLY.is_pressed() and GDREConfig.get_setting("ask_for_download", true)):
		for file in FILE_TREE.get_checked_files():
			var ext = file.get_extension().to_lower()
			if ext == "gdextension" or ext == "gdnlib":
				%DownloadConfirmDialog.popup_centered()
				return
	_go()


func cancelled():
	close()

func _on_directory_button_pressed() -> void:
	open_extract_dir_dialog()

func _on_file_tree_item_selected() -> void:
	if not RESOURCE_PREVIEW.is_visible_in_tree():
		return
	var item = FILE_TREE.get_selected()
	if item:
		var path = item.get_metadata(0)
		if not path.is_empty():
			RESOURCE_PREVIEW.load_resource(path)


func _on_show_resource_preview_toggled(toggled_on: bool) -> void:
	if toggled_on:
		RESOURCE_PREVIEW.visible = true
		# get the current size of the window
		# set the split offset to 50% of the window size
		HSPLIT_CONTAINER.set_split_offset(self.size.x / 2)
		SHOW_PREVIEW_BUTTON.text = "Hide Resource Preview"
		_on_file_tree_item_selected()
	else:
		RESOURCE_PREVIEW.visible = false
		HSPLIT_CONTAINER.set_split_offset(0)
		SHOW_PREVIEW_BUTTON.text = "Show Resource Preview..."
		RESOURCE_PREVIEW.reset()


func _on_download_confirm_dialog_canceled() -> void:
	GDREConfig.set_setting("ask_for_download", not %DontAskAgainCheck.is_pressed())
	GDREConfig.set_setting("download_plugins", false)
	_go()

func _on_download_confirm_dialog_confirmed() -> void:
	GDREConfig.set_setting("ask_for_download", not %DontAskAgainCheck.is_pressed())
	GDREConfig.set_setting("download_plugins", true)
	_go()

func set_assembly_good(good: bool) -> void:
	if good:
		%AssemblyLabel.text = "C# Assembly"
		%AssemblyLabel.tooltip_text = ""
	else:
		%AssemblyLabel.text = "C# Assembly ⚠️"
		if FileAccess.file_exists(%Assembly.text):
			%AssemblyLabel.tooltip_text = "File is not a valid .NET IL assembly"
		else:
			%AssemblyLabel.tooltip_text = "File does not exist"


func _on_assembly_button_pressed() -> void:
	var assembly_name = GDRESettings.get_project_dotnet_assembly_name()
	var filter = "*.dll"
	if !assembly_name.is_empty():
		filter = assembly_name + ".dll"
	var assembly_dir = %Assembly.text.get_base_dir()
	if assembly_dir.is_empty():
		assembly_dir = GDRESettings.get_pack_path().get_base_dir()
	%AssemblyPickerDialog.current_dir = assembly_dir
	%AssemblyPickerDialog.filters = PackedStringArray([filter])

	open_subwindow(%AssemblyPickerDialog)

var changed_assembly = false

func handle_assembly_change() -> void:
	changed_assembly = false
	GDRESettings.set_dotnet_assembly_path(%Assembly.text)
	set_assembly_good(GDRESettings.has_loaded_dotnet_assembly())

func _on_assembly_picker_dialog_file_selected(path:  String) -> void:
	%Assembly.text = path
	handle_assembly_change()

func _on_assembly_text_submitted(_new_text:  String) -> void:
	handle_assembly_change()

func _on_assembly_text_changed(_new_text:  String) -> void:
	changed_assembly = true
	# we don't want to handle this change yet, the user might be still typing, wait until the focus is lost

func _on_assembly_focus_exited() -> void:
	if changed_assembly:
		handle_assembly_change()


func _on_export_settings_button_pressed() -> void:
	%GDREConfigDialog.clear()
	%GDREConfigDialog.show()


func _on_gdre_config_dialog_config_changed(changed_settings: Dictionary[String, Variant]) -> void:
	GDRESettings.update_from_ephemeral_settings()


func _on_add_pcks_dialog_files_selected(paths: PackedStringArray) -> void:
	self.call_on_next_process(self.call_on_next_process.bind(self._reload_with.bind(paths)))

func _reload_with(paths: PackedStringArray):
	var curr_pcks = GDRESettings.get_pack_info_list()
	var new_paths: PackedStringArray = []
	for pck in curr_pcks:
		new_paths.append(pck.get_pack_file())
	var added_path = false
	for path in paths:
		if not new_paths.has(path):
			added_path = true
			new_paths.append(path)
	if not added_path:
		popup_error_box("Error: selected PCK(s) are already loaded")
		return
	GDRESettings.unload_project(true)
	var err = add_project(new_paths)
	if (err != OK):
		popup_error_box("Error: failed to open " + str(paths), "Error", self.close)
		return



func _on_add_pcks_button_pressed() -> void:
	var curr_pck = GDRESettings.get_pack_path()
	%AddPcksDialog.current_dir = curr_pck.get_base_dir()
	%AddPcksDialog.popup_centered()

func _on_extract_success_dialog_confirmed() -> void:
	pass # Replace with function body.
