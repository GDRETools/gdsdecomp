#include "gdre_logger.h"
#include "core/os/mutex.h"
#include "gdre_settings.h"
#include "gui/gdre_standalone.h"

#include "core/io/dir_access.h"

bool inGuiMode() {
	//check if we are in GUI mode
	if (GDRESettings::get_singleton() && !GDRESettings::get_singleton()->is_headless() && GodotREEditorStandalone::get_singleton()) {
		return true;
	}
	return false;
}
thread_local uint64_t thread_warning_count = 0;
thread_local uint64_t thread_error_count = 0;
thread_local bool previous_was_error = false;
thread_local Vector<String> thread_error_queue;
thread_local bool thread_local_silent_errors = false;

std::atomic<uint64_t> GDRELogger::error_count = 0;
std::atomic<uint64_t> GDRELogger::warning_count = 0;
std::atomic<bool> GDRELogger::silent_errors = false;
StaticParallelQueue<String, 1024> GDRELogger::error_queue;
Logger *GDRELogger::stdout_logger = nullptr;
std::atomic<bool> GDRELogger::just_printed_status_bar = false;
static constexpr const char *STATUS_BAR_CLEAR = "\r                                                                      \r";

void GDRELogger::logv(const char *p_format, va_list p_list, bool p_err) {
	if (disabled || !should_log(p_err)) {
		if (p_err) {
			error_count++;
			thread_error_count++;
		}
		return;
	}
	if (just_printed_status_bar.exchange(false) && (!(thread_local_silent_errors || silent_errors) || !p_err)) {
		stdout_print(STATUS_BAR_CLEAR);
	}
	const int static_buf_size = 512;
	char static_buf[static_buf_size];
	char *buf = static_buf;
	va_list list_copy;
	va_copy(list_copy, p_list);
	int len = vsnprintf(buf, static_buf_size, p_format, list_copy);
	if (len >= static_buf_size) {
		buf = (char *)Memory::alloc_static(len + 1);
		vsnprintf(buf, len + 1, p_format, list_copy);
	}
	va_end(list_copy);

	bool is_gdscript_backtrace = false;
	bool is_stacktrace = false;
	String str = String::utf8(buf);
	if (p_err) {
		String lstripped = str.strip_edges(true, false);
		is_gdscript_backtrace = lstripped.begins_with("GDScript backtrace");
		// If it's the follow-up stacktrace line of an error, don't count it.
		is_stacktrace = lstripped.begins_with("at:");
		if (!is_stacktrace && !is_gdscript_backtrace) {
			if (len >= 8 && lstripped.begins_with("WARNING:")) {
				warning_count++;
				thread_warning_count++;
			} else {
				error_count++;
				thread_error_count++;
			}
			previous_was_error = true;
		} else if (is_stacktrace) {
			previous_was_error = false;
		}
		error_queue.try_push(str); // Ignore if the queue is full
		thread_error_queue.push_back(str);
	} else {
		previous_was_error = false;
	}

	if (p_err && (is_thread_local_silencing_errors() || is_silencing_errors())) {
		return;
	}

	if (inGuiMode() && !is_gdscript_backtrace) {
		GodotREEditorStandalone::get_singleton()->call_deferred(SNAME("write_log_message"), str);
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
			buffer.push_back(str);
		}
	}
	if (len >= static_buf_size) {
		Memory::free_static(buf);
	}
	stdout_logger->logv(p_format, p_list, p_err);
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

Vector<String> GDRELogger::get_errors() {
	Vector<String> errors;
	String tmp;
	while (error_queue.try_pop(tmp)) {
		errors.push_back(tmp);
	}
	thread_error_queue.clear();
	return errors;
}

Vector<String> GDRELogger::get_thread_errors() {
	Vector<String> errors = thread_error_queue;
	thread_error_queue.clear();
	return errors;
}

void GDRELogger::clear_error_queues() {
	String tmp;
	while (error_queue.try_pop(tmp)) {
	}
	thread_error_queue.clear();
}

GDRELogger::GDRELogger() {
	GDRELogger::error_count = 0;
}

GDRELogger::~GDRELogger() {
	close_file();
}

void GDRELogger::stdout_print(const char *p_format, ...) {
	if (!stdout_logger) {
		return;
	}
	va_list args;
	va_start(args, p_format);
	stdout_logger->logv(p_format, args, false);
	va_end(args);
	just_printed_status_bar = false;
}

void GDRELogger::print_status_bar(const String &p_status, float p_progress, float p_indeterminate_progress) {
	constexpr size_t width = 30;
	size_t progress_width = MIN(width, width * (p_indeterminate_progress != -1 ? p_indeterminate_progress : p_progress));

	char progress_bar[width + 1];
	for (size_t i = 0; i < progress_width; i++) {
		if (p_indeterminate_progress != -1 && i != progress_width - 1) {
			// all spaces except the last one
			progress_bar[i] = ' ';
		} else {
			progress_bar[i] = '=';
		}
	}
	for (size_t i = progress_width; i < width; i++) {
		progress_bar[i] = ' ';
	}
	progress_bar[width] = '\0';
	stdout_print("\r%s [%s] %d%%", p_status.utf8().get_data(), progress_bar, (int)(p_progress * 100));
	just_printed_status_bar = true;
}

void GDRELogger::set_silent_errors(bool p_silent) {
	silent_errors = p_silent;
}

bool GDRELogger::is_silencing_errors() {
	return silent_errors;
}

void GDRELogger::set_thread_local_silent_errors(bool p_silent) {
	thread_local_silent_errors = p_silent;
}

bool GDRELogger::is_thread_local_silencing_errors() {
	return thread_local_silent_errors;
}
