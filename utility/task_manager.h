#pragma once
#include "core/error/error_macros.h"
#include "core/object/worker_thread_pool.h"
#include "gui/gdre_progress.h"
#include "utility/gd_parallel_hashmap.h"
#include "utility/gd_parallel_queue.h"
#include "utility/gdre_config.h"

struct TaskRunnerStruct {
	virtual int get_current_task_step_value() = 0;
	virtual String get_current_task_step_description() = 0;
	virtual void cancel() = 0;
	virtual void run(void *p_userdata) = 0;
	virtual bool auto_close_progress_bar() { return false; }
};

class TaskManager : public Object {
	GDCLASS(TaskManager, Object);

public:
	typedef int64_t DownloadTaskID;
	typedef int64_t TaskManagerID;

	class BaseTemplateTaskData {
	protected:
		bool dont_update_progress_bg = false;
		bool canceled = false;
		bool runs_current_thread = false;
		bool started = false;
		bool auto_start = true;
		bool progress_enabled = true;
		bool timed_out = false;
		bool _aborted = false;
		Ref<EditorProgressGDDC> progress;

		virtual void wait_for_task_completion_internal() = 0;
		virtual void start_internal() = 0;
		virtual void cancel_internal() {}
		bool _wait_after_cancel();
		String _get_task_description();
		bool wait_update_progress(bool p_force_refresh = false);

	public:
		std::atomic<bool> is_waiting = false;

		virtual bool is_done() const = 0;
		virtual int get_current_task_step_value() = 0;
		virtual String get_current_task_step_description() = 0;
		virtual void run_on_current_thread() = 0;
		virtual bool auto_close_progress_bar() { return false; }

		void start();
		bool is_started() const;
		bool is_canceled() const;
		void cancel();
		void finish_progress();
		bool is_progress_enabled() const;
		// returns true if the task was cancelled before completion
		bool update_progress(bool p_force_refresh = false);
		bool is_timed_out() const;
		bool _is_aborted() const;

		virtual bool wait_for_completion(uint64_t timeout_s_no_progress = 0);

		virtual ~BaseTemplateTaskData();
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
		WorkerThreadPool::GroupID group_id = -1;
		WorkerThreadPool::TaskID task_id = WorkerThreadPool::TaskID(-1);
		std::atomic<int64_t> last_completed = 0;
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
				progress_start(p_progress_start) {
			progress_enabled = p_progress_enabled;
			progress = p_progress;
			runs_current_thread = p_runs_current_thread;
		}

		String get_current_task_step_description() override {
			if (elements == 0) {
				return "<UNKNOWN>";
			}
			return (instance->*task_step_desc_callback)(last_completed < elements ? (int)last_completed : elements - 1, userdata);
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

		bool is_done() const override {
			if (runs_current_thread) {
				return last_completed >= elements;
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
			if (canceled) {
				return;
			}
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

	template <typename U>
	class TaskData : public BaseTemplateTaskData {
		std::shared_ptr<TaskRunnerStruct> cb_struct;
		U userdata;
		String description;
		bool can_cancel = false;
		bool high_priority = false;
		bool done = false;
		WorkerThreadPool::TaskID task_id = WorkerThreadPool::TaskID(-1);
		int progress_step_count = -1;

	protected:
		virtual void wait_for_task_completion_internal() override {
			if (!runs_current_thread) {
				WorkerThreadPool::get_singleton()->wait_for_task_completion(task_id);
			} else {
				run_on_current_thread();
			}
		}
		virtual void cancel_internal() override {
			cb_struct->cancel();
		}

	public:
		TaskData(
				std::shared_ptr<TaskRunnerStruct> task_cancelled_callback,

				U p_userdata,
				const String &p_description,
				int progress_steps = -1,
				bool p_can_cancel = false,
				bool p_high_priority = false,
				bool p_progress_enabled = true,
				bool p_runs_current_thread = false) :
				cb_struct(task_cancelled_callback),
				userdata(p_userdata),
				description(p_description),
				can_cancel(p_can_cancel),
				high_priority(p_high_priority),
				progress_step_count(progress_steps) {
			progress_enabled = p_progress_enabled;
			runs_current_thread = p_runs_current_thread;
		}

		void run_internal(void *p_data) {
			cb_struct->run(p_data);
			done = true;
		}
		virtual void run_on_current_thread() override {
			if (canceled) {
				return;
			}
			run_internal(userdata);
		}
		virtual int get_current_task_step_value() override {
			return cb_struct->get_current_task_step_value();
		}
		virtual String get_current_task_step_description() override {
			return cb_struct->get_current_task_step_description();
		}
		virtual bool is_done() const override {
			return done;
		}
		void start_internal() override {
			if (task_id != -1) {
				return;
			}
			if (runs_current_thread) {
				// random group id
				task_id = abs(rand());
			} else {
				task_id = WorkerThreadPool::get_singleton()->add_template_task(this, &TaskData::run_internal, userdata, high_priority, description);
			}
			if (progress_enabled && progress.is_null()) {
				progress = EditorProgressGDDC::create(nullptr, description + itos(task_id), description, progress_step_count, can_cancel);
				if (progress_step_count == -1) {
					progress->set_progress_length(true);
				}
			}
		}

		virtual bool auto_close_progress_bar() override {
			return cb_struct->auto_close_progress_bar();
		}
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
		virtual bool is_done() const override;
		virtual void cancel_internal() override;
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
	ParallelFlatHashMap<TaskManagerID, std::shared_ptr<BaseTemplateTaskData>> group_id_to_description;
	DownloadQueueThread download_thread;
	std::atomic<TaskManagerID> current_task_id = 0;
	bool updating_bg = false;

public:
	TaskManager();
	~TaskManager();
	static TaskManager *get_singleton();
	template <typename C, typename M, typename U, typename R>
	TaskManagerID add_group_task(
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
		ERR_FAIL_COND_V_MSG(p_elements == 0, -1, "Task has 0 elements, this is not allowed!");
		bool is_singlethreaded = GDREConfig::get_singleton()->get_setting("force_single_threaded", false);
		auto task = std::make_shared<GroupTaskData<C, M, U, R>>(p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, p_tasks, p_high_priority, is_singlethreaded, true, p_preexisting_progress, p_progress_start);
		task->start();
		auto group_id = ++current_task_id;
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
		ERR_FAIL_COND_V_MSG(p_elements == 0, ERR_INVALID_PARAMETER, "Task has 0 elements, this is not allowed!");
		auto task_id = add_group_task(p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, p_tasks, p_high_priority, p_preexisting_progress, p_progress_start);
		return wait_for_task_completion(task_id);
	}

	template <typename C, typename M, typename U, typename R>
	Error run_group_task_on_current_thread(
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
		ERR_FAIL_COND_V_MSG(p_elements == 0, ERR_INVALID_PARAMETER, "Task has 0 elements, this is not allowed!");
		GroupTaskData<C, M, U, R> task = GroupTaskData<C, M, U, R>(p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, -1, true, true, true, p_preexisting_progress, p_progress_start);
		task.start();
		return task.wait_for_completion() ? ERR_SKIP : OK;
	}

	template <typename U>
	TaskManagerID add_task(
			std::shared_ptr<TaskRunnerStruct> p_task_runner,
			U p_userdata,
			const String &p_description,
			int progress_steps = -1,
			bool p_can_cancel = false,
			bool p_high_priority = false,
			bool p_progress_enabled = true,
			bool p_runs_current_thread = false) {
		auto task = std::make_shared<TaskData<U>>(p_task_runner, p_userdata, p_description, progress_steps, p_can_cancel, p_high_priority, p_progress_enabled, p_runs_current_thread);
		task->start();
		bool already_exists = false;
		auto task_id = ++current_task_id;
		group_id_to_description.try_emplace_l(task_id, [&](auto &v) { already_exists = true; }, task);
		if (already_exists) {
			CRASH_COND_MSG(already_exists, "Task already exists?!?!?!");
		}
		return task_id;
	}

	template <typename U>
	Error run_task(
			std::shared_ptr<TaskRunnerStruct> p_task_runner,
			U p_userdata,
			const String &p_description,
			int progress_steps = -1,
			bool p_can_cancel = false,
			bool p_high_priority = false,
			bool p_progress_enabled = true) {
		auto task_id = add_task(p_task_runner, p_userdata, p_description, progress_steps, p_can_cancel, p_high_priority, p_progress_enabled);
		return wait_for_task_completion(task_id);
	}

	DownloadTaskID add_download_task(const String &p_download_url, const String &p_save_path, bool silent = false);
	Error wait_for_download_task_completion(DownloadTaskID p_task_id);
	Error wait_for_task_completion(TaskManagerID p_task_id, uint64_t timeout_s_no_progress = 0);
	bool is_current_task_completed(TaskManagerID p_task_id) const;
	bool is_current_task_canceled();
	bool is_current_task_timed_out();
	bool update_progress_bg(bool p_force_refresh = false, bool called_from_process = false);
	void cancel_all();
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
