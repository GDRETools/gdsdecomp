class_name GDREConfigDialog
extends GDREWindow

@export var show_ephemeral_settings: bool = false

signal config_changed(changed_settings: Dictionary[String, Variant])

func create_section_settings() -> LabelSettings:
	var label_settings: LabelSettings = LabelSettings.new()
	label_settings.font_size = 20
	label_settings.outline_size = 1
	return label_settings

func create_subsection_label_settings() -> LabelSettings:
	var label_settings: LabelSettings = LabelSettings.new()
	label_settings.font_size = 18
	return label_settings

@onready var SECTION_LABEL_SETTINGS: LabelSettings = create_section_settings()
@onready var SUBSECTION_LABEL_SETTINGS: LabelSettings = create_subsection_label_settings()

var force_change = false
var setting_value_map: Dictionary = {}
var setting_button_map: Dictionary = {}
const RESET_BUTTON_ICON = preload("res://gdre_icons/gdre_Reload.svg")

func create_section_label(text: String) -> Label:
	var label: Label = Label.new()
	label.label_settings = SECTION_LABEL_SETTINGS
	label.text = text + " Settings"
	label.horizontal_alignment = 1
	return label

func create_subsection_label(text: String) -> Label:
	var label: Label = Label.new()
	label.label_settings = SUBSECTION_LABEL_SETTINGS
	label.text = text
	label.horizontal_alignment = 1
	return label

func create_h_separator() -> HSeparator:
	var separator: HSeparator = HSeparator.new()
	return separator

func create_new_section(parent: VBoxContainer, text: String) -> VBoxContainer:
	var section: VBoxContainer = VBoxContainer.new()
	if parent.get_child_count() != 0:
		parent.add_child(create_h_separator())
	parent.add_child(section)
	section.add_child(create_section_label(text))
	section.add_child(create_h_separator())
	return section

func create_new_subsection(text: String, section: VBoxContainer) -> VBoxContainer:
	var subsection: VBoxContainer = VBoxContainer.new()
	if section.get_child_count() != 2: # including the label and h_separator
		section.add_child(create_h_separator())
	section.add_child(subsection)
	subsection.add_child(create_subsection_label(text))
	subsection.add_child(create_h_separator())
	return subsection

func create_new_subsubsection(text: String, subsection: VBoxContainer) -> VBoxContainer:
	var subsubsection: FoldableContainer = FoldableContainer.new()
	subsubsection.title = text
	subsubsection.title_alignment = 1
	if subsection.get_child_count() != 2: # including the label and h_separator
		subsection.add_child(create_h_separator())
	subsection.add_child(subsubsection)
	var vbox: VBoxContainer = VBoxContainer.new()
	subsubsection.add_child(vbox)
	return vbox

var section_map: Dictionary = {}
var vboxes: Array[VBoxContainer] = []

func make_button_hbox(setting: GDREConfigSetting, button: Control, label: Label) -> HBoxContainer:
	var hbox: HBoxContainer = HBoxContainer.new()
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hbox.add_child(label)
	hbox.add_child(make_reset_button(setting, button))
	hbox.add_child(button)
	return hbox

func make_button_label(text: String) -> Label:
	var label: Label = Label.new()
	label.text = text
	return label


func set_setting_value(setting: GDREConfigSetting, value: Variant):
	setting_value_map[setting] = value

func popup_error(message: String):
	%ErrorDialog.dialog_text = message
	%ErrorDialog.popup_centered()



func setting_callback(setting: GDREConfigSetting, value: Variant, control: Control):
	print('setting "' + setting.get_brief_description() + '" set to ', value)
	var reset = false


	if (setting.is_virtual_setting()):
		var temp_map = {}
		# need to detect if the setting was set to something else by the virtual setting
		for s: GDREConfigSetting in GDREConfig.get_all_settings():
			# get the CURRENT value of the setting, not the value that's in the setting_value_map
			temp_map[s] = s.get_value()
		setting.set_value(value)
		var error_message = setting.get_error_message()
		if (error_message.is_empty()):
			reset = true
			for s: GDREConfigSetting in temp_map.keys():
				if temp_map[s] != s.get_value():
					# Setting changed
					force_change = true
					setting_value_map[s] = s.get_value()
		else:
			popup_error(error_message)
			setting.clear_error_message()

	else:
		set_setting_value(setting, value)
	# check if the control is a button
	if control is Button:
		control.get_child(0).get_child(0).visible = value != setting.get_default_value()
	elif control is HBoxContainer:
		control.get_child(1).visible = value != setting.get_default_value()

	if (reset):
		clear(false)


func reset_callback(setting: GDREConfigSetting, control: Control, reset_button: Control):
	reset_button.visible = false
	var default_val = setting.get_default_value()
	if control is OptionButton:
		for i in range(0, control.item_count):
			if (control.get_item_metadata(i) == default_val):
				control.selected = i
				break
	elif control is CheckButton:
		control.button_pressed = setting.get_default_value()
	elif control is SpinBox:
		control.value = setting.get_default_value()
	elif control is LineEdit:
		control.text = setting.get_default_value()
	set_setting_value(setting, setting.get_default_value())

func make_reset_button(setting: GDREConfigSetting, parent: Control) -> Button:
	var button: Button = Button.new()
	button.theme_type_variation = &"FlatButton"
	button.icon = RESET_BUTTON_ICON
	button.pressed.connect(func(): reset_callback(setting, parent, button))
	button.visible = setting_value_map.get(setting, setting.get_value()) != setting.get_default_value()
	return button


func add_reset_button_to_toggle_button(setting: GDREConfigSetting, button: Button):
	var hbox: HBoxContainer = HBoxContainer.new()
	hbox.anchors_preset = 6
	hbox.anchor_left = 1.0
	hbox.anchor_top = 0.5
	hbox.anchor_right = 1.0
	hbox.anchor_bottom = 0.5
	hbox.offset_left = -71.0
	hbox.offset_top = -15.0
	hbox.offset_bottom = 15.0
	hbox.grow_horizontal = 0
	hbox.grow_vertical = 2
	button.add_child(hbox)
	var reset_button = make_reset_button(setting, button)
	hbox.add_child(reset_button)


func _on_file_picker_file_selected(path: String, callback: Callable):
	%FileDialog.file_selected.disconnect(_on_file_picker_file_selected.bindv([callback]))
	callback.call(path)

func open_file_picker(callback: Callable):
	%FileDialog.file_selected.connect(_on_file_picker_file_selected.bindv([callback]))
	%FileDialog.popup_centered()

func create_setting_button(setting: GDREConfigSetting) -> Control:
	if setting.is_hidden() or (not show_ephemeral_settings and setting.is_ephemeral()):
		return null
	var control: Control = null
	var button = null
	var value = setting_value_map.get(setting, setting.get_value())
	if setting.is_filepicker():
		button = Button.new()
		if (String(value).is_empty()):
			button.text = "Select File..."
		else:
			button.text = value
		control = make_button_hbox(setting, button, make_button_label(setting.get_brief_description()))
		button.pressed.connect(open_file_picker.bind(func(path: String): setting_callback(setting, path, control)))
	elif setting.has_special_value():
		button = OptionButton.new()
		var items = setting.get_list_of_possible_values()
		for val in items.keys(): # the keys are the values, the values are the descriptions
			var desc = items[val]
			button.add_item(desc, -1)
			var idx = button.get_item_count() - 1
			if (val == value):
				button.selected = idx
			button.set_item_metadata(idx, val)

		var label: Label = make_button_label(setting.get_brief_description())
		label.tooltip_text = setting.get_description()
		control = make_button_hbox(setting, button, label)
		button.item_selected.connect(
			func(idx):
				var val = button.get_item_metadata(idx)
				setting_callback(setting, val, control)
		)
	elif setting.get_type() == TYPE_BOOL:
		button = CheckButton.new()
		button.button_pressed = value

		button.text = setting.get_brief_description()
		button.toggled.connect(func(val): setting_callback(setting, val, button))
		add_reset_button_to_toggle_button(setting, button)
		control = button
	elif setting.get_type() == TYPE_INT:
		button = SpinBox.new()
		button.value = value
		button.step = 1
		var label: Label = make_button_label(setting.get_brief_description())
		label.tooltip_text = setting.get_description()
		control = make_button_hbox(setting, button, label)
		button.value_changed.connect(func(val): setting_callback(setting, val, control))
	elif setting.get_type() == TYPE_FLOAT:
		button = SpinBox.new()
		button.value = value
		button.step = 0.1
		var label: Label = make_button_label(setting.get_brief_description())
		label.tooltip_text = setting.get_description()
		control = make_button_hbox(setting, button, label)
		button.value_changed.connect(func(val): setting_callback(setting, val, control))
	elif setting.get_type() == TYPE_STRING:
		button = LineEdit.new()
		button.custom_minimum_size = Vector2i(80,0)
		button.text = value
		var label: Label = make_button_label(setting.get_brief_description())
		label.tooltip_text = setting.get_description()
		control = make_button_hbox(setting, button, label)
		button.text_changed.connect(func(val): setting_callback(setting, val, control))

	button.tooltip_text = setting.get_description()
	control.tooltip_text = setting.get_description()

	setting_button_map[setting] = button
	return control


# [node name="FirstSectionVBox" type="VBoxContainer" parent="MainHBox"]
# unique_name_in_owner = true
# layout_mode = 2
# size_flags_horizontal = 3

# [node name="GeneralVBox" type="VBoxContainer" parent="MainHBox/FirstSectionVBox"]
# layout_mode = 2
# theme_override_constants/separation = 0

func create_sections_container() -> VBoxContainer:
	var vbox: VBoxContainer = VBoxContainer.new()
	#vbox.layout_mode = 2
	vbox.size_flags_horizontal = 3
	return vbox

func clear_main_hbox():
	for child in %MainHBox.get_children():
		%MainHBox.remove_child(child)
		child.queue_free()
	var vbox: VBoxContainer = create_sections_container()
	vboxes.append(vbox)
	%MainHBox.add_child(vbox)

func _render_settings():
	if not section_map.has("General"):
		section_map["General"] = create_new_section(vboxes[0], "General")
	var curr_vbox_index = vboxes.size() - 1
	for setting: GDREConfigSetting in GDREConfig.get_all_settings():
		if setting.is_hidden() or (not show_ephemeral_settings and setting.is_ephemeral()):
			continue
		var full_name: String = setting.get_full_name()
		var parts: PackedStringArray = full_name.split("/")
		var toAdd
		if parts.size() == 1:
			toAdd = section_map["General"]
		else:
			if parts.size() >= 2:
				if not section_map.has(parts[0]):
					section_map[parts[0]] = create_new_section(vboxes[curr_vbox_index], parts[0])
					# TODO: handle logic for creating a additional vboxes if window is getting too tall
				toAdd = section_map[parts[0]]
			if parts.size() >= 3:
				var subsection = "/".join(parts.slice(0, 2))
				if not section_map.has(subsection):
					section_map[subsection] = create_new_subsection(parts[1], section_map[parts[0]])
				toAdd = section_map[subsection]
			if parts.size() >= 4:
				var subsection = "/".join(parts.slice(0, 2))
				var subsubsection = "/".join(parts.slice(0, 3))
				if not section_map.has(subsubsection):
					section_map[subsubsection] = create_new_subsubsection(parts[2], section_map[subsection])
				toAdd = section_map[subsubsection]
		if toAdd:
			toAdd.add_child(create_setting_button(setting))


func save_settings():
	var changed_settings: Dictionary[String, Variant] = {}
	for setting: GDREConfigSetting in setting_value_map.keys():
		if setting.get_value() != setting_value_map[setting]:
			changed_settings[setting.get_full_name()] = setting_value_map[setting]
			setting.set_value(setting_value_map[setting])
	GDREConfig.save_config()
	if changed_settings.size() != 0 or force_change:
		emit_signal("config_changed", changed_settings)

# MUST CALL set_root_window() first!!!
# Called when the node enters the scene tree for the first time.
func _ready():
	%FileDialog.current_dir = OS.get_system_dir(OS.SYSTEM_DIR_DESKTOP)
	# The children are not already in the window for ease of GUI creation
	clear()


func clear(clear_settings: bool = true):
	section_map.clear()
	vboxes.clear()
	if clear_settings:
		force_change = false
		setting_value_map.clear()
	clear_main_hbox()
	_render_settings()
	%MainHBox.queue_redraw()

func close():
	force_change = false
	setting_value_map.clear()
	hide()
	pass

func confirm():
	save_settings()
	close()

func cancelled():
	if force_change:
		emit_signal("config_changed", {})
	close()

func has_unsaved_changes() -> bool:
	for setting: GDREConfigSetting in setting_value_map.keys():
		if setting.get_value() != setting_value_map[setting]:
			return true
	return force_change

var unclose = false

func _close_requested():
	if has_unsaved_changes():
		# Godot unfortunately does not expose any ability to prevent closing the window,
		# so we have to re-show ourselves when a close is requested
		self.show()
		unclose = true
		print("has unsaved changes")
		%ConfirmClose.popup_centered()
		return
	#print("closing")
	close()

func _process(_delta):
	if unclose:
		self.show()
		unclose = false
	pass

func _on_confirm_close_canceled() -> void:
	pass # Automatically closed.


func _on_confirm_close_confirmed() -> void:
	cancelled()
	pass


func _on_cancel_button_pressed() -> void:
	cancelled()


func _on_ok_button_pressed() -> void:
	confirm()


func _on_reset_button_pressed() -> void:
	for setting: GDREConfigSetting in GDREConfig.get_all_settings():
		setting.reset()
	clear()
