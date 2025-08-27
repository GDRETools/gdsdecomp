class_name GDREResourcePreview
extends Control

const RESOURCE_INFO_TEXT_FORMAT = "[b]Path:[/b] %s\n[b]Type:[/b] %s\n[b]Format:[/b] %s"
const IMAGE_FORMAT_NAME = [
	"Lum8",
	"LumAlpha8",
	"Red8",
	"RedGreen",
	"RGB8",
	"RGBA8",
	"RGBA4444",
	"RGBA5551", # Actually RGB565, kept as RGBA5551 for compatibility.
	"RFloat",
	"RGFloat",
	"RGBFloat",
	"RGBAFloat",
	"RHalf",
	"RGHalf",
	"RGBHalf",
	"RGBAHalf",
	"RGBE9995",
	"DXT1 RGB8",
	"DXT3 RGBA8",
	"DXT5 RGBA8",
	"RGTC Red8",
	"RGTC RedGreen8",
	"BPTC_RGBA",
	"BPTC_RGBF",
	"BPTC_RGBFU",
	"ETC",
	"ETC2_R11",
	"ETC2_R11S",
	"ETC2_RG11",
	"ETC2_RG11S",
	"ETC2_RGB8",
	"ETC2_RGBA8",
	"ETC2_RGB8A1",
	"ETC2_RA_AS_RG",
	"FORMAT_DXT5_RA_AS_RG",
	"ASTC_4x4",
	"ASTC_4x4_HDR",
	"ASTC_8x8",
	"ASTC_8x8_HDR",
]

const SWITCH_TO_SCENE_TEXT = "Switch to Scene View"
const SWITCH_TO_MESH_TEXT = "Switch to Mesh View"
const SWITCH_TO_TEXT_TEXT = "Switch to Text View"


var cached_scenes: Array = []
var current_resource_path: String = ""
var current_resource_type: String = ""

func reset():
	cached_scenes.clear()
	_reset()

func make_all_views_invisible():
	%SwitchViewButton.visible = false
	%MediaPlayer.visible = false
	%TextView.visible = false
	%TextureView.visible = false
	%MeshPreviewer.visible = false
	%ScenePreviewer3D.visible = false

func _reset():
	current_resource_path = ""
	current_resource_type = ""
	make_all_views_invisible()
	%MediaPlayer.reset()
	%TextView.reset()
	%TextureInfo.text = ""
	%TextureRect.texture = null
	%ResourceInfo.text = ""
	%MeshPreviewer.reset()
	%ScenePreviewer3D.reset()


var previous_res_info_size = Vector2(0, 0)

func load_texture(path):
	var ext = path.get_extension().to_lower()
	if (ext == "image"):
		%TextureRect.texture = ImageTexture.create_from_image(ResourceCompatLoader.real_load(path, "", ResourceFormatLoader.CACHE_MODE_IGNORE_DEEP))
	elif (is_image(ext)):
		%TextureRect.texture = ImageTexture.create_from_image(Image.load_from_file(path))
	else:
		%TextureRect.texture = ResourceCompatLoader.real_load(path, "", ResourceFormatLoader.CACHE_MODE_IGNORE_DEEP) # TODO: handle other texture types
	if (%TextureRect.texture == null):
		return false
	%TextureRect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	%TextureRect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	var image = %TextureRect.texture.get_image()
	var info_text = str(%TextureRect.texture.get_width()) + "x" + str(%TextureRect.texture.get_height()) + " " + IMAGE_FORMAT_NAME[image.get_format()]
	if image.has_mipmaps():

		info_text += "\n" + str(image.get_mipmap_count()) + " Mipmaps" + "\n" + "Memory: " + String.humanize_size(image.get_data_size())
	else:
		info_text += "\n" + "No Mipmaps" + "\n" + "Memory: " + String.humanize_size(image.get_data_size())

	%TextureInfo.text = info_text
	%TextureView.visible = true
	return true

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

func is_mesh(ext, type: String):
	return ext == "mesh" || type == "Mesh" || type == "ArrayMesh" || type == "PlaceholderMesh"

func is_scene(ext, type: String):
	return ext == "tscn" || ext == "scn" || type == "PackedScene"

func load_mesh(path):
	var res = ResourceCompatLoader.real_load(path, "", ResourceFormatLoader.CACHE_MODE_IGNORE_DEEP)
	if not res:
		return false
	# check if the resource is a mesh or a descendant of mesh
	if not res.get_class().contains("Mesh"):
		return false
	%SwitchViewButton.text = SWITCH_TO_TEXT_TEXT
	%SwitchViewButton.visible = true
	%MeshPreviewer.edit(res)
	%MeshPreviewer.visible = true
	return true

func load_scene(path):
	var res = null
	var is_cached = false
	for scene in cached_scenes:
		if scene.get_path() == path:
			res = scene
			is_cached = true
			break
	var start_time = Time.get_ticks_msec()
	if not res:
		res = ResourceCompatLoader.real_load(path, "", ResourceFormatLoader.CACHE_MODE_REUSE)
	if not res:
		return false
	# check if the resource is a scene or a descendant of scene
	if not res.get_class().contains("PackedScene"):
		return false
	%SwitchViewButton.text = SWITCH_TO_TEXT_TEXT
	%SwitchViewButton.visible = true
	%ScenePreviewer3D.edit(res)
	%ScenePreviewer3D.visible = true
	var time_to_load = Time.get_ticks_msec() - start_time
	if time_to_load > 200 and not is_cached:
		print("Caching scene: ", path)
		cached_scenes.append(res)
	else:
		print("Loaded scene in ", time_to_load, "ms")
	return true


func can_preview_scene():
	return SceneExporter.get_minimum_godot_ver_supported() <= GDRESettings.get_ver_major()

func text_preview_check_button(path, type):
	if (is_mesh(path.get_extension().to_lower(), type)):
		%SwitchViewButton.text = SWITCH_TO_MESH_TEXT
		%SwitchViewButton.visible = true
	elif (is_scene(path.get_extension().to_lower(), type)):
		if (can_preview_scene()):
			%SwitchViewButton.text = SWITCH_TO_SCENE_TEXT
			%SwitchViewButton.visible = true


func handle_error_opening(path):
	# %SwitchViewButton.visible = false
	%TextView.load_text_string("Error opening resource:\n" + GDRESettings.get_recent_error_string())
	%TextView.visible = true
	%ResourceInfo.text = path



func load_resource(path: String) -> void:
	_reset()
	current_resource_path = path
	current_resource_type = ""
	var ext = path.get_extension().to_lower()
	var error_opening = false
	var not_supported = false
	var info: Dictionary = {}

	# clear errors
	GDRESettings.get_errors()
	if ResourceCompatLoader.handles_resource(path, ""):
		info = ResourceCompatLoader.get_resource_info(path)
		current_resource_type = info.get("type", "")
	if (is_sample(ext)):
		error_opening = not %MediaPlayer.load_sample(path)
		if not error_opening:
			%MediaPlayer.visible = true
	elif (is_video(ext)):
		error_opening = not %MediaPlayer.load_video(path)
		if not error_opening:
			%MediaPlayer.visible = true
	elif (is_image(ext)):
		error_opening = not load_texture(path)
	elif (is_texture(ext)):
		error_opening = not load_texture(path)
	elif (is_mesh(ext, current_resource_type)):
		error_opening = not load_mesh(path)
	elif (GDREConfig.get_setting("Preview/use_scene_view_by_default", false) and is_scene(ext, current_resource_type) and can_preview_scene()):
		error_opening = not load_scene(path)
	else:
		var type = %TextView.recognize(path)
		if type == -1:
			not_supported = true
		else:
			error_opening = not try_text_preview(path, type, current_resource_type)

	if (not_supported):
		%TextView.load_text_string("Not a supported resource")
		%TextView.visible = true
		%ResourceInfo.text = path
	elif (error_opening):
		handle_error_opening(path)
	if (%ResourceInfo.text == ""):
		pop_resource_info(path, info)


func try_text_preview(path, type, res_type):
	if type == -1:
		return false
	if not %TextView.load_path(path, type):
		return false
	text_preview_check_button(path, res_type)
	%TextView.visible = true
	return true

	# TODO: handle binary resources
	# var res_info:Dictionary = ResourceCompatLoader.get_resource_info(path)
	# if (res_info.size() == 0):
	# 	return

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

func is_shader(ext, p_type = ""):
	if (ext == "shader" || ext == "gdshader"):
		return true
	return false

func is_code(ext, p_type = ""):
	if (ext == "gd" || ext == "gdc" || ext == "gde" || ext == "cs"):
		return true
	return false

func is_text(ext, p_type = ""):
	if (ext == "txt" || ext == "xml" || ext == "csv" || ext == "html" || ext == "md" || ext == "yml" || ext == "yaml"):
		return true
	return false

func is_text_resource(ext, p_type = ""):
	return ext == "tscn" || ext == "tres"

func is_ini_like(ext, p_type = ""):
	return ext == "cfg" || ext == "remap" || ext == "import" || ext == "gdextension" || ext == "gdnative" || ext == "godot"


func is_non_resource_smp(ext, p_type = ""):
	return (ext == "wav" || ext == "ogg" || ext == "mp3")

func is_sample(ext, p_type = ""):
	if (ext == "oggstr" || ext == "mp3str" || ext == "oggvorbisstr" || ext == "sample" || ext == "smp" || is_non_resource_smp(ext, p_type)):
		return true
	return false

func is_video(ext, p_type = ""):
	if (ext == "webm" || ext == "ogv" || ext == "mp4" || ext == "avi" || ext == "mov" || ext == "flv" || ext == "mkv" || ext == "wmv" || ext == "mpg" || ext == "mpeg"):
		return true
	return false


func is_texture(ext, p_type = ""):
	if (ext == "ctex" || ext == "stex" || ext == "tex"):
		return true
	# return p_type == "CompressedTexture2D" || p_type == "StreamTexture" || p_type == "Texture2D" || p_type == "ImageTexture"
	return false

func is_binary_project_settings(path):
	return path.get_file() == "project.binary" || path.get_file() == "engine.cfb"

func is_image(ext, p_type = ""):
	if (ext == "image" || ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "svg" || ext == "webp" || ext == "bmp" || ext == "tga" || ext == "tiff" || ext == "hdr" || ext == "ico" || ext == "icns"):
		return true
	return false

func get_currently_visible_view() -> Control:
	if %TextView.visible:
		return %TextView
	elif %MediaPlayer.visible:
		return %MediaPlayer
	elif %TextureView.visible:
		return %TextureView
	elif %MeshPreviewer.visible:
		return %MeshPreviewer
	elif %ScenePreviewer3D.visible:
		return %ScenePreviewer3D
	return null


func _on_gdre_resource_preview_visibility_changed() -> void:
	if not self.is_visible_in_tree():
		%MediaPlayer.stop()
		self.reset()
	pass # Replace with function body.

func _ready():
	reset()
	self.connect("visibility_changed", self._on_gdre_resource_preview_visibility_changed)
	self.connect("resized", self._on_resized)
	previous_res_info_size = Vector2(0, 100)
	%ResourceInfoContainer.custom_minimum_size = previous_res_info_size
	# load_resource("res://.godot/imported/kyuu_on_bike.glb-ecab64cc65c256db28f6d03df73eb447.scn")
	# load_resource("res://.godot/imported/ScifiStruct_3.obj-8ad9868dec2ef9403c73f82a7404489a.mesh")
	# load_resource("res://.godot/imported/gdre_Script.svg-4c68c9c5e02f5e7a41dddea59a95e245.ctex")
	#load_resource("res://.godot/imported/anomaly 105 jun12.ogg-d3e939934d210d1a4e1f9d2d34966046.oggvorbisstr")

# audio player stuff


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
	var path = current_resource_path
	var cur_text = %SwitchViewButton.text
	make_all_views_invisible()
	current_resource_path = path
	var error_opening = false

	match cur_text:
		SWITCH_TO_SCENE_TEXT:
			if %ScenePreviewer3D.get_edited_resource_path() != path:
				error_opening = not load_scene(path)
			else:
				%ScenePreviewer3D.visible = true
			if not error_opening:
				%SwitchViewButton.text = SWITCH_TO_TEXT_TEXT
				%SwitchViewButton.visible = true
		SWITCH_TO_MESH_TEXT:
			if %MeshPreviewer.get_edited_resource_path() != path:
				error_opening = not load_mesh(path)
			else:
				%MeshPreviewer.visible = true
			if not error_opening:
				%SwitchViewButton.text = SWITCH_TO_TEXT_TEXT
				%SwitchViewButton.visible = true
		SWITCH_TO_TEXT_TEXT:
			if %TextView.current_path != path:
				error_opening = not try_text_preview(path, %TextView.recognize(path), current_resource_type)
			else:
				%TextView.visible = true
			if not error_opening:
				text_preview_check_button(path, current_resource_type)
		_:
			print("!!!!!Unknown switch view button text: ", cur_text)
			pass
	if error_opening:
		handle_error_opening(path)
