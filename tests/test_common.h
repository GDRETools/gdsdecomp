#pragma once
#include "core/io/marshalls.h"
#include "core/os/os.h"
#include "external/dtl/dtl.hpp"
#include "utility/text_diff.h"
#include <utility/common.h>
#include <utility/gdre_settings.h>

_ALWAYS_INLINE_ String get_gdsdecomp_path() {
	return GDRESettings::get_singleton()->get_cwd().path_join("modules/gdsdecomp");
}

_ALWAYS_INLINE_ String get_tmp_path() {
	return get_gdsdecomp_path().path_join(".tmp");
}
_ALWAYS_INLINE_ String get_test_resources_path() {
	return get_gdsdecomp_path().path_join("tests/test_files");
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
