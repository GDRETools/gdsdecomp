class_name GDREMediaPlayer
extends Control
var AUDIO_PLAYER: Control = null
var TIME_LABEL: Label = null
var AUDIO_PLAYER_STREAM: AudioStreamPlayer = null
var VIDEO_PLAYER_STREAM: VideoStreamPlayer = null
var VIDEO_VIEW_BOX: Control = null
var VIDEO_ASPECT_RATIO_CONTAINER: AspectRatioContainer = null
var AUDIO_PREVIEW_BOX: GDREAudioPreviewBox = null
var AUDIO_VIEW_BOX: Control = null
var AUDIO_STREAM_INFO: Label = null
var PROGRESS_BAR: Slider = null
var PLAY_BUTTON: Button = null
var PAUSE_BUTTON: Button = null
var STOP_BUTTON: Button = null
var controller: PlayerController = null
var dragging_slider: bool = false
var last_updated_time: float = 0
var last_seek_pos: float = -1
@export var play_icon: Texture = preload("res://gdre_icons/gdre_Play.svg")
@export var pause_icon: Texture = preload("res://gdre_icons/gdre_Pause.svg")
@export var stop_icon: Texture = preload("res://gdre_icons/gdre_Stop.svg")

func reset():
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


class PlayerController:
	enum PlayerType {
		NONE,
		AUDIO,
		VIDEO
	}
	func _init(_player = null):
		pass
	func stop():
		pass
	func play(_pos: float):
		pass
	func is_playing() -> bool:
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
	func stop():
		audio_player.stop()
	func play(pos: float):
		audio_player.play(pos)
	func is_playing() -> bool:
		return audio_player.playing
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
	func stop():
		video_player.stop()
	func play(pos: float):
		if supports_seek():
			video_player.stream_position = pos
		video_player.play()
	func is_playing() -> bool:
		return video_player.is_playing()
	func seek(pos: float):
		if supports_seek():
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
	controller.stop()

func stop():
	controller.stop()
	seek(0)
	update_progress_bar()
	update_audio_preview_pos()

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

func is_video(path) -> bool:
	var ext = path.get_extension().to_lower()
	return ext == "ogv" or ext == "mp4" or ext == "webm"

func is_audio(path) -> bool:
	return !is_video(path)


func is_non_resource_smp(ext, p_type = ""):
	return (ext == "wav" || ext == "ogg" || ext == "mp3")

func load_media(path):
	if is_video(path):
		return load_video(path)
	return load_sample(path)
	
func load_video(path):
	var video_stream: VideoStream = ResourceCompatLoader.real_load(path, "", ResourceFormatLoader.CACHE_MODE_IGNORE_DEEP)
	if (video_stream == null):
		return false
	reset()
	VIDEO_VIEW_BOX.visible = true
	VIDEO_PLAYER_STREAM.stream = video_stream
	VIDEO_PLAYER_STREAM.expand = false
	var texture: Texture2D = VIDEO_PLAYER_STREAM.get_video_texture()
	var sz = texture.get_size()
	VIDEO_PLAYER_STREAM.expand = true
	VIDEO_ASPECT_RATIO_CONTAINER.ratio = sz.x / sz.y
	controller = VideoPlayerController.new(VIDEO_PLAYER_STREAM)
	setup_progress_bar()
	return true


func load_sample(path):
	var audio_stream: AudioStream = null
	var ext = path.get_extension().to_lower()
	if not is_non_resource_smp(ext):
		audio_stream = ResourceCompatLoader.real_load(path, "", ResourceFormatLoader.CACHE_MODE_IGNORE_DEEP)
	else:
		if ext == "wav":
			audio_stream = AudioStreamWAV.load_from_file(path)
		elif ext == "ogg":
			audio_stream = AudioStreamOggVorbis.load_from_file(path)
		elif ext == "mp3":
			audio_stream = AudioStreamMP3.load_from_file(path)
	if (audio_stream == null):
		return false
	reset()
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
	return true

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

func update_audio_preview_pos():
	if controller.get_type() == PlayerController.PlayerType.AUDIO:
		if not is_stream_loaded() or controller.supports_seek():
			return
		AUDIO_PREVIEW_BOX.update_pos(get_playback_position())

func update_text_label():
	if not controller.supports_seek():
		TIME_LABEL.text = "--:-- / --:--"
	else:
		TIME_LABEL.text = time_from_float(PROGRESS_BAR.value, PROGRESS_BAR.step) + " / " + time_from_float(PROGRESS_BAR.max_value, PROGRESS_BAR.step)


func _on_slider_drag_started() -> void:
	var pos = PROGRESS_BAR.value
	seek(pos)
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

func _process(delta: float) -> void:
	if not is_stream_loaded() or not self.visible:
		return
	if is_playing():
		if not dragging_slider:
			update_progress_bar()
		else:
			last_updated_time += delta
			if last_updated_time > 0.10:
				if (abs(last_seek_pos - PROGRESS_BAR.value)) > 0.2:
					seek(PROGRESS_BAR.value)
					last_updated_time = 0
					last_seek_pos = PROGRESS_BAR.value
		update_audio_preview_pos()


func _on_progress_bar_value_changed(_value: float) -> void:
	update_text_label()
	AUDIO_PREVIEW_BOX.update_pos(PROGRESS_BAR.value)

func _on_audio_preview_box_pos_changed(value: float) -> void:
	seek(value)
	PROGRESS_BAR.max_value = AUDIO_PLAYER_STREAM.stream.get_length()
	PROGRESS_BAR.value = value
	update_text_label()

func _ready():
	TIME_LABEL = get_node("BarHBox/TimeLabel")
	PROGRESS_BAR = get_node("BarHBox/ProgressBar")
	PLAY_BUTTON = get_node("MediaControlsHBox/Play")
	PAUSE_BUTTON = get_node("MediaControlsHBox/Pause")
	STOP_BUTTON = get_node("MediaControlsHBox/Stop")
	AUDIO_PLAYER_STREAM = get_node("AudioStreamPlayer")
	AUDIO_PREVIEW_BOX = get_node("AudioViewBox/AudioPreviewBox")
	AUDIO_VIEW_BOX = get_node("AudioViewBox")
	AUDIO_STREAM_INFO = get_node("AudioViewBox/AudioStreamInfo")
	VIDEO_PLAYER_STREAM = get_node("VideoViewBox/AspectRatioContainer/VideoStreamPlayer")
	VIDEO_VIEW_BOX = get_node("VideoViewBox")
	VIDEO_ASPECT_RATIO_CONTAINER = get_node("VideoViewBox/AspectRatioContainer")
	# connect signals
	PROGRESS_BAR.connect("drag_started", self._on_slider_drag_started)
	PROGRESS_BAR.connect("drag_ended", self._on_slider_drag_ended)
	AUDIO_PLAYER_STREAM.connect("finished", self._on_audio_stream_player_finished)
	PLAY_BUTTON.connect("pressed", self.play)
	PAUSE_BUTTON.connect("pressed", self.pause)
	STOP_BUTTON.connect("pressed", self.stop)
	PROGRESS_BAR.connect("value_changed", self._on_progress_bar_value_changed)
	AUDIO_PREVIEW_BOX.connect("pos_changed", self._on_audio_preview_box_pos_changed)

	PLAY_BUTTON.icon = play_icon
	PAUSE_BUTTON.icon = pause_icon
	STOP_BUTTON.icon = stop_icon

	controller = PlayerController.new()
	# load_sample("res://anomaly 105 jun12.ogg")
	# load_sample("res://2.wav")
	# load_media("res://Door_OGV.ogv")
