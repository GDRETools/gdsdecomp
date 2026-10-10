class_name GDREMediaPlayer
extends GDREPreviewer

var MAIN_MARGIN: MarginContainer
var MAIN_VBOX: VBoxContainer
var TAB_CONTAINER: TabContainer
var TIME_LABEL: Label
var PROGRESS_BAR: Slider
var PLAY_BUTTON: Button
var PAUSE_BUTTON: Button
var STOP_BUTTON: Button
var DEFAULT_BOX: Control
var AUDIO_PLAYER_STREAM: AudioStreamPlayer
var AUDIO_PREVIEW_BOX: Control
var AUDIO_VIEW_BOX: Control
var AUDIO_STREAM_INFO: Label
var VIDEO_PLAYER_STREAM: VideoStreamPlayer
var VIDEO_VIEW_BOX: Control
var VIDEO_ASPECT_RATIO_CONTAINER: AspectRatioContainer

var controller: PlayerController = null
var dragging_slider: bool = false
var last_updated_time: float = 0
var last_seek_pos: float = -1


const DEFAULT_PLAY_ICON_TEXT = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><path fill="#e0e0e0" d="M4 12a1 1 0 0 0 1.555.832l6-4a1 1 0 0 0 0-1.664l-6-4A1 1 0 0 0 4 4z"/></svg>'
const DEFAULT_PAUSE_ICON_TEXT = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><path fill="#e0e0e0" d="M4 3a1 1 0 0 0-1 1v8a1 1 0 0 0 1 1h2a1 1 0 0 0 1-1V4a1 1 0 0 0-1-1zm6 0a1 1 0 0 0-1 1v8a1 1 0 0 0 1 1h2a1 1 0 0 0 1-1V4a1 1 0 0 0-1-1z"/></svg>'
const DEFAULT_STOP_ICON_TEXT = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="10" height="10" x="3" y="3" fill="#e0e0e0" rx="1"/></svg>'

var default_icon_data: Dictionary = {
	"play": DEFAULT_PLAY_ICON_TEXT,
	"pause": DEFAULT_PAUSE_ICON_TEXT,
	"stop": DEFAULT_STOP_ICON_TEXT
}

var default_icon_textures: Dictionary = {
	"play": null,
	"pause": null,
	"stop": null
}


var current_edited_resource_path: String = ""

func _get_previewer_name() -> String:
	return "media"

func _reset():
	if controller:
		controller.stop()
	controller = PlayerController.new()
	AUDIO_VIEW_BOX.visible = false
	AUDIO_PREVIEW_BOX.reset()
	AUDIO_PLAYER_STREAM.stream = null
	VIDEO_VIEW_BOX.visible = false
	VIDEO_PLAYER_STREAM.stream = null
	setup_progress_bar()
	last_updated_time = 0
	last_updated_time = -1
	dragging_slider = false
	current_edited_resource_path = ""

func _can_edit(path: String, p_type: String) -> bool:
	return is_video(path, p_type) or is_audio(path, p_type)

func _edit(resource: Resource) -> Error:
	return load_media_stream(resource)

func _edit_from_path(path: String) -> Error:
	return load_media(path)

func _get_load_type() -> int:
	return ResourceCompatLoader.REAL_LOAD

func _get_edited_resource_path() -> String:
	return current_edited_resource_path

class PlayerController:
	enum PlayerType {
		NONE,
		AUDIO,
		VIDEO
	}
	signal finished()
	func _on_finished():
		finished.emit()

	func _init(_player = null):
		pass
	func stop():
		pass
	func pause():
		pass
	func play(_pos: float):
		pass
	func is_playing() -> bool:
		return false
	func is_paused() -> bool:
		return false
	func seek(_pos: float):
		pass
	func is_stream_loaded() -> bool:
		return false
	func get_stream_length() -> float:
		return 0
	func get_playback_position() -> float:
		return 0
	func supports_seek() -> bool:
		return false
	func get_type() -> PlayerType:
		return PlayerType.NONE
	pass

class AudioPlayerController extends PlayerController:
	var audio_player: AudioStreamPlayer = null
	func _init(p_audio_player: AudioStreamPlayer):
		audio_player = p_audio_player
		audio_player.finished.connect(_on_finished)
	func stop():
		audio_player.stop()
		seek(0)
	func pause():
		audio_player.stop()
	func play(pos: float):
		audio_player.play(pos)
	func is_playing() -> bool:
		return audio_player.playing
	func is_paused() -> bool:
		# Can't pause audio streams
		return false
	func seek(pos: float):
		audio_player.seek(pos)
	func is_stream_loaded() -> bool:
		return !(not audio_player or not audio_player.stream)
	func get_stream_length() -> float:
		if not is_stream_loaded():
			return 0
		return audio_player.stream.get_length()
	func get_playback_position() -> float:
		if not is_stream_loaded():
			return 0
		return audio_player.get_playback_position()
	func supports_seek() -> bool:
		return true
	func get_type() -> PlayerType:
		return PlayerType.AUDIO

class VideoPlayerController extends PlayerController:
	var video_player: VideoStreamPlayer = null

	func _init(p_video_player: VideoStreamPlayer):
		video_player = p_video_player
		video_player.finished.connect(self._on_finished)
		# play and then stop to queue up the first frame
		play(0)
		stop()
	func stop():
		# seek 0 first before stopping to queue up the first frame
		seek(0)
		video_player.stop()
		video_player.paused = false
	func pause():
		video_player.paused = true
	func play(pos: float):
		video_player.paused = false
		video_player.play()
		if supports_seek():
			video_player.stream_position = pos
	func is_playing() -> bool:
		return video_player.is_playing()
	func is_paused() -> bool:
		return video_player.paused
	func seek(pos: float):
		if supports_seek():
			if not is_playing():
				# play and then pause to queue up the frame at the given position
				play(pos)
				pause()
			# seeking is expensive in video streams
			elif video_player.stream_position != pos:
				video_player.stream_position = pos
	func is_stream_loaded() -> bool:
		return !(not video_player or not video_player.stream)
	func get_stream_length() -> float:
		if not is_stream_loaded():
			return 0
		return video_player.get_stream_length()
	func get_playback_position() -> float:
		if not is_stream_loaded():
			return 0
		return video_player.stream_position
	func supports_seek() -> bool:
		if not is_stream_loaded():
			return false
		if video_player.get_stream_length() == 0:
			return false
		return true
	func get_type() -> PlayerType:
		return PlayerType.VIDEO

func pause():
	controller.pause()

func stop():
	controller.stop()
	last_seek_pos = 0
	update_progress_bar()

func play():
	var pos = PROGRESS_BAR.value
	if (not controller.supports_seek() or PROGRESS_BAR.value == PROGRESS_BAR.max_value):
		pos = 0
	controller.play(pos)
	update_progress_bar()

func is_playing() -> bool:
	return controller.is_playing()

func seek(pos: float):
	if not controller.supports_seek():
		return
	var length = get_stream_length()
	if (length - pos < PROGRESS_BAR.step):
		pos = length - PROGRESS_BAR.step
	controller.seek(pos)
	last_seek_pos = pos

func is_stream_loaded() -> bool:
	return controller.is_stream_loaded()

func get_stream_length() -> float:
	return controller.get_stream_length()

func get_playback_position() -> float:
	return controller.get_playback_position()


const sample_info_box_text_format = """WAV
Sample Rate: %d Hz
Channels: %d
Format: %s
Loop Mode: %s"""

func loopmode_to_string(mode: int) -> String:
	match mode:
		AudioStreamWAV.LOOP_DISABLED:
			return "Disabled"
		AudioStreamWAV.LOOP_FORWARD:
			return "Forward"
		AudioStreamWAV.LOOP_PINGPONG:
			return "PingPong"
		AudioStreamWAV.LOOP_BACKWARD:
			return "Backward"
	return "Unknown"

func sample_format_to_string(format: int) -> String:
	match format:
		AudioStreamWAV.FORMAT_8_BITS:
			return "PCM 8-bit"
		AudioStreamWAV.FORMAT_16_BITS:
			return "PCM 16-bit"
		AudioStreamWAV.FORMAT_IMA_ADPCM:
			return "ADPCM"
		AudioStreamWAV.FORMAT_QOA:
			return "Quite OK"
	return "Unknown"

func is_supported_video_format(path) -> bool:
	var ext = path.get_extension().to_lower()
	return ext == "ogv" || ext == "ogm"


func is_non_resource_smp(ext, p_type = ""):
	return (ext == "wav" || ext == "ogg" || ext == "mp3")

func is_video(path, p_type = "") -> bool:
	if is_supported_video_format(path):
		return true
	if p_type.is_empty():
		return false
	return ClassDB.is_parent_class(p_type, "VideoStream")

func is_audio(path, p_type = "") -> bool:
	var ext = path.get_extension().to_lower()

	if (ext == "oggstr" || ext == "mp3str" || ext == "oggvorbisstr" || ext == "sample" || ext == "smp" || is_non_resource_smp(ext, p_type)):
		return true
	if p_type.is_empty():
		return false
	return ClassDB.is_parent_class(p_type, "AudioStream")

func load_media(path):
	if is_supported_video_format(path):
		return load_video(path)
	elif is_audio(path):
		return load_sample(path)
	printerr("Could not load unsupported stream: " + path)
	return ERR_INVALID_PARAMETER

func load_media_stream(media: Resource):
	if media is VideoStream:
		return load_video_stream(media)
	elif media is AudioStream:
		return load_audio_stream(media)
	printerr("Could not load unsupported stream of type " + media.get_class() + ": " + media.get_path())
	return ERR_INVALID_PARAMETER

func load_video(path):
	var video_stream: VideoStream = ResourceCompatLoader.real_load(path, "", ResourceCompatLoader.CACHE_MODE_IGNORE_DEEP)
	if video_stream == null:
		return ERR_FILE_CANT_OPEN
	return load_media(video_stream)


func load_video_stream(video_stream: VideoStream):
	reset()
	if (video_stream == null):
		return ERR_INVALID_PARAMETER
	current_edited_resource_path = video_stream.get_path()
	VIDEO_VIEW_BOX.visible = true
	VIDEO_PLAYER_STREAM.stream = video_stream
	VIDEO_PLAYER_STREAM.expand = false
	var texture: Texture2D = VIDEO_PLAYER_STREAM.get_video_texture()
	var sz = texture.get_size()
	VIDEO_PLAYER_STREAM.expand = true
	VIDEO_ASPECT_RATIO_CONTAINER.ratio = sz.x / sz.y
	controller = VideoPlayerController.new(VIDEO_PLAYER_STREAM)
	setup_progress_bar()
	controller.finished.connect(self.update_progress_bar)
	return OK


func load_sample(path):
	var audio_stream: AudioStream = null
	var ext = path.get_extension().to_lower()
	if not is_non_resource_smp(ext):
		audio_stream = ResourceCompatLoader.real_load(path, "", ResourceCompatLoader.CACHE_MODE_IGNORE_DEEP)
	else:
		if ext == "wav":
			audio_stream = AudioStreamWAV.load_from_file(path)
		elif ext == "ogg":
			audio_stream = AudioStreamOggVorbis.load_from_file(path)
		elif ext == "mp3":
			audio_stream = AudioStreamMP3.load_from_file(path)
		if audio_stream:
			audio_stream.set_path_cache(path)
	if audio_stream == null:
		return ERR_FILE_CANT_OPEN
	return load_audio_stream(audio_stream)

func load_audio_stream(audio_stream: AudioStream):
	reset()
	if (audio_stream == null):
		return ERR_INVALID_PARAMETER
	current_edited_resource_path = audio_stream.get_path()
	AUDIO_VIEW_BOX.visible = true
	AUDIO_PLAYER_STREAM.stream = audio_stream
	controller = AudioPlayerController.new(AUDIO_PLAYER_STREAM)
	AUDIO_PREVIEW_BOX.set_stream(AUDIO_PLAYER_STREAM.stream)
	# check if it's an AudioStreamSample
	if (audio_stream.get_class() == "AudioStreamWAV"):
		var sample: AudioStreamWAV = audio_stream

		AUDIO_STREAM_INFO.text = sample_info_box_text_format % [sample.mix_rate, 2 if sample.stereo else 1, sample_format_to_string(sample.format),  loopmode_to_string(sample.loop_mode)]
		if (sample.loop_mode != AudioStreamWAV.LOOP_DISABLED):
			AUDIO_STREAM_INFO.text += "\nLoop Begin: " + str(sample.loop_begin) + "\nLoop End: " + str(sample.loop_end)
	elif (audio_stream.get_class() == "AudioStreamOggVorbis" or audio_stream.get_class() == "AudioStreamMP3"):
		var info_string = ""
		var sampling_rate: String = "unknown"
		if audio_stream.get_class() == "AudioStreamOggVorbis":
			sampling_rate = str(int(audio_stream.packet_sequence.sampling_rate))
			info_string += "Ogg Vorbis"
			info_string += "\nSample Rate: " + sampling_rate + " Hz"
		elif audio_stream.get_class() == "AudioStreamMP3":
			info_string += "MP3"
			# no sample rate information for MP3 in Godot yet
		if (audio_stream.bpm != 0):
			info_string += "\nBPM: " + str(audio_stream.bpm)
		if (audio_stream.bar_beats != 4):
			info_string += "\nBar Beats: " + str(audio_stream.bar_beats)
		if (audio_stream.beat_count != 0):
			info_string += "\nBeat Count: " + str(audio_stream.beat_count)
		if (audio_stream.loop):
			info_string += "\nLoop: Yes"
			info_string += "\nLoop Offset: " + str(audio_stream.loop_offset)
		else:
			info_string += "\nLoop: No"
		AUDIO_STREAM_INFO.text = info_string
	else:
		AUDIO_STREAM_INFO.text = ""
	setup_progress_bar()
	controller.finished.connect(self.update_progress_bar)
	return OK

func time_from_float(time: float, step: float) -> String:
	var minutes = int(time / 60)
	var seconds = int(time) % 60
	var microseconds = int((time - int(time)) * 1000)
	var rounded_microseconds_zero_padding = max(0, 4 - str(int(1000 * step)).length())
	var ret = str(minutes) + ":" + str(seconds).pad_zeros(2)
	if rounded_microseconds_zero_padding > 0:
		var step_val = 1000 * step
		var rounded_microseconds = int(round(float(microseconds) / int(step_val)))
		ret += "." + str(rounded_microseconds).pad_zeros(rounded_microseconds_zero_padding)
	return ret

func setup_progress_bar():
	if (get_stream_length() < 30):
		PROGRESS_BAR.step = 0.01
	else:
		PROGRESS_BAR.step = 0.1
	PROGRESS_BAR.value = get_playback_position()
	PROGRESS_BAR.max_value = get_stream_length()
	PROGRESS_BAR.editable = controller.supports_seek()
	if not controller.supports_seek():
		PAUSE_BUTTON.disabled = true
	else:
		PAUSE_BUTTON.disabled = false
	update_text_label()

func update_progress_bar():
	if not PROGRESS_BAR.editable or not is_stream_loaded():
		return
	PROGRESS_BAR.max_value = get_stream_length()
	PROGRESS_BAR.value = get_playback_position()
	update_text_label()

func update_text_label():
	if not controller.supports_seek():
		TIME_LABEL.text = "--:-- / --:--"
	else:
		TIME_LABEL.text = time_from_float(PROGRESS_BAR.value, PROGRESS_BAR.step) + " / " + time_from_float(PROGRESS_BAR.max_value, PROGRESS_BAR.step)


func _on_slider_drag_started() -> void:
	var pos = PROGRESS_BAR.value
	seek(pos)
	last_seek_pos = pos
	dragging_slider = true
	pass # Replace with function body.


func _on_slider_drag_ended(value_changed: bool) -> void:
	if value_changed:
		var pos = PROGRESS_BAR.value
		if last_seek_pos != pos:
			seek(pos)
	dragging_slider = false


	pass # Replace with function body.


func _on_audio_stream_player_finished() -> void:
	PROGRESS_BAR.value = 0
	pass # Replace with function body.

func get_drag_delta_threshold():
	if controller.get_type() == PlayerController.PlayerType.VIDEO:
		return 0.1
	return 0.1

func get_drag_pos_threshold():
	var min_t = 0.05
	if controller.get_type() == PlayerController.PlayerType.VIDEO:
		min_t = 0.1
	return max(PROGRESS_BAR.step, min_t)

func _process(delta: float) -> void:
	if not is_stream_loaded() or not self.visible:
		return
	if controller.is_playing() or controller.is_paused():
		if not dragging_slider:
			if controller.is_playing() and not controller.is_paused():
				update_progress_bar()
		else: # dragging slider
			last_updated_time += delta
			if last_updated_time > get_drag_delta_threshold():
				if (abs(last_seek_pos - PROGRESS_BAR.value)) > get_drag_pos_threshold():
					seek(PROGRESS_BAR.value)
					last_updated_time = 0
					last_seek_pos = PROGRESS_BAR.value

func _on_progress_bar_value_changed(_value: float) -> void:
	update_text_label()
	AUDIO_PREVIEW_BOX.update_pos(PROGRESS_BAR.value)

func _on_audio_preview_box_pos_changed(value: float) -> void:
	seek(value)
	PROGRESS_BAR.max_value = AUDIO_PLAYER_STREAM.stream.get_length()
	PROGRESS_BAR.value = value
	update_text_label()

func _ready():
	# connect signals
	PROGRESS_BAR.connect("drag_started", self._on_slider_drag_started)
	PROGRESS_BAR.connect("drag_ended", self._on_slider_drag_ended)
	AUDIO_PLAYER_STREAM.connect("finished", self._on_audio_stream_player_finished)
	PLAY_BUTTON.connect("pressed", self.play)
	PAUSE_BUTTON.connect("pressed", self.pause)
	STOP_BUTTON.connect("pressed", self.stop)
	PROGRESS_BAR.connect("value_changed", self._on_progress_bar_value_changed)
	AUDIO_PREVIEW_BOX.connect("pos_changed", self._on_audio_preview_box_pos_changed)
	self.theme_changed.connect(self._on_theme_changed)
	_on_theme_changed()
	reset()
	# load_media("/Users/nikita/Workspace/godot-ws/test-decomps/_test_files/Door_OGV.ogv")
	#load_media("/Users/nikita/Desktop/_test_individual_export/gearhead.ogv")
	#load_media("/Users/nikita/Downloads/4K_resolution_sample.ogv")
	# load_media('/Users/nikita/Desktop/_test_individual_export/A moment of silent.ogg')
	# load_sample("res://anomaly 105 jun12.ogg")
	# load_sample("res://2.wav")
	# load_media("res://Door_OGV.ogv")

func _init():
	# Root (self) layout. Mirrors the original gdre_media_player.tscn root.

	# MainMarginContainer
	MAIN_MARGIN = MarginContainer.new()
	MAIN_MARGIN.name = "MainMarginContainer"
	MAIN_MARGIN.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	MAIN_MARGIN.grow_horizontal = Control.GROW_DIRECTION_BOTH
	MAIN_MARGIN.grow_vertical = Control.GROW_DIRECTION_BOTH
	MAIN_MARGIN.add_theme_constant_override("margin_left", 0)
	MAIN_MARGIN.add_theme_constant_override("margin_right", 0)
	MAIN_MARGIN.add_theme_constant_override("margin_top", 0)
	MAIN_MARGIN.add_theme_constant_override("margin_bottom", 20)
	add_child(MAIN_MARGIN)

	# VBoxContainer
	MAIN_VBOX = VBoxContainer.new()
	MAIN_VBOX.name = "VBoxContainer"
	MAIN_MARGIN.add_child(MAIN_VBOX)

	# TabContainer (hosts the Default/Audio/Video sub-views)
	TAB_CONTAINER = TabContainer.new()
	TAB_CONTAINER.name = "TabContainer"
	TAB_CONTAINER.size_flags_vertical = Control.SIZE_EXPAND_FILL
	TAB_CONTAINER.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	TAB_CONTAINER.current_tab = 0
	TAB_CONTAINER.tabs_visible = false
	MAIN_VBOX.add_child(TAB_CONTAINER)

	# DefaultBox (empty placeholder tab shown when nothing is loaded)
	DEFAULT_BOX = Control.new()
	DEFAULT_BOX.name = "DefaultBox"
	DEFAULT_BOX.size_flags_vertical = Control.SIZE_EXPAND_FILL
	TAB_CONTAINER.add_child(DEFAULT_BOX)

	# AudioViewBox
	AUDIO_VIEW_BOX = Control.new()
	AUDIO_VIEW_BOX.name = "AudioViewBox"
	AUDIO_VIEW_BOX.visible = false
	AUDIO_VIEW_BOX.size_flags_vertical = Control.SIZE_EXPAND_FILL
	TAB_CONTAINER.add_child(AUDIO_VIEW_BOX)

	# AudioPreviewBox (waveform renderer)
	AUDIO_PREVIEW_BOX = GDREAudioPreviewBox.new()
	AUDIO_PREVIEW_BOX.name = "AudioPreviewBox"
	AUDIO_PREVIEW_BOX.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	AUDIO_PREVIEW_BOX.grow_horizontal = Control.GROW_DIRECTION_BOTH
	AUDIO_PREVIEW_BOX.grow_vertical = Control.GROW_DIRECTION_BOTH
	AUDIO_PREVIEW_BOX.color = Color(0.129412, 0.14902, 0.176471, 1)
	AUDIO_VIEW_BOX.add_child(AUDIO_PREVIEW_BOX)

	# AudioStreamInfo (bottom-right overlay label)
	AUDIO_STREAM_INFO = Label.new()
	AUDIO_STREAM_INFO.name = "AudioStreamInfo"
	AUDIO_STREAM_INFO.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_RIGHT)
	AUDIO_STREAM_INFO.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	AUDIO_STREAM_INFO.grow_vertical = Control.GROW_DIRECTION_BEGIN
	AUDIO_STREAM_INFO.layout_direction = Control.LAYOUT_DIRECTION_LTR
	AUDIO_STREAM_INFO.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 1))
	AUDIO_STREAM_INFO.add_theme_color_override("font_outline_color", Color(0, 0, 0, 1))
	AUDIO_STREAM_INFO.add_theme_constant_override("outline_size", 8)
	AUDIO_STREAM_INFO.add_theme_font_size_override("font_size", 14)
	AUDIO_STREAM_INFO.text = "Sampling Rate: 48000\nLoop: No"
	AUDIO_VIEW_BOX.add_child(AUDIO_STREAM_INFO)

	# AudioStreamPlayer
	AUDIO_PLAYER_STREAM = AudioStreamPlayer.new()
	AUDIO_PLAYER_STREAM.name = "AudioStreamPlayer"
	AUDIO_VIEW_BOX.add_child(AUDIO_PLAYER_STREAM)

	# VideoViewBox
	VIDEO_VIEW_BOX = Control.new()
	VIDEO_VIEW_BOX.name = "VideoViewBox"
	VIDEO_VIEW_BOX.visible = false
	VIDEO_VIEW_BOX.size_flags_vertical = Control.SIZE_EXPAND_FILL
	TAB_CONTAINER.add_child(VIDEO_VIEW_BOX)

	# AspectRatioContainer (keeps the video at its native aspect ratio)
	VIDEO_ASPECT_RATIO_CONTAINER = AspectRatioContainer.new()
	VIDEO_ASPECT_RATIO_CONTAINER.name = "AspectRatioContainer"
	VIDEO_ASPECT_RATIO_CONTAINER.clip_contents = true
	VIDEO_ASPECT_RATIO_CONTAINER.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	VIDEO_ASPECT_RATIO_CONTAINER.grow_horizontal = Control.GROW_DIRECTION_BOTH
	VIDEO_ASPECT_RATIO_CONTAINER.grow_vertical = Control.GROW_DIRECTION_BOTH
	VIDEO_ASPECT_RATIO_CONTAINER.ratio = 1.7778
	VIDEO_VIEW_BOX.add_child(VIDEO_ASPECT_RATIO_CONTAINER)

	# BG (opaque black backing behind the video)
	var bg := Panel.new()
	bg.name = "BG"
	var bg_sb := StyleBoxFlat.new()
	bg_sb.bg_color = Color(0, 0, 0, 1)
	bg.add_theme_stylebox_override("panel", bg_sb)
	VIDEO_ASPECT_RATIO_CONTAINER.add_child(bg)

	# VideoStreamPlayer
	VIDEO_PLAYER_STREAM = VideoStreamPlayer.new()
	VIDEO_PLAYER_STREAM.name = "VideoStreamPlayer"
	VIDEO_PLAYER_STREAM.custom_minimum_size = Vector2(16, 9)
	VIDEO_PLAYER_STREAM.expand = true
	VIDEO_ASPECT_RATIO_CONTAINER.add_child(VIDEO_PLAYER_STREAM)

	# BarMarginContainer (hosts the time label + progress bar row)
	var bar_margin := MarginContainer.new()
	bar_margin.name = "BarMarginContainer"
	bar_margin.add_theme_constant_override("margin_left", 40)
	bar_margin.add_theme_constant_override("margin_top", 8)
	bar_margin.add_theme_constant_override("margin_right", 40)
	bar_margin.add_theme_constant_override("margin_bottom", 0)
	MAIN_VBOX.add_child(bar_margin)

	var bar_hbox := HBoxContainer.new()
	bar_hbox.name = "BarHBox"
	bar_margin.add_child(bar_hbox)

	TIME_LABEL = Label.new()
	TIME_LABEL.name = "TimeLabel"
	TIME_LABEL.layout_direction = Control.LAYOUT_DIRECTION_RTL
	TIME_LABEL.text = "0:00.0 / 0:00.0"
	TIME_LABEL.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	TIME_LABEL.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	bar_hbox.add_child(TIME_LABEL)

	var bar_spacer := Label.new()
	bar_spacer.name = "Spacer"
	bar_spacer.text = " "
	bar_hbox.add_child(bar_spacer)

	PROGRESS_BAR = HSlider.new()
	PROGRESS_BAR.name = "ProgressBar"
	PROGRESS_BAR.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	PROGRESS_BAR.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	PROGRESS_BAR.step = 0.1
	bar_hbox.add_child(PROGRESS_BAR)

	# MediaControlsHBox (Play / Pause / Stop)
	var media_controls := HBoxContainer.new()
	media_controls.name = "MediaControlsHBox"
	media_controls.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	media_controls.alignment = BoxContainer.ALIGNMENT_CENTER
	MAIN_VBOX.add_child(media_controls)

	PLAY_BUTTON = Button.new()
	PLAY_BUTTON.name = "Play"
	PLAY_BUTTON.theme_type_variation = &"FlatButton"
	PLAY_BUTTON.flat = true
	media_controls.add_child(PLAY_BUTTON)

	var ctrl_spacer_1 := Control.new()
	ctrl_spacer_1.name = "Spacer"
	ctrl_spacer_1.custom_minimum_size = Vector2(10, 0)
	media_controls.add_child(ctrl_spacer_1)

	PAUSE_BUTTON = Button.new()
	PAUSE_BUTTON.name = "Pause"
	PAUSE_BUTTON.size_flags_horizontal = Control.SIZE_EXPAND | Control.SIZE_SHRINK_CENTER
	PAUSE_BUTTON.theme_type_variation = &"FlatButton"
	PAUSE_BUTTON.disabled = true
	PAUSE_BUTTON.flat = true
	media_controls.add_child(PAUSE_BUTTON)

	var ctrl_spacer_2 := Control.new()
	ctrl_spacer_2.name = "Spacer2"
	ctrl_spacer_2.custom_minimum_size = Vector2(10, 0)
	media_controls.add_child(ctrl_spacer_2)

	STOP_BUTTON = Button.new()
	STOP_BUTTON.name = "Stop"
	STOP_BUTTON.theme_type_variation = &"FlatButton"
	STOP_BUTTON.flat = true
	media_controls.add_child(STOP_BUTTON)

func get_icon_texture(icon_name: String) -> Texture:
	var icon = get_theme_icon(icon_name, "GDREMediaPlayer")
	if is_instance_valid(icon):
		return icon
	var icon_texture = default_icon_textures.get(icon_name, null)
	if icon_texture != null:
		return icon_texture
	var icon_data = default_icon_data.get(icon_name, null)
	if icon_data == null:
		printerr("Could not find default icon data for icon: " + icon_name)
		return null
	icon_texture = DPITexture.new()
	icon_texture.base_scale = 2.0
	icon_texture.set_source(icon_data)
	default_icon_textures[icon_name] = icon_texture
	return icon_texture


func _on_theme_changed():
	PLAY_BUTTON.icon = get_icon_texture("play")
	PAUSE_BUTTON.icon = get_icon_texture("pause")
	STOP_BUTTON.icon = get_icon_texture("stop")
	var font_color = get_theme_color("font_color", "GDREMediaPlayer")
	var font_outline_color = get_theme_color("font_outline_color", "GDREMediaPlayer")
	var font_shadow_color = get_theme_color("font_shadow_color", "GDREMediaPlayer")
	TIME_LABEL.add_theme_color_override("font_color", font_color)
	TIME_LABEL.add_theme_color_override("font_outline_color", font_outline_color)
	TIME_LABEL.add_theme_color_override("font_shadow_color", font_shadow_color)
	AUDIO_STREAM_INFO.add_theme_color_override("font_color", font_color)
	AUDIO_STREAM_INFO.add_theme_color_override("font_outline_color", font_outline_color)
	AUDIO_STREAM_INFO.add_theme_color_override("font_shadow_color", font_shadow_color)
	pass
