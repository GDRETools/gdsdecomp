class_name GDRENewPck
extends GDREAcceptDialogBase

const file_icon: Texture2D = preload("res://gdre_icons/gdre_File.svg")
const file_ok: Texture2D = preload("res://gdre_icons/gdre_FileOk.svg")
const file_broken: Texture2D = preload("res://gdre_icons/gdre_FileBroken.svg")
const gdre_export_report = preload("res://gdre_export_report.tscn")

var NEW_PCK_WINDOW :Window = null
var POPUP_PARENT_WINDOW : Window = null

var DIRECTORY_DIALOG : FileDialog = null
var EMBED_SOURCE_DIALOG : FileDialog = null
var SAVE_DIALOG : FileDialog = null

var CONTAINER: Control = null
var DIRECTORY: LineEdit = null
var ENCRYPT: CheckButton = null
var EMBED: CheckButton = null
var EMBED_SOURCE: LineEdit = null
var VER_MAJOR: SpinBox = null
var VER_MINOR: SpinBox = null
var VER_PATCH: SpinBox = null
var VERSION: OptionButton = null
var EXTRA_TAG: LineEdit = null
var INCLUDES: LineEdit = null
var EXCLUDES: LineEdit = null
var SAVE_BUTTON: Button = null
var ERROR_TEXT: RichTextLabel = null
var DESKTOP_DIR = OS.get_system_dir(OS.SystemDir.SYSTEM_DIR_DESKTOP)


var ver_info = Engine.get_version_info()
# TODO: make this use something dynamic (GDRESettings?)
const PCK_VERSION_DEFAULT = 2
var DEFAULT_VER_MAJOR = ver_info["major"]
var DEFAULT_VER_MINOR = ver_info["minor"]
var DEFAULT_VER_PATCH = ver_info["patch"]

# var isHiDPI = DisplayServer.screen_get_dpi() >= 240
var isHiDPI = false
var root: TreeItem = null
var userroot: TreeItem = null
var num_files:int = 0
var num_broken:int = 0
var num_malformed:int = 0
var _is_test:bool = false
var _file_dialog: FileDialog = null

signal save_pck_requested(path: String)

func instance_things():
	NEW_PCK_WINDOW = $"."
	CONTAINER = $Control
	DIRECTORY = $Control/Directory
	ENCRYPT = $Control/Encrypt
	EMBED = $Control/Embed
	EMBED_SOURCE = $Control/EmbedSource
	VER_MAJOR = $Control/VerMajor
	VER_MINOR = $Control/VerMinor
	VER_PATCH = $Control/VerPatch
	VERSION = $Control/Version
	EXTRA_TAG = $Control/ExtraTag
	INCLUDES = $Control/IncludeFilters
	EXCLUDES = $Control/ExcludeFilters
	SAVE_BUTTON = NEW_PCK_WINDOW.get_ok_button()
	ERROR_TEXT = $Control/ErrorText
	DIRECTORY_DIALOG = $DirectoryDialog
	EMBED_SOURCE_DIALOG = $EmbedSourceDialog
	SAVE_DIALOG = $SaveDialog
	DIRECTORY_DIALOG.current_dir = DESKTOP_DIR
	EMBED_SOURCE_DIALOG.current_dir = DESKTOP_DIR
	VERSION.select(PCK_VERSION_DEFAULT)
	VER_MAJOR.value = DEFAULT_VER_MAJOR
	VER_MINOR.value = DEFAULT_VER_MINOR
	VER_PATCH.value = DEFAULT_VER_PATCH


# MUST CALL set_root_window() first!!!
# Called when the node enters the scene tree for the first time.
func _ready():
	instance_things()
	clear()
	_verify(null)

# called before _ready
func set_root_window(window: Window):
	POPUP_PARENT_WINDOW = window
	_is_test = false

func show_window(window: Window):
	# get the screen size
	var safe_area: Rect2i = DisplayServer.get_display_safe_area()
	var center = (safe_area.position + safe_area.size - NEW_PCK_WINDOW.size) / 2
	window.set_position(center)
	window.set_exclusive(true)
	window.show()


func show_win():
	# get the screen size
	show_window(NEW_PCK_WINDOW)

func hide_win():
	NEW_PCK_WINDOW.hide()

func clear():
	pass

func _report_done():
	# print("REPORT DONE WITHOUT REPORT_DIALOG?!?!")
	close()

func close():
	# _exit_tree()
	hide_win()

func cancel_extract():
	close()

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta):
	pass

func _verify(_val = null) -> void:
	var errors: PackedStringArray = []
	if DIRECTORY.text.is_empty():
		errors.append("Directory is not selected")
	if EMBED.is_pressed() and EMBED_SOURCE.text.is_empty():
		errors.append("Embed source is empty")
	if errors.size() > 0:
		SAVE_BUTTON.disabled = true
		ERROR_TEXT.text = "[color=#FF0000]ERROR:[/color]"
		ERROR_TEXT.visible = true
		for err in errors:
			ERROR_TEXT.text += "\n" + "[color=#FF0000]" + err + "[/color]"
	else:
		ERROR_TEXT.visible = false
		SAVE_BUTTON.disabled = false
	pass # Replace with function body.


func _on_directory_select_pressed() -> void:
	DIRECTORY_DIALOG.popup_centered()

func _on_directory_dialog_dir_selected(dir: String) -> void:
	DIRECTORY.text = dir
	_verify()

func _on_embed_source_select_pressed() -> void:
	EMBED_SOURCE_DIALOG.filename_filter = "*.exe,*.bin,*.32,*.64;Self contained executable files"
	EMBED_SOURCE_DIALOG.popup_centered()

func _on_embed_source_dialog_file_selected(dir: String) -> void:
	EMBED_SOURCE.text = dir
	_verify()


func _on_save_pressed() -> void:
	var text = DIRECTORY.text.get_file().get_basename()
	if EMBED.is_pressed():
		SAVE_DIALOG.filename_filter = "*.exe,*.bin,*.32,*.64;Self contained executable files"
		if (OS.get_name() == "Windows"):
			text = text + ".exe"
		else:
			text = text + ".64"
	else:
		SAVE_DIALOG.filename_filter = "*.pck;PCK files"
		text = text + ".pck"
	SAVE_DIALOG.current_dir = DIRECTORY.text.get_base_dir()
	SAVE_DIALOG.current_file = text
	SAVE_DIALOG.popup_centered()

func _on_save_dialog_file_selected(path: String) -> void:
	SAVE_DIALOG.hide()
	self.call_on_next_process(func(): _do_save(path))

func _do_save(path: String):
	# remove extra extension that macos adds
	var ext = path.get_extension()
	var dot_ext = "." + ext
	if (path.ends_with(dot_ext + dot_ext)):
		path = path.trim_suffix(dot_ext + dot_ext) + dot_ext

	emit_signal("save_pck_requested", path)
	# var creator = PckCreator.new()
	# creator.encrypt = ENCRYPT.is_pressed()
	# creator.embed = EMBED.is_pressed()
	# creator.exe_to_embed = EMBED_SOURCE.text
	# creator.ver_major = VER_MAJOR.get_value()
	# creator.ver_minor = VER_MINOR.get_value()
	# creator.ver_rev = VER_PATCH.get_value()
	# creator.pack_version = VERSION.get_value()
	# creator.watermark = EXTRA_TAG.text
	# var includes = INCLUDES.text.split(",")
	# var excludes = EXCLUDES.text.split(",")
	# creator.pck_create(path, DIRECTORY.text, includes, excludes)
	# close()



func _on_close_requested() -> void:
	close()
