#pragma once

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "utility/task_manager.h"

class ProcessRunner : public RefCounted {
	GDCLASS(ProcessRunner, RefCounted);

	struct ProcessRunnerStruct : public TaskRunnerStruct {
		String command;
		Vector<String> arguments;
		bool quiet = false;
		int error_code = -1;
		String error_message;
		ProcessID process_id = -1;
		String step_description;
		bool is_cancelled = false;
		Ref<FileAccess> fa_stdout;
		Ref<FileAccess> fa_stderr;
		String output;
		bool done = false;

		ProcessRunnerStruct() = default;
		ProcessRunnerStruct(const String &p_command, const Vector<String> &p_arguments, bool p_quiet);
		virtual ~ProcessRunnerStruct();
		void set_command(const String &p_command, const Vector<String> &p_arguments);
		bool pre_run() override;
		int get_current_task_step_value() override;
		String get_current_task_step_description() override;
		void cancel() override;
		void run(void *p_userdata) override;
		bool auto_close_progress_bar() override;
		void after_run();
		void close_files();
	};

	bool no_progress = false;
	String description;
	TaskManager::TaskManagerID task_id = -1;
	std::shared_ptr<ProcessRunnerStruct> runner_struct;
	Error ret_error = OK;

protected:
	ProcessRunner() = default;

public:
	~ProcessRunner();

	static Ref<ProcessRunner> start_process(const String &p_command, const Vector<String> &p_arguments, const String &p_description = "", bool quiet = false);
	static Error run_process(const String &p_command, const Vector<String> &p_arguments, const String &p_description = "", bool quiet = false, String *p_output = nullptr);
	Error wait_for_completion();
	bool is_finished() const;
	void cancel();
	String get_output() const;
	int get_error_code() const;
};
