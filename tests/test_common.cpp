#include "test_common.h"
#include "bytecode/bytecode_base.h"
#include "bytecode/bytecode_versions.h"
#include "compat/fake_gdscript.h"
#include "core/os/main_loop.h"
#include "modules/gdscript/gdscript_tokenizer_buffer.h"
#include "servers/audio/audio_server.h"

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

struct GDRETestListener : public doctest::IReporter {
	MainLoop *main_loop = nullptr;
	XRServer *xr_server = nullptr;
	// SignalWatcher *signal_watcher = nullptr;
public:
	GDRETestListener(const doctest::ContextOptions &p_in) {}

	void set_main_loop(MainLoop *p_main_loop) {
		GDRETestOS *os = reinterpret_cast<GDRETestOS *>(OS::get_singleton());
		GDRETestOS::do_set_main_loop(os, p_main_loop);
	}

	void test_case_start(const doctest::TestCaseData &p_in) override {
		String name = String(p_in.m_name);
		String suite_name = String(p_in.m_test_suite);

		GDRESettings::set_is_testing(true);
		if (name.contains("[ProjectRecovery]") || name.contains("[ResourceExport]")) {
			int dummy_idx = AudioDriverManager::get_driver_count() - 1;
			AudioDriverManager::initialize(dummy_idx);
			AudioServer *audio_server = memnew(AudioServer);
			audio_server->init();
			return;
		}
		if (name.contains("[ProjectRecovery]")) {
#ifndef XR_DISABLED
			xr_server = memnew(XRServer);
			CHECK(xr_server != nullptr);
			CHECK(xr_server->get_xr_mode() == XRServer::XRMODE_OFF);
#endif // XR_DISABLED

			main_loop = memnew(MainLoop);

			set_main_loop(main_loop);
		}
	}

	void test_case_end(const doctest::CurrentTestCaseStats &) override {
		GDRESettings::set_is_testing(false);
		if (XRServer::get_singleton() && XRServer::get_singleton() == xr_server) {
			memdelete(xr_server);
		}
		xr_server = nullptr;

		if (OS::get_singleton()->get_main_loop() == main_loop) {
			set_main_loop(nullptr);
		}
		if (main_loop) {
			memdelete(main_loop);
			main_loop = nullptr;
		}
		if (AudioServer::get_singleton()) {
			AudioServer::get_singleton()->finish();
			memdelete(AudioServer::get_singleton());
		}
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
