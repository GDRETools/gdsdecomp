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

#pragma once
#include "core/object/object.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "modules/regex/regex.h"

class Glob : public Object {
	GDCLASS(Glob, Object);

protected:
	friend void init_ver_regex();
	friend void free_ver_regex();
	static void _bind_methods();
	static String normalize_path(const String &path);
	static Ref<RegEx> magic_check;
	static constexpr const char *magic_pattern = "([*?[])";
	static Ref<RegEx> escapere;
	static constexpr const char *escape_pattern = R"([&~|])";

	static bool has_magic(const String &pathname);
	static Vector<String> _glob(const String &inpath, bool recursive = false,
			bool dironly = false, bool include_hidden = false);

public:
	static String translate(const String &pattern);

	/// \param pathname string containing a path specification
	/// \return vector of paths that match the pathname
	///
	/// Pathnames can be absolute (/usr/src/Foo/Makefile) or relative (../../Tools/*/*.gif)
	/// Pathnames can contain shell-style wildcards
	/// Broken symlinks are included in the results (as in the shell)
	static Vector<String> glob(const String &pathname, bool hidden = false);

	/// \param pathnames string containing a path specification
	/// \return vector of paths that match the pathname
	///
	/// Globs recursively.
	/// The pattern “**” will match any files and zero or more directories, subdirectories and
	/// symbolic links to directories.
	static Vector<String> rglob(const String &pathname, bool hidden = false);

	/// Runs `glob` against each pathname in `pathnames` and accumulates the results
	static Vector<String> glob_list(const Vector<String> &pathnames, bool hidden = false);

	/// Runs `rglob` against each pathname in `pathnames` and accumulates the results
	static Vector<String> rglob_list(const Vector<String> &pathnames, bool hidden = false);

	/// Returns true if the input path matche the glob pattern
	static bool fnmatch(const String &name, const String &pattern);

	/// Returns a list of names that match any of the patterns
	static Vector<String> fnmatch_list(const Vector<String> &names, const Vector<String> &patterns, bool p_exclude_matches = false);

	/// Returns a list of patterns in which any of the names match
	static Vector<String> pattern_match_list(const Vector<String> &names, const Vector<String> &patterns);

	/// Returns a list of names that are in any of the directories
	static Vector<String> names_in_dirs(const Vector<String> &names, const Vector<String> &dirs);

	/// Returns a list of directories that are in any of the names
	static Vector<String> dirs_in_names(const Vector<String> &names, const Vector<String> &dirs);

}; // namespace glob
