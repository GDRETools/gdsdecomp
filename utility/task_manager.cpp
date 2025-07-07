#include "task_manager.h"
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

Error TaskManager::wait_for_task_completion(TaskManagerID p_group_id) {
	if (p_group_id == -1) {
		return ERR_INVALID_PARAMETER;
	}
	Error err = OK;
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
		if (task->wait_for_completion()) {
			err = ERR_SKIP;
		}
	}
	group_id_to_description.erase(p_group_id);
	return err;
}

void TaskManager::update_progress_bg() {
	group_id_to_description.for_each_m([&](auto &v) {
		if (!v.second->is_waiting) {
			v.second->update_progress();
		}
	});
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

TaskManager::DownloadTaskData::DownloadTaskData(const String &p_download_url, const String &p_save_path, bool silent) :
		download_url(p_download_url), save_path(p_save_path), silent(silent) {
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
	bool found = tasks.modify_if(p_task_id, [&](auto &v) {
		task = v.second;
		already_waiting = task->is_waiting;
		task->is_waiting = true;
	});
	if (!task || !found) {
		return ERR_INVALID_PARAMETER;
	} else if (already_waiting) {
		return ERR_ALREADY_IN_USE;
	}
	Error err = OK;
	while (!task->is_started()) {
		if (GDREProgressDialog::get_singleton() && GDREProgressDialog::is_safe_to_redraw()) {
			GDREProgressDialog::get_singleton()->main_thread_update();
		}
		OS::get_singleton()->delay_usec(10000);
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
