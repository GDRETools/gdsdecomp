#pragma once
#include "core/error/error_macros.h"
#include "core/object/worker_thread_pool.h"
#include "utility/gd_parallel_hashmap.h"
#include "utility/gd_parallel_queue.h"
#include "utility/gdre_config.h"
#include "utility/gdre_progress.h"

class TaskManager : public Object {
	GDCLASS(TaskManager, Object);

public:
	typedef int64_t DownloadTaskID;
	typedef int64_t GroupTaskID;

	class BaseTemplateTaskData {
	protected:
		bool canceled = false;
		bool runs_current_thread = false;
		bool started = false;
		bool auto_start = true;
		Ref<EditorProgressGDDC> progress;

		virtual void wait_for_task_completion_internal() = 0;
		virtual void start_internal() = 0;

	public:
		std::atomic<bool> is_waiting = false;

		virtual bool is_done() = 0;
		virtual int get_current_task_step_value() = 0;
		virtual String get_current_task_step_description() = 0;
		virtual void run_on_current_thread() = 0;

		void start() {
			if (started) {
				return;
			}
			start_internal();
			started = true;
		}
		bool is_started() const { return started; }
		bool is_canceled() const { return canceled; }
		void cancel() { canceled = true; }
		void finish_progress() {
			progress = nullptr;
		}
		// returns true if the task was cancelled before completion
		bool update_progress(bool p_force_refresh = false) {
			if (!is_canceled() && progress.is_valid() && progress->step(get_current_task_step_description(), get_current_task_step_value(), p_force_refresh)) {
				cancel();
				return true;
			}

			return is_canceled();
		}
		virtual bool wait_for_completion() {
			if (is_canceled()) {
				return true;
			}
			if (!started) {
				if (auto_start) {
					start();
				} else {
					while (!started && !is_canceled()) {
						OS::get_singleton()->delay_usec(10000);
					}
				}
			}
			if (is_canceled()) {
				return true;
			}
			if (runs_current_thread) {
				run_on_current_thread();
			} else {
				bool is_main_thread = Thread::is_main_thread();
				if (!is_main_thread) {
					WARN_PRINT("Waiting for group task completion on non-main thread, progress will not be updated!");
				}
				while (!is_done()) {
					OS::get_singleton()->delay_usec(10000);
					update_progress(is_main_thread);
					if (is_canceled()) {
						break;
					}
				}
				wait_for_task_completion_internal();
			}
			finish_progress();
			return is_canceled();
		}

		virtual ~BaseTemplateTaskData() {}
	};

	template <typename C, typename M, typename U, typename R>
	class GroupTaskData : public BaseTemplateTaskData {
		C *instance;
		M method;
		U userdata;
		int elements = -1;
		int tasks = -1;
		R task_step_desc_callback;
		String task;
		String description;
		bool can_cancel = false;
		bool high_priority = false;
		bool progress_enabled = true;
		WorkerThreadPool::GroupID group_id = -1;
		WorkerThreadPool::TaskID task_id = WorkerThreadPool::TaskID(-1);
		std::atomic<int64_t> last_completed = -1;
		int progress_start = 0;

	public:
		GroupTaskData(
				C *p_instance,
				M p_method,
				U p_userdata,
				int p_elements,
				R p_task_step_callback,
				const String &p_task,
				const String &p_description,
				bool p_can_cancel,
				int p_tasks,
				bool p_high_priority,
				bool p_runs_current_thread = false,
				bool p_progress_enabled = true,
				Ref<EditorProgressGDDC> p_progress = nullptr,
				int p_progress_start = 0) :
				instance(p_instance),
				method(p_method),
				userdata(p_userdata),
				elements(p_elements),
				tasks(p_tasks),
				task_step_desc_callback(p_task_step_callback),
				task(p_task),
				description(p_description),
				can_cancel(p_can_cancel),
				high_priority(p_high_priority),
				progress_enabled(p_progress_enabled),
				progress_start(p_progress_start) {
			runs_current_thread = p_runs_current_thread;
			progress = p_progress;
		}

		String get_current_task_step_description() override {
			return (instance->*task_step_desc_callback)(last_completed > 0 ? (int)last_completed : 0, userdata);
		}

		void start_internal() override {
			if (group_id != -1 || task_id != -1) {
				return;
			}
			if (runs_current_thread) {
				// random group id
				group_id = abs(rand());
			} else if (tasks != 1) {
				group_id = WorkerThreadPool::get_singleton()->add_template_group_task(this, &GroupTaskData::group_task_callback, userdata, elements, tasks, high_priority, task);
			} else {
				task_id = WorkerThreadPool::get_singleton()->add_template_task(this, &GroupTaskData::regular_task_callback, userdata, high_priority, task);
			}
			if (progress_enabled && progress.is_null()) {
				progress = EditorProgressGDDC::create(nullptr, task + itos(group_id), description, elements, can_cancel);
			}
		}

		bool group_task_callback(uint32_t p_index, U p_userdata) {
			if (unlikely(canceled)) {
				return true;
			}
			(instance->*method)(p_index, p_userdata);
			last_completed++;
			return false;
		}

		void regular_task_callback(U p_userdata) {
			for (int i = 0; i < elements; i++) {
				if (group_task_callback(i, p_userdata)) {
					return;
				}
			}
		}

		bool is_done() override {
			if (runs_current_thread) {
				return last_completed >= elements - 1;
			} else if (group_id != -1) {
				return WorkerThreadPool::get_singleton()->is_group_task_completed(group_id);
			} else if (task_id != -1) {
				return WorkerThreadPool::get_singleton()->is_task_completed(task_id);
			}
			return true;
		}

		inline int get_current_task_step_value() override {
			return last_completed + progress_start;
		}

		void run_on_current_thread() override {
			uint64_t last_progress_upd = OS::get_singleton()->get_ticks_usec();
			for (int i = 0; i < elements; i++) {
				if (group_task_callback(i, userdata) || OS::get_singleton()->get_ticks_usec() - last_progress_upd > 50000) {
					if (update_progress()) {
						break;
					}
					last_progress_upd = OS::get_singleton()->get_ticks_usec();
				}
			}
		}

		void wait_for_task_completion_internal() override {
			if (!runs_current_thread) {
				if (group_id != -1) {
					WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_id);
				} else if (task_id != -1) {
					WorkerThreadPool::get_singleton()->wait_for_task_completion(task_id);
				}
			}
		}
	};
	template <typename C, typename M, typename U, typename R>
	class TaskData : public BaseTemplateTaskData {
		C *instance;
		M method;
		U userdata;
		String description;
		bool can_cancel = false;
		bool high_priority = false;
	};

	class DownloadTaskData : public BaseTemplateTaskData {
		String download_url;
		String save_path;
		bool silent = false;
		float download_progress = 0.0f;
		Error download_error = OK;
		bool done = false;

	protected:
		virtual void wait_for_task_completion_internal() override;

	public:
		DownloadTaskData(const String &p_download_url, const String &p_save_path, bool p_silent = false);

		virtual void run_on_current_thread() override;
		virtual int get_current_task_step_value() override;
		virtual String get_current_task_step_description() override;
		virtual bool is_done() override;
		virtual void start_internal() override;
		void callback_data(void *p_data);
		Error get_download_error() const { return download_error; }
	};

	class DownloadQueueThread {
		Thread *thread = nullptr;
		Thread *worker_thread = nullptr;
		Mutex write_mutex;
		std::atomic<bool> running = true;
		std::atomic<bool> waiting = false;
		mutable BinaryMutex worker_mutex;
		ConditionVariable worker_cv;
		std::shared_ptr<DownloadTaskData> running_task;
		std::atomic<DownloadTaskID> current_task_id = 0;

		ParallelFlatHashMap<DownloadTaskID, std::shared_ptr<DownloadTaskData>> tasks;
		StaticParallelQueue<DownloadTaskID, 1024> queue;

		void main_loop();
		void worker_main_loop();
		static void thread_func(void *p_userdata);
		static void worker_thread_func(void *p_userdata);

	public:
		DownloadTaskID add_download_task(const String &p_download_url, const String &p_save_path, bool silent = false);
		Error wait_for_task_completion(DownloadTaskID p_task_id);
		DownloadQueueThread();
		~DownloadQueueThread();
	};

protected:
	static TaskManager *singleton;
	ParallelFlatHashMap<GroupTaskID, std::shared_ptr<BaseTemplateTaskData>> group_id_to_description;
	DownloadQueueThread download_thread;
	std::atomic<GroupTaskID> current_group_task_id = 0;

public:
	TaskManager();
	~TaskManager();
	static TaskManager *get_singleton();
	template <typename C, typename M, typename U, typename R>
	GroupTaskID add_group_task(
			C *p_instance,
			M p_method,
			U p_userdata,
			int p_elements,
			R p_task_step_callback,
			const String &p_task,
			const String &p_label,
			bool p_can_cancel = true,
			int p_tasks = -1,
			bool p_high_priority = true,
			Ref<EditorProgressGDDC> p_preexisting_progress = nullptr,
			int p_progress_start = 0) {
		bool is_singlethreaded = GDREConfig::get_singleton()->get_setting("force_single_threaded", false);
		auto task = std::make_shared<GroupTaskData<C, M, U, R>>(p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, p_tasks, p_high_priority, is_singlethreaded, true, p_preexisting_progress, p_progress_start);
		task->start();
		auto group_id = ++current_group_task_id;
		bool already_exists = false;
		group_id_to_description.try_emplace_l(group_id, [&](auto &v) { already_exists = true; }, task);
		if (already_exists) {
			CRASH_COND_MSG(already_exists, "Task already exists?!?!?!");
		}
		return group_id;
	}

	template <typename C, typename M, typename U, typename R>
	Error run_multithreaded_group_task(
			C *p_instance,
			M p_method,
			U p_userdata,
			int p_elements,
			R p_task_step_callback,
			const String &p_task,
			const String &p_label,
			bool p_can_cancel = true,
			int p_tasks = -1,
			bool p_high_priority = true,
			Ref<EditorProgressGDDC> p_preexisting_progress = nullptr,
			int p_progress_start = 0) {
		auto task_id = add_group_task(p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, p_tasks, p_high_priority, p_preexisting_progress, p_progress_start);
		return wait_for_group_task_completion(task_id);
	}

	template <typename C, typename M, typename U, typename R>
	Error run_task_on_current_thread(
			C *p_instance,
			M p_method,
			U p_userdata,
			int p_elements,
			R p_task_step_callback,
			const String &p_task,
			const String &p_label,
			bool p_can_cancel = true,
			Ref<EditorProgressGDDC> p_preexisting_progress = nullptr,
			int p_progress_start = 0) {
		GroupTaskData<C, M, U, R> task = GroupTaskData<C, M, U, R>(p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, -1, true, true, true, p_preexisting_progress, p_progress_start);
		task.start();
		return task.wait_for_completion() ? ERR_SKIP : OK;
	}

	DownloadTaskID add_download_task(const String &p_download_url, const String &p_save_path, bool silent = false);
	Error wait_for_download_task_completion(DownloadTaskID p_task_id);
	Error wait_for_group_task_completion(GroupTaskID p_group_id);
	bool is_current_group_task_canceled();
	void update_progress_bg();
};

class DownloadToken {
	String url;
	String save_path;
	TaskManager::DownloadTaskID task_id;

	DownloadToken(const String &p_url, const String &p_save_path, TaskManager::DownloadTaskID p_task_id) :
			url(p_url), save_path(p_save_path), task_id(p_task_id) {}

	void cancel() {
		TaskManager::get_singleton()->wait_for_download_task_completion(task_id);
	}
};
