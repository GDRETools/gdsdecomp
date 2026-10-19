#include "task_manager.h"

#include "core/io/file_access.h"

#include "gui/gdre_progress.h"
#include "main/gdre_main_loop.h"
#include "utility/common.h"
#include "utility/http_requester.h"

static constexpr int64_t ONE_GB = 1024LL * 1024LL * 1024LL;
static constexpr int64_t TWELVE_GB = 12 * ONE_GB;
static constexpr int64_t FOUR_GB = 4 * ONE_GB;

int64_t TaskManager::maximum_memory_usage = TWELVE_GB;

TaskManager *TaskManager::singleton = nullptr;

TaskManager::TaskManager() :
		main_thread_dispatch_queue(WorkerThreadPool::get_singleton()->get_thread_count() * 2) {
	singleton = this;
	Dictionary mem_info = OS::get_singleton()->get_memory_info();
	// 3/4ths of the physical memory, but no more than 12GB
	int64_t max_usage = (int64_t)mem_info["physical"] * 0.75;
	if (max_usage <= 0) {
		max_usage = FOUR_GB;
	}
	named_pool = WorkerThreadPool::get_named_pool("TaskManager");
	maximum_memory_usage = MIN(max_usage, TWELVE_GB);
	auto thread_count = named_pool->get_thread_count();
	thread_index_to_thread_id.resize(thread_count);
	thread_index_to_task_ids.resize(thread_count);
	for (int i = 0; i < thread_count; i++) {
		thread_index_to_thread_id[i] = -1;
		thread_index_to_task_ids[i] = -1;
	}
	auto task_id = named_pool->add_template_group_task(
			this,
			&TaskManager::_set_thread_name_task,
			nullptr,
			thread_count,
			thread_count,
			true,
			"TaskManagerThreadSetup");
	named_pool->wait_for_group_task_completion(task_id);
}

TaskManager::~TaskManager() {
	group_id_to_description.clear();
	singleton = nullptr;
}

void TaskManager::_set_thread_name_task(uint32_t i, void *p_userdata) {
	// delaying here to ensure all the threads are started
	OS::get_singleton()->delay_usec(10000);
	int thread_index = named_pool->get_thread_index();
	thread_index_to_thread_id[thread_index] = Thread::get_caller_id();
	Thread::set_name(vformat("GDRETask %d (%d)", thread_index, Thread::get_caller_id()));
}

TaskManager *TaskManager::get_singleton() {
	return singleton;
}

int TaskManager::get_max_thread_count() {
	if (get_singleton() && get_singleton()->named_pool) {
		return get_singleton()->named_pool->get_thread_count();
	}
	return WorkerThreadPool::get_singleton()->get_thread_count();
}

WorkerThreadPool *TaskManager::get_thread_pool() {
	return get_singleton() ? get_singleton()->named_pool : WorkerThreadPool::get_singleton();
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
	if (not_in_main_queue && progress_enabled) {
		update_progress(false);
	}
	if (Thread::is_main_thread()) {
		bg_ret = GDREMainLoop::wait_until_next_frame();
	} else {
		bg_ret = TaskManager::get_singleton()->is_current_task_canceled();
		if (!bg_ret) {
			OS::get_singleton()->delay_usec(10000);
		}
	}
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
	if (progress_enabled && progress.is_valid()) {
		if (progress->step(_get_task_description(), get_current_task_step_value(), p_force_refresh)) {
			if (!is_canceled()) {
				cancel();
			}
		}
		if (is_done() && auto_close_progress_bar()) {
			finish_progress();
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
	constexpr uint64_t ABORT_THRESHOLD_MS = 15000;
	while (!is_done() && OS::get_singleton()->get_ticks_msec() - curr_time < ABORT_THRESHOLD_MS) {
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
				if (is_main_thread) {
					if (GDREMainLoop::wait_until_next_frame()) {
						break;
					}
				} else {
					OS::get_singleton()->delay_usec(10000);
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
		[[maybe_unused]] uint64_t last_reported_mem_usage_ms = 0;
		while (!is_done()) {
#if 0
			if (OS::get_singleton()->get_ticks_msec() - last_reported_mem_usage_ms > 1000) {
				print_line("Memory usage: " + String::humanize_size(OS::get_singleton()->get_static_memory_usage()));
				last_reported_mem_usage_ms = OS::get_singleton()->get_ticks_msec();
			}
#endif
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

TaskManager::BaseGroupTaskData::BaseGroupTaskData(
		TaskManagerID p_task_manager_id,
		int p_elements,
		const String &p_task,
		const String &p_description,
		bool p_can_cancel,
		int p_tasks,
		bool p_high_priority,
		bool p_runs_current_thread,
		bool p_progress_enabled,
		Ref<EditorProgressGDDC> p_progress,
		int p_progress_start) :
		task_manager_id(p_task_manager_id),
		elements(p_elements),
		tasks(p_tasks == -1 ? TaskManager::get_max_thread_count() : p_tasks),
		task(p_task),
		description(p_description),
		can_cancel(p_can_cancel),
		high_priority(p_high_priority),
		progress_start(p_progress_start) {
	if (tasks > elements) { // Don't spin up more threads than we need
		tasks = elements;
	}
	progress_enabled = p_progress_enabled;
	progress = p_progress;
	runs_current_thread = p_runs_current_thread;
}

String TaskManager::BaseGroupTaskData::get_current_task_step_description() {
	if (elements == 0) {
		return "<UNKNOWN>";
	}
	return get_current_task_step_description_internal(last_completed < elements ? (int)last_completed : elements - 1);
}

void TaskManager::BaseGroupTaskData::start_internal() {
	if (runs_current_thread) {
		// random group id
		started_current_thread = true;
	} else {
		for (int i = 0; i < tasks; i++) {
			task_ids.push_back(TaskManager::get_thread_pool()->add_template_task(this, &BaseGroupTaskData::task_callback, nullptr, high_priority, task + "_" + itos(i)));
		}
	}
	if (progress_enabled && progress.is_null()) {
		progress = EditorProgressGDDC::create(nullptr, task + itos(task_manager_id), description, elements, can_cancel);
	}
}

bool TaskManager::BaseGroupTaskData::_do_one_iteration(bool p_no_memory_check) {
	// if we're using too much memory, wait until it goes down
	if (unlikely(!p_no_memory_check && is_memory_usage_too_high() && !canceled)) {
		tasks_busy_waiting++;
		while (is_memory_usage_too_high() && !canceled) {
			if (tasks_busy_waiting == tasks || elements - last_completed == tasks_busy_waiting) {
				// all tasks are busy waiting, so we should break to prevent a deadlock and just deal with the potential thrashing.
				break;
			}
			OS::get_singleton()->delay_usec(10000);
		}
		tasks_busy_waiting--;
	}
	if (unlikely(canceled)) {
		return true;
	}

	int64_t work_index = current_work_index++;

	if (work_index >= elements) {
		return true;
	}
	call_instance_method_internal(work_index);

	// This is the only way to ensure posting is done when all tasks are really complete.
	int64_t completed_amount = ++last_completed;

	if (completed_amount == elements) {
		done = true; // TODO: check that this doesn't break
		return true;
	}
	return false;
}

void TaskManager::BaseGroupTaskData::task_callback(void *_unused) {
	TaskManager::get_singleton()->set_thread_task_id(task_manager_id);
	bool no_memory_check = tasks == 1;
	while (true) {
		if (_do_one_iteration(no_memory_check)) {
			break;
		}
	}
	TaskManager::get_singleton()->set_thread_task_id(-1);
	tasks_quit++;
}

bool TaskManager::BaseGroupTaskData::is_done() const {
	if (runs_current_thread) {
		return last_completed >= elements;
	}
	if (canceled) {
		return tasks_quit >= tasks;
	}
	return done;
}

int TaskManager::BaseGroupTaskData::get_current_task_step_value() {
	return last_completed + progress_start;
}

void TaskManager::BaseGroupTaskData::run_on_current_thread() {
	if (canceled) {
		return;
	}
	TaskManager::get_singleton()->set_thread_task_id(task_manager_id);
	uint64_t last_progress_upd = OS::get_singleton()->get_ticks_usec();
	for (int i = 0; i < elements; i++) {
		if (_do_one_iteration(true) || OS::get_singleton()->get_ticks_usec() - last_progress_upd > 50000) {
			if (update_progress()) {
				break;
			}
			last_progress_upd = OS::get_singleton()->get_ticks_usec();
		}
	}
	TaskManager::get_singleton()->set_thread_task_id(-1);
}

void TaskManager::BaseGroupTaskData::wait_for_task_completion_internal() {
	if (!runs_current_thread) {
		for (auto task_id : task_ids) {
			TaskManager::get_thread_pool()->wait_for_task_completion(task_id);
		}
	}
}

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
			if (!already_waiting) {
				task->is_waiting = true;
			}
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
				task->is_waiting = false;
				err = ERR_LOCKED;
			}
		}
	}
	if (erase) {
		group_id_to_description.erase(p_group_id);
	}
	return err;
}

bool TaskManager::update_progress_bg(bool p_force_refresh, bool called_from_process, bool *r_did_redraw, bool *r_has_tasks) {
	bool has_tasks = !group_id_to_description.empty();
	if (r_has_tasks) {
		*r_has_tasks = has_tasks;
	}
	if (updating_bg || !has_tasks) {
		if (r_did_redraw) {
			*r_did_redraw = false;
		}
		return false;
	}
	updating_bg = true;
	bool main_loop_iterating = false;
	bool canceled = false;
	Vector<TaskManagerID> task_ids_to_erase;
	group_id_to_description.for_each_m([&](auto &v) {
		if (v.second->_is_aborted()) {
			if (v.second->is_done()) {
				task_ids_to_erase.push_back(v.first);
			}
		} else {
			if (v.second->is_progress_enabled() && v.second->is_started()) {
				main_loop_iterating = true;
				v.second->update_progress(false);
			}
			if (v.second->is_canceled()) {
				canceled = true;
			}
		}
	});
	for (auto &task_id : task_ids_to_erase) {
		group_id_to_description.erase(task_id);
	}
	if (p_force_refresh && main_loop_iterating && GDREProgressDialog::is_safe_to_redraw() && GDREProgressDialog::get_singleton()) {
		bool did_redraw = GDREProgressDialog::get_singleton()->main_thread_update();
		if (r_did_redraw) {
			*r_did_redraw = did_redraw;
		}
	}
	updating_bg = false;
	return canceled;
}

void TaskManager::set_thread_task_id(TaskManagerID p_task_manager_id) {
	if (Thread::is_main_thread()) {
		main_thread_task_id = p_task_manager_id;
	} else {
		int thread_index = named_pool->get_thread_index();
		ERR_FAIL_COND_MSG(thread_index == -1, "This can only be called from a task thread");
		thread_index_to_task_ids[thread_index] = p_task_manager_id;
	}
}

TaskManager::TaskManagerID TaskManager::get_thread_task_id() const {
	if (Thread::is_main_thread()) {
		return main_thread_task_id;
	} else {
		int thread_index = named_pool->get_thread_index();
		ERR_FAIL_COND_V_MSG(thread_index == -1, -1, "This can only be called from a task thread");
		return thread_index_to_task_ids[thread_index];
	}
}
bool TaskManager::_dispatch_to_main_thread(std::shared_ptr<BaseMainThreadDispatchData> p_data) {
	if (Thread::is_main_thread()) {
		p_data->callback();
		return p_data->is_canceled();
	}
	while (!main_thread_dispatch_queue.try_push(p_data)) {
		OS::get_singleton()->delay_usec(10000); // wait for a slot to become available
	}
	p_data->wait_for_completion();
	return p_data->is_canceled();
}

void TaskManager::process_main_thread_dispatch_queue_for(int64_t time_usec) {
	if (!Thread::is_main_thread()) {
		return;
	}
	if (processing_main_thread_dispatch_queue) {
		return;
	}
	processing_main_thread_dispatch_queue = true;
	int64_t start_time_usec = OS::get_singleton()->get_ticks_usec();
	int64_t elapsed_time_usec = 0;
	while (elapsed_time_usec < time_usec) {
		std::shared_ptr<BaseMainThreadDispatchData> data;
		if (!main_thread_dispatch_queue.try_pop(data)) {
			break;
		}
		TaskManagerID previous_main_thread_task_id = main_thread_task_id;
		if (previous_main_thread_task_id == data->get_calling_task_id()) {
			WARN_PRINT("Trying to dispatch to the same task id, this is not supported!");
		}
		set_thread_task_id(data->get_calling_task_id());
		data->callback();
		set_thread_task_id(previous_main_thread_task_id);
		elapsed_time_usec = OS::get_singleton()->get_ticks_usec() - start_time_usec;
	}
	processing_main_thread_dispatch_queue = false;
}

void TaskManager::cancel_main_thread_dispatch_queue() {
	if (!Thread::is_main_thread()) {
		return;
	}
	std::shared_ptr<BaseMainThreadDispatchData> data;
	while (main_thread_dispatch_queue.try_pop(data)) {
		data->cancel();
	}
}

TaskManager::DownloadTaskID TaskManager::add_fake_download_task(const String &p_download_url, const String &p_save_path) {
	return download_thread.add_fake_download_task(p_download_url, p_save_path);
}

TaskManager::DownloadTaskID TaskManager::add_download_task(const String &p_download_url, const String &p_save_path, bool silent, bool write_sha256) {
	return download_thread.add_download_task(p_download_url, p_save_path, silent, write_sha256);
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
	done = true;
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
	if (size == -1) {
		return "Downloading...";
	}
	int64_t current_time = OS::get_singleton()->get_ticks_msec();
	int64_t elapsed_time_ms = current_time - start_time;
	int64_t elapsed_time_seconds = elapsed_time_ms / 1000;
	int64_t bytes_downloaded = int64_t(size * download_progress);
	String time_remaining_str = "";
	String bytes_per_second_str = "";

	if (elapsed_time_seconds > 0) {
		int64_t bytes_per_ms = (bytes_downloaded / elapsed_time_ms);
		int64_t bytes_per_second = bytes_per_ms * 1000;
		speed_history.push_back(bytes_per_second);
		int64_t time_remaining = (size - bytes_downloaded) / (bytes_per_second > 0 ? bytes_per_second : 1);
		if (time_remaining > 60) {
			time_remaining_str = vformat("%dm %02ds left, ", time_remaining / 60, time_remaining % 60);
		} else if (time_remaining > 3) {
			time_remaining_str = vformat("%ds left, ", time_remaining);
		} else {
			time_remaining_str = "a few seconds left, ";
		}
		bytes_per_second_str = vformat(" (%s/sec)", String::humanize_size(bytes_per_second));
	}

	return vformat("%s%s of %s%s",
			time_remaining_str,
			String::humanize_size(bytes_downloaded),
			String::humanize_size(size),
			bytes_per_second_str);
}

void TaskManager::DownloadTaskData::callback_data(void *p_data) {
	start_time = OS::get_singleton()->get_ticks_msec();
	download_error = HTTPRequester::download_file_sync(download_url, save_path, &download_progress, &canceled, &size);
#if TOOLS_ENABLED
	speed_history.sort();
	int64_t end_time = OS::get_singleton()->get_ticks_msec();
	int64_t median_speed = speed_history.size() > 0 ? speed_history[speed_history.size() / 2] : size;
	int64_t average_speed = 0;
	if (speed_history.size() > 0) {
		for (int64_t speed : speed_history) {
			average_speed += speed;
		}
		average_speed /= speed_history.size();
	} else {
		average_speed = size;
	}
	print_line(vformat("%s: Downloaded %s in %sms, Median speed: %s, Average speed: %s", download_url.get_file(), String::humanize_size(size), end_time - start_time, String::humanize_size(median_speed), String::humanize_size(average_speed)));
#endif
	if (download_error == OK && write_sha256) {
		if (auto fa = FileAccess::open(save_path + ".sha256", FileAccess::WRITE); fa.is_valid()) {
			String sha256 = gdre::get_sha256(save_path);
			fa->store_string(sha256);
		}
	}
}

void TaskManager::DownloadTaskData::start_internal() {
	if (!silent) {
		String short_desc = "Downloading " + download_url.get_file();
		progress = EditorProgressGDDC::create(nullptr, "Downloading " + download_url + itos(rand()), short_desc, 1000, true);
	}
}

TaskManager::DownloadTaskData::DownloadTaskData(const String &p_download_url, const String &p_save_path, bool p_silent, bool p_fake, bool p_write_sha256) :
		download_url(p_download_url), save_path(p_save_path), write_sha256(p_write_sha256), silent(p_silent), fake(p_fake) {
	not_in_main_queue = true;
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

TaskManager::DownloadTaskID TaskManager::DownloadQueueThread::add_fake_download_task(const String &p_download_url, const String &p_save_path) {
	MutexLock lock(write_mutex);
	DownloadTaskID task_id = ++current_task_id;
	tasks.try_emplace(task_id, std::make_shared<DownloadTaskData>(p_download_url, p_save_path, true, true, false));
	// Don't add it to the queue; it will never be actually run
	return task_id;
}

TaskManager::DownloadTaskID TaskManager::DownloadQueueThread::add_download_task(const String &p_download_url, const String &p_save_path, bool silent, bool p_write_sha256) {
	MutexLock lock(write_mutex);

	DownloadTaskID task_id = ++current_task_id;
	tasks.try_emplace(task_id, std::make_shared<DownloadTaskData>(p_download_url, p_save_path, silent, false, p_write_sha256));
	queue.try_push(task_id);
	return task_id;
}

Error TaskManager::DownloadQueueThread::wait_for_task_completion(DownloadTaskID p_task_id) {
	waiting = true;
	std::shared_ptr<DownloadTaskData> task;
	bool already_waiting = false;
	bool is_main_thread = Thread::is_main_thread();
	bool is_fake = false;
	bool found = tasks.modify_if(p_task_id, [&](auto &v) {
		task = v.second;
		already_waiting = task->is_waiting;
		is_fake = task->fake;
		task->is_waiting = true; // is_main_thread;
	});
	if (!task || !found) {
		waiting = false;
		return ERR_INVALID_PARAMETER;
	} else if (already_waiting) {
		waiting = false;
		return ERR_ALREADY_IN_USE;
	}
	if (is_fake) {
		tasks.erase(p_task_id);
		waiting = false;
		return OK;
	}
	Error err = OK;
	while (!task->is_started()) {
		if (is_main_thread) {
			if (GDREMainLoop::wait_until_next_frame()) {
				err = ERR_SKIP;
				break;
			}
		} else {
			OS::get_singleton()->delay_usec(10000);
		}
		if (task->is_canceled()) {
			err = ERR_SKIP;
			break;
		}
		if (task->is_done()) {
			break;
		}
	}
	if (err) {
		waiting = false;
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
		auto task = running_task;
		running_task = nullptr;
		task->start();
		task->run_on_current_thread();
	}
}

void TaskManager::DownloadQueueThread::thread_func(void *p_userdata) {
	Thread::set_name("DownloadQueueThread");
	((DownloadQueueThread *)p_userdata)->main_loop();
}
void TaskManager::DownloadQueueThread::worker_thread_func(void *p_userdata) {
	Thread::set_name("DownloadWorkerThread");
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
