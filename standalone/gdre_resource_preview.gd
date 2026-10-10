class_name GDREResourcePreview
extends Control

const RESOURCE_INFO_TEXT_FORMAT = "[b]Path:[/b] %s\n[b]Type:[/b] %s\n[b]Format:[/b] %s"
const SWITCH_TO_TEMPLATE = "Switch to %s View"

@onready var LOADING_WAIT_VIEW = %LoadingWaitView
@onready var BLANK_VIEW = %BlankView

const USE_THREADED_LOAD = true
var pending_resources: Array[LoadTask] = []

class TextPreviewer extends GDREPreviewer:
	var text_view: GDRETextEditor = null

	func _get_previewer_name() -> String:
		return "text"
	func _edit(resource: Resource) -> Error:
		var text = ResourceCompatLoader.resource_to_string(resource)
		text_view.load_text_string(text)
		return OK
	func _edit_from_path(resource_path: String) -> Error:
		return text_view.load_path(resource_path, text_view.recognize(resource_path))
	func _can_edit(resource_path: String, resource_type: String) -> bool:
		return text_view.recognize(resource_path) != -1
	func _get_edited_resource_path() -> String:
		return text_view.current_path
	func _reset():
		text_view.reset()
	func _init():
		text_view = GDRETextEditor.new()
		text_view.editable = false
		text_view.set_anchors_preset(Control.PRESET_FULL_RECT)
		add_child(text_view)
		text_view.visible = true

	func _refresh():
		text_view.reload_from_disk()

	func _get_load_type():
		return 0

	func load_text_string(text: String):
		text_view.load_text_string(text)

	func load_text(path: String, text: String, type: int):
		text_view.load_text(path, text, type)

	func recognize(path: String) -> int:
		return text_view.recognize(path)

class LoadTask extends RefCounted:
	var path: String
	var type: int = -1
	var task_id: int = -1
	var res: Resource = null
	var text: String = ""
	var previewer: GDREPreviewer = null
	var error: Error = OK
	var done: bool = false

	func _init(p_path: String, p_previewer: GDREPreviewer):
		path = p_path
		previewer = p_previewer
		if previewer is TextPreviewer:
			type = previewer.recognize(path)
			if type == -1:
				return
			task_id = WorkerThreadPool.add_task(func(): self.text = ResourceCompatLoader.resource_file_to_string(path), true)
			if task_id == -1:
				error = ERR_CANT_CREATE
				done = true
				return
		else:
			error = ResourceLoader.load_threaded_request(path, "", false, ResourceLoader.CACHE_MODE_REUSE)
			if error != OK:
				done = true
				return
		return

	func get_error() -> Error:
		return error

	func check_status() -> Error:
		if self.done:
			return error
		if self.task_id != -1:
			if !WorkerThreadPool.is_task_completed(self.task_id):
				return ERR_BUSY
			WorkerThreadPool.wait_for_task_completion(self.task_id)
			self.done = true
			self.task_id = -1
			return self.error
		else:
			var status = ResourceLoader.load_threaded_get_status(path)
			match status:
				ResourceLoader.THREAD_LOAD_LOADED:
					res = ResourceLoader.load_threaded_get(path)
					self.done = true
					if is_instance_valid(res):
						self.error = OK
					else:
						self.error = ERR_FILE_CORRUPT
				ResourceLoader.THREAD_LOAD_IN_PROGRESS:
					return ERR_BUSY
				ResourceLoader.THREAD_LOAD_FAILED:
					self.error = ERR_FILE_CORRUPT
					done = true
					return self.error
				ResourceLoader.THREAD_LOAD_INVALID_RESOURCE:
					self.error = ERR_FILE_CORRUPT
					done = true
					return self.error
			return error

	func matches(current_path: String, current_previewer: GDREPreviewer) -> bool:
		return path == current_path and previewer == current_previewer



var TEXT_PREVIEWER: TextPreviewer
var resource_previewers: Array[GDREPreviewer] = []

var current_view: GDREPreviewer = null
var alt_previewer: GDREPreviewer = null

var current_resource_path: String = ""
var current_resource_info: Dictionary = {}

func reset():
	_reset()

func is_main_view_visible() -> bool:
	return %ResourceView.is_visible_in_tree()

func set_main_view_visible(p_visible: bool):
	%ResourceView.visible = p_visible

func _make_all_views_invisible():
	%SwitchViewButton.visible = false
	# %ResourceView is a TabContainer, so setting this to true will set all of the child views to false
	BLANK_VIEW.visible = true

func _reset():
	current_view = null
	alt_previewer = null
	current_resource_path = ""
	_make_all_views_invisible()
	for previewer in resource_previewers:
		previewer.reset()
	%ResourceInfo.text = ""

func set_current_view(previewer: GDREPreviewer):
	current_view = previewer
	previewer.visible = true
	if alt_previewer:
		%SwitchViewButton.text = SWITCH_TO_TEMPLATE % alt_previewer.get_previewer_name().capitalize()
		%SwitchViewButton.visible = true
	else:
		%SwitchViewButton.visible = false


var previous_res_info_size = Vector2(0, 0)

func pop_resource_info(path: String, info: Dictionary):
	var info_text = ""
	if not info.is_empty():
		var type = info.get("type", "")
		var format = info.get("format_type", "")
		info_text = RESOURCE_INFO_TEXT_FORMAT % [path, type, format]
		var ver_major = info.get("ver_major", 0)
		if format == "binary" and ver_major > 0:
			var ver_minor = info.get("ver_minor", 0)
			info_text += "\n[b]Engine Version:[/b] " + str(ver_major) + "." + str(ver_minor)
			if ver_major <= 2:
				# Showing V2 import info in the info box because there's no other way to look at the v2 import metadata in binary resources
				var iinfo: ImportInfo = GDRESettings.get_import_info_by_dest(path)
				if iinfo and iinfo.get_iitype() == ImportInfo.V2 and iinfo.is_import():
					info_text += "\n"
					if (iinfo.get_additional_sources().size() > 0):
						info_text += "[b]Source Files:[/b] [" + "\n"
						for source in PackedStringArray([iinfo.source_file]) + iinfo.get_additional_sources():
							info_text += "\t" + source + "\n"
						info_text += "]\n"
					else:
						info_text += "[b]Source File:[/b] "
						info_text += iinfo.source_file + "\n"
					info_text += "[b]Importer:[/b] "
					info_text += iinfo.get_importer() + "\n"
					if (iinfo.params.size() > 0):
						info_text += "[b]Import Options:[/b] {" + "\n"
						for key in iinfo.params.keys():
							info_text += "\t" + str(key) + ": " + str(iinfo.params[key]) + "\n"
						info_text += "}\n"
					else:
						info_text += "[b]Import Options:[/b] {}\n"
	else:
		info_text = "[b]Path:[/b] " + path
	%ResourceInfo.text = info_text

func _start_resource_load(path):
	if current_view == null:
		printerr("Current view is null")
		return false
	if not ResourceCompatLoader.handles_resource(path, "") and current_view != TEXT_PREVIEWER:
		if current_view.edit_from_path(path) != OK:
			return false
		set_current_view(current_view)
		return true
	var res: Resource = null
	var load_type = current_view.get_load_type()
	if current_view == TEXT_PREVIEWER or (ResourceCompatLoader.is_globally_available() and load_type == ResourceInfo.LoadType.REAL_LOAD):
		if current_view == TEXT_PREVIEWER or USE_THREADED_LOAD:
			var task = LoadTask.new(path, current_view)
			if task.get_error() != OK:
				return false
			pending_resources.append(task)
			set_current_view(current_view)
			%LoadingWaitView.visible = true
			return true
		else:
			res = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_REUSE)
	else:
		if load_type == ResourceInfo.LoadType.REAL_LOAD:
			res = ResourceCompatLoader.real_load(path, "", ResourceCompatLoader.CACHE_MODE_REUSE)
		elif load_type == ResourceInfo.LoadType.FAKE_LOAD:
			res = ResourceCompatLoader.fake_load(path)
	return _load_resource_complete(res)

func _load_resource_complete(res: Resource, is_cached: bool = false):
	if not res:
		return false
	if current_view.edit(res) != OK:
		handle_error_opening(res.get_path())
		return false
	set_current_view(current_view)
	return true

func handle_pending_resources():
	if pending_resources.size() == 0:
		return
	var to_remove: Array[LoadTask] = []
	var res: Resource = null
	for task in pending_resources:
		var error = task.check_status()
		if error == ERR_BUSY:
			continue
		to_remove.append(task)
		if task.matches(current_resource_path, current_view):
			if error != OK:
				handle_error_opening(task.path)
				continue
			if current_view == TEXT_PREVIEWER:
				TEXT_PREVIEWER.load_text(task.path, task.text, task.type)
				set_current_view(TEXT_PREVIEWER)
			else:
				_load_resource_complete(task.res, false)
	for task in to_remove:
		pending_resources.erase(task)

func handle_error_opening(path):
	alt_previewer = null
	_make_all_views_invisible()
	TEXT_PREVIEWER.load_text_string("Error opening resource:\n" + GDRESettings.get_recent_error_string())
	set_current_view(TEXT_PREVIEWER)
	%ResourceInfo.text = path

func previewer_is_not_default(previewer: GDREPreviewer) -> bool:
	if previewer is ScenePreviewer and not GDREConfig.get_setting("Preview/use_scene_view_by_default", false):
		return true
	return false

func previewer_works_on_resource(previewer: GDREPreviewer, path: String, info: Dictionary) -> bool:
	if not previewer.can_edit(path, info.get("type", "")):
		return false
	if previewer is ScenePreviewer and info.get("ver_major", 0) < SceneExporter.get_minimum_godot_ver_supported():
		return false
	return true

func load_resource(path: String) -> void:
	_reset()
	current_resource_path = path
	var ext = path.get_extension().to_lower()

	# clear errors
	GDRESettings.get_errors()
	if ResourceCompatLoader.handles_resource(path, ""):
		current_resource_info = ResourceCompatLoader.get_resource_info(path)
	else:
		current_resource_info = {}
	var previewer = null
	for p in resource_previewers:
		if previewer_works_on_resource(p, path, current_resource_info):
			previewer = p
			break

	if not previewer:
		TEXT_PREVIEWER.load_text_string("Not a supported resource")
		set_current_view(TEXT_PREVIEWER)
		%ResourceInfo.text = path
		return

	if previewer_is_not_default(previewer):
		current_view = TEXT_PREVIEWER
		alt_previewer = previewer
	else:
		current_view = previewer
		if previewer.can_switch_to_text():
			alt_previewer = TEXT_PREVIEWER
		else:
			alt_previewer = null

	if not _start_resource_load(path):
		handle_error_opening(path)
	if (%ResourceInfo.text == ""):
		pop_resource_info(path, current_resource_info)

func refresh():
	if current_view == TEXT_PREVIEWER and current_resource_path != "":
		TEXT_PREVIEWER._refresh()
	# TODO: handle other views? Not currently necessary, config settings only affect the text view currently

func get_currently_visible_view() -> Control:
	if current_view and current_view.visible:
		return current_view
	return null


func _on_gdre_resource_preview_visibility_changed() -> void:
	if not self.is_visible_in_tree():
		self.reset()

func _ready():
	reset()
	self.connect("visibility_changed", self._on_gdre_resource_preview_visibility_changed)
	self.connect("resized", self._on_resized)
	previous_res_info_size = Vector2(0, 100)
	%ResourceInfoContainer.custom_minimum_size = previous_res_info_size
	for previewer in resource_previewers:
		previewer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		%ResourceView.add_child(previewer)

func _on_v_box_container_drag_started() -> void:
	pass
	%ResourceInfoContainer.custom_minimum_size = Vector2(0,0)
	previous_res_info_size = %ResourceInfoContainer.size


func _on_v_box_container_drag_ended() -> void:
	pass
	previous_res_info_size = %ResourceInfoContainer.size
	%ResourceInfoContainer.custom_minimum_size = Vector2(0, previous_res_info_size.y)


func _on_resized() -> void:
	# get the current size of the currently visible view so that it stays the same when we set the split_offset
	var current_view = get_currently_visible_view()
	var current_view_size = current_view.size if current_view else Vector2(0, 0)
	$VBoxContainer.split_offset = self.size.y / 2.0 - previous_res_info_size.y
	if current_view:
		current_view.size = current_view_size
	pass # Replace with function body.

func _on_switch_view_button_pressed() -> void:
	var cur_text = %SwitchViewButton.text
	_make_all_views_invisible()
	if not alt_previewer:
		printerr("No alt previewer")
		return
	var prev_view = current_view
	current_view = alt_previewer
	alt_previewer = prev_view

	var error_opening = false
	set_current_view(current_view)
	if current_view.get_edited_resource_path() != current_resource_path:
		error_opening = not _start_resource_load(current_resource_path)
	if error_opening:
		handle_error_opening(current_resource_path)

func _process(delta: float) -> void:
	handle_pending_resources()

func _init():
	TEXT_PREVIEWER = TextPreviewer.new()
	resource_previewers = [
		TexturePreviewer.new(),
		GDREMediaPlayer.new(),
		MeshPreviewer.new(),
		ScenePreviewer.new(),
		TextureLayeredPreviewer.new(),
		TEXT_PREVIEWER, # Always last so that it's the default view
	]
