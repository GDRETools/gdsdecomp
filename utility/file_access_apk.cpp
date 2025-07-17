/*************************************************************************/
/*  file_access_apk.cpp                                                  */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/
#include "file_access_apk.h"

#ifdef MINIZIP_ENABLED
#include "thirdparty/minizip/ioapi.h"

#include "axml_parser.h"
#include "file_access_gdre.h"
#include "gdre_settings.h"

#include "core/io/file_access.h"

APKArchive *APKArchive::instance = nullptr;

extern "C" {

struct APKData {
	Ref<FileAccess> f;
};

static void *godot_open(voidpf opaque, const void *p_fname, int mode) {
	if (mode & ZLIB_FILEFUNC_MODE_WRITE) {
		return nullptr;
	}

	Ref<FileAccess> f = FileAccess::open(String::utf8((const char *)p_fname), FileAccess::READ);
	ERR_FAIL_COND_V(f.is_null(), nullptr);

	APKData *zd = memnew(APKData);
	zd->f = f;
	return zd;
}

static uLong godot_read(voidpf opaque, voidpf stream, void *buf, uLong size) {
	APKData *zd = (APKData *)stream;
	zd->f->get_buffer((uint8_t *)buf, size);
	return size;
}

static uLong godot_write(voidpf opaque, voidpf stream, const void *buf, uLong size) {
	return 0;
}

static ZPOS64_T godot_tell(voidpf opaque, voidpf stream) {
	APKData *zd = (APKData *)stream;
	return zd->f->get_position();
}

static long godot_seek(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) {
	APKData *zd = (APKData *)stream;

	uint64_t pos = offset;
	switch (origin) {
		case ZLIB_FILEFUNC_SEEK_CUR:
			pos = zd->f->get_position() + offset;
			break;
		case ZLIB_FILEFUNC_SEEK_END:
			pos = zd->f->get_length() + offset;
			break;
		default:
			break;
	}

	zd->f->seek(pos);
	return 0;
}

static int godot_close(voidpf opaque, voidpf stream) {
	APKData *zd = (APKData *)stream;
	memdelete(zd);
	return 0;
}

static int godot_testerror(voidpf opaque, voidpf stream) {
	APKData *zd = (APKData *)stream;
	return zd->f->get_error() != OK ? 1 : 0;
}

static voidpf godot_alloc(voidpf opaque, uInt items, uInt size) {
	return memalloc((size_t)items * size);
}

static void godot_free(voidpf opaque, voidpf address) {
	memfree(address);
}
} // extern "C"

void APKArchive::close_handle(unzFile p_file) const {
	ERR_FAIL_COND_MSG(!p_file, "Cannot close a file if none is open.");
	unzCloseCurrentFile(p_file);
	unzClose(p_file);
}

unzFile APKArchive::get_file_handle(String p_file) const {
	ERR_FAIL_COND_V_MSG(!file_exists(p_file), nullptr, "File '" + p_file + " doesn't exist.");
	File file = files[p_file];

	zlib_filefunc64_def io;
	memset(&io, 0, sizeof(io));

	io.opaque = nullptr;
	io.zopen64_file = godot_open;
	io.zread_file = godot_read;
	io.zwrite_file = godot_write;

	io.ztell64_file = godot_tell;
	io.zseek64_file = godot_seek;
	io.zclose_file = godot_close;
	io.zerror_file = godot_testerror;

	io.alloc_mem = godot_alloc;
	io.free_mem = godot_free;

	unzFile pkg = unzOpen2_64(packages[file.package].filename.utf8().get_data(), &io);
	ERR_FAIL_COND_V_MSG(!pkg, nullptr, "Cannot open file '" + packages[file.package].filename + "'.");
	int unz_err = unzGoToFilePos(pkg, &file.file_pos);
	if (unz_err != UNZ_OK || unzOpenCurrentFile(pkg) != UNZ_OK) {
		unzClose(pkg);
		ERR_FAIL_V(nullptr);
	}

	return pkg;
}

Error APKArchive::get_version_string_from_manifest(String &version_string) {
	AXMLParser parser;
	Ref<FileAccessAPK> thing = memnew(FileAccessAPK("AndroidManifest.xml"));
	Vector<uint8_t> buf;
	buf.resize(thing->get_length());
	thing->get_buffer(buf.ptrw(), thing->get_length());
	Error err = parser.parse_manifest(buf);
	ERR_FAIL_COND_V_MSG(err, err, "Failed to parse manifest!");
	//right now we are only interested in the version
	version_string = parser.get_godot_library_version_string();
	// Godot 2.x did not write version strings to the manifest
	if (version_string.is_empty()) {
		version_string = "unknown";
	}
	return OK;
}

bool APKArchive::try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset = 0) {
	// load with offset feature only supported for PCK files
	ERR_FAIL_COND_V_MSG(p_offset != 0, false, "Invalid PCK data. Note that loading files with a non-zero offset isn't supported with ZIP archives.");
	String pack_path = p_path.replace("_GDRE_a_really_dumb_hack", "");
	String ext = pack_path.get_extension().to_lower();
	// This handles zip files, too
	if (ext != "apk" && ext != "zip") {
		return false;
	}
	bool is_apk = ext == "apk";
	zlib_filefunc64_def io;
	memset(&io, 0, sizeof(io));

	io.opaque = nullptr;
	io.zopen64_file = godot_open;
	io.zread_file = godot_read;
	io.zwrite_file = godot_write;

	io.ztell64_file = godot_tell;
	io.zseek64_file = godot_seek;
	io.zclose_file = godot_close;
	io.zerror_file = godot_testerror;

	io.alloc_mem = godot_alloc;
	io.free_mem = godot_free;

	unzFile zfile = unzOpen2_64(pack_path.utf8().get_data(), &io);
	ERR_FAIL_COND_V(!zfile, false);

	unz_global_info64 gi;
	int err = unzGetGlobalInfo64(zfile, &gi);
	ERR_FAIL_COND_V(err != UNZ_OK, false);

	Package pkg;
	pkg.filename = pack_path;
	pkg.zfile = zfile;
	packages.push_back(pkg);
	int pkg_num = packages.size() - 1;
	uint32_t asset_count = 0;
	uint32_t fmt_ver = 1;
	Ref<GodotVer> godot_ver;
	godot_ver.instantiate();
	String ver_string = "unknown";
	for (uint64_t i = 0; i < gi.number_entry; i++) {
		char filename_inzip[256];

		unz_file_info64 file_info;
		err = unzGetCurrentFileInfo64(zfile, &file_info, filename_inzip, sizeof(filename_inzip), nullptr, 0, nullptr, 0);
		ERR_CONTINUE(err != UNZ_OK);

		File f;
		f.package = pkg_num;

		unzGetFilePos(zfile, &f.file_pos);
		String original_fname = String::utf8(filename_inzip);
		String fname;
		if (is_apk) {
			if (original_fname == "AndroidManifest.xml") {
				files[original_fname] = f;
				if (get_version_string_from_manifest(ver_string) != OK) {
					WARN_PRINT("Failed to parse APK manifest!");
				}
				if (ver_string == "unknown" || ver_string.is_empty()) {
					// Godot 2.x did not set a version string in the manifest, and there's no other easy way to get it
					// get it from the bin resources
					WARN_PRINT("Could not retrieve version string from AndroidManifest.xml");
				} else {
					godot_ver = GodotVer::parse(ver_string);
				}

				// reset the position
				unzGoToFilePos(zfile, &f.file_pos);
				if ((i + 1) < gi.number_entry) {
					unzGoToNextFile(zfile);
				}
				continue;
			} else if (!original_fname.begins_with("assets/")) {
				files[original_fname] = f;
				if ((i + 1) < gi.number_entry) {
					unzGoToNextFile(zfile);
				}
				continue;
			} else {
				fname = original_fname.replace_first("assets/", "res://");
			}
		} else {
			fname = "res://" + original_fname;
			if ((fname.ends_with("/") || fname.ends_with("\\")) && file_info.uncompressed_size == 0) {
				// phantom directory, skip it
				if ((i + 1) < gi.number_entry) {
					unzGoToNextFile(zfile);
				}
				continue;
			}
		}

		asset_count++;
		files[fname] = f;

		static constexpr const uint8_t md5[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		GDREPackedData::get_singleton()->add_path(pack_path, fname, 1, file_info.uncompressed_size, md5, this, p_replace_files, false);

		if ((i + 1) < gi.number_entry) {
			unzGoToNextFile(zfile);
		}
	}
	Ref<GDRESettings::PackInfo> pckinfo;
	pckinfo.instantiate();
	pckinfo->init(pack_path, godot_ver, fmt_ver, 0, 0, asset_count, is_apk ? GDRESettings::PackInfo::APK : GDRESettings::PackInfo::ZIP);
	GDRESettings::get_singleton()->add_pack_info(pckinfo);

	return true;
}

bool APKArchive::file_exists(String p_name) const {
	return files.has(p_name);
}

Ref<FileAccess> APKArchive::get_file(const String &p_path, PackedData::PackedFile *p_file) {
	return memnew(FileAccessAPK(p_path, *p_file));
}

APKArchive *APKArchive::get_singleton() {
	if (instance == nullptr) {
		instance = memnew(APKArchive);
	}

	return instance;
}

APKArchive::APKArchive() {
	instance = this;
}

APKArchive::~APKArchive() {
	for (int i = 0; i < packages.size(); i++) {
		unzClose(packages[i].zfile);
	}

	packages.clear();
	files.clear();
	if (instance == this) {
		instance = nullptr;
	}
}

Error FileAccessAPK::open_internal(const String &p_path, int p_mode_flags) {
	_close();

	ERR_FAIL_COND_V(p_mode_flags & FileAccess::WRITE, FAILED);
	APKArchive *arch = APKArchive::get_singleton();
	ERR_FAIL_COND_V(!arch, FAILED);
	zfile = arch->get_file_handle(p_path);
	ERR_FAIL_COND_V(!zfile, FAILED);

	int err = unzGetCurrentFileInfo64(zfile, &file_info, nullptr, 0, nullptr, 0, nullptr, 0);
	ERR_FAIL_COND_V(err != UNZ_OK, FAILED);

	return OK;
}

void FileAccessAPK::_close() {
	if (!zfile) {
		return;
	}

	APKArchive *arch = APKArchive::get_singleton();
	ERR_FAIL_COND(!arch);
	arch->close_handle(zfile);
	zfile = nullptr;
}

bool FileAccessAPK::is_open() const {
	return zfile != nullptr;
}

void FileAccessAPK::seek(uint64_t p_position) {
	ERR_FAIL_COND(!zfile);

	unzSeekCurrentFile(zfile, p_position);
}

void FileAccessAPK::seek_end(int64_t p_position) {
	ERR_FAIL_COND(!zfile);
	unzSeekCurrentFile(zfile, get_length() + p_position);
}

uint64_t FileAccessAPK::get_position() const {
	ERR_FAIL_COND_V(!zfile, 0);
	return unztell(zfile);
}

uint64_t FileAccessAPK::get_length() const {
	ERR_FAIL_COND_V(!zfile, 0);
	return file_info.uncompressed_size;
}

bool FileAccessAPK::eof_reached() const {
	ERR_FAIL_COND_V(!zfile, true);

	return at_eof;
}

uint8_t FileAccessAPK::get_8() const {
	uint8_t ret = 0;
	get_buffer(&ret, 1);
	return ret;
}

uint64_t FileAccessAPK::get_buffer(uint8_t *p_dst, uint64_t p_length) const {
	ERR_FAIL_COND_V(!p_dst && p_length > 0, -1);
	ERR_FAIL_COND_V(!zfile, -1);

	at_eof = unzeof(zfile);
	if (at_eof) {
		return 0;
	}
	int64_t read = unzReadCurrentFile(zfile, p_dst, p_length);
	ERR_FAIL_COND_V(read < 0, read);
	if ((uint64_t)read < p_length) {
		at_eof = true;
	}
	return read;
}

Error FileAccessAPK::get_error() const {
	if (!zfile) {
		return ERR_UNCONFIGURED;
	}
	if (eof_reached()) {
		return ERR_FILE_EOF;
	}

	return OK;
}

void FileAccessAPK::flush() {
	ERR_FAIL();
}

bool FileAccessAPK::store_8(uint8_t p_dest) {
	ERR_FAIL_V(false);
}
bool FileAccessAPK::store_buffer(const uint8_t *p_src, uint64_t p_length) {
	ERR_FAIL_V(false);
} ///< store an array of bytes, needs to be overwritten by children.

bool FileAccessAPK::file_exists(const String &p_name) {
	return false;
}
void FileAccessAPK::close() {
	_close();
}
FileAccessAPK::FileAccessAPK(const String &p_path) {
	open_internal(p_path, FileAccess::READ);
}
FileAccessAPK::FileAccessAPK(const String &p_path, const PackedData::PackedFile &p_file) {
	open_internal(p_path, FileAccess::READ);
}

FileAccessAPK::~FileAccessAPK() {
	_close();
}

#endif // MINIZIP_ENABLED
