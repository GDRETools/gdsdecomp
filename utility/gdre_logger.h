#pragma once

#include "core/io/logger.h"
#include "gd_parallel_queue.h"

class GDRESettings;
class GDRELogger : public Logger {
	friend class GDRESettings;
	Ref<FileAccess> file;
	String base_path;
	bool disabled = false;
	static std::atomic<uint64_t> warning_count;
	static std::atomic<uint64_t> error_count;
	static StaticParallelQueue<String, 1024> error_queue;
	static std::atomic<bool> silent_errors;
	bool is_prebuffering = false;
	Mutex buffer_mutex;
	Vector<String> buffer;
	static std::atomic<bool> just_printed_status_bar;
	static Logger *stdout_logger;
	static void set_stdout_logger(Logger *p_logger) { stdout_logger = p_logger; }

public:
	// print only to stdout, not to the file
	static void stdout_print(const char *p_format, ...);
	static void print_status_bar(const String &p_status, float p_progress, float p_indeterminate_progress = -1);
	String get_path() { return base_path; }
	GDRELogger();
	bool is_prebuffering_enabled() { return is_prebuffering; }
	void start_prebuffering();
	void stop_prebuffering();
	Error open_file(const String &p_base_path);
	void close_file();
	void _disable(); // only used for during cleanup, because we can't remove the logger
	virtual void logv(const char *p_format, va_list p_list, bool p_err) _PRINTF_FORMAT_ATTRIBUTE_2_0;
	static uint64_t get_error_count();
	static uint64_t get_thread_error_count();
	static Vector<String> get_errors();
	static Vector<String> get_thread_errors();
	static void clear_error_queues();
	// Silences errors, but still collects them in the error queue
	static void set_silent_errors(bool p_silent);
	static void set_thread_local_silent_errors(bool p_silent);
	static bool is_silencing_errors();
	static bool is_thread_local_silencing_errors();
	virtual ~GDRELogger();
};
