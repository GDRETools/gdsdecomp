#include "gdre_logger.h"
#include "core/os/mutex.h"
#include "editor/gdre_editor.h"
#include "gdre_settings.h"

#include "core/io/dir_access.h"

bool inGuiMode() {
	//check if we are in GUI mode
	if (GDRESettings::get_singleton() && !GDRESettings::get_singleton()->is_headless() && GodotREEditor::get_singleton()) {
		return true;
	}
	return false;
}
thread_local uint64_t thread_warning_count = 0;
thread_local uint64_t thread_error_count = 0;
thread_local bool previous_was_error = false;

std::atomic<uint64_t> GDRELogger::error_count = 0;
std::atomic<uint64_t> GDRELogger::warning_count = 0;

void GDRELogger::logv(const char *p_format, va_list p_list, bool p_err) {
	if (disabled || !should_log(p_err)) {
		if (p_err) {
			error_count++;
			thread_error_count++;
		}
		return;
	}
	if (file.is_valid() || inGuiMode() || is_prebuffering) {
		const int static_buf_size = 512;
		char static_buf[static_buf_size];
		char *buf = static_buf;
		va_list list_copy;
		va_copy(list_copy, p_list);
		int len = vsnprintf(buf, static_buf_size, p_format, p_list);
		if (len >= static_buf_size) {
			buf = (char *)Memory::alloc_static(len + 1);
			vsnprintf(buf, len + 1, p_format, list_copy);
		}
		va_end(list_copy);
		if (p_err) {
			String str = String::utf8(buf, 8).strip_edges();
			// If it's the follow-up stacktrace line of an error, don't count it.
			if (!previous_was_error || !str.strip_edges().begins_with("at:")) {
				if (len >= 8 && str.length() == 8 && (String::utf8(buf, 8) == "WARNING:")) {
					warning_count++;
					thread_warning_count++;
				} else {
					error_count++;
					thread_error_count++;
				}
				previous_was_error = true;
			} else {
				previous_was_error = false;
			}
		} else {
			previous_was_error = false;
		}

		if (inGuiMode()) {
			// TODO: route this through GDRE Settings rather than GDRE Editor
			GodotREEditor::get_singleton()->call_deferred(SNAME("emit_signal"), "write_log_message", String(buf));
		}
		if (file.is_valid()) {
			file->store_buffer((uint8_t *)buf, len);

			if (p_err || _flush_stdout_on_print) {
				// Don't always flush when printing stdout to avoid performance
				// issues when `print()` is spammed in release builds.
				file->flush();
			}
		}
		if (is_prebuffering) {
			MutexLock lock(buffer_mutex);
			if (is_prebuffering) {
				buffer.push_back(String(buf));
			}
		}
		if (len >= static_buf_size) {
			Memory::free_static(buf);
		}
	}
}

Error GDRELogger::open_file(const String &p_base_path) {
	if (file.is_valid()) {
		return ERR_ALREADY_IN_USE;
	}
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_USERDATA);
	if (da.is_valid()) {
		da->make_dir_recursive(p_base_path.get_base_dir());
	}
	Error err;
	file = FileAccess::open(p_base_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(file.is_null(), err, "Failed to open log file " + p_base_path + " for writing.");
	base_path = p_base_path.simplify_path();
	{
		MutexLock lock(buffer_mutex);
		if (is_prebuffering) {
			for (int i = 0; i < buffer.size(); i++) {
				file->store_string(buffer[i]);
			}
			is_prebuffering = false;
			buffer.clear();
		}
	}

	return OK;
}

void GDRELogger::start_prebuffering() {
	is_prebuffering = true;
}

void GDRELogger::stop_prebuffering() {
	if (is_prebuffering) {
		MutexLock lock(buffer_mutex);
		is_prebuffering = false;
		buffer.clear();
	}
}

void GDRELogger::close_file() {
	if (file.is_valid()) {
		file->flush();
		file = Ref<FileAccess>();
		base_path = "";
	}
}

void GDRELogger::_disable() {
	disabled = true;
}

uint64_t GDRELogger::get_error_count() {
	return error_count;
}

uint64_t GDRELogger::get_thread_error_count() {
	return thread_error_count;
}

GDRELogger::GDRELogger() {
	GDRELogger::error_count = 0;
}

GDRELogger::~GDRELogger() {
	close_file();
}