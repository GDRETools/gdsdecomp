#include "pck_dumper.h"
#include "core/error/error_list.h"
#include "gdre_settings.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "utility/common.h"
#include "utility/packed_file_info.h"

#include <utility/gdre_standalone.h>
#include <utility/task_manager.h>

const static Vector<uint8_t> empty_md5 = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

bool PckDumper::_pck_file_check_md5(Ref<PackedFileInfo> &file) {
	auto hash = FileAccess::get_md5(file->get_path());
	auto p_md5 = String::md5(file->get_md5().ptr());
	bool ret = hash == p_md5;
	if (!ret && file->is_encrypted()) {
		encryption_error = true;
	}
	return ret;
}

Error PckDumper::check_md5_all_files() {
	Vector<String> f;
	int ch = 0;
	return _check_md5_all_files(f, ch);
}

void PckDumper::_do_md5_check(uint32_t i, Ref<PackedFileInfo> *tokens) {
	// Taken care of in the main thread
	// if (unlikely(cancelled)) {
	// 	return;
	// }
	if (tokens[i]->get_md5() == empty_md5) {
		skipped_cnt++;
	} else {
		tokens[i]->set_md5_match(_pck_file_check_md5(tokens[i]));
		if (!tokens[i]->md5_passed) {
			print_error("Checksum failed for " + tokens[i]->get_path());
			broken_cnt++;
		}
	}
	completed_cnt++;
}

void PckDumper::reset() {
	completed_cnt = 0;
	skipped_cnt = 0;
	broken_cnt = 0;
}

Error PckDumper::_check_md5_all_files(Vector<String> &broken_files, int &checked_files) {
	reset();
	auto ext = GDRESettings::get_singleton()->get_pack_type();
	// uint64_t last_progress_upd = OS::get_singleton()->get_ticks_usec();

	if (ext != GDRESettings::PackInfo::PCK && ext != GDRESettings::PackInfo::EXE) {
		print_verbose("Not a pack file, skipping MD5 check...");
		return ERR_SKIP;
	}
	Error err = OK;
	auto files = GDRESettings::get_singleton()->get_file_info_list();
	int skipped_files = 0;
	String task_desc;
	if (GDRESettings::get_singleton()->is_headless()) {
		task_desc = RTR("Reading PCK archive...");
	} else {
		task_desc = RTR("Reading PCK archive, click cancel to skip MD5 checking...");
	}
	err = TaskManager::get_singleton()->run_multithreaded_group_task(
			this,
			&PckDumper::_do_md5_check,
			files.ptrw(),
			files.size(),
			&PckDumper::get_file_description,
			"PckDumper::_check_md5_all_files",
			task_desc, true, -1, true);
	if (encryption_error) {
		GDRESettings::get_singleton()->_set_error_encryption(encryption_error);
	}
	checked_files = completed_cnt - skipped_cnt;
	skipped_files = skipped_cnt;
	if (broken_cnt > 0) {
		err = ERR_BUG;
		for (int i = 0; i < files.size(); i++) {
			if (files[i]->get_md5() != empty_md5 && !files[i]->md5_passed) {
				print_error("Checksum failed for " + files[i]->path);
				broken_files.push_back(files[i]->get_path());
			}
		}
	}

	if (err == ERR_SKIP) {
		print_error("Verification cancelled!\n");
	} else if (err) {
		print_error("At least one error was detected while verifying files in pack!\n");
		if (encryption_error) {
			print_error("Encryption error detected, please check your encryption key and try again.\n");
		}
		//show_warning(failed_files, RTR("Read PCK"), RTR("At least one error was detected!"));
	} else if (skipped_files > 0) {
		print_line("Verified " + itos(checked_files) + " files, " + itos(skipped_files) + " files skipped (MD5 hash entry was empty)");
		if (skipped_files == files.size()) {
			return ERR_SKIP;
		}
	} else {
		print_line("Verified " + itos(checked_files) + " files, no errors detected!");
		//show_warning(RTR("No errors detected."), RTR("Read PCK"), RTR("The operation completed successfully!"));
	}
	return err;
}
Error PckDumper::pck_dump_to_dir(const String &dir, const Vector<String> &files_to_extract = Vector<String>()) {
	String t;
	return _pck_dump_to_dir(dir, files_to_extract, t);
}

void PckDumper::_do_extract(uint32_t i, ExtractToken *tokens) {
	auto &file = tokens[i].file;
	auto &dir = tokens[i].output_dir;
	Error err = OK;
	Ref<FileAccess> pck_f = FileAccess::open(file->get_path(), FileAccess::READ, &err);
	if (err || pck_f.is_null()) {
		broken_cnt++;
		completed_cnt++;
		tokens[i].err = ERR_FILE_CANT_OPEN;
		return;
	}
	String target_name = dir.path_join(file->get_path().replace("res://", ""));
	err = gdre::ensure_dir(target_name.get_base_dir());
	if (err != OK) {
		broken_cnt++;
		completed_cnt++;
		tokens[i].err = ERR_CANT_CREATE;
		return;
	}
	Ref<FileAccess> fa = FileAccess::open(target_name, FileAccess::WRITE, &err);
	if (err || fa.is_null()) {
		broken_cnt++;
		completed_cnt++;
		tokens[i].err = ERR_FILE_CANT_WRITE;
		return;
	}

	int64_t rq_size = file->get_size();
	uint8_t buf[16384];
	while (rq_size > 0) {
		int got = pck_f->get_buffer(buf, MIN(16384, rq_size));
		fa->store_buffer(buf, got);
		rq_size -= 16384;
	}
	fa->flush();
	completed_cnt++;
	if (file->is_malformed() && file->get_raw_path() != file->get_path()) {
		print_line("Warning: " + file->get_raw_path() + " is a malformed path!\nSaving to " + file->get_path() + " instead.");
	}
	print_verbose("Extracted " + target_name);
}

Error PckDumper::_pck_dump_to_dir(
		const String &dir,
		const Vector<String> &files_to_extract,
		String &error_string) {
	ERR_FAIL_COND_V_MSG(!GDRESettings::get_singleton()->is_pack_loaded(), ERR_DOES_NOT_EXIST,
			"Pack not loaded!");
	reset();
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	auto files = GDRESettings::get_singleton()->get_file_info_list();

	if (DirAccess::create(DirAccess::ACCESS_FILESYSTEM).is_null()) {
		return ERR_FILE_CANT_WRITE;
	}
	int files_extracted = 0;
	Error err = OK;
	Vector<ExtractToken> tokens;
	Vector<String> paths_to_extract;
	int actual = 0;
	HashSet<String> files_to_extract_set = gdre::vector_to_hashset(files_to_extract);
	for (int i = 0; i < files.size(); i++) {
		if (!files_to_extract_set.is_empty() && !files_to_extract_set.has(files.get(i)->get_path())) {
			continue;
		}
		actual++;
		tokens.push_back({ files.get(i), dir, OK });
	}
	tokens.resize(actual);

	err = TaskManager::get_singleton()->run_multithreaded_group_task(
			this,
			&PckDumper::_do_extract,
			tokens.ptrw(),
			tokens.size(),
			&PckDumper::get_extract_token_description,
			"PckDumper::_pck_dump_to_dir",
			RTR("Extracting files..."),
			true,
			-1,
			true);
	files_extracted = completed_cnt;
	if (broken_cnt > 0) {
		err = ERR_BUG;
		for (int i = 0; i < tokens.size(); i++) {
			if (tokens[i].err != OK) {
				String err_type;
				if (tokens[i].err == ERR_FILE_CANT_OPEN) {
					err_type = "FileAccess error";
				} else if (tokens[i].err == ERR_CANT_CREATE) {
					err_type = "FileCreate error";
				} else if (tokens[i].err == ERR_FILE_CANT_WRITE) {
					err_type = "FileWrite error";
				} else {
					err_type = "Unknown error";
				}
				error_string += tokens[i].file->get_path() + "(" + err_type + ")\n";
			}
			if (files.get(i)->is_malformed() && files.get(i)->get_raw_path() != files.get(i)->get_path()) {
				print_line("Warning: " + files.get(i)->get_raw_path() + " is a malformed path!\nSaving to " + files.get(i)->get_path() + " instead.");
			}
		}
	}

	if (error_string.length() > 0) {
		print_error("At least one error was detected while extracting pack!\n" + error_string);
		//show_warning(failed_files, RTR("Read PCK"), RTR("At least one error was detected!"));
	} else {
		print_line("Extracted " + itos(files_extracted) + " files, no errors detected!");
		//show_warning(RTR("No errors detected."), RTR("Read PCK"), RTR("The operation completed successfully!"));
	}
	return err;
}

String PckDumper::get_file_description(int64_t p_index, Ref<PackedFileInfo> *p_userdata) {
	if (p_index < 0) {
		return "Extracting files...";
	}
	return p_userdata[p_index]->get_path();
}

String PckDumper::get_extract_token_description(int64_t p_index, ExtractToken *p_userdata) {
	return p_userdata[p_index].file->get_path();
}

void PckDumper::_bind_methods() {
	ClassDB::bind_method(D_METHOD("check_md5_all_files"), &PckDumper::check_md5_all_files);
	ClassDB::bind_method(D_METHOD("pck_dump_to_dir", "dir", "files_to_extract"), &PckDumper::pck_dump_to_dir, DEFVAL(Vector<String>()));
	//ClassDB::bind_method(D_METHOD("get_dumped_files"), &PckDumper::get_dumped_files);
}
