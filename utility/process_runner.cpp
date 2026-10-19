#include "process_runner.h"
#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "main/gdre_main_loop.h"
#include "utility/task_manager.h"

ProcessRunner::ProcessRunnerStruct::ProcessRunnerStruct(const String &p_command, const Vector<String> &p_arguments, bool p_quiet) :
		command(p_command), arguments(p_arguments), quiet(p_quiet) {
	step_description = "Running " + command + " " + String(" ").join(arguments);
}

void ProcessRunner::ProcessRunnerStruct::set_command(const String &p_command, const Vector<String> &p_arguments) {
	command = p_command;
	arguments = p_arguments;
	step_description = "Running " + command + " " + String(" ").join(arguments);
}

int ProcessRunner::ProcessRunnerStruct::get_current_task_step_value() {
	return 0;
}

String ProcessRunner::ProcessRunnerStruct::get_current_task_step_description() {
	return step_description;
}

void ProcessRunner::ProcessRunnerStruct::cancel() {
	if (!is_cancelled && process_id != -1) {
		OS::get_singleton()->kill(process_id);
	}
	is_cancelled = true;
}

// We have to start the process on the main thread because of inscrutable WINPROC stuff.
bool ProcessRunner::ProcessRunnerStruct::pre_run() {
	List<String> args;
	for (const String &arg : arguments) {
		args.push_back(arg);
	}
	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(command, args, false);
	if (pipe_info.is_empty()) {
		error_code = -2;
		after_run();
		return false;
	}
	fa_stdout = pipe_info["stdio"];
	fa_stderr = pipe_info["stderr"];
	process_id = pipe_info["pid"];
	return true;
}

void ProcessRunner::ProcessRunnerStruct::run(void *p_userdata) {
	if (process_id == -1) {
		error_code = -3;
		after_run();
		return;
	}

	while (OS::get_singleton()->is_process_running(process_id) && !is_cancelled) {
		// we have to do continually read from the pipes or the process will hang
		output += fa_stdout->get_as_text() + fa_stderr->get_as_text();
		GDREMainLoop::wait_until_next_frame(10000);
	}
	error_code = OS::get_singleton()->get_process_exit_code(process_id);
	process_id = -1;
	after_run();
}

bool ProcessRunner::ProcessRunnerStruct::auto_close_progress_bar() {
	return true;
}

void ProcessRunner::ProcessRunnerStruct::after_run() {
	close_files();
	if (error_code != 0) {
		if (error_code == -3) {
			output = "Process not started???\n" + output;
		} else if (error_code == -2) {
			output = "execute_with_pipe failed\n" + output;
		}
		error_message = vformat("Failed to run process %s (error code: %d):\n%s", command, error_code, output);
		if (!quiet) {
			ERR_PRINT(error_message);
		}
	} else {
		error_message = vformat("Successfully ran %s %s", command, String(" ").join(arguments));
		print_line(error_message);
	}
}

void ProcessRunner::ProcessRunnerStruct::close_files() {
	if (fa_stdout.is_valid()) {
		fa_stdout->close();
	}
	if (fa_stderr.is_valid()) {
		fa_stderr->close();
	}
}

ProcessRunner::ProcessRunnerStruct::~ProcessRunnerStruct() {
	close_files();
}

Ref<ProcessRunner> ProcessRunner::start_process(const String &p_command, const Vector<String> &p_arguments, const String &p_description, bool p_quiet) {
	Ref<ProcessRunner> runner = memnew(ProcessRunner);
	runner->runner_struct = std::make_shared<ProcessRunnerStruct>(p_command, p_arguments, p_quiet);
	runner->no_progress = p_quiet;
	runner->description = p_description;
	if (p_description.is_empty()) {
		runner->description = vformat("Running %s...", p_command);
	}
	runner->task_id = TaskManager::get_singleton()->add_task(
			runner->runner_struct,
			nullptr,
			runner->description,
			-1, true, true, !runner->no_progress, false);
	return runner;
}

Error ProcessRunner::run_process(const String &p_command, const Vector<String> &p_arguments, const String &p_description, bool p_quiet, String *p_output) {
	Ref<ProcessRunner> runner = start_process(p_command, p_arguments, p_description, p_quiet);
	Error err = runner->wait_for_completion();
	if (p_output) {
		*p_output = runner->runner_struct->output;
	}
	return err;
}

Error ProcessRunner::wait_for_completion() {
	if (task_id == -1) {
		return ret_error;
	}
	ret_error = TaskManager::get_singleton()->wait_for_task_completion(task_id);
	task_id = -1;
	if (ret_error != OK) {
		return ret_error;
	}
	if (runner_struct->error_code != 0) {
		switch (runner_struct->error_code) {
			case -3:
				ret_error = ERR_FILE_CANT_OPEN;
			case -2:
				ret_error = ERR_CANT_FORK;
			case -1:
				ret_error = ERR_UNCONFIGURED;
			default:
				ret_error = ERR_CANT_OPEN;
		}
	}
	return ret_error;
}

void ProcessRunner::cancel() {
	if (runner_struct) {
		runner_struct->cancel();
	}
}

bool ProcessRunner::is_finished() const {
	if (task_id == -1) {
		return true;
	}
	return TaskManager::get_singleton()->is_current_task_completed(task_id);
}

ProcessRunner::~ProcessRunner() {
	if (task_id != -1) {
		if (!is_finished()) {
			ERR_PRINT(vformat("Command %s was not finished, cancelling...", runner_struct->command));
			cancel();
		}
		wait_for_completion();
	}
}

String ProcessRunner::get_output() const {
	if (runner_struct) {
		return runner_struct->output;
	}
	return "";
}

int ProcessRunner::get_error_code() const {
	if (runner_struct) {
		return runner_struct->error_code;
	}
	return 0;
}
