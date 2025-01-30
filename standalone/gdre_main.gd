extends GodotREEditorStandalone

var ver_major = 0
var ver_minor = 0
var scripts_only = false
var disable_multi_threading = false
var config: ConfigFile = null
var last_error = ""
var CONFIG_PATH = "user://gdre_settings.cfg"

# var isHiDPI = DisplayServer.screen_get_dpi() >= 240
var isHiDPI = false

var gdre_recover = preload("res://gdre_recover.tscn")
var gdre_new_pck = preload("res://gdre_new_pck.tscn")
var gdre_patch_pck = preload("res://gdre_patch_pck.tscn")
var RECOVERY_DIALOG: GDRERecoverDialog = null
var NEW_PCK_DIALOG: GDRENewPck = null
var PATCH_PCK_DIALOG: GDREPatchPCK = null
var ERROR_DIALOG: AcceptDialog = null
var _file_dialog: Window = null
var last_dir: String = ""
var REAL_ROOT_WINDOW = null

enum PckMenuID {
	NEW_PCK,
	PATCH_PCK
}

enum GDScriptMenuID {
	DECOMPILE,
	COMPILE
}

enum REToolsMenuID {
	RECOVER,
	SET_KEY,
	ABOUT,
	REPORT_BUG,
	QUIT
}

enum ResourcesMenuID {
	BIN_TO_TXT,
	TXT_TO_BIN,
	_SEP,
	TEXTURE_TO_PNG,
	OGGSTREAM_TO_OGG,
	MP3STREAM_TO_MP3,
	SAMPLE_TO_WAV,
}

func test_text_to_bin(txt_to_bin: String, output_dir: String):
	var importer:ImportExporter = ImportExporter.new()
	var dst_file = txt_to_bin.get_file().replace(".tscn", ".scn").replace(".tres", ".res")
	importer.convert_res_txt_2_bin(output_dir, txt_to_bin, dst_file)
	importer.convert_res_bin_2_txt(output_dir, output_dir.path_join(dst_file), dst_file.replace(".scn", ".tscn").replace(".res", ".tres"))

func dequote(arg):
	if arg.begins_with("\"") and arg.ends_with("\""):
		return arg.substr(1, arg.length() - 2)
	if arg.begins_with("'") and arg.ends_with("'"):
		return arg.substr(1, arg.length() - 2)
	return arg

func _on_re_editor_standalone_dropped_files(files: PackedStringArray):
	if files.size() == 0:
		return
	var new_files = []
	for file in files:
		new_files.append(dequote(file))
	_on_recover_project_files_selected(new_files)

func popup_error_box(message: String, title: String, parent_window: Window) -> AcceptDialog:
	var dialog = AcceptDialog.new()
	dialog.set_text(message)
	dialog.set_title(title)
	get_tree().get_root().add_child(dialog)	
	dialog.connect("confirmed", parent_window.show)
	dialog.connect("canceled", parent_window.show)
	dialog.popup_centered()
	return dialog

const ERR_SKIP = 45

func _on_recovery_done():
	if RECOVERY_DIALOG:
		RECOVERY_DIALOG.hide_win()
		#get_tree().get_root().remove_child(RECOVERY_DIALOG)
		#RECOVERY_DIALOG = null



func split_args(args: String, splitter = ",") -> PackedStringArray:
	var parts = args.split(splitter, false)
	for i in range(parts.size()):
		parts[i] = dequote(parts[i].strip_edges())
	return parts



func _on_new_pck_selected(pck_path: String):
	NEW_PCK_DIALOG.hide_win()
	var pck_version = NEW_PCK_DIALOG.VERSION.text
	var major = NEW_PCK_DIALOG.VER_MAJOR.value
	var minor = NEW_PCK_DIALOG.VER_MINOR.value
	var ver_rev = NEW_PCK_DIALOG.VER_PATCH.value
	# var enc_key = GDRESettings.get_encryption_key_string()
	# # var engine_version_str = "%s.%s.%s" % [major, minor, ver_rev]
	var directory = NEW_PCK_DIALOG.DIRECTORY.text
	var includes = split_args(NEW_PCK_DIALOG.INCLUDES.text)
	var excludes = split_args(NEW_PCK_DIALOG.EXCLUDES.text)
	var extra_tag = NEW_PCK_DIALOG.EXTRA_TAG.text
	var embed = NEW_PCK_DIALOG.EMBED.is_pressed()
	var exe_to_embed = NEW_PCK_DIALOG.EMBED_SOURCE.text
	var watermark = NEW_PCK_DIALOG.EXTRA_TAG.text
	if not embed:
		exe_to_embed = ""
	var creator = PckCreator.new()
	creator.embed = embed
	creator.exe_to_embed = exe_to_embed
	creator.watermark = watermark
	creator.ver_major = major
	creator.ver_minor = minor
	creator.ver_rev = ver_rev
	creator.pack_version = pck_version.to_int()
	creator.watermark = extra_tag
	creator.encrypt = NEW_PCK_DIALOG.ENCRYPT.is_pressed()
	var err = creator.pck_create(pck_path, directory, includes, excludes)
	if (err):
		popup_error_box("Error creating PCK file!", "Error", REAL_ROOT_WINDOW)
		return


var _last_paths = PackedStringArray()

func _retry_recover():
	var paths = _last_paths
	_last_paths = PackedStringArray()
	_on_recover_project_files_selected(paths)

func close_recover_file_dialog():
	if _file_dialog:
		last_dir = _file_dialog.current_dir
		_file_dialog.hide()
		_file_dialog.set_transient(false)
		_file_dialog.set_exclusive(false)


func launch_recovery_window(paths: PackedStringArray):
	setup_new_pck_window()
	#RECOVERY_DIALOG = gdre_recover.instantiate()
	RECOVERY_DIALOG.set_root_window(REAL_ROOT_WINDOW)
	#REAL_ROOT_WINDOW.add_child(RECOVERY_DIALOG)
	#REAL_ROOT_WINDOW.move_child(RECOVERY_DIALOG, self.get_index() -1)
	RECOVERY_DIALOG.add_project(paths)
	#RECOVERY_DIALOG.connect("recovery_done", self._on_recovery_done)
	RECOVERY_DIALOG.show_win()

func setup_new_pck_window():
	pass
	
	if not RECOVERY_DIALOG:
		RECOVERY_DIALOG = $GdreRecover
	if not NEW_PCK_DIALOG:
		NEW_PCK_DIALOG = $GdreNewPck
	if not PATCH_PCK_DIALOG:
		PATCH_PCK_DIALOG = $GdrePatchPck
	if not ERROR_DIALOG:
		ERROR_DIALOG = $ErrorDialog



func launch_new_pck_window():
	setup_new_pck_window()
	NEW_PCK_DIALOG.show_win()

func launch_patch_pck_window():
	setup_new_pck_window()
	PATCH_PCK_DIALOG.show_win()

func _on_recover_project_files_selected(paths: PackedStringArray):
	close_recover_file_dialog()
	launch_recovery_window(paths)
	
func _on_recover_project_dir_selected(path):
	# just check if the dir path ends in ".app"
	close_recover_file_dialog()
	if path.ends_with(".app"):
		launch_recovery_window([path])
	else:
		# pop up an accept dialog
		popup_error_box("Invalid Selection!!", "Error", REAL_ROOT_WINDOW)
		return

func open_subwindow(window: Window):
	window.set_transient(true)
	window.set_exclusive(true)
	window.popup_centered()
	window.set_unparent_when_invisible(true)

func close_subwindow(window: Window):
	window.hide()
	window.set_exclusive(false)
	window.set_transient(false)


func open_about_window():
	$LegalNoticeWindow.popup_centered()

func open_setenc_window():
	$SetEncryptionKeyWindow.popup_centered()



func setup_file_dialog():
	setup_new_pck_window()
	# pop open a file dialog
	_file_dialog = FileDialog.new()
	_file_dialog.set_use_native_dialog(true)
	# This is currently broken in Godot, so we use the native dialogs
	#var prev_size = _file_dialog.size
	if isHiDPI:
		_file_dialog.size *= 2.0
	#_file_dialog.min_size = _file_dialog.size
	#d_viewport.content_scale_factor = 2.0
	_file_dialog.set_access(FileDialog.ACCESS_FILESYSTEM)
	_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILES #FileDialog.FILE_MODE_OPEN_FILE
	#_file_dialog.filters = ["*"]
	_file_dialog.filters = ["*.exe,*.bin,*.32,*.64,*.x86_64,*.x86,*.arm64,*.universal,*.pck,*.apk,*.app;Supported files"]
	#_file_dialog.filters = ["*.exe,*.bin,*.32,*.64,*.x86_64,*.x86,*.arm64,*.universal;Self contained executable files", "*.pck;PCK files", "*.apk;APK files", "*;All files"]
	## TODO: remove this
	_file_dialog.current_dir = GDRESettings.get_home_dir()
	_file_dialog.connect("files_selected", self._on_recover_project_files_selected)
	_file_dialog.connect("dir_selected", self._on_recover_project_dir_selected)
	get_tree().get_root().add_child.call_deferred(_file_dialog)

func open_recover_file_dialog():
	if last_dir != "":
		_file_dialog.current_dir = last_dir
	else:
		_file_dialog.current_dir = GDRESettings.get_home_dir()
	open_subwindow(_file_dialog)

func open_new_pck_dialog():
	launch_new_pck_window()

func _on_GDScriptMenu_item_selected(index):
	match index:
		GDScriptMenuID.DECOMPILE:
			# Decompile
			pass # TODO: open_decompile_file_dialog()
			$GdreDecompile.show_win()
		GDScriptMenuID.COMPILE:
			$GdreCompile.show_win()
			
			# open_compile_file_dialog()

func _on_REToolsMenu_item_selected(index):
	match index:
		REToolsMenuID.RECOVER:
			# Recover Project...
			open_recover_file_dialog()
		REToolsMenuID.SET_KEY:  # set key
			# Open the set key dialog
			open_setenc_window()
		REToolsMenuID.ABOUT:  # about
			open_about_window()
		REToolsMenuID.REPORT_BUG:  # Report a bug
			OS.shell_open("https://github.com/bruvzg/gdsdecomp/issues/new?assignees=&labels=bug&template=bug_report.yml&sys_info=" + GDRESettings.get_sys_info_string())
		REToolsMenuID.QUIT:  # Quit
			get_tree().quit()

func _on_ResourcesMenu_item_selected(index):
	match index:
		ResourcesMenuID.BIN_TO_TXT:
			# Convert binary resources to text...
			$BinToTextFileDialog.popup_centered()
		ResourcesMenuID.TXT_TO_BIN:
			# Convert text resources to binary...
			$TextToBinFileDialog.popup_centered()
		ResourcesMenuID.TEXTURE_TO_PNG:
			# Convert textures to PNG...
			$TextureToPNGFileDialog.popup_centered()
		ResourcesMenuID.OGGSTREAM_TO_OGG:
			# Convert OGG streams to OGG...
			$OggStreamToOGGFileDialog.popup_centered()
		ResourcesMenuID.MP3STREAM_TO_MP3:
			# Convert MP3 streams to MP3...
			$MP3StreamToMP3FileDialog.popup_centered()
		ResourcesMenuID.SAMPLE_TO_WAV:
			# Convert samples to WAV...
			$SampleToWAVFileDialog.popup_centered()
	

func get_recent_error_string():
	var error = GDRESettings.get_errors()
	if (error.is_empty()):
		return ""
	var error_message = ""
	for err in error:
		error_message += err.strip_edges() + "\n"
	return error_message


func _on_text_to_bin_file_dialog_files_selected(paths: PackedStringArray) -> void:
	GDRESettings.get_errors()
	var had_errors = false
	for path in paths:
		var new_path = path.get_basename()
		if path.get_extension().to_lower() == "scn":
			new_path += ".tscn"
		else:
			new_path += ".tres"
		if ResourceCompatLoader.to_text(path, new_path) != OK:
			had_errors = true
	if had_errors:
		GDREChildDialog.popup_box(self, ERROR_DIALOG, "Error: failed to convert files:\n" + get_recent_error_string(), "Error")


func _on_bin_to_text_file_dialog_files_selected(paths: PackedStringArray) -> void:
	GDRESettings.get_errors()
	var had_errors = false
	for path in paths:
		var new_path = path.get_basename()
		if path.get_extension().to_lower() == "tscn":
			new_path += ".scn"
		else:
			new_path += ".res"
		if ResourceCompatLoader.to_binary(path, new_path) != OK:
			had_errors = true
	if had_errors:
		GDREChildDialog.popup_box(self, ERROR_DIALOG, "Error: failed to convert files:\n" + get_recent_error_string(), "Error")


func _do_export(paths, new_ext):
	GDRESettings.get_errors()
	var had_errors = false
	for path in paths:
		var new_path = path.get_basename() + new_ext
		if Exporter.export_file(new_path, path) != OK:
			had_errors = true
	if had_errors:
		GDREChildDialog.popup_box(self, ERROR_DIALOG, "Error: failed to convert files:\n" + get_recent_error_string(), "Error")


func _on_texture_file_dialog_files_selected(paths: PackedStringArray) -> void:
	_do_export(paths, ".png")

func _on_ogg_file_dialog_files_selected(paths: PackedStringArray) -> void:
	_do_export(paths, ".wav")

func _on_sample_file_dialog_files_selected(paths: PackedStringArray) -> void:
	_do_export(paths, ".wav")

func _on_mp_3_stream_to_mp_3_file_dialog_files_selected(paths: PackedStringArray) -> void:
	_do_export(paths, ".mp3")


func _on_PCKMenu_item_selected(index):
	match index:
		PckMenuID.NEW_PCK:
			launch_new_pck_window()
		PckMenuID.PATCH_PCK:
			launch_patch_pck_window()
	
func _on_re_editor_standalone_write_log_message(message):
	$log_window.text += message
	$log_window.scroll_to_line($log_window.get_line_count() - 1)

func register_dropped_files():
	pass
	var window = get_viewport()
	var err = window.files_dropped.connect(_on_re_editor_standalone_dropped_files)
	if err != OK:
		print("Error: failed to connect window to files_dropped signal")
		print("Type: " + self.get_class())
		print("name: " + str(self.get_name()))


var repo_url = "https://github.com/bruvzg/gdsdecomp"
var latest_release_url = "https://github.com/bruvzg/gdsdecomp/releases/latest"

func _on_setenc_key_ok_pressed():
	# get the current text in the line edit
	var keytextbox = $SetEncryptionKeyWindow/VBoxContainer/KeyText
	var key:String = keytextbox.text
	if key.length() == 0:
		GDRESettings.reset_encryption_key()
	# set the key
	else:
		var err:int = GDRESettings.set_encryption_key_string(key)
		if (err != OK):
			keytextbox.text = ""
			# pop up an accept dialog
			popup_error_box("Invalid key!\nKey must be a hex string with 64 characters", "Error", $SetEncryptionKeyWindow)
			return
	# close the window
	$SetEncryptionKeyWindow.hide()
	
func _on_setenc_key_cancel_pressed():
	$SetEncryptionKeyWindow.hide()


func _on_version_lbl_pressed():
	OS.shell_open(repo_url)

func is_dev_version()-> bool:
	var version = GDRESettings.get_gdre_version()
	if "-dev" in version:
		return true
	return false

func check_version() -> bool:
	# check the version
	var http = HTTPRequest.new()
	# add it to the tree so it doesn't get deleted
	add_child(http)
	http.request_completed.connect(_on_version_check_completed)
	http.request("https://api.github.com/repos/bruvzg/gdsdecomp/releases/latest")
	return true
	
func is_new_version(new_version: String):
	var curr_version = GDRESettings.get_gdre_version()
	if curr_version == new_version:
		return false
	var curr_semver = SemVer.parse_semver(curr_version)
	var new_semver = SemVer.parse_semver(new_version)
	if curr_semver == null or new_semver == null:
		print("Error: invalid semver format")
		print("Current version: " + curr_version)
		print("New version: " + new_version)
		return false
	if new_semver.gt(curr_semver):
		return true
	return false


func _on_version_check_completed(_result, response_code, _headers, body):
	if response_code != 200:
		print("Error: failed to check for latest version")
		return
	var json = JSON.parse_string(body.get_string_from_utf8())
	var checked_version = json["tag_name"].strip_edges()
	var draft = json["draft"]
	var prerelease = json["prerelease"]
	var curr_version = GDRESettings.get_gdre_version()
	
	if draft or (prerelease and not ("-" in curr_version)) or not is_new_version(checked_version):
		return
	
	var update_str = "Update available! Click here! " + curr_version
	repo_url = latest_release_url
	$version_lbl.text = update_str
	print("New version of GDRE available: " + checked_version)
	print("Get it here: " + repo_url)

func should_show_disclaimer():
	var curr_version = GDRESettings.get_gdre_version()
	var last_showed  = GDRESettings.get_setting("last_showed_disclaimer")
	if last_showed == "<NONE>":
		return true
	if last_showed == curr_version:
		return false
	var curr_semver = SemVer.parse_semver(curr_version)
	var last_semver = SemVer.parse_semver(last_showed)
	if curr_semver == null or last_semver == null:
		return true
	return not (curr_semver.major == last_semver.major and curr_semver.minor == last_semver.minor)

func set_showed_disclaimer(setting: bool):
	var version = "<NONE>"
	if setting:
		version = GDRESettings.get_gdre_version()
	GDRESettings.set_setting("last_showed_disclaimer", version)

func handle_quit(save_cfg = true):
	if GDRESettings.is_pack_loaded():
		GDRESettings.unload_project()


	#readd_items($MenuContainer/REToolsMenu
func _resize_menu_times(menu_container:HBoxContainer):
	for menu_btn: MenuButton in menu_container.get_children():

		var popup : PopupMenu = menu_btn.get_popup()
		# broken
		#popup.visible = true
		#var size = popup.size * 2
		#popup.size = size
		#popup.min_size = size
		#popup.max_size = size * 2
		#popup.get_viewport().content_scale_factor = 2.0
		#popup.visible = false
		# readd_items(menu_btn)
		if isHiDPI:
			var old_theme = popup.theme
			#menu_btn.connect("theme_changed", self._on_menu_btn_theme_changed)
			var new_theme = old_theme
			if !new_theme:
				new_theme = Theme.new()
			var font_size = new_theme.get_font_size("", "")
			#new_theme.set_font_size("", "", font_size * 2)
			new_theme.set_default_font_size(font_size * 2)
			popup.theme = new_theme
		var item_count = menu_btn.get_popup().get_item_count()
		for i in range(item_count):
			var icon: Texture2D = popup.get_item_icon(i)
			if icon:
				var icon_size = icon.get_size()
				popup.set_item_icon_max_width(i, icon_size.x * (2.0 if isHiDPI else 0.5))


func _notification(what: int) -> void:
	if what == NOTIFICATION_EXIT_TREE:
		handle_quit()
	elif what == NOTIFICATION_WM_ABOUT:
		open_about_window()
	


func get_glob_files(glob: String) -> PackedStringArray:
	var files: PackedStringArray = Glob.rglob(glob)
	# doing this because non-windows platforms can have '?' and '[' in filenames
	if files.size() == 0 and FileAccess.file_exists(glob):
		files.append(glob)
	return files

func get_globs_files(globs: PackedStringArray) -> PackedStringArray:
	var files: PackedStringArray = []
	for glob in globs:
		files.append_array(get_glob_files(glob))
	return files

func _ready():
	$version_lbl.text = GDRESettings.get_gdre_version()
	# If CLI arguments were passed in, just quit
	var args = get_sanitized_args()
	if handle_cli(args):
		get_tree().quit()
		return
	var show_disclaimer = should_show_disclaimer()
	show_disclaimer = show_disclaimer and len(args) == 0
	if show_disclaimer:
		set_showed_disclaimer(true)
		GDRESettings.save_config()
	register_dropped_files()
	REAL_ROOT_WINDOW = get_window()
	var popup_menu_gdremenu: PopupMenu = $MenuContainer/REToolsMenu.get_popup()
	popup_menu_gdremenu.connect("id_pressed", self._on_REToolsMenu_item_selected)
	GDRESettings.connect("write_log_message", self._on_re_editor_standalone_write_log_message)
	$MenuContainer/PCKMenu.get_popup().connect("id_pressed", self._on_PCKMenu_item_selected)
	$MenuContainer/GDScriptMenu.get_popup().connect("id_pressed", self._on_GDScriptMenu_item_selected)
	$MenuContainer/ResourcesMenu.get_popup().connect("id_pressed", self._on_ResourcesMenu_item_selected)
	$version_lbl.text = GDRESettings.get_gdre_version()
	$LegalNoticeWindow/OkButton.connect("pressed", $LegalNoticeWindow.hide)
	$LegalNoticeWindow.connect("close_requested", $LegalNoticeWindow.hide)

	# check if the current screen is hidpi
	if isHiDPI:
		# set the content scaling factor to 2x
		ThemeDB.fallback_base_scale = 2.0
		get_viewport().content_scale_factor = 2.0
		get_viewport().size *= 2
		$SetEncryptionKeyWindow.content_scale_factor = 2.0
		$SetEncryptionKeyWindow.size *= 2
		$LegalNoticeWindow.content_scale_factor = 2.0
		$LegalNoticeWindow.size *=2

	if show_disclaimer:
		open_about_window()
	if len(args) > 0:
		var window = get_viewport()
		window.call_deferred("emit_signal", "files_dropped", args)

	setup_file_dialog()
	check_version()
	# _resize_menu_times($MenuContainer)

# CLI stuff below

func get_arg_value(arg):
	var split_args = arg.split("=", false, 1)
	if split_args.size() < 2:
		last_error = "Error: args have to be in the format of --key=value (with equals sign)"
		return ""
	return dequote(split_args[1])

func normalize_path(path: String):
	return path.replace("\\","/")

func test_decomp(fname):
	var decomp = GDScriptDecomp_ed80f45.new()
	var f = fname
	if f.get_extension() == "gdc":
		print("decompiling " + f)
		#
		#if decomp.decompile_byte_code(output_dir.path_join(f)) != OK: 
		if decomp.decompile_byte_code(f) != OK: 
			print("error decompiling " + f)
		else:
			var text = decomp.get_script_text()
			var gdfile:FileAccess = FileAccess.open(f.replace(".gdc",".gd"), FileAccess.WRITE)
			if gdfile == null:
				gdfile.store_string(text)
				gdfile.close()
				#da.remove(f)
				print("successfully decompiled " + f)
			else:
				print("error failed to save "+ f)

func export_imports(output_dir:String, files: PackedStringArray):
	var importer:ImportExporter = ImportExporter.new()
	importer.set_multi_thread(not disable_multi_threading)
	importer.export_imports(output_dir, files)
	importer.reset()
				
	
func dump_files(output_dir:String, files: PackedStringArray, ignore_checksum_errors: bool = false) -> int:
	var err:int = OK;
	var pckdump = PckDumper.new()
	# var start_time = Time.get_ticks_msec()
	pckdump.set_multi_thread(not disable_multi_threading)
	err = pckdump.check_md5_all_files()
	if err != OK:
		if (err != ERR_SKIP and not ignore_checksum_errors):
			print("MD5 checksum failed, not proceeding...")
			return err
		elif (ignore_checksum_errors):
			print("MD5 checksum failed, but --ignore_checksum_errors specified, proceeding anyway...")
	err = pckdump.pck_dump_to_dir(output_dir, files)
	if err != OK:
		print("error dumping to dir")
	# var end_time = Time.get_ticks_msec()
	# var secs_taken = (end_time - start_time) / 1000
	# print("Extraction complete in %02dm%02ds" % [(secs_taken) / 60, (secs_taken) % 60])
	return err;

var MAIN_COMMANDS = ["--recover", "--extract", "--compile", "--list-bytecode-versions"]
var MAIN_CMD_NOTES = """Main commands:
--recover=<GAME_PCK/EXE/APK/DIR>   Perform full project recovery on the specified PCK, APK, EXE, or extracted project directory.
--extract=<GAME_PCK/EXE/APK>       Extract the specified PCK, APK, or EXE.
--compile=<GD_FILE>                Compile GDScript files to bytecode (can be repeated and use globs, requires --bytecode)
--decompile=<GDC_FILE>             Decompile GDC files to text (can be repeated and use globs)
--pck-create=<PCK_DIR>             Create a PCK file from the specified directory (requires --pck-version and --pck-engine-version)
--pck-patch=<GAME_PCK/EXE>         Patch a PCK file with the specified files
--list-bytecode-versions           List all available bytecode versions
--txt-to-bin=<FILE>                Convert text-based scene or resource files to binary format (can be repeated)
--bin-to-txt=<FILE>                Convert binary scene or resource files to text-based format (can be repeated)
"""

var GLOB_NOTES = """Notes on Include/Exclude globs:
	- Recursive patterns can be specified with '**'
		- Example: 'res://**/*.gdc' matches 'res://main.gdc', 'res://scripts/script.gdc', etc.)
	- Globs should be rooted to 'res://' or 'user://'
		- Example: 'res://*.gdc' will match all .gdc files in the root of the project, but not any of the subdirectories.
	- If not rooted, globs will be rooted to 'res://'
		- Example: 'addons/plugin/main.gdc' is equivalent to 'res://addons/plugin/main.gdc'
	- As a special case, if the glob has a wildcard and does contain a directory, it will be assumed to be a recursive pattern.
		- Example: '*.gdc' would be equivalent to 'res://**/*.gdc'
	- Include/Exclude globs will only match files that are actually in the project PCK/dir, not any non-present resource source files.
		Example: 
			- A project contains the file "res://main.gdc". 'res://main.gd' is the source file of 'res://main.gdc',
			  but is not included in the project PCK.
			- Performing project recovery with the include glob 'res://main.gd' would not recover 'main.gd'.
			- Performing project recovery with the include glob 'res://main.gdc' would recover 'res://main.gd'
"""

var RECOVER_OPTS_NOTES = """Recover/Extract Options:

--key=<KEY>                 The Key to use if project is encrypted as a 64-character hex string,
							e.g.: '000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F'
--output=<DIR>              Output directory, defaults to <NAME_extracted>, or the project directory if one of specified
--scripts-only              Only extract/recover scripts
--include=<GLOB>            Include files matching the glob pattern (can be repeated)
--exclude=<GLOB>            Exclude files matching the glob pattern (can be repeated)
--ignore-checksum-errors    Ignore MD5 checksum errors when extracting/recovering
"""
# todo: handle --key option
var COMPILE_OPTS_NOTES = """Decompile/Compile Options:
--bytecode=<COMMIT_OR_VERSION>          Either the commit hash of the bytecode revision (e.g. 'f3f05dc'),
										   or the version of the engine (e.g. '4.3.0')
--output=<DIR>                          Directory where compiled files will be output to. 
										  - If not specified, compiled files will be output to the same location 
										  (e.g. '<PROJ_DIR>/main.gd' -> '<PROJ_DIR>/main.gdc')
"""

var CREATE_OPTS_NOTES = """Create PCK Options:
--output=<OUTPUT_PCK/EXE>                The output PCK file to create
--pck-version=<VERSION>                  The format version of the PCK file to create (0, 1, 2)
--pck-engine-version=<ENGINE_VERSION>    The version of the engine to create the PCK for (x.y.z)
--embed=<EXE_TO_EMBED>                   The executable to embed the PCK into
--key=<KEY>                              64-character hex string to encrypt the PCK with
"""

var PATCH_OPTS_NOTES = """Patch PCK Options:
--output=<OUTPUT_PCK>                    The output PCK file to create
--patch-file=<SRC_FILE>=<DEST_FILE>      The file to patch the PCK with (e.g. "/path/to/file.gd=res://file.gd") (can be repeated)
--include=<GLOB>                         Only include files from original PCK matching the glob pattern (can be repeated)
--exclude=<GLOB>                         Exclude files from original PCK matching the glob pattern (can be repeated)
--key=<KEY>                              64-character hex string to decrypt/encrypt the PCK with
"""

func print_usage():
	print("Godot Reverse Engineering Tools")
	print("")
	print("Without any CLI options, the tool will start in GUI mode")
	print("\nGeneral options:")
	print("  -h, --help: Display this help message")
	print("\nFull Project Recovery options:")
	print("Usage: GDRE_Tools.exe --headless <main_command> [options]")
	print(MAIN_CMD_NOTES)
	print(RECOVER_OPTS_NOTES)
	print(GLOB_NOTES)
	print(COMPILE_OPTS_NOTES)
	print(CREATE_OPTS_NOTES)
	print(PATCH_OPTS_NOTES)


# TODO: remove this hack
var translation_only = false
var SCRIPTS_EXT = ["gd", "gdc", "gde"]
func copy_dir(src:String, dst:String) -> int:
	var da:DirAccess = DirAccess.open(src)
	if !da.dir_exists(src):
		print("Error: " + src + " does not appear to be a directory")
		return ERR_FILE_NOT_FOUND
	da.make_dir_recursive(dst)
	da.copy_dir(src, dst)
	return OK

func get_cli_abs_path(path:String) -> String:
	path = path.simplify_path()
	if path.is_absolute_path():
		return path
	var exec_path = GDRESettings.get_exec_dir()
	if path.begins_with('~/'):
		path = GDRESettings.get_home_dir() + path.trim_prefix('~')
	var abs_path = exec_path.path_join(path).simplify_path()
	return abs_path

func normalize_cludes(cludes: PackedStringArray, dir = "res://") -> PackedStringArray:
	var new_cludes: PackedStringArray = []
	if dir != dir.get_base_dir() and dir.ends_with("/"):
		dir = dir.substr(0, dir.length() - 1)
	for clude in cludes:
		clude = clude.replace("\\", "/")
		if not "**" in clude and "*" in clude and not "/" in clude:
			new_cludes.append("res://**/" + clude)
			# new_cludes.append("user://**/" + clude)
			continue
		if clude.begins_with("/") and dir == "res://":
			clude = clude.substr(1, clude.length() - 1)
		if not clude.is_absolute_path():
			clude = dir.path_join(clude)
		elif dir != "res://":
			clude = clude.replace("res:/", dir)
		elif clude.begins_with("/"):
			clude = dir + clude.substr(1, clude.length() - 1)
		new_cludes.append(clude.simplify_path())
	return new_cludes


func recovery(  input_files:PackedStringArray,
				output_dir:String,
				enc_key:String,
				extract_only: bool,
				ignore_checksum_errors: bool = false,
				excludes: PackedStringArray = [],
				includes: PackedStringArray = []):
	var _new_files = []
	for file in input_files:
		file = get_cli_abs_path(file)
		var _files = get_glob_files(file)
		if _files.size() > 0:
			_new_files.append_array(_files)
		else:
			print_usage()
			print("Error: failed to locate " + file)
			return
	print("Input files: ", str(_new_files))
	input_files = _new_files
	var input_file = input_files[0]
	var da:DirAccess
	var is_dir:bool = false
	var err: int = OK
	var parent_dir = "res://"
	# get the current time
	var start_time = Time.get_ticks_msec()
	if output_dir == "":
		output_dir = input_file.get_basename()
		if output_dir.get_extension():
			output_dir += "_recovery"
	else:
		output_dir = get_cli_abs_path(output_dir)

	da = DirAccess.open(input_file.get_base_dir())

	# check if da works
	if da == null:
		print_usage()
		print("Error: failed to locate parent dir for " + input_file)
		return
	#directory
	if da.dir_exists(input_file):
		if input_files.size() > 1:
			print_usage()
			print("Error: cannot specify multiple directories")
			return
		if input_file.get_extension().to_lower() == "app":
			is_dir = false
		elif !da.dir_exists(input_file.path_join(".import")) && !da.dir_exists(input_file.path_join(".godot")):
			print_usage()
			print("Error: " + input_file + " does not appear to be a project directory")
			return
		else:
			parent_dir = input_file
			is_dir = true
	#PCK/APK
	elif not da.file_exists(input_file):
		print_usage()
		print("Error: failed to locate " + input_file)
		return

	GDRESettings.open_log_file(output_dir)
	if (enc_key != ""):
		err = GDRESettings.set_encryption_key_string(enc_key)
		if (err != OK):
			print_usage()
			print("Error: failed to set key!")
			return
	
	err = GDRESettings.load_project(input_files, extract_only)
	if (err != OK):
		print_usage()
		print("Error: failed to open ", (input_files))
		return

	print("Successfully loaded PCK!") 
	ver_major = GDRESettings.get_ver_major()
	ver_minor = GDRESettings.get_ver_minor()
	var version:String = GDRESettings.get_version_string()
	print("Version: " + version)
	var files: PackedStringArray = []
	if translation_only and scripts_only:
		print("Error: cannot specify both --translation-only and --scripts-only")
		return
	elif ((translation_only or scripts_only) and (includes.size() > 0 or excludes.size() > 0)):
		print("Error: cannot specify both --translation-only or --scripts-only and --include or --exclude")
		return
	if (translation_only):
		var new_files:PackedStringArray = []
		# remove all the non ".translation" files
		for file in GDRESettings.get_file_list():
			if (file.get_extension().to_lower() == "translation"):
				new_files.append(file)
		files.append_array(new_files)
		print("Translation only mode, only extracting translation files")
	elif scripts_only:
		var new_files:PackedStringArray = []
		# remove all the non ".gd" files
		for file in GDRESettings.get_file_list():
			if (file.get_extension().to_lower() in SCRIPTS_EXT):
				new_files.append(file)
		files.append_array(new_files)
		print("Scripts only mode, only extracting scripts")
	else:
		if includes.size() > 0:
			includes = normalize_cludes(includes, parent_dir)
			files = get_globs_files(includes)
			if len(files) == 0:
				print("Error: no files found that match includes")
				print("Includes: " + str(includes))
				print(GLOB_NOTES)
				return
		else:
			files = GDRESettings.get_file_list()
		if excludes.size() > 0:
			excludes = normalize_cludes(excludes, parent_dir)
			var result = Glob.fnmatch_list(files, excludes)
			for file in result:
				files.remove_at(files.rfind(file))

		if (includes.size() > 0 or excludes.size() > 0) and files.size() == 0:
			print("Error: no files to extract after filtering")
			if len(includes) > 0:
				print("Includes: " + str(includes))
			if len(excludes) > 0:
				print("Excludes: " + str(excludes))
			print(GLOB_NOTES)
			return

	if output_dir != input_file and not is_dir: 
		if (da.file_exists(output_dir)):
			print("Error: output dir appears to be a file, not extracting...")
			return
	if is_dir:
		if extract_only:
			print("Why did you open a folder to extract it??? What's wrong with you?!!?")
			return
		if copy_dir(input_file, output_dir) != OK:
			print("Error: failed to copy " + input_file + " to " + output_dir)
			return
	else:
		err = dump_files(output_dir, files, ignore_checksum_errors)
		if (err != OK):
			print("Error: failed to extract PAK file, not exporting assets")
			return
	var end_time;
	var secs_taken;
	if (extract_only):
		end_time = Time.get_ticks_msec()
		secs_taken = (end_time - start_time) / 1000
		print("Extraction operation complete in %02dm%02ds" % [(secs_taken) / 60, (secs_taken) % 60])
		return
	export_imports(output_dir, files)
	end_time = Time.get_ticks_msec()
	secs_taken = (end_time - start_time) / 1000
	print("Recovery complete in %02dm%02ds" % [(secs_taken) / 60, (secs_taken) % 60])


func load_pck(input_files: PackedStringArray, extract_only: bool, includes, excludes, enc_key: String = ""):
	var _new_files = []
	for file in input_files:
		file = get_cli_abs_path(file)
		var _files = get_glob_files(file)
		if _files.size() > 0:
			_new_files.append_array(_files)
		else:
			print_usage()
			print("Error: failed to locate " + file)
			return []
	print("Input files: ", str(_new_files))
	input_files = _new_files
	var input_file = input_files[0]
	var da:DirAccess
	var is_dir:bool = false
	var err: int = OK
	var parent_dir = "res://"
	# get the current time
	var start_time = Time.get_ticks_msec()
	da = DirAccess.open(input_file.get_base_dir())

	# check if da works
	if da == null:
		print_usage()
		print("Error: failed to locate parent dir for " + input_file)
		return []
	#directory
	if da.dir_exists(input_file):
		if input_files.size() > 1:
			print_usage()
			print("Error: cannot specify multiple directories")
			return []
		if input_file.get_extension().to_lower() == "app":
			is_dir = false
		elif !da.dir_exists(input_file.path_join(".import")) && !da.dir_exists(input_file.path_join(".godot")):
			print_usage()
			print("Error: " + input_file + " does not appear to be a project directory")
			return []
		else:
			parent_dir = input_file
			is_dir = true
	#PCK/APK
	elif not da.file_exists(input_file):
		print_usage()
		print("Error: failed to locate " + input_file)
		return []

	if (enc_key != ""):
		err = GDRESettings.set_encryption_key_string(enc_key)
		if (err != OK):
			print_usage()
			print("Error: failed to set key!")
			return []
	
	err = GDRESettings.load_project(input_files, extract_only)
	if (err != OK):
		print_usage()
		print("Error: failed to open ", (input_files))
		return []

	var files: PackedStringArray = []
	if includes.size() > 0:
		includes = normalize_cludes(includes, parent_dir)
		files = get_globs_files(includes)
		if len(files) == 0:
			print("Error: no files found that match includes")
			print("Includes: " + str(includes))
			print(GLOB_NOTES)
			return []
	else:
		files = GDRESettings.get_file_list()
	if excludes.size() > 0:
		excludes = normalize_cludes(excludes, parent_dir)
		var result = Glob.fnmatch_list(files, excludes)
		for file in result:
			files.remove_at(files.rfind(file))

	if (includes.size() > 0 or excludes.size() > 0) and files.size() == 0:
		print("Error: no files to extract after filtering")
		if len(includes) > 0:
			print("Includes: " + str(includes))
		if len(excludes) > 0:
			print("Excludes: " + str(excludes))
		print(GLOB_NOTES)
		return []
	return files
	


func print_version():
	print("Godot RE Tools " + GDRESettings.get_gdre_version())

func close_log():
	var path = GDRESettings.get_log_file_path()
	if path == "":
		return
	GDRESettings.close_log_file()
	print("Log file written to: " + path)
	print("Please include this file when reporting issues!")

func ensure_dir_exists(dir: String):
	var da:DirAccess = DirAccess.open(GDRESettings.get_exec_dir())
	if !da.dir_exists(dir):
		da.make_dir_recursive(dir)

func get_decomp(bytecode_version: String) -> GDScriptDecomp:
	var decomp: GDScriptDecomp = null
	if '.' in bytecode_version:
		decomp = GDScriptDecomp.create_decomp_for_version(bytecode_version)
	else:
		decomp = GDScriptDecomp.create_decomp_for_commit(bytecode_version.hex_to_int())
	if decomp == null:
		print("Error: failed to create decompiler for commit " + bytecode_version + "!\n(run --list-bytecode-versions to see available versions)")
	return decomp

func compile(files: PackedStringArray, bytecode_version: String, output_dir: String):
	# TODO: handle key
	if output_dir == "":
		output_dir = get_cli_abs_path(".") # default to current directory
	if bytecode_version == "":
		print("Error: --bytecode is required for --compile (use --list-bytecode-versions to see available versions)")
		print(COMPILE_OPTS_NOTES)
		return -1
	var decomp: GDScriptDecomp = get_decomp(bytecode_version)
	if decomp == null:
		return -1
	print("Compiling to bytecode version %x (%s)" % [decomp.get_bytecode_rev(), decomp.get_engine_version()])

	var new_files = get_globs_files(files)
	if new_files.size() == 0:
		print("Error: no files found to compile")
		return -1
	ensure_dir_exists(output_dir)
	for file in new_files:
		print("Compiling " + file)
		if file.get_extension() != "gd":
			print("Error: " + file + " is not a GDScript file")
			continue
		var f = FileAccess.open(file, FileAccess.READ)
		var code = f.get_as_text()
		var bytecode: PackedByteArray = decomp.compile_code_string(code)
		if bytecode.is_empty():
			print("Error: failed to compile " + file)
			print(decomp.get_error_message())
			continue
		var out_file = output_dir.path_join(file.get_file().replace(".gd", ".gdc"))
		var out_f = FileAccess.open(out_file, FileAccess.WRITE)
		out_f.store_buffer(bytecode)
		out_f.close()
		print("Compiled " + file + " to " + out_file)		
	print("Compilation complete")
	return 0

func decompile(files: PackedStringArray, bytecode_version: String, output_dir: String, key: String = ""):
	if output_dir == "":
		output_dir = get_cli_abs_path(".") # default to current directory
	if bytecode_version == "":
		print("Error: --bytecode is required for --decompile (use --list-bytecode-versions to see available versions)")
		print(COMPILE_OPTS_NOTES)
		return -1
	var decomp: GDScriptDecomp = get_decomp(bytecode_version)
	if decomp == null:
		return -1
	print("Decompiling from bytecode version %x (%s)" % [decomp.get_bytecode_rev(), decomp.get_engine_version()])
	var new_files = get_globs_files(files)
	if new_files.size() == 0:
		print("Error: no files found to decompile")
		return -1
	ensure_dir_exists(output_dir)

	var err = OK
	if key != "":
		err = GDRESettings.set_encryption_key_string(key)
		if err != OK:
			print("Error: failed to set key!")
			return -1
	for file in new_files:
		var src_ext = file.get_extension().to_lower()
		
		if src_ext != "gdc" and src_ext != "gde":
			print("Error: " + file + " is not a GDScript bytecode file")
			continue
		print("Decompiling " + file)
		err = decomp.decompile_byte_code(file)
		var out_file = file.get_basename() + ".gd"
		if output_dir:
			out_file = output_dir.path_join(out_file.get_file())
		print("Output file: " + out_file)

		if err != OK:
			print("Error: failed to decompile " + file)
			print(decomp.get_error_message())
			continue
		var text = decomp.get_script_text()
		var out_f = FileAccess.open(out_file, FileAccess.WRITE)
		if out_f == null:
			print("Error: failed to open " + out_file + " for writing")
			continue
		out_f.store_string(text)
		out_f.close()
		print("Decompiled " + file + " to " + out_file)		
	print("Decompilation complete")
	return 0

func get_sanitized_args():
	var args = OS.get_cmdline_args()
	#var scene_path = get_tree().root.scene_file_path
	var scene_path = "res://gdre_main.tscn"
	if args.size() > 0 and args[0].begins_with("res://gdre") and args[0].ends_with(".tscn"):
		return args.slice(1)
	return args

func text_to_bin(files: PackedStringArray, output_dir: String):
	var importer:ImportExporter = ImportExporter.new()
	for file in files:
		file = get_cli_abs_path(file)
		var dst_file = file.get_file().replace(".tscn", ".scn").replace(".tres", ".res")
		importer.convert_res_txt_2_bin(output_dir, file, dst_file)

func bin_to_text(files: PackedStringArray, output_dir: String):
	var importer:ImportExporter = ImportExporter.new()
	for file in files:
		file = get_cli_abs_path(file)
		var dst_file = file.get_file().replace(".scn", ".tscn").replace(".res", ".tres")
		importer.convert_res_bin_2_txt(output_dir, file, dst_file)



func create_pck(pck_file: String, pck_dir: String, pck_version: int, pck_engine_version: String, includes: PackedStringArray = [], excludes: PackedStringArray = [], enc_key: String = "", embed_pck: String = "", watermark: String = ""):
	if (pck_version < 0 or pck_engine_version == ""):
		print_usage()
		print("Error: --pck-version and --pck-engine-version are required for --pck-create")
		return "Error: --pck-version and --pck-engine-version are required for --pck-create"
	if (pck_file.is_empty()):
		print_usage()
		print("Error: --output is required for --pck-create")
		return "Error: --output is required for --pck-create"

	pck_dir = get_cli_abs_path(pck_dir)
	pck_file = get_cli_abs_path(pck_file)
	if (not DirAccess.dir_exists_absolute(pck_dir)):
		print_usage()
		print("Error: directory '" + pck_dir + "' does not exist")
		return "Error: --output is required for --pck-create"
	var pck = PckCreator.new()
	pck.pack_version = pck_version
	# split the engine version
	var split = pck_engine_version.split(".")
	if (split.size() != 3):
		print("Error: invalid engine version format (x.y.z)")
		return "Error: invalid engine version format (x.y.z)"
	pck.ver_major = split[0].to_int()
	pck.ver_minor = split[1].to_int()
	pck.ver_rev = split[2].to_int()
	if (not enc_key.is_empty()):
		var err = GDRESettings.set_encryption_key_string(enc_key)
		if (err != OK):
			print("Error: failed to set key!")
			return "Error: failed to set key!"
		pck.set_encrypt(true)
	if (not embed_pck.is_empty()):
		embed_pck = get_cli_abs_path(embed_pck)
		if (not FileAccess.file_exists(embed_pck)):
			print("Error: embed EXE file '" + embed_pck + "' does not exist")
			return "Error: embed EXE file '" + embed_pck + "' does not exist"
		pck.exe_to_embed = embed_pck
		print("Embedding PCK: " + embed_pck)
	if (not watermark.is_empty()):
		pck.watermark = watermark
	pck.pck_create(pck_file, pck_dir, includes, excludes)


func handle_cli(args: PackedStringArray) -> bool:
	var input_extract_file:PackedStringArray = []
	var input_file:PackedStringArray = []
	var pck_create_dir: String       = ""
	var pck_patch_pck: String = ""
	var patch_map: Dictionary[String, String] = {}
	var pck_version: int             = -1
	var pck_engine_version: String   = ""
	var embed_pck: String             = ""
	var output_dir: String = ""
	var enc_key: String = ""
	var txt_to_bin = PackedStringArray()
	var bin_to_txt = PackedStringArray()
	var ignore_md5: bool = false
	var decompile_files = PackedStringArray()
	var compile_files = PackedStringArray()
	var bytecode_version: String = ""
	var main_cmds = {}
	var excludes: PackedStringArray = []
	var includes: PackedStringArray = []
	var prepop: PackedStringArray = []
	var set_setting: bool = false
	if (args.size() == 0):
		return false
	var any_commands = false
	for i in range(args.size()):
		var arg:String = args[i]
		if arg.begins_with("--"):
			any_commands = true
			break
	if any_commands == false:
		if not GDRESettings.is_headless():
			# not cli mode, drag-and-drop
			return false
		print_usage()
		print("ERROR: no command specified")
		return true
	for i in range(args.size()):
		var arg:String = args[i]
		if arg == "--help":
			print_version()
			print_usage()
			return true
		elif arg.begins_with("--version"):
			print_version()
			return true
		elif arg.begins_with("--extract"):
			input_extract_file.append(get_arg_value(arg).simplify_path())
			main_cmds["extract"] = true
		elif arg.begins_with("--recover"):
			input_file.append(get_arg_value(arg).simplify_path())
			main_cmds["recover"] = true
		elif arg.begins_with("--txt-to-bin"):
			txt_to_bin.append(get_arg_value(arg).simplify_path())
			main_cmds["txt-to-bin"] = true
		elif arg.begins_with("--bin-to-txt"):
			bin_to_txt.append(get_arg_value(arg).simplify_path())
			main_cmds["bin-to-txt"] = true
		elif arg.begins_with("--output") or arg.begins_with("--output-dir"):
			output_dir = get_arg_value(arg).simplify_path()
		elif arg.begins_with("--scripts-only"):
			scripts_only = true
		elif arg.begins_with("--key"):
			enc_key = get_arg_value(arg)
		elif arg.begins_with("--ignore-checksum-errors"):
			ignore_md5 = true
		elif arg.begins_with("--translation-only"):
			translation_only = true
		elif arg.begins_with("--disable-multithreading"):
			disable_multi_threading = true
		elif arg.begins_with("--enable-experimental-plugin-downloading"):
			GDRESettings.set_setting("download_plugins", true)
			set_setting = true
		elif arg.begins_with("--list-bytecode-versions"):
			var versions = GDScriptDecomp.get_bytecode_versions()
			print("\n--- Available bytecode versions:")
			for version in versions:
				print(version)
			return true
		elif arg.begins_with("--bytecode"):
			bytecode_version = get_arg_value(arg)
		elif arg.begins_with("--decompile"):
			main_cmds["decompile"] = true
			decompile_files.append(get_arg_value(arg))
		elif arg.begins_with("--compile"):
			main_cmds["compile"] = true
			compile_files.append(get_arg_value(arg))
		elif arg.begins_with("--exclude"):
			excludes.append(get_arg_value(arg))
		elif arg.begins_with("--include"):
			includes.append(get_arg_value(arg))
		elif arg.begins_with("--pck-create"):
			main_cmds["pck-create"] = true
			pck_create_dir = get_cli_abs_path(get_arg_value(arg))
		elif arg.begins_with("--pck-version"):
			pck_version = (get_arg_value(arg)).to_int()
		elif arg.begins_with("--pck-engine-version"):
			pck_engine_version = (get_arg_value(arg))
		elif arg.begins_with("--embed"):
			embed_pck = get_cli_abs_path(get_arg_value(arg))
		elif arg.begins_with("--plcache"):
			main_cmds["plcache"] = true
			prepop.append(get_arg_value(arg))
		elif arg.begins_with("--pck-patch"):
			main_cmds["pck-patch"] = true
			pck_patch_pck = get_cli_abs_path(get_arg_value(arg))
		elif arg.begins_with("--patch-file"):
			var parsed_arg = get_arg_value(arg)
			var patch_files = parsed_arg.split("=", false, 2)
			if patch_files.size() != 2:
				print_usage()
				print("ERROR: invalid --patch-file format: must be <src_file>=<dest_file>")
				print(arg)
				print(parsed_arg)
				print(patch_files)
				return true
			patch_map[get_cli_abs_path(dequote(patch_files[0]).strip_edges())] = dequote(patch_files[1]).strip_edges()
		else:
			print_usage()
			print("ERROR: invalid option '" + arg + "'")
			return true
		if last_error != "":
			print_usage()
			print(last_error)
			return true
	if main_cmds.size() > 1:
		print_usage()
		print("ERROR: invalid option! Must specify only one of " + ", ".join(MAIN_COMMANDS))
		return true
	print(main_cmds)
	if prepop.size() > 0:
		var start_time = Time.get_ticks_msec()
		GDRESettings.prepop_plugin_cache(prepop)
		var end_time = Time.get_ticks_msec()
		var secs_taken = (end_time - start_time) / 1000
		print("Prepop complete in %02dm%02ds" % [(secs_taken) / 60, (secs_taken) % 60])
	elif compile_files.size() > 0:
		compile(compile_files, bytecode_version, output_dir)
	elif decompile_files.size() > 0:
		decompile(decompile_files, bytecode_version, output_dir, enc_key)
	elif not input_file.is_empty():
		recovery(input_file, output_dir, enc_key, false, ignore_md5, excludes, includes)
		GDRESettings.unload_project()
		close_log()
	elif not input_extract_file.is_empty():
		recovery(input_extract_file, output_dir, enc_key, true, ignore_md5, excludes, includes)
		GDRESettings.unload_project()
		close_log()
	elif txt_to_bin.is_empty() == false:
		text_to_bin(txt_to_bin, output_dir)
	elif bin_to_txt.is_empty() == false:
		bin_to_text(bin_to_txt, output_dir)
	elif not pck_create_dir.is_empty():
		create_pck(output_dir, pck_create_dir, pck_version, pck_engine_version, includes, excludes, enc_key, embed_pck)
	elif not pck_patch_pck.is_empty():
		patch_pck(pck_patch_pck, output_dir, patch_map, includes, excludes, enc_key)
		GDRESettings.unload_project()
	elif set_setting:
		return false # don't quit
	else:
		print_usage()
		print("ERROR: invalid option! Must specify one of " + ", ".join(MAIN_COMMANDS))
	return true

func _start_patch_pck(dest_pck: String, pack_info: PackInfo):
	var engine_version: GodotVer = pack_info.get_version()
	var encrypted = pack_info.is_encrypted()
	var embed = false
	var embed_src = ""
	if pack_info.get_type() == 4: # EXE; TODO: FIX ENUM NOT BINDING CORRECTLY
		embed = true
		embed_src = pack_info.get_path()
	var pck_creator = PckCreator.new()
	pck_creator.start_pck(dest_pck, 
							pack_info.get_fmt_version(),
							engine_version.major,
							engine_version.minor,
							engine_version.patch,
							encrypted,
							embed,
							embed_src)
	return pck_creator

func patch_pck(src_file: String, dest_pck:String, patch_file_map: Dictionary, includes: PackedStringArray = [], excludes: PackedStringArray = [], enc_key: String = ""):
	if (src_file.is_empty()):
		print_usage()
		print("Error: --pck-patch is required")
		return "Error: --pck-patch is required"
	if (dest_pck.is_empty()):
		print_usage()
		print("Error: --output is required")
		return "Error: --output is required"
	src_file = get_cli_abs_path(src_file)
	if (not FileAccess.file_exists(src_file)):
		print("Error: PCK file '" + src_file + "' does not exist")
		return "Error: PCK file '" + src_file + "' does not exist"
	var existing_pck_files = load_pck([src_file], true, includes, excludes, enc_key)
	if (existing_pck_files.size() == 0):
		print("Error: failed to load PCK file")
		return "Error: failed to load PCK file"
	var pack_infos = GDRESettings.get_pack_info_list()
	if (pack_infos.is_empty()):
		print("Error: no PCK existing_pck_files loaded")
		return "Error: no PCK files loaded"
	if (pack_infos.size() > 1):
		print("Error: multiple PCK existing_pck_files loaded, specify which one to patch")
		return "Error: multiple PCK files loaded, specify which one to patch"

	if (pack_infos[0].get_type() != 0 and pack_infos[0].get_type() != 4):
		print("Error: file is not a PCK or EXE")
		return "Error: file is not a PCK or EXE"

	var reverse_map:Dictionary[String, String] = {}
	for key in patch_file_map.keys():
		reverse_map[patch_file_map[key]] = key
	for pck_file in existing_pck_files:
		if (reverse_map.has(pck_file) or reverse_map.has(pck_file.trim_prefix("res://"))):
			continue
		if (pck_file.is_relative_path()):
			pck_file = "res://" + pck_file
		patch_file_map[pck_file] = pck_file
	var pck_patcher = _start_patch_pck(dest_pck, pack_infos[0])
	pck_patcher.set_multi_thread(false)
	var err = pck_patcher.add_files(patch_file_map)
	if (err != OK):
		print("Error: failed to add files to patch PCK: " + pck_patcher.get_error_message())
		return "Error: failed to add files to patch PCK: " + pck_patcher.get_error_message()
	err = pck_patcher.finish_pck()
	if (err != OK):
		print("Error: failed to write patching PCK:" + pck_patcher.get_error_message())
		return "Error: failed to finish patching PCK:" + pck_patcher.get_error_message()
	print("Patched PCK file: " + dest_pck)
	return ""


func _on_gdre_patch_pck_do_patch_pck(dest_pck: String, file_map: Dictionary[String, String]) -> void:
	PATCH_PCK_DIALOG.hide_win()
	var pack_infos = GDRESettings.get_pack_info_list()
	if (pack_infos.is_empty()):
		GDRESettings.unload_project()
		GDREChildDialog.popup_box(self, ERROR_DIALOG, "Error: no PCK files found, cannot patch", "Error")
		return
	if (pack_infos.size() > 1):
		GDRESettings.unload_project()
		GDREChildDialog.popup_box(self, ERROR_DIALOG, "Error: multiple PCK files found, cannot patch", "Error")
		return
	var pck_creator = _start_patch_pck(dest_pck, pack_infos[0])
	var err = pck_creator.add_files(file_map)
	if (err != OK):
		GDRESettings.unload_project()
		GDREChildDialog.popup_box(self, ERROR_DIALOG, "Error: failed to add files to PCK:\n" + pck_creator.get_error_message(), "Error")
		return
	err = pck_creator.finish_pck()
	if (err != OK):
		GDRESettings.unload_project()
		GDREChildDialog.popup_box(self, ERROR_DIALOG, "Error: failed to write PCK:\n" + pck_creator.get_error_message(), "Error")
		return
	GDRESettings.unload_project()
	GDREChildDialog.popup_box(self, ERROR_DIALOG, "PCK patching complete", "Success")
	pass # Replace with function body.
