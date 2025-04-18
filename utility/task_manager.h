#include "core/error/error_macros.h"
#include "core/object/worker_thread_pool.h"
#include "utility/gd_parallel_hashmap.h"
#include "utility/gd_parallel_queue.h"
#include "utility/gdre_progress.h"
#include "utility/gdre_settings.h"

class TaskManager : public Object {
	GDCLASS(TaskManager, Object);

public:
	typedef int64_t DownloadTaskID;

	class BaseTemplateTaskData {
	protected:
		std::atomic<bool> canceled = false;
		bool singlethreaded = false;

		virtual void wait_for_task_completion_internal() {}

	public:
		std::atomic<bool> is_waiting = false;
		bool is_canceled() { return canceled; }
		void cancel() { canceled = true; }
		void start() {}

		virtual bool is_done() { return true; }
		virtual String get_current_task_step_description() { return ""; }
		virtual WorkerThreadPool::TaskID get_task_id() { return WorkerThreadPool::TaskID(-1); }
		virtual WorkerThreadPool::GroupID get_group_id() { return WorkerThreadPool::GroupID(-1); }
		virtual void callback() {}
		virtual void callback_indexed(uint32_t p_index) {}
		virtual bool update_progress(bool p_force_refresh = false) { return false; }
		virtual void run_singlethreaded() {}
		// returns true if the task was cancelled before completion
		virtual bool wait_for_completion() { return false; }

		virtual ~BaseTemplateTaskData() {}
	};

	template <typename C, typename M, typename U, typename R>
	class GroupTaskData : public BaseTemplateTaskData {
		C *instance;
		M method;
		U userdata;
		int p_elements = -1;
		R task_step_desc_callback;
		String task;
		String description;
		bool can_cancel = false;
		bool high_priority = false;
		WorkerThreadPool::GroupID group_id = -1;
		Ref<EditorProgressGDDC> progress;
		std::atomic<int64_t> last_completed = -1;

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
				bool p_singlethreaded = false,
				bool p_progress_enabled = true) :
				instance(p_instance),
				method(p_method),
				userdata(p_userdata),
				p_elements(p_elements),
				task_step_desc_callback(p_task_step_callback),
				task(p_task),
				description(p_description),
				can_cancel(p_can_cancel),
				high_priority(p_high_priority) {
			singlethreaded = p_singlethreaded;
			if (singlethreaded) {
				// random group id
				group_id = rand();
			} else {
				group_id = WorkerThreadPool::get_singleton()->add_template_group_task(this, &GroupTaskData::task_callback, userdata, p_elements, p_tasks, p_high_priority, p_description);
			}
			if (p_progress_enabled) {
				progress = EditorProgressGDDC::create(nullptr, task, description, p_elements, can_cancel);
			}
		}

		String get_current_task_step_description() override {
			return (instance->*task_step_desc_callback)(last_completed, userdata);
		}

		void task_callback(uint32_t p_index, U p_userdata) {
			if (unlikely(canceled)) {
				return;
			}
			(instance->*method)(p_index, p_userdata);
			last_completed++;
		}

		void callback_indexed(uint32_t p_index) override {
			task_callback(p_index, userdata);
		}

		WorkerThreadPool::GroupID get_group_id() override {
			return group_id;
		}

		bool is_done() override {
			if (singlethreaded) {
				return last_completed >= p_elements - 1;
			}
			return WorkerThreadPool::get_singleton()->is_group_task_completed(group_id);
		}

		bool update_progress(bool p_force_refresh = false) override {
			if (progress.is_valid() && progress->step(get_current_task_step_description(), last_completed, p_force_refresh)) {
				cancel();
				return true;
			}
			return false;
		}

		void run_singlethreaded() {
			uint64_t last_progress_upd = OS::get_singleton()->get_ticks_usec();
			for (int i = 0; i < p_elements; i++) {
				callback_indexed(i);
				if (OS::get_singleton()->get_ticks_usec() - last_progress_upd > 50000) {
					update_progress();
					last_progress_upd = OS::get_singleton()->get_ticks_usec();
				}
			}
		}

		void wait_for_task_completion_internal() override {
			if (!singlethreaded) {
				WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_id);
			}
		}

		bool wait_for_completion() {
			if (singlethreaded) {
				run_singlethreaded();
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
			return is_canceled();
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

	class DownloadQueueThread {
		Thread *thread = nullptr;
		Mutex mutex;
		std::atomic<bool> running = true;
		std::atomic<bool> waiting = false;
		std::condition_variable cv;
		std::atomic<DownloadTaskID> current_task_id = 0;

		ParallelFlatHashMap<DownloadTaskID, std::shared_ptr<BaseTemplateTaskData>> tasks;
		StaticParallelQueue<DownloadTaskID, 1024> queue;

		void main_loop();
		DownloadTaskID add_download_task(const String &p_download_url, const String &p_save_path);
		Error wait_for_task_completion(DownloadTaskID p_task_id);
		static void thread_func(void *p_userdata);
		DownloadQueueThread();
		~DownloadQueueThread();
	};

protected:
	static TaskManager *singleton;
	ParallelFlatHashMap<WorkerThreadPool::GroupID, std::shared_ptr<BaseTemplateTaskData>> group_id_to_description;
	WorkerThreadPool::TaskID download_task_id = WorkerThreadPool::TaskID(-1);
	Thread *download_thread = nullptr;

public:
	TaskManager();
	~TaskManager();
	static TaskManager *get_singleton();
	template <typename C, typename M, typename U, typename R>
	WorkerThreadPool::GroupID add_group_task(
			C *p_instance,
			M p_method,
			U p_userdata,
			int p_elements,
			R p_task_step_callback,
			const String &p_task,
			const String &p_label, bool p_can_cancel = true, int p_tasks = -1, bool p_high_priority = false) {
		// bool is_singlethreaded = GDRESettings::get_singleton()->get_setting("singlethreaded", false);
		bool is_singlethreaded = false;
		auto task = std::make_shared<GroupTaskData<C, M, U, R>>(p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, p_tasks, p_high_priority, is_singlethreaded);
		auto group_id = task->get_group_id();
		bool already_exists = false;
		group_id_to_description.try_emplace_l(group_id, [&](auto &v) { already_exists = true; }, task);
		if (already_exists) {
			CRASH_COND_MSG(already_exists, "Task already exists?!?!?!");
		}
		return group_id;
	}

	template <typename C, typename M, typename U, typename R>
	Error run_singlethreaded_task(
			C *p_instance,
			M p_method,
			U p_userdata,
			int p_elements,
			R p_task_step_callback,
			const String &p_task,
			const String &p_label, bool p_can_cancel = true) {
		GroupTaskData<C, M, U, R> task = GroupTaskData<C, M, U, R>(p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, -1, true, true);
		return task.wait_for_completion() ? ERR_SKIP : OK;
	}

	Error wait_for_group_task_completion(WorkerThreadPool::GroupID p_group_id);
	void update_progress_bg();
};
