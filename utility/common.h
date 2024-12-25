#pragma once
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"

class Image;
namespace gdre {
Vector<String> get_recursive_dir_list(const String dir, const Vector<String> &wildcards = Vector<String>(), const bool absolute = true, const String rel = "", const bool &res = false);
bool check_header(const Vector<uint8_t> &p_buffer, const char *p_expected_header, int p_expected_len);
Error ensure_dir(const String &dst_dir);
Error save_image_as_tga(const String &p_path, const Ref<Image> &p_img);
Error save_image_as_webp(const String &p_path, const Ref<Image> &p_img, bool lossy = false);
Error save_image_as_jpeg(const String &p_path, const Ref<Image> &p_img);
void get_strings_from_variant(const Variant &p_var, Vector<String> &r_strings, const String &engine_version = "");
Error decompress_image(const Ref<Image> &img);
String get_md5(const String &dir, bool ignore_code_signature = false);
String get_md5_for_dir(const String &dir, bool ignore_code_signature = false);
Error unzip_file_to_dir(const String &zip_path, const String &output_dir);
Error wget_sync(const String &p_url, Vector<uint8_t> &response, int retries = 5);
Error download_file_sync(const String &url, const String &output_path);
Error rimraf(const String &dir);
bool dir_is_empty(const String &dir);
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

bool string_is_ascii(const String &s);
bool string_has_whitespace(const String &s);
void get_chars_in_set(const String &s, const HashSet<char32_t> &chars, HashSet<char32_t> &ret);
bool has_chars_in_set(const String &s, const HashSet<char32_t> &chars);
String remove_chars(const String &s, const HashSet<char32_t> &chars);
Vector<String> split_multichar(const String &s, const HashSet<char32_t> &splitters, bool allow_empty = true,
		int maxsplit = 0);
Vector<String> rsplit_multichar(const String &s, const HashSet<char32_t> &splitters, bool allow_empty = true,
		int maxsplit = 0);
} // namespace gdre

#define GDRE_ERR_DECOMPRESS_OR_FAIL(img)                                    \
	{                                                                       \
		Error err = gdre::decompress_image(img);                            \
		if (err == ERR_UNAVAILABLE) {                                       \
			return ERR_UNAVAILABLE;                                         \
		}                                                                   \
		ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to decompress image."); \
	}
// Can only pass in string literals
#define _GDRE_CHECK_HEADER(p_buffer, p_expected_header) gdre::check_header(p_buffer, p_expected_header, sizeof(p_expected_header) - 1)