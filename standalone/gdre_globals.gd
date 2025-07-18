class_name GDREGlobals
extends Node

const banned_files: PackedStringArray = [".DS_Store", "thumbs.db"]

static func convert_pcfg_to_text(path: String, output_dir: String) -> Array:
	var loader = ProjectConfigLoader.new()
	var ver_major = GDRESettings.get_ver_major()
	var ver_minor = GDRESettings.get_ver_minor()
	var text_file = "project.godot"
	var err = OK
	if path.get_file() == "engine.cfb":
		text_file = "engine.cfg"
	if (ver_major > 0):
		err = loader.load_cfb(path, ver_major, ver_minor)
		if err != OK:
			return [err, text_file]
	else:
		if (path.get_file() == "engine.cfb"):
			ver_major = 2
			err = loader.load_cfb(path, ver_major, ver_minor)
			if err != OK:
				return [err, text_file]
		else:
			err = loader.load_cfb(path, 4, 3)
			if (err == OK):
				ver_major = 4
			else:
				err = loader.load_cfb(path, 3, 3)
				if err != OK:
					return [err, text_file]
				ver_major = 3
	return [loader.save_cfb(output_dir, ver_major, ver_minor), text_file]

static func get_recent_error_string() -> String:
	return parse_log_errors(GDRESettings.get_errors())


static func get_files_for_paths(paths: PackedStringArray) -> PackedStringArray:
	var files: PackedStringArray = []
	for path in paths:
		if path.is_empty():
			continue
		files.append(path.get_file())
	return files

static func parse_log_errors(errors: PackedStringArray) -> String:
	var ret = ""
	for error in errors:
		var stripped = error.strip_edges()
		if stripped.is_empty() or stripped.begins_with("at:") or stripped.begins_with("GDScript backtrace"):
			continue
		ret += stripped + "\n"
	return ret

static func parse_and_print_log_errors(errors: PackedStringArray) -> String:
	for error in errors:
		print(error)
	return parse_log_errors(errors)


const MINIMUM_GLB_VERSION: int = 4
