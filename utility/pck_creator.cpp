#include "pck_creator.h"

#include "core/error/error_list.h"
#include "gdre_packed_source.h"
#include "gdre_settings.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "utility/common.h"
#include "utility/packed_file_info.h"
#include "utility/task_manager.h"

#include <core/io/file_access_encrypted.h>
#include <scene/resources/surface_tool.h>
#include <utility/gdre_standalone.h>

void PckCreator::reset() {
	files_to_pck.clear();
	offset = 0;
	error_string.clear();
	cancelled = false;
	broken_cnt = 0;
	data_read = 0;
}

static const Vector<String> banned_files = { "thumbs.db", ".DS_Store" };

struct PckCreateListDirTaskData {
	const String dir;
	const Vector<String> wildcards;
	const bool absolute;
	const String start_rel;
	const Vector<String> excludes;

	struct Token {
		String subdir;
		Vector<String> ret;
	};

	bool should_filter_file(const String &file) {
		for (int64_t j = 0; j < banned_files.size(); j++) {
			if (file.ends_with(banned_files[j])) {
				return true;
			}
		}
		// we have to check the exclude filters now
		for (int64_t j = 0; j < excludes.size(); j++) {
			if (file.matchn(excludes[j])) {
				return true;
			}
		}
		if (wildcards.size() > 0) {
			for (int64_t j = 0; j < wildcards.size(); j++) {
				if (file.matchn(wildcards[j])) {
					return true;
				}
			}
			return false;
		}
		return false;
	}

	void do_subdir_task(int i, Token *p_subdir) {
		Token &token = p_subdir[i];
		token.ret = list_dir(token.subdir, false);
	}

	String get_step_description(int i, Token *p_subdir) {
		return "Reading folder " + p_subdir[i].subdir + "...";
	}

	Vector<String> run() {
		return list_dir(start_rel, true);
	}

	Vector<String> list_dir(const String &rel, bool is_main = false) {
		Vector<String> ret;
		Error err;
		Ref<DirAccess> da = DirAccess::open(dir.path_join(rel), &err);
		ERR_FAIL_COND_V_MSG(da.is_null(), ret, "Failed to open directory " + dir);

		if (da.is_null()) {
			return ret;
		}
		Vector<String> dirs;
		Vector<String> files;

		String base = absolute ? dir : "";
		da->list_dir_begin();
		String f = da->get_next();
		while (!f.is_empty()) {
			if (f == "." || f == "..") {
				f = da->get_next();
				continue;
			} else if (da->current_is_dir()) {
				dirs.push_back(f);
			} else {
				files.push_back(f);
			}
			f = da->get_next();
		}
		da->list_dir_end();

		dirs.sort_custom<FileNoCaseComparator>();
		if (is_main) {
			Vector<PckCreateListDirTaskData::Token> tokens;
			for (auto &d : dirs) {
				tokens.push_back(PckCreateListDirTaskData::Token{ rel.path_join(d), {} });
			}

			Ref<EditorProgressGDDC> ep;
			TaskManager::TaskManagerID group_id = -1;
			if (tokens.size() > 0) {
				String desc = "Reading folder " + dir + " structure...";
				String task = "ListDirTaskData(" + dir + +")_" + String::num_int64(OS::get_singleton()->get_ticks_usec());
				ep = EditorProgressGDDC::create(nullptr, task, desc, -1, true);
				group_id = TaskManager::get_singleton()->add_group_task(
						this, &PckCreateListDirTaskData::do_subdir_task,
						tokens.ptrw(), tokens.size(),
						&PckCreateListDirTaskData::get_step_description,
						task, desc,
						true, -1, true, ep, 0);
			}
			// while we wait for the subdirs to be read, we can filter the files
			files.sort_custom<FileNoCaseComparator>();
			for (int64_t i = files.size() - 1; i >= 0; i--) {
				TaskManager::get_singleton()->update_progress_bg();
				files.write[i] = base.path_join(rel).path_join(files[i]);
				if (should_filter_file(files[i])) {
					files.remove_at(i);
					continue;
				}
			}
			if (group_id != -1) {
				TaskManager::get_singleton()->wait_for_task_completion(group_id);
			}
			for (auto &t : tokens) {
				ret.append_array(std::move(t.ret));
			}
			ret.append_array(std::move(files));
		} else {
			for (auto &d : dirs) {
				ret.append_array(list_dir(rel.path_join(d), false));
			}
			for (auto &file : files) {
				String full_path = base.path_join(rel).path_join(file);
				if (!should_filter_file(full_path)) {
					ret.append(full_path);
				}
			}
		}

		return ret;
	}
};

Vector<String> PckCreator::get_files_to_pack(const String &p_dir, const Vector<String> &include_filters, const Vector<String> &p_exclude_filters) {
	return PckCreateListDirTaskData{ p_dir, include_filters, false, "", p_exclude_filters }.run();
}

#ifdef DEBUG_ENABLED
#define bl_print(...) print_line(__VA_ARGS__)
#else
#define bl_print(...) print_verbose(__VA_ARGS__)
#endif

Error PckCreator::pck_create(const String &p_pck_path, const String &p_dir, const Vector<String> &include_filters, const Vector<String> &exclude_filters) {
	uint64_t start_time = OS::get_singleton()->get_ticks_msec();
	Vector<String> file_paths_to_pack;
	{
		file_paths_to_pack = get_files_to_pack(p_dir, include_filters, exclude_filters);
		bl_print("PCK get_files_to_pack took " + itos(OS::get_singleton()->get_ticks_msec() - start_time) + "ms");
	}
	Error err = OK;
	{
		err = _process_folder(p_pck_path, p_dir, file_paths_to_pack);
	}
	ERR_FAIL_COND_V_MSG(err, err, "Error processing folder: " + error_string);
	{
		err = _create_after_process();
	}
	ERR_FAIL_COND_V_MSG(err, err, "Error creating pck: " + error_string);
	return OK;
}

void PckCreator::_do_process_folder(uint32_t i, File *tokens) {
	if (unlikely(cancelled)) {
		return;
	}
	auto &token = tokens[i];
	String path = token.src_path;
	if (!FileAccess::exists(path)) {
		token.err = ERR_FILE_NOT_FOUND;
		++broken_cnt;
		return;
	}
	{
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
		if (file.is_null()) {
			token.err = ERR_FILE_CANT_OPEN;
			++broken_cnt;
			return;
		}
		token.size = file->get_length();
	}
	if (token.size == 0) {
		token.md5.resize_initialized(16);
		return;
	}
	auto md5_str = FileAccess::get_md5(path);
	if (md5_str.is_empty()) {
		token.err = ERR_FILE_CANT_OPEN;
		++broken_cnt;
		return;
	}
	token.md5 = md5_str.hex_decode();
}

namespace {
static constexpr int64_t PCK_PADDING = 32;
static int64_t _get_pad(int64_t p_alignment, int64_t p_n) {
	int64_t rest = p_n % p_alignment;
	int64_t pad = 0;
	if (rest > 0) {
		pad = p_alignment - rest;
	}

	return pad;
}

static uint64_t get_encryption_padding(uint64_t _size) {
	uint64_t pad = 0;
	if (_size % 16) { // Pad to encryption block size.
		pad += 16 - (_size % 16);
	}
	pad += 16; // hash
	pad += 8; // data size
	pad += 16; // iv
	return pad;
}
const static Vector<uint8_t> empty_md5 = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

} //namespace

// TODO: rename this to something like "GUI start" or something
Error PckCreator::_process_folder(
		const String &p_pck_path,
		const String &p_dir,
		const Vector<String> &file_paths_to_pack) {
	HashMap<String, String> map;
	for (int64_t i = 0; i < file_paths_to_pack.size(); i++) {
		map[p_dir.path_join(file_paths_to_pack[i])] = file_paths_to_pack[i];
	}
	start_pck(p_pck_path, version, ver_major, ver_minor, ver_rev, encrypt, embed, exe_to_embed, watermark);
	auto ret = _add_files(map);
	return ret;
}

void PckCreator::start_pck(const String &p_pck_path, int pck_version, int p_ver_major, int p_ver_minor, int p_ver_rev, bool p_encrypt, bool p_embed, const String &p_exe_to_embed, const String &p_watermark) {
	reset();
	offset = 0;
	pck_path = p_pck_path;
	version = pck_version;
	ver_major = p_ver_major;
	ver_minor = p_ver_minor;
	ver_rev = p_ver_rev;
	encrypt = p_encrypt;
	embed = p_embed;
	exe_to_embed = p_exe_to_embed;
	watermark = p_watermark;
}

Error PckCreator::add_files(Dictionary file_paths_to_pack) {
	HashMap<String, String> map;
	for (auto &e : file_paths_to_pack.keys()) {
		map[e] = file_paths_to_pack[e];
	}
	return _add_files(map);
}

Error PckCreator::_add_files(
		const HashMap<String, String> &file_paths_to_pack) {
	uint64_t start_time = OS::get_singleton()->get_ticks_msec();
	files_to_pck.resize(file_paths_to_pack.size());
	Vector<String> keys;
	keys.resize(file_paths_to_pack.size());
	{
		size_t i = 0;
		for (auto &e : file_paths_to_pack) {
			files_to_pck.write[i] = { e.value, e.key, 0, 0, encrypt, false, empty_md5 };
			keys.write[i] = e.key;
			i++;
		}
	}
	Error err = OK;
	err = TaskManager::get_singleton()->run_multithreaded_group_task(
			this,
			&PckCreator::_do_process_folder,
			files_to_pck.ptrw(),
			files_to_pck.size(),
			&PckCreator::get_file_description,
			"PckCreator::_do_process_folder",
			"Getting file info...");
	if (err == ERR_SKIP) {
		return ERR_SKIP;
	}
	if (broken_cnt > 0) {
		err = ERR_BUG;
		for (int64_t i = 0; i < files_to_pck.size(); i++) {
			if (files_to_pck[i].err != OK) {
				String err_type;
				if (files_to_pck[i].err == ERR_FILE_CANT_OPEN) {
					err_type = "FileAccess error";
				} else if (files_to_pck[i].err == ERR_CANT_CREATE) {
					err_type = "FileCreate error";
				} else if (files_to_pck[i].err == ERR_FILE_CANT_WRITE) {
					err_type = "FileWrite error";
				} else {
					err_type = "Unknown error";
				}
				error_string += files_to_pck[i].src_path + "(" + err_type + ")\n";
			}
		}
	}

	if (error_string.length() > 0) {
		print_error("At least one error was detected while adding files!\n" + error_string);
		return err;
	}
	files_to_pck.resize(files_to_pck.size());
	for (int64_t i = 0; i < files_to_pck.size(); i++) {
		files_to_pck.write[i].ofs = offset;
		uint64_t _size = files_to_pck[i].size;
		if (encrypt) { // Add encryption overhead.
			_size += get_encryption_padding(_size);
		}

		offset += _size;
		offset += _get_pad(PCK_PADDING, offset);
	}
	bl_print("PCK folder processing took " + itos(OS::get_singleton()->get_ticks_msec() - start_time) + "ms");
	return OK;
}

Error PckCreator::read_and_write_file(size_t i, Ref<FileAccess> write_handle) {
	Error error;
	Ref<FileAccess> fa = FileAccess::open(files_to_pck[i].src_path, FileAccess::READ, &error);
	if (!fa.is_valid()) {
		return error ? error : ERR_FILE_CANT_OPEN;
	}
	int64_t rq_size = files_to_pck[i].size;
	uint8_t buf[piecemeal_read_size];
	while (rq_size > 0) {
		uint64_t got = fa->get_buffer(buf, MIN(piecemeal_read_size, rq_size));
		write_handle->store_buffer(buf, got);
		rq_size -= got;
	}
	return OK;
}

Error PckCreator::finish_pck() {
	Error error = _create_after_process();
	ERR_FAIL_COND_V_MSG(error && error != ERR_SKIP && error != ERR_PRINTER_ON_FIRE, error, "Error creating pck: " + error_string);
	return error;
}

String PckCreator::get_file_description(int64_t i, File *userdata) {
	return userdata[i].src_path;
}

void PckCreator::_do_write_file(uint32_t i, File *files_to_pck) {
	if (unlikely(cancelled)) {
		return;
	}
	if (encryption_error != OK) {
		return;
	}
	DEV_ASSERT(f->get_position() == files_start + files_to_pck[i].ofs);
	Ref<FileAccessEncrypted> fae;
	Ref<FileAccess> ftmp = f;
	if (encrypt) {
		fae.instantiate();

		files_to_pck[i].err = fae->open_and_parse(f, key, FileAccessEncrypted::MODE_WRITE_AES256, false);
		if (files_to_pck[i].err != OK) {
			encryption_error = files_to_pck[i].err;
			broken_cnt++;
			cancelled = true;
			return;
		}
		ftmp = fae;
	}
	files_to_pck[i].err = read_and_write_file(i, ftmp);
	if (files_to_pck[i].err != OK) {
		switch (files_to_pck[i].err) {
			case ERR_FILE_CANT_OPEN:
				error_string += files_to_pck[i].path + " (File read error)\n";
				break;
			case ERR_FILE_CANT_WRITE:
				error_string += files_to_pck[i].path + " (File write error)\n";
				break;
			default:
				error_string += files_to_pck[i].path + " (Unknown error)\n";
				break;
		}
		broken_cnt++;
		cancelled = true;
		return;
	}

	if (fae.is_valid()) {
		ftmp.unref();
		fae.unref();
	}

	int pad = _get_pad(PCK_PADDING, f->get_position());
	for (int j = 0; j < pad; j++) {
		f->store_8(0);
	}
}

Error PckCreator::_create_after_process() {
	Ref<EditorProgressGDDC> pr = EditorProgressGDDC::create(nullptr, "re_write_pck", "Writing PCK archive...", (int)files_to_pck.size(), true);
	cancelled = false;
	broken_cnt = 0;
	f = nullptr;
	encryption_error = OK;
	files_start = 0;
	file_base = 0;
	key = GDRESettings::get_singleton()->get_encryption_key();
	uint64_t start_time = OS::get_singleton()->get_ticks_msec();

	auto temp_path = pck_path;
	if (embed && get_exe_to_embed().is_empty()) {
		error_string = "No executable to embed!";
		return ERR_FILE_NOT_FOUND;
	}

	// create a tmp file if the pck file already exists
	if (FileAccess::exists(pck_path) || exe_to_embed.simplify_path() == pck_path.simplify_path()) {
		temp_path = pck_path + ".tmp";
	}

	f = FileAccess::open(temp_path, FileAccess::WRITE);
	if (f.is_null()) {
		error_string = ("Error opening PCK file: ") + temp_path;
		return ERR_FILE_CANT_WRITE;
	}
	int64_t embedded_start = 0;
	int64_t embedded_size = 0;

	GDREPackedSource::EXEPCKInfo exe_pck_info;
	int64_t absolute_exe_end = 0;
	if (embed) {
		// append to exe

		Ref<FileAccess> fs = FileAccess::open(exe_to_embed, FileAccess::READ);
		if (fs.is_null()) {
			error_string = ("Error opening executable file: ") + exe_to_embed;
			return ERR_FILE_CANT_READ;
		}
		pr->step("Exec...", 0, true);

		fs->seek_end();
		absolute_exe_end = fs->get_position();
		int64_t exe_end;
		if (GDREPackedSource::get_exe_embedded_pck_info(exe_to_embed, exe_pck_info)) {
			exe_end = exe_pck_info.pck_embed_off;
		} else {
			exe_end = absolute_exe_end;
		}
		fs->seek(0);
		// copy executable data
		for (size_t i = 0; i < exe_end; i++) {
			f->store_8(fs->get_8());
		}

		embedded_start = f->get_position();

		// ensure embedded PCK starts at a 64-bit multiple
		size_t pad = f->get_position() % 8;
		for (size_t i = 0; i < pad; i++) {
			f->store_8(0);
		}
	}
	pck_start_pos = f->get_position();

	f->store_32(0x43504447); //GDPK
	f->store_32(version);
	f->store_32(ver_major);
	f->store_32(ver_minor);
	f->store_32(ver_rev);

	int64_t file_base_ofs = 0;
	int64_t dir_base_ofs = 0;
	uint32_t pack_flags = version >= PACK_FORMAT_VERSION_V3 ? PACK_REL_FILEBASE : 0;
	if (version >= PACK_FORMAT_VERSION_V2) {
		if (encrypt) {
			pack_flags |= PACK_DIR_ENCRYPTED;
		}
		f->store_32(pack_flags); // flags
		file_base_ofs = f->get_position();
		f->store_64(0); // files base
		if (version >= PACK_FORMAT_VERSION_V3) {
			dir_base_ofs = f->get_position();
			f->store_64(0); // directory offset
		}
	}

	for (size_t i = 0; i < 16; i++) {
		//reserved
		f->store_32(0);
	}

	auto write_header = [&]() {
		bool use_rel_filebase = (pack_flags & PACK_REL_FILEBASE) != 0;
		pr->step("Header...", 0, true);
		f->store_32(files_to_pck.size()); //amount of files
		// used by pck version 0-1, where the file offsets include the header size; pck version 2 uses the offset from the header
		size_t header_size = f->get_position();
		size_t predir_size = header_size;

		Ref<FileAccessEncrypted> fae;
		Ref<FileAccess> fhead = f;
		if (version >= PACK_FORMAT_VERSION_V2) {
			if (encrypt) {
				fae.instantiate();
				Error err = fae->open_and_parse(f, key, FileAccessEncrypted::MODE_WRITE_AES256, false);
				if (err != OK) {
					encryption_error = err;
					error_string = "Encryption error: Could not open file for writing (invalid key?)";
					f = nullptr;
					return err;
				}

				fhead = fae;
			}
		}

		bool add_res_prefix = !(ver_major == 4 && ver_minor >= 4);

		for (int64_t i = 0; i < files_to_pck.size(); i++) {
			if (add_res_prefix) {
				files_to_pck.write[i].path = files_to_pck[i].path.trim_prefix("res://");
				if (!files_to_pck[i].path.is_absolute_path()) {
					files_to_pck.write[i].path = "res://" + files_to_pck[i].path;
				}
			} else {
				files_to_pck.write[i].path = files_to_pck[i].path.trim_prefix("res://");
			}
			header_size += 4; // size of path string (32 bits is enough)
			uint32_t string_len = files_to_pck[i].path.utf8().length();
			uint32_t pad = _get_pad(4, string_len);
			header_size += string_len + pad; ///size of path string
			header_size += 8; // offset to file _with_ header size included
			header_size += 8; // size of file
			header_size += 16; // md5
			if (version >= PACK_FORMAT_VERSION_V2) {
				header_size += 4; // flags
			}
		}
		if (encrypt && version >= PACK_FORMAT_VERSION_V2) {
			header_size += get_encryption_padding(header_size - predir_size);
		}

		size_t header_padding = _get_pad(PCK_PADDING, header_size);
		pr->step("Directory...", 0, true);
		for (int64_t i = 0; i < files_to_pck.size(); i++) {
			uint32_t string_len = files_to_pck[i].path.utf8().length();
			uint32_t pad = _get_pad(4, string_len);

			fhead->store_32(uint32_t(string_len + pad));
			fhead->store_buffer((const uint8_t *)files_to_pck[i].path.utf8().get_data(), string_len);
			for (uint32_t j = 0; j < pad; j++) {
				fhead->store_8(0);
			}

			if (version >= PACK_FORMAT_VERSION_V2) {
				fhead->store_64(files_to_pck[i].ofs);
			} else {
				fhead->store_64(files_to_pck[i].ofs + header_padding + header_size);
			}
			fhead->store_64(files_to_pck[i].size); // pay attention here, this is where file is
			fhead->store_buffer(files_to_pck[i].md5.ptr(), 16); //also save md5 for file
			if (version >= PACK_FORMAT_VERSION_V2) {
				uint32_t flags = 0;
				if (files_to_pck[i].encrypted) {
					flags |= PACK_FILE_ENCRYPTED;
				}
				if (files_to_pck[i].removal) {
					flags |= PACK_FILE_REMOVAL;
				}
				fhead->store_32(flags);
			}
		}

		if (fae.is_valid()) {
			fhead.unref();
			fae.unref();
		}

		for (uint32_t j = 0; j < header_padding; j++) {
			f->store_8(0);
		}
		return OK;
	};

	if (version < PACK_FORMAT_VERSION_V3) {
		Error err = write_header();
		if (err != OK) {
			return err;
		}
	} else {
		// re-align
		int pad = _get_pad(PCK_PADDING, f->get_position());
		for (int i = 0; i < pad; i++) {
			f->store_8(0);
		}
	}

	files_start = f->get_position();
	file_base = files_start - ((pack_flags & PACK_REL_FILEBASE) != 0 ? pck_start_pos : 0);
	// DEV_ASSERT(file_base == header_size + header_padding);
	if (version >= PACK_FORMAT_VERSION_V2) {
		f->seek(file_base_ofs);
		f->store_64(file_base); // update files base
		f->seek(files_start);
	}

	Error err = TaskManager::get_singleton()->run_multithreaded_group_task(
			this,
			&PckCreator::_do_write_file,
			files_to_pck.ptrw(),
			files_to_pck.size(),
			&PckCreator::get_file_description,
			"Writing files...",
			"Writing files...",
			true,
			1, // single-threaded, but runs on the thread pool
			true,
			pr);
	if (err) { // cancelled
		f = nullptr;
		return err;
	}
	if (encryption_error != OK) {
		error_string = "Encryption error: Could not encrypt file!";
		f = nullptr;
		return encryption_error;
	}
	if (broken_cnt > 0) {
		error_string = "Error writing files: " + error_string;
		f = nullptr;
		return ERR_FILE_CANT_WRITE;
	}

	if (version >= PACK_FORMAT_VERSION_V3) {
		int dir_padding = _get_pad(PCK_PADDING, f->get_position());
		for (int i = 0; i < dir_padding; i++) {
			f->store_8(0);
		}

		uint64_t dir_offset = f->get_position();
		f->seek(dir_base_ofs);
		f->store_64(dir_offset - pck_start_pos); // update directory base
		f->seek(dir_offset);
		if (write_header() != OK) {
			return ERR_FILE_CANT_WRITE;
		}
	}

	if (watermark != "") {
		f->store_32(0);
		f->store_32(0);
		f->store_string(watermark);
		f->store_32(0);
		f->store_32(0);
	}

	f->store_32(0x43504447); //GDPK
	if (embed) {
		// ensure embedded data ends at a 64-bit multiple
		int64_t embed_end = f->get_position() - embedded_start + 12;
		uint64_t pad = embed_end % 8;
		for (int64_t i = 0; i < pad; i++) {
			f->store_8(0);
		}

		int64_t pck_size = f->get_position() - pck_start_pos;
		f->store_64(pck_size);
		f->store_32(0x43504447); //GDPC

		uint64_t current_eof = f->get_position();
		embedded_size = current_eof - embedded_start;

		// fixup headers
		pr->step("Exec header fix...", files_to_pck.size() + 2, true);
		if (exe_pck_info.pck_section_header_pos != 0) {
			if (exe_pck_info.type == GDREPackedSource::EXEPCKInfo::PE) {
				// Set virtual size to a little to avoid it taking memory (zero would give issues)
				f->seek(exe_pck_info.pck_section_header_pos + 8);
				f->store_32(8);
				f->seek(exe_pck_info.pck_section_header_pos + 16);
				f->store_32(embedded_size);
				f->seek(exe_pck_info.pck_section_header_pos + 20);
				f->store_32(embedded_start);
			} else if (exe_pck_info.type == GDREPackedSource::EXEPCKInfo::ELF) {
				if (exe_pck_info.section_bit_size == 32) {
					f->seek(exe_pck_info.pck_section_header_pos + 0x10);
					f->store_32(embedded_start);
					f->store_32(embedded_size);
				} else { // 64
					f->seek(exe_pck_info.pck_section_header_pos + 0x18);
					f->store_64(embedded_start);
					f->store_64(embedded_size);
				}
			}
		}
		// in case there is data at the end?
		if (absolute_exe_end > exe_pck_info.pck_embed_off + exe_pck_info.pck_embed_size) {
			WARN_PRINT("There is data at the end of the executable past the pck, this data will be lost!");
		}
	}
	f->flush();
	f = nullptr;
	if (temp_path != pck_path) {
		if (GDRESettings::get_singleton()->is_pack_loaded()) {
			// refusing to remove the original file while a pack is loaded
			error_string = temp_path;
			return ERR_PRINTER_ON_FIRE;
		}
		// rename temp file to final file
		auto da = DirAccess::open(pck_path.get_base_dir());
		if (da.is_null()) {
			error_string = "Error opening directory for renaming: " + pck_path.get_base_dir();
			return ERR_FILE_CANT_OPEN;
		}
		da->remove(pck_path.get_file());
		Error err = da->rename(temp_path.get_file(), pck_path.get_file());
		if (err != OK) {
			error_string = "Error renaming PCK file: " + pck_path;
			return err;
		}
	}
	bl_print("PCK write took " + itos(OS::get_singleton()->get_ticks_msec() - start_time) + "ms");
	return OK;
}

void PckCreator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("pck_create", "pck_path", "dir", "include_filters", "exclude_filters"), &PckCreator::pck_create, DEFVAL(Vector<String>()), DEFVAL(Vector<String>()));
	ClassDB::bind_method(D_METHOD("reset"), &PckCreator::reset);
	ClassDB::bind_method(D_METHOD("start_pck", "pck_path", "pck_version", "ver_major", "ver_minor", "ver_rev", "encrypt", "embed", "exe_to_embed", "watermark"), &PckCreator::start_pck, DEFVAL(false), DEFVAL(false), DEFVAL(""), DEFVAL(""));
	ClassDB::bind_method(D_METHOD("add_files", "file_paths_to_pack"), &PckCreator::add_files);
	ClassDB::bind_method(D_METHOD("finish_pck"), &PckCreator::finish_pck);
	ClassDB::bind_method(D_METHOD("set_pack_version", "ver"), &PckCreator::set_pack_version);
	ClassDB::bind_method(D_METHOD("get_pack_version"), &PckCreator::get_pack_version);
	ClassDB::bind_method(D_METHOD("set_ver_major", "ver"), &PckCreator::set_ver_major);
	ClassDB::bind_method(D_METHOD("get_ver_major"), &PckCreator::get_ver_major);
	ClassDB::bind_method(D_METHOD("set_ver_minor", "ver"), &PckCreator::set_ver_minor);
	ClassDB::bind_method(D_METHOD("get_ver_minor"), &PckCreator::get_ver_minor);
	ClassDB::bind_method(D_METHOD("set_ver_rev", "ver"), &PckCreator::set_ver_rev);
	ClassDB::bind_method(D_METHOD("get_ver_rev"), &PckCreator::get_ver_rev);
	ClassDB::bind_method(D_METHOD("set_encrypt", "enc"), &PckCreator::set_encrypt);
	ClassDB::bind_method(D_METHOD("get_encrypt"), &PckCreator::get_encrypt);
	ClassDB::bind_method(D_METHOD("set_embed", "emb"), &PckCreator::set_embed);
	ClassDB::bind_method(D_METHOD("get_embed"), &PckCreator::get_embed);
	ClassDB::bind_method(D_METHOD("set_exe_to_embed", "exe"), &PckCreator::set_exe_to_embed);
	ClassDB::bind_method(D_METHOD("get_exe_to_embed"), &PckCreator::get_exe_to_embed);
	ClassDB::bind_method(D_METHOD("set_watermark", "watermark"), &PckCreator::set_watermark);
	ClassDB::bind_method(D_METHOD("get_watermark"), &PckCreator::get_watermark);
	ClassDB::bind_method(D_METHOD("get_error_message"), &PckCreator::get_error_message);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "pack_version"), "set_pack_version", "get_pack_version");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ver_major"), "set_ver_major", "get_ver_major");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ver_minor"), "set_ver_minor", "get_ver_minor");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ver_rev"), "set_ver_rev", "get_ver_rev");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "encrypt"), "set_encrypt", "get_encrypt");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "embed"), "set_embed", "get_embed");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "exe_to_embed"), "set_exe_to_embed", "get_exe_to_embed");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "watermark"), "set_watermark", "get_watermark");
	//ClassDB::bind_method(D_METHOD("get_dumped_files"), &PckCreator::get_dumped_files);
}
