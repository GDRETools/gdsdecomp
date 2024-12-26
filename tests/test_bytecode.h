#ifndef TEST_BYTECODE_H
#define TEST_BYTECODE_H

#include "../bytecode/bytecode_base.h"
#include "../utility/gdre_settings.h"
#include "core/io/marshalls.h"
#include "tests/test_macros.h"

#include <modules/gdscript/gdscript_tokenizer_buffer.h>
#include <utility/common.h>

namespace TestBytecode {

// Uitility functions for finding differences in noise generation

String get_gdsdecomp_path() {
	return GDRESettings::get_singleton()->get_cwd().path_join("modules/gdsdecomp");
}

String get_tmp_path() {
	return get_gdsdecomp_path().path_join(".tmp");
}

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
			CHECK(da->file_exists(helper_script_path));
			Error err;
			auto helper_script_text = FileAccess::get_file_as_string(helper_script_path, &err);
			CHECK(err == OK);
			auto decomp = GDScriptDecomp::create_decomp_for_commit(script_to_revision.revision);
			CHECK(decomp.is_valid());
			auto bytecode = decomp->compile_code_string(helper_script_text);
			CHECK(bytecode.size() > 0);
			CHECK(decomp->get_error_message() == "");
			auto result = decomp->test_bytecode(bytecode, false);
			// TODO: remove BYTECODE_TEST_UNKNOWN and just make it PASS, there are no proper pass cases now
			CHECK(result == GDScriptDecomp::BYTECODE_TEST_UNKNOWN);
			// test compiling the decompiled code
			err = decomp->decompile_buffer(bytecode);
			CHECK(err == OK);
			CHECK(decomp->get_error_message() == "");
			// no whitespace
			auto decompiled_string = decomp->get_script_text();
			CHECK(decompiled_string != "");
#if DEBUG_ENABLED
			if (decompiled_string != helper_script_text) {
				// write the script to a temp path
				auto temp_path = get_tmp_path().path_join(script_to_revision.script) + ".gd";
				gdre::ensure_dir(get_tmp_path());
				auto fa = FileAccess::open(temp_path, FileAccess::WRITE);
				if (fa.is_valid()) {
					fa->store_string(decompiled_string);
					fa->flush();
					fa->close();
					Vector<String> dasgd{ "foo", "bar" };
					auto thingy = { String("-u"), helper_script_path, get_tmp_path().path_join(script_to_revision.script) + ".gd" };
					List<String> args;
					for (auto &arg : thingy) {
						args.push_back(arg);
					}
					String pipe;
					OS::get_singleton()->execute("diff", args, &pipe);
					auto temp_path_diff = temp_path + ".diff";
					auto fa_diff = FileAccess::open(temp_path_diff, FileAccess::WRITE);
					if (fa_diff.is_valid()) {
						fa_diff->store_string(pipe);
						fa_diff->flush();
						fa_diff->close();
					}
				}
			}
#endif
			CHECK(gdre::remove_whitespace(decompiled_string) == gdre::remove_whitespace(helper_script_text));
			CHECK(decompiled_string == helper_script_text);
			// test recompiling the decompiled code
			auto recompiled_bytecode = decomp->compile_code_string(decompiled_string);
			CHECK(recompiled_bytecode.size() > 0);
			CHECK(decomp->get_error_message() == "");
			auto recompiled_result = decomp->test_bytecode(recompiled_bytecode, false);
			CHECK(result == GDScriptDecomp::BYTECODE_TEST_UNKNOWN);
			CHECK(recompiled_bytecode == bytecode);
		}
	}
}

} //namespace TestBytecode

#endif // TEST_BYTECODE_H
