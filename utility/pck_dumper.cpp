#include "pck_dumper.h"
#include "core/error/error_list.h"
#include "gdre_settings.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "utility/common.h"
#include "utility/packed_file_info.h"

#include <editor/gdre_editor.h>

const static Vector<uint8_t> empty_md5 = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

bool PckDumper::_pck_file_check_md5(Ref<PackedFileInfo> &file) {
	// Loading an encrypted file automatically checks the md5
	if (file->is_encrypted()) {
		return true;
	}
	auto hash = FileAccess::get_md5(file->get_path());
	auto p_md5 = String::md5(file->get_md5().ptr());
	return hash == p_md5;
}

Error PckDumper::check_md5_all_files() {
	Vector<String> f;
	int ch = 0;
	if (!GDRESettings::get_singleton()->is_headless()) {
		EditorProgressGDDC pr{ GodotREEditorStandalone::get_singleton(), "pck_dump_to_dir", "Reading PCK archive, click cancel to skip MD5 checking...", static_cast<int>(GDRESettings::get_singleton()->get_file_count()), true };
		return _check_md5_all_files(f, ch, &pr);
	}

	return _check_md5_all_files(f, ch, nullptr);
}

void PckDumper::_do_md5_check(uint32_t i, Ref<PackedFileInfo> *tokens) {
	// Taken care of in the main thread
	if (unlikely(cancelled)) {
		return;
	}
	if (tokens[i]->get_md5() == empty_md5) {
		skipped_cnt++;
	} else {
		tokens[i]->set_md5_match(_pck_file_check_md5(tokens[i]));
		if (!tokens[i]->md5_passed) {
			print_error("Checksum failed for " + tokens[i]->get_path());
			broken_cnt++;
		}
	}
	last_completed++;
}

void PckDumper::reset() {
	cancelled = false;
	last_completed = -1;
	skipped_cnt = 0;
	broken_cnt = 0;
}

Error PckDumper::wait_for_task(WorkerThreadPool::GroupID group_task, const Vector<String> &paths_to_check, EditorProgressGDDC *pr) {
	if (pr) {
		int fl_sz = paths_to_check.size();
		while (!WorkerThreadPool::get_singleton()->is_group_task_completed(group_task)) {
			OS::get_singleton()->delay_usec(10000);
			int i = last_completed;
			if (i < 0) {
				i = 0;
			} else if (i >= fl_sz) {
				i = fl_sz - 1;
			}
			bool cancel = pr->step(paths_to_check[i], i, true);
			if (cancel) {
				cancelled = true;
				WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);
				return ERR_PRINTER_ON_FIRE;
			}
		}
	}

	// Always wait for completion; otherwise we leak memory.
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);
	return OK;
}

Error PckDumper::_check_md5_all_files(Vector<String> &broken_files, int &checked_files, EditorProgressGDDC *pr) {
	reset();
	auto ext = GDRESettings::get_singleton()->get_pack_type();
	uint64_t last_progress_upd = OS::get_singleton()->get_ticks_usec();

	if (ext != GDRESettings::PackInfo::PCK && ext != GDRESettings::PackInfo::EXE) {
		print_verbose("Not a pack file, skipping MD5 check...");
		return ERR_SKIP;
	}
	Error err = OK;
	auto files = GDRESettings::get_singleton()->get_file_info_list();
	int skipped_files = 0;
	if (opt_multi_thread) {
		Vector<String> paths_to_check;
		if (pr) {
			pr->step("Checking MD5 for all files...", 0, true);
			for (const auto &file : files) {
				paths_to_check.push_back(file->get_path());
			}
		}
		WorkerThreadPool::GroupID group_task = WorkerThreadPool::get_singleton()->add_template_group_task(
				this,
				&PckDumper::_do_md5_check,
				files.ptrw(),
				files.size(), -1, true, SNAME("PckDumper::_check_md5_all_files"));
		err = wait_for_task(group_task, paths_to_check, pr);
		checked_files = last_completed + 1 - skipped_cnt;
		skipped_files = skipped_cnt;
		if (broken_cnt > 0) {
			err = ERR_BUG;
			for (int i = 0; i < files.size(); i++) {
				if (files[i]->get_md5() != empty_md5 && !files[i]->md5_passed) {
					broken_files.push_back(files[i]->get_path());
				}
			}
		}
	} else {
		for (int i = 0; i < files.size(); i++) {
			if (pr) {
				if (OS::get_singleton()->get_ticks_usec() - last_progress_upd > 20000) {
					last_progress_upd = OS::get_singleton()->get_ticks_usec();
					bool cancel = pr->step(files[i]->path, i, true);
					if (cancel) {
						err = ERR_PRINTER_ON_FIRE;
					}
				}
			}
			if (files[i]->get_md5() == empty_md5) {
				print_verbose("Skipping MD5 check for " + files[i]->path + " (no MD5 hash found)");
				skipped_files++;
				continue;
			}
			files.write[i]->set_md5_match(_pck_file_check_md5(files.write[i]));
			if (files[i]->md5_passed) {
				print_verbose("Verified " + files[i]->path);
			} else {
				print_error("Checksum failed for " + files[i]->path);
				broken_files.push_back(files[i]->path);
				err = ERR_BUG;
			}
			checked_files++;
		}
	}
	if (err == ERR_PRINTER_ON_FIRE) {
		print_error("Verification cancelled!\n");
	} else if (err) {
		print_error("At least one error was detected while verifying files in pack!\n");
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
	if (!GDRESettings::get_singleton()->is_headless()) {
		EditorProgressGDDC pr{ GodotREEditorStandalone::get_singleton(), "pck_dump_to_dir", "Extracting files...",
			static_cast<int>(files_to_extract.is_empty() ? GDRESettings::get_singleton()->get_file_count() : files_to_extract.size()), true };
		return _pck_dump_to_dir(dir, files_to_extract, &pr, t);
	}
	return _pck_dump_to_dir(dir, files_to_extract, nullptr, t);
}

void PckDumper::_do_extract(uint32_t i, ExtractToken *tokens) {
	if (unlikely(cancelled)) {
		return;
	}
	auto &file = tokens[i].file;
	auto &dir = tokens[i].output_dir;
	Error err = OK;
	Ref<FileAccess> pck_f = FileAccess::open(file->get_path(), FileAccess::READ, &err);
	if (err || pck_f.is_null()) {
		broken_cnt++;
		last_completed++;
		tokens[i].err = ERR_FILE_CANT_OPEN;
		return;
	}
	String target_name = dir.path_join(file->get_path().replace("res://", ""));
	err = gdre::ensure_dir(target_name.get_base_dir());
	if (err != OK) {
		broken_cnt++;
		last_completed++;
		tokens[i].err = ERR_CANT_CREATE;
		return;
	}
	Ref<FileAccess> fa = FileAccess::open(target_name, FileAccess::WRITE, &err);
	if (err || fa.is_null()) {
		broken_cnt++;
		last_completed++;
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
	last_completed++;
	if (file->is_malformed() && file->get_raw_path() != file->get_path()) {
		print_line("Warning: " + file->get_raw_path() + " is a malformed path!\nSaving to " + file->get_path() + " instead.");
	}
	print_verbose("Extracted " + target_name);
}

Error PckDumper::_pck_dump_to_dir(
		const String &dir,
		const Vector<String> &files_to_extract,
		EditorProgressGDDC *pr,
		String &error_string) {
	ERR_FAIL_COND_V_MSG(!GDRESettings::get_singleton()->is_pack_loaded(), ERR_DOES_NOT_EXIST,
			"Pack not loaded!");
	reset();
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	auto files = GDRESettings::get_singleton()->get_file_info_list();
	Vector<uint8_t> key = GDRESettings::get_singleton()->get_encryption_key();
	uint64_t last_progress_upd = OS::get_singleton()->get_ticks_usec();

	if (DirAccess::create(DirAccess::ACCESS_FILESYSTEM).is_null()) {
		return ERR_FILE_CANT_WRITE;
	}
	int files_extracted = 0;
	Error err;
	if (opt_multi_thread) {
		Vector<ExtractToken> tokens;
		Vector<String> paths_to_extract;
		int actual = 0;
		HashSet<String> files_to_extract_set;
		for (const String &f : files_to_extract) {
			files_to_extract_set.insert(f);
		}
		for (int i = 0; i < files.size(); i++) {
			if (!files_to_extract_set.is_empty() && !files_to_extract_set.has(files.get(i)->get_path())) {
				continue;
			}
			actual++;
			if (pr) {
				paths_to_extract.push_back(files.get(i)->get_path());
			}
			tokens.push_back({ files.get(i), dir, OK });
		}
		tokens.resize(actual);
		paths_to_extract.resize(actual);
		WorkerThreadPool::GroupID group_task = WorkerThreadPool::get_singleton()->add_template_group_task(
				this,
				&PckDumper::_do_extract,
				tokens.ptrw(),
				tokens.size(), -1, true, SNAME("PckDumper::_pck_dump_to_dir"));
		err = wait_for_task(group_task, paths_to_extract, pr);
		files_extracted = last_completed + 1;
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
			}
		}
	} else {
		for (int i = 0; i < files.size(); i++) {
			if (files_to_extract.size() && !files_to_extract.has(files.get(i)->get_path())) {
				continue;
			}

			if (pr) {
				if (OS::get_singleton()->get_ticks_usec() - last_progress_upd > 20000) {
					last_progress_upd = OS::get_singleton()->get_ticks_usec();
					bool cancel = pr->step(files.get(i)->get_path(), i, true);
					if (cancel) {
						return ERR_PRINTER_ON_FIRE;
					}
				}
			}
			Ref<FileAccess> pck_f = FileAccess::open(files.get(i)->get_path(), FileAccess::READ, &err);
			if (pck_f.is_null()) {
				error_string += files.get(i)->get_path() + " (FileAccess error)\n";
				continue;
			}
			String target_name = dir.path_join(files.get(i)->get_path().replace("res://", ""));
			gdre::ensure_dir(target_name.get_base_dir());
			Ref<FileAccess> fa = FileAccess::open(target_name, FileAccess::WRITE);
			if (fa.is_null()) {
				error_string += files.get(i)->get_path() + " (FileWrite error)\n";
				continue;
			}

			int64_t rq_size = files.get(i)->get_size();
			uint8_t buf[16384];
			while (rq_size > 0) {
				int got = pck_f->get_buffer(buf, MIN(16384, rq_size));
				fa->store_buffer(buf, got);
				rq_size -= 16384;
			}
			fa->flush();
			files_extracted++;
			if (files.get(i)->is_malformed() && files.get(i)->get_raw_path() != files.get(i)->get_path()) {
				print_line("Warning: " + files.get(i)->get_raw_path() + " is a malformed path!\nSaving to " + files.get(i)->get_path() + " instead.");
			}
			print_verbose("Extracted " + target_name);
		}
	}

	if (error_string.length() > 0) {
		print_error("At least one error was detected while extracting pack!\n" + error_string);
		//show_warning(failed_files, RTR("Read PCK"), RTR("At least one error was detected!"));
	} else {
		print_line("Extracted " + itos(files_extracted) + " files, no errors detected!");
		//show_warning(RTR("No errors detected."), RTR("Read PCK"), RTR("The operation completed successfully!"));
	}
	return OK;
}

void PckDumper::_bind_methods() {
	ClassDB::bind_method(D_METHOD("check_md5_all_files"), &PckDumper::check_md5_all_files);
	ClassDB::bind_method(D_METHOD("pck_dump_to_dir", "dir", "files_to_extract"), &PckDumper::pck_dump_to_dir, DEFVAL(Vector<String>()));
	ClassDB::bind_method(D_METHOD("set_multi_thread", "multi_thread"), &PckDumper::set_multi_thread);
	//ClassDB::bind_method(D_METHOD("get_dumped_files"), &PckDumper::get_dumped_files);
}
