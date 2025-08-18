#pragma once
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"
#include "macros.h"

#include <core/io/dir_access.h>
#include <core/object/class_db.h>
#include <core/object/object.h>
#include <core/variant/typed_array.h>
#include <core/variant/typed_dictionary.h>

class Image;
class FileAccess;
namespace gdre {
Vector<String> get_recursive_dir_list(const String &dir, const Vector<String> &wildcards = {}, const bool absolute = true, const String &rel = "");
bool dir_has_any_matching_wildcards(const String &dir, const Vector<String> &wildcards = {});

bool check_header(const Vector<uint8_t> &p_buffer, const char *p_expected_header, int p_expected_len);
Error ensure_dir(const String &dst_dir);
void get_strings_from_variant(const Variant &p_var, Vector<String> &r_strings, const String &engine_version = "");
String get_md5(const String &dir, bool ignore_code_signature = false);
String get_md5_for_dir(const String &dir, bool ignore_code_signature = false);
String get_sha256(const String &file_or_dir);
Error unzip_file_to_dir(const String &zip_path, const String &output_dir);
Error wget_sync(const String &p_url, Vector<uint8_t> &response, int retries = 5, float *p_progress = nullptr, bool *p_cancelled = nullptr);
Error download_file_sync(const String &url, const String &output_path, float *p_progress = nullptr, bool *p_cancelled = nullptr);
Error rimraf(const String &dir);
bool dir_is_empty(const String &dir);
Error touch_file(const String &path);
bool store_var_compat(Ref<FileAccess> f, const Variant &p_var, int ver_major, bool p_full_objects = false);
String get_full_path(const String &p_path, DirAccess::AccessType p_access);
bool directory_has_any_of(const String &p_dir_path, const Vector<String> &p_files);
Vector<String> get_files_at(const String &p_dir, const Vector<String> &wildcards, bool absolute = true);

String num_scientific(double p_num);
String num_scientific(float p_num);

template <class T>
Vector<T> hashset_to_vector(const HashSet<T> &hs) {
	Vector<T> ret;
	for (const T &E : hs) {
		ret.push_back(E);
	}
	return ret;
}

template <class T>
HashSet<T> vector_to_hashset(const Vector<T> &vec) {
	HashSet<T> ret;
	for (int i = 0; i < vec.size(); i++) {
		ret.insert(vec[i]);
	}
	return ret;
}

template <class T>
Array hashset_to_array(const HashSet<T> &hs) {
	Array ret;
	for (const T &E : hs) {
		ret.push_back(E);
	}
	return ret;
}

template <class T>
bool vectors_intersect(const Vector<T> &a, const Vector<T> &b) {
	const Vector<T> &bigger = a.size() > b.size() ? a : b;
	const Vector<T> &smaller = a.size() > b.size() ? b : a;
	for (int i = 0; i < smaller.size(); i++) {
		if (bigger.has(smaller[i])) {
			return true;
		}
	}
	return false;
}

template <class T>
bool hashset_intersects_vector(const HashSet<T> &a, const Vector<T> &b) {
	for (int i = 0; i < b.size(); i++) {
		if (a.has(b[i])) {
			return true;
		}
	}
	return false;
}

template <class K, class V>
Vector<K> get_keys(const HashMap<K, V> &map) {
	Vector<K> ret;
	for (const auto &E : map) {
		ret.push_back(E.key);
	}
	return ret;
}

template <class K, class V>
HashSet<K> get_set_of_keys(const HashMap<K, V> &map) {
	HashSet<K> ret;
	for (const auto &E : map) {
		ret.insert(E.key);
	}
	return ret;
}

template <class T>
Vector<T> get_vector_intersection(const Vector<T> &a, const Vector<T> &b) {
	Vector<T> ret;
	const Vector<T> &bigger = a.size() > b.size() ? a : b;
	const Vector<T> &smaller = a.size() > b.size() ? b : a;
	for (int i = 0; i < smaller.size(); i++) {
		if (bigger.has(smaller[i])) {
			ret.push_back(smaller[i]);
		}
	}
	return ret;
}

template <class T>
void shuffle_vector(Vector<T> &vec) {
	const int n = vec.size();
	if (n < 2) {
		return;
	}
	T *data = vec.ptrw();
	for (int i = n - 1; i >= 1; i--) {
		const int j = Math::rand() % (i + 1);
		const T tmp = data[j];
		data[j] = data[i];
		data[i] = tmp;
	}
}

template <class T>
TypedArray<T> vector_to_typed_array(const Vector<T> &vec) {
	TypedArray<T> arr;
	arr.resize(vec.size());
	for (int i = 0; i < vec.size(); i++) {
		arr.set(i, vec[i]);
	}
	return arr;
}

template <class T>
Vector<T> array_to_vector(const Array &arr) {
	Vector<T> vec;
	for (int i = 0; i < arr.size(); i++) {
		vec.push_back(arr[i]);
	}
	return vec;
}

template <class T>
Array vector_to_array(const Vector<T> &vec) {
	Array arr;
	arr.resize(vec.size());
	for (int i = 0; i < vec.size(); i++) {
		arr.set(i, vec[i]);
	}
	return arr;
}

// specialization for Ref<T>
template <class T>
TypedArray<T> vector_to_typed_array(const Vector<Ref<T>> &vec) {
	TypedArray<T> arr;
	arr.resize(vec.size());
	for (int i = 0; i < vec.size(); i++) {
		arr.set(i, vec[i]);
	}
	return arr;
}

template <class K, class V>
TypedDictionary<K, V> hashmap_to_typed_dict(const HashMap<K, V> &map) {
	TypedDictionary<K, V> dict;
	for (const auto &E : map) {
		dict[E.key] = E.value;
	}
	return dict;
}

template <class K, class V>
TypedDictionary<K, V> hashmap_to_typed_dict(const HashMap<K, Ref<V>> &map) {
	TypedDictionary<K, V> dict;
	for (const auto &E : map) {
		dict[E.key] = E.value;
	}
	return dict;
}
template <class K, class V>
TypedDictionary<K, V> hashmap_to_typed_dict(const HashMap<Ref<K>, V> &map) {
	TypedDictionary<K, V> dict;
	for (const auto &E : map) {
		dict[E.key] = E.value;
	}
	return dict;
}

template <class K, class V>
TypedDictionary<K, V> hashmap_to_typed_dict(const HashMap<Ref<K>, Ref<V>> &map) {
	TypedDictionary<K, V> dict;
	for (const auto &E : map) {
		dict[E.key] = E.value;
	}
	return dict;
}

template <typename T>
T get_most_popular_value(const Vector<T> &p_values) {
	if (p_values.is_empty()) {
		return T();
	}
	HashMap<T, int64_t> dict;
	for (int i = 0; i < p_values.size(); i++) {
		size_t current_count = dict.has(p_values[i]) ? dict.get(p_values[i]) : 0;
		dict[p_values[i]] = current_count + 1;
	}
	int64_t max_count = 0;
	T most_popular_value;
	for (auto &E : dict) {
		if (E.value > max_count) {
			max_count = E.value;
			most_popular_value = E.key;
		}
	}
	return most_popular_value;
}

bool string_is_ascii(const String &s);
bool string_has_whitespace(const String &s);
void get_chars_in_set(const String &s, const HashSet<char32_t> &chars, HashSet<char32_t> &ret);
bool has_chars_in_set(const String &s, const HashSet<char32_t> &chars);
String remove_chars(const String &s, const HashSet<char32_t> &chars);
String remove_chars(const String &s, const Vector<char32_t> &chars);
String remove_whitespace(const String &s);

Vector<String> _split_multichar(const String &s, const Vector<String> &splitters, bool allow_empty = true,
		int maxsplit = 0);
Vector<String> _rsplit_multichar(const String &s, const Vector<String> &splitters, bool allow_empty = true,
		int maxsplit = 0);

Vector<String> split_multichar(const String &s, const HashSet<char32_t> &splitters, bool allow_empty = true,
		int maxsplit = 0);
Vector<String> rsplit_multichar(const String &s, const HashSet<char32_t> &splitters, bool allow_empty = true,
		int maxsplit = 0);

bool detect_utf8(const PackedByteArray &p_utf8_buf);
Error copy_dir(const String &src, const String &dst);

Ref<FileAccess> open_encrypted_v3(const String &p_path, int p_mode, const Vector<uint8_t> &p_key);
Vector<String> filter_error_backtraces(const Vector<String> &p_error_messages);
Vector<String> get_files_for_paths(const Vector<String> &p_paths);
} // namespace gdre

class GDRECommon : public Object {
	GDCLASS(GDRECommon, Object);

protected:
	static void _bind_methods();
};

// Can only pass in string literals
#define _GDRE_CHECK_HEADER(p_buffer, p_expected_header) gdre::check_header(p_buffer, p_expected_header, sizeof(p_expected_header) - 1)
