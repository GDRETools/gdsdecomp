#pragma once
#include "core/error/error_macros.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"

#include "gui/gdre_progress.h"
#include "utility/gd_parallel_hashmap.h"
#include "utility/gd_parallel_queue.h"
#include "utility/gdre_config.h"

#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

struct TaskRunnerStruct {
	virtual bool pre_run() { return true; }
	virtual int get_current_task_step_value() = 0;
	virtual String get_current_task_step_description() = 0;
	virtual void cancel() = 0;
	virtual void run(void *p_userdata) = 0;
	virtual bool auto_close_progress_bar() { return false; }
};

class GDREMainLoop;

class TaskManager : public Object {
	GDCLASS(TaskManager, Object);

public:
	typedef int64_t DownloadTaskID;
	typedef int64_t TaskManagerID;

	static int64_t maximum_memory_usage;

	static inline bool is_memory_usage_too_high() {
		return (int64_t)OS::get_singleton()->get_static_memory_usage() > TaskManager::maximum_memory_usage;
	}

	class BaseMainThreadDispatchData {
		TaskManager::TaskManagerID calling_task_id;
		Semaphore semaphore;
		bool canceled = false;

	protected:
		virtual void callback_internal() = 0;

	public:
		BaseMainThreadDispatchData(TaskManager::TaskManagerID p_calling_task_id) :
				calling_task_id(p_calling_task_id) {}
		virtual ~BaseMainThreadDispatchData() = default;
		void callback() {
			callback_internal();
			semaphore.post();
		}
		void wait_for_completion() {
			semaphore.wait();
		}
		void cancel() {
			canceled = true;
			semaphore.post();
		}
		bool is_canceled() const {
			return canceled;
		}
		TaskManager::TaskManagerID get_calling_task_id() const {
			return calling_task_id;
		}
	};

	template <typename R, bool IsVoid = std::is_void_v<R>>
	class MainThreadDispatchResultStorage;

	template <typename R>
	class MainThreadDispatchResultStorage<R, false> {
		std::optional<std::decay_t<R>> result;

	public:
		template <typename F>
		void execute(F &&p_callback) {
			result = std::forward<F>(p_callback)();
		}

		std::optional<std::decay_t<R>> take_result() {
			return std::move(result);
		}
	};

	template <typename R>
	class MainThreadDispatchResultStorage<R, true> {
	public:
		template <typename F>
		void execute(F &&p_callback) {
			std::forward<F>(p_callback)();
		}
	};

	template <typename R>
	using MainThreadDispatchReturn = std::optional<std::conditional_t<std::is_void_v<R>, bool, std::decay_t<R>>>;

	// This is a helper class to dispatch a callback to the main thread.
	template <typename C, typename M, typename U>
	class TemplateMainThreadDispatchData : public BaseMainThreadDispatchData {
		using ReturnType = std::invoke_result_t<M, C *, U>;

		C *instance;
		M method;
		U userdata;
		MainThreadDispatchResultStorage<ReturnType> result_storage;

	public:
		TemplateMainThreadDispatchData(TaskManager::TaskManagerID p_calling_task_id, C *p_instance, M p_method, U p_userdata) :
				BaseMainThreadDispatchData(p_calling_task_id),
				instance(p_instance),
				method(p_method),
				userdata(p_userdata) {}
		void callback_internal() override {
			result_storage.execute([this]() -> ReturnType {
				return std::invoke(method, instance, userdata);
			});
		}

		template <typename T = ReturnType, typename = std::enable_if_t<!std::is_void_v<T>>>
		std::optional<std::decay_t<T>> take_result() {
			return result_storage.take_result();
		}
	};

	// This is a helper class to dispatch a callback to the main thread.
	template <typename R, typename... Args>
	class FunctionMainThreadDispatchData : public BaseMainThreadDispatchData {
		std::function<R(Args...)> function;
		std::tuple<std::decay_t<Args>...> userdata;
		MainThreadDispatchResultStorage<R> result_storage;

	public:
		FunctionMainThreadDispatchData(TaskManager::TaskManagerID p_calling_task_id, std::function<R(Args...)> p_function, std::decay_t<Args>... p_userdata) :
				BaseMainThreadDispatchData(p_calling_task_id),
				function(std::move(p_function)),
				userdata(std::move(p_userdata)...) {}
		void callback_internal() override {
			result_storage.execute([this]() -> R {
				return std::apply(function, userdata);
			});
		}

		template <typename T = R, typename = std::enable_if_t<!std::is_void_v<T>>>
		std::optional<std::decay_t<T>> take_result() {
			return result_storage.take_result();
		}
	};

	class BaseTemplateTaskData {
	protected:
		bool not_in_main_queue = false;
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

	class BaseGroupTaskData : public BaseTemplateTaskData {
		TaskManagerID task_manager_id;
		int elements = -1;
		int tasks = -1;
		String task;
		String description;
		bool can_cancel = false;
		bool high_priority = false;
		std::atomic<int64_t> last_completed = 0;
		std::atomic<int64_t> current_work_index = 0;
		std::atomic<int64_t> tasks_busy_waiting = 0;
		std::atomic<int64_t> tasks_quit = 0;
		Vector<WorkerThreadPool::TaskID> task_ids;
		bool started_current_thread = false;
		bool done = false;
		int progress_start = 0;

	protected:
		virtual String get_current_task_step_description_internal(int p_index) = 0;
		virtual void call_instance_method_internal(int p_work_index) = 0;
		bool _do_one_iteration(bool p_no_memory_check = false);
		void task_callback(void *_unused);

	public:
		BaseGroupTaskData(
				TaskManagerID p_task_manager_id,
				int p_elements,
				const String &p_task,
				const String &p_description,
				bool p_can_cancel,
				int p_tasks,
				bool p_high_priority,
				bool p_runs_current_thread = false,
				bool p_progress_enabled = true,
				Ref<EditorProgressGDDC> p_progress = nullptr,
				int p_progress_start = 0);

		String get_current_task_step_description() override;
		void start_internal() override;
		bool is_done() const override;
		int get_current_task_step_value() override;
		void run_on_current_thread() override;
		void wait_for_task_completion_internal() override;
	};

	template <typename C, typename M, typename U, typename R>
	class GroupTaskData : public BaseGroupTaskData {
		C *instance;
		M method;
		U userdata;
		R task_step_desc_callback;

	protected:
		virtual String get_current_task_step_description_internal(int p_index) override {
			return (instance->*task_step_desc_callback)(p_index, userdata);
		}
		virtual void call_instance_method_internal(int p_work_index) override {
			(instance->*method)(p_work_index, userdata);
		}

	public:
		GroupTaskData(
				TaskManagerID p_task_manager_id,
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
				BaseGroupTaskData(p_task_manager_id, p_elements, p_task, p_description, p_can_cancel, p_tasks, p_high_priority, p_runs_current_thread, p_progress_enabled, p_progress, p_progress_start),
				instance(p_instance),
				method(p_method),
				userdata(p_userdata),
				task_step_desc_callback(p_task_step_callback) {
		}
	};

	template <typename U>
	class TaskData : public BaseTemplateTaskData {
		TaskManagerID task_manager_id;
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
				ERR_FAIL_COND_MSG(task_id == -1, "Task not started");
				TaskManager::get_thread_pool()->wait_for_task_completion(task_id);
			} else {
				run_on_current_thread();
			}
		}
		virtual void cancel_internal() override {
			cb_struct->cancel();
		}

	public:
		TaskData(
				TaskManagerID p_task_manager_id,
				std::shared_ptr<TaskRunnerStruct> task_cancelled_callback,
				U p_userdata,
				const String &p_description,
				int progress_steps = -1,
				bool p_can_cancel = false,
				bool p_high_priority = false,
				bool p_progress_enabled = true,
				bool p_runs_current_thread = false) :
				task_manager_id(p_task_manager_id),
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
			if (canceled || done) {
				return;
			}
			TaskManager::get_singleton()->set_thread_task_id(task_manager_id);
			cb_struct->run(p_data);
			TaskManager::get_singleton()->set_thread_task_id(-1);
			done = true;
		}
		virtual void run_on_current_thread() override {
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
			if (task_id != -1 || done || canceled) {
				return;
			}
			if (!cb_struct->pre_run()) {
				done = true;
				ERR_FAIL_MSG("Failed to pre-run task: " + description);
			}
			if (runs_current_thread) {
				// random group id
				task_id = abs(rand());
			} else {
				task_id = TaskManager::get_thread_pool()->add_template_task(this, &TaskData::run_internal, userdata, high_priority, description);
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
		int64_t size = -1;
		int64_t start_time = 0;
		Vector<int64_t> speed_history;
		bool write_sha256 = false;
		bool silent = false;
		float download_progress = 0.0f;
		Error download_error = OK;
		bool done = false;

	protected:
		virtual void wait_for_task_completion_internal() override;

	public:
		bool fake = false;
		DownloadTaskData(const String &p_download_url, const String &p_save_path, bool p_silent = false, bool p_write_sha256 = false, bool p_fake = false);

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
		DownloadTaskID add_fake_download_task(const String &p_download_url, const String &p_save_path);
		DownloadTaskID add_download_task(const String &p_download_url, const String &p_save_path, bool silent, bool write_sha256);
		Error wait_for_task_completion(DownloadTaskID p_task_id);
		DownloadQueueThread();
		~DownloadQueueThread();
	};

protected:
	static TaskManager *singleton;
	ParallelFlatHashMap<TaskManagerID, std::shared_ptr<BaseTemplateTaskData>> group_id_to_description;
	ParalellQueue<std::shared_ptr<BaseMainThreadDispatchData>> main_thread_dispatch_queue;
	DownloadQueueThread download_thread;
	WorkerThreadPool *named_pool = nullptr;
	std::atomic<TaskManagerID> current_task_id = 0;
	TaskManagerID main_thread_task_id = -1;
	LocalVector<Thread::ID> thread_index_to_thread_id;
	LocalVector<TaskManagerID> thread_index_to_task_ids;
	bool processing_main_thread_dispatch_queue = false;

	friend class GDREMainLoop;
	bool updating_bg = false;

	void _set_thread_name_task(uint32_t i, void *p_userdata);
	bool _dispatch_to_main_thread(std::shared_ptr<BaseMainThreadDispatchData> p_data);

	void process_main_thread_dispatch_queue_for(int64_t time_usec);
	void cancel_main_thread_dispatch_queue();

public:
	TaskManager();
	~TaskManager();
	static TaskManager *get_singleton();

	static int get_max_thread_count();

	static WorkerThreadPool *get_thread_pool();

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
			int p_progress_start = 0,
			bool p_show_progress = true) {
		ERR_FAIL_COND_V_MSG(p_elements == 0, -1, "Task has 0 elements, this is not allowed!");
		bool is_singlethreaded = GDREConfig::get_singleton()->get_setting("force_single_threaded", false);
		if (p_tasks <= 0) {
			p_tasks = (int)GDREConfig::get_singleton()->get_setting("max_cores_to_use", -1);
			if (p_tasks <= 0) {
				p_tasks = MAX(1, get_max_thread_count() - 1);
			}
		}
		auto group_id = ++current_task_id;

		auto task = std::make_shared<GroupTaskData<C, M, U, R>>(group_id,
				p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, p_tasks, p_high_priority,
				is_singlethreaded,
				p_show_progress, p_preexisting_progress, p_progress_start);
		task->start();
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
			int p_progress_start = 0,
			bool p_show_progress = true) {
		ERR_FAIL_COND_V_MSG(p_elements == 0, ERR_INVALID_PARAMETER, "Task has 0 elements, this is not allowed!");
		auto task_id = add_group_task(p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, p_tasks, p_high_priority, p_preexisting_progress, p_progress_start, p_show_progress);
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
			int p_progress_start = 0,
			bool p_show_progress = true) {
		ERR_FAIL_COND_V_MSG(p_elements == 0, ERR_INVALID_PARAMETER, "Task has 0 elements, this is not allowed!");
		auto group_id = ++current_task_id;

		GroupTaskData<C, M, U, R> task = GroupTaskData<C, M, U, R>(group_id, p_instance, p_method, p_userdata, p_elements, p_task_step_callback, p_task, p_label, p_can_cancel, -1, true, true, p_show_progress, p_preexisting_progress, p_progress_start);
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
		auto task_id = ++current_task_id;
		auto task = std::make_shared<TaskData<U>>(task_id, p_task_runner, p_userdata, p_description, progress_steps, p_can_cancel, p_high_priority, p_progress_enabled, p_runs_current_thread);
		task->start();
		bool already_exists = false;

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
		bool is_singlethreaded = GDREConfig::get_singleton()->get_setting("force_single_threaded", false);
		auto task_id = add_task(p_task_runner, p_userdata, p_description, progress_steps, p_can_cancel, p_high_priority, p_progress_enabled, is_singlethreaded);
		return wait_for_task_completion(task_id);
	}

	template <typename C, typename M, typename U>
	MainThreadDispatchReturn<std::invoke_result_t<M, C *, U>> dispatch_to_main_thread(C *p_instance, M p_method, U p_userdata) {
		using ReturnType = std::invoke_result_t<M, C *, U>;
		auto data = std::make_shared<TemplateMainThreadDispatchData<C, M, U>>(get_thread_task_id(), p_instance, p_method, p_userdata);
		bool canceled = _dispatch_to_main_thread(data);
		if (canceled) {
			return std::nullopt;
		}
		if constexpr (std::is_void_v<ReturnType>) {
			return true;
		} else {
			return data->take_result();
		}
	}

	template <typename R, typename... Args>
	MainThreadDispatchReturn<R> dispatch_to_main_thread(std::function<R(Args...)> func, std::decay_t<Args>... userdata) {
		auto data = std::make_shared<FunctionMainThreadDispatchData<R, Args...>>(get_thread_task_id(), std::move(func), std::move(userdata)...);
		bool canceled = _dispatch_to_main_thread(data);
		if (canceled) {
			return std::nullopt;
		}
		if constexpr (std::is_void_v<R>) {
			return true;
		} else {
			return data->take_result();
		}
	}

	DownloadTaskID add_fake_download_task(const String &p_download_url, const String &p_save_path);
	DownloadTaskID add_download_task(const String &p_download_url, const String &p_save_path, bool silent = false, bool write_sha256 = false);
	Error wait_for_download_task_completion(DownloadTaskID p_task_id);
	Error wait_for_task_completion(TaskManagerID p_task_id, uint64_t timeout_s_no_progress = 0);
	bool is_current_task_completed(TaskManagerID p_task_id) const;
	bool is_current_task_canceled();
	bool is_current_task_timed_out();
	bool update_progress_bg(bool p_force_refresh = false, bool called_from_process = false, bool *r_did_redraw = nullptr, bool *r_has_tasks = nullptr);
	void set_thread_task_id(TaskManagerID p_task_manager_id);
	TaskManagerID get_thread_task_id() const;
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
