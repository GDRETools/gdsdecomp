#include "task_manager.h"
#include "main/main.h"
#include "utility/common.h"

TaskManager *TaskManager::singleton = nullptr;

TaskManager::TaskManager() {
	singleton = this;
}

TaskManager::~TaskManager() {
	group_id_to_description.clear();
	singleton = nullptr;
}

TaskManager *TaskManager::get_singleton() {
	return singleton;
}

void TaskManager::BaseTemplateTaskData::start() {
	if (started) {
		return;
	}
	start_internal();
	started = true;
}
bool TaskManager::BaseTemplateTaskData::is_started() const {
	return started;
}
bool TaskManager::BaseTemplateTaskData::is_canceled() const {
	return canceled;
}
void TaskManager::BaseTemplateTaskData::cancel() {
	canceled = true;
	cancel_internal();
}
void TaskManager::BaseTemplateTaskData::finish_progress() {
	progress = nullptr;
}
bool TaskManager::BaseTemplateTaskData::is_progress_enabled() const {
	return progress_enabled;
}

bool TaskManager::BaseTemplateTaskData::wait_update_progress(bool p_force_refresh) {
	bool bg_ret = false;
	if (!dont_update_progress_bg) {
		// We only want to force a redraw for the other tasks if the progress is not enabled, since `update_progress` can force the redraw itself
		bool force_redraw_bg = !progress_enabled ? p_force_refresh : false;
		// This will not update this task's progress, as `is_waiting` is true here.
		bg_ret = TaskManager::get_singleton()->update_progress_bg(force_redraw_bg);
	} else if (!progress_enabled) {
		bg_ret = TaskManager::get_singleton()->is_current_task_canceled();
	}
	update_progress(p_force_refresh);
	// Only use the cancel value if the progress is not enabled
	if (!progress_enabled && bg_ret && !is_canceled()) {
		cancel();
	}

	return is_canceled();
}

String TaskManager::BaseTemplateTaskData::_get_task_description() {
	if (is_canceled()) {
		return "Cancelling...";
	}
	return get_current_task_step_description();
}

// returns true if the task was cancelled before completion
bool TaskManager::BaseTemplateTaskData::update_progress(bool p_force_refresh) {
	if (progress_enabled && progress.is_valid() && progress->step(_get_task_description(), get_current_task_step_value(), p_force_refresh)) {
		if (!is_canceled()) {
			cancel();
		}
	}

	return is_canceled();
}
bool TaskManager::BaseTemplateTaskData::is_timed_out() const {
	return timed_out;
}

bool TaskManager::BaseTemplateTaskData::_is_aborted() const {
	return _aborted;
}

bool TaskManager::BaseTemplateTaskData::_wait_after_cancel() {
	bool is_main_thread = Thread::is_main_thread();
	if (progress_enabled && progress.is_valid()) {
		progress->set_progress_length(true);
		wait_update_progress(is_main_thread);
	}

	auto curr_time = OS::get_singleton()->get_ticks_msec();
	constexpr uint64_t ABORT_THRESHOLD_MS = 10000;
	while (!is_done() && OS::get_singleton()->get_ticks_msec() - curr_time < ABORT_THRESHOLD_MS) {
		OS::get_singleton()->delay_usec(10000);
		wait_update_progress(is_main_thread);
	}
	if (is_done()) {
		wait_for_task_completion_internal();
		return true;
	} else {
		WARN_PRINT("Couldn't wait for task completion!!!!!");
		_aborted = true;
	}
	return false;
}

bool TaskManager::BaseTemplateTaskData::wait_for_completion(uint64_t timeout_s_no_progress) {
	bool is_main_thread = Thread::is_main_thread();
	_aborted = false;
	if (is_canceled()) {
		if (started && !runs_current_thread) {
			_wait_after_cancel();
		}
		return true;
	}
	if (!started) {
		if (auto_start) {
			start();
		} else {
			while (!started && !is_canceled()) {
				OS::get_singleton()->delay_usec(10000);
				if (!dont_update_progress_bg) {
					if (TaskManager::get_singleton()->update_progress_bg(is_main_thread)) {
						break;
					}
				}
			}
		}
	}
	if (is_canceled()) {
		if (started && !runs_current_thread) {
			_wait_after_cancel();
		}
		return true;
	}
	if (runs_current_thread) {
		run_on_current_thread();
	} else {
#if DEBUG_ENABLED
		if (!is_main_thread) {
			WARN_PRINT("Waiting for group task completion on non-main thread, progress will not be updated!");
		}
#endif
		uint64_t last_progress_made = OS::get_singleton()->get_ticks_msec();
		auto last_progress = get_current_task_step_value();
		bool printed_warning = false;
		while (!is_done()) {
			OS::get_singleton()->delay_usec(10000);
			if (timeout_s_no_progress != 0) {
				auto curr_progress = get_current_task_step_value();
				auto curr_time = OS::get_singleton()->get_ticks_msec();
				if (curr_progress != last_progress) {
					last_progress_made = curr_time;
					last_progress = curr_progress;
				} else {
					auto delta = curr_time - last_progress_made;
					if (!printed_warning && delta > (timeout_s_no_progress - 5) * 1000) {
						print_line("Task is taking an unusually long time to complete, cancelling in 5 seconds...");
						printed_warning = true;
					} else if (delta > timeout_s_no_progress * 1000) {
						ERR_PRINT("Task is taking too long to complete, cancelling...");
						timed_out = true;
						cancel();
						break;
					}
				}
			}
			wait_update_progress(is_main_thread);
			if (is_canceled()) {
				break;
			}
		}
		if (!is_canceled()) {
			wait_for_task_completion_internal();
		} else {
			_wait_after_cancel();
		}
	}
	finish_progress();
	return is_canceled();
}

TaskManager::BaseTemplateTaskData::~BaseTemplateTaskData() {}

Error TaskManager::wait_for_task_completion(TaskManagerID p_group_id, uint64_t timeout_s_no_progress) {
	if (p_group_id == -1) {
		return ERR_INVALID_PARAMETER;
	}
	Error err = OK;
	bool erase = true;
	{
		std::shared_ptr<BaseTemplateTaskData> task;
		bool already_waiting = false;
		bool found = group_id_to_description.modify_if(p_group_id, [&](auto &v) {
			task = v.second;
			already_waiting = task->is_waiting;
			task->is_waiting = true;
		});
		if (!task || !found) {
			return ERR_INVALID_PARAMETER;
		} else if (already_waiting) {
			return ERR_ALREADY_IN_USE;
		}
		if (task->wait_for_completion(timeout_s_no_progress)) {
			if (task->is_timed_out()) {
				err = ERR_TIMEOUT;
			} else {
				err = ERR_SKIP;
			}
			if (task->_is_aborted()) {
				erase = false;
			}
		}
	}
	if (erase) {
		group_id_to_description.erase(p_group_id);
	}
	return err;
}

bool TaskManager::update_progress_bg(bool p_force_refresh) {
	bool main_loop_iterating = false;
	bool canceled = false;
	group_id_to_description.for_each_m([&](auto &v) {
		if (v.second->is_progress_enabled() && v.second->is_started()) {
			main_loop_iterating = true;
			if (!v.second->is_waiting) {
				v.second->update_progress(p_force_refresh);
			}
		}
		if (v.second->is_canceled()) {
			canceled = true;
		}
	});
	if (!main_loop_iterating && !Main::is_iterating() && Thread::is_main_thread() && !MessageQueue::get_singleton()->is_flushing()) {
		Main::iteration();
	}
	return canceled;
}

TaskManager::DownloadTaskID TaskManager::add_download_task(const String &p_download_url, const String &p_save_path, bool silent) {
	return download_thread.add_download_task(p_download_url, p_save_path, silent);
}

Error TaskManager::wait_for_download_task_completion(DownloadTaskID p_task_id) {
	return download_thread.wait_for_task_completion(p_task_id);
}

int TaskManager::DownloadTaskData::get_current_task_step_value() {
	return download_progress * 1000;
}

void TaskManager::DownloadTaskData::run_on_current_thread() {
	if (is_canceled()) {
		done = true;
		return;
	}
	callback_data(nullptr);
}

void TaskManager::DownloadTaskData::wait_for_task_completion_internal() {
	while (!is_done()) {
		OS::get_singleton()->delay_usec(10000);
	}
}

bool TaskManager::DownloadTaskData::is_done() const {
	return done;
}

void TaskManager::DownloadTaskData::cancel_internal() {
	if (!is_started()) {
		done = true;
	}
}

String TaskManager::DownloadTaskData::get_current_task_step_description() {
	return "Downloading " + download_url;
}

void TaskManager::DownloadTaskData::callback_data(void *p_data) {
	download_error = gdre::download_file_sync(download_url, save_path, &download_progress, &canceled);
	done = true;
}

void TaskManager::DownloadTaskData::start_internal() {
	if (!silent) {
		progress = EditorProgressGDDC::create(nullptr, get_current_task_step_description() + itos(rand()), get_current_task_step_description(), 1000, true);
	}
}

TaskManager::DownloadTaskData::DownloadTaskData(const String &p_download_url, const String &p_save_path, bool p_silent) :
		download_url(p_download_url), save_path(p_save_path), silent(p_silent) {
	dont_update_progress_bg = true;
	auto_start = false;
}

void TaskManager::DownloadQueueThread::main_loop() {
	while (running) {
		DownloadTaskID item;
		if (!queue.try_pop(item)) {
			// if (!waiting) {
			// 	TaskManager::get_singleton()->update_progress_bg();
			// }
			OS::get_singleton()->delay_usec(10000);
			continue;
		}
		std::shared_ptr<DownloadTaskData> task;
		tasks.if_contains(item, [&](auto &v) {
			task = v.second;
			if (task) {
				MutexLock lock(worker_mutex);
				running_task = task;
				worker_cv.notify_all();
			}
		});
		ERR_CONTINUE_MSG(!task, "Download task ID " + itos(item) + " not found");
		while (!task->is_done() && !task->is_waiting) {
			task->update_progress();
			OS::get_singleton()->delay_usec(10000);
		}
		while (!task->is_done()) {
			OS::get_singleton()->delay_usec(10000);
		}
		if (!task->is_waiting) {
			task->finish_progress();
		}
		if (task->is_canceled()) {
			// pop off the rest of the queue
			MutexLock lock(write_mutex);
			tasks.for_each_m([&](auto &v) {
				v.second->cancel();
			});
			tasks.clear();
			while (queue.try_pop(item)) {
			}
		}
	}
}

TaskManager::DownloadTaskID TaskManager::DownloadQueueThread::add_download_task(const String &p_download_url, const String &p_save_path, bool silent) {
	MutexLock lock(write_mutex);

	DownloadTaskID task_id = ++current_task_id;
	tasks.try_emplace(task_id, std::make_shared<DownloadTaskData>(p_download_url, p_save_path, silent));
	queue.try_push(task_id);
	return task_id;
}

Error TaskManager::DownloadQueueThread::wait_for_task_completion(DownloadTaskID p_task_id) {
	waiting = true;
	std::shared_ptr<DownloadTaskData> task;
	bool already_waiting = false;
	bool is_main_thread = Thread::is_main_thread();
	bool found = tasks.modify_if(p_task_id, [&](auto &v) {
		task = v.second;
		already_waiting = task->is_waiting;
		task->is_waiting = true; // is_main_thread;
	});
	if (!task || !found) {
		return ERR_INVALID_PARAMETER;
	} else if (already_waiting) {
		return ERR_ALREADY_IN_USE;
	}
	Error err = OK;
	while (!task->is_started()) {
		if (is_main_thread) {
			if (TaskManager::get_singleton()->update_progress_bg(true)) {
				err = ERR_SKIP;
				break;
			}
		}
		if (task->is_canceled() || task->is_done()) {
			err = ERR_SKIP;
			break;
		}
		OS::get_singleton()->delay_usec(10000);
	}
	if (err) {
		return err;
	}
	if (task->wait_for_completion()) {
		err = ERR_SKIP;
	} else {
		err = task->get_download_error();
	}
	tasks.erase(p_task_id);
	waiting = false;
	return err;
}

void TaskManager::DownloadQueueThread::worker_main_loop() {
	while (running) {
		if (!running_task) {
			MutexLock lock(worker_mutex);
			worker_cv.wait(lock);
		}
		if (!running_task) {
			continue;
		}
		running_task->start();
		running_task->run_on_current_thread();
		running_task = nullptr;
	}
}

void TaskManager::DownloadQueueThread::thread_func(void *p_userdata) {
	((DownloadQueueThread *)p_userdata)->main_loop();
}
void TaskManager::DownloadQueueThread::worker_thread_func(void *p_userdata) {
	((DownloadQueueThread *)p_userdata)->worker_main_loop();
}
TaskManager::DownloadQueueThread::DownloadQueueThread() {
	thread = memnew(Thread);
	thread->start(thread_func, this);
	worker_thread = memnew(Thread);
	worker_thread->start(worker_thread_func, this);
}

TaskManager::DownloadQueueThread::~DownloadQueueThread() {
	running = false;
	{
		MutexLock lock(worker_mutex);
		worker_cv.notify_all();
	}
	thread->wait_to_finish();
	worker_thread->wait_to_finish();
	memdelete(thread);
	memdelete(worker_thread);
	running_task = nullptr;
	current_task_id = -1;
	tasks.clear();
}

bool TaskManager::is_current_task_canceled() {
	bool canceled = false;
	group_id_to_description.for_each([&](auto &v) {
		if (v.second->is_canceled()) {
			canceled = true;
		}
	});
	return canceled;
}

bool TaskManager::is_current_task_timed_out() {
	bool timed_out = false;
	group_id_to_description.for_each([&](auto &v) {
		if (v.second->is_timed_out()) {
			timed_out = true;
		}
	});
	return timed_out;
}

bool TaskManager::is_current_task_completed(TaskManagerID p_task_id) const {
	bool done = false;
	group_id_to_description.if_contains(p_task_id, [&](auto &v) {
		auto task = v.second;
		if (task->is_done()) {
			done = true;
		}
	});
	return done;
}

void TaskManager::cancel_all() {
	group_id_to_description.for_each_m([&](auto &v) {
		v.second->cancel();
	});
}
