class_name GDREChildDialog
extends GDREAcceptDialogBase

var POPUP_PARENT_WINDOW: Window = null

var isHiDPI = false
static var void_func: Callable = func(): return

# MUST CALL set_root_window() first!!!
# Called when the node enters the scene tree for the first time.
# called before _ready
func set_root_window(window: Window):
	POPUP_PARENT_WINDOW = window

func show_win():
	# get the screen size
	var safe_area: Rect2i = DisplayServer.get_display_safe_area()
	var center = (safe_area.position + safe_area.size - self.size) / 2
	self.set_position(center)
	set_exclusive(true)
	self.show()

func hide_win():
	self.hide()



func open_subwindow(window: Window):
	window.set_transient(true)
	window.set_exclusive(true)
	window.popup_centered()
	# window.set_unparent_when_invisible(true)

func close_subwindow(window: Window):
	window.hide()
	window.set_exclusive(false)
	window.set_transient(false)

func clear():
	assert(false, "clear() not implemented")

func close():
	assert(false, "close() not implemented")

func confirm():
	assert(false, "closed() not implemented")

func cancelled():
	assert(false, "cancelled() not implemented")


func _on_cancelled():
	cancelled()

func _on_confirmed():
	confirm()

func _on_close_requested():
	close()


func _parent_ready():
	self.connect("close_requested", self._on_close_requested)
	self.connect("confirmed", self._on_confirmed)
	self.connect("canceled", self._on_cancelled)

func _init() -> void:
	self.connect("ready", self._parent_ready)
