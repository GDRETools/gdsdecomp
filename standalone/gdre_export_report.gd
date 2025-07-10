extends Window

const file_icon: Texture2D = preload("res://gdre_icons/gdre_File.svg")
const file_ok: Texture2D = preload("res://gdre_icons/gdre_FileOk.svg")
const file_broken: Texture2D = preload("res://gdre_icons/gdre_FileBroken.svg")

var NOTE_TREE : Tree = null
var TOTALS_TREE: Tree = null
var EDITOR_MESSAGE_LABEL: RichTextLabel = null
var LOG_FILE_LABEL: RichTextLabel = null
var editor_message_default_text: String = ""
var log_file_default_text: String = ""
var recovery_folder: String = ""

# var isHiDPI = DisplayServer.screen_get_dpi() >= 240
var isHiDPI = false
var root: TreeItem = null
var userroot: TreeItem = null
var num_files:int = 0
var num_broken:int = 0
var num_malformed:int = 0
var _is_test:bool = false
var report: ImportExporterReport = null

const skippable_keys: PackedStringArray = ["rewrote_metadata", "failed_rewrite_md5"]


signal report_done()

enum TotalsTreeButton {
	DOWNLOAD_URL
}

func _on_totals_tree_button_clicked(item: TreeItem, _column: int, id: int, mouse_button_index: int):
	if (mouse_button_index != MOUSE_BUTTON_LEFT):
		return
	match id:
		TotalsTreeButton.DOWNLOAD_URL:
			OS.shell_open(item.get_text(_column))
	pass
# MUST CALL set_root_window() first!!!
# Called when the node enters the scene tree for the first time.
func _ready():
	NOTE_TREE = 	 %NoteTree
	TOTALS_TREE =    %TotalsTree
	EDITOR_MESSAGE_LABEL = %EditorMessageLabel
	LOG_FILE_LABEL = %LogFileLabel
	editor_message_default_text = EDITOR_MESSAGE_LABEL.text
	log_file_default_text = LOG_FILE_LABEL.text

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

	if _is_test:
		load_test()
	TOTALS_TREE.connect("button_clicked", self._on_totals_tree_button_clicked)

	pass # Replace with function body.

func _on_click_uri(meta):
	OS.shell_open(meta)

func ver_to_tag(ver:GodotVer):
	var tag_str = String.num_uint64(ver.major) + "." + String.num_uint64(ver.minor)
	if (ver.patch != 0):
		tag_str += "." + String.num_uint64(ver.patch)
	if !ver.prerelease.is_empty() && ver.is_valid_semver():
		tag_str += "-" + ver.prerelease
	else:
		tag_str += "-stable"
	return tag_str

func path_to_uri(path:String):
	var uri = "file://"
	if (!path.simplify_path().begins_with("/")):
		uri += "/"
	uri += path.simplify_path()
	return uri

func get_url_for_tag(tag: String, is_steam_release: bool = false):
	if is_steam_release: # don't bother with the tag here
		return "https://github.com/CoaguCo-Industries/GodotSteam/releases"
	else:
		return "https://github.com/godotengine/godot-builds/releases/tag/" + tag

func load_test():
	add_report_sections(TEST_REPORT, {})

func add_ver_string(ver_string: String):
	var ver = GodotVer.parse_godotver(ver_string)
	var tag = ver_to_tag(ver)
	if report.is_steam_detected():
		ver_string += " (Steam edition)"
	EDITOR_MESSAGE_LABEL.text = EDITOR_MESSAGE_LABEL.text.replace("<GODOT_VER>", "[url=" + get_url_for_tag(tag) + "]"+ ver_string + "[/url]")

func add_log_file(log_path: String):
	recovery_folder = log_path.get_base_dir()
	var uri = path_to_uri(log_path)
	LOG_FILE_LABEL.text = LOG_FILE_LABEL.text.replace("<LOG_FILE_URI>", "[url=" + uri + "]" + log_path + "[/url]")

# called before _ready
func set_root_window(window: Window):
	pass

func show_win():
	# get the screen size
	var safe_area: Rect2i = DisplayServer.get_display_safe_area()
	var center = (safe_area.position + safe_area.size - self.size) / 2
	self.set_position(center)
	self.show()

func hide_win():
	self.hide()

func get_note_header_item_icon(_key: String) -> Texture2D:
	# TODO: add warning and info icons
	#var warning_icon = POPUP_PARENT_WINDOW.get_theme_icon("Warning", "EditorIcons")
	#var info_icon = POPUP_PARENT_WINDOW.get_theme_icon("Info", "EditorIcons")
	#if key == "unsupported_types":
		#return warning_icon
	#elif key == "translation_export_message":
		#return warning_icon
	#elif key == "failed_plugins":
		#return warning_icon
	#elif key == "godot_2_assets":
		#return info_icon
	return null


func add_dictionary_item(parent_key: TreeItem, item_name: String, dict: Dictionary):
	var item = TOTALS_TREE.create_item(parent_key)
	item.set_text(0, item_name)
	item.set_text(1, "")
	for subkey in dict.keys():
		var thing = dict[subkey]
		if typeof(thing) == TYPE_DICTIONARY:
			add_dictionary_item(item, subkey, thing)
		else:
			var subitem = TOTALS_TREE.create_item(item)
			subitem.set_text(0, subkey)
			if typeof(thing) == TYPE_PACKED_STRING_ARRAY:
				var arr: PackedStringArray = thing
				subitem.set_text(1, String.num_uint64(arr.size()))
				for i in range(arr.size()):
					var subsubitem = TOTALS_TREE.create_item(subitem)
					subsubitem.set_text(0, arr[i])
					subsubitem.set_text(1, "")
			else:
				subitem.set_text(1, str(thing))

func add_report(rep: ImportExporterReport) -> int:
	self.clear()
	self.report = rep
	add_ver_string(report.get_ver())
	add_log_file(report.get_log_file_location())
	var notes = report.get_session_notes()
	var report_sections: Dictionary = report.get_report_sections()
	var report_labels: Dictionary = report.get_section_labels()

	add_notes(notes)
	add_report_sections(report_sections, report_labels)
	# iterate over all the keys in the notes
	# add fake root
	return OK

func add_notes(notes: Dictionary):
	var note_root = NOTE_TREE.create_item(null)
	for key in notes.keys():
		var note_dict = notes[key]
		var header_item = NOTE_TREE.create_item(note_root)
		header_item.set_text(0, note_dict["title"])
		var message_item = NOTE_TREE.create_item(header_item)
		message_item.set_text(0, note_dict["message"])
		if note_dict.has("details"):
			for item in note_dict["details"]:
				var subitem = NOTE_TREE.create_item(header_item)
				subitem.set_text(0, item)



func add_report_sections(report_sections: Dictionary, report_labels: Dictionary):
	var report_root = TOTALS_TREE.create_item(null)

	for key in report_sections.keys():
		if skippable_keys.has(key):
			continue
		var section:Variant = report_sections[key]
		var header_item = TOTALS_TREE.create_item(report_root)
		header_item.set_text(0, report_labels.get(key, key))
		# check that section is actually a dictionary
		if typeof(section) == TYPE_DICTIONARY:
			var dict :Dictionary = section
			header_item.set_text(1, String.num_uint64(dict.keys().size()))
			# iterate over all the keys in the section
			if key == "downloaded_plugins":
				for subkey in dict.keys():
					var plugin_info: Dictionary = dict[subkey]
					var release_info: Dictionary = plugin_info["release_info"]
					var version: String = release_info["version"]
					var download_url: String = release_info["download_url"]
					var subitem = TOTALS_TREE.create_item(header_item)
					subitem.set_text(0, subkey)
					subitem.set_text(1, version)
					var subsubitem: TreeItem = TOTALS_TREE.create_item(subitem)
					subsubitem.set_text(0, "Download URL:")
					subsubitem.set_text(1, download_url)
					subsubitem.add_button(1, file_icon, TotalsTreeButton.DOWNLOAD_URL, false, "Open download URL")
			else:
				for subkey in dict.keys():
					var subitem = TOTALS_TREE.create_item(header_item)
					subitem.set_text(1, subkey)
					subitem.set_text(0, dict[subkey])
		elif typeof(section) == TYPE_PACKED_STRING_ARRAY:
			var arr: PackedStringArray = section
			header_item.set_text(1, String.num_uint64(arr.size()))
			for i in range(arr.size()):
				var subitem = TOTALS_TREE.create_item(header_item)
				subitem.set_text(0, arr[i])
				subitem.set_text(1, "")
		else:
			print("Unknown type for section: " + String.num(typeof(section)))
			continue
		header_item.set_collapsed_recursive(true)




func clear():
	NOTE_TREE.clear()
	TOTALS_TREE.clear()
	EDITOR_MESSAGE_LABEL.text = editor_message_default_text
	LOG_FILE_LABEL.text = log_file_default_text
	report = null

func close():
	_exit_tree()
	emit_signal("report_done")

func cancel_extract():
	close()

func _open_folder():
	OS.shell_open(path_to_uri(recovery_folder))

func confirmed():
	close()

func cancelled():
	close()

func _close_requested():
	close()

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta):
	pass

func _exit_tree():
	hide_win()






var TEST_REPORT = {
 "lossy_imports": {
  "res://.godot/imported/foo.svg-452efe7dd3f28f4f1fe0cc1124dba826.ctex": "res://control_panel/foo.svg",
 },
 "rewrote_metadata": {
  "res://.godot/imported/f.png-d381692a53860f07d51d71fc9e712219.ctex": "res://addons/cs-fetch/foo.png",
 },
 "downloaded_plugins": {
  "godotgif": {
   "cache_version": 1,
   "plugin_name": "godotgif",
   "release_info": {
	"plugin_source": "github",
	"primary_id": 129561407,
	"secondary_id": 135676324,
	"version": "1.0.1",
	"engine_ver_major": 0,
	"release_date": "2023-11-15T14:40:41Z",
	"download_url": "https://github.com/BOTLANNER/godot-gif/releases/download/1.0.1/godotgif.zip"
   },
   "min_godot_version": "4.1",
   "max_godot_version": "",
   "base_folder": "",
   "gdexts": [
	{
	 "relative_path": "addons/godotgif/godotgif.gdextension",
	 "min_godot_version": "4.1",
	 "max_godot_version": "",
	 "bins": [
	  {
	   "name": "bin/godotgif.macos.template_debug.framework",
	   "md5": "36e578fb1a624a4fefa14f4fde008250",
	   "tags": [
		"macos",
        "debug"
	   ]
	  },
	  {
	   "name": "bin/godotgif.macos.template_release.framework",
	   "md5": "23112c353920456c47cf2cee79ea4402",
	   "tags": [
		"macos",
        "release"
	   ]
	  },
	  {
	   "name": "bin/godotgif.windows.template_debug.x86_32.dll",
	   "md5": "f72a606b2432c428acc69f86025ea96b",
	   "tags": [
		"windows",
		"debug",
        "x86_32"
	   ]
	  },
	  {
	   "name": "bin/godotgif.windows.template_release.x86_32.dll",
	   "md5": "77c47c999cfc1d7961efe0a5c5d495b5",
	   "tags": [
		"windows",
		"release",
        "x86_32"
	   ]
	  },
	  {
	   "name": "bin/godotgif.windows.template_debug.x86_64.dll",
	   "md5": "08d5899192c4ab7ce119c86e81c0f83c",
	   "tags": [
		"windows",
		"debug",
        "x86_64"
	   ]
	  },
	  {
	   "name": "bin/godotgif.windows.template_release.x86_64.dll",
	   "md5": "289c5d0667923438011e7033eee598ab",
	   "tags": [
		"windows",
		"release",
        "x86_64"
	   ]
	  },
	  {
	   "name": "bin/libgodotgif.linux.template_debug.x86_64.so",
	   "md5": "5d4b197dfa69a8876158efd899654af6",
	   "tags": [
		"linux",
		"debug",
        "x86_64"
	   ]
	  },
	  {
	   "name": "bin/libgodotgif.linux.template_release.x86_64.so",
	   "md5": "f5f388dc570904c08f7dfde417e9a378",
	   "tags": [
		"linux",
		"release",
        "x86_64"
	   ]
	  },
	  {
	   "name": "bin/libgodotgif.linux.template_debug.arm64.so",
	   "md5": "",
	   "tags": [
		"linux",
		"debug",
        "arm64"
	   ]
	  },
	  {
	   "name": "bin/libgodotgif.linux.template_release.arm64.so",
	   "md5": "",
	   "tags": [
		"linux",
		"release",
        "arm64"
	   ]
	  },
	  {
	   "name": "bin/libgodotgif.linux.template_debug.rv64.so",
	   "md5": "",
	   "tags": [
		"linux",
		"debug",
        "rv64"
	   ]
	  },
	  {
	   "name": "bin/libgodotgif.linux.template_release.rv64.so",
	   "md5": "",
	   "tags": [
		"linux",
		"release",
        "rv64"
	   ]
	  },
	  {
	   "name": "bin/godotgif.android.template_debug.x86_64.so",
	   "md5": "",
	   "tags": [
		"android",
		"debug",
        "x86_64"
	   ]
	  },
	  {
	   "name": "bin/godotgif.android.template_release.x86_64.so",
	   "md5": "",
	   "tags": [
		"android",
		"release",
        "x86_64"
	   ]
	  },
	  {
	   "name": "bin/godotgif.android.template_debug.arm64.so",
	   "md5": "",
	   "tags": [
		"android",
		"debug",
        "arm64"
	   ]
	  },
	  {
	   "name": "bin/godotgif.android.template_release.arm64.so",
	   "md5": "",
	   "tags": [
		"android",
		"release",
        "arm64"
	   ]
	  }
	 ],
	 "dependencies": []
	}
   ]
  }
 },
 "success": {
  "res://addons/godotgif/godotgif.gdextension": "res://addons/godotgif/godotgif.gdextension",
 },
 "decompiled_scripts": {

 }
}
