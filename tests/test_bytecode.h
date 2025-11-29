#ifndef TEST_BYTECODE_H
#define TEST_BYTECODE_H

#include "../bytecode/bytecode_base.h"
#include "bytecode/bytecode_versions.h"
#include "bytecode/gdscript_tokenizer_compat.h"
#include "core/io/image.h"
#include "core/math/quaternion.h"
#include "modules/gdscript/gdscript_tokenizer.h"
#include "test_common.h"
#include "tests/test_macros.h"
#include <compat/fake_gdscript.h>
#include <compat/resource_compat_text.h>
#include <compat/resource_loader_compat.h>

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

static constexpr const char *test_unique_id_modulo = R"(
extends AnimationPlayer

func _ready() -> void :
    %BlinkTimer.timeout.connect(check_for_blink)
    var thingy = 10 % 3
    var thingy2 = 10 % thingy
    var thingy3 = thingy % 20
)";

// should pass on all versions of GDScript
static constexpr const char *test_reserved_word_as_accessor_name = R"(
extends Object


func _ready():
	var thingy = {}
	thingy["func"] = "bar"
	thingy["enum"] = "foo"
	thingy["preload"] = "foo"
	thingy["yield"] = "foo"
	thingy["sin"] = "foo"
	thingy["static"] = "foo"
	thingy["pass"] = "foo"
	foo.sin()
	print(thingy.func)
	print(thingy.enum)
	print(thingy.preload)
	print(thingy.yield)
	print(thingy.sin)
	print(thingy.static)
	print(thingy.pass)
)";

// clang-format off
static constexpr const char *test_eof_newline = R"(
extends RefCounted
func _ready():
	pass
	)";
// clang-format on

inline void test_script_binary(const String &script_name, const Vector<uint8_t> &bytecode, const String &helper_script_text, int revision, bool helper_script, bool no_text_equality_check, bool compare_whitespace = false) {
	auto decomp = GDScriptDecomp::create_decomp_for_commit(revision);
	CHECK(decomp.is_valid());
	auto result = decomp->test_bytecode(bytecode, false);
	CHECK(result == GDScriptDecomp::BYTECODE_TEST_PASS);
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
	CHECK(recompiled_result == GDScriptDecomp::BYTECODE_TEST_PASS);
	err = decomp->test_bytecode_match(bytecode, recompiled_bytecode);
#if DEBUG_ENABLED
	if (err) {
		TextDiff::print_diff(TextDiff::get_diff_with_header(script_name + "_original", script_name + "_decompiled", helper_script_text_stripped, decompiled_string_stripped));
		output_diff(script_name, decompiled_string_stripped, helper_script_text_stripped);
	}
#endif

	if (revision == GDScriptDecompVersion::LATEST_GDSCRIPT_COMMIT) {
		// test with the latest GDScriptTokenizer
		auto reference_result = GDScriptTokenizerBuffer::parse_code_string(helper_script_text, GDScriptTokenizerBuffer::CompressMode::COMPRESS_ZSTD);
		CHECK(reference_result.size() > 0);
		err = decomp->test_bytecode_match(bytecode, reference_result);
		CHECK(err == OK);
	}
	CHECK(decomp->get_error_message() == "");
	CHECK(err == OK);

	Ref<FakeGDScript> fake_script = memnew(FakeGDScript);
	fake_script->set_override_bytecode_revision(revision);
	CHECK(fake_script->get_override_bytecode_revision() == revision);
	fake_script->set_source_code(helper_script_text);
	CHECK(fake_script->is_loaded());
	CHECK(fake_script->get_error_message() == "");
	fake_script->load_binary_tokens(bytecode);
	CHECK(fake_script->is_loaded());
	CHECK(fake_script->get_error_message() == "");
	CHECK(fake_script->get_override_bytecode_revision() == revision);

	// if (decomp->get_bytecode_version() <= GDScriptDecomp::GDSCRIPT_2_0_VERSION) {
	// 	auto tokenizer = GDScriptTokenizerBufferCompat(decomp.ptr());
	// 	tokenizer.set_code_buffer(bytecode);
	// 	auto token = tokenizer.scan();
	// 	while (token.type != GDScriptDecomp::G_TK_EOF) {
	// 		print_line(vformat("Token: '%s', Line: %d, Column: %d, Indent: %d, Function: %s, Error: %s", GDScriptTokenizerBufferCompat::get_token_name(token.type), token.line, token.col, token.current_indent, token.func_name, token.error));
	// 		token = tokenizer.scan();
	// 	}
	// 	bool thignas = false;
	// }
}

inline void test_script_text(const String &script_name, const String &helper_script_text, int revision, bool helper_script, bool no_text_equality_check, bool compare_whitespace = false) {
	auto decomp = GDScriptDecomp::create_decomp_for_commit(revision);
	CHECK(decomp.is_valid());
	auto bytecode = decomp->compile_code_string(helper_script_text);
	auto compile_error_message = decomp->get_error_message();
	CHECK(compile_error_message == "");
	CHECK(bytecode.size() > 0);
	test_script_binary(script_name, bytecode, helper_script_text, revision, helper_script, no_text_equality_check, compare_whitespace);
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

TEST_CASE("[GDSDecomp][Bytecode][GDScript1.0] Compiling Helper Scripts") {
	for (int i = 0; tests[i].script != nullptr; i++) {
		auto &script_to_revision = tests[i];
		String sub_case_name = vformat("Testing compiling script %s, revision %07x", String(script_to_revision.script), script_to_revision.revision);
		SUBCASE(sub_case_name.utf8().get_data()) {
			// tests are located in modules/gdsdecomp/helpers
			auto helpers_path = get_gdsdecomp_helpers_path();
			auto da = DirAccess::open(helpers_path);
			CHECK(da.is_valid());
			CHECK(da->dir_exists(helpers_path));
			auto helper_script_path = helpers_path.path_join(script_to_revision.script) + ".gd";
			test_script(helper_script_path, script_to_revision.revision, true, false);
		}
	}
}

TEST_CASE("[GDSDecomp][Bytecode][GDScript2.0] Compiling GDScript Tests") {
	REQUIRE(GDRESettings::get_singleton());
	auto cwd = GDRESettings::get_singleton()->get_cwd();
	String gdscript_tests_path = get_gdscript_tests_path();
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

	for (int64_t i = 0; i < gdscript_test_scripts.size(); i++) {
		auto &script_path = gdscript_test_scripts[i];
		auto sub_case_name = vformat("Testing compiling script %s", script_path);
		SUBCASE(sub_case_name.utf8().get_data()) {
			test_script(script_path, 0x77af6ca, false, true);
			test_script(script_path, GDScriptDecompVersion::LATEST_GDSCRIPT_COMMIT, false, true);
		}
	}
}

TEST_CASE("[GDSDecomp][Bytecode][GDScript2.0] Test unique_id modulo operator") {
	test_script_text("test_unique_id_modulo", test_unique_id_modulo, 0x77af6ca, false, false, true);
	test_script_text("test_unique_id_modulo", test_unique_id_modulo, GDScriptDecompVersion::LATEST_GDSCRIPT_COMMIT, false, false, true);
}

TEST_CASE("[GDSDecomp][Bytecode] Test sample GDScript bytecode") {
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);

	for (const String &version : versions) {
		auto decomp = GDScriptDecomp::create_decomp_for_version(version);
		CHECK(decomp.is_valid());
		int revision = decomp->get_bytecode_rev();
		String test_dir = get_test_resources_path().path_join(version).path_join("code");
		Vector<String> files = gdre::get_recursive_dir_list(test_dir, { "*.gdc" });
		String output_dir = get_tmp_path().path_join(version).path_join("code");
		for (const String &file : files) {
			auto sub_case_name = vformat("Testing compiling script %s, version %s", file, version);
			SUBCASE(sub_case_name.utf8().get_data()) {
				String original_file = file.get_basename() + ".gd";

				Vector<uint8_t> bytecode = FileAccess::get_file_as_bytes(file);
				String original_script_text = FileAccess::get_file_as_string(original_file);
				test_script_binary(original_file, bytecode, original_script_text, revision, false, true);
			}
		}
	}
}

void simple_pass_fail_test(const String &script_name, const String &helper_script_text, int revision, bool expect_fail) {
	SUBCASE(vformat("Testing %s, revision %07x", script_name, revision).utf8().get_data()) {
		auto decomp = GDScriptDecomp::create_decomp_for_commit(revision);
		CHECK(decomp.is_valid());
		auto bytecode = decomp->compile_code_string(helper_script_text);
		CHECK(decomp->get_error_message() == "");
		CHECK(bytecode.size() > 0);
		auto result = decomp->test_bytecode(bytecode, false);
		if (!expect_fail) {
			CHECK(decomp->get_error_message() == "");
			CHECK(result == GDScriptDecomp::BYTECODE_TEST_PASS);
		} else {
			CHECK(result == GDScriptDecomp::BYTECODE_TEST_FAIL);
		}
	}
}

TEST_CASE("[GDSDecomp][Bytecode] Test reserved words as global function names") {
	// get all the decomp versions for 2.x and 3.x (GDScript 1.0)

	Vector<GDScriptDecompVersion> versions = GDScriptDecompVersion::get_decomp_versions(true, 0);

	static constexpr const char *test_global_function_name = R"(
extends Object

func %s():
	pass
)";
	static constexpr const char *func_call_fragment = R"(
func _ready():
	%s()
)";

	// TODO: We should test all reserved words to see if they're being used in a function declaration in _test_bytecode
	Vector<Pair<bool, String>> keywords_to_test = {
		// { true, "func" },
		{ true, "enum" },
		// { false, "preload" },
		// { false, "yield" },
		// { false, "sin" },
		{ true, "static" },
		// { false, "pass" },
	};

	for (int i = 0; i < keywords_to_test.size(); i++) {
		auto &keyword = keywords_to_test[i];
		String test_name = keyword.second;
		String test_script = vformat(test_global_function_name, test_name);
		if (keyword.first) {
			test_script += vformat(func_call_fragment, test_name);
		}
		for (const GDScriptDecompVersion &version : versions) {
			int revision = version.commit;
			bool expect_fail = version.bytecode_version >= GDScriptDecomp::GDSCRIPT_2_0_VERSION;

			simple_pass_fail_test(test_name, test_script, revision, expect_fail);
		}
	}
}

TEST_CASE("[GDSDecomp][Bytecode] Test reserved words as member function names") {
	static constexpr const char *test_member_function_name = R"(
extends Object

class test_class:
	func %s():
		pass
)";
	static constexpr const char *func_call_fragment = R"(
func _ready():
	var test = test_class.new()
	test.%s()
)";

	auto versions = GDScriptDecompVersion::get_decomp_versions(true, 0);
	Vector<Pair<bool, String>> keywords_to_test = {
		// { true, "func" },
		{ true, "enum" },
		// { false, "preload" },
		// { false, "yield" },
		// { false, "sin" },
		{ true, "static" },
		// { false, "pass" },
	};

	for (int i = 0; i < keywords_to_test.size(); i++) {
		auto &keyword = keywords_to_test[i];
		String test_name = keyword.second;
		String test_script = vformat(test_member_function_name, test_name);
		if (keyword.first) {
			test_script += vformat(func_call_fragment, test_name);
		}
		for (const GDScriptDecompVersion &version : versions) {
			int revision = version.commit;
			bool expect_fail = version.bytecode_version >= GDScriptDecomp::GDSCRIPT_2_0_VERSION;

			simple_pass_fail_test(test_name, test_script, revision, expect_fail);
		}
	}
}

TEST_CASE("[GDSDecomp][Bytecode] Test reserved words as accessor names") {
	auto versions = GDScriptDecompVersion::get_decomp_versions(true, 0);
	for (const GDScriptDecompVersion &version : versions) {
		int revision = version.commit;
		simple_pass_fail_test("all", test_reserved_word_as_accessor_name, revision, false);
	}
}

TEST_CASE("[GDSDecomp][Bytecode][Create] Test creating custom decomp") {
	REQUIRE(GDRESettings::get_singleton());
	auto cwd = GDRESettings::get_singleton()->get_cwd();
	String gdscript_tests_path = get_gdscript_tests_path();
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

	GDScriptDecompVersion ver = GDScriptDecompVersion::create_derived_version_from_custom_def(GDScriptDecompVersion::LATEST_GDSCRIPT_COMMIT, Dictionary());
	CHECK(!ver.name.is_empty());
	int revision = GDScriptDecompVersion::register_decomp_version_custom(ver.custom);
	CHECK(revision != 0);
	Ref<GDScriptDecomp> decomp = GDScriptDecompVersion::create_decomp_for_commit(revision);
	CHECK(decomp.is_valid());

	for (int64_t i = 0; i < gdscript_test_scripts.size(); i++) {
		auto &script_path = gdscript_test_scripts[i];
		auto sub_case_name = vformat("Testing compiling script %s", script_path);
		SUBCASE(sub_case_name.utf8().get_data()) {
			test_script(script_path, revision, false, true);
		}
	}
}

TEST_CASE("[GDSDecomp][Bytecode][EOFNewline] Test indented newline at EOF") {
	auto versions = GDScriptDecompVersion::get_decomp_versions(true, 0);
	for (const GDScriptDecompVersion &version : versions) {
		int revision = version.commit;
		test_script_text("indented_newline_at_eof", test_eof_newline, revision, false, true, false);
	}
}

} //namespace TestBytecode

#endif // TEST_BYTECODE_H
