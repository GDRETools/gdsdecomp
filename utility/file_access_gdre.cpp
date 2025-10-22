//
// Created by Nikita on 3/26/2023.
//

#include "file_access_gdre.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "file_access_apk.h"
#include "gdre_packed_source.h"
#include "gdre_settings.h"
#include "packed_file_info.h"
#include "utility/common.h"

bool DirSource::try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset) {
	if (!DirAccess::exists(p_path)) {
		return false;
	}
	Ref<DirAccess> da = DirAccess::open(p_path);
	if (da.is_null()) {
		return false;
	}
	PackedStringArray pa = gdre::get_recursive_dir_list(p_path, {}, false);
	if (is_print_verbose_enabled()) {
		for (auto s : pa) {
			print_verbose(s);
		}
	}
	Ref<GDRESettings::PackInfo> pckinfo;
	pckinfo.instantiate();
	pckinfo->init(p_path, Ref<GodotVer>(memnew(GodotVer)), 1, 0, 0, pa.size(), GDRESettings::PackInfo::DIR);
	GDRESettings::get_singleton()->add_pack_info(pckinfo);
	for (auto &path : pa) {
		size_t size = 0;
		Ref<FileAccess> fa = FileAccess::open(p_path.path_join(path), FileAccess::READ);
		if (fa.is_valid()) {
			size = fa->get_length();
		}
		GDREPackedData::get_singleton()->add_path(p_path, path, 1, size, MD5_EMPTY, this, p_replace_files, false, false);
	}
	packs.push_back(p_path);
	return true;
}

Ref<FileAccess> DirSource::get_file(const String &p_path, PackedData::PackedFile *p_file) {
	if (!p_file) {
		return nullptr;
	}
	if (p_path.begins_with("res://")) {
		String path = p_file->pack.path_join(p_path.trim_prefix("res://"));
		return FileAccess::open(path, FileAccess::READ);
	}
	return nullptr;
}

bool DirSource::file_exists(const String &p_path) const {
	for (auto &pack : packs) {
		if (FileAccess::exists(pack.path_join(p_path.trim_prefix("res://")))) {
			return true;
		}
	}
	return false;
}

String DirSource::get_pack_path(const String &p_path) const {
	for (auto &pack : packs) {
		if (FileAccess::exists(pack.path_join(p_path.trim_prefix("res://")))) {
			return pack;
		}
	}
	return "";
}

void DirSource::reset() {
	packs.clear();
}

bool DirSource::loaded_pack() const {
	return !packs.is_empty();
}

DirSource *DirSource::singleton = nullptr;

DirSource::DirSource() {
	singleton = this;
}

DirSource::~DirSource() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

DirSource *DirSource::get_singleton() {
	return singleton;
}

Error GDREPackedData::add_pack(const String &p_path, bool p_replace_files, uint64_t p_offset) {
	if (sources.is_empty()) {
		sources.push_back(memnew(GDREPackedSource));
		sources.push_back(memnew(APKArchive));
	}
	for (int i = 0; i < sources.size(); i++) {
		if (sources[i]->try_open_pack(p_path, p_replace_files, p_offset)) {
			// need to set the default file access to use our own
			set_disabled(false);
			// set_default_file_access();
			return OK;
		}
	}
	return ERR_FILE_UNRECOGNIZED;
}

Error GDREPackedData::add_dir(const String &p_path, bool p_replace_files) {
	if (dir_source.try_open_pack(p_path, p_replace_files, 0)) {
		set_disabled(false);
		return OK;
	}
	return ERR_FILE_CANT_OPEN;
}

namespace {
// Solely here to test that PackedData::PackedFile hasn't changed
struct __ExpectedPackedFile {
	String pack;
	uint64_t offset; //if offset is ZERO, the file was ERASED
	uint64_t size;
	uint8_t md5[16];
	PackSource *src = nullptr;
	bool encrypted;
	bool bundle;
};
CHECK_SIZE_MATCH_NO_PADDING(__ExpectedPackedFile, PackedData::PackedFile);
} //namespace

static_assert(has_same_signature<decltype(&GDREPackedData::add_path), decltype(&PackedData::add_path)>::value, "GDREPackedData::add_path does not have the same signature as PackedData::add_path");

void GDREPackedData::add_path(const String &p_pkg_path, const String &p_path, uint64_t p_ofs, uint64_t p_size, const uint8_t *p_md5, PackSource *p_src, bool p_replace_files, bool p_encrypted, bool p_bundle) {
	// TODO: This might be a performance hit? If so, use the commented out one.
	bool p_pck_src = dynamic_cast<GDREPackedSource *>(p_src) != nullptr;
	// bool p_pck_src = p_src == sources[0];
	PackedData::PackedFile pf;
	pf.bundle = p_bundle;
	pf.encrypted = p_encrypted;
	pf.pack = p_pkg_path;
	pf.offset = p_ofs;
	pf.size = p_size;
	for (int i = 0; i < 16; i++) {
		pf.md5[i] = p_md5[i];
	}
	pf.src = p_src;
	Ref<PackedFileInfo> pf_info;
	pf_info.instantiate();
	String abs_path = p_path.is_relative_path() ? "res://" + p_path : p_path;
	pf_info->init(abs_path, &pf);

	// Get the fixed path if this is from a PCK source
	String path = p_pck_src ? pf_info->get_path() : abs_path.simplify_path();

	PathMD5 pmd5(path.trim_prefix("res://").md5_buffer());

	bool exists = files.has(pmd5);

	if (!exists || p_replace_files) {
		files[pmd5] = pf;
		file_map[path] = pf_info;
	}

	if (!exists) {
		//search for dir
		String p = path.trim_prefix("res://");
		PackedDir *cd = root;

		if (p.contains("/")) { //in a subdir

			Vector<String> ds = p.get_base_dir().split("/");

			for (int j = 0; j < ds.size(); j++) {
				if (!cd->subdirs.has(ds[j])) {
					PackedDir *pd = memnew(PackedDir);
					pd->name = ds[j];
					pd->parent = cd;
					cd->subdirs[pd->name] = pd;
					cd = pd;
				} else {
					cd = cd->subdirs[ds[j]];
				}
			}
		}
		String filename = path.get_file();
		// Don't add as a file if the path points to a directory
		if (!filename.is_empty()) {
			cd->files.insert(filename);
		}
	}
}

void GDREPackedData::add_pack_source(PackSource *p_source) {
	if (p_source != nullptr) {
		sources.push_back(p_source);
	}
}

uint8_t *GDREPackedData::get_file_hash(const String &p_path) {
	String simplified_path = p_path.simplify_path().trim_prefix("res://");
	PathMD5 pmd5(simplified_path.md5_buffer());
	HashMap<PathMD5, PackedData::PackedFile, PathMD5>::Iterator E = files.find(pmd5);
	if (!E) {
		return nullptr;
	}

	return E->value.md5;
}

HashSet<String> GDREPackedData::get_file_paths() const {
	HashSet<String> file_paths;
	_get_file_paths(root, root->name, file_paths);
	return file_paths;
}

void GDREPackedData::_get_file_paths(PackedDir *p_dir, const String &p_parent_dir, HashSet<String> &r_paths) const {
	for (const String &E : p_dir->files) {
		r_paths.insert(p_parent_dir.path_join(E));
	}

	for (const KeyValue<String, PackedDir *> &E : p_dir->subdirs) {
		_get_file_paths(E.value, p_parent_dir.path_join(E.key), r_paths);
	}
}

GDREPackedData *GDREPackedData::singleton = nullptr;

GDREPackedData::GDREPackedData() {
	singleton = this;
	root = memnew(PackedDir);
}

Vector<Ref<PackedFileInfo>> GDREPackedData::get_file_info_list(const Vector<String> &filters) {
	Vector<Ref<PackedFileInfo>> ret;
	bool no_filters = !filters.size();
	for (auto E : file_map) {
		if (no_filters) {
			ret.push_back(E.value);
			continue;
		}
		for (int j = 0; j < filters.size(); j++) {
			if (E.key.get_file().match(filters[j])) {
				ret.push_back(E.value);
				break;
			}
		}
	}
	return ret;
}

void GDREPackedData::remove_path(const String &p_path) {
	String simplified_path = p_path.simplify_path().trim_prefix("res://");

	PathMD5 pmd5(simplified_path.md5_buffer());
	if (!files.has(pmd5)) {
		return;
	}

	// Search for directory.
	PackedDir *cd = root;

	if (simplified_path.contains_char('/')) { // In a subdirectory.
		Vector<String> ds = simplified_path.get_base_dir().split("/");

		for (int j = 0; j < ds.size(); j++) {
			if (!cd->subdirs.has(ds[j])) {
				return; // Subdirectory does not exist, do not bother creating.
			} else {
				cd = cd->subdirs[ds[j]];
			}
		}
	}

	if (file_map.has(p_path.simplify_path())) {
		file_map.erase(p_path.simplify_path());
	}

	cd->files.erase(simplified_path.get_file());

	files.erase(pmd5);
}

void GDREPackedData::set_disabled(bool p_disabled) {
	if (p_disabled) {
		// we need to re-enable the default create funcs
		reset_default_file_access();
		if (packed_data_was_enabled) {
			PackedData::get_singleton()->set_disabled(false);
		}
	} else {
		// we need to disable the default create funcs and put our own in
		set_default_file_access();
		if (!PackedData::get_singleton()->is_disabled()) {
			packed_data_was_enabled = true;
			PackedData::get_singleton()->set_disabled(true);
		}
	}
	disabled = p_disabled;
}

bool GDREPackedData::is_disabled() const {
	return disabled;
}

GDREPackedData *GDREPackedData::get_singleton() {
	return singleton;
}

int64_t GDREPackedData::get_file_size(const String &p_path) {
	String simplified_path = p_path.simplify_path().trim_prefix("res://");
	PathMD5 pmd5(simplified_path.md5_buffer());
	HashMap<PathMD5, PackedData::PackedFile, PathMD5>::Iterator E = files.find(pmd5);
	if (!E) {
		return -1; //not found
	}
	if (E->value.offset == 0) {
		return -1; //was erased
	}
	return E->value.size;
}

Ref<FileAccess> GDREPackedData::try_open_path(const String &p_path) {
	String simplified_path = p_path.simplify_path().trim_prefix("res://");
	PathMD5 pmd5(simplified_path.md5_buffer());
	HashMap<PathMD5, PackedData::PackedFile, PathMD5>::Iterator E = files.find(pmd5);
	if (!E) {
		return nullptr; //not found
	}

	if (E->value.offset == 0 && !E->value.bundle) {
		return nullptr; //was erased
	}

	return E->value.src->get_file(p_path, &E->value);
}

bool GDREPackedData::has_path(const String &p_path) {
	return files.has(PathMD5(p_path.simplify_path().trim_prefix("res://").md5_buffer()));
}

Ref<DirAccess> GDREPackedData::try_open_directory(const String &p_path) {
	Ref<DirAccess> da = memnew(DirAccessGDRE());
	if (da->change_dir(p_path) != OK) {
		da = Ref<DirAccess>();
	}
	return da;
}

bool GDREPackedData::has_directory(const String &p_path) {
	Ref<DirAccess> da = try_open_directory(p_path);
	if (da.is_valid()) {
		return true;
	}
	return false;
}
void GDREPackedData::_free_packed_dirs(GDREPackedData::PackedDir *p_dir) {
	for (const KeyValue<String, PackedDir *> &E : p_dir->subdirs) {
		_free_packed_dirs(E.value);
	}
	memdelete(p_dir);
}

bool GDREPackedData::has_loaded_packs() {
	return (!sources.is_empty() || dir_source.loaded_pack()) && !files.is_empty();
}

// Test for the existence of project.godot or project.binary in the packed data
// This works even when PackedData is disabled
bool GDREPackedData::real_packed_data_has_pack_loaded() {
	return PackedData::get_singleton() && (PackedData::get_singleton()->has_path("res://project.binary") || PackedData::get_singleton()->has_path("res://project.godot"));
}

void GDREPackedData::clear() {
	_clear();
}

void GDREPackedData::_clear() {
	// TODO: refactor the sources to have the option to clear data instead of just deleting them
	for (int i = 0; i < sources.size(); i++) {
		memdelete(sources[i]);
	}
	sources.clear();
	dir_source.reset();
	set_disabled(true);
	_free_packed_dirs(root);
	root = memnew(PackedDir);
	file_map.clear();
	files.clear();
}

GDREPackedData::~GDREPackedData() {
	_clear();
	if (root) {
		_free_packed_dirs(root);
	}
}

constexpr bool should_check_pack(int p_mode_flags) {
	return !(p_mode_flags & FileAccess::WRITE) && !(p_mode_flags & FileAccess::SKIP_PACK);
}

bool is_gdre_file(const String &p_path) {
	return p_path.begins_with("res://") && p_path.get_file().begins_with("gdre_");
}

Error FileAccessGDRE::open_internal(const String &p_path, int p_mode_flags) {
	mode_flags = p_mode_flags;
	//try packed data first
	if (should_check_pack(p_mode_flags) && GDREPackedData::get_singleton() && !GDREPackedData::get_singleton()->is_disabled()) {
		proxy = GDREPackedData::get_singleton()->try_open_path(p_path);
		if (proxy.is_valid()) {
			if (is_gdre_file(p_path)) {
				WARN_PRINT(vformat("Opening gdre file %s from a loaded external pack???? PLEASE REPORT THIS!!!!", p_path));
			};
			return OK;
		}
	}
	String path = p_path;
	if (should_check_pack(p_mode_flags) && is_gdre_file(p_path)) {
		WARN_PRINT(vformat("Attempted to open a gdre file %s while we have a pack loaded...", p_path));
		if (PathFinder::real_packed_data_has_path(p_path)) {
			// this works even when PackedData is disabled
			proxy = PackedData::get_singleton()->try_open_path(p_path);
			if (proxy.is_valid()) {
				return OK;
			}
		}
	}
	// Otherwise, it's on the file system.
	path = PathFinder::_fix_path_file_access(p_path, p_mode_flags);
	Error err;
	proxy = _open_filesystem(path, p_mode_flags, &err);
	if (err != OK) {
		proxy = Ref<FileAccess>();
	}
	return err;
}

bool FileAccessGDRE::is_open() const {
	if (proxy.is_null()) {
		return false;
	}
	return proxy->is_open();
}

void FileAccessGDRE::seek(uint64_t p_position) {
	ERR_FAIL_COND_MSG(proxy.is_null(), "File must be opened before use.");
	proxy->seek(p_position);
}

void FileAccessGDRE::seek_end(int64_t p_position) {
	ERR_FAIL_COND_MSG(proxy.is_null(), "File must be opened before use.");
	proxy->seek_end(p_position);
}

uint64_t FileAccessGDRE::get_position() const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), 0, "File must be opened before use.");
	return proxy->get_position();
}

uint64_t FileAccessGDRE::get_length() const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), 0, "File must be opened before use.");
	return proxy->get_length();
}

bool FileAccessGDRE::eof_reached() const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), true, "File must be opened before use.");
	return proxy->eof_reached();
}

uint8_t FileAccessGDRE::get_8() const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), 0, "File must be opened before use.");
	return proxy->get_8();
}

uint64_t FileAccessGDRE::get_buffer(uint8_t *p_dst, uint64_t p_length) const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), -1, "File must be opened before use.");
	return proxy->get_buffer(p_dst, p_length);
}

Error FileAccessGDRE::get_error() const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), ERR_FILE_NOT_FOUND, "File must be opened before use.");
	return proxy->get_error();
}

Error FileAccessGDRE::resize(int64_t p_length) {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), ERR_FILE_NOT_FOUND, "File must be opened before use.");
	return proxy->resize(p_length);
}

void FileAccessGDRE::flush() {
	ERR_FAIL_COND_MSG(proxy.is_null(), "File must be opened before use.");
	return proxy->flush();
}

bool FileAccessGDRE::store_8(uint8_t p_dest) {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), false, "File must be opened before use.");
	return proxy->store_8(p_dest);
}

bool FileAccessGDRE::store_buffer(const uint8_t *p_src, uint64_t p_length) {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), false, "File must be opened before use.");
	return proxy->store_buffer(p_src, p_length);
}

bool FileAccessGDRE::file_exists(const String &p_name) {
	if (PathFinder::gdre_packed_data_valid_path(p_name)) {
		return true;
	}
	if (proxy.is_valid()) {
		return proxy->file_exists(p_name);
	}
	// TODO: I don't think this is necessary and may screw things up; revisit this
	// After proxy, check if it's in the real packed data
	// if (PathFinder::real_packed_data_has_path(p_name)) {
	// 	return true;
	// }

	return false;
}

void FileAccessGDRE::close() {
	if (proxy.is_null()) {
		return;
	}
	proxy->close();
}

uint64_t FileAccessGDRE::_get_modified_time(const String &p_file) {
	if (proxy.is_valid()) {
		return proxy->_get_unix_permissions(p_file);
	}
	return 0;
}

BitField<FileAccess::UnixPermissionFlags> FileAccessGDRE::_get_unix_permissions(const String &p_file) {
	if (proxy.is_valid()) {
		return proxy->_get_unix_permissions(p_file);
	}
	return 0;
}

Error FileAccessGDRE::_set_unix_permissions(const String &p_file, BitField<FileAccess::UnixPermissionFlags> p_permissions) {
	if (proxy.is_valid()) {
		return proxy->_set_unix_permissions(p_file, p_permissions);
	}
	return FAILED;
}

bool FileAccessGDRE::_get_hidden_attribute(const String &p_file) {
	if (proxy.is_valid()) {
		return proxy->_get_hidden_attribute(p_file);
	}
	return false;
}

Error FileAccessGDRE::_set_hidden_attribute(const String &p_file, bool p_hidden) {
	if (proxy.is_valid()) {
		return proxy->_set_hidden_attribute(p_file, p_hidden);
	}
	return FAILED;
}

bool FileAccessGDRE::_get_read_only_attribute(const String &p_file) {
	if (proxy.is_valid()) {
		return proxy->_get_read_only_attribute(p_file);
	}
	return false;
}

Error FileAccessGDRE::_set_read_only_attribute(const String &p_file, bool p_ro) {
	if (proxy.is_valid()) {
		return proxy->_set_read_only_attribute(p_file, p_ro);
	}
	return FAILED;
}

uint64_t FileAccessGDRE::_get_access_time(const String &p_file) {
	if (proxy.is_valid()) {
		return ((FileAccessGDRE *)proxy.ptr())->_get_access_time(p_file);
	}
	return 0;
}

int64_t FileAccessGDRE::_get_size(const String &p_file) {
	if (PathFinder::gdre_packed_data_valid_path(p_file)) {
		return GDREPackedData::get_singleton()->get_file_size(p_file);
	}

	if (proxy.is_valid()) {
		return ((FileAccessGDRE *)proxy.ptr())->_get_size(p_file);
	}
	return 0;
}

// DirAccessGDRE
// This is a copy/paste implementation of DirAccessPack, with the exception of the proxying
// DirAccessPack accessed the PackedData singleton, which we don't want to do.
// Instead, we use the GDREPackedData singleton
// None of the built-in resource loaders use this to load resources, so we do not have to check for "res://gdre_"
Error DirAccessGDRE::list_dir_begin() {
	if (proxy.is_valid()) {
		return proxy->list_dir_begin();
	}
	if (!current) {
		return ERR_UNCONFIGURED;
	}
	list_dirs.clear();
	list_files.clear();

	for (const KeyValue<String, GDREPackedData::PackedDir *> &E : current->subdirs) {
		list_dirs.push_back(E.key);
	}

	for (const String &E : current->files) {
		list_files.push_back(E);
	}

	return OK;
}

String DirAccessGDRE::get_next() {
	if (proxy.is_valid()) {
		return proxy->get_next();
	}
	if (list_dirs.size()) {
		cdir = true;
		String d = list_dirs.front()->get();
		list_dirs.pop_front();
		return d;
	} else if (list_files.size()) {
		cdir = false;
		String f = list_files.front()->get();
		list_files.pop_front();
		return f;
	} else {
		return String();
	}
}

bool DirAccessGDRE::current_is_dir() const {
	if (proxy.is_valid()) {
		return proxy->current_is_dir();
	}
	return cdir;
}

bool DirAccessGDRE::current_is_hidden() const {
	if (proxy.is_valid()) {
		return proxy->current_is_hidden();
	}
	return false;
}

void DirAccessGDRE::list_dir_end() {
	if (proxy.is_valid()) {
		return proxy->list_dir_end();
	}

	list_dirs.clear();
	list_files.clear();
}

int DirAccessGDRE::get_drive_count() {
	if (proxy.is_valid()) {
		return proxy->get_drive_count();
	}

	return 0;
}

String DirAccessGDRE::get_drive(int p_drive) {
	if (proxy.is_valid()) {
		return proxy->get_drive(p_drive);
	}

	return "";
}

int DirAccessGDRE::get_current_drive() {
	if (proxy.is_valid()) {
		return proxy->get_current_drive();
	}
	return 0;
}

bool DirAccessGDRE::drives_are_shortcuts() {
	if (proxy.is_valid()) {
		return proxy->drives_are_shortcuts();
	}
	return false;
}

// internal method
GDREPackedData::PackedDir *DirAccessGDRE::_find_dir(String p_dir) {
	if (!current) {
		return nullptr;
	}
	String nd = p_dir.replace("\\", "/");

	// Special handling since simplify_path() will forbid it
	if (p_dir == "..") {
		return current->parent;
	}

	bool absolute = false;
	if (nd.begins_with("res://")) {
		nd = nd.trim_prefix("res://");
		absolute = true;
	}

	nd = nd.simplify_path();

	if (nd.is_empty()) {
		nd = ".";
	}

	if (nd.begins_with("/")) {
		nd = nd.replace_first("/", "");
		absolute = true;
	}

	Vector<String> paths = nd.split("/");

	GDREPackedData::PackedDir *pd;

	if (absolute) {
		pd = GDREPackedData::get_singleton()->root;
	} else {
		pd = current;
	}

	for (int i = 0; i < paths.size(); i++) {
		String p = paths[i];
		if (p == ".") {
			continue;
		} else if (p == "..") {
			if (pd->parent) {
				pd = pd->parent;
			}
		} else if (pd->subdirs.has(p)) {
			pd = pd->subdirs[p];

		} else {
			return nullptr;
		}
	}

	return pd;
}

Error DirAccessGDRE::change_dir(String p_dir) {
	if (proxy.is_valid()) {
		return proxy->change_dir(p_dir);
	}
	if (!current) {
		return ERR_UNCONFIGURED;
	}
	GDREPackedData::PackedDir *pd = _find_dir(p_dir);
	if (pd) {
		current = pd;
		return OK;
	} else {
		return ERR_INVALID_PARAMETER;
	}
}

String DirAccessGDRE::get_current_dir(bool p_include_drive) const {
	if (proxy.is_valid()) {
		return proxy->get_current_dir(p_include_drive);
	}
	if (!current) {
		return "";
	}
	GDREPackedData::PackedDir *pd = current;
	String p = current->name;

	while (pd->parent) {
		pd = pd->parent;
		p = pd->name.path_join(p);
	}

	return "res://" + p;
}

bool DirAccessGDRE::file_exists(String p_file) {
	if (proxy.is_valid()) {
		return proxy->file_exists(p_file);
	}

	GDREPackedData::PackedDir *pd = _find_dir(p_file.get_base_dir());
	if (pd) {
		return pd->files.has(p_file.get_file());
	}
	return false;
}

bool DirAccessGDRE::dir_exists(String p_dir) {
	if (proxy.is_valid()) {
		return proxy->dir_exists(p_dir);
	}
	return _find_dir(p_dir) != nullptr;
}

bool DirAccessGDRE::is_readable(String p_dir) {
	if (proxy.is_valid()) {
		return proxy->is_readable(p_dir);
	}
	return true;
}

bool DirAccessGDRE::is_writable(String p_dir) {
	if (proxy.is_valid()) {
		return proxy->is_writable(p_dir);
	}
	return true;
}

uint64_t DirAccessGDRE::get_modified_time(String p_file) {
	if (proxy.is_valid()) {
		return proxy->make_dir(p_file);
	}
	return 0;
}

Error DirAccessGDRE::make_dir(String p_dir) {
	if (proxy.is_valid()) {
		return proxy->make_dir(p_dir);
	}
	return ERR_UNAVAILABLE;
}

Error DirAccessGDRE::rename(String p_from, String p_to) {
	if (proxy.is_valid()) {
		return proxy->rename(p_from, p_to);
	}
	return ERR_UNAVAILABLE;
}

Error DirAccessGDRE::remove(String p_name) {
	if (proxy.is_valid()) {
		return proxy->remove(p_name);
	}
	return ERR_UNAVAILABLE;
}

uint64_t DirAccessGDRE::get_space_left() {
	if (proxy.is_valid()) {
		return proxy->get_space_left();
	}
	return 0;
}

String DirAccessGDRE::get_filesystem_type() const {
	if (proxy.is_valid()) {
		return proxy->get_filesystem_type();
	}
	return "PCK";
}
/**
 * 	virtual bool is_link(String p_file) override;
	virtual String read_link(String p_file) override;
	virtual Error create_link(String p_source, String p_target) override;
*/
bool DirAccessGDRE::is_link(String p_file) {
	if (proxy.is_valid()) {
		return proxy->is_link(p_file);
	}
	return false;
}

String DirAccessGDRE::read_link(String p_file) {
	if (proxy.is_valid()) {
		return proxy->read_link(p_file);
	}
	return "";
}

Error DirAccessGDRE::create_link(String p_source, String p_target) {
	if (proxy.is_valid()) {
		String new_source;
		return proxy->create_link(p_source, p_target);
	}
	return ERR_UNAVAILABLE;
}

DirAccessGDRE::DirAccessGDRE() {
	if (GDREPackedData::get_singleton()->is_disabled() || !GDREPackedData::get_singleton()->has_loaded_packs()) {
		proxy = _open_filesystem();
		current = nullptr;
	} else {
		current = GDREPackedData::get_singleton()->root;
		proxy = Ref<DirAccess>();
	}
}

DirAccessGDRE::~DirAccessGDRE() {
}

// At the end so that the header files that they pull in don't screw up the above.
#ifdef WINDOWS_ENABLED
#include "drivers/windows/dir_access_windows.h"
#include "drivers/windows/file_access_windows.h"
#else // UNIX_ENABLED -- covers OSX, Linux, FreeBSD, Web.
#include "drivers/unix/dir_access_unix.h"
#include "drivers/unix/file_access_unix.h"
#ifdef MACOS_ENABLED
#include "platform/macos/dir_access_macos.h"
#endif
#ifdef ANDROID_ENABLED
#include "platform/android/dir_access_jandroid.h"
#include "platform/android/file_access_android.h"
#endif
#endif
// static FileAccess::CreateFunc default_file_res_create_func{ nullptr };
// enum DirAccessType {
// 	UNKNOWN = -1,
// 	UNIX,
// 	PACK,
// 	WINDOWS,
// 	MACOS,
// 	ANDROID
// };
// static DirAccessType default_dir_res_create_type = UNKNOWN;

#if defined(WINDOWS_ENABLED)
#define FILE_ACCESS_OS FileAccessWindows
#define DIR_ACCESS_OS DirAccessWindows
#define FILE_ACCESS_OS_STR "FileAccessWindows"
#define DIR_ACCESS_OS_STR "DirAccessWindows"
#elif defined(ANDROID_ENABLED)
#define FILE_ACCESS_OS FileAccessAndroid
#define DIR_ACCESS_OS DirAccessJAndroid
#define FILE_ACCESS_OS_STR "FileAccessAndroid"
#define DIR_ACCESS_OS_STR "DirAccessJAndroid"

#elif defined(MACOS_ENABLED)
#define FILE_ACCESS_OS FileAccessUnix
#define DIR_ACCESS_OS DirAccessMacOS
#define FILE_ACCESS_OS_STR "FileAccessUnix"
#define DIR_ACCESS_OS_STR "DirAccessMacOS"
#elif defined(UNIX_ENABLED) // -- covers Linux, FreeBSD, Web.
#define FILE_ACCESS_OS FileAccessUnix
#define DIR_ACCESS_OS DirAccessUnix
#define FILE_ACCESS_OS_STR "FileAccessUnix"
#define DIR_ACCESS_OS_STR "DirAccessUnix"
#else
#error "Unknown platform"
#endif

String FileAccessGDRE::fix_path(const String &p_path) const {
	return PathFinder::_fix_path_file_access(p_path, mode_flags);
}

String DirAccessGDRE::fix_path(const String &p_path) const {
	return PathFinder::_fix_path_file_access(p_path);
}
//get_current_file_access_class
String GDREPackedData::get_current_file_access_class(FileAccess::AccessType p_access_type) {
	Ref<FileAccess> fa = FileAccess::create(p_access_type);
	Ref<FILE_ACCESS_OS> fa_os = fa;
	if (fa_os.is_valid()) {
		return FILE_ACCESS_OS_STR;
	}
	Ref<FileAccessPack> fa_pack = fa;
	if (fa_pack.is_valid()) {
		return "FileAccessPack";
	}
	Ref<FileAccessGDRE> fa_gdre = fa;
	if (fa_gdre.is_valid()) {
		return "FileAccessGDRE";
	}
	return "";
}

String GDREPackedData::get_current_dir_access_class(DirAccess::AccessType p_access_type) {
	Ref<DirAccess> da = DirAccess::create(p_access_type);
	Ref<DIR_ACCESS_OS> da_os = da;
	if (da_os.is_valid()) {
		return DIR_ACCESS_OS_STR;
	}
	Ref<DirAccessPack> da_pack = da;
	if (da_pack.is_valid()) {
		return "DirAccessPack";
	}
	Ref<DirAccessGDRE> da_gdre = da;
	if (da_gdre.is_valid()) {
		return "DirAccessGDRE";
	}
	return "";
}

String GDREPackedData::get_os_file_access_class_name() {
	return FILE_ACCESS_OS_STR;
}

String GDREPackedData::get_os_dir_access_class_name() {
	return DIR_ACCESS_OS_STR;
}

Ref<DirAccess> DirAccessGDRE::_open_filesystem() {
	// DirAccessGDRE is only made default for DirAccess::ACCESS_RESOURCES
#if DEBUG_ENABLED
	WARN_PRINT("opening filesystem path in DirAccessGDRE...");
#endif
	String path = GDRESettings::get_singleton()->get_project_path();
	if (path == "") {
		path = "res://";
	}
	Ref<DirAccessProxy<DIR_ACCESS_OS>> dir_proxy = memnew(DirAccessProxy<DIR_ACCESS_OS>);
	dir_proxy->change_dir(path);
	return dir_proxy;
}

void GDREPackedData::set_default_file_access() {
	if (set_file_access_defaults) {
		return;
	}
	if (old_dir_access_class.is_empty()) {
		old_dir_access_class = get_current_dir_access_class(DirAccess::ACCESS_RESOURCES);
	}
	FileAccess::make_default<FileAccessGDRE>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessGDRE>(FileAccess::ACCESS_USERDATA); // for user:// files in the pack
	DirAccess::make_default<DirAccessGDRE>(DirAccess::ACCESS_RESOURCES);
	set_file_access_defaults = true;
}

void GDREPackedData::reset_default_file_access() {
	if (!set_file_access_defaults) {
		return;
	}
	// we need to check to see if the real PackedData has the GDRE packed data loaded
	// if it does, we need to reset the default DirAccess to DirAccessPack
	// (FileAccessPack is never set to the default for ACCESS_RESOURCES)
	if (old_dir_access_class == "DirAccessPack") {
		DirAccess::make_default<DirAccessPack>(DirAccess::ACCESS_RESOURCES);
	} else if (old_dir_access_class == DIR_ACCESS_OS_STR) {
		DirAccess::make_default<DIR_ACCESS_OS>(DirAccess::ACCESS_RESOURCES);
	} else {
		WARN_PRINT("WARNING: reset_default_file_access: Unknown default DirAccess class, guessing...");
		if (real_packed_data_has_pack_loaded()) { // if the real PackedData has the GDRE packed data loaded
			DirAccess::make_default<DirAccessPack>(DirAccess::ACCESS_RESOURCES);
		} else {
			DirAccess::make_default<DIR_ACCESS_OS>(DirAccess::ACCESS_RESOURCES);
		}
	}
	old_dir_access_class = "";
	FileAccess::make_default<FILE_ACCESS_OS>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FILE_ACCESS_OS>(FileAccess::ACCESS_USERDATA);
	set_file_access_defaults = false;
}

Ref<FileAccess> FileAccessGDRE::_open_filesystem(const String &p_path, int p_mode_flags, Error *r_error) {
	Ref<FileAccessProxy<FILE_ACCESS_OS>> file_proxy = memnew(FileAccessProxy<FILE_ACCESS_OS>);

	Error err = file_proxy->open_internal(p_path, p_mode_flags);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		file_proxy.unref();
	}
	return file_proxy;
}

void FileAccessGDRE::_set_access_type(AccessType p_access) {
	access_type = p_access;
	FileAccess::_set_access_type(p_access);
}

bool PathFinder::real_packed_data_has_path(const String &p_path, bool check_disabled) {
	return PackedData::get_singleton() && (!check_disabled || !PackedData::get_singleton()->is_disabled()) && PackedData::get_singleton()->has_path(p_path);
}

bool PathFinder::gdre_packed_data_valid_path(const String &p_path) {
	return GDREPackedData::get_singleton() && !GDREPackedData::get_singleton()->is_disabled() && GDREPackedData::get_singleton()->has_path(p_path);
}

String PathFinder::_fix_path_file_access(const String &p_p_path, int p_mode_flags) {
	String p_path = p_p_path.replace("\\", "/");
	if (p_path.begins_with("res://")) {
		if (is_gdre_file(p_path)) {
			WARN_PRINT("WARNING: Calling fix_path on a gdre file...");
			if (should_check_pack(p_mode_flags)) {
				if (gdre_packed_data_valid_path(p_path)) {
					WARN_PRINT("WARNING: fix_path: gdre file is in a loaded external pack???? PLEASE REPORT THIS!!!!");
					return p_path.trim_prefix("res://");
				};
				// PackedData is disabled if an external pack is loaded, so we don't check if it's disabled here
				if (real_packed_data_has_path(p_path)) {
					return p_path.trim_prefix("res://");
				}
			}
			String res_path = GDRESettings::get_singleton()->get_gdre_resource_path();
			if (res_path != "") {
				return p_path.replace("res:/", res_path);
			}
			return p_path.trim_prefix("res://");
		}
		String project_path = GDRESettings::get_singleton()->get_project_path();
		if (project_path != "") {
			return p_path.replace("res:/", project_path);
		}
		return p_path.trim_prefix("res://");
	} else if (p_path.begins_with("user://")) { // Some packs have user files in them, so we need to check for those
		if (p_path.get_file().begins_with("gdre_")) {
			WARN_PRINT(vformat("WARNING: Calling fix_path on a gdre file %s...", p_path));
			if (should_check_pack(p_mode_flags)) {
				if (gdre_packed_data_valid_path(p_path)) {
					WARN_PRINT(vformat("WARNING: fix_path: gdre file %s is in a loaded external pack???? PLEASE REPORT THIS!!!!", p_path));
					return p_path.replace_first("user://", "");
				};
			}
		}

		// check if the file is in the PackedData first
		if (should_check_pack(p_mode_flags) && gdre_packed_data_valid_path(p_path)) {
			return p_path.replace_first("user://", "");
		}
		String data_dir = OS::get_singleton()->get_user_data_dir();
		if (!data_dir.is_empty()) {
			return p_path.replace("user:/", data_dir);
		}
		return p_path.replace("user://", "");

		// otherwise, fall through to call base class
	}
	String r_path = p_p_path;
	if (r_path.is_relative_path()) {
		if (!GDRESettings::get_singleton()->get_project_path().is_empty()) {
			r_path = GDRESettings::get_singleton()->get_project_path().path_join(r_path);
		} else {
			r_path = GDRESettings::get_singleton()->get_cwd().path_join(r_path);
		}
	}
	// TODO: This will have to be modified if additional fix_path overrides are added
#ifdef WINDOWS_ENABLED
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
	if (r_path.is_absolute_path() && !r_path.is_network_share_path() && r_path.length() > MAX_PATH) {
		r_path = "\\\\?\\" + r_path.replace("/", "\\");
	}
	return r_path;
#else
	r_path = r_path.replace("\\", "/");
	return r_path;
#endif
}
