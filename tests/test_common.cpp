#include "test_common.h"
#include "core/os/main_loop.h"
#include "main/gdre_main_loop.h"
#include "servers/audio/audio_server.h"
#include <utility/gdre_settings.h>

TEST_FORCE_LINK(test_common)

String get_gdsdecomp_path() {
	REQUIRE(GDRESettings::get_singleton());
	return GDRESettings::get_singleton()->get_cwd().path_join("modules/gdsdecomp");
}

String get_tmp_path() {
	return get_gdsdecomp_path().path_join(".tmp");
}

String get_test_resources_path() {
	return get_gdsdecomp_path().path_join("tests/test_files");
}

String get_test_scripts_path() {
	return get_gdsdecomp_path().path_join("tests/test_scripts");
}

String get_gdscript_tests_path() {
	REQUIRE(GDRESettings::get_singleton());
	return GDRESettings::get_singleton()->get_cwd().path_join("modules/gdscript/tests/scripts");
}

String get_gdsdecomp_helpers_path() {
	return get_gdsdecomp_path().path_join("helpers");
}

Vector<String> get_test_versions() {
	Vector<String> versions;
	Ref<DirAccess> da = DirAccess::open(get_test_resources_path());
	da->list_dir_begin();
	while (true) {
		String file = da->get_next();
		if (file.is_empty()) {
			break;
		}
		if (file == "." || file == "..") {
			continue;
		}

		if (da->current_is_dir()) {
			versions.push_back(file);
		}
	}
	return versions;
}

#ifndef XR_DISABLED
#include "servers/xr/xr_server.h"
#endif // XR_DISABLED
#ifdef WINDOWS_ENABLED
#include "platform/windows/os_windows.h"
#define PLATFORM_OS OS_Windows
#endif
#ifdef LINUXBSD_ENABLED
#include "platform/linuxbsd/os_linuxbsd.h"
#define PLATFORM_OS OS_LinuxBSD
#endif
#ifdef MACOS_ENABLED
#include "drivers/unix/os_unix.h"
#define PLATFORM_OS OS_Unix
#endif
#ifdef WEB_ENABLED
#include "platform/web/os_web.h"
#define PLATFORM_OS OS_Web
#endif
#ifdef ANDROID_ENABLED
#include "platform/android/os_android.h"
#define PLATFORM_OS OS_Android
#endif
#ifdef IPHONE_ENABLED
#include "platform/ios/os_ios.h"
#define PLATFORM_OS OS_IOS
#endif

class GDRETestOS : public PLATFORM_OS {
	static_assert(std::is_base_of<OS, PLATFORM_OS>::value, "T must derive from OS");

public:
	static void do_add_logger(GDRETestOS *ptr, Logger *p_logger) {
		ptr->add_logger(p_logger);
	}
	static void do_set_logger(GDRETestOS *ptr, CompositeLogger *p_logger) {
		ptr->_set_logger(p_logger);
	}
	static void do_set_main_loop(GDRETestOS *ptr, MainLoop *p_main_loop) {
		ptr->set_main_loop(p_main_loop);
	}
};

class GDRETestMainLoop : public MainLoop {
	GDCLASS(GDRETestMainLoop, MainLoop);

public:
	void initialize() override {
		GDREMainLoop::get_singleton()->initialize();
		if (SceneTree::get_singleton()) {
			SceneTree::get_singleton()->initialize();
		}
	}
	void iteration_prepare() override {
		GDREMainLoop::get_singleton()->iteration_prepare();
		if (SceneTree::get_singleton()) {
			SceneTree::get_singleton()->iteration_prepare();
		}
	}
	void iteration_end() override {
		GDREMainLoop::get_singleton()->iteration_end();
		if (SceneTree::get_singleton()) {
			SceneTree::get_singleton()->iteration_end();
		}
	}
	bool physics_process(double p_time) override {
		if (GDREMainLoop::get_singleton()->physics_process(p_time)) {
			return true;
		}
		if (SceneTree::get_singleton()) {
			return SceneTree::get_singleton()->physics_process(p_time);
		}
		return false;
	}
	bool process(double p_time) override {
		if (GDREMainLoop::get_singleton()->process(p_time)) {
			return true;
		}
		if (SceneTree::get_singleton()) {
			return SceneTree::get_singleton()->process(p_time);
		}
		return false;
	}
	void finalize() override {
		GDREMainLoop::get_singleton()->finalize();
		if (SceneTree::get_singleton()) {
			SceneTree::get_singleton()->finalize();
		}
	}
};

struct GDRETestListener : public doctest::IReporter {
	GDRETestMainLoop *main_loop = nullptr;
#ifndef XR_DISABLED
	XRServer *xr_server = nullptr;
#endif // XR_DISABLED
	// SignalWatcher *signal_watcher = nullptr;
public:
	GDRETestListener(const doctest::ContextOptions &p_in) {}

	void set_main_loop(MainLoop *p_main_loop) {
		GDRETestOS *os = reinterpret_cast<GDRETestOS *>(OS::get_singleton());
		GDRETestOS::do_set_main_loop(os, p_main_loop);
	}

	void test_case_start(const doctest::TestCaseData &p_in) override {
		GDREMainLoop::set_is_testing(true);
		String name = String(p_in.m_name);
		String suite_name = String(p_in.m_test_suite);
		if (name.contains("[ProjectRecovery]") || name.contains("[FakeScript]")) {
#ifndef XR_DISABLED
			xr_server = memnew(XRServer);
			CHECK(xr_server != nullptr);
			CHECK(xr_server->get_xr_mode() == XRServer::XRMODE_OFF);
#endif // XR_DISABLED
		}
		if (name.contains("[ProjectRecovery]") || name.contains("[ResourceExport]") || name.contains("[FakeScript]")) {
			main_loop = memnew(GDRETestMainLoop);
			main_loop->initialize();
			set_main_loop(main_loop);
			int dummy_idx = AudioDriverManager::get_driver_count() - 1;
			AudioDriverManager::initialize(dummy_idx);
			AudioServer *audio_server = memnew(AudioServer);
			audio_server->init();
			return;
		}
	}

	void test_case_end(const doctest::CurrentTestCaseStats &) override {
#ifndef XR_DISABLED
		if (XRServer::get_singleton() && XRServer::get_singleton() == xr_server) {
			memdelete(xr_server);
		}
		xr_server = nullptr;
#endif // XR_DISABLED

		if (OS::get_singleton()->get_main_loop() == main_loop) {
			set_main_loop(nullptr);
		}
		if (main_loop) {
			main_loop->finalize();
			memdelete(main_loop);
			main_loop = nullptr;
		}
		if (AudioServer::get_singleton()) {
			AudioServer::get_singleton()->finish();
			memdelete(AudioServer::get_singleton());
		}
		GDREMainLoop::set_is_testing(false);
	}

	void test_run_start() override {
		// signal_watcher = memnew(SignalWatcher);
	}

	void test_run_end(const doctest::TestRunStats &) override {
		// memdelete(signal_watcher);
	}

	void test_case_reenter(const doctest::TestCaseData &) override {
		reinitialize();
	}

	void subcase_start(const doctest::SubcaseSignature &) override {
		reinitialize();
	}

	void report_query(const doctest::QueryData &) override {}
	void test_case_exception(const doctest::TestCaseException &) override {}
	void subcase_end() override {}

	void log_assert(const doctest::AssertData &in) override {}
	void log_message(const doctest::MessageData &) override {}
	void test_case_skipped(const doctest::TestCaseData &) override {}

private:
	void reinitialize() {
		// nothing right now
	}
};

String remove_comments(const String &script_text) {
	// gdscripts have comments starting with #, remove them
	auto lines = script_text.split("\n", true);
	auto new_lines = Vector<String>();
	for (int i = 0; i < lines.size(); i++) {
		auto &line = lines.write[i];
		auto comment_pos = line.find("#");
		if (comment_pos != -1) {
			if (line.contains("\"") || line.contains("'")) {
				bool in_quote = false;
				char32_t quote_char = '"';
				comment_pos = -1;
				for (int j = 0; j < line.length(); j++) {
					if (line[j] == '"' || line[j] == '\'') {
						if (in_quote) {
							if (quote_char == line[j]) {
								in_quote = false;
							}
						} else {
							in_quote = true;
							quote_char = line[j];
						}
					} else if (!in_quote && line[j] == '#') {
						comment_pos = j;
						break;
					}
				}
			}
			if (comment_pos != -1) {
				line = line.substr(0, comment_pos).strip_edges(false, true);
			}
		}
		new_lines.push_back(line);
	}
	String new_text;
	for (int i = 0; i < new_lines.size() - 1; i++) {
		new_text += new_lines[i] + "\n";
	}
	new_text += new_lines[new_lines.size() - 1];
	return new_text;
}

REGISTER_LISTENER("GDRETestListener", 2, GDRETestListener);
