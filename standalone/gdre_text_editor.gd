class_name GDRETextEditor
extends CodeEdit

var CODE_VIWER_OPTIONS: MenuButton = null
var CODE_VIWER_OPTIONS_POPUP: PopupMenu = null
var CODE_VIEWER: CodeEdit = null
const SAMPLE_TEXT = '''@tool # this is a tool
class_name FOO
extends Node
@export var string_prop: String = "haldo"
func thingy():
	var foo = 1
	var bl = true
	var bar = "var"
	var baz: Color = Color(0.1,0.2,0.3)
	var far: float = baz.r
	var string_name: StringName = &"haldo"
	var barf: NodePath = ^"farts/lmao"
	$Cam / Camera2D.drag_margin_left = margin
	$Cam / Camera2D.drag_margin_right = margin
	var node_id = $Path/To/Node
	var node_id_2 = $Path/To/"A"/Node/With/"Some Spaces"/In/It.thing()
# this is a really long comment that is going to wrap around the text box!
	lerp()
	pass
#  haldodelimiter_comments
# NOTE: This is a note
# BUG: This is a warning
# ALERT: this is a critical
'''

const GDRESOURCE_COMMENTS = [";"]
const GDSHADER_COMMENTS = ["//", "/* */"]
const GDSCRIPT_COMMENTS = ["#"]

var show_tabs_popup_id: int = 0
var show_spaces_popup_id: int = 0
var word_wrap_popup_id: int = 0
var text_size_plus_id: int = 0
var text_size_minus_id: int = 0
var find_popup_id: int = 0
var replace_popup_id: int = 0
var find_next_popup_id: int = 0
var find_prev_popup_id: int = 0

var current_path: String = ""

var last_editable: bool = false

enum HighlightType {
	UNKNOWN = -1,
	TEXT = 0,
	GDSCRIPT,
	GDRESOURCE,
	INI_LIKE,
	GDSHADER,
	JSON_TEXT,
	CSHARP,
	XML
}

var current_type: HighlightType = HighlightType.UNKNOWN

func _add_regions_to_gdscript_highlighter():
	# Not working, if it ends in a newline, the color wraps to the next line
	gdscript_highlighter.add_color_region("@", " ", Color("#ffb373"))
	gdscript_highlighter.add_color_region("$", " ", Color("#63c259"))

@export var gdscript_highlighter: CodeHighlighter = preload("res://gdre_code_highlighter.tres"):
	set(val):
		if CODE_VIEWER.syntax_highlighter == gdscript_highlighter:
			CODE_VIEWER.syntax_highlighter = val
		gdscript_highlighter = val
		# _add_regions_to_gdscript_highlighter()
	get:
		return gdscript_highlighter
@export var gdresource_highlighter: CodeHighlighter = preload("res://gdre_gdresource_highlighter.tres"):
	set(val):
		if CODE_VIEWER.syntax_highlighter == gdresource_highlighter:
			CODE_VIEWER.syntax_highlighter = val
		gdresource_highlighter = val
	get:
		return gdresource_highlighter

@export var gdshader_highlighter: CodeHighlighter = preload("res://gdre_gdshader_highlighter.tres"):
	set(val):
		if CODE_VIEWER.syntax_highlighter == gdshader_highlighter:
			CODE_VIEWER.syntax_highlighter = val
		gdshader_highlighter = val
	get:
		return gdshader_highlighter

@export var json_highlighter: CodeHighlighter = preload("res://gdre_json_highlighter.tres"):
	set(val):
		if CODE_VIEWER.syntax_highlighter == json_highlighter:
			CODE_VIEWER.syntax_highlighter = val
		json_highlighter = val
	get:
		return json_highlighter

@export var csharp_highlighter: CodeHighlighter = preload("res://gdre_csharp_highlighter.tres"):
	set(val):
		if CODE_VIEWER.syntax_highlighter == csharp_highlighter:
			CODE_VIEWER.syntax_highlighter = val
		csharp_highlighter = val
	get:
		return csharp_highlighter

@export var xml_highlighter: GDREXMLHighlighter = preload("res://gdre_xml_highlighter.tres"):
	set(val):
		if CODE_VIEWER.syntax_highlighter == xml_highlighter:
			CODE_VIEWER.syntax_highlighter = val
		xml_highlighter = val
	get:
		return xml_highlighter

@onready var font_size = get_theme_font_size("TextEdit")

var code_opts_panel_button_icon = preload("res://gdre_icons/gdre_GuiTabMenuHl.svg")

var find_replace_bar: GDREFindReplaceBar = null

func _init():
	CODE_VIEWER = self

	# _add_regions_to_gdscript_highlighter()
	last_editable = self.editable

	var code_opts_box = Control.new()
	code_opts_box.anchor_left = 1.0
	code_opts_box.anchor_top = 0
	code_opts_box.anchor_right = 1.0
	code_opts_box.anchor_bottom = 0
	code_opts_box.offset_left = -39
	code_opts_box.offset_top = 6
	code_opts_box.offset_right = -12
	code_opts_box.offset_bottom = 33
	code_opts_box.grow_horizontal = 0 as Control.GrowDirection

	var code_opts_panel_button = Button.new()
	code_opts_panel_button.theme_type_variation = &"CodeOptsButton"
	code_opts_panel_button.layout_mode = 1
	code_opts_panel_button.anchors_preset = 15
	code_opts_panel_button.anchor_right = 1.0
	code_opts_panel_button.anchor_bottom = 1.0
	code_opts_panel_button.grow_horizontal = 2 as Control.GrowDirection
	code_opts_panel_button.grow_vertical = 2 as Control.GrowDirection
	code_opts_panel_button.pressed.connect(self._on_code_viewer_options_pressed)

	CODE_VIWER_OPTIONS = MenuButton.new()
	CODE_VIWER_OPTIONS.layout_mode = 1
	CODE_VIWER_OPTIONS.anchors_preset = -1
	CODE_VIWER_OPTIONS.anchor_right = 1.0
	CODE_VIWER_OPTIONS.anchor_bottom = 1.0
	CODE_VIWER_OPTIONS.offset_left = -0.5
	CODE_VIWER_OPTIONS.offset_right = 0.5
	CODE_VIWER_OPTIONS.grow_horizontal = 2 as Control.GrowDirection
	CODE_VIWER_OPTIONS.grow_vertical = 2 as Control.GrowDirection
	CODE_VIWER_OPTIONS.theme_type_variation = &"FlatMenuButton"
	CODE_VIWER_OPTIONS.icon = code_opts_panel_button_icon
	CODE_VIWER_OPTIONS.icon_alignment = 1 as HorizontalAlignment

	find_replace_bar = GDREFindReplaceBar.new()
	find_replace_bar.set_text_edit(self)
	find_replace_bar.replace_enabled = self.editable
	find_replace_bar.show_panel_background = true
	find_replace_bar.hide()
	find_replace_bar.anchor_left = 1.0
	find_replace_bar.anchor_top = 0
	find_replace_bar.anchor_right = 1.0
	find_replace_bar.anchor_bottom = 0
	find_replace_bar.offset_left = -39
	find_replace_bar.offset_top = 0
	find_replace_bar.offset_right = -50
	find_replace_bar.offset_bottom = 33
	find_replace_bar.grow_horizontal = Control.GrowDirection.GROW_DIRECTION_BEGIN
	add_child(find_replace_bar)

	var menu: PopupMenu = CODE_VIEWER.get_menu()
	var idx = menu.item_count
	menu.add_separator("", idx)
	show_tabs_popup_id = idx + 1
	show_spaces_popup_id = idx + 2
	word_wrap_popup_id = idx + 3
	text_size_plus_id = idx + 5
	text_size_minus_id = idx + 6
	find_popup_id = idx + 7
	replace_popup_id = idx + 8
	find_next_popup_id = idx + 9
	find_prev_popup_id = idx + 10

	_add_items_to_popup_menu(menu)
	CODE_VIWER_OPTIONS_POPUP = CODE_VIWER_OPTIONS.get_popup()
	_add_items_to_popup_menu(CODE_VIWER_OPTIONS_POPUP)

	CODE_VIWER_OPTIONS_POPUP = CODE_VIWER_OPTIONS.get_popup()


	code_opts_box.add_child(code_opts_panel_button)
	code_opts_box.add_child(CODE_VIWER_OPTIONS)
	add_child(code_opts_box)
	set_text_viewer_props()

func _ready():
	set_highlight_type(HighlightType.UNKNOWN)

func reset():
	current_path = ""
	set_viewer_text("")
	set_highlight_type(HighlightType.UNKNOWN)
	pass

func reload_from_disk():
	if current_path.is_empty():
		return
	var h_scroll = CODE_VIEWER.scroll_horizontal
	var v_scroll = CODE_VIEWER.scroll_vertical
	var caret_line = CODE_VIEWER.get_caret_line()
	var caret_column = CODE_VIEWER.get_caret_column()
	load_path(current_path, current_type)
	CODE_VIEWER.scroll_horizontal = h_scroll
	CODE_VIEWER.scroll_vertical = v_scroll
	CODE_VIEWER.set_caret_line(caret_line, false)
	CODE_VIEWER.set_caret_column(caret_column, false)

func set_viewer_text(text: String):
	# This is a workaround for a bug in CodeEdit where setting the text property throws errors when wrapping is enabled
	if CODE_VIEWER.wrap_mode == TextEdit.LINE_WRAPPING_BOUNDARY:
		CODE_VIEWER.wrap_mode = TextEdit.LINE_WRAPPING_NONE
		CODE_VIEWER.text = ""
		CODE_VIEWER.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
		# detect whether or not we should force-disable wrapping
	var prev_line = text.find("\n")
	var next_line = text.find("\n", prev_line + 1)
	var disabled = false
	var too_long = false
	while next_line != -1:
		if (next_line - prev_line) > 4000: # greater than 4000 characters really chugs the editor when wrapping
			CODE_VIEWER.wrap_mode = TextEdit.LINE_WRAPPING_NONE
			disabled = true
		if (next_line - prev_line) > 12000:
			too_long = true
			break
		prev_line = next_line
		next_line = text.find("\n", prev_line + 1)
	if too_long:
		# split it into lines, truncate lines > 12000 characters
		var lines = text.split("\n")
		var truncated_text = ""
		for line in lines:
			if line.length() > 12000:
				var truncated_suffix = " <TRUNCATED...>"
				var truncated_line = line.left(12000)
				# prevent highlighter from breaking on the truncated line
				if (truncated_line.count("\"") % 2 != 0):
					truncated_suffix += "\""
				truncated_text += truncated_line + truncated_suffix + "\n"
			else:
				truncated_text += line + "\n"
		text = truncated_text
	if disabled:
		disable_word_wrap_option()
	else:
		enable_word_wrap_option()
	CODE_VIEWER.text = text
	CODE_VIEWER.scroll_vertical = 0
	CODE_VIEWER.scroll_horizontal = 0
	if find_replace_bar.visible:
		find_replace_bar.refresh_search()

func load_code(path, override_bytecode_revision: int = 0) -> bool:
	var code_text = ""
	current_path = path
	var ext = path.get_extension().to_lower()
	if ext == "cs":
		# try to load the literal file first; if it's empty, try to decompile it from the assembly
		code_text = FileAccess.get_file_as_string(path)
		if code_text.strip_edges().is_empty():
			if GDRESettings.has_loaded_dotnet_assembly():
				var decompiler = GDRESettings.get_dotnet_decompiler()
				code_text = decompiler.decompile_individual_file(path)
				set_highlight_type(HighlightType.CSHARP)
			else:
				code_text = "Error loading script:\nNo .NET assembly loaded"
				set_highlight_type(HighlightType.TEXT)
		else:
			set_highlight_type(HighlightType.CSHARP)
	elif ext == "gde" or ext == "gdc":
		var script: FakeGDScript = FakeGDScript.new()
		if (override_bytecode_revision != 0):
			script.set_override_bytecode_revision(override_bytecode_revision)
		script.load_source_code(path)
		if not script.get_error_message().is_empty():
			code_text = "Error loading script:\n" + script.get_error_message()
			set_highlight_type(HighlightType.TEXT)
		else:
			code_text = script.get_source_code()
			set_highlight_type(HighlightType.GDSCRIPT)
	else: # ext == "gd"
		code_text = FileAccess.get_file_as_string(path)
		set_highlight_type(HighlightType.GDSCRIPT)

	set_viewer_text(code_text)
	return true

func load_text_resource(path):
	current_path = path
	set_highlight_type(HighlightType.GDRESOURCE)
	set_viewer_text(ResourceCompatLoader.resource_file_to_string(path))
	return true

func load_text(path: String, text: String, type: HighlightType = HighlightType.UNKNOWN):
	if type == HighlightType.UNKNOWN:
		type = recognize(path)
	current_path = path
	set_highlight_type(type)
	set_viewer_text(text)
	return true

func load_text_string(text):
	current_path = ""
	set_highlight_type(HighlightType.TEXT)
	set_viewer_text(text)
	return true

func is_shader(ext, p_type = ""):
	if (ext == "shader" || ext == "gdshader" || ext == "gdshaderinc"):
		return true
	return false

func is_gdscript(ext, p_type = ""):
	if (ext == "gd" || ext == "gdc" || ext == "gde"):
		return true
	return false

func is_csharp_script(ext, p_type = ""):
	if (ext == "cs"):
		return true
	return false

func is_text(ext, p_type = ""):
	if (ext == "txt" || ext == "csv" || ext == "html" || ext == "md" || ext == "yml" || ext == "yaml"):
		return true
	return false

func is_xml(ext, p_type = ""):
	return ext == "xml"

func is_text_resource(ext, p_type = ""):
	return ext == "tscn" || ext == "tres"

func is_ini_like(ext, p_type = ""):
	return ext == "cfg" || ext == "remap" || ext == "import" || ext == "gdextension" || ext == "gdnative" || ext == "godot"

func is_binary_project_settings(path):
	return path.get_file().to_lower() == "engine.cfb" || path.get_file().to_lower() == "project.binary"

func is_json(ext, p_type = ""):
	return ext == "json" || ext == "jsonc"

func is_content_text(path):
	# load up the first 8000 bytes, check if there are any null bytes
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		return false
	var data = file.get_buffer(8000)
	if data.find(0) != -1:
		return false
	if GDRECommon.detect_utf8(data):
		return true
	return false

func recognize(path):
	var ext = path.get_extension().to_lower()
	if (is_shader(ext)):
		return HighlightType.GDSHADER
	elif (is_gdscript(ext)):
		return HighlightType.GDSCRIPT
	elif (is_csharp_script(ext)):
		return HighlightType.CSHARP
	elif (is_binary_project_settings(path) or is_text_resource(ext) or ResourceCompatLoader.handles_resource(path, "")):
		return HighlightType.GDRESOURCE
	elif (is_ini_like(ext)):
		return HighlightType.INI_LIKE
	elif (is_json(ext)):
		return HighlightType.JSON_TEXT
	elif (is_xml(ext)):
		return HighlightType.XML
	elif (is_text(ext)):
		return HighlightType.TEXT
	elif is_content_text(path):
		return HighlightType.TEXT
	return HighlightType.UNKNOWN

func set_highlight_type(type: HighlightType):
	if current_type == type:
		return
	current_type = type
	match current_type:
		HighlightType.GDSHADER:
			set_shader_viewer_props()
		HighlightType.GDSCRIPT:
			set_code_viewer_props()
		HighlightType.CSHARP:
			set_csharp_viewer_props()
		HighlightType.INI_LIKE:
			set_resource_viewer_props()
		HighlightType.GDRESOURCE:
			set_resource_viewer_props()
		HighlightType.JSON_TEXT:
			set_json_viewer_props()
		HighlightType.XML:
			set_xml_viewer_props()
		HighlightType.TEXT:
			set_text_viewer_props()
		_:
			set_text_viewer_props()

func load_path(path, highlight_type: HighlightType = HighlightType.UNKNOWN):
	reset()
	var type = highlight_type if highlight_type != HighlightType.UNKNOWN else recognize(path)
	if type == HighlightType.UNKNOWN:
		return false
	current_path = path
	set_highlight_type(type)
	if type == HighlightType.GDSCRIPT or type == HighlightType.CSHARP:
		load_code(path)
	elif type == HighlightType.GDRESOURCE:
		load_text_resource(path)
	else:
		var text = FileAccess.get_file_as_string(path)
		set_viewer_text(text)
	return true


func set_common_code_viewer_props(is_code: bool):
	CODE_VIEWER.line_folding = is_code
	CODE_VIEWER.gutters_draw_fold_gutter = is_code
	CODE_VIEWER.gutters_draw_line_numbers = is_code
	CODE_VIEWER.auto_brace_completion_highlight_matching = is_code
	CODE_VIEWER.set_tab_size(GDREConfig.get_setting("Script/Indent/size"))


func set_shader_viewer_props():
	CODE_VIEWER.syntax_highlighter = gdshader_highlighter
	set_common_code_viewer_props(true)
	CODE_VIEWER.highlight_all_occurrences = true
	CODE_VIEWER.highlight_current_line = true
	CODE_VIEWER.draw_control_chars = true
	CODE_VIEWER.draw_tabs = true
	CODE_VIEWER.draw_spaces = true
	CODE_VIEWER.delimiter_comments = GDSHADER_COMMENTS

func set_csharp_viewer_props():
	CODE_VIEWER.syntax_highlighter = csharp_highlighter
	set_common_code_viewer_props(true)
	CODE_VIEWER.highlight_all_occurrences = true
	CODE_VIEWER.highlight_current_line = true
	CODE_VIEWER.draw_control_chars = true
	CODE_VIEWER.draw_tabs = true
	CODE_VIEWER.draw_spaces = true
	CODE_VIEWER.delimiter_comments = GDSHADER_COMMENTS

func set_code_viewer_props():
	CODE_VIEWER.syntax_highlighter = gdscript_highlighter
	set_common_code_viewer_props(true)
	CODE_VIEWER.highlight_all_occurrences = true
	CODE_VIEWER.highlight_current_line = true
	CODE_VIEWER.draw_control_chars = true
	CODE_VIEWER.draw_tabs = true
	CODE_VIEWER.draw_spaces = true
	CODE_VIEWER.delimiter_comments = GDSCRIPT_COMMENTS

func set_json_viewer_props():
	CODE_VIEWER.syntax_highlighter = gdscript_highlighter
	set_common_code_viewer_props(true)
	CODE_VIEWER.highlight_all_occurrences = true
	CODE_VIEWER.highlight_current_line = true
	CODE_VIEWER.draw_control_chars = true
	CODE_VIEWER.draw_tabs = false
	CODE_VIEWER.draw_spaces = false
	CODE_VIEWER.delimiter_comments = []

func set_xml_viewer_props():
	CODE_VIEWER.syntax_highlighter = xml_highlighter
	set_common_code_viewer_props(false)
	CODE_VIEWER.highlight_all_occurrences = true
	CODE_VIEWER.highlight_current_line = true
	CODE_VIEWER.draw_control_chars = false
	CODE_VIEWER.draw_tabs = false
	CODE_VIEWER.draw_spaces = false
	CODE_VIEWER.delimiter_comments = []

func set_resource_viewer_props():
	CODE_VIEWER.syntax_highlighter = gdresource_highlighter
	set_common_code_viewer_props(false)
	CODE_VIEWER.highlight_all_occurrences = true
	CODE_VIEWER.highlight_current_line = true
	CODE_VIEWER.draw_control_chars = false
	CODE_VIEWER.draw_tabs = false
	CODE_VIEWER.draw_spaces = false
	CODE_VIEWER.delimiter_comments = GDRESOURCE_COMMENTS

func set_text_viewer_props():
	CODE_VIEWER.syntax_highlighter = null
	set_common_code_viewer_props(false)
	CODE_VIEWER.highlight_all_occurrences = false
	CODE_VIEWER.highlight_current_line = false
	CODE_VIEWER.draw_control_chars = false
	CODE_VIEWER.draw_tabs = false
	CODE_VIEWER.draw_spaces = false
	CODE_VIEWER.delimiter_comments = PackedStringArray()

func reset_popup_menu(menu: PopupMenu) -> void:
	menu.set_item_checked(menu.get_item_index(show_tabs_popup_id),  CODE_VIEWER.draw_tabs)
	menu.set_item_checked(menu.get_item_index(show_spaces_popup_id),  CODE_VIEWER.draw_spaces)
	menu.set_item_checked(menu.get_item_index(word_wrap_popup_id),  CODE_VIEWER.wrap_mode == TextEdit.LINE_WRAPPING_BOUNDARY)

func disable_word_wrap_option():
	CODE_VIEWER.get_menu().set_item_disabled(CODE_VIEWER.get_menu().get_item_index(word_wrap_popup_id), true)
	CODE_VIWER_OPTIONS_POPUP.set_item_disabled(CODE_VIWER_OPTIONS_POPUP.get_item_index(word_wrap_popup_id), true)

func enable_word_wrap_option():
	CODE_VIEWER.get_menu().set_item_disabled(CODE_VIEWER.get_menu().get_item_index(word_wrap_popup_id), false)
	CODE_VIWER_OPTIONS_POPUP.set_item_disabled(CODE_VIWER_OPTIONS_POPUP.get_item_index(word_wrap_popup_id), false)

func set_replace_option_enabled(enabled: bool):
	CODE_VIEWER.get_menu().set_item_disabled(CODE_VIEWER.get_menu().get_item_index(replace_popup_id), enabled)
	CODE_VIWER_OPTIONS_POPUP.set_item_disabled(CODE_VIWER_OPTIONS_POPUP.get_item_index(replace_popup_id), enabled)

func _on_code_viewer_options_pressed(id) -> void:
	if (id == show_tabs_popup_id): # show tabs
		CODE_VIEWER.draw_tabs = not CODE_VIEWER.draw_tabs
	elif (id == show_spaces_popup_id): # Show spaces
		CODE_VIEWER.draw_spaces = not CODE_VIEWER.draw_spaces
	elif (id == word_wrap_popup_id): # Word-wrap
		CODE_VIEWER.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY if CODE_VIEWER.wrap_mode == TextEdit.LINE_WRAPPING_NONE else TextEdit.LINE_WRAPPING_NONE
	elif (id == text_size_plus_id): # Zoom in
		zoom_in()
	elif (id == text_size_minus_id): # Zoom out
		zoom_out()
	elif (id == find_popup_id): # Find
		find_replace_bar.popup_search()
	elif (id == replace_popup_id): # Replace
		find_replace_bar.popup_replace()
	elif true: #(find_replace_bar.visible):
		if (id == find_next_popup_id): # Find Next
			find_replace_bar.search_next()
		elif (id == find_prev_popup_id): # Find Previous
			find_replace_bar.search_prev()
	pass # Replace with function body.

func gen_short_cut(keys, ctrl_pressed = false, alt_pressed = false) -> Shortcut:
	var shortcut = Shortcut.new()
	var events = []
	if not (keys is Array):
		keys = [keys]
	for key in keys:
		var event = InputEventKey.new()
		event.keycode = key
		event.ctrl_pressed = ctrl_pressed
		event.command_or_control_autoremap = ctrl_pressed
		event.alt_pressed = alt_pressed
		events.append(event)
	shortcut.events = events
	return shortcut

func add_item_with_shortcut(menu: PopupMenu, id, text: String, shortcut: Shortcut = null, checkable: bool = false, disabled: bool = false):
	if shortcut == null:
		menu.add_item(text, id)
	else:
		menu.add_shortcut(shortcut, id)
		menu.set_item_text(menu.get_item_index(id), text)
	menu.set_item_as_checkable(menu.get_item_index(id), checkable)
	menu.set_item_disabled(menu.get_item_index(id), disabled)

func _add_items_to_popup_menu(menu: PopupMenu):
	add_item_with_shortcut(menu, show_tabs_popup_id, "Show Tabs", null, true)
	add_item_with_shortcut(menu, show_spaces_popup_id, "Show Spaces", null, true)
	add_item_with_shortcut(menu, word_wrap_popup_id, "Word Wrap", gen_short_cut(KEY_Z, false, true), true)
	menu.add_separator()
	add_item_with_shortcut(menu, text_size_plus_id, "Zoom In", gen_short_cut([KEY_EQUAL, KEY_PLUS], true))
	add_item_with_shortcut(menu, text_size_minus_id, "Zoom Out", gen_short_cut(KEY_MINUS, true))
	menu.add_separator()
	add_item_with_shortcut(menu, find_popup_id, "Find...", find_replace_bar.get_find_shortcut())
	add_item_with_shortcut(menu, replace_popup_id, "Replace...", find_replace_bar.get_replace_shortcut(), false, not find_replace_bar.is_replace_enabled())
	add_item_with_shortcut(menu, find_next_popup_id, "Find Next", find_replace_bar.get_find_next_shortcut())
	add_item_with_shortcut(menu, find_prev_popup_id, "Find Previous", find_replace_bar.get_find_prev_shortcut())


	_on_code_viewer_options_pressed(-1)
	menu.connect("id_pressed", self._on_code_viewer_options_pressed)
	menu.connect("about_to_popup", self.reset_popup_menu.bind(menu))
	reset_popup_menu(menu)

func zoom_in():
	font_size += 1
	add_theme_font_size_override("font_size", font_size)

func zoom_out():
	font_size -= 1
	add_theme_font_size_override("font_size", font_size)

func _enter_tree() -> void:
	pass

func _input(event: InputEvent) -> void:
	if not self.has_focus():
		if (find_replace_bar.visible) && (find_replace_bar.has_focus() || (get_viewport().gui_get_focus_owner() && find_replace_bar.is_ancestor_of(get_viewport().gui_get_focus_owner()))):
			if event.is_action("ui_find_next") and event.is_pressed():
				find_replace_bar.search_next()
				accept_event()
			elif event.is_action("ui_find_previous") and event.is_pressed():
				find_replace_bar.search_prev()
				accept_event()

func _process(_delta: float) -> void:
	if self.editable != last_editable:
		last_editable = self.editable
		find_replace_bar.replace_enabled = self.editable
		set_replace_option_enabled(self.editable)
