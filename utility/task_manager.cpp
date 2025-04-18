#include "task_manager.h"

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

//	void wait_for_group_task_completion(WorkerThreadPool::GroupID p_group_id);
Error TaskManager::wait_for_group_task_completion(WorkerThreadPool::GroupID p_group_id) {
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

void TaskManager::DownloadQueueThread::main_loop() {
	while (running) {
		DownloadTaskID item;
		if (!queue.try_pop(item)) {
			if (!waiting) {
				TaskManager::get_singleton()->update_progress_bg();
			}
			OS::get_singleton()->delay_usec(10000);
			continue;
		}
		std::shared_ptr<BaseTemplateTaskData> task;
		tasks.if_contains(item, [&](auto &v) {
			task = v.second;
			if (!task->is_waiting) {
				task->start();
			}
		});
		if (!task) {
			continue;
		}
		while (!task->is_waiting && !task->is_done()) {
			task->update_progress();
			OS::get_singleton()->delay_usec(50000);
		}
		while (!task->is_done()) {
			OS::get_singleton()->delay_usec(10000);
		}
	}
}

TaskManager::DownloadTaskID TaskManager::DownloadQueueThread::add_download_task(const String &p_download_url, const String &p_save_path) {
	DownloadTaskID task_id = current_task_id.fetch_add(1);
	// TODO: DownloadTaskData
	// tasks.try_emplace(task_id, std::make_shared<DownloadTaskData>(p_download_url, p_save_path));
	return task_id;
}

Error TaskManager::DownloadQueueThread::wait_for_task_completion(DownloadTaskID p_task_id) {
	waiting = true;
	std::shared_ptr<BaseTemplateTaskData> task;
	bool already_waiting = false;
	bool found = tasks.modify_if(p_task_id, [&](auto &v) {
		task = v.second;
		already_waiting = task->is_waiting;
		task->is_waiting = true;
		task->start();
	});
	if (!task || !found) {
		return ERR_INVALID_PARAMETER;
	} else if (already_waiting) {
		return ERR_ALREADY_IN_USE;
	}
	task->wait_for_completion();
	waiting = false;
	return OK;
}
void TaskManager::DownloadQueueThread::thread_func(void *p_userdata) {
	((DownloadQueueThread *)p_userdata)->main_loop();
}

TaskManager::DownloadQueueThread::DownloadQueueThread() {
	thread = memnew(Thread);
	thread->start(thread_func, this);
}

TaskManager::DownloadQueueThread::~DownloadQueueThread() {
	running = false;
	thread->wait_to_finish();
	memdelete(thread);
}
