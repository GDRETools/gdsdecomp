class_name GDREResourcePreview
extends Control

var TEXT_VIEW: GDRETextEditor = null

var TEXTURE_RECT: TextureRect = null
var TEXTURE_VIEW: Control = null
var TEXTURE_INFO: Label = null
var RESOURCE_INFO: RichTextLabel = null
var VIEWER: Control = null
var VBOX_CONTAINER: VSplitContainer = null
var MEDIA_PLAYER: Control = null



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

func reset():
	MEDIA_PLAYER.visible = false
	MEDIA_PLAYER.reset()
	TEXT_VIEW.visible = false
	TEXTURE_VIEW.visible = false
	TEXTURE_INFO.text = ""
	TEXTURE_RECT.texture = null
	RESOURCE_INFO.text = ""


var previous_size = Vector2(0, 0)

func load_texture(path):
	TEXTURE_RECT.texture = ResourceCompatLoader.real_load(path, "", ResourceFormatLoader.CACHE_MODE_IGNORE_DEEP)
	if (TEXTURE_RECT.texture == null):
		return false
	TEXTURE_RECT.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	TEXTURE_RECT.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	var image = TEXTURE_RECT.texture.get_image()
	var info_text = str(TEXTURE_RECT.texture.get_width()) + "x" + str(TEXTURE_RECT.texture.get_height()) + " " + IMAGE_FORMAT_NAME[image.get_format()] 
	if image.has_mipmaps():
		
		info_text += "\n" + str(image.get_mipmap_count()) + " Mipmaps" + "\n" + "Memory: " + String.humanize_size(image.get_data_size())
	else:
		info_text += "\n" + "No Mipmaps" + "\n" + "Memory: " + String.humanize_size(image.get_data_size())

	TEXTURE_INFO.text = info_text
	TEXTURE_VIEW.visible = true
	return true

func pop_resource_info(path: String):
	if ResourceCompatLoader.handles_resource(path, ""):
		var info = ResourceCompatLoader.get_resource_info(path)
		var type = info["type"]
		var format = info["format_type"]
		RESOURCE_INFO.text = RESOURCE_INFO_TEXT_FORMAT % [path, type, format]
	else:
		RESOURCE_INFO.text = "Path: " + path

func load_resource(path: String) -> void:
	reset()
	var ext = path.get_extension().to_lower()
	var error_opening = false
	var not_supported = false
	if (is_text(ext)):
		TEXT_VIEW.load_text(path)
		TEXT_VIEW.visible = true
		return
	elif (is_shader(ext)):
		TEXT_VIEW.load_gdshader(path)
		TEXT_VIEW.visible = true
	elif (is_code(ext)):
		error_opening = not TEXT_VIEW.load_code(path)
		TEXT_VIEW.visible = true
	elif (is_sample(ext)):
		error_opening = not MEDIA_PLAYER.load_sample(path)
		if not error_opening:
			MEDIA_PLAYER.visible = true
	elif (is_video(ext)):
		error_opening = not MEDIA_PLAYER.load_video(path)
		if not error_opening:
			MEDIA_PLAYER.visible = true
	elif (is_image(ext)):
		error_opening = not load_texture(path)
	elif (is_texture(ext)):
		error_opening = not load_texture(path)
	elif (is_text_resource(ext) or is_ini_like(ext)):
		TEXT_VIEW.load_text_resource(path)
		TEXT_VIEW.visible = true
	elif is_binary_project_settings(path):
		pass
		var loader = ProjectConfigLoader.new()
		var ver_major = GDRESettings.get_ver_major()
		var text_file = "project.godot"
		if (ver_major > 0):
			loader.load_cfb(path, ver_major, 0)
		else:
			if (path.get_file() == "engine.cfb"):
				ver_major = 2
				loader.load_cfb(path, ver_major, 0)
			else:
				var err = loader.load_cfb(path, 4, 3)
				if (err == OK):
					ver_major = 4
				else:
					loader.load_cfb(path, 3, 3)
					ver_major = 3
		if ver_major == 2:
			text_file = "engine.cfg"
		var temp_path = OS.get_temp_dir().path_join(text_file)
		var config = loader.save_cfb(OS.get_temp_dir(), ver_major, 0)
		TEXT_VIEW.load_text_resource(temp_path)
		TEXT_VIEW.visible = true
		var da = DirAccess.open(OS.get_temp_dir())
		if da:
			da.remove(temp_path)
	elif ResourceCompatLoader.handles_resource(path, ""):
		var tmp_dir = OS.get_temp_dir()
		var temp_path = tmp_dir.path_join(path.get_file().get_basename() + "." + str(Time.get_ticks_msec()))
		if (path.get_extension().to_lower() == "scn"):
			temp_path += ".tscn"
		else:
			temp_path += ".tres"
		if ResourceCompatLoader.to_text(path, temp_path) != OK:
			error_opening = true
		else:
			TEXT_VIEW.load_text_resource(temp_path)
			TEXT_VIEW.visible = true
			var da = DirAccess.open(tmp_dir)
			if da:
				da.remove(temp_path)
	else:
		not_supported = true
	if (not_supported):
		TEXT_VIEW.load_text_string("Not a supported resource")
		TEXT_VIEW.visible = true
		RESOURCE_INFO.text = path
	elif (error_opening):
		TEXT_VIEW.load_text_string("Error opening resource")
		TEXT_VIEW.visible = true
		RESOURCE_INFO.text = path
	if (RESOURCE_INFO.text == ""):
		pop_resource_info(path)
		
	

	# TODO: handle binary resources
	# var res_info:Dictionary = ResourceCompatLoader.get_resource_info(path)
	# if (res_info.size() == 0):
	# 	return


	
func is_shader(ext, p_type = ""):
	if (ext == "shader" || ext == "gdshader"):
		return true
	return false

func is_code(ext, p_type = ""):
	if (ext == "gd" || ext == "gdc" || ext == "gde"):
		return true
	return false

func is_text(ext, p_type = ""):
	if (ext == "txt" || ext == "json" || ext == "xml" || ext == "csv" || ext == "html" || ext == "md" || ext == "yml" || ext == "yaml"):
		return true
	return false

func is_text_resource(ext, p_type = ""):
	return ext == "tscn" || ext == "tres"

func is_ini_like(ext, p_type = ""):
	return ext == "cfg" || ext == "remap" || ext == "import" || ext == "gdextension" || ext == "gdnative" || ext == "godot"

func is_sample(ext, p_type = ""):
	if (ext == "oggstr" || ext == "mp3str" || ext == "oggvorbisstr" || ext == "sample" || ext == "wav" || ext == "ogg" || ext == "mp3"):
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
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "svg" || ext == "webp" || ext == "bmp" || ext == "tga" || ext == "tiff" || ext == "hdr" || ext == "ico" || ext == "icns"):
		return true
	return false


func _on_gdre_resource_preview_visibility_changed() -> void:
	if not self.is_visible_in_tree():
		self.reset()
		MEDIA_PLAYER.stop()
	pass # Replace with function body.

func reset_resource_info_size():
	# get the minimum size of the resource info label with all the text it has
	var min_size = RESOURCE_INFO.get_minimum_size()
	var size = 0

func _ready():
	TEXT_VIEW = $VBoxContainer/ResourceView/TextView
	MEDIA_PLAYER = $VBoxContainer/ResourceView/MediaPlayer
	TEXTURE_RECT = $VBoxContainer/ResourceView/TextureView/TextureRect
	TEXTURE_VIEW = $VBoxContainer/ResourceView/TextureView
	TEXTURE_INFO = $VBoxContainer/ResourceView/TextureView/TextureInfo
	VIEWER = $VBoxContainer/ResourceView
	VBOX_CONTAINER = $VBoxContainer
	RESOURCE_INFO = $VBoxContainer/ResourceInfoContainer/ResourceInfo
	# reset()
	self.connect("visibility_changed", self._on_gdre_resource_preview_visibility_changed)
	RESOURCE_INFO.minimum_size_changed.connect(self.reset_resource_info_size)
	var min_size = RESOURCE_INFO.get_minimum_size()
	self.connect("resized", self._on_resized)
	previous_size = Vector2(0, self.size.y / 2 - VBOX_CONTAINER.split_offset)
	$VBoxContainer/ResourceInfoContainer.custom_minimum_size = previous_size
	#_on_resized()
	# TODO: remove me
	#load_resource("res://gdre_file_tree.gd")
	# load_resource("res://.godot/imported/gdre_Script.svg-4c68c9c5e02f5e7a41dddea59a95e245.ctex")
	#load_resource("res://.godot/imported/anomaly 105 jun12.ogg-d3e939934d210d1a4e1f9d2d34966046.oggvorbisstr")

# audio player stuff


func _on_v_box_container_drag_started() -> void:
	$VBoxContainer/ResourceInfoContainer.custom_minimum_size = Vector2(0,0)
	previous_size = $VBoxContainer/ResourceInfoContainer.size


func _on_v_box_container_drag_ended() -> void:
	previous_size = $VBoxContainer/ResourceInfoContainer.size
	$VBoxContainer/ResourceInfoContainer.custom_minimum_size = $VBoxContainer/ResourceInfoContainer.size


func _on_resized() -> void:
	var current_size = self.size
	var curr_y_middle: float = current_size.y / 2.0
	
	var new_y: float =  curr_y_middle - previous_size.y
	print("Current size: ", current_size)
	print("Current resource info size: ", previous_size)
	print("Current y middle: ", curr_y_middle)
	print("New y: ", new_y)
	VBOX_CONTAINER.split_offset = new_y
	pass # Replace with function body.
