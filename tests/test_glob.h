#pragma once

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "tests/test_common.h"
#include "tests/test_macros.h"
#include "utility/glob.h"

namespace TestGlob {

// Helper function to create a test directory structure
void create_test_directory_structure(const String &base_path) {
	// Create base directory
	Ref<DirAccess> da = DirAccess::open(base_path.get_base_dir());
	CHECK(da.is_valid());
	da->make_dir_recursive(base_path);

	// Create directories
	da->make_dir_recursive(base_path.path_join("dir1"));
	da->make_dir_recursive(base_path.path_join("dir2"));
	da->make_dir_recursive(base_path.path_join("dir3"));
	da->make_dir_recursive(base_path.path_join("dir1/subdir1"));
	da->make_dir_recursive(base_path.path_join("dir1/subdir2"));
	da->make_dir_recursive(base_path.path_join("dir2/subdir1"));
	da->make_dir_recursive(base_path.path_join("dir3/subdir1"));
	da->make_dir_recursive(base_path.path_join("dir3/subdir2"));
	da->make_dir_recursive(base_path.path_join("dir3/subdir3"));
	da->make_dir_recursive(base_path.path_join(".hidden_dir"));

	// Create files
	Ref<FileAccess> fa;

	// Regular files
	fa = FileAccess::open(base_path.path_join("file1.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("file1 content");

	fa = FileAccess::open(base_path.path_join("file2.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("file2 content");

	fa = FileAccess::open(base_path.path_join("file3.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("file3 content");

	// Files in subdirectories
	fa = FileAccess::open(base_path.path_join("dir1/file1.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir1/file1 content");

	fa = FileAccess::open(base_path.path_join("dir1/file2.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir1/file2 content");

	fa = FileAccess::open(base_path.path_join("dir1/subdir1/file1.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir1/subdir1/file1 content");

	fa = FileAccess::open(base_path.path_join("dir1/subdir2/file1.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir1/subdir2/file1 content");

	fa = FileAccess::open(base_path.path_join("dir2/file1.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir2/file1 content");

	fa = FileAccess::open(base_path.path_join("dir2/subdir1/file1.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir2/subdir1/file1 content");

	fa = FileAccess::open(base_path.path_join("dir3/file1.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir3/file1 content");

	fa = FileAccess::open(base_path.path_join("dir3/subdir1/file1.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir3/subdir1/file1 content");

	fa = FileAccess::open(base_path.path_join("dir3/subdir2/file1.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir3/subdir2/file1 content");

	fa = FileAccess::open(base_path.path_join("dir3/subdir3/file1.txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir3/subdir3/file1 content");

	// Hidden files
	fa = FileAccess::open(base_path.path_join(".hidden_file"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string(".hidden_file content");

	fa = FileAccess::open(base_path.path_join("dir1/.hidden_file"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("dir1/.hidden_file content");

	// Files with special characters
	fa = FileAccess::open(base_path.path_join("file[1].txt"), FileAccess::WRITE);
	CHECK(fa.is_valid());
	fa->store_string("file[1] content");
}

// Helper function to clean up the test directory structure
void cleanup_test_directory_structure(const String &base_path) {
	Ref<DirAccess> da = DirAccess::open(base_path.get_base_dir());
	CHECK(da.is_valid());
	gdre::rimraf(base_path);
}

// Helper function to sort a vector of strings for consistent comparison
Vector<String> sort_strings(const Vector<String> &strings) {
	Vector<String> sorted = strings;
	sorted.sort();
	return sorted;
}

// Helper function to check if two vectors of strings are equal (regardless of order)
bool vectors_equal(const Vector<String> &vec1, const Vector<String> &vec2) {
	if (vec1.size() != vec2.size()) {
		return false;
	}

	Vector<String> sorted1 = sort_strings(vec1);
	Vector<String> sorted2 = sort_strings(vec2);

	for (int i = 0; i < sorted1.size(); i++) {
		if (sorted1[i] != sorted2[i]) {
			return false;
		}
	}

	return true;
}

// Test glob function with absolute paths
TEST_CASE("[GDSDecomp][Glob] glob with absolute paths") {
	String test_dir = get_tmp_path().path_join("glob_test");
	create_test_directory_structure(test_dir);

	// Test glob with a simple pattern
	Vector<String> result = Glob::glob(test_dir.path_join("*.txt"), false);
	Vector<String> expected = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt"),
		test_dir.path_join("file3.txt"),
		test_dir.path_join("file[1].txt"),
	};
	CHECK(vectors_equal(result, expected));

	// Test glob with a pattern that includes a directory
	result = Glob::glob(test_dir.path_join("dir1/*.txt"), false);
	expected = {
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir1/file2.txt")
	};
	CHECK(vectors_equal(result, expected));

	// Test glob with a pattern that includes a subdirectory
	result = Glob::glob(test_dir.path_join("dir1/subdir1/*.txt"), false);
	expected = {
		test_dir.path_join("dir1/subdir1/file1.txt")
	};
	CHECK(vectors_equal(result, expected));

	// Test glob with a pattern that includes a wildcard for directories
	result = Glob::glob(test_dir.path_join("dir*/file1.txt"), false);
	expected = {
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir2/file1.txt"),
		test_dir.path_join("dir3/file1.txt")
	};
	CHECK(vectors_equal(result, expected));

	// Test glob with a pattern that includes a wildcard for subdirectories
	result = Glob::glob(test_dir.path_join("dir1/subdir*/file1.txt"), false);
	expected = {
		test_dir.path_join("dir1/subdir1/file1.txt"),
		test_dir.path_join("dir1/subdir2/file1.txt")
	};
	CHECK(vectors_equal(result, expected));

	// Test glob with a pattern that includes a question mark
	result = Glob::glob(test_dir.path_join("file?.txt"), false);
	expected = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt"),
		test_dir.path_join("file3.txt")
	};
	CHECK(vectors_equal(result, expected));

	// Test glob with a pattern that includes a character class
	result = Glob::glob(test_dir.path_join("file[12].txt"), false);
	expected = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt")
	};
	CHECK(vectors_equal(result, expected));

	// Test glob with a pattern that includes a negated character class
	result = Glob::glob(test_dir.path_join("file[!12].txt"), false);
	expected = {
		test_dir.path_join("file3.txt"),
	};
	CHECK(vectors_equal(result, expected));

	// Test glob with a pattern that includes a range
	result = Glob::glob(test_dir.path_join("file[1-2].txt"), false);
	expected = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt"),
	};
	CHECK(vectors_equal(result, expected));

	// Test glob with a pattern that includes a tilde (should be expanded)
	String home_dir = OS::get_singleton()->get_environment("HOME");
	if (!home_dir.is_empty()) {
		result = Glob::glob("~/*", false);
		// just check that the result has at least one element and that it starts with the home directory
		CHECK(result.size() > 0);
		CHECK(result[0].begins_with(home_dir));
	}

	cleanup_test_directory_structure(test_dir);
}

// Test glob function with relative paths
TEST_CASE("[GDSDecomp][Glob] glob with relative paths") {
	REQUIRE(GDRESettings::get_singleton());
	String test_dir = get_tmp_path().path_join("glob_test_rel");
	create_test_directory_structure(test_dir);
	// get the current working directory
	// the reason we set the exec dir is because Glob is relative to the exec dir, not the cwd; the cwd changes when a project is loaded
	String current_dir = GDRESettings::get_singleton()->get_exec_dir();
	// change the current working directory to the test directory
	GDRESettings::get_singleton()->set_exec_dir(test_dir);
	CHECK(GDRESettings::get_singleton()->get_exec_dir() == test_dir);
	// Test glob with an absolute path
	Vector<String> result = Glob::glob("*.txt", false);
	Vector<String> expected = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt"),
		test_dir.path_join("file3.txt"),
		test_dir.path_join("file[1].txt"),
	};
	CHECK(vectors_equal(result, expected));

	// Test glob with a pattern that includes a directory
	result = Glob::glob("dir1/*.txt", false);
	expected = {
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir1/file2.txt")
	};
	CHECK(vectors_equal(result, expected));
	GDRESettings::get_singleton()->set_exec_dir(current_dir);
	cleanup_test_directory_structure(test_dir);
}

// Test rglob function (recursive glob)
TEST_CASE("[GDSDecomp][Glob] rglob (recursive glob)") {
	String test_dir = get_tmp_path().path_join("glob_test_recursive");
	create_test_directory_structure(test_dir);

	// Test rglob with a simple pattern
	Vector<String> result = Glob::rglob(test_dir.path_join("**/*.txt"), false);
	Vector<String> expected = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt"),
		test_dir.path_join("file3.txt"),
		test_dir.path_join("file[1].txt"),
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir1/file2.txt"),
		test_dir.path_join("dir1/subdir1/file1.txt"),
		test_dir.path_join("dir1/subdir2/file1.txt"),
		test_dir.path_join("dir2/file1.txt"),
		test_dir.path_join("dir2/subdir1/file1.txt"),
		test_dir.path_join("dir3/file1.txt"),
		test_dir.path_join("dir3/subdir1/file1.txt"),
		test_dir.path_join("dir3/subdir2/file1.txt"),
		test_dir.path_join("dir3/subdir3/file1.txt")
	};
	CHECK(vectors_equal(result, expected));

	// Test rglob with a pattern that includes a directory
	result = Glob::rglob(test_dir.path_join("dir1/**/*.txt"), false);
	expected = {
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir1/file2.txt"),
		test_dir.path_join("dir1/subdir1/file1.txt"),
		test_dir.path_join("dir1/subdir2/file1.txt")
	};
	CHECK(vectors_equal(result, expected));

	// Test rglob with a pattern that includes a wildcard for directories
	result = Glob::rglob(test_dir.path_join("dir*/**/file1.txt"), false);
	expected = {
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir2/file1.txt"),
		test_dir.path_join("dir3/file1.txt"),
		test_dir.path_join("dir1/subdir1/file1.txt"),
		test_dir.path_join("dir1/subdir2/file1.txt"),
		test_dir.path_join("dir2/subdir1/file1.txt"),
		test_dir.path_join("dir3/subdir1/file1.txt"),
		test_dir.path_join("dir3/subdir2/file1.txt"),
		test_dir.path_join("dir3/subdir3/file1.txt")
	};
	CHECK(vectors_equal(result, expected));

	// Test rglob with a pattern that includes a double asterisk (recursive)
	result = Glob::rglob(test_dir.path_join("**/*.txt"), false);
	expected = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt"),
		test_dir.path_join("file3.txt"),
		test_dir.path_join("file[1].txt"),
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir1/file2.txt"),
		test_dir.path_join("dir1/subdir1/file1.txt"),
		test_dir.path_join("dir1/subdir2/file1.txt"),
		test_dir.path_join("dir2/file1.txt"),
		test_dir.path_join("dir2/subdir1/file1.txt"),
		test_dir.path_join("dir3/file1.txt"),
		test_dir.path_join("dir3/subdir1/file1.txt"),
		test_dir.path_join("dir3/subdir2/file1.txt"),
		test_dir.path_join("dir3/subdir3/file1.txt")
	};
	CHECK(vectors_equal(result, expected));

	cleanup_test_directory_structure(test_dir);
}

// Test glob_list and rglob_list functions
TEST_CASE("[GDSDecomp][Glob] glob_list and rglob_list") {
	String test_dir = get_tmp_path().path_join("glob_test_list");
	create_test_directory_structure(test_dir);

	// Test glob_list with multiple patterns
	Vector<String> patterns = {
		test_dir.path_join("*.txt"),
		test_dir.path_join("dir1/*.txt")
	};
	Vector<String> result = Glob::glob_list(patterns, false);
	Vector<String> expected = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt"),
		test_dir.path_join("file3.txt"),
		test_dir.path_join("file[1].txt"),
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir1/file2.txt")
	};
	CHECK(vectors_equal(result, expected));

	// Test rglob_list with multiple patterns
	patterns = {
		test_dir.path_join("*.txt"),
		test_dir.path_join("dir1/**/*.txt")
	};
	result = Glob::rglob_list(patterns, false);
	expected = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt"),
		test_dir.path_join("file3.txt"),
		test_dir.path_join("file[1].txt"),
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir1/file2.txt"),
		test_dir.path_join("dir1/subdir1/file1.txt"),
		test_dir.path_join("dir1/subdir2/file1.txt")
	};
	CHECK(vectors_equal(result, expected));

	cleanup_test_directory_structure(test_dir);
}

// Test fnmatch function
TEST_CASE("[GDSDecomp][Glob] fnmatch") {
	// Test fnmatch with a simple pattern
	CHECK(Glob::fnmatch("file1.txt", "file1.txt"));
	CHECK(Glob::fnmatch("file1.txt", "*.txt"));
	CHECK(Glob::fnmatch("file1.txt", "file?.txt"));
	CHECK(Glob::fnmatch("file1.txt", "file[12].txt"));
	CHECK(Glob::fnmatch("file1.txt", "file[!34].txt"));
	CHECK(Glob::fnmatch("file1.txt", "file[1-2].txt"));

	// Test fnmatch with a pattern that doesn't match
	CHECK_FALSE(Glob::fnmatch("file1.txt", "file2.txt"));
	CHECK_FALSE(Glob::fnmatch("file1.txt", "*.dat"));
	CHECK_FALSE(Glob::fnmatch("file1.txt", "file??.txt"));
	CHECK_FALSE(Glob::fnmatch("file1.txt", "file[34].txt"));
	CHECK_FALSE(Glob::fnmatch("file1.txt", "file[!12].txt"));
	CHECK_FALSE(Glob::fnmatch("file1.txt", "file[3-4].txt"));
}

// Test fnmatch_list function
TEST_CASE("[GDSDecomp][Glob] fnmatch_list") {
	Vector<String> names = {
		"file1.txt",
		"file2.txt",
		"file3.txt",
		"file.dat"
	};
	Vector<String> patterns = {
		"*.txt",
		"file2.*"
	};

	Vector<String> result = Glob::fnmatch_list(names, patterns);
	Vector<String> expected = {
		"file1.txt",
		"file2.txt",
		"file3.txt"
	};
	CHECK(vectors_equal(result, expected));

	// Test with empty patterns
	result = Glob::fnmatch_list(names, Vector<String>());
	CHECK(result.is_empty());

	// Test with empty names
	result = Glob::fnmatch_list(Vector<String>(), patterns);
	CHECK(result.is_empty());
}

// Test pattern_match_list function
TEST_CASE("[GDSDecomp][Glob] pattern_match_list") {
	Vector<String> names = {
		"file1.txt",
		"file2.txt",
		"file3.txt",
		"file.dat"
	};
	Vector<String> patterns = {
		"*.txt",
		"file2.*"
	};

	Vector<String> result = Glob::pattern_match_list(names, patterns);
	Vector<String> expected = {
		"*.txt",
		"file2.*"
	};
	CHECK(vectors_equal(result, expected));

	// Test with empty patterns
	result = Glob::pattern_match_list(names, Vector<String>());
	CHECK(result.is_empty());

	// Test with empty names
	result = Glob::pattern_match_list(Vector<String>(), patterns);
	CHECK(result.is_empty());
}

// Test names_in_dirs function
TEST_CASE("[GDSDecomp][Glob] names_in_dirs") {
	String test_dir = get_tmp_path().path_join("glob_test_names_in_dirs");
	create_test_directory_structure(test_dir);

	Vector<String> names = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt"),
		test_dir.path_join("file3.txt"),
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir1/file2.txt"),
		test_dir.path_join("dir1/subdir1/file1.txt"),
		test_dir.path_join("dir2/file1.txt"),
		test_dir.path_join("dir2/subdir1/file1.txt"),
		test_dir.path_join("dir3/file1.txt"),
		test_dir.path_join("dir3/subdir1/file1.txt"),
		test_dir.path_join("dir3/subdir2/file1.txt"),
		test_dir.path_join("dir3/subdir3/file1.txt"),
		test_dir.path_join("nonexistent.txt")
	};
	Vector<String> dirs = {
		test_dir.path_join("dir1"),
		test_dir.path_join("dir2")
	};

	Vector<String> result = Glob::names_in_dirs(names, dirs);
	Vector<String> expected = {
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir1/file2.txt"),
		test_dir.path_join("dir1/subdir1/file1.txt"),
		test_dir.path_join("dir2/file1.txt"),
		test_dir.path_join("dir2/subdir1/file1.txt")
	};
	CHECK(vectors_equal(result, expected));

	cleanup_test_directory_structure(test_dir);
}

// Test dirs_in_names function
TEST_CASE("[GDSDecomp][Glob] dirs_in_names") {
	String test_dir = get_tmp_path().path_join("glob_test_dirs_in_names");
	create_test_directory_structure(test_dir);

	Vector<String> names = {
		test_dir.path_join("file1.txt"),
		test_dir.path_join("file2.txt"),
		test_dir.path_join("file3.txt"),
		test_dir.path_join("dir1/file1.txt"),
		test_dir.path_join("dir1/file2.txt"),
		test_dir.path_join("dir1/subdir1/file1.txt"),
		test_dir.path_join("dir2/file1.txt"),
		test_dir.path_join("dir2/subdir1/file1.txt"),
		test_dir.path_join("dir3/file1.txt"),
		test_dir.path_join("dir3/subdir1/file1.txt"),
		test_dir.path_join("dir3/subdir2/file1.txt"),
		test_dir.path_join("dir3/subdir3/file1.txt"),
		test_dir.path_join("nonexistent.txt")
	};
	Vector<String> dirs = {
		test_dir.path_join("dir1"),
		test_dir.path_join("dir2"),
		test_dir.path_join("dir3")
	};

	Vector<String> result = Glob::dirs_in_names(names, dirs);
	Vector<String> expected = {
		test_dir.path_join("dir1"),
		test_dir.path_join("dir2"),
		test_dir.path_join("dir3")
	};
	CHECK(vectors_equal(result, expected));

	cleanup_test_directory_structure(test_dir);
}

// Test glob with hidden files
TEST_CASE("[GDSDecomp][Glob] glob with hidden files") {
	String test_dir = get_tmp_path().path_join("glob_test_hidden");
	create_test_directory_structure(test_dir);

	// Test glob with hidden files excluded
	// don't run this test on windows
#if !defined(WINDOWS_ENABLED)
	if (OS::get_singleton()->get_name() != "Windows") {
		Vector<String> result = Glob::glob(test_dir.path_join(".*"), false);
		Vector<String> expected = {};
		CHECK(vectors_equal(result, expected));
	}
#endif
	// Test glob with hidden files included
	Vector<String> result = Glob::glob(test_dir.path_join(".*"), true);
	Vector<String> expected = {
		test_dir.path_join(".hidden_file"),
		test_dir.path_join(".hidden_dir")
	};
	CHECK(vectors_equal(result, expected));

	cleanup_test_directory_structure(test_dir);
}

// Test glob with special characters
TEST_CASE("[GDSDecomp][Glob] glob with special characters") {
	String test_dir = get_tmp_path().path_join("glob_test_special");
	create_test_directory_structure(test_dir);

	// Test glob with a pattern that includes a square bracket
	Vector<String> result = Glob::glob(test_dir.path_join("file\\[1\\].txt"), false);
	Vector<String> expected = {
		test_dir.path_join("file[1].txt")
	};
	CHECK(vectors_equal(result, expected));

	cleanup_test_directory_structure(test_dir);
}

} // namespace TestGlob
