class_name GDREFileTree
extends Tree
enum SortType {
	SORT_NAME_ASCENDING,
	SORT_NAME_DESCENDING,
	SORT_SIZE_DESCENDING,
	SORT_SIZE_ASCENDING,
	SORT_INFO,
	SORT_REVERSE_INFO,
}

enum ColType {
	NAME,
	SIZE,
	INFO,
}

@export var file_icon: Texture2D = preload("res://gdre_icons/gdre_File.svg")
@export var file_ok: Texture2D = preload("res://gdre_icons/gdre_FileOk.svg")
@export var file_broken: Texture2D = preload("res://gdre_icons/gdre_FileBroken.svg")
@export var folder_icon: Texture2D = get_theme_icon("folder", "FileDialog")
@export var root_name: String = "res://"
# @export var editable_only_when_checkbox_clicked: bool = true
# make setters for the export

@export var show_copy_paths_in_right_click_menu: bool = true
# Enables a check mark on the first column of the tree.
@export var check_mode: bool = true:
	set(val):
		if check_mode == val:
			return
		check_mode = val
		if val:
			GodotREEditorStandalone.tree_set_edit_checkbox_cell_only_when_checkbox_is_pressed(self, editable_only_when_checkbox_clicked)
		else:
			GodotREEditorStandalone.tree_set_edit_checkbox_cell_only_when_checkbox_is_pressed(self, false)
		var items = get_all_file_items() + get_all_folder_items()
		if items.is_empty():
			return
		for item in items:
			if not check_mode:
				item.set_cell_mode(_name_col, TreeItem.CELL_MODE_CHECK)
			else:
				item.set_cell_mode(_name_col, TreeItem.CELL_MODE_STRING)
	get:
		return check_mode

@export var flat_mode: bool = false:
	set(val):
		flat_mode = val
		if flat_mode:
			self.hide_root = true

		else:
			self.hide_root = false
		if not root:
			return
		root.set_text(_name_col, root_name if not flat_mode else "")
		var items = get_all_file_items()
		if items.is_empty():
			return
		for item in items:
			var size = item.get_metadata(_size_col) if _size_col_exists else -1
			var info = item.get_text(_info_col) if _info_col_exists else ""
			var error_str = ""
			if item.get_icon(_name_col) == file_broken:
				error_str = item.get_tooltip_text(_name_col)
			add_file_tree_item(item.get_metadata(_name_col), item.get_icon(_name_col), size or -1, error_str, info)
			item.get_parent().remove_child(item)
		if flat_mode:
			for item in get_all_folder_items(get_root()):
				item.get_parent().remove_child(item)
				items.append(item)
		for item in items:
			if item:
				item.free()
		sort_entire_tree()

	get:
		return flat_mode
@export var editable_only_when_checkbox_clicked: bool = true:
	set(val):
		editable_only_when_checkbox_clicked = val
		GodotREEditorStandalone.tree_set_edit_checkbox_cell_only_when_checkbox_is_pressed(self, editable_only_when_checkbox_clicked)
	get:
		return editable_only_when_checkbox_clicked
@export var right_click_outline_color: Color = Color(0.8, 0.8, 0.8, 0.9)

func set_column_map_cache(val: Dictionary):
	assert(val.has(ColType.NAME), "Column map must have a mapping for Name!")
	_name_col = val.get(ColType.NAME, -1)
	_size_col = val.get(ColType.SIZE, -1)
	_info_col = val.get(ColType.INFO, -1)
	_size_col_exists = _size_col != -1
	_info_col_exists = _info_col != -1
	columns = val.size()

@export var columnMap: Dictionary[ColType, int] = { ColType.NAME: 0, ColType.SIZE: 1}: 
	set(val):
		set_column_map_cache(val)
		columnMap = val
	get:
		return columnMap

@export var nameColumnName: String = "File Name"
@export var sizeColumnName: String = "Size"
@export var infoColumnName: String = "Info"
		
# cached column positions for performance reasons
var _name_col = 0
var _size_col = -1
var _info_col = -1
var _size_col_exists = false
var _info_col_exists = false

var FILTER_DELAY = 0.25
var LARGE_PCK = 5000
var prev_filter_string: String = ""
var timer: SceneTreeTimer = null

# var isHiDPI = DisplayServer.screen_get_dpi() >= 240
var isHiDPI = false
var root: TreeItem = null
var userroot: TreeItem = null
var num_files:int = 0
var num_broken:int = 0
var num_malformed:int = 0
var right_click_menu: PopupMenu = null
var right_clicked_item: TreeItem = null

var custom_right_click_map: Dictionary[int, String] = {}
var custom_right_click_items: Dictionary[String, Callable] = {}


enum {
	POPUP_COPY_PATHS,
	POPUP_CHECK_ALL,
	POPUP_UNCHECK_ALL,
	POPUP_SEPERATOR,
	POPUP_FOLD_ALL,
	POPUP_UNFOLD_ALL,
	POPUP_CUSTOM_SEPERATOR,
}
var last_open_id = POPUP_CUSTOM_SEPERATOR + 1
var current_sort = SortType.SORT_NAME_ASCENDING

func get_highlighted_items() -> Array:
	var highlighted_items = []
	if (select_mode == SELECT_MULTI):
		var item = self.get_next_selected(get_root())
		while (item):
			highlighted_items.append(item)
			item = self.get_next_selected(item)
	if highlighted_items.is_empty():
		var item = self.get_selected()
		if (item):
			highlighted_items.append(item)
	return highlighted_items


func check_if_multiple_items_are_highlighted():
	if (select_mode == SELECT_MULTI):
		var first_item = self.get_next_selected(get_root())
		var item = self.get_next_selected(first_item)
		if (item):
			return true
	return false


func item_is_folder(item: TreeItem) -> bool:
	return item.get_icon(_name_col) == folder_icon

func items_has_folder(items: Array) -> bool:
	for item in items:
		if item_is_folder(item):
			return true
	return false


# Right click stuff


func add_custom_right_click_item(text: String, callable: Callable):
	var id = last_open_id
	last_open_id += 1
	custom_right_click_map[id] = text
	custom_right_click_items[text] = callable

func _on_gui_input(input:InputEvent):
	if input is InputEventKey:
		if input.is_command_or_control_pressed() and input.pressed:
			match(input.keycode):
				KEY_A:
					for item in root.get_children():
						for i in range(columns):
							item.select(i)
				KEY_C:
					var selected_items = get_highlighted_items()
					var rows = []
					
					for item in selected_items:
						var arr = []
						for col in range(columns):
							if item.is_selected(col):
								arr.append(item.get_text(col))
						rows.append(arr)
					var clipboard = ""
					for i in range(rows.size()):
						rows[i] = "\t".join(rows[i])
					clipboard = "\n".join(rows)
					DisplayServer.clipboard_set(clipboard)
	if input is InputEventMouseButton:
		var item = self.get_item_at_position(get_local_mouse_position())
		if (item):
			if input.double_click and input.button_index == MOUSE_BUTTON_LEFT:
				if item_is_folder(item):
					item.collapsed = not item.collapsed
			else:
				_on_custom_item_clicked(input.button_index)

func get_all_file_items(item = get_root()) -> Array:
	if not item:
		return []
	var items = []
	var it: TreeItem = item.get_first_child()
	while (it):
		if item_is_folder(it):
			items.append_array(get_all_file_items(it))
		else:
			items.append(it)
		it = it.get_next()
	return items

func get_all_folder_items(item = get_root()) -> Array:
	if not item:
		return []
	var items = []
	var it: TreeItem = item.get_first_child()
	while (it):
		if item_is_folder(it):
			items.append(it)
		items.append_array(get_all_folder_items(it))
		it = it.get_next()
	return items

func _on_custom_item_clicked(mouse_button: MouseButton):
	if (mouse_button == MOUSE_BUTTON_RIGHT):
		_on_item_right_clicked()

func _on_item_right_clicked():
	var plural = false
	var check_name = "Item"
	var has_folder = false

	clear_right_click_state()
	var selected_items = get_highlighted_items()
	right_clicked_item = self.get_item_at_position(get_local_mouse_position())
	if (not right_clicked_item_is_selected(selected_items)):
		set_right_clicked_outline_color(false)
		if right_clicked_item and item_is_folder(right_clicked_item):
			has_folder = true
	else:
		check_name = "Selected"
		if items_has_folder(selected_items):
			has_folder = true
		if (selected_items.size() > 1):
			plural = true
	pop_right_menu_items(check_name, plural, has_folder)
	right_click_menu.position = DisplayServer.mouse_get_position()
	right_click_menu.visible = true


func right_clicked_item_is_selected(selected_items: Array = []) -> bool:
	if not selected_items:
		selected_items = get_highlighted_items()
	var selected_items_has_right_clicked_item = true
	if right_clicked_item:
		selected_items_has_right_clicked_item = selected_items.has(right_clicked_item)
	return selected_items_has_right_clicked_item

func set_right_clicked_outline_color(clear_bg: bool = false):
	if not right_clicked_item:
		return
	for i in range(_name_col, columns):
		if not clear_bg:
			right_clicked_item.set_custom_bg_color(i, self.right_click_outline_color, true)
		else:
			right_clicked_item.set_custom_bg_color(i, Color(0,0,0,0), true)


func clear_right_click_state():
	set_right_clicked_outline_color(true)
	right_clicked_item = null

func _on_right_click_id(id):
	var selected_items = get_highlighted_items()
	if not right_clicked_item_is_selected(selected_items):
		selected_items = [right_clicked_item]
	clear_right_click_state()
	right_click_menu.hide()
	match id:
		POPUP_COPY_PATHS:
			var selected_files: PackedStringArray = []
			for item in selected_items:
				selected_files.append(_get_path(item))
			DisplayServer.clipboard_set("\n".join(selected_files))
		POPUP_UNCHECK_ALL:
			for item in selected_items:
				_propagate_check(item, false)
		POPUP_CHECK_ALL:
			for item in selected_items:
				_propagate_check(item, true)
		POPUP_FOLD_ALL:
			for item in selected_items:
				set_fold_all(item, true, true)
		POPUP_UNFOLD_ALL:
			for item in selected_items:
				set_fold_all(item, false, true)
		_:
			if custom_right_click_map.has(id):
				var text = custom_right_click_map.get(id)
				if custom_right_click_items.has(text):
					var callable = custom_right_click_items.get(text)
					callable.call(selected_items)

func _on_right_click_visibility_changed():
	if not right_click_menu.visible:
		set_right_clicked_outline_color(true)

func pop_right_menu_items(check_name: String = "Item", plural: bool = false, has_folder: bool = false):
	right_click_menu.clear(true)
	right_click_menu.reset_size()
	if self.show_copy_paths_in_right_click_menu:
		right_click_menu.add_item("Copy path" + ("s" if plural else ""), POPUP_COPY_PATHS)
	if self.check_mode:
		right_click_menu.add_item("Check " + check_name, POPUP_CHECK_ALL)
		right_click_menu.add_item("Uncheck " + check_name, POPUP_UNCHECK_ALL)
	if not self.flat_mode:
		# check if the right_click_menu has no items yet
		if right_click_menu.get_item_count() != 0:
			right_click_menu.add_separator("", POPUP_SEPERATOR)
		right_click_menu.add_item("Fold all", POPUP_FOLD_ALL)
		right_click_menu.add_item("Unfold all", POPUP_UNFOLD_ALL)
		if (has_folder):
			right_click_menu.set_item_disabled(POPUP_FOLD_ALL, false)
			right_click_menu.set_item_disabled(POPUP_UNFOLD_ALL, false)
		else:
			right_click_menu.set_item_disabled(POPUP_FOLD_ALL, true)
			right_click_menu.set_item_disabled(POPUP_UNFOLD_ALL, true)
	if custom_right_click_map.size() > 0:
		if right_click_menu.get_item_count() != 0:
			right_click_menu.add_separator("", POPUP_CUSTOM_SEPERATOR)
		for id in custom_right_click_map.keys():
			var text = custom_right_click_map.get(id)
			right_click_menu.add_item(text, id)

# ** CHECK PROPAGATION

func _propagate_check(item: TreeItem, checked: bool, check_visible_ignore_folders: bool = false):
	if (check_visible_ignore_folders and (not item.is_visible_in_tree())):
		return
	if (not check_visible_ignore_folders or not item_is_folder(item)):
		item.set_checked(_name_col, checked)
	var it: TreeItem = item.get_first_child()
	while (it):
		_propagate_check(it, checked, check_visible_ignore_folders)
		it = it.get_next()

func _on_item_edited():
	if check_mode:
		var item = self.get_edited()
		var checked = item.is_checked(_name_col)
		_propagate_check(item, checked)



# ** SORTING

func cmp_item_folders(a: TreeItem, b: TreeItem, descending_name_sort: bool) -> int:
	var a_is_folder = item_is_folder(a)
	var b_is_folder = item_is_folder(b)
	if (a_is_folder and !b_is_folder):
		return 1 if not descending_name_sort else -1
	if (!a_is_folder and b_is_folder):
		return -1 if not descending_name_sort else 1
	if (descending_name_sort):
		return a.get_text(_name_col).filenocasecmp_to(b.get_text(_name_col))
	return b.get_text(_name_col).filenocasecmp_to(a.get_text(_name_col))

func sort_tree(item:TreeItem, recursive: bool = true):
	var it: TreeItem = item.get_first_child()
	var arr: Array = []
	while (it):
		if recursive:
			sort_tree(it)
		arr.append(it)
		it = it.get_next()
	if (arr.size() <= 1):
		return
	match current_sort:
		SortType.SORT_NAME_ASCENDING:
			arr.sort_custom(func(a: TreeItem, b: TreeItem) -> bool:
				return cmp_item_folders(a, b, false) > 0
			)
		SortType.SORT_NAME_DESCENDING:
			arr.sort_custom(func(a: TreeItem, b: TreeItem) -> bool:
				return cmp_item_folders(a, b, true) > 0
			)
		SortType.SORT_SIZE_DESCENDING:
			arr.sort_custom(func(a: TreeItem, b: TreeItem) -> bool:
				var a_size = a.get_metadata(_size_col)
				var b_size = b.get_metadata(_size_col)
				if (a_size == b_size):
					return cmp_item_folders(a, b, true) > 0
				return a_size > b_size
			)
		SortType.SORT_SIZE_ASCENDING:
			arr.sort_custom(func(a: TreeItem, b: TreeItem) -> bool:
				var a_size = (a.get_metadata(_size_col))
				var b_size = (b.get_metadata(_size_col))
				if (a_size == b_size):
					return cmp_item_folders(a, b, false) > 0
				return a_size < b_size
			)
		SortType.SORT_INFO:
			arr.sort_custom(func(a: TreeItem, b: TreeItem) -> bool:
				return a.get_text(_info_col).filenocasecmp_to(b.get_text(_info_col)) > 0
			)
		SortType.SORT_REVERSE_INFO:
			arr.sort_custom(func(a: TreeItem, b: TreeItem) -> bool:
				return a.get_text(_info_col).filenocasecmp_to(b.get_text(_info_col)) < 0
			)
	var names: PackedStringArray = []
	for i in range(arr.size()):
		names.push_back(arr[i].get_text(_name_col))
	
	arr[0].move_before(arr[1])
	for i in range(1, arr.size()):
		arr[i].move_after(arr[i - 1])

func sort_entire_tree():
	match(current_sort):
		SortType.SORT_NAME_ASCENDING:
			self.set_column_title(_name_col, nameColumnName + " ▲")
		SortType.SORT_NAME_DESCENDING:
			self.set_column_title(_name_col, nameColumnName + " ▼")
		_:
			self.set_column_title(_name_col, nameColumnName)
	if (_size_col_exists):
		match(current_sort):
			SortType.SORT_SIZE_DESCENDING:
				self.set_column_title(_size_col, sizeColumnName + " ▼")
			SortType.SORT_SIZE_ASCENDING:
				self.set_column_title(_size_col, sizeColumnName + " ▲")
			_:
				self.set_column_title(_size_col, sizeColumnName)
	if (_info_col_exists):
		match(current_sort):
			SortType.SORT_INFO:
				self.set_column_title(_info_col, infoColumnName + " ▲")
			SortType.SORT_REVERSE_INFO:
				self.set_column_title(_info_col, infoColumnName + " ▼")
			_:
				self.set_column_title(_info_col, infoColumnName)
	sort_tree(root)
	if (userroot):
		sort_tree(userroot)


func _on_column_title_clicked(column: int, mouse_button_index: int):
	if (mouse_button_index == MOUSE_BUTTON_LEFT):
		if column == _name_col:
			current_sort = SortType.SORT_NAME_ASCENDING if current_sort != SortType.SORT_NAME_ASCENDING else SortType.SORT_NAME_DESCENDING
		elif column == _size_col:
			current_sort = SortType.SORT_SIZE_DESCENDING if current_sort != SortType.SORT_SIZE_DESCENDING else SortType.SORT_SIZE_ASCENDING
		elif column == _info_col:
			current_sort = SortType.SORT_INFO if current_sort != SortType.SORT_INFO else SortType.SORT_REVERSE_INFO
		else:
			return
		sort_entire_tree()

func _on_empty_clicked(_mouse_pos, mouse_button: MouseButton):
	if (mouse_button == MOUSE_BUTTON_LEFT):
		self.deselect_all()

func _get_path(item: TreeItem) -> String:
	var path = item.get_metadata(_name_col)
	if (not self.flat_mode and path.is_empty()):
		path = item.get_text(_name_col)
		item = item.get_parent()
		while (item):
			var item_name: String = item.get_text(_name_col)
			item = item.get_parent()
			if (item == null):
				path = item_name + path
				break
			path = item_name + "/" + path
	return path



# folding
func set_fold_all_children(item: TreeItem, collapsed: bool = true, recursive: bool = false):
	var it: TreeItem = item.get_first_child()
	while (it):
		if (item_is_folder(it)):
			it.collapsed = collapsed
			if (recursive):
				set_fold_all_children(it, collapsed, recursive)
		it = it.get_next()

func set_fold_all(item: TreeItem, collapsed: bool = true, recursive: bool = false):
	set_fold_all_children(item, collapsed, recursive)
	item.collapsed = collapsed



# creating and adding items

func create_file_item(p_parent_item: TreeItem, p_fullname: String, p_name: String, p_icon: Texture2D, p_size: int = -1, p_error: String = "", p_info: String = "", p_idx: int = -1) -> TreeItem:
	var item: TreeItem = self.create_item(p_parent_item, p_idx)
	if check_mode:
		item.set_cell_mode(_name_col, TreeItem.CELL_MODE_CHECK)
		item.set_checked(_name_col, true)
	item.set_editable(_name_col, true)
	item.set_icon(_name_col, p_icon)
	item.set_text(_name_col, p_name)
	item.set_metadata(_name_col, p_fullname)
	if _size_col_exists:
		if p_size > -1:
			if (p_size < (1024)):
				item.set_text(_size_col, String.num_int64(p_size) + " B")
			elif (p_size < (1024 * 1024)):
				item.set_text(_size_col, String.num(float(p_size) / 1024, 2) + " KiB")
			elif (p_size < (1024 * 1024 * 1024)):
				item.set_text(_size_col, String.num(float(p_size) / (1024 * 1024), 2) + " MiB")
			else:
				item.set_text(_size_col, String.num(float(p_size) / (1024 * 1024 * 1024), 2) + " GiB")
		else:
			p_size = 0
		item.set_metadata(_size_col, p_size)
		item.set_tooltip_text(_size_col, p_error);
	if _info_col_exists:
		item.set_text(_info_col, p_info);
	return item

func add_files_from_packed_infos(infos: Array, skipped_md5_check: bool = false):
	# reverse alphabetical order, we want to put directories at the front in alpha order

	# infos.sort_custom(func(a, b) -> bool:
	# 	return a.get_path().filenocasecmp_to(b.get_path()) <= 0
	# )
	for file in infos:
		_add_file_from_packed_info(file, skipped_md5_check)
	# collapse all the first level directories
	sort_entire_tree()
	set_fold_all_children(root, true, true)

func _add_file_from_packed_info(info: PackedFileInfo, skipped_md5_check: bool = false):
	num_files += 1
	var file_size = info.get_size()
	var path = info.get_path()
	var is_malformed = info.is_malformed()
	var is_verified = info.is_checksum_validated()
	var has_md5 = info.has_md5()
	var p_info = "Encrypted" if info.is_encrypted() else ""
	var icon = file_icon
	var errstr = ""
	if is_malformed:
		icon = file_broken
		errstr = "Malformed path"
		num_malformed += 1
	elif is_verified:
		icon = file_ok
	elif skipped_md5_check:
		icon = file_icon
	elif !is_verified && has_md5:
		icon = file_broken
		errstr = "Checksum mismatch"
		num_broken += 1
	var item

	add_file_tree_item(path, icon, file_size, errstr, p_info)
	if (num_files > LARGE_PCK):
		FILTER_DELAY = 0.5

func add_file_tree_item(path: String, icon: Texture2D, file_size: int = -1, errstr: String = "", p_info: String = ""):
	var root_name = root.get_text(_name_col) if root else ""
	if not flat_mode:
		if ("user://" in path):
			if userroot == null:
				userroot = create_root_item("user://", root)
			return add_file_to_item_node_mode(userroot, path, path.replace("user://", ""), icon, file_size, errstr, p_info)
		else:
			return add_file_to_item_node_mode(root, path, path.replace(root_name, ""), icon, file_size, errstr, p_info)
	return create_file_item(root, path, path, icon, file_size, errstr, p_info)


func create_root_item(root_name: String, root_item: TreeItem = null) -> TreeItem:
	var item: TreeItem = self.create_item(root_item)
	if check_mode:
		item.set_cell_mode(_name_col, TreeItem.CELL_MODE_CHECK)
		item.set_checked(_name_col, true)
	item.set_editable(_name_col, true)
	item.set_icon(_name_col, folder_icon)
	item.set_text(_name_col, root_name)
	item.set_metadata(_name_col, String())
	if _size_col_exists:
		item.set_metadata(_size_col, 0)
	return item

func get_first_index_of_non_folder_child(p_item: TreeItem):
	var it: TreeItem = p_item.get_first_child()
	while (it):
		if (not item_is_folder(it)):
			return it.get_index()
		it = it.get_next()
	return -1

func add_file_to_item_node_mode(p_item: TreeItem, p_fullname: String, p_name: String, p_icon: Texture2D, p_size: int,  p_error: String, p_info: String):
	var pp: int = p_name.find("/")
	if (pp == -1):
		return create_file_item(p_item, p_fullname, p_name, p_icon, p_size, p_error, p_info);
	else:
		var fld_name: String = p_name.substr(_name_col, pp);
		var path: String = p_name.substr(pp + 1, p_name.length());
		# Add folder if any
		var it: TreeItem = p_item.get_first_child();
		while (it) :
			if (it.get_text(_name_col) == fld_name) :
				return add_file_to_item_node_mode(it, p_fullname, path, p_icon, p_size, p_error, p_info);
			it = it.get_next()
		
		var folder_item:TreeItem = create_file_item(p_item, "", fld_name, folder_icon, -1, "", "", get_first_index_of_non_folder_child(p_item))
		return add_file_to_item_node_mode(folder_item, p_fullname, path, p_icon, p_size, p_error, p_info);

# filtering

func _filter_item(filter_str: String, item: TreeItem, is_glob: bool, clear_filter: bool):
	if (item_is_folder(item)): # directory
		var one_item_visible = false
		var it: TreeItem = item.get_first_child()
		while (it):
			var item_visible = _filter_item(filter_str, it, is_glob, clear_filter)
			if (not one_item_visible and item_visible):
				one_item_visible = true
			it = it.get_next()
		item.visible = one_item_visible
		if (not clear_filter and one_item_visible and item.collapsed):
			item.collapsed = false
		return one_item_visible
	var path: String = item.get_metadata(_name_col)
	if clear_filter:
		item.visible = true
		return true
	if (is_glob and path.matchn(filter_str)) or (!is_glob and path.contains(filter_str)):
		item.visible = true
		return true
	else:
		item.visible = false
		return false
	
func _filter(filter_str):
	var is_glob = filter_str.contains("*")
	var clear_filter = filter_str.is_empty() or filter_str == "*"
	_filter_item(filter_str, root, is_glob, clear_filter)
	if (userroot):
		_filter_item(filter_str, userroot, is_glob, clear_filter)

func _filter_timeout():
	timer = null
	_filter(prev_filter_string)
	prev_filter_string = ""

func filter(filter_str: String):
	if FILTER_DELAY > 0:
		if (timer != null):
			timer.disconnect("timeout", self._filter_timeout)
			timer = null
		timer = get_tree().create_timer(FILTER_DELAY)
		timer.connect("timeout", self._filter_timeout)
		prev_filter_string = filter_str
		return
	_filter(filter_str)
			
func check_all_shown(checked: bool):
	var it: TreeItem = root.get_first_child()
	while (it):
		_propagate_check(it, checked, true)
		it = it.get_next()
	if (userroot):
		it = userroot.get_first_child()
		while (it):
			_propagate_check(it, checked, true)
			it = it.get_next()


func get_checked_files() -> PackedStringArray:
	var arr: PackedStringArray = _get_checked_files(root)
	if (userroot != null):
		arr.append_array(_get_checked_files(userroot))
	return arr

func _get_checked_files(p_item: TreeItem):
	var arr: PackedStringArray = []
	var it: TreeItem = p_item
	var p_name: String = it.get_metadata(_name_col)
	if (it.is_checked(_name_col) and !p_name.is_empty()):
		arr.append(p_name)
	it = p_item.get_first_child();
	while (it):
		arr.append_array(_get_checked_files(it))
		it = it.get_next()
	return arr



func _clear():
	if (userroot != null):
		userroot.clear()
		userroot = null
	self.clear()
	num_files = 0
	num_broken = 0
	num_malformed = 0
	root = create_root_item(root_name if not self.flat_mode else "")

# Node overrides

func _ready():
	if self.flat_mode:
		self.hide_root = true
	_clear()
	right_click_menu = PopupMenu.new()
	pop_right_menu_items()
	right_click_menu.visible = false
	right_click_menu.connect("id_pressed", self._on_right_click_id)
	right_click_menu.connect("visibility_changed", self._on_right_click_visibility_changed)
	
	add_child(right_click_menu)
	set_column_map_cache(columnMap)
	if self.columns <= 0:
		print("No columns set in the Tree")
		return
	self.connect("gui_input", self._on_gui_input)
	self.connect("empty_clicked", self._on_empty_clicked)
	self.connect("column_title_clicked", self._on_column_title_clicked)
	self.set_column_title(_name_col, nameColumnName)
	self.set_column_expand(_name_col, true)
	self.add_theme_constant_override("draw_relationship_lines", 1)
	if (_size_col_exists):
		self.set_column_title(_size_col, sizeColumnName)
		self.set_column_expand(_size_col, false)
		self.set_column_custom_minimum_width(_size_col, 120)
	elif (_info_col_exists):
		self.set_column_title(_info_col, infoColumnName)
		self.set_column_custom_minimum_width(_info_col, 120)
		self.set_column_expand(_info_col, false)
	self.connect("item_edited", self._on_item_edited)

func _init():
	# var arrow = get_theme_icon("arrow", "Tree")
	if check_mode:
		GodotREEditorStandalone.tree_set_edit_checkbox_cell_only_when_checkbox_is_pressed(self, editable_only_when_checkbox_clicked)
	pass


func _process(_delta:float):
	pass
