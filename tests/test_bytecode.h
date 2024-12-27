#ifndef TEST_BYTECODE_H
#define TEST_BYTECODE_H

#include "../bytecode/bytecode_base.h"
#include "test_common.h"
#include "tests/test_macros.h"

#include <modules/gdscript/gdscript_tokenizer_buffer.h>
#include <utility/common.h>
#include <utility/glob.h>

namespace TestBytecode {

TEST_CASE("[GDSDecomp] GDRESettings works") {
	GDRESettings *settings = GDRESettings::get_singleton();
	CHECK(settings != nullptr);
	CHECK(settings->get_cwd() != "");
	auto cwd = settings->get_cwd();
	auto gdsdecomp_path = get_gdsdecomp_path();
	// check that modules/gdsdecomp exists
	auto da = DirAccess::open(gdsdecomp_path);
	CHECK(da.is_valid());
	CHECK(da->dir_exists(gdsdecomp_path));
}

struct ScriptToRevision {
	const char *script;
	int revision;
};

static const ScriptToRevision tests[] = {
	{ "has_ord", 0x5565f55 },
	{ "has_lerp_angle", 0x6694c11 },
	{ "has_posmod", 0xa60f242 },
	{ "has_move_toward", 0xc00427a },
	{ "has_step_decimals", 0x620ec47 },
	{ "has_is_equal_approx", 0x7f7d97f },
	{ "has_smoothstep", 0x514a3fb },
	{ "has_do", 0x1a36141 },
	{ "has_push_error", 0x1a36141 },
	{ "has_puppetsync", 0xd6b31da },
	{ "has_typed", 0x8aab9a0 },
	{ "has_classname", 0xa3f1ee5 },
	{ "has_slavesync", 0x8e35d93 },
	// { "has_OS.alert_debug", 0x3ea6d9f },
	{ "has_get_stack", 0xa56d6ff },
	{ "has_is_instance_valid", 0xff1e7cf },
	{ "has_cartesian2polar", 0x054a2ac },
	{ "has_tau", 0x91ca725 },
	{ "has_wrapi", 0x216a8aa },
	{ "has_inverse_lerp", 0xd28da86 },
	{ "has_len", 0xc6120e7 },
	{ "has_is", 0x015d36d },
	{ "has_inf", 0x5e938f0 },
	{ "has_wc", 0xc24c739 },
	{ "has_match", 0xf8a7c46 },
	{ "has_to_json", 0x62273e5 },
	{ "has_path", 0x8b912d1 },
	{ "has_colorn", 0x85585c7 },
	{ "has_type_exists", 0x7124599 },
	{ "has_var2bytes", 0x23441ec },
	{ "has_pi", 0x6174585 },
	{ "has_color8", 0x64872ca },
	{ "has_bp", 0x7d2d144 },
	{ "has_onready", 0x30c1229 },
	{ "has_signal", 0x48f1d02 },
	// { "has_OS.alerts", 0x65d48d6 },
	{ "has_instance_from_id", 0xbe46be7 },
	{ "has_var2str", 0x2185c01 },
	{ "has_setget", 0xe82dc40 },
	{ "has_yield", 0x8cab401 },
	{ "has_hash", 0x703004f },
	{ "has_funcref", 0x31ce3c5 },
	{ nullptr, 0 },
};

inline String remove_comments(const String &script_text) {
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

inline void test_script_text(const String &script_name, const String &helper_script_text, int revision, bool check_whitespace_equality) {
	auto decomp = GDScriptDecomp::create_decomp_for_commit(revision);
	CHECK(decomp.is_valid());
	auto bytecode = decomp->compile_code_string(helper_script_text);
	CHECK(bytecode.size() > 0);
	CHECK(decomp->get_error_message() == "");
	auto result = decomp->test_bytecode(bytecode, false);
	// TODO: remove BYTECODE_TEST_UNKNOWN and just make it PASS, there are no proper pass cases now
	CHECK(result == GDScriptDecomp::BYTECODE_TEST_UNKNOWN);
	// test compiling the decompiled code
	Error err = decomp->decompile_buffer(bytecode);
	CHECK(err == OK);
	CHECK(decomp->get_error_message() == "");
	// no whitespace
	auto decompiled_string = decomp->get_script_text();
	CHECK(decompiled_string != "");
	auto helper_script_text_stripped = remove_comments(helper_script_text).replace("\"\"\"", "\"");
	auto decompiled_string_stripped = remove_comments(decompiled_string).replace("\"\"\"", "\"");
#if DEBUG_ENABLED
	if (decompiled_string_stripped != helper_script_text_stripped) {
		// write the script to a temp path
		auto old_path = get_tmp_path().path_join(script_name + ".old.gd");
		auto new_path = get_tmp_path().path_join(script_name + ".new.gd");
		gdre::ensure_dir(get_tmp_path());
		auto fa = FileAccess::open(new_path, FileAccess::WRITE);
		if (fa.is_valid()) {
			fa->store_string(decompiled_string);
			fa->flush();
			fa->close();
			auto fa2 = FileAccess::open(old_path, FileAccess::WRITE);
			if (fa2.is_valid()) {
				fa2->store_string(helper_script_text_stripped);
				fa2->flush();
				fa2->close();
				auto thingy = { String("-u"), old_path, new_path };
				List<String> args;
				for (auto &arg : thingy) {
					args.push_back(arg);
				}
				String pipe;
				OS::get_singleton()->execute("diff", args, &pipe);
				auto temp_path_diff = new_path + ".diff";
				auto fa_diff = FileAccess::open(temp_path_diff, FileAccess::WRITE);
				if (fa_diff.is_valid()) {
					fa_diff->store_string(pipe);
					fa_diff->flush();
					fa_diff->close();
				}
			}
		}
	}
#endif
	CHECK(gdre::remove_whitespace(decompiled_string_stripped) == gdre::remove_whitespace(helper_script_text_stripped));
	if (check_whitespace_equality) {
		CHECK(decompiled_string_stripped == helper_script_text_stripped);
	}
	auto recompiled_bytecode = decomp->compile_code_string(decompiled_string);
	CHECK(decomp->get_error_message() == "");
	CHECK(recompiled_bytecode.size() > 0);
	auto recompiled_result = decomp->test_bytecode(recompiled_bytecode, false);
	CHECK(recompiled_result == GDScriptDecomp::BYTECODE_TEST_UNKNOWN);
	err = decomp->test_bytecode_match(bytecode, recompiled_bytecode);
	CHECK(decomp->get_error_message() == "");
	CHECK(err == OK);
}

inline void test_script(const String &helper_script_path, int revision, bool check_whitespace_equality) {
	// tests are located in modules/gdsdecomp/helpers
	auto da = DirAccess::create_for_path(helper_script_path);
	CHECK(da.is_valid());
	CHECK(da->file_exists(helper_script_path));
	Error err;
	auto helper_script_text = FileAccess::get_file_as_string(helper_script_path, &err);
	CHECK(err == OK);
	CHECK(helper_script_text != "");
	auto script_name = helper_script_path.get_file().get_basename();
	test_script_text(script_name, helper_script_text, revision, check_whitespace_equality);
}

TEST_CASE("[GDSDecomp][Bytecode] Compiling") {
	SUBCASE("GDScriptTokenizer outputs bytecode_version == LATEST_GDSCRIPT_VERSION") {
		auto buf = GDScriptTokenizerBuffer::parse_code_string("", GDScriptTokenizerBuffer::CompressMode::COMPRESS_NONE);
		CHECK(buf.size() >= 8);
		int this_ver = decode_uint32(&buf[4]);
		CHECK(this_ver == GDScriptDecomp::LATEST_GDSCRIPT_VERSION);
	}
	for (int i = 0; tests[i].script != nullptr; i++) {
		auto &script_to_revision = tests[i];
		String sub_case_name = vformat("Testing compiling script %s, revision %08x", String(script_to_revision.script), script_to_revision.revision);
		SUBCASE(sub_case_name.utf8().get_data()) {
			// tests are located in modules/gdsdecomp/helpers
			auto helpers_path = get_gdsdecomp_path().path_join("helpers");
			auto da = DirAccess::open(helpers_path);
			CHECK(da.is_valid());
			CHECK(da->dir_exists(helpers_path));
			auto helper_script_path = helpers_path.path_join(script_to_revision.script) + ".gd";
			test_script(helper_script_path, script_to_revision.revision, true);
		}
	}
	//modules/gdscript/tests/scripts
	String gdscript_tests_path = "modules/gdscript/tests/scripts";
	auto gdscript_test_scripts = Glob::rglob(gdscript_tests_path.path_join("**/*.gd"), true);
	auto gdscript_test_error_scripts = Vector<String>();
	for (int i = 0; i < gdscript_test_scripts.size(); i++) {
		// remove any that contain ".notest." or "/error/"
		auto &script_path = gdscript_test_scripts[i];
		if (script_path.contains(".notest.") || script_path.contains("/error/")) {
			if (script_path.contains("/error/")) {
				gdscript_test_error_scripts.push_back(script_path);
			}
			gdscript_test_scripts.erase(script_path);
			i--;
		}
	}

	for (auto &script_path : gdscript_test_scripts) {
		auto sub_case_name = vformat("Testing compiling script %s", script_path);
		SUBCASE(sub_case_name.utf8().get_data()) {
			test_script(script_path, 0x77af6ca, false);
		}
	}
}

} //namespace TestBytecode

#endif // TEST_BYTECODE_H
