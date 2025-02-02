#include "pck_creator.h"

#include "core/error/error_list.h"
#include "gdre_packed_source.h"
#include "gdre_settings.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "utility/common.h"
#include "utility/packed_file_info.h"

#include <core/io/file_access_encrypted.h>
#include <editor/gdre_editor.h>
#include <scene/resources/surface_tool.h>

void PckCreator::reset() {
	files_to_pck.clear();
	offset = 0;
	error_string.clear();
	cancelled = false;
	last_completed = -1;
	skipped_cnt = 0;
	broken_cnt = 0;
	data_read = 0;
}

Error PckCreator::wait_for_task(WorkerThreadPool::GroupID group_task, const Vector<String> &paths_to_check, EditorProgressGDDC *pr) {
	if (pr) {
		int64_t fl_sz = paths_to_check.size();
		while (!WorkerThreadPool::get_singleton()->is_group_task_completed(group_task)) {
			OS::get_singleton()->delay_usec(10000);
			int64_t i = last_completed;
			if (i < 0) {
				i = 0;
			} else if (i >= fl_sz) {
				i = fl_sz - 1;
			}
			bool cancel = pr->step(paths_to_check[i], i, true);
			if (cancel) {
				cancelled = true;
				WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);
				return ERR_SKIP;
			}
		}
	}

	// Always wait for completion; otherwise we leak memory.
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);
	return OK;
}

Vector<String> PckCreator::get_files_to_pack(const String &p_dir, const Vector<String> &include_filters, const Vector<String> &p_exclude_filters) {
	Vector<String> excludes = p_exclude_filters;
	excludes.append("*/thumbs.db");
	excludes.append("thumbs.db");

	excludes.append(".DS_Store");
	excludes.append("*/.DS_Store");

	auto file_paths_to_pack = gdre::get_recursive_dir_list(p_dir, include_filters, false);
	// auto exclude_filters = p_exclude_filters;
	static Vector<String> banned_files = { "thumbs.db", ".DS_Store" };

	for (int64_t i = file_paths_to_pack.size() - 1; i >= 0; i--) {
		bool banned = false;
		for (int64_t j = 0; j < banned_files.size(); j++) {
			if (file_paths_to_pack[i].ends_with(banned_files[j])) {
				file_paths_to_pack.remove_at(i);
				banned = true;
				break;
			}
		}
		if (banned) {
			continue;
		}
		// we have to check the exclude filters now
		for (int64_t j = 0; j < p_exclude_filters.size(); j++) {
			if (file_paths_to_pack[i].matchn(p_exclude_filters[j])) {
				file_paths_to_pack.remove_at(i);
				break;
			}
		}
	}
	return file_paths_to_pack;
}

#ifdef DEBUG_ENABLED
#define bl_print(...) print_line(__VA_ARGS__)
#else
#define bl_print(...) print_verbose(__VA_ARGS__)
#endif

Error PckCreator::pck_create(const String &p_pck_path, const String &p_dir, const Vector<String> &include_filters, const Vector<String> &exclude_filters) {
	uint64_t start_time = OS::get_singleton()->get_ticks_msec();
	if (GDRESettings::get_singleton() && !GDRESettings::get_singleton()->is_headless()) {
		Vector<String> file_paths_to_pack;
		{
			EditorProgressGDDC pr{ GodotREEditorStandalone::get_singleton(), "re_read_folder", "Reading folder structure...", -1, true };

			file_paths_to_pack = get_files_to_pack(p_dir, include_filters, exclude_filters);
			bl_print("PCK get_files_to_pack took " + itos(OS::get_singleton()->get_ticks_msec() - start_time) + "ms");
		}
		Error err = OK;
		{
			EditorProgressGDDC pr{ GodotREEditorStandalone::get_singleton(), "re_folder_info", "Getting file info...", (int)file_paths_to_pack.size(), true };
			err = _process_folder(p_pck_path, p_dir, file_paths_to_pack, &pr, error_string);
		}
		ERR_FAIL_COND_V_MSG(err, err, "Error processing folder: " + error_string);
		{
			EditorProgressGDDC pr{ GodotREEditorStandalone::get_singleton(), "re_write_pck", "Writing PCK archive...", (int)file_paths_to_pack.size(), true };
			err = _create_after_process(&pr, error_string);
		}
		ERR_FAIL_COND_V_MSG(err, err, "Error creating pck: " + error_string);
		return OK;
	}
	auto file_paths_to_pack = get_files_to_pack(p_dir, include_filters, exclude_filters);
	bl_print("PCK get_files_to_pack took " + itos(OS::get_singleton()->get_ticks_msec() - start_time) + "ms");
	Error err = _process_folder(p_pck_path, p_dir, file_paths_to_pack, nullptr, error_string);
	ERR_FAIL_COND_V_MSG(err, err, "Error processing folder: " + error_string);
	err = _create_after_process(nullptr, error_string);
	ERR_FAIL_COND_V_MSG(err, err, "Error creating pck: " + error_string);
	uint64_t end_time = OS::get_singleton()->get_ticks_msec();
	print_line("PCK creation took " + itos(end_time - start_time) + "ms");

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
		++last_completed;
		++broken_cnt;
		return;
	}
	{
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
		if (file.is_null()) {
			token.err = ERR_FILE_CANT_OPEN;
			++last_completed;
			++broken_cnt;
			return;
		}
		token.size = file->get_length();
	}
	if (token.size == 0) {
		token.md5.resize_zeroed(16);
		++last_completed;
		return;
	}
	auto md5_str = FileAccess::get_md5(path);
	if (md5_str.is_empty()) {
		token.err = ERR_FILE_CANT_OPEN;
		++last_completed;
		++broken_cnt;
		return;
	}
	token.md5 = md5_str.hex_decode();

	++last_completed;
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
		const Vector<String> &file_paths_to_pack,
		EditorProgressGDDC *pr, String &p_error_string) {
	HashMap<String, String> map;
	for (size_t i = 0; i < file_paths_to_pack.size(); i++) {
		map[p_dir.path_join(file_paths_to_pack[i])] = file_paths_to_pack[i];
	}
	start_pck(p_pck_path, version, ver_major, ver_minor, ver_rev, encrypt, embed, exe_to_embed, watermark);
	auto ret = _add_files(map, pr);
	p_error_string = error_string;
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
	if (GDRESettings::get_singleton() && !GDRESettings::get_singleton()->is_headless()) {
		EditorProgressGDDC pr{ GodotREEditorStandalone::get_singleton(), "re_add_files", "Adding files to PCK archive...", (int)file_paths_to_pack.size(), true };
		return _add_files(map, &pr);
	} else {
		return _add_files(map, nullptr);
	}
}

Error PckCreator::_add_files(
		const HashMap<String, String> &file_paths_to_pack,
		EditorProgressGDDC *pr) {
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
	if (opt_multi_thread) {
		WorkerThreadPool::GroupID group_task = WorkerThreadPool::get_singleton()->add_template_group_task(
				this,
				&PckCreator::_do_process_folder,
				files_to_pck.ptrw(),
				files_to_pck.size(), -1, true, SNAME("PckCreator::_do_process_folder"));
		err = wait_for_task(group_task, keys, pr);
		if (err == ERR_SKIP) {
			return ERR_SKIP;
		}
	} else {
		for (size_t i = 0; i < files_to_pck.size(); i++) {
			_do_process_folder(i, files_to_pck.ptrw());
		}
	}
	if (broken_cnt > 0) {
		err = ERR_BUG;
		for (size_t i = 0; i < files_to_pck.size(); i++) {
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
	for (size_t i = 0; i < files_to_pck.size(); i++) {
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
	Error error;
	if (GDRESettings::get_singleton() && !GDRESettings::get_singleton()->is_headless()) {
		EditorProgressGDDC pr{ GodotREEditorStandalone::get_singleton(), "re_write_pck", "Writing PCK archive...", (int)files_to_pck.size(), true };
		error = _create_after_process(&pr, error_string);
	} else {
		error = _create_after_process(nullptr, error_string);
	}
	ERR_FAIL_COND_V_MSG(error && error != ERR_SKIP && error != ERR_PRINTER_ON_FIRE, error, "Error creating pck: " + error_string);
	return error;
}

Error PckCreator::_create_after_process(EditorProgressGDDC *pr, String &error_string) {
	cancelled = false;
	last_completed = -1;
	skipped_cnt = 0;
	broken_cnt = 0;
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

	Ref<FileAccess> f = FileAccess::open(temp_path, FileAccess::WRITE);
	if (f.is_null()) {
		error_string = ("Error opening PCK file: ") + temp_path;
		return ERR_FILE_CANT_WRITE;
	}
	int64_t embedded_start = 0;
	int64_t embedded_size = 0;

	GDREPackedSource::EXEPCKInfo exe_pck_info;
	int64_t absolute_exe_end = 0;
	bool has_pck_already = false;
	if (embed) {
		// append to exe

		Ref<FileAccess> fs = FileAccess::open(exe_to_embed, FileAccess::READ);
		if (fs.is_null()) {
			error_string = ("Error opening executable file: ") + exe_to_embed;
			return ERR_FILE_CANT_READ;
		}
		if (pr) {
			pr->step("Exec...", 0, true);
		}

		fs->seek_end();
		absolute_exe_end = fs->get_position();
		int64_t exe_end;
		if (GDREPackedSource::get_exe_embedded_pck_info(exe_to_embed, exe_pck_info)) {
			has_pck_already = true;
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
	int64_t pck_start_pos = f->get_position();

	auto key = GDRESettings::get_singleton()->get_encryption_key();

	f->store_32(0x43504447); //GDPK
	f->store_32(version);
	f->store_32(ver_major);
	f->store_32(ver_minor);
	f->store_32(ver_rev);

	int64_t file_base_ofs = 0;
	if (version == 2) {
		uint32_t pack_flags = 0;
		if (encrypt) {
			pack_flags |= (1 << 0);
		}
		f->store_32(pack_flags); // flags
		file_base_ofs = f->get_position();
		f->store_64(0); // files base
	}

	for (size_t i = 0; i < 16; i++) {
		//reserved
		f->store_32(0);
	}
	if (pr) {
		pr->step("Header...", 0, true);
	}
	f->store_32(files_to_pck.size()); //amount of files
	// used by pck version 0-1, where the file offsets include the header size; pck version 2 uses the offset from the header
	size_t header_size = f->get_position();
	size_t predir_size = header_size;

	Ref<FileAccessEncrypted> fae;
	Ref<FileAccess> fhead = f;
	if (version == 2) {
		if (encrypt) {
			fae.instantiate();
			ERR_FAIL_COND_V(fae.is_null(), ERR_BUG);

			Error err = fae->open_and_parse(f, key, FileAccessEncrypted::MODE_WRITE_AES256, false);
			ERR_FAIL_COND_V(err != OK, err);

			fhead = fae;
		}
	}

	bool add_res_prefix = !(ver_major == 4 && ver_minor >= 4);

	for (size_t i = 0; i < files_to_pck.size(); i++) {
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
		if (version >= 2) {
			header_size += 4; // flags
		}
	}
	if (encrypt && version >= 2) {
		header_size += get_encryption_padding(header_size - predir_size);
	}

	size_t header_padding = _get_pad(PCK_PADDING, header_size);
	if (pr) {
		pr->step("Directory...", 0, true);
	}
	for (size_t i = 0; i < files_to_pck.size(); i++) {
		uint32_t string_len = files_to_pck[i].path.utf8().length();
		uint32_t pad = _get_pad(4, string_len);

		fhead->store_32(uint32_t(string_len + pad));
		fhead->store_buffer((const uint8_t *)files_to_pck[i].path.utf8().get_data(), string_len);
		for (uint32_t j = 0; j < pad; j++) {
			fhead->store_8(0);
		}

		if (version == 2) {
			fhead->store_64(files_to_pck[i].ofs);
		} else {
			fhead->store_64(files_to_pck[i].ofs + header_padding + header_size);
		}
		fhead->store_64(files_to_pck[i].size); // pay attention here, this is where file is
		fhead->store_buffer(files_to_pck[i].md5.ptr(), 16); //also save md5 for file
		if (version == 2) {
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

	int64_t file_base = f->get_position();
	DEV_ASSERT(file_base == header_size + header_padding);
	if (version == 2) {
		f->seek(file_base_ofs);
		f->store_64(file_base); // update files base
		f->seek(file_base);
	}

	uint64_t last_progress_upd = 0;
	for (size_t i = 0; i < files_to_pck.size(); i++) {
		if (pr && OS::get_singleton()->get_ticks_usec() - last_progress_upd > 200000) {
			last_progress_upd = OS::get_singleton()->get_ticks_usec();
			if (pr->step(files_to_pck[i].path, i + 2, false)) {
				cancelled = true;
				break;
			}
		}
		DEV_ASSERT(f->get_position() == file_base + files_to_pck[i].ofs);

		Ref<FileAccess> ftmp = f;
		if (encrypt) {
			fae.instantiate();
			ERR_FAIL_COND_V(fae.is_null(), ERR_BUG);

			Error err = fae->open_and_parse(f, key, FileAccessEncrypted::MODE_WRITE_AES256, false);
			ERR_FAIL_COND_V(err != OK, err);
			ftmp = fae;
		}
		if (read_and_write_file(i, ftmp) != OK) {
			error_string += files_to_pck[i].path + " (FileAccess error)\n";
			broken_cnt++;
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
		if (pr)
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
			} else if ((exe_pck_info.type == GDREPackedSource::EXEPCKInfo::ELF)) {
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
	ClassDB::bind_method(D_METHOD("set_multi_thread", "multi_thread"), &PckCreator::set_multi_thread);
	ClassDB::bind_method(D_METHOD("get_multi_thread"), &PckCreator::get_multi_thread);
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

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "multi_thread"), "set_multi_thread", "get_multi_thread");
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
