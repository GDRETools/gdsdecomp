/**
 * MIT License
 *
 * Copyright (c) 2019 Pranav, 2023 Nikita Lita
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "glob.h"
#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "modules/regex/regex.h"
#include "utility/gdre_settings.h"

#include <functional>
namespace {

// SPECIAL_CHARS
// closing ')', '}' and ']'
// '-' (a range in character set)
// '&', '~', (extended character set operations)
// '#' (comment) and WHITESPACE (ignored) in verbose mode
static const String special_characters = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f";

HashMap<char32_t, String> _init_map() {
	HashMap<char32_t, String> map;
	for (int i = 0; i < special_characters.length(); i++) {
		auto sc = special_characters[i];
		map.insert(
				static_cast<char32_t>(sc), String{ "\\" } + sc);
	}
	return map;
}

static const HashMap<char32_t, String> special_characters_map = _init_map();

String get_real_cwd() {
	return GDRESettings::get_singleton()->get_exec_dir();
}

String get_real_dir(const String &path) {
	if (path.is_absolute_path()) {
		return path;
	}
	return get_real_cwd().path_join(path);
}

String get_real_base_dir(const String &path) {
	return get_real_dir(path).get_base_dir();
}

bool dir_exists(const String &path, bool include_hidden = false) {
	String basename = path.get_file();
	Ref<DirAccess> da = DirAccess::open(get_real_base_dir(path));
	if (da.is_null()) {
		return false;
	}
	da->set_include_hidden(include_hidden);
	if ((da->dir_exists(basename))) {
		return true;
	}
	return false;
}

bool dir_or_file_exists(const String &path, bool include_hidden = false) {
	String basename = path.get_file();
	Ref<DirAccess> da = DirAccess::open(get_real_base_dir(path));
	if (da.is_null()) {
		return false;
	}
	da->set_include_hidden(include_hidden);
	if ((da->file_exists(basename) || da->dir_exists(basename))) {
		return true;
	}
	return false;
}

Vector<String> filter(const Vector<String> &names,
		const String &pattern) {
	// std::cout << "Pattern: " << pattern << "\n";
	Vector<String> result;
	for (auto &name : names) {
		// std::cout << "Checking for " << name.string() << "\n";
		if (Glob::fnmatch(name, pattern)) {
			result.push_back(name);
		}
	}
	return result;
}

bool is_hidden(const String &pathname) {
	return pathname[0] == '.';
}

bool is_recursive(const String &pattern) {
	return pattern == "**";
}

Vector<String> iter_directory(const String &dir, bool dironly, bool include_hidden) {
	Error err;
	String real_dir = get_real_dir(dir);
	Ref<DirAccess> da = DirAccess::open(real_dir, &err);
	if (da.is_null()) {
		return Vector<String>(); // fail silently
	}
	da->set_include_hidden(include_hidden);
	Vector<String> ret = da->get_directories();
	if (!dironly) {
		ret.append_array(da->get_files());
	}
	if (dir.is_absolute_path()) {
		for (int i = 0; i < ret.size(); i++) {
			ret.ptrw()[i] = dir.path_join(ret[i]);
		}
	}
	return ret;
}

// Recursively yields relative pathnames inside a literal directory.
Vector<String> rlistdir(const String &dirname, bool dironly, bool include_hidden) {
	Vector<String> result;

	auto names = iter_directory(dirname, dironly, include_hidden);
	for (auto &x : names) {
		result.push_back(x);
		for (auto &y : rlistdir(x, dironly, include_hidden)) {
			if (!dirname.is_absolute_path()) {
				y = x.path_join(y);
			}
			result.push_back(y);
		}
	}
	return result;
}

// This helper function recursively yields relative pathnames inside a literal
// directory.
Vector<String> glob2(const String &dirname, [[maybe_unused]] const String &pattern,
		bool dironly, bool include_hidden) {
	// std::cout << "In glob2\n";
	Vector<String> result;
	//assert(is_recursive(pattern));
	for (auto &dir : rlistdir(dirname, dironly, include_hidden)) {
		result.push_back(dir);
	}
	if (dironly) {
		result.push_back(dirname);
	}
	return result;
}

// These 2 helper functions non-recursively glob inside a literal directory.
// They return a list of basenames.  _glob1 accepts a pattern while _glob0
// takes a literal basename (so it only has to check for its existence).

Vector<String> glob1(const String &dirname, const String &pattern,
		bool dironly, bool include_hidden) {
	// std::cout << "In glob1\n";
	auto names = iter_directory(dirname, dironly, include_hidden);
	Vector<String> filtered_names;
	for (auto &n : names) {
		if (!is_hidden(n) || include_hidden) {
			filtered_names.push_back(n.get_file());
		}
	}
	return filter(filtered_names, pattern);
}

Vector<String> glob0(const String &dirname, const String &basename,
		bool dironly, bool include_hidden) {
	// std::cout << "In glob0\n";
	Vector<String> result;
	if (basename.is_empty()) {
		// 'q*x/' should match only directories.
		if (dir_exists(dirname, include_hidden)) {
			result = { basename };
		}
	} else {
		if (dir_or_file_exists(dirname.path_join(basename), include_hidden)) {
			result = { basename };
		}
	}
	if (!result.is_empty() && dirname.is_absolute_path()) {
		result = { basename.is_empty() ? dirname : dirname.path_join(basename) };
	}
	return result;
}

String get_user_home_dir() {
	auto ident = OS::get_singleton()->get_identifier();
	if (ident == "web") {
		return OS::get_singleton()->get_user_data_dir();
	} else if (ident == "windows") {
		return OS::get_singleton()->get_environment("USERPROFILE");
	}
	return OS::get_singleton()->get_environment("HOME");
}

String expand_tilde(String path) {
	if (path.is_empty() || path[0] != '~')
		return path;

	String home = get_user_home_dir();

	if (path.size() > 1) {
		return home + path.substr(1, path.size() - 1);
	} else {
		return home;
	}
}
} //namespace

Ref<RegEx> Glob::escapere = nullptr;
Ref<RegEx> Glob::magic_check = nullptr;

String Glob::translate(const String &pattern) {
	std::size_t i = 0, n = pattern.length();
	String result_string;

	while (i < n) {
		auto c = pattern[i];
		i += 1;
		if (c == '*') {
			result_string += ".*";
		} else if (c == '?') {
			result_string += ".";
		} else if (c == '[') {
			auto j = i;
			if (j < n && pattern[j] == '!') {
				j += 1;
			}
			if (j < n && pattern[j] == ']') {
				j += 1;
			}
			while (j < n && pattern[j] != ']') {
				j += 1;
			}
			if (j >= n) {
				result_string += "\\[";
			} else {
				auto stuff = pattern.substr(i, j);
				if (stuff.find("--") == String::npos) {
					stuff.replace(String{ "\\" }, String{ R"(\\)" });
				} else {
					Vector<String> chunks;
					std::size_t k = 0;
					if (pattern[i] == '!') {
						k = i + 2;
					} else {
						k = i + 1;
					}

					while (true) {
						size_t off = k;
						k = pattern.substr(off, j).find("-");
						if (k == -1) {
							break;
						}
						k += off;
						chunks.push_back(pattern.substr(i, k));
						i = k + 1;
						k = k + 3;
					}

					chunks.push_back(pattern.substr(i, j));
					// Escape backslashes and hyphens for set difference (--).
					// Hyphens that create ranges shouldn't be escaped.
					bool first = true;
					for (auto &s : chunks) {
						s.replace(String{ "\\" }, String{ R"(\\)" });
						s.replace(String{ "-" }, String{ R"(\-)" });
						if (first) {
							stuff += s;
							first = false;
						} else {
							stuff += "-" + s;
						}
					}
				}

				// Escape set operations (&&, ~~ and ||).
				String result = Glob::escapere->sub(stuff, R"(\\\1)", true);
				stuff = result;
				i = j + 1;
				if (stuff[0] == '!') {
					stuff = "^" + stuff.substr(1);
				} else if (stuff[0] == '^' || stuff[0] == '[') {
					stuff = "\\\\" + stuff;
				}
				result_string = result_string + "[" + stuff + "]";
			}
		} else {
			if (special_characters.find_char(c) != String::npos) {
				result_string += special_characters_map[static_cast<char32_t>(c)];
			} else {
				result_string += c;
			}
		}
	}
	return String{ "((" } + result_string + String{ R"()|[\r\n])$)" };
}

bool Glob::has_magic(const String &pathname) {
	return Glob::magic_check->search(pathname).is_valid();
}

Vector<String> Glob::_glob(const String &inpath, bool recursive,
		bool dironly, bool include_hidden) {
	Vector<String> result;

	String path = inpath;

	if (path[0] == '~') {
		// expand tilde
		path = expand_tilde(path);
	}

	const String dirname = path.get_base_dir();
	const String basename = path.get_file();

	if (!has_magic(path)) {
		//assert(!dironly);
		if (!basename.is_empty()) {
			if (dir_or_file_exists(path, include_hidden)) {
				result.push_back(path);
			}
		} else {
			// Patterns ending with a slash should match only directories
			if (dir_exists(dirname, include_hidden)) {
				result.push_back(path);
			}
		}
		return result;
	}

	if (dirname.is_empty()) {
		if (recursive && is_recursive(basename)) {
			return glob2(dirname, basename, dironly, include_hidden);
		} else {
			return glob1(dirname, basename, dironly, include_hidden);
		}
	}

	Vector<String> dirs;
	if (dirname != path && has_magic(dirname)) {
		dirs = _glob(dirname, recursive, true, include_hidden);
	} else {
		dirs = { dirname };
	}

	std::function<Vector<String>(const String &, const String &, bool, bool)>
			glob_in_dir;
	if (has_magic(basename)) {
		if (recursive && is_recursive(basename)) {
			glob_in_dir = glob2;
		} else {
			glob_in_dir = glob1;
		}
	} else {
		glob_in_dir = glob0;
	}

	for (auto &d : dirs) {
		for (auto &name : glob_in_dir(d, basename, dironly, include_hidden)) {
			String subresult = name;
			if (name.get_base_dir().is_empty()) {
				subresult = d.path_join(name);
			}
			result.push_back(subresult);
		}
	}

	return result;
}

bool Glob::fnmatch(const String &name, const String &pattern) {
	return RegEx::create_from_string(translate(pattern))->search(name).is_valid();
}

Vector<String> Glob::fnmatch_list(const Vector<String> &names, const Vector<String> &patterns) {
	Vector<String> result;
	if (patterns.is_empty() || names.is_empty()) {
		return result;
	}
	Vector<Ref<RegEx>> regexes;
	for (auto &pattern : patterns) {
		auto re = RegEx::create_from_string(translate(pattern));
		regexes.push_back(re);
	}
	for (auto &n : names) {
		for (auto &re : regexes) {
			if (re->search(n).is_valid()) {
				result.push_back(n);
				break;
			}
		}
	}
	return result;
}

Vector<String> Glob::pattern_match_list(const Vector<String> &names, const Vector<String> &patterns) {
	Vector<String> result;
	if (patterns.is_empty() || names.is_empty()) {
		return result;
	}
	Vector<Ref<RegEx>> regexes;
	for (auto &pattern : patterns) {
		auto re = RegEx::create_from_string(translate(pattern));
		regexes.push_back(re);
	}
	for (int i = 0; i < regexes.size(); i++) {
		auto &re = regexes[i];
		for (auto &n : names) {
			if (re->search(n).is_valid()) {
				result.push_back(patterns[i]);
				break;
			}
		}
	}
	return result;
}

Vector<String> Glob::names_in_dirs(const Vector<String> &names, const Vector<String> &dirs) {
	Vector<String> patterns;
	for (auto &dir : dirs) {
		patterns.push_back(dir.path_join("*"));
	}
	return fnmatch_list(names, patterns);
}

Vector<String> Glob::dirs_in_names(const Vector<String> &names, const Vector<String> &dirs) {
	Vector<String> patterns;
	Vector<String> ret;
	for (auto &dir : dirs) {
		patterns.push_back(dir.path_join("*"));
	}
	patterns = pattern_match_list(names, patterns);
	for (int i = 0; i < patterns.size(); i++) {
		patterns.write[i] = patterns[i].get_base_dir();
	}
	return patterns;
}

Vector<String> Glob::glob(const String &pathname, bool hidden) {
	return _glob(pathname, false, false, hidden);
}

Vector<String> Glob::rglob(const String &pathname, bool hidden) {
	return _glob(pathname, true, false, hidden);
}

Vector<String> Glob::glob_list(const Vector<String> &pathnames, bool hidden) {
	Vector<String> result;
	for (auto &pathname : pathnames) {
		for (auto &match : _glob(pathname, false, false, hidden)) {
			result.push_back(std::move(match));
		}
	}
	return result;
}

Vector<String> Glob::rglob_list(const Vector<String> &pathnames, bool hidden) {
	Vector<String> result;
	for (auto &pathname : pathnames) {
		for (auto &match : _glob(pathname, true, false, hidden)) {
			result.push_back(std::move(match));
		}
	}
	return result;
}

void Glob::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("glob", "pathname", "hidden"), &Glob::glob, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("rglob", "pathname", "hidden"), &Glob::rglob, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("glob_list", "pathnames", "hidden"), &Glob::glob_list, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("rglob_list", "pathnames", "hidden"), &Glob::rglob_list, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("fnmatch", "name", "pattern"), &Glob::fnmatch);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("fnmatch_list", "names", "patterns"), &Glob::fnmatch_list);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("pattern_match_list", "names", "patterns"), &Glob::pattern_match_list);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("names_in_dirs", "names", "dirs"), &Glob::names_in_dirs);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("dirs_in_names", "names", "dirs"), &Glob::dirs_in_names);
}
