class_name GDRETextEditor
extends Control

var CODE_VIWER_OPTIONS: MenuButton = null
var CODE_VIWER_OPTIONS_POPUP: PopupMenu = null
var CODE_VIEWER: CodeEdit = null
var TEXT_VIEW: Control = null

var show_tabs_popup_id: int = 0
var show_spaces_popup_id: int = 0
var word_wrap_popup_id: int = 0
var text_size_plus_id: int = 0
var text_size_minus_id: int = 0

@export var editable: bool = false:
	set(val):
		CODE_VIEWER.editable = val
	get:
		return CODE_VIEWER.editable
@export var gdscript_highlighter: CodeHighlighter = preload("res://gdre_code_highlighter.tres"):
	set(val):
		if CODE_VIEWER.syntax_highlighter == gdscript_highlighter:
			CODE_VIEWER.syntax_highlighter = val
		gdscript_highlighter = val
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


func reset():
	pass

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
	while next_line != -1:
		if (next_line - prev_line) > 4000: # greater than 4000 characters really chugs the editor when wrapping
			CODE_VIEWER.wrap_mode = TextEdit.LINE_WRAPPING_NONE
			disabled = true
			break
		prev_line = next_line
		next_line = text.find("\n", prev_line + 1)
	if disabled:
		disable_word_wrap_option()
	else:
		enable_word_wrap_option()
	CODE_VIEWER.text = text

func load_code(path):
	var code_text = ""
	if path.get_extension().to_lower() == "gd":
		code_text = FileAccess.get_file_as_string(path)
	else:
		var code: FakeGDScript = ResourceCompatLoader.non_global_load(path)
		if (code == null):
			return false
		code_text = code.get_source_code()
	set_code_viewer_props()
	set_viewer_text(code_text)
	return true

func load_gdshader(path):
	set_shader_viewer_props()
	set_viewer_text(FileAccess.get_file_as_string(path))
	return true

func load_text_resource(path):
	set_resource_viewer_props()
	set_viewer_text(FileAccess.get_file_as_string(path))
	return true

func load_text(path):
	set_text_viewer_props()
	set_viewer_text(FileAccess.get_file_as_string(path))
	return true

func load_text_string(text):
	set_text_viewer_props()
	set_viewer_text(text)
	return true

func set_shader_viewer_props():
	CODE_VIEWER.syntax_highlighter = gdshader_highlighter
	CODE_VIEWER.gutters_draw_line_numbers = true
	CODE_VIEWER.auto_brace_completion_highlight_matching = true
	CODE_VIEWER.highlight_all_occurrences = true
	CODE_VIEWER.highlight_current_line = true
	CODE_VIEWER.draw_control_chars = true
	CODE_VIEWER.draw_tabs = true
	CODE_VIEWER.draw_spaces = true

func set_code_viewer_props():
	CODE_VIEWER.syntax_highlighter = gdscript_highlighter
	CODE_VIEWER.gutters_draw_line_numbers = true
	CODE_VIEWER.auto_brace_completion_highlight_matching = true
	CODE_VIEWER.highlight_all_occurrences = true
	CODE_VIEWER.highlight_current_line = true
	CODE_VIEWER.draw_control_chars = true
	CODE_VIEWER.draw_tabs = true
	CODE_VIEWER.draw_spaces = true


func set_resource_viewer_props():
	CODE_VIEWER.syntax_highlighter = gdresource_highlighter
	CODE_VIEWER.gutters_draw_line_numbers = false
	CODE_VIEWER.auto_brace_completion_highlight_matching = false
	CODE_VIEWER.highlight_all_occurrences = true
	CODE_VIEWER.highlight_current_line = true
	CODE_VIEWER.draw_control_chars = false
	CODE_VIEWER.draw_tabs = false
	CODE_VIEWER.draw_spaces = false

func set_text_viewer_props():
	CODE_VIEWER.syntax_highlighter = null
	CODE_VIEWER.gutters_draw_line_numbers = false
	CODE_VIEWER.auto_brace_completion_highlight_matching = false
	CODE_VIEWER.highlight_all_occurrences = false
	CODE_VIEWER.highlight_current_line = false
	CODE_VIEWER.draw_control_chars = false
	CODE_VIEWER.draw_tabs = false
	CODE_VIEWER.draw_spaces = false

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

func add_item_with_shortcut(menu: PopupMenu, id, text: String, shortcut: Shortcut = null, checkable: bool = false):
	if shortcut == null:
		menu.add_item(text, id)
	else:
		menu.add_shortcut(shortcut, id)
		menu.set_item_text(menu.get_item_index(id), text)
	if checkable:
		menu.set_item_as_checkable(menu.get_item_index(id), true)

func _add_items_to_popup_menu(menu: PopupMenu):
	add_item_with_shortcut(menu, show_tabs_popup_id, "Show Tabs", null, true)
	add_item_with_shortcut(menu, show_spaces_popup_id, "Show Spaces", null, true)
	add_item_with_shortcut(menu, word_wrap_popup_id, "Word Wrap", gen_short_cut(KEY_Z, false, true), true)
	menu.add_separator()
	add_item_with_shortcut(menu, text_size_plus_id, "Zoom In", gen_short_cut([KEY_EQUAL, KEY_PLUS], true))
	add_item_with_shortcut(menu, text_size_minus_id, "Zoom Out", gen_short_cut(KEY_MINUS, true))

	_on_code_viewer_options_pressed(-1)
	menu.connect("id_pressed", self._on_code_viewer_options_pressed)
	menu.connect("about_to_popup", self.reset_popup_menu.bind(menu))
	reset_popup_menu(menu)

func zoom_in():
	var font_size = CODE_VIEWER.theme.get_theme_item(Theme.DATA_TYPE_FONT_SIZE, "font_size", "TextEdit")
	CODE_VIEWER.theme.set_theme_item(Theme.DATA_TYPE_FONT_SIZE, "font_size", "TextEdit", font_size + 1)

func zoom_out():
	var font_size = CODE_VIEWER.theme.get_theme_item(Theme.DATA_TYPE_FONT_SIZE, "font_size", "TextEdit")
	CODE_VIEWER.theme.set_theme_item(Theme.DATA_TYPE_FONT_SIZE, "font_size", "TextEdit", font_size - 1)


func _ready():
	CODE_VIEWER = $CodeViewer
	CODE_VIWER_OPTIONS = $CodeOptsBox/CodeViewerOptions
	CODE_VIWER_OPTIONS_POPUP = CODE_VIWER_OPTIONS.get_popup()
	TEXT_VIEW = $"."
	var menu: PopupMenu = CODE_VIEWER.get_menu()
	var idx = menu.item_count
	menu.add_separator("", idx)
	show_tabs_popup_id = idx + 1
	show_spaces_popup_id = idx + 2
	word_wrap_popup_id = idx + 3
	text_size_plus_id = idx + 5
	text_size_minus_id = idx + 6
	_add_items_to_popup_menu(menu)
	CODE_VIWER_OPTIONS_POPUP = CODE_VIWER_OPTIONS.get_popup()
	_add_items_to_popup_menu(CODE_VIWER_OPTIONS_POPUP)
	set_code_viewer_props()
