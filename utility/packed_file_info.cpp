#include "packed_file_info.h"

void PackedFileInfo::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_pack"), &PackedFileInfo::get_pack);
	ClassDB::bind_method(D_METHOD("get_path"), &PackedFileInfo::get_path);
	ClassDB::bind_method(D_METHOD("get_raw_path"), &PackedFileInfo::get_raw_path);
	ClassDB::bind_method(D_METHOD("get_offset"), &PackedFileInfo::get_offset);
	ClassDB::bind_method(D_METHOD("get_size"), &PackedFileInfo::get_size);
	ClassDB::bind_method(D_METHOD("get_md5"), &PackedFileInfo::get_md5);
	ClassDB::bind_method(D_METHOD("has_md5"), &PackedFileInfo::has_md5);
	ClassDB::bind_method(D_METHOD("is_malformed"), &PackedFileInfo::is_malformed);
	ClassDB::bind_method(D_METHOD("is_encrypted"), &PackedFileInfo::is_encrypted);
	ClassDB::bind_method(D_METHOD("is_checksum_validated"), &PackedFileInfo::is_checksum_validated);
}

#define PATH_REPLACER "_"

void PackedFileInfo::fix_path() {
	path = raw_path;
	malformed_path = false;
	String prefix = "";

	//remove prefix first
	if (path.begins_with("res://")) {
		path = path.replace_first("res://", "");
		prefix = "res://";
	} else if (path.begins_with("local://")) {
		path = path.replace_first("local://", "");
		prefix = "local://";
	} else if (path.begins_with("user://")) {
		path = path.replace_first("user://", "");
		prefix = "user://";
	}

	while (path.begins_with("~")) {
		path = path.substr(1, path.length() - 1);
		malformed_path = true;
	}

	while (path.begins_with("/") || path.begins_with("./")) {
		while (path.begins_with("/")) {
			path = path.substr(1, path.length() - 1);
			malformed_path = true;
		}
		while (path.begins_with("./")) {
			path = path.substr(2, path.length() - 1);
			malformed_path = true;
		}
	}

	if (path.find("//") >= 0) {
		path = path.replace("//", "/");
		malformed_path = true;
	}
	if (path.find("/./") >= 0) {
		path = path.replace("/./", "/");
		malformed_path = true;
	}
	if (path.find("\\") >= 0) {
		path = path.replace("\\", PATH_REPLACER);
		malformed_path = true;
	}
	if (path.find(":") >= 0) {
		path = path.replace(":", PATH_REPLACER);
		malformed_path = true;
	}
	if (path.find("|") >= 0) {
		path = path.replace("|", PATH_REPLACER);
		malformed_path = true;
	}
	if (path.find("?") >= 0) {
		path = path.replace("?", PATH_REPLACER);
		malformed_path = true;
	}
	if (path.find(">") >= 0) {
		path = path.replace(">", PATH_REPLACER);
		malformed_path = true;
	}
	if (path.find("<") >= 0) {
		path = path.replace("<", PATH_REPLACER);
		malformed_path = true;
	}
	if (path.find("*") >= 0) {
		path = path.replace("*", PATH_REPLACER);
		malformed_path = true;
	}
	if (path.find("\"") >= 0) {
		path = path.replace("\"", PATH_REPLACER);
		malformed_path = true;
	}

	// add the prefix back
	if (prefix != "") {
		path = prefix + path;
	}
}