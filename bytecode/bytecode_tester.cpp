#include "bytecode/bytecode_tester.h"
#include "bytecode/bytecode_1a36141.h"
#include "bytecode/bytecode_1ca61a3.h"
#include "bytecode/bytecode_514a3fb.h"
#include "bytecode/bytecode_7124599.h"
#include "bytecode/bytecode_85585c7.h"
#include "bytecode/bytecode_base.h"
#include "bytecode/bytecode_ed80f45.h"
#include "bytecode/bytecode_versions.h"

#include "core/io/file_access.h"
#include "utility/gdre_settings.h"
#include "utility/godotver.h"

/***********2.1 testing ********
 discontintuities in the functions for bytecode 10 starts here (-1 means varargs):


| 23441ec          | Args | 7124599          | Args | 85585c7          | Args | ed80f45          | Args |
| ---------------- | ---- | ---------------- | ---- | ---------------- | ---- | ---------------- | ---- |
| typeof           | 1    | typeof           | 1    | typeof           | 1    | typeof           | 1    |
| str              | -1   | type_exists      | 1    | type_exists      | 1    | type_exists      | 1    |
| print            | -1   | str              | -1   | str              | -1   | str              | -1   |
| printt           | -1   | print            | -1   | print            | -1   | print            | -1   |
| prints           | -1   | printt           | -1   | printt           | -1   | printt           | -1   |
| printerr         | -1   | prints           | -1   | prints           | -1   | prints           | -1   |
| printraw         | -1   | printerr         | -1   | printerr         | -1   | printerr         | -1   |
| var2str          | 1    | printraw         | -1   | printraw         | -1   | printraw         | -1   |
| str2var          | 1    | var2str          | 1    | var2str          | 1    | var2str          | 1    |
| var2bytes        | 1    | str2var          | 1    | str2var          | 1    | str2var          | 1    |
| bytes2var        | 1    | var2bytes        | 1    | var2bytes        | 1    | var2bytes        | 1    |
| range            | -1   | bytes2var        | 1    | bytes2var        | 1    | bytes2var        | 1    |
| load             | 1    | range            | -1   | range            | -1   | range            | -1   |
| inst2dict        | 1    | load             | 1    | load             | 1    | load             | 1    |
| dict2inst        | 1    | inst2dict        | 1    | inst2dict        | 1    | inst2dict        | 1    |
| hash             | 1    | dict2inst        | 1    | dict2inst        | 1    | dict2inst        | 1    |
| Color8           | 3    | hash             | 1    | hash             | 1    | hash             | 1    |
| print_stack      | 0    | Color8           | 3    | Color8           | 3    | Color8           | 3    |
| instance_from_id | 1    | print_stack      | 0    | ColorN           | 1-2  | ColorN           | 1-2  |
|                  |      | instance_from_id | 1    | print_stack      | 0    | print_stack      | 0    |
|                  |      |                  |      | instance_from_id | 1    | instance_from_id | 1    |


Discontinuities in the token types for bytecode 10 start here:
( "space" means that there can be newlines between this token and the next )
| 85585c7  tkn           | next                         | space | ed80f45                | next                                   | space |
| ---------------------- | --------------------------   | ----- | ---------------------- | -------------------------------------- | ----- |
| TK_PR_VAR              | TK_IDENTIFIER                | FALSE | TK_PR_VAR              | TK_IDENTIFIER                          |       |
| TK_PR_PRELOAD          | TK_PARENTHESIS_OPEN          | FALSE | TK_PR_ENUM             | TK_IDENTIFIER OR TK_CURLY_BRACKET_OPEN |       |
| TK_PR_ASSERT           | TK_PARENTHESIS_OPEN          | TRUE  | TK_PR_PRELOAD          | TK_PARENTHESIS_OPEN                    | FALSE |
| TK_PR_YIELD            | TK_PARENTHESIS_OPEN          | FALSE | TK_PR_ASSERT           | TK_PARENTHESIS_OPEN                    | TRUE  |
| TK_PR_SIGNAL           | TK_PARENTHESIS_OPEN          | FALSE | TK_PR_YIELD            | TK_PARENTHESIS_OPEN                    | FALSE |
| TK_PR_BREAKPOINT       | N/A (not in release exports) | FALSE | TK_PR_SIGNAL           | TK_PARENTHESIS_OPEN                    | FALSE |
| TK_BRACKET_OPEN        | N/A                          | FALSE | TK_PR_BREAKPOINT       | N/A (not in release exports)           | FALSE |
| TK_BRACKET_CLOSE       | N/A                          | FALSE | TK_BRACKET_OPEN        | N/A                                    | FALSE |
| TK_CURLY_BRACKET_OPEN  | N/A                          | FALSE | TK_BRACKET_CLOSE       | N/A                                    | FALSE |
| TK_CURLY_BRACKET_CLOSE | N/A                          | FALSE | TK_CURLY_BRACKET_OPEN  | N/A                                    | FALSE |
| TK_PARENTHESIS_OPEN    | N/A                          | FALSE | TK_CURLY_BRACKET_CLOSE | N/A                                    | FALSE |
| TK_PARENTHESIS_CLOSE   | N/A                          | FALSE | TK_PARENTHESIS_OPEN    | N/A                                    | FALSE |
| TK_COMMA               | N/A                          | FALSE | TK_PARENTHESIS_CLOSE   | N/A                                    | FALSE |
| TK_SEMICOLON           | N/A                          | FALSE | TK_COMMA               | N/A                                    | FALSE |
| TK_PERIOD              | see below                    | FALSE | TK_SEMICOLON           | N/A                                    | FALSE |
| TK_QUESTION_MARK       | no semantic meaning in 2.x   | FALSE | TK_PERIOD              | see below                              | FALSE |
| TK_COLON               | N/A                          | FALSE | TK_QUESTION_MARK       | no semantic meaning in 2.x             | FALSE |
| TK_NEWLINE             | N/A                          | FALSE | TK_COLON               | N/A                                    | FALSE |
| TK_CONST_PI            | N/A                          | FALSE | TK_NEWLINE             | N/A                                    | FALSE |
| TK_ERROR               | INVALID                      | FALSE | TK_CONST_PI            | N/A                                    | FALSE |
| TK_EOF                 | INVALID                      | FALSE | TK_ERROR               | INVALID                                | FALSE |
| TK_CURSOR              | INVALID                      | FALSE | TK_EOF                 | INVALID                                | FALSE |
| TK_MAX                 | INVALID                      | FALSE | TK_CURSOR              | INVALID                                | FALSE |
|                        |                              |       | TK_MAX                 | INVALID                                | FALSE |

TODO: Have yet to add this to the version 2.1 testing
TK_PERIOD cases (all of bytecode 10):

if preceding is TK_PARENTHESIS_CLOSE, then TK_PARENTHESIS_OPEN, TK_IDENTIFIER, or TK_BUILT_IN_FUNC must follow
if preceding is TK_BUILT_IN_TYPE, then TK_IDENTIFIER must follow
otherwise, TK_IDENTIFIER or TK_BUILT_IN_FUNC must follow

* special case for TK_PR_EXTENDS statements:
  They made a mistake while writing the parser, and you can technically have multiple periods between identifiers if you're parsing an extends statement.
  So we must copy the _parse_extends logic from Godot 2.x to validate those statements.
	The TK_PR_EXTENDS parser does not allow for question marks in TK_PERIOD, so if we find a TK_PERIOD, then we know it's ed80f45.

*/

/***********3.1 testing********

(For sanities sake, we're only going to be testing the beta and release versions of 3.1)

built-in func divergence for bytecode version 13 (3.1.x only)

| 1ca61a3 (3.1 beta) | args  | 1a36141 (3.1.0)   | args  | 514a3fb (3.1.1)   | args  |
| ------------------ | ----- | ----------------- | ----- | ----------------- | ----- |
| range_lerp         | 5     | range_lerp        | 5     | range_lerp        | 5     |
| dectime            | 3     | dectime           | 3     | smoothstep        | 3     |
| randomize          | 0     | randomize         | 0     | dectime           | 3     |
| randi              | 0     | randi             | 0     | randomize         | 0     |
| randf              | 0     | randf             | 0     | randi             | 0     |
| rand_range         | 2     | rand_range        | 2     | randf             | 0     |
| seed               | 1     | seed              | 1     | rand_range        | 2     |
| rand_seed          | 1     | rand_seed         | 1     | seed              | 1     |
| deg2rad            | 1     | deg2rad           | 1     | rand_seed         | 1     |
| rad2deg            | 1     | rad2deg           | 1     | deg2rad           | 1     |
| linear2db          | 1     | linear2db         | 1     | rad2deg           | 1     |
| db2linear          | 1     | db2linear         | 1     | linear2db         | 1     |
| polar2cartesian    | 2     | polar2cartesian   | 2     | db2linear         | 1     |
| cartesian2polar    | 2     | cartesian2polar   | 2     | polar2cartesian   | 2     |
| wrapi              | 3     | wrapi             | 3     | cartesian2polar   | 2     |
| wrapf              | 3     | wrapf             | 3     | wrapi             | 3     |
| max                | 2     | max               | 2     | wrapf             | 3     |
| min                | 2     | min               | 2     | max               | 2     |
| clamp              | 3     | clamp             | 3     | min               | 2     |
| nearest_po2        | 1     | nearest_po2       | 1     | clamp             | 3     |
| weakref            | 1     | weakref           | 1     | nearest_po2       | 1     |
| funcref            | 2     | funcref           | 2     | weakref           | 1     |
| convert            | 2     | convert           | 2     | funcref           | 2     |
| typeof             | 1     | typeof            | 1     | convert           | 2     |
| type_exists        | 1     | type_exists       | 1     | typeof            | 1     |
| char               | 1     | char              | 1     | type_exists       | 1     |
| str                | -1    | str               | -1    | char              | 1     |
| print              | -1    | print             | -1    | str               | -1    |
| printt             | -1    | printt            | -1    | print             | -1    |
| prints             | -1    | prints            | -1    | printt            | -1    |
| printerr           | -1    | printerr          | -1    | prints            | -1    |
| printraw           | -1    | printraw          | -1    | printerr          | -1    |
| print_debug        | -1    | print_debug       | -1    | printraw          | -1    |
| push_error         | 1     | push_error        | 1     | print_debug       | -1    |
| push_warning       | 1     | push_warning      | 1     | push_error        | 1     |
| var2str            | 1     | var2str           | 1     | push_warning      | 1     |
| str2var            | 1     | str2var           | 1     | var2str           | 1     |
| var2bytes          | 1 - 2 | var2bytes         | 1 - 2 | str2var           | 1     |
| bytes2var          | 1 - 2 | bytes2var         | 1 - 2 | var2bytes         | 1 - 2 |
| range              | 1 - 3 | range             | 1 - 3 | bytes2var         | 1 - 2 |
| load               | 1     | load              | 1     | range             | 1 - 3 |
| inst2dict          | 1     | inst2dict         | 1     | load              | 1     |
| dict2inst          | 1     | dict2inst         | 1     | inst2dict         | 1     |
| validate_json      | 1     | validate_json     | 1     | dict2inst         | 1     |
| parse_json         | 1     | parse_json        | 1     | validate_json     | 1     |
| to_json            | 1     | to_json           | 1     | parse_json        | 1     |
| hash               | 1     | hash              | 1     | to_json           | 1     |
| Color8             | 3     | Color8            | 3     | hash              | 1     |
| ColorN             | 1 - 2 | ColorN            | 1 - 2 | Color8            | 3     |
| print_stack        | 0     | print_stack       | 0     | ColorN            | 1 - 2 |
| get_stack          | 0     | get_stack         | 0     | print_stack       | 0     |
| instance_from_id   | 1     | instance_from_id  | 1     | get_stack         | 0     |
| len                | 1     | len               | 1     | instance_from_id  | 1     |
| is_instance_valid  | 1     | is_instance_valid | 1     | len               | 1     |
|                    |       |                   |       | is_instance_valid | 1     |


	discontinuities between tokens in bytecode 13 (3.1 only) start here:
// TODO: add this
*/

int BytecodeTester::get_bytecode_version(const Vector<String> &bytecode_files) {
	int bytecode_version = 0;
	if (bytecode_files.size() == 0) {
		return bytecode_version;
	}
	for (const String &file : bytecode_files) {
		int this_ver = 0;
		if (file.get_extension().to_lower() == "gde") {
			this_ver = GDScriptDecomp::read_bytecode_version_encrypted(file, 3, GDRESettings::get_singleton()->get_encryption_key());
		} else {
			this_ver = GDScriptDecomp::read_bytecode_version(file);
		}
		if (this_ver == -2) {
			return -2; // encryption error
		}
		if (this_ver == -1) {
			// WARN_PRINT("Could not read bytecode version from file: " + file);
			continue;
		}
		if (bytecode_version == 0) {
			bytecode_version = this_ver;
		} else if (this_ver != bytecode_version) {
			// Hell no.
			return -1;
		}
	}
	return bytecode_version;
}

uint64_t BytecodeTester::generic_test(const Vector<String> &p_paths, int ver_major_hint, int ver_minor_hint, bool include_dev, bool print_log_on_fail) {
	int detected_bytecode_version = get_bytecode_version(p_paths);
	ERR_FAIL_COND_V_MSG(detected_bytecode_version == -1, {}, "Inconsistent byecode versions across files!!!");
	ERR_FAIL_COND_V_MSG(detected_bytecode_version <= 0, {}, "Could not read bytecode version from files.");

	Vector<Ref<GDScriptDecomp>> decomp_versions = BytecodeTester::get_possible_decomps(p_paths, include_dev);
	if (decomp_versions.size() == 1) {
		// easy
		return decomp_versions[0]->get_bytecode_rev();
	}
	if (decomp_versions.size() == 0) {
		if (!include_dev) {
			// try again with the dev versions
			return generic_test(p_paths, ver_major_hint, ver_minor_hint, true, print_log_on_fail);
		}
		// else fail
		if (print_log_on_fail) {
			// run the tests with print_verbose = true to put out a decent error log of what happened.
			BytecodeTester::get_possible_decomps(p_paths, include_dev, true);
		}
		ERR_FAIL_V_MSG(0, "Failed to detect GDScript revision for bytecode version " + vformat("%d", detected_bytecode_version) + ", engine version " + vformat("%d.%d", ver_major_hint, ver_minor_hint) + ", please report this issue on GitHub.");
	}

	// otherwise...
	auto get_candidate_string = [&](const Ref<GDScriptDecomp> &decomp) -> String {
		return vformat("%s (%07x)", decomp->get_engine_version().utf8().get_data(), decomp->get_bytecode_rev());
	};
	auto get_candidates_string = [&](const Vector<Ref<GDScriptDecomp>> &decomps) -> String {
		String candidates_str;
		for (int i = 0; i < decomps.size(); i++) {
			candidates_str += get_candidate_string(decomps[i]);
			if (i < decomps.size() - 1) {
				candidates_str += ", ";
			}
		}
		return candidates_str;
	};
	auto candidates = BytecodeTester::filter_decomps(decomp_versions, ver_major_hint, ver_minor_hint);

	if (candidates.size() == 1) {
		if (print_log_on_fail) {
			print_line("Multiple passing candidates for bytecode version " + vformat("%d", detected_bytecode_version) + ":\n" + get_candidates_string(decomp_versions) + "\nChoosing only one that matches engine version: " + get_candidate_string(candidates[0]) + ".");
		}
		return candidates[0]->get_bytecode_rev();
	}
	ERR_FAIL_V_MSG(0, "Failed to detect GDScript revision for bytecode version " + vformat("%d", detected_bytecode_version) + ", engine version " + vformat("%d.%d", ver_major_hint, ver_minor_hint) + ", candidates: " + get_candidates_string(decomp_versions) + ".");
	// TODO: Smarter handling for this
}

uint64_t BytecodeTester::test_files_2_1(const Vector<String> &p_paths) {
	uint64_t rev = 0;
	bool ed80f45_failed = false;
	bool _85585c7_failed = false;
	bool _7124599_failed = false;
	Ref<GDScriptDecomp_ed80f45> decomp_ed80f45 = memnew(GDScriptDecomp_ed80f45);
	Ref<GDScriptDecomp_85585c7> decomp_85585c7 = memnew(GDScriptDecomp_85585c7);
	Ref<GDScriptDecomp_7124599> decomp_7124599 = memnew(GDScriptDecomp_7124599);
	int func_max = 0;
	int token_max = 0;
	for (String path : p_paths) {
		Vector<uint8_t> data = FileAccess::get_file_as_bytes(path);
		if (data.size() == 0) {
			continue;
		}
		if (!ed80f45_failed) {
			auto test_result = decomp_ed80f45->_test_bytecode(data, token_max, func_max);
			switch (test_result) {
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_FAIL:
					ed80f45_failed = true;
					break;
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_CORRUPT:
					WARN_PRINT("BYTECODE_TEST_CORRUPT test result for ed80f45, script " + path);
					ed80f45_failed = true;
					break;
				default:
					break;
			}
		}
		if (!_85585c7_failed) {
			auto test_result = decomp_85585c7->_test_bytecode(data, token_max, func_max);
			switch (test_result) {
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_FAIL:
					_85585c7_failed = true;
					break;
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_CORRUPT:
					WARN_PRINT("BYTECODE_TEST_CORRUPT test result for 85585c7, script " + path);
					_85585c7_failed = true;
					break;
				default:
					break;
			}
		}
		if (!_7124599_failed) {
			auto test_result = decomp_7124599->_test_bytecode(data, token_max, func_max);
			switch (test_result) {
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_FAIL:
					_7124599_failed = true;
					break;
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_CORRUPT:
					WARN_PRINT("BYTECODE_TEST_CORRUPT test result for 7124599, script " + path);
					_7124599_failed = true;
					break;
				default:
					break;
			}
		}
		if (rev != 0) {
			break;
		}
		if (ed80f45_failed && _85585c7_failed && _7124599_failed) {
			// Welp
			break;
		}
	}
	if (rev == 0) {
		if (!ed80f45_failed) {
			if (_7124599_failed && _85585c7_failed) {
				rev = 0xed80f45;
			}
		} else if (ed80f45_failed) {
			// it's quite possible for both 85585c7 and 7124599 to not fail, as 85585c7 is testing ColorN, which came towards the end of the builtin_func list
			// and our pass cases are pretty limited there.
			// if the game doesn't use 	"ColorN", "print_stack", or "instance_from_id", neither will fail.
			int colorn_idx = decomp_85585c7->get_function_index("ColorN");
			if (func_max >= colorn_idx && !_7124599_failed && !_85585c7_failed) {
				// We got incredibly unlucky.
				// Neither failed but yet it uses functions past the shift
				// We can't safely decompile this with either bytecode version, so fail.
				ERR_FAIL_V_MSG(0, "UNLUCKY!! Failed to detect GDScript revision for engine version 2.1.x, please report this issue on GitHub");
			} else if (!_85585c7_failed) {
				rev = 0x85585c7;
				// otherwise, it's probably 7124599
			} else if (!_7124599_failed) {
				rev = 0x7124599;
			}
		}
		if (rev == 0) {
			// try it with the dev versions.
			return BytecodeTester::generic_test(p_paths, 2, 1, true, true);
		}
	}
	return rev;
}

uint64_t BytecodeTester::test_files_3_1(const Vector<String> &p_paths, const Vector<uint8_t> &p_key) {
	uint64_t rev = 0;
	bool _514a3fb_failed = false;
	bool _1a36141_failed = false;
	bool _1ca61a3_failed = false;

	Ref<GDScriptDecomp_514a3fb> decomp_514a3fb = memnew(GDScriptDecomp_514a3fb);
	Ref<GDScriptDecomp_1a36141> decomp_1a36141 = memnew(GDScriptDecomp_1a36141);
	Ref<GDScriptDecomp_1ca61a3> decomp_1ca61a3 = memnew(GDScriptDecomp_1ca61a3);
	int func_max = 0;
	int token_max = 0;

	for (String path : p_paths) {
		Vector<uint8_t> data;
		if (p_key.size() > 0) {
			Error err = GDScriptDecomp::get_buffer_encrypted(path, 3, p_key, data);
			ERR_FAIL_COND_V_MSG(err == ERR_UNAUTHORIZED, 0, "Failed to decrypt file " + path + " (Did you set the correct key?)");
			ERR_FAIL_COND_V_MSG(err != OK, 0, "Failed to read file " + path);
		} else {
			data = FileAccess::get_file_as_bytes(path);
		}
		if (data.size() == 0) {
			continue;
		}
		if (!_514a3fb_failed) {
			auto test_result = decomp_514a3fb->_test_bytecode(data, token_max, func_max);
			switch (test_result) {
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_FAIL:
					_514a3fb_failed = true;
					break;
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_CORRUPT:
					WARN_PRINT("BYTECODE_TEST_CORRUPT test result for 514a3fb, script " + path);
					_514a3fb_failed = true;
					break;
				default:
					break;
			}
		}
		if (!_1a36141_failed) {
			auto test_result = decomp_1a36141->_test_bytecode(data, token_max, func_max);
			switch (test_result) {
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_FAIL:
					_1a36141_failed = true;
					break;
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_CORRUPT:
					WARN_PRINT("BYTECODE_TEST_CORRUPT test result for 1a36141, script " + path);
					_1a36141_failed = true;
					break;
				default:
					break;
			}
		}
		if (!_1ca61a3_failed) {
			auto test_result = decomp_1ca61a3->_test_bytecode(data, token_max, func_max);
			switch (test_result) {
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_FAIL:
					_1ca61a3_failed = true;
					break;
				case GDScriptDecomp::BytecodeTestResult::BYTECODE_TEST_CORRUPT:
					WARN_PRINT("BYTECODE_TEST_CORRUPT test result for 1ca61a3, script " + path);
					_1ca61a3_failed = true;
					break;
				default:
					break;
			}
		}
		if (rev != 0) {
			// we found a match, no need to keep going
			break;
		}
		if (_514a3fb_failed && _1a36141_failed && _1ca61a3_failed) {
			// Welp
			break;
		}
	}

	if (rev == 0) {
		if (!_514a3fb_failed && !_1a36141_failed && _1ca61a3_failed) { // there were major token changes in 3.1-beta, 1ca61a3 should have failed if the others didn't
			// The smoothstep shift happens pretty early in the function table;
			// We may have gotten unlucky and not found any discontinuities in the scripts we tested.
			// Check if we can safely decompile with either.
			if (func_max >= decomp_514a3fb->get_function_index("smoothstep")) {
				// Unlucky, we can't safely decompile this with either bytecode version.
				// We should just fail here.
				ERR_FAIL_V_MSG(0, "UNLUCKY!!! Failed to detect GDScript revision for engine version 3.1.x, please report this issue on GitHub.");
			}
			// Otherwise, we can safely decompile with either.
			// We'll just use 514a3fb.
			rev = 0x514a3fb;
		} else if (!_514a3fb_failed && _1a36141_failed && _1ca61a3_failed) {
			rev = 0x514a3fb;
		} else if (_514a3fb_failed && !_1a36141_failed && _1ca61a3_failed) {
			rev = 0x1a36141;
		} else if (_514a3fb_failed && _1a36141_failed && !_1ca61a3_failed) {
			rev = 0x1ca61a3;
		} else {
			// Try it with the dev versions.
			return generic_test(p_paths, 3, 1, true, true);
		}
	}

	return rev;
}

uint64_t BytecodeTester::test_files(const Vector<String> &p_paths, int ver_major_hint, int ver_minor_hint, bool print_log_on_fail) {
	uint64_t rev = 0;
	ERR_FAIL_COND_V_MSG(p_paths.size() == 0, 0, "No files to test");
	Vector<uint8_t> key;
	if (p_paths[0].get_extension().to_lower() == "gde") {
		key = GDRESettings::get_singleton()->get_encryption_key();
	}

	if (ver_major_hint == 3 && ver_minor_hint == 1) {
		rev = test_files_3_1(p_paths, key);
	} else if (ver_major_hint == 2 && ver_minor_hint == 1) {
		rev = test_files_2_1(p_paths);
	} else {
		rev = generic_test(p_paths, ver_major_hint, ver_minor_hint, false, print_log_on_fail);
	}
	return rev;
}

Vector<Ref<GDScriptDecomp>> get_possibles_from_set(const Vector<String> &bytecode_files, const Vector<Ref<GDScriptDecomp>> &decomps, bool print_verbosely = false) {
	Vector<Ref<GDScriptDecomp>> passed;

	for (const auto &decomp : decomps) {
		bool failed = false;
		for (const String &file : bytecode_files) {
			Vector<uint8_t> buffer;
			if (file.get_extension().to_lower() == "gde") {
				Error err = GDScriptDecomp::get_buffer_encrypted(file, 3, GDRESettings::get_singleton()->get_encryption_key(), buffer);
				if (err) {
					WARN_PRINT("Could not read encrypted bytecode file: " + file);
					continue;
				}
			} else {
				buffer = FileAccess::get_file_as_bytes(file);
				if (buffer.size() == 0) {
					WARN_PRINT("Could not read bytecode file: " + file);
					continue;
				}
			}
			auto result = decomp->test_bytecode(buffer, print_verbosely);
			if (result == GDScriptDecomp::BYTECODE_TEST_FAIL || result == GDScriptDecomp::BYTECODE_TEST_CORRUPT) {
				if (print_verbosely) {
					print_line("\t Test failed on file " + file);
				}
				failed = true;
				break;
			}
		}
		if (!failed) {
			passed.append(decomp);
		}
	}
	return passed;
}

Vector<Ref<GDScriptDecomp>> BytecodeTester::get_possible_decomps(Vector<String> bytecode_files, bool include_dev, bool print_verbosely) {
	int bytecode_version = get_bytecode_version(bytecode_files);
	ERR_FAIL_COND_V_MSG(bytecode_version == -1, {}, "Inconsistent bytecode versions across files!!!");
	ERR_FAIL_COND_V_MSG(bytecode_version <= 0, {}, "Could not read bytecode version from files.");
	auto decomps = GDScriptDecompVersion::get_decomps_for_bytecode_ver(bytecode_version, include_dev);
	return get_possibles_from_set(bytecode_files, decomps, print_verbosely);
}

Vector<Ref<GDScriptDecomp>> BytecodeTester::filter_decomps(const Vector<Ref<GDScriptDecomp>> &p_decomp_versions, int ver_major_hint, int ver_minor_hint) {
	Vector<Ref<GDScriptDecomp>> candidates;
	if (ver_major_hint > 0 && ver_minor_hint < 0) {
		for (int i = 0; i < p_decomp_versions.size(); i++) {
			if (p_decomp_versions[i]->get_engine_ver_major() == ver_major_hint) {
				candidates.push_back(p_decomp_versions[i]);
			}
		}
	} else if (ver_major_hint > 0 && ver_minor_hint >= 0) {
		for (int i = 0; i < p_decomp_versions.size(); i++) {
			if (p_decomp_versions[i]->get_engine_ver_major() == ver_major_hint) {
				bool good = false;
				if (p_decomp_versions[i]->get_max_engine_version() != "") {
					Ref<GodotVer> min_ver = GodotVer::parse(p_decomp_versions[i]->get_engine_version());
					Ref<GodotVer> max_ver = GodotVer::parse(p_decomp_versions[i]->get_max_engine_version());
					if (max_ver->get_minor() >= ver_minor_hint && min_ver->get_minor() <= ver_minor_hint) {
						good = true;
					}
				} else {
					Ref<GodotVer> ver = memnew(GodotVer(p_decomp_versions[i]->get_engine_version()));
					if (ver->get_minor() == ver_minor_hint) {
						good = true;
					}
				}
				if (good) {
					candidates.push_back(p_decomp_versions[i]);
				}
			}
		}
	} else {
		// no version hint
		for (int i = 0; i < p_decomp_versions.size(); i++) {
			candidates.push_back(p_decomp_versions[i]);
		}
	}
	return candidates;
}
