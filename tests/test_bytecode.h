#ifndef TEST_BYTECODE_H
#define TEST_BYTECODE_H

#include "../bytecode/bytecode_base.h"
#include "bytecode/bytecode_versions.h"
#include "core/io/image.h"
#include "test_common.h"
#include "tests/test_macros.h"

#include "core/version_generated.gen.h"
#include <modules/gdscript/gdscript_tokenizer_buffer.h>
#include <utility/common.h>
#include <utility/glob.h>

namespace TestBytecode {

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

static const char *test_unique_id_modulo = R"(
extends AnimationPlayer

func _ready() -> void :
    %BlinkTimer.timeout.connect(check_for_blink)
    var thingy = 10 % 3
    var thingy2 = 10 % thingy
    var thingy3 = thingy % 20
)";

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

inline void test_script_text(const String &script_name, const String &helper_script_text, int revision, bool helper_script, bool no_text_equality_check, bool compare_whitespace = false) {
	auto decomp = GDScriptDecomp::create_decomp_for_commit(revision);
	CHECK(decomp.is_valid());
	auto bytecode = decomp->compile_code_string(helper_script_text);
	auto compile_error_message = decomp->get_error_message();
	CHECK(compile_error_message == "");
	CHECK(bytecode.size() > 0);
	auto result = decomp->test_bytecode(bytecode, false);
	// TODO: remove BYTECODE_TEST_UNKNOWN and just make it PASS, there are no proper pass cases now
	CHECK(result == GDScriptDecomp::BYTECODE_TEST_UNKNOWN);
	if (helper_script && decomp->get_parent() != 0) {
		// Test our previously compiled bytecode against the parent
		auto parent = GDScriptDecomp::create_decomp_for_commit(decomp->get_parent());
		CHECK(parent.is_valid());
		// Can't test be46be7, the only thing that changed in this commit is the name of the function
		if (revision != 0xbe46be7) {
			auto parent_result = parent->test_bytecode(bytecode, false);
			CHECK(parent_result == GDScriptDecomp::BYTECODE_TEST_FAIL);
		}
	}

	// test compiling the decompiled code
	Error err = decomp->decompile_buffer(bytecode);
	CHECK(err == OK);
	CHECK(decomp->get_error_message() == "");
	// no whitespace
	auto decompiled_string = decomp->get_script_text();
	auto helper_script_text_stripped = remove_comments(helper_script_text).replace("\"\"\"", "\"").replace("'", "\"");
	if (!helper_script_text_stripped.strip_edges().is_empty()) {
		CHECK(decompiled_string != "");
	}

	auto decompiled_string_stripped = remove_comments(decompiled_string).replace("\"\"\"", "\"").replace("'", "\"");

#if DEBUG_ENABLED
	if (!no_text_equality_check && gdre::remove_whitespace(decompiled_string_stripped) != gdre::remove_whitespace(helper_script_text_stripped)) {
		TextDiff::print_diff(TextDiff::get_diff_with_header(script_name, script_name, decompiled_string_stripped, helper_script_text_stripped));
		output_diff(script_name, decompiled_string_stripped, helper_script_text_stripped);
	}
#endif
	if (!no_text_equality_check) {
		CHECK_MESSAGE(gdre::remove_whitespace(decompiled_string_stripped) == gdre::remove_whitespace(helper_script_text_stripped), (String("No whitespace text diff failed: \n") + TextDiff::get_diff_with_header(script_name, script_name, decompiled_string_stripped, helper_script_text_stripped)));
	}
	if (compare_whitespace) {
		if (decompiled_string_stripped != helper_script_text_stripped) {
			TextDiff::print_diff(TextDiff::get_diff_with_header(script_name + "_original", script_name + "_decompiled", helper_script_text_stripped, decompiled_string_stripped));
		}
		CHECK(decompiled_string_stripped == helper_script_text_stripped);
	}
	auto recompiled_bytecode = decomp->compile_code_string(decompiled_string);
	CHECK(decomp->get_error_message() == "");
	CHECK(recompiled_bytecode.size() > 0);
	auto recompiled_result = decomp->test_bytecode(recompiled_bytecode, false);
	CHECK(recompiled_result == GDScriptDecomp::BYTECODE_TEST_UNKNOWN);
	err = decomp->test_bytecode_match(bytecode, recompiled_bytecode);
#if DEBUG_ENABLED
	if (err) {
		TextDiff::print_diff(TextDiff::get_diff_with_header(script_name + "_original", script_name + "_decompiled", helper_script_text_stripped, decompiled_string_stripped));
		output_diff(script_name, decompiled_string_stripped, helper_script_text_stripped);
	}
#endif

	if (revision == LATEST_GDSCRIPT_COMMIT) {
		// test with the latest GDScriptTokenizer
		auto reference_result = GDScriptTokenizerBuffer::parse_code_string(helper_script_text, GDScriptTokenizerBuffer::CompressMode::COMPRESS_ZSTD);
		CHECK(reference_result.size() > 0);
		err = decomp->test_bytecode_match(bytecode, reference_result);
		CHECK(err == OK);
	}
	CHECK(decomp->get_error_message() == "");
	CHECK(err == OK);
}

inline void test_script(const String &helper_script_path, int revision, bool helper_script, bool no_text_equality_check) {
	// tests are located in modules/gdsdecomp/helpers
	auto da = DirAccess::create_for_path(helper_script_path);
	CHECK(da.is_valid());
	CHECK(da->file_exists(helper_script_path));
	Error err;
	auto helper_script_text = FileAccess::get_file_as_string(helper_script_path, &err);
	CHECK(err == OK);
	CHECK(helper_script_text != "");
	auto script_name = helper_script_path.get_file().get_basename();
	test_script_text(script_name, helper_script_text, revision, helper_script, no_text_equality_check);
}

TEST_CASE("[GDSDecomp][Bytecode] GDScriptTokenizer outputs bytecode_version == LATEST_GDSCRIPT_VERSION") {
	auto buf = GDScriptTokenizerBuffer::parse_code_string("", GDScriptTokenizerBuffer::CompressMode::COMPRESS_NONE);
	CHECK(buf.size() >= 8);
	int this_ver = decode_uint32(&buf[4]);
	CHECK(this_ver == GDScriptDecomp::LATEST_GDSCRIPT_VERSION);
}

TEST_CASE("[GDSDecomp][Bytecode] Bytecode for current engine version has same number of tokens") {
	auto decomp = GDScriptDecomp::create_decomp_for_version(vformat("%d.%d.%d", GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR, GODOT_VERSION_PATCH));
	CHECK(decomp.is_valid());
	CHECK(decomp->get_token_max() == GDScriptTokenizerBuffer::Token::TK_MAX);
}

TEST_CASE("[GDSDecomp][Bytecode] Compiling Helper Scripts") {
	for (int i = 0; tests[i].script != nullptr; i++) {
		auto &script_to_revision = tests[i];
		String sub_case_name = vformat("Testing compiling script %s, revision %07x", String(script_to_revision.script), script_to_revision.revision);
		SUBCASE(sub_case_name.utf8().get_data()) {
			// tests are located in modules/gdsdecomp/helpers
			auto helpers_path = get_gdsdecomp_path().path_join("helpers");
			auto da = DirAccess::open(helpers_path);
			CHECK(da.is_valid());
			CHECK(da->dir_exists(helpers_path));
			auto helper_script_path = helpers_path.path_join(script_to_revision.script) + ".gd";
			test_script(helper_script_path, script_to_revision.revision, true, false);
		}
	}
}

TEST_CASE("[GDSDecomp][Bytecode][GDScript2.0] Compiling GDScript Tests") {
	auto cwd = GDRESettings::get_singleton()->get_cwd();
	String gdscript_tests_path = GDRESettings::get_singleton()->get_cwd().path_join("modules/gdscript/tests/scripts");
	auto gdscript_test_scripts = Glob::rglob(gdscript_tests_path.path_join("**/*.gd"), true);
	auto gdscript_test_error_scripts = Vector<String>();
	for (int i = 0; i < gdscript_test_scripts.size(); i++) {
		// remove any that contain ".notest." or "/error/"
		auto script_path = gdscript_test_scripts[i].trim_prefix(cwd + "/");
		if (script_path.contains(".notest.") || script_path.contains("error") || script_path.contains("completion")) {
			gdscript_test_error_scripts.push_back(script_path);
			gdscript_test_scripts.erase(gdscript_test_scripts[i]);
			i--;
		} else {
			gdscript_test_scripts.write[i] = script_path;
		}
	}

	for (size_t i = 0; i < gdscript_test_scripts.size(); i++) {
		auto &script_path = gdscript_test_scripts[i];
		auto sub_case_name = vformat("Testing compiling script %s", script_path);
		SUBCASE(sub_case_name.utf8().get_data()) {
			test_script(script_path, 0x77af6ca, false, true);
			test_script(script_path, LATEST_GDSCRIPT_COMMIT, false, true);
		}
	}
}

TEST_CASE("[GDSDecomp][Bytecode][GDScript2.0] Test unique_id modulo operator") {
	test_script_text("test_unique_id_modulo", test_unique_id_modulo, 0x77af6ca, false, false, true);
	test_script_text("test_unique_id_modulo", test_unique_id_modulo, LATEST_GDSCRIPT_COMMIT, false, false, true);
}

} //namespace TestBytecode

#endif // TEST_BYTECODE_H
