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

func reset():
	%MediaPlayer.visible = false
	%MediaPlayer.reset()
	%TextView.visible = false
	%TextView.reset()
	%TextureView.visible = false
	%TextureInfo.text = ""
	%TextureRect.texture = null
	%ResourceInfo.text = ""
	%MeshPreviewer.visible = false
	%MeshPreviewer.reset()


var previous_res_info_size = Vector2(0, 0)

func load_texture(path):
	if is_image(path.get_extension().to_lower()):
		%TextureRect.texture = ImageTexture.create_from_image(Image.load_from_file(path))
	else:
		%TextureRect.texture = ResourceCompatLoader.real_load(path, "", ResourceFormatLoader.CACHE_MODE_IGNORE_DEEP)
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

func pop_resource_info(path: String):
	if ResourceCompatLoader.handles_resource(path, ""):
		var info = ResourceCompatLoader.get_resource_info(path)
		var type = info["type"]
		var format = info["format_type"]
		%ResourceInfo.text = RESOURCE_INFO_TEXT_FORMAT % [path, type, format]
		if (info["ver_major"] <= 2):
			var iinfo = GDRESettings.get_import_info_by_dest(path)
			if iinfo:
				%ResourceInfo.text += "\n" + iinfo.to_string()
	else:
		%ResourceInfo.text = "[b]Path:[/b] " + path

func is_mesh(ext):
	return ext == "mesh"

func load_mesh(path):
	var res = ResourceCompatLoader.real_load(path, "", ResourceFormatLoader.CACHE_MODE_IGNORE_DEEP)
	if not res:
		return false
	# check if the resource is a mesh or a descendant of mesh
	if not res.get_class().contains("Mesh"):
		return false
	print(res.get_class())
	%MeshPreviewer.edit(res)
	%MeshPreviewer.visible = true
	return true


func load_resource(path: String) -> void:
	reset()
	var ext = path.get_extension().to_lower()
	var error_opening = false
	var not_supported = false
	if (is_shader(ext)):
		%TextView.load_gdshader(path)
		%TextView.visible = true
	elif (is_code(ext)):
		error_opening = not %TextView.load_code(path)
		%TextView.visible = true
	elif (is_sample(ext)):
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
	elif (is_mesh(ext)):
		error_opening = not load_mesh(path)
	elif (is_text_resource(ext) or is_ini_like(ext)):
		%TextView.load_text_resource(path)
		%TextView.visible = true
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
		%TextView.load_text_resource(temp_path)
		%TextView.visible = true
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
			%TextView.load_text_resource(temp_path)
			%TextView.visible = true
			var da = DirAccess.open(tmp_dir)
			if da:
				da.remove(temp_path)

	elif (is_text(ext) or is_content_text(path)):
		%TextView.load_text(path)
		%TextView.visible = true
		return
	else:
		not_supported = true
	if (not_supported):
		%TextView.load_text_string("Not a supported resource")
		%TextView.visible = true
		%ResourceInfo.text = path
	elif (error_opening):
		%TextView.load_text_string("Error opening resource")
		%TextView.visible = true
		%ResourceInfo.text = path
	if (%ResourceInfo.text == ""):
		pop_resource_info(path)



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
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "svg" || ext == "webp" || ext == "bmp" || ext == "tga" || ext == "tiff" || ext == "hdr" || ext == "ico" || ext == "icns"):
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
	return null


func _on_gdre_resource_preview_visibility_changed() -> void:
	if not self.is_visible_in_tree():
		%MediaPlayer.stop()
		self.reset()
	pass # Replace with function body.

func _ready():
	self.connect("visibility_changed", self._on_gdre_resource_preview_visibility_changed)
	self.connect("resized", self._on_resized)
	previous_res_info_size = Vector2(0, 100)
	%ResourceInfoContainer.custom_minimum_size = previous_res_info_size
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
