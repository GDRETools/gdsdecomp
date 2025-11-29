#pragma once
#include "core/io/marshalls.h"
#include "core/os/os.h"
#include "external/dtl/dtl.hpp"
#include "tests/test_macros.h"
#include "utility/text_diff.h"
#include <utility/common.h>
#include <utility/gdre_settings.h>

_ALWAYS_INLINE_ String get_gdsdecomp_path() {
	REQUIRE(GDRESettings::get_singleton());
	return GDRESettings::get_singleton()->get_cwd().path_join("modules/gdsdecomp");
}

_ALWAYS_INLINE_ String get_tmp_path() {
	return get_gdsdecomp_path().path_join(".tmp");
}

_ALWAYS_INLINE_ String get_test_resources_path() {
	return get_gdsdecomp_path().path_join("tests/test_files");
}

_ALWAYS_INLINE_ String get_test_scripts_path() {
	return get_gdsdecomp_path().path_join("tests/test_scripts");
}

_ALWAYS_INLINE_ String get_gdscript_tests_path() {
	REQUIRE(GDRESettings::get_singleton());
	return GDRESettings::get_singleton()->get_cwd().path_join("modules/gdscript/tests/scripts");
}

_ALWAYS_INLINE_ String get_gdsdecomp_helpers_path() {
	return get_gdsdecomp_path().path_join("helpers");
}

_ALWAYS_INLINE_ Vector<String> get_test_versions() {
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
inline void output_diff(const String &file_name, const String &old_text, const String &new_text) {
	// write the script to a temp path
	auto new_path = get_tmp_path().path_join(file_name.get_basename() + ".diff");
	gdre::ensure_dir(new_path.get_base_dir());
	auto diff = TextDiff::get_diff_with_header(file_name, file_name, old_text, new_text);
	auto fa_diff = FileAccess::open(new_path, FileAccess::WRITE);
	if (fa_diff.is_valid()) {
		fa_diff->store_string(diff);
		fa_diff->flush();
		fa_diff->close();
	}
}

String remove_comments(const String &script_text);
