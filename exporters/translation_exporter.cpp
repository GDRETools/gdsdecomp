#include "translation_exporter.h"

#include "compat/optimized_translation_extractor.h"
#include "compat/resource_loader_compat.h"
#include "core/templates/hash_set.h"
#include "exporters/export_report.h"
#include "utility/common.h"
#include "utility/gd_parallel_hashmap.h"
#include "utility/gdre_settings.h"

#include "core/error/error_list.h"
#include "core/string/optimized_translation.h"
#include "core/string/translation.h"
#include "core/string/ustring.h"
#include "modules/regex/regex.h"
#include <cstdio>

Error TranslationExporter::export_file(const String &out_path, const String &res_path) {
	// Implementation for exporting translation files
	String iinfo_path = res_path.get_basename().get_basename() + ".csv.import";
	auto iinfo = ImportInfo::load_from_file(iinfo_path);
	ERR_FAIL_COND_V_MSG(iinfo.is_null(), ERR_CANT_OPEN, "Cannot find import info for translation.");
	Ref<ExportReport> report = export_resource(out_path.get_base_dir(), iinfo);
	ERR_FAIL_COND_V_MSG(report->get_error(), report->get_error(), "Failed to export translation resource.");
	return OK;
}
#ifdef DEBUG_ENABLED
#define bl_debug(...) print_line(__VA_ARGS__)
#else
#define bl_debug(...) print_verbose(__VA_ARGS__)
#endif

#define TEST_TR_KEY(key)                          \
	test = default_translation->get_message(key); \
	if (test == s) {                              \
		return key;                               \
	}                                             \
	key = key.to_upper();                         \
	test = default_translation->get_message(key); \
	if (test == s) {                              \
		return key;                               \
	}                                             \
	key = key.to_lower();                         \
	test = default_translation->get_message(key); \
	if (test == s) {                              \
		return key;                               \
	}

static const HashSet<char32_t> ALL_PUNCTUATION = { '.', '!', '?', ',', ';', ':', '(', ')', '[', ']', '{', '}', '<', '>', '/', '\\', '|', '`', '~', '@', '#', '$', '%', '^', '&', '*', '-', '_', '+', '=', '\'', '"', '\n', '\t', ' ' };
static const HashSet<char32_t> REMOVABLE_PUNCTUATION = { '.', '!', '?', ',', ';', ':', '%' };
static const Vector<String> STANDARD_SUFFIXES = { "Name", "Text", "Title", "Description", "Label", "Button", "Speech", "Tooltip", "Legend", "Body", "Content", "Hint", "Desc", "UI" };

template <class K, class V>
static constexpr _ALWAYS_INLINE_ const K &get_key(const KeyValue<K, V> &kv) {
	return kv.key;
}

template <class K, class V>
static constexpr _ALWAYS_INLINE_ const K &get_key(const std::pair<K, V> &kv) {
	return kv.first;
}

template <class K, class V>
static constexpr _ALWAYS_INLINE_ const V &get_value(const KeyValue<K, V> &kv) {
	return kv.value;
}

template <class K, class V>
static constexpr _ALWAYS_INLINE_ V &get_value(KeyValue<K, V> &kv) {
	return kv.value;
}

template <class K, class V>
static constexpr _ALWAYS_INLINE_ const V &get_value(const std::pair<K, V> &kv) {
	return kv.second;
}

template <class K, class V>
static constexpr _ALWAYS_INLINE_ V &get_value(std::pair<K, V> &kv) {
	return kv.second;
}

template <typename T>
void update_maximum(std::atomic<T> &maximum_value, T const &value) noexcept {
	T prev_value = maximum_value;
	while (prev_value < value &&
			!maximum_value.compare_exchange_weak(prev_value, value)) {
	}
}

template <class K, class V>
static constexpr _ALWAYS_INLINE_ bool map_has(const HashMap<K, V> &map, const K &key) {
	return map.has(key);
}

template <class K, class V>
static constexpr _ALWAYS_INLINE_ bool map_has(const ParallelFlatHashMap<K, V> &map, const K &key) {
	return map.contains(key);
}

bool map_has_str(const ParallelFlatHashMap<String, String> &map, const String &key) {
	return map.contains(key);
}

struct KeyWorker {
	static constexpr int MAX_FILT_RES_STRINGS_STAGE_3 = 15000;
	static constexpr int MAX_FILT_RES_STRINGS_STAGE_4 = 12000;
	static constexpr int MAX_FILT_RES_STRINGS_STAGE_5 = 5000;
	static constexpr uint64_t MAX_STAGE_TIME = 30 * 1000ULL;

	using KeyType = String;
	using ValueType = String;
	using KeyMessageMap = HashMap<KeyType, ValueType>;

	Vector<KeyType> get_keys(const KeyMessageMap &map) {
		Vector<KeyType> ret;
		for (const auto &E : map) {
			ret.push_back(get_key(E));
		}
		return ret;
	}

	String output_dir;
	Mutex mutex;
	KeyMessageMap key_to_message;
	HashSet<String> resource_strings;
	HashSet<String> filtered_resource_strings;
	Vector<CharString> filtered_resource_strings_t;

	const Ref<OptimizedTranslationExtractor> default_translation;
	const Vector<String> default_messages;
	Vector<Vector<String>> translation_messages;
	const HashSet<String> previous_keys_found;

	Vector<String> keys;
	int64_t dupe_keys = 0;
	bool use_multithread = true;
	std::atomic<bool> keys_have_whitespace = false;
	std::atomic<bool> keys_are_all_upper = true;
	std::atomic<bool> keys_are_all_lower = true;
	std::atomic<bool> keys_are_all_ascii = true;
	bool has_common_prefix = false;
	bool do_combine_all = false; // disabled for now, it's too slow
	bool done_setting_key_stats = false;
	HashMap<char32_t, int64_t> punctuation_counts;
	HashSet<char32_t> punctuation;
	HashSet<CharString> punctuation_str;

	std::atomic<size_t> keys_that_are_all_upper = 0;
	std::atomic<size_t> keys_that_are_all_lower = 0;
	std::atomic<size_t> keys_that_are_all_ascii = 0;
	std::atomic<size_t> keys_that_have_whitespace = 0;
	std::atomic<size_t> max_key_len = 0;
	String common_to_all_prefix;
	Vector<String> common_prefixes;
	Vector<String> common_suffixes;
	Vector<CharString> common_suffixes_t;
	Vector<CharString> common_prefixes_t;

	ParallelFlatHashSet<String> successful_suffixes;
	ParallelFlatHashSet<String> successful_prefixes;

	Ref<RegEx> gd_format_regex;
	Ref<RegEx> word_regex;
	static constexpr const char *GD_FORMAT_REGEX = "(?<!%)%(?:[+\\-]?[0-9*]*\\.?[0-9*]*)?[sdioxXfcv]|%%";
	ParallelFlatHashSet<String> current_stage_keys_found;
	HashMap<String, ParallelFlatHashSet<String>> stage_keys_found;
	HashMap<String, Pair<uint64_t, uint64_t>> stage_time_and_keys_total;
	// 30 seconds in msec
	uint64_t start_time = OS::get_singleton()->get_ticks_usec();
	String path;
	String current_stage;
	//default_translation,  default_messages;
	KeyWorker(const Ref<OptimizedTranslation> &p_default_translation,
			const HashSet<String> &p_previous_keys_found) :
			default_translation(OptimizedTranslationExtractor::create_from(p_default_translation)),
			default_messages(default_translation->get_translated_message_list()),
			previous_keys_found(p_previous_keys_found) {
		gd_format_regex.instantiate();
		gd_format_regex->compile(GD_FORMAT_REGEX);
	}

	String sanitize_key(const String &s) {
		String str = s;
		str = str.replace("\n", "").replace(".", "").replace("…", "").replace("!", "").replace("?", "").strip_escapes().strip_edges();
		return str;
	}

	// make this a template that can take in either a HashMap or a HashMap
	//  use the is_flat_or_parallel_flat_hash_map trait
	static String find_common_prefix(const KeyMessageMap &key_to_msg) {
		// among all the keys in the vector, find the common prefix
		if (key_to_msg.size() == 0) {
			return "";
		}
		String prefix;
		auto add_to_prefix_func = [&](int i) {
			char32_t candidate = 0;
			for (const auto &E : key_to_msg) {
				auto &s = get_key(E);
				if (!s.is_empty()) {
					if (s.length() - 1 < i) {
						return false;
					}
					candidate = s[i];
					break;
				}
			}
			if (candidate == 0) {
				return false;
			}
			for (const auto &E : key_to_msg) {
				auto &s = get_key(E);
				if (!s.is_empty()) {
					if (s.length() - 1 < i || s[i] != candidate) {
						return false;
					}
				}
			}
			prefix += candidate;
			return true;
		};

		for (int i = 0; i < 100; i++) {
			if (!add_to_prefix_func(i)) {
				break;
			}
		}
		return prefix;
	}

	template <bool reverse = false>
	struct StringLengthCompare {
		static _ALWAYS_INLINE_ bool compare(const String &p_lhs, const String &p_rhs) {
			return reverse ? p_lhs.length() > p_rhs.length() : p_lhs.length() < p_rhs.length();
		}

		_ALWAYS_INLINE_ bool operator()(const Variant &p_lhs, const Variant &p_rhs) const {
			return compare(p_lhs, p_rhs);
		}
	};

	template <typename T>
	void find_common_prefixes_and_suffixes(const Vector<T> &res_strings, int count_threshold = 3, bool clear = false) {
		HashMap<String, int> prefix_counts;
		HashMap<String, int> suffix_counts;

		if (clear) {
			common_prefixes.clear();
			common_suffixes.clear();
		}
		auto inc_counts = [&](HashMap<String, int> &counts, const String &part) {
			if (part.is_empty()) {
				return;
			}
			if (counts.has(part)) {
				counts[part] += 1;
			} else {
				counts[part] = 1;
			}
		};

		for (const auto &res_s : res_strings) {
			if (res_s.is_empty()) {
				continue;
			}
			auto parts = gdre::split_multichar(res_s, punctuation, false, 0);
			String prefix = parts.size() > 0 ? parts[0] : "";
			inc_counts(prefix_counts, prefix);
			for (int i = 1; i < parts.size() - 1; i++) {
				auto &part = parts[i];
				int part_start_idx = prefix.length();
				while (part_start_idx < res_s.length()) {
					auto chr = res_s[part_start_idx];
					if (punctuation.has(chr)) {
						prefix += chr;
					} else {
						break;
					}
					part_start_idx++;
				}
				prefix += part;
				inc_counts(prefix_counts, prefix);
			}
			auto suffix_parts = gdre::split_multichar(res_s, punctuation, false, 0);
			String suffix = suffix_parts.size() > 0 ? suffix_parts[suffix_parts.size() - 1] : "";
			inc_counts(suffix_counts, suffix);
			// check if the suffix ends with a number
			if (suffix.is_empty()) {
				continue;
			}
			int end_pad = 0;
			char32_t last_char = suffix[suffix.length() - 1];
			if (last_char >= '0' && last_char <= '9') {
				// strip the trailing numbers
				while (suffix.length() > 0) {
					last_char = suffix[suffix.length() - 1];
					if ((last_char >= '0' && last_char <= '9') || (punctuation.has(last_char))) {
						suffix = suffix.substr(0, suffix.length() - 1);
						end_pad++;
					} else {
						break;
					}
				}
				inc_counts(suffix_counts, suffix);
			}

			for (int i = suffix_parts.size() - 2; i > 0; i--) {
				auto &part = suffix_parts[i];
				int part_end_idx = res_s.length() - (suffix.length() + end_pad) - 1;
				while (part_end_idx > 0) {
					auto chr = res_s[part_end_idx];
					if (punctuation.has(chr)) {
						suffix = chr + suffix;
					} else {
						break;
					}
					part_end_idx--;
				}
				suffix = part + suffix;
				inc_counts(suffix_counts, suffix);
			}
		}
		for (const auto &E : prefix_counts) {
			if (get_value(E) >= count_threshold && !common_prefixes.has(get_key(E))) {
				common_prefixes.push_back(get_key(E));
			}
		}
		for (const auto &E : suffix_counts) {
			if (get_value(E) >= count_threshold && !common_suffixes.has(get_key(E))) {
				common_suffixes.push_back(get_key(E));
			}
		}
		// sort the prefixes and suffixes by length

		common_prefixes.sort_custom<StringLengthCompare<true>>();
		common_suffixes.sort_custom<StringLengthCompare<true>>();
	}

	_FORCE_INLINE_ void _set_key_stuff(const String &key) {
		current_stage_keys_found.insert(key);
		if (done_setting_key_stats) {
			return;
		}
		if (gdre::string_has_whitespace(key)) {
			keys_have_whitespace = true;
			keys_that_have_whitespace++;
		}
		if (key.to_upper() == key) {
			keys_that_are_all_upper++;
		} else {
			keys_are_all_upper = false;
		}
		if (key.to_lower() == key) {
			keys_that_are_all_lower++;
		} else {
			keys_are_all_lower = false;
		}
		if (gdre::string_is_ascii(key)) {
			keys_that_are_all_ascii++;
		} else {
			keys_are_all_ascii = false;
		}
		update_maximum(max_key_len, (size_t)key.length());
		HashSet<char32_t> punctuation_set;
		gdre::get_chars_in_set(key, ALL_PUNCTUATION, punctuation_set);
		for (char32_t p : punctuation_set) {
			if (!punctuation_counts.has(p)) {
				punctuation_counts[p] = 0;
			}
			punctuation_counts[p]++;
		}
		for (char32_t p : punctuation_set) {
			punctuation.insert(p);
			punctuation_str.insert(String::chr(p).utf8());
		}
	}

	_FORCE_INLINE_ bool _set_key(const String &key, const String &msg) {
		MutexLock lock(mutex);
		if (map_has(key_to_message, key)) {
			return true;
		}
		_set_key_stuff(key);

		key_to_message[key] = msg;
		return true;
	}

	_FORCE_INLINE_ bool _set_key(const char *key, const String &msg) {
		return _set_key(String::utf8(key), msg);
	}

	_FORCE_INLINE_ bool try_key(const String &key) {
		auto msg = default_translation->get_message_str(key);
		if (!msg.is_empty()) {
			return _set_key(key, msg);
		}
		return false;
	}

	_FORCE_INLINE_ bool try_key(const char *key) {
		auto msg = default_translation->get_message_str(key);
		if (!msg.is_empty()) {
			return _set_key(key, msg);
		}
		return false;
	}

	constexpr bool is_empty_or_null(const char *str) {
		return !str || *str == 0;
	}

	String combine_string(const char *part1, const char *part2 = "", const char *part3 = "", const char *part4 = "", const char *part5 = "", const char *part6 = "") {
		auto str = String::utf8(part1);
		if (!is_empty_or_null(part2)) {
			str += String::utf8(part2);
		}
		if (!is_empty_or_null(part3)) {
			str += String::utf8(part3);
		}
		if (!is_empty_or_null(part4)) {
			str += String::utf8(part4);
		}
		if (!is_empty_or_null(part5)) {
			str += String::utf8(part5);
		}
		if (!is_empty_or_null(part6)) {
			str += String::utf8(part6);
		}
		return str;
	}

	void reg_successful_prefix(const char *prefix) {
		reg_successful_prefix(String::utf8(prefix));
	}

	void reg_successful_prefix(const String &prefix) {
		if (!prefix.is_empty()) {
			successful_prefixes.insert(prefix);
		}
	}

	void reg_successful_suffix(const char *suffix) {
		reg_successful_suffix(String::utf8(suffix));
	}

	void reg_successful_suffix(const String &suffix) {
		if (!suffix.is_empty()) {
			successful_suffixes.insert(suffix);
		}
	}

	_FORCE_INLINE_ bool try_key_multipart(const char *part1, const char *part2 = "", const char *part3 = "", const char *part4 = "", const char *part5 = "", const char *part6 = "") {
		auto msg = default_translation->get_message_multipart_str(part1, part2, part3, part4, part5, part6);
		if (!msg.is_empty()) {
			auto key = combine_string(part1, part2, part3, part4, part5, part6);
			_set_key(key, msg);
			return true;
		}
		return false;
	}

	template <bool dont_register_success = false>
	bool try_key_prefix(const char *prefix, const char *suffix) {
		if (try_key_multipart(prefix, suffix)) {
			if constexpr (!dont_register_success) {
				reg_successful_prefix(suffix);
			}
			return true;
		}
		for (auto p : punctuation_str) {
			if (try_key_multipart(prefix, p.get_data(), suffix)) {
				if constexpr (!dont_register_success) {
					reg_successful_prefix(suffix);
				}
				return true;
			}
		}
		return false;
	}

	template <bool dont_register_success = false>
	bool try_key_suffix(const char *prefix, const char *suffix) {
		if (try_key_multipart(prefix, suffix)) {
			if constexpr (!dont_register_success) {
				reg_successful_suffix(suffix);
			}
			return true;
		}
		for (auto p : punctuation_str) {
			if (try_key_multipart(prefix, p.get_data(), suffix)) {
				if constexpr (!dont_register_success) {
					reg_successful_suffix(suffix);
				}
				return true;
			}
		}
		return false;
	}

	template <bool dont_register_success = true>
	bool try_key_suffixes(const char *prefix, const char *suffix, const char *suffix2) {
		bool suffix1_empty = !suffix || *suffix == 0;
		if (suffix1_empty) {
			return try_key_suffix<dont_register_success>(prefix, suffix2);
		}
		if (try_key_multipart(prefix, suffix, suffix2)) {
			if constexpr (!dont_register_success) {
				reg_successful_suffix(combine_string(suffix, suffix2));
			}
			return true;
		}
		for (auto p : punctuation_str) {
			if (try_key_multipart(prefix, suffix, p.get_data(), suffix2)) {
				if constexpr (!dont_register_success) {
					reg_successful_suffix(combine_string(suffix, p.get_data(), suffix2));
				}
				return true;
			}
		}
		return false;
	}

	bool try_key_prefix_suffix(const char *prefix, const char *key, const char *suffix) {
		if (try_key_multipart(prefix, key, suffix)) {
			reg_successful_prefix(combine_string(prefix));
			reg_successful_suffix(combine_string(suffix));
			return true;
		}
		for (auto p : punctuation_str) {
			if (try_key_multipart(prefix, p.get_data(), key, p.get_data(), suffix)) {
				reg_successful_prefix(combine_string(prefix));
				reg_successful_suffix(combine_string(suffix));
				return true;
			}
		}
		return false;
	}

	CharString cs_num(int64_t num, int zero_prefix_len) {
		CharString ret;
		ret.resize_uninitialized(32);
		const char *format;
		if (zero_prefix_len > 0) {
			if (zero_prefix_len == 1) {
				format = "%02lld";
			} else if (zero_prefix_len == 2) {
				format = "%03lld";
			} else if (zero_prefix_len == 3) {
				format = "%04lld";
			} else if (zero_prefix_len == 4) {
				format = "%05lld";
			} else if (zero_prefix_len == 5) {
				format = "%06lld";
			} else if (zero_prefix_len == 6) {
				format = "%07lld";
			} else {
				format = "%08lld";
			}
		} else {
			format = "%lld";
		}
		int len = snprintf(ret.ptrw(), 31, format, num);
		ret.resize_uninitialized(len + 1);
		return ret;
	}

	auto try_strip_numeric_suffix(const CharString &p_res_s, int &magnitude) {
		size_t res_s_len = p_res_s.size();
		if (res_s_len < 2) {
			return p_res_s;
		}
		char last_char = p_res_s[res_s_len - 2];
		bool stripped_last_char = false;
		int new_len = res_s_len;
		while (last_char >= '0' && last_char <= '9') {
			stripped_last_char = true;
			new_len = new_len - 1;
			if (new_len == 0) {
				stripped_last_char = false;
				break;
			}
			last_char = p_res_s[new_len - 1];
		}
		CharString res_s_copy;
		String num_str;

		if (stripped_last_char) {
			res_s_copy = p_res_s;
			res_s_copy.resize_uninitialized(new_len + 1);
			res_s_copy[new_len] = '\0';
			String num_str_value = String(p_res_s.get_data() + new_len);
			// check how many zeros are in the num_str
			int zero_count = 0;
			for (int i = 0; i < num_str_value.length(); i++) {
				if (num_str_value[i] == '0') {
					zero_count++;
				} else {
					break;
				}
			}
			magnitude = zero_count;
		} else {
			magnitude = -1;
			return p_res_s;
		}
		return res_s_copy;
	}

	int try_num_suffix(const char *res_s, const char *suffix = "", int magnitude = -1, int max_num = 4, bool force = false) {
		bool found_num = try_key_suffixes(res_s, suffix, "1");
		int zero_prefix_len = magnitude;
		if (magnitude == -1) {
			zero_prefix_len = try_key_suffixes(res_s, suffix, "01") ? 1 : 0;
			if (!found_num && zero_prefix_len == 0) {
				zero_prefix_len = try_key_suffixes(res_s, suffix, "001") ? 2 : 0;
				if (zero_prefix_len == 0) {
					zero_prefix_len = try_key_suffixes(res_s, suffix, "0001") ? 3 : 0;
				}
			}
		}
		int numbers_found = 0;
		if (found_num || zero_prefix_len > 0 || force) {
			try_key_suffixes(res_s, suffix, "N");
			try_key_suffixes(res_s, suffix, "n");
			try_key_suffixes(res_s, suffix, "0");
			bool found_most = true;
			int min_num = 2;
			if (magnitude != -1 || force) {
				min_num = 0;
			}

			while (found_most) {
				int iter_numbers_found = 0;
				bool found_last = false;
				for (int num = min_num; num < max_num; num++) {
					auto nstr = cs_num(num, zero_prefix_len);
					if (try_key_suffixes(res_s, suffix, nstr.get_data())) {
						iter_numbers_found++;
						found_last = true;
					} else {
						found_last = false;
					}
				}
				if (found_last || iter_numbers_found > (max_num - min_num) / 2) {
					found_most = true;
				} else {
					found_most = false;
				}
				min_num = max_num;
				max_num = max_num * 2;
				numbers_found += iter_numbers_found;
			}
		}
		return numbers_found;
	}

	template <bool no_first_try_num_suffix = false>
	void prefix_suffix_task_2(uint32_t i, CharString *res_strings) {
		const CharString &res_s = res_strings[i];
		if constexpr (!no_first_try_num_suffix) {
			try_num_suffix(res_s.get_data());
		}

		for (const auto &E : common_suffixes_t) {
			try_key_suffix(res_s.get_data(), E.get_data());
			try_num_suffix(res_s.get_data(), E.get_data());
		}
		for (const auto &E : common_prefixes_t) {
			try_key_prefix(E.get_data(), res_s.get_data());
			try_num_suffix(E.get_data(), res_s.get_data());
		}
	}

	template <int64_t max_num>
	void stage_4_task(uint32_t i, Pair<CharString, int> *res_strings) {
		const Pair<CharString, int> &res_s_pair = res_strings[i];
		const char *res_s_data = res_s_pair.first.get_data();
		int magnitude = res_s_pair.second;
		int act_max = MAX(max_num, (magnitude + 1) * 10);
		try_num_suffix(res_s_data, nullptr, magnitude, act_max, true);
	}

	void partial_task(uint32_t i, String *res_strings) {
		const String &res_s = res_strings[i];
		if (!has_common_prefix || res_s.contains(common_to_all_prefix)) {
			auto matches = word_regex->search_all(res_s);
			for (const Ref<RegExMatch> match : matches) {
				for (const String &key : match->get_strings()) {
					try_key(key);
				}
			}
		}
	}

	void stage_6_task_2(uint32_t i, CharString *res_strings) {
		const CharString &res_s = res_strings[i];
		auto frs_size = filtered_resource_strings.size();
		for (uint32_t j = 0; j < frs_size; j++) {
			const CharString &res_s2 = res_strings[j];
			try_key_suffix(res_s.get_data(), res_s2.get_data());
		}
	}

	void end_stage() {
		stage_keys_found.insert(current_stage, current_stage_keys_found);
		stage_time_and_keys_total.insert(current_stage, { OS::get_singleton()->get_ticks_msec(), current_stage_keys_found.size() });
		current_stage_keys_found.clear();
	}

	void skip_stage(const String &stage_name) {
		stage_keys_found.insert(stage_name, {});
		stage_time_and_keys_total.insert(stage_name, { 0, 0 });
	}

	bool skipped_last_stage() {
		return stage_time_and_keys_total.last()->value.first == 0;
	}

	static bool check_for_timeout(const uint64_t start_time, const uint64_t max_time) {
		if ((OS::get_singleton()->get_ticks_msec() - start_time) > max_time) {
			return true;
		}
		return false;
	}

	// Does not filter based on spaces
	bool has_nonspace_and_std_punctuation(const String &s) {
		for (int i = 0; i < s.length(); i++) {
			char32_t c = s.ptr()[i];
			if (c != ' ' && !punctuation.has(c) && ALL_PUNCTUATION.has(c)) {
				return true;
			}
		}
		return false;
	}

	bool should_filter(const String &res_s, bool ignore_spaces = false) {
		if (res_s.is_empty()) {
			return true;
		}
		if (res_s.size() > static_cast<int64_t>(max_key_len)) {
			return true;
		}

		// if (filter_punctuation) {
		if (has_nonspace_and_std_punctuation(res_s)) {
			return true;
		}
		// contains any whitespace
		if (!ignore_spaces && !keys_have_whitespace && gdre::string_has_whitespace(res_s)) {
			return true;
		}
		if (res_s.begins_with("res://")) {
			return true;
		}
		if (!common_to_all_prefix.is_empty() && !res_s.begins_with(common_to_all_prefix)) {
			return true;
		}
		if (keys_are_all_upper && res_s.to_upper() != res_s) {
			return true;
		}
		if (keys_are_all_lower && res_s.to_lower() != res_s) {
			return true;
		}
		if (keys_are_all_ascii && !gdre::string_is_ascii(res_s)) {
			return true;
		}
		return false;
	}

	bool basic_filter(const String &res_s) {
		if (!keys_have_whitespace && gdre::string_has_whitespace(res_s)) {
			return true;
		}
		if (keys_are_all_upper && res_s.to_upper() != res_s) {
			return true;
		}
		if (keys_are_all_lower && res_s.to_lower() != res_s) {
			return true;
		}
		if (keys_are_all_ascii && !gdre::string_is_ascii(res_s)) {
			return true;
		}
		return false;
	}

	String remove_removable_punct(const String &s) {
		String ret;
		for (int i = 0; i < s.length(); i++) {
			char32_t c = s.ptr()[i];
			if (punctuation.has(c) || !REMOVABLE_PUNCTUATION.has(c)) {
				ret += c;
			}
		}
		return ret;
	}

	String _san_string_no_spaces(const String &msg) {
		auto msg_str = remove_removable_punct(msg).strip_escapes().strip_edges();

		for (auto ch : punctuation) {
			// strip edges
			msg_str = msg_str.trim_suffix(String::chr(ch)).trim_prefix(String::chr(ch));
		}
		if (msg_str.is_empty() || has_nonspace_and_std_punctuation(msg_str) || (keys_are_all_ascii && !gdre::string_is_ascii(msg_str))) {
			return "";
		}
		if (keys_are_all_upper) {
			msg_str = msg_str.to_upper();
		} else if (keys_are_all_lower) {
			msg_str = msg_str.to_lower();
		}
		return msg_str;
	}

	String sanitize_string(const String &msg) {
		auto msg_str = _san_string_no_spaces(msg);
		if (msg_str.contains(" ")) {
			// choose the most popular one
			char32_t ch = 0;
			int64_t max_count = 0;
			if (punctuation_counts.size() == 1) {
				ch = punctuation_counts.begin()->key;
			} else {
				for (auto kv : punctuation_counts) {
					if (kv.value > max_count) {
						max_count = kv.value;
						ch = kv.key;
					}
				}
			}
			return msg_str.replace(" ", String::chr(ch));
		}
		return msg_str;
	}

	template <class T>
	Vector<String> get_sanitized_strings(const Vector<T> &input_messages) {
		static_assert(std::is_same<T, String>::value || std::is_same<T, StringName>::value, "T must be either String or StringName");
		HashSet<String> new_strings;
		for (const T &msg : input_messages) {
			auto msg_str = _san_string_no_spaces(msg);
			if (msg_str.contains(" ")) {
				for (char32_t p : punctuation) {
					auto nar = msg_str.replace(" ", String::chr(p));
					new_strings.insert(nar);
				}
			} else {
				new_strings.insert(msg_str);
			}
		}
		return gdre::hashset_to_vector(new_strings);
	}

	void get_sanitized_message_strings(Vector<String> &new_strings) {
		auto hshset = filtered_resource_strings;
		for (const auto &msg_str : get_sanitized_strings(default_messages)) {
			if (filtered_resource_strings.has(msg_str)) {
				continue;
			}
			new_strings.push_back(msg_str);
		}
	}

	void extract_middles(const Vector<String> &frs, Vector<String> &middles) {
		auto old_hshset = gdre::vector_to_hashset(frs);
		auto hshset = gdre::vector_to_hashset(frs);
		auto insert_into_hashset = [&](const String &s) {
			if (hshset.has(s)) {
				return false;
			}
			hshset.insert(s);
			middles.push_back(s);
			return true;
		};
		auto trim_punctuation = [&](const String &s) {
			auto ret = s;
			for (auto ch : punctuation) {
				ret = ret.trim_suffix(String::chr(ch)).trim_prefix(String::chr(ch));
			}
			return ret;
		};
		for (auto &res_s : frs) {
			// String s = res_s;
			for (auto &prefix : common_prefixes) {
				if (prefix.length() != res_s.length() && res_s.begins_with(prefix)) {
					auto s = trim_punctuation(res_s.substr(prefix.length()));
					if (!insert_into_hashset(s)) {
						continue;
					}
					for (auto &suffix : common_suffixes) {
						if (suffix.length() != s.length() && s.ends_with(suffix)) {
							auto t = trim_punctuation(s.substr(0, s.length() - suffix.length()));
							insert_into_hashset(t);
						}
					}
				}
			}
			for (auto &suffix : common_suffixes) {
				if (suffix.length() != res_s.length() && res_s.ends_with(suffix)) {
					auto s = trim_punctuation(res_s.substr(0, res_s.length() - suffix.length()));
					insert_into_hashset(s);
				}
			}
		}
	}

	// Rise of the Golden Idol specific hack
	void dynamic_rgi_hack() {
		if (GDRESettings::get_singleton()->get_game_name() == "The Rise of the Golden Idol") {
			constexpr const char *ITEM_TR_SEP = "|";
			constexpr const char *ITEM_TR = "DB_%d";
			constexpr const char *ITEM_TR_PREFIX_ARC = "ARC";
			int min_scenario_id = 0;
			int max_scenario_id = 120;
			int min_arc_id = 0;
			int max_arc_id = 12;
			int max_item_id = 10000;
			char trans_id_buffer[100];
			char composite_trans_id_buffer[100];
			char composite_arc_trans_id_buffer[100];
			char prefix_arc_buffer[100];
			auto get_translation_id = [&](int id) {
				snprintf(trans_id_buffer, sizeof(trans_id_buffer), ITEM_TR, id);
				return trans_id_buffer;
			};
			auto get_composite_translation_id = [&](int scenario_id, int item_id) {
				snprintf(composite_trans_id_buffer, sizeof(composite_trans_id_buffer), "%d%s%s", scenario_id, ITEM_TR_SEP, get_translation_id(item_id));
				return composite_trans_id_buffer;
			};
			auto get_composite_arc_translation_id = [&](int arc_id, int item_id) {
				snprintf(prefix_arc_buffer, sizeof(prefix_arc_buffer), "%s%d", ITEM_TR_PREFIX_ARC, arc_id);
				snprintf(composite_arc_trans_id_buffer, sizeof(composite_arc_trans_id_buffer), "%s%s%s", prefix_arc_buffer, ITEM_TR_SEP, get_translation_id(item_id));
				return composite_arc_trans_id_buffer;
			};
			for (int item_id = 0; item_id < max_item_id; item_id++) {
				try_key(get_translation_id(item_id));
				for (int scenario_id = min_scenario_id; scenario_id < max_scenario_id; scenario_id++) {
					try_key(get_composite_translation_id(scenario_id, item_id));
				}
				for (int arc_id = min_arc_id; arc_id < max_arc_id; arc_id++) {
					try_key(get_composite_arc_translation_id(arc_id, item_id));
				}
			}
			current_stage = "Rise of the Golden Idol Hack";
			end_stage();
		}
	}

	String get_step_desc(uint32_t i, void *userdata) {
		return "Searching for keys for " + path.get_file() + "... (" + current_stage + "/4) ";
	}

	template <typename M, class VE>
	Error run_stage(M p_multi_method, Vector<VE> p_userdata, const String &stage_name, bool multi = true, bool dont_end_stage = false) {
		// assert that M is a method belonging to this class
		auto desc = "TranslationExporter::find_missing_keys::" + stage_name;
		current_stage = stage_name;
		static_assert(std::is_member_function_pointer<M>::value, "M must be a method of this class");
		int tasks = 1;
		if (multi) {
			tasks = -1;
		}
		if (p_userdata.is_empty()) {
			WARN_PRINT(vformat("No userdata to run %s with!", stage_name));
			skip_stage(stage_name);
			return OK;
		}
		Error err = TaskManager::get_singleton()->run_multithreaded_group_task(
				this,
				p_multi_method,
				p_userdata.ptrw(),
				p_userdata.size(),
				&KeyWorker::get_step_desc,
				KeyWorker::get_step_desc(0, nullptr),
				stage_name, true, tasks, true);

		if (!dont_end_stage) {
			end_stage();
		}
		return err;
	}

	uint64_t get_last_stage_keys_found() {
		ERR_FAIL_COND_V(!stage_time_and_keys_total.has(current_stage), 0);
		return stage_time_and_keys_total[current_stage].second;
	}

	bool met_threshold() {
		return (double)default_messages.size() / (double)key_to_message.size() > ((double)1 - TranslationExporter::threshold);
	}

	static const Vector<CharString> vec_string_to_charstring(const Vector<String> &p_strings) {
		Vector<CharString> ret;
		for (const auto &E : p_strings) {
			ret.push_back(E.utf8());
		}
		return ret;
	}

	void pop_charstr_vectors() {
		filtered_resource_strings_t.clear();
		common_prefixes_t.clear();
		common_suffixes_t.clear();
		for (const auto &E : filtered_resource_strings) {
			filtered_resource_strings_t.push_back(E.utf8());
		}
		for (const auto &E : common_prefixes) {
			common_prefixes_t.push_back(E.utf8());
		}
		for (const auto &E : common_suffixes) {
			common_suffixes_t.push_back(E.utf8());
		}
	}

	void stage_1(uint32_t i, String *input_resource_strings) {
		const String &key = input_resource_strings[i];
		try_key(key);
	}

	int64_t pop_keys() {
		int64_t missing_keys = 0;
		dupe_keys = 0;
		Vector<String> dupe_keys_v;
		keys.clear();
		// Sort the key_to_message map by key
		// this does not change the order of the messages as we write them to the CSV
		// This is just to ensure that keys are grouped together in case of duplicate messages
		// e.g. we want:
		// bob_dialogue_1: "Hello",
		// bob_dialogue_2: "I'm Bob",
		// fred_dialogue_1: "Hello",
		// fred_dialogue_2: "I'm Fred"
		// not:
		// fred_dialogue_1: "Hello",
		// bob_dialogue_2: "I'm Bob",
		// bob_dialogue_1: "Hello",
		// fred_dialogue_2: "I'm Fred"
		key_to_message.sort();

		for (int i = 0; i < default_messages.size(); i++) {
			auto &msg = default_messages[i];
			bool all_empty = false;
			if (msg.is_empty()) {
				all_empty = true;
				for (auto &messages : translation_messages) {
					if (messages.size() > i) {
						if (!messages[i].is_empty()) {
							all_empty = false;
							break;
						}
					}
				}
			}
			if (all_empty) {
				// not missing, just empty
				keys.push_back("");
				continue;
			}
			bool found = false;
			bool has_match = false;
			String matching_key;
			for (const auto &E : key_to_message) {
				DEV_ASSERT(!get_value(E).is_empty());

				if (get_value(E) == msg) {
					has_match = true;
					matching_key = get_key(E);
					if (!keys.has(get_key(E))) {
						keys.push_back(get_key(E));
						found = true;
						break;
					}
				}
			}
			if (!found) {
				if (has_match) {
					if (const auto &matching_message = key_to_message[matching_key]; msg != matching_message) {
						WARN_PRINT(vformat("Found matching key '%s' for message '%s' but key is used for message '%s'", matching_key, msg, matching_message));
					} else {
						print_verbose(vformat("WARNING: Found duplicate key '%s' for message '%s'", matching_key, msg));
						dupe_keys++;
						dupe_keys_v.push_back(matching_key);
						keys.push_back(matching_key);
						continue;
					}
				} else {
					print_verbose(vformat("Could not find key for message '%s'", msg));
				}
				missing_keys++;
				keys.push_back(TranslationExporter::MISSING_KEY_PREFIX + String(msg).split("\n")[0] + ">");
			}
		}
		if (dupe_keys > 0) {
			bl_debug(vformat("Found %d duplicate keys: %s", dupe_keys, String(", ").join(dupe_keys_v)));
		}
		return missing_keys;
	}

	Pair<int64_t, int64_t> find_smallest_and_largest_string_lengths(const Vector<String> &strings) {
		int64_t smallest_len = INT64_MAX;
		int64_t largest_len = 0;
		for (const auto &str : strings) {
			if (str.is_empty()) {
				continue;
			}
			if (str.length() < smallest_len) {
				smallest_len = str.length();
			} else if (str.length() > largest_len) {
				largest_len = str.length();
			}
		}
		return { smallest_len, largest_len };
	}

	struct input_sorter {
		bool operator()(const String &a, const String &b) const {
			return a < b;
		}

		bool operator()(const Pair<CharString, int> &a, const Pair<CharString, int> &b) const {
			return a.first < b.first;
		}

		bool operator()(const CharString &a, const CharString &b) const {
			return a < b;
		}
	};

	template <typename T>
	void sort_input(Vector<T> &input) {
		input.template sort_custom<input_sorter>();
	}

	template <typename T>
	void sort_input(HashSet<T> &input) {
		auto vec = gdre::hashset_to_vector(input);
		vec.template sort_custom<input_sorter>();
		input = gdre::vector_to_hashset(vec);
	}
#ifdef DEBUG_ENABLED
#define DEBUG_SORT_INPUT(input) sort_input(input)
#else
#define DEBUG_SORT_INPUT(input)
#endif

	template <typename T>
	Vector<Pair<CharString, int>> get_strings_without_numeric_suffix(T strings) {
		static_assert(std::is_same_v<T, HashSet<String>> || std::is_same_v<T, Vector<String>>, "T must be a HashSet or Vector of Strings");
		HashSet<Pair<CharString, int>> stripped_strings_set;
		for (auto &str : strings) {
			int num_suffix_val = -1;
			CharString ut = str.utf8();
			ut = try_strip_numeric_suffix(ut, num_suffix_val);
			stripped_strings_set.insert({ ut, num_suffix_val });
		}
		auto ret = gdre::hashset_to_vector(stripped_strings_set);
		DEBUG_SORT_INPUT(ret);
		return ret;
	}

#define RET_ON_ERROR(err)      \
	{                          \
		if (err != OK) {       \
			return pop_keys(); \
		}                      \
	}

	void stage_1_try_key(const String &key) {
		if (key.is_empty()) {
			return;
		}
		if (try_key(key)) {
			resource_strings.insert(key);
			return;
		}
		String sanitized_key = sanitize_string(key);
		if (!sanitized_key.is_empty() && try_key(sanitized_key)) {
			resource_strings.insert(sanitized_key);
		}
	}

	template <typename T>
	Vector<String> strip_prefixes(const HashSet<String> &input, const T &prefixes) {
		Vector<String> ret;
		bool found = false;
		for (const auto &str : input) {
			for (const auto &prefix : prefixes) {
				if (str.begins_with(prefix)) {
					ret.push_back(str.trim_prefix(prefix));
					found = true;
					break;
				}
			}
			if (!found) {
				ret.push_back(str);
			}
		}
		return ret;
	}

	template <typename T>
	Vector<String> strip_suffixes(const HashSet<String> &input, const T &suffixes) {
		Vector<String> ret;
		bool found = false;
		for (const auto &str : input) {
			for (const auto &suffix : suffixes) {
				if (str.ends_with(suffix)) {
					ret.push_back(str.trim_suffix(suffix));
					found = true;
					break;
				}
			}
			if (!found) {
				ret.push_back(str);
			}
		}
		return ret;
	}

	Vector<String> to_upper_vector(const Vector<String> &input) {
		Vector<String> ret;
		for (const auto &str : input) {
			ret.push_back(str.to_upper());
		}
		return ret;
	}

	Vector<String> to_lower_vector(const Vector<String> &input) {
		Vector<String> ret;
		for (const auto &str : input) {
			ret.push_back(str.to_lower());
		}
		return ret;
	}

	String remove_all_godot_format_placeholders(const String &str) {
		// Create a regex pattern to match all GDScript format string placeholders
		// This pattern matches:
		// - % followed by optional modifiers (+, -, digits, ., *)
		// - followed by format specifiers (s, c, d, o, x, X, f, v)
		// - handles escaped %% sequences properly
		if (!str.contains("%")) {
			return str;
		}

		String result = str;

		// First, temporarily replace escaped %% with a unique marker
		result = result.replace("%%", "\x01PERCENT\x01");

		// Remove all format placeholders
		result = gd_format_regex->sub(result, "", true);

		// Restore escaped percent signs
		result = result.replace("\x01PERCENT\x01", "%");

		return result;
	}

	Vector<String> split_on_godot_format_placeholders(const String &str) {
		Vector<String> result;
		String current_part;

		// First, temporarily replace escaped %% with a unique marker
		String working_str = str.replace("%%", "\x01PERCENT\x01");

		// Find all matches of format placeholders
		auto matches = gd_format_regex->search_all(working_str);

		if (matches.is_empty()) {
			// No format placeholders found, return the original string (with %% restored)
			result.push_back(working_str.replace("\x01PERCENT\x01", "%"));
			return result;
		}

		int last_end = 0;
		for (const Ref<RegExMatch> match : matches) {
			// Add the text before this placeholder
			int start_pos = match->get_start(0);
			if (start_pos > last_end) {
				String before_placeholder = working_str.substr(last_end, start_pos - last_end);
				if (!before_placeholder.is_empty()) {
					result.push_back(before_placeholder.replace("\x01PERCENT\x01", "%"));
				}
			}

			// Add the placeholder itself
			String placeholder = match->get_string(0);
			result.push_back(placeholder.replace("\x01PERCENT\x01", "%"));

			last_end = match->get_end(0);
		}

		// Add any remaining text after the last placeholder
		if (last_end < working_str.length()) {
			String after_last_placeholder = working_str.substr(last_end);
			if (!after_last_placeholder.is_empty()) {
				result.push_back(after_last_placeholder.replace("\x01PERCENT\x01", "%"));
			}
		}

		return result;
	}

	// Test function to verify format placeholder removal
	void test_format_placeholder_removal() {
		// Test cases based on the GDScript format string documentation
		struct TestCase {
			String input;
			String expected;
		};

		TestCase tests[] = {
			// Basic format specifiers
			{ "Hello %s", "Hello " },
			{ "Value: %d", "Value: " },
			{ "Float: %f", "Float: " },
			{ "Hex: %x", "Hex: " },
			{ "Vector: %v", "Vector: " },

			// With modifiers
			{ "Padded: %10d", "Padded: " },
			{ "Zero-padded: %010d", "Zero-padded: " },
			{ "Precision: %.3f", "Precision: " },
			{ "Combined: %10.3f", "Combined: " },
			{ "Right-aligned: %-10d", "Right-aligned: " },
			{ "With sign: %+d", "With sign: " },

			// Dynamic padding
			{ "Dynamic: %*.*f", "Dynamic: " },
			{ "Zero dynamic: %0*d", "Zero dynamic: " },

			// Multiple placeholders
			{ "%s has %d items", " has  items" },
			{ "%s: %f%%", ": %" }, // Note: %% should be preserved

			// Escaped percent signs
			{ "Health: %d%%", "Health: %" }, // %% becomes %
			{ "%%s is escaped", "%s is escaped" }, // %%s becomes %s
			{ "100%% complete", "100% complete" }, // %% becomes %

			// Complex examples
			{ "Player %s has %d health (%.1f%%)", "Player  has  health (%)" },
			{ "Vector: %v, Int: %d, String: %s", "Vector: , Int: , String: " },

			// Edge cases
			{ "", "" }, // Empty string
			{ "No placeholders", "No placeholders" }, // No placeholders
			{ "%", "%" }, // Lone percent (not a valid placeholder)
			{ "%z", "%z" }, // Invalid format specifier
		};

		for (const TestCase &test : tests) {
			String result = remove_all_godot_format_placeholders(test.input);
			if (result != test.expected) {
				WARN_PRINT(vformat("Format placeholder removal test failed:\nInput: '%s'\nExpected: '%s'\nGot: '%s'",
						test.input, test.expected, result));
			}
		}
	}

	// Test function to verify format placeholder splitting
	void test_format_placeholder_splitting() {
		// Test cases for splitting on format placeholders
		struct SplitTestCase {
			String input;
			Vector<String> expected;
		};

		SplitTestCase tests[] = {
			// Basic format specifiers
			{ "Hello %s", { "Hello ", "%s" } },
			{ "Value: %d", { "Value: ", "%d" } },
			{ "Float: %f", { "Float: ", "%f" } },

			// With modifiers
			{ "Padded: %10d", { "Padded: ", "%10d" } },
			{ "Precision: %.3f", { "Precision: ", "%.3f" } },
			{ "Combined: %10.3f", { "Combined: ", "%10.3f" } },

			// Dynamic padding
			{ "Dynamic: %*.*f", { "Dynamic: ", "%*.*f" } },
			{ "Zero dynamic: %0*d", { "Zero dynamic: ", "%0*d" } },

			// Multiple placeholders
			{ "%s has %d items", { "%s", " has ", "%d", " items" } },
			{ "%s: %f%%", { "%s", ": ", "%f", "%" } }, // %% becomes %

			// Escaped percent signs
			{ "Health: %d%%", { "Health: ", "%d", "%" } }, // %% becomes %
			{ "%%s is escaped", { "%s is escaped" } }, // %%s becomes %s
			{ "100%% complete", { "100% complete" } }, // %% becomes %

			// Complex examples
			{ "Player %s has %d health (%.1f%%)", { "Player ", "%s", " has ", "%d", " health (", "%.1f", "%)" } },
			{ "Vector: %v, Int: %d, String: %s", { "Vector: ", "%v", ", Int: ", "%d", ", String: ", "%s" } },

			// Edge cases
			{ "", { "" } }, // Empty string
			{ "No placeholders", { "No placeholders" } }, // No placeholders
			{ "%", { "%" } }, // Lone percent (not a valid placeholder)
			{ "%z", { "%z" } }, // Invalid format specifier
		};

		for (const SplitTestCase &test : tests) {
			Vector<String> result = split_on_godot_format_placeholders(test.input);
			if (result != test.expected) {
				String result_str = "[" + String(", ").join(result) + "]";
				String expected_str = "[" + String(", ").join(test.expected) + "]";
				WARN_PRINT(vformat("Format placeholder splitting test failed:\nInput: '%s'\nExpected: %s\nGot: %s",
						test.input, expected_str, result_str));
			}
		}
	}

	int64_t run() {
		// Test format placeholder removal functionality
#ifdef DEBUG_ENABLED
		test_format_placeholder_removal();
		test_format_placeholder_splitting();
#endif

		uint64_t missing_keys = 0;
		auto progress = EditorProgressGDDC::create(nullptr, "TranslationExporter - " + path, "Exporting translation " + path + "...", -1, true);
		start_time = OS::get_singleton()->get_ticks_msec();

		uint64_t time_to_load_resource_strings = 0;
		// Stage 1: Unmodified resource strings
		// We need to load all the resource strings in all resources to find the keys
		if (!GDRESettings::get_singleton()->loaded_resource_strings()) {
			GDRESettings::get_singleton()->load_all_resource_strings();
			time_to_load_resource_strings = OS::get_singleton()->get_ticks_msec() - start_time;
			if (GDREConfig::get_singleton()->get_setting("Exporter/Translation/dump_resource_strings", false)) {
				GDRESettings::get_singleton()->get_resource_strings(resource_strings);
				String dir = output_dir.path_join(".assets");
				gdre::ensure_dir(dir);
				Ref<FileAccess> f = FileAccess::open(dir.path_join("resource_strings.stringdump"), FileAccess::WRITE);
				for (const auto &str : resource_strings) {
					// put the bell character in there so that we have a separator between the resource strings
					f->store_string(str + "\b\n");
				}
				f->close();
			}
		}
		GDRESettings::get_singleton()->get_resource_strings(resource_strings);
		DEBUG_SORT_INPUT(resource_strings);
		Error err = run_stage(&KeyWorker::stage_1, gdre::hashset_to_vector(resource_strings), "Resource strings and messages", false, true);
		if (err != OK) {
			return pop_keys();
		}

		// if less than 2% of the keys have whitespace, we can safely assume that the keys don't have whitespace
		if (keys_have_whitespace && (double)keys_that_have_whitespace / (double)key_to_message.size() < (0.02)) {
			keys_have_whitespace = false;
			if (punctuation.has(' ')) {
				punctuation.erase(' ');
				punctuation_str.erase(String::chr(' ').utf8());
				punctuation_counts.erase(' ');
			}
		}

		// Stage 1.25: try the messages themselves
		for (const String &message : default_messages) {
			stage_1_try_key(message);
		}

		for (const Vector<String> &messages : translation_messages) {
			for (const String &message : messages) {
				stage_1_try_key(message);
			}
		}

		// try the basenames of all files in the pack, as filenames can correspond to keys
		if (key_to_message.size() != default_messages.size()) {
			auto file_list = GDRESettings::get_singleton()->get_file_info_list();
			for (auto &file : file_list) {
				String key = file->get_path().get_file().get_basename();
				stage_1_try_key(key);
			}
		}

		// Stage 1.5: Previous keys found
		if (key_to_message.size() != default_messages.size()) {
			if (key_to_message.size() / default_messages.size() > 0.5) {
				done_setting_key_stats = true;
			}
			for (const String &key : previous_keys_found) {
				try_key(key);
			}
			done_setting_key_stats = false;
		}
		end_stage();
		common_to_all_prefix = find_common_prefix(key_to_message);
		has_common_prefix = !common_to_all_prefix.is_empty();

		// the above finds the vast majority of the keys, so we can stop setting key stats
		done_setting_key_stats = true;
		auto refilter_res_strings = [&]() {
			filtered_resource_strings.clear();
			for (String res_s : resource_strings) {
				res_s = remove_all_godot_format_placeholders(res_s);
				if (should_filter(res_s)) {
					continue;
				}
				filtered_resource_strings.insert(res_s);
			}
			return filtered_resource_strings.size();
		};

		// filter resource strings before subsequent stages, as they can be very large
		if (key_to_message.size() != default_messages.size()) {
			refilter_res_strings();
			// check if upper case strings are >90% of the strings
			if ((!keys_are_all_upper && !keys_are_all_lower) || !keys_are_all_ascii || keys_have_whitespace || punctuation_counts.size() > 1) {
				auto threshold = filtered_resource_strings.size() > MAX_FILT_RES_STRINGS_STAGE_3 ? 0.8 : 0.95;
				bool changed = false;
				if (!keys_are_all_upper && (double)keys_that_are_all_upper / (double)key_to_message.size() > threshold) {
					// if so, we can safely assume that the keys are all upper case
					keys_are_all_upper = true;
					changed = true;
				} else if (!keys_are_all_lower && (double)keys_that_are_all_lower / (double)key_to_message.size() > threshold) {
					// if so, we can safely assume that the keys are all lower case
					keys_are_all_lower = true;
					changed = true;
				}
				if (!keys_are_all_ascii && (double)keys_that_are_all_ascii / (double)key_to_message.size() > threshold) {
					// if so, we can safely assume that the keys are all ascii
					keys_are_all_ascii = true;
					changed = true;
				}
				// if less than 20% (or 5%) of the keys have whitespace, we can safely assume that the keys don't have whitespace
				if (keys_have_whitespace && (double)keys_that_have_whitespace / (double)key_to_message.size() < (1.0 - threshold)) {
					keys_have_whitespace = false;
					changed = true;
				}

				if (punctuation_counts.size() > 1) {
					for (const auto [p, count] : punctuation_counts) {
						// if it's used in less than 1% of the keys, we can safely assume that it's not a punctuation mark we have to worry about
						if ((double)count / (double)key_to_message.size() < 0.01) {
							changed = true;
							punctuation.erase(p);
							punctuation_str.erase(String::chr(p).utf8());
						}
					}
				}
				if (changed && filtered_resource_strings.size() > MAX_FILT_RES_STRINGS_STAGE_3) {
					refilter_res_strings();
				}
			}
		}

		// Stage 2: Partial resource strings
		// look for keys in every PART of the resource strings
		// Only do this if no keys have spaces or punctuation is only one character, otherwise it's practically useless
		if (key_to_message.size() != default_messages.size() && (!keys_have_whitespace || punctuation.size() == 1)) {
			Ref<RegEx> re;
			word_regex.instantiate();

			String char_re = "[\\w\\d";
			for (char32_t p : punctuation) {
				char_re += "\\" + String::chr(p);
			}
			char_re += "]";
			if (!keys_have_whitespace) {
				word_regex->compile(common_to_all_prefix + char_re + "+");
			} else {
				word_regex->compile("\\b" + common_to_all_prefix + char_re + "+" + "\\b");
			}

			err = run_stage(&KeyWorker::partial_task, gdre::hashset_to_vector(resource_strings), "Partials");
			RET_ON_ERROR(err);
		} else {
			skip_stage("Partials");
		}

		// Stage 2.75: dynamic_rgi_hack
		dynamic_rgi_hack();

		// Stage 3: Try to find keys with numeric suffixes
		if (key_to_message.size() != default_messages.size()) {
			auto stripped_res_string = get_strings_without_numeric_suffix(filtered_resource_strings);
			stripped_res_string.append_array(get_strings_without_numeric_suffix(get_sanitized_strings(default_messages)));
			Error stage4_err = run_stage(&KeyWorker::stage_4_task<10>, stripped_res_string, "Numeric suffixes", true);
			RET_ON_ERROR(stage4_err);
		} else {
			skip_stage("Numeric suffixes");
		}
		// Stage 3.5: Try to find keys with numeric suffixes (keys only, with max num 1000)
		if (key_to_message.size() != default_messages.size()) {
			// try the same thing but with just the already found keys, and set the max num to try to 1000
			auto stripped_keys = get_strings_without_numeric_suffix(get_keys(key_to_message));
			Error stage4_5_err = run_stage(&KeyWorker::stage_4_task<1000>, stripped_keys, "Numeric suffixes (keys only)", true);
			RET_ON_ERROR(stage4_5_err);
		} else {
			skip_stage("Numeric suffixes (keys only)");
		}

		// Stage 4: commonly known suffixes
		if (key_to_message.size() != default_messages.size()) {
			auto sanitized_standard_suffixes = get_sanitized_strings(STANDARD_SUFFIXES);

			common_suffixes = sanitized_standard_suffixes;
			common_prefixes = sanitized_standard_suffixes;
			// append format strings to the filtered resource strings
			if (!keys_are_all_upper && !keys_are_all_lower) {
				common_prefixes.append_array(to_lower_vector(sanitized_standard_suffixes));
				common_prefixes.append_array(to_upper_vector(sanitized_standard_suffixes));
				common_suffixes.append_array(to_lower_vector(sanitized_standard_suffixes));
				common_suffixes.append_array(to_upper_vector(sanitized_standard_suffixes));
			}
			// looking for format strings; eg "${foo}_DESC"
			if (!keys_have_whitespace && punctuation.size() > 0 && punctuation.size() <= 2) {
				for (const auto &str : filtered_resource_strings) {
					for (const auto &punct : punctuation) {
						if (str.begins_with(String::chr(punct))) {
							common_suffixes.append(str);
							break;
						} else if (str.ends_with(String::chr(punct))) {
							common_prefixes.append(str);
							break;
						}
					}
				}
			}

			if (filtered_resource_strings.size() > MAX_FILT_RES_STRINGS_STAGE_4) {
				auto [smallest_key_len, largest_key_len] = find_smallest_and_largest_string_lengths(get_keys(key_to_message));
				auto min_filter_size = smallest_key_len - 1;
				auto max_filter_size = largest_key_len + 1;
				for (const auto &str : filtered_resource_strings) {
					if (str.length() < min_filter_size) {
						filtered_resource_strings.erase(str);
					} else if (str.length() > max_filter_size) {
						filtered_resource_strings.erase(str);
					}
				}
			}

			if (filtered_resource_strings.size() > MAX_FILT_RES_STRINGS_STAGE_4 || (common_prefixes.size() == 0 && common_suffixes.size() == 0)) {
				bl_debug("Skipping stage 4 because there are too many resource strings");
				skip_stage("Common prefix/suffix");
			} else {
				gdre::hashset_insert_iterable(filtered_resource_strings, get_sanitized_strings(default_messages));
				gdre::hashset_insert_iterable(filtered_resource_strings, get_keys(key_to_message));
				pop_charstr_vectors();
				String stage_name = "Common prefix/suffix";
				Error stage3_err = run_stage(&KeyWorker::prefix_suffix_task_2<false>, filtered_resource_strings_t, "Common prefix/suffix", true);

				RET_ON_ERROR(stage3_err);
			}
		} else {
			skip_stage("Common prefix/suffix");
		}

		// Stage 5: Combine resource strings with detected prefixes and suffixes
		// If we're still missing keys and no keys have spaces, we try combining every string with every other string
		if (!skipped_last_stage() && key_to_message.size() != default_messages.size()) {
			current_stage = "Detected prefix/suffix";
			auto curr_keys = get_keys(key_to_message);
			find_common_prefixes_and_suffixes(curr_keys);

			Vector<String> middle_candidates;
			extract_middles(gdre::hashset_to_vector(filtered_resource_strings), middle_candidates);
			extract_middles(curr_keys, middle_candidates);
			middle_candidates = gdre::hashset_to_vector(gdre::vector_to_hashset(middle_candidates));
			for (auto &middle : middle_candidates) {
				filtered_resource_strings.insert(middle);
			}
			DEBUG_SORT_INPUT(common_prefixes);
			DEBUG_SORT_INPUT(common_suffixes);

			pop_charstr_vectors();
			bool found_prefix_suffix = false;
			// stage 4.1: try to find keys with just prefixes and suffixes
			for (const auto &prefix : common_prefixes_t) {
				for (const auto &suffix : common_suffixes_t) {
					String combined = String::utf8(prefix.get_data()) + String::utf8(suffix.get_data());
					if (try_key_suffix(prefix.get_data(), suffix.get_data())) {
						if (!key_to_message.has(combined)) {
							reg_successful_prefix(prefix.get_data());
							found_prefix_suffix = true;
						}
					}
					try_num_suffix(prefix.get_data(), suffix.get_data());
				}
			}

			if (filtered_resource_strings.size() > MAX_FILT_RES_STRINGS_STAGE_5) {
				// find the smallest prefix and the smallest suffix
				auto [smallest_prefix_len, largest_prefix_len] = find_smallest_and_largest_string_lengths(common_prefixes);
				auto [smallest_suffix_len, largest_suffix_len] = find_smallest_and_largest_string_lengths(common_suffixes);
				// find the smallest and largest key
				auto [smallest_key_len, largest_key_len] = find_smallest_and_largest_string_lengths(get_keys(key_to_message));
				int64_t min_filter_size = smallest_key_len + smallest_prefix_len + smallest_suffix_len;
				int64_t max_filter_size = largest_key_len + largest_prefix_len + largest_suffix_len;
				// remove all strings that are shorter than the min filter size
				size_t size_before = filtered_resource_strings.size();
				for (const auto &str : filtered_resource_strings) {
					if (str.length() < min_filter_size) {
						filtered_resource_strings.erase(str);
					} else if (str.length() > max_filter_size) {
						filtered_resource_strings.erase(str);
					}
				}
				refilter_res_strings();
				bl_debug(vformat("Filtered resource strings size: %d (before: %d)", filtered_resource_strings.size(), (int64_t)size_before));
				pop_charstr_vectors();
			}

			if (filtered_resource_strings.size() <= MAX_FILT_RES_STRINGS_STAGE_5) {
				Error stage4_err = run_stage(&KeyWorker::prefix_suffix_task_2<false>, filtered_resource_strings_t, "Detected prefix/suffix", true);
				RET_ON_ERROR(stage4_err);
			} else {
				bl_debug("Skipping stage 5 because there are too many resource strings");
				skip_stage("Detected prefix/suffix");
			}
		} else {
			skip_stage("Detected prefix/suffix");
		}

		do_combine_all = do_combine_all && !skipped_last_stage() && key_to_message.size() != default_messages.size() && filtered_resource_strings.size() <= MAX_FILT_RES_STRINGS_STAGE_3;
		if (do_combine_all) {
			Error stage5_err = run_stage(&KeyWorker::stage_6_task_2, filtered_resource_strings_t, "Combine all");
			RET_ON_ERROR(stage5_err);
		} else {
			skip_stage("Combine all");
		}

		missing_keys = pop_keys();
		// print out the times taken
		bl_debug("Key guessing took " + itos(OS::get_singleton()->get_ticks_msec() - start_time) + "ms");
		int i = 0;
		uint64_t last_time = 0;
		if (time_to_load_resource_strings > 0) {
			bl_debug("Loading resource strings took " + itos(time_to_load_resource_strings) + "ms");
		}
		for (auto &[stage, time_and_key_total] : stage_time_and_keys_total) {
			auto time = time_and_key_total.first;
			auto num_keys = time_and_key_total.second;
			if (time == 0) {
				bl_debug(vformat("- %s was skipped", stage));
				continue;
			}
			auto delta = i == 0 ? time - start_time - time_to_load_resource_strings : time - last_time;
			bl_debug(vformat("- %s took %dms, found %d keys", stage, (int64_t)delta, (int64_t)num_keys));
			last_time = time;
			i++;
		}
		bl_debug("---------------Found keys-----------------");
		static const HashSet<String> stages_to_skip = { "Resource strings and messages", "Partials", "Rise of the Golden Idol Hack" };
		for (auto &[stage, keys_found] : stage_keys_found) {
			if (keys_found.size() > 0 && !stages_to_skip.has(stage)) {
				size_t key_idx = 0;
				bl_debug("--- Keys found in " + stage + " ---");
				constexpr size_t MAX_KEYS_TO_PRINT = 90;
				if (keys_found.size() > MAX_KEYS_TO_PRINT / 2) {
					bl_debug("******** " + stage + " found a LOT keys");
				}
				for (const auto &key : keys_found) {
					bl_debug("* " + key);
					key_idx++;
					if (key_idx > MAX_KEYS_TO_PRINT) {
						bl_debug(vformat("* ... and %d more keys", (int64_t)(keys_found.size() - MAX_KEYS_TO_PRINT)));
						break;
					}
				}
				bl_debug("----");
			}
		}

		bl_debug(vformat("Total found: %d/%d", default_messages.size() - missing_keys, default_messages.size()));
		bl_debug("-----------------------------------------------------------\n");
		return missing_keys;
	}
};

Ref<ExportReport> TranslationExporter::export_resource(const String &output_dir, Ref<ImportInfo> iinfo) {
	// Implementation for exporting resources related to translations
	Error export_err = OK;
	// translation files are usually imported from one CSV and converted to multiple "<LOCALE>.translation" files
	// TODO: make this also check for the first file in GDRESettings::get_singleton()->get_project_setting("internationalization/locale/translations")
	const String locale_setting_key = GDRESettings::get_singleton()->get_ver_major() >= 4 ? "internationalization/locale/fallback" : "locale/fallback";
	String default_locale = GDRESettings::get_singleton()->pack_has_project_config() && GDRESettings::get_singleton()->has_project_setting(locale_setting_key)
			? GDRESettings::get_singleton()->get_project_setting(locale_setting_key)
			: "en";
	auto dest_files = iinfo->get_dest_files();
	bool has_default_translation = false;
	if (dest_files.size() > 1) {
		for (const String &path : dest_files) {
			if (path.get_basename().get_extension().to_lower() == default_locale) {
				has_default_translation = true;
				break;
			}
		}
	}
	if (!has_default_translation) {
		default_locale = dest_files[0].get_basename().get_extension().to_lower();
		has_default_translation = !default_locale.is_empty();
	}
	bl_debug("\n\n-----------------------------------------------------------");
	bl_debug("Exporting translation file " + iinfo->get_export_dest());
	Vector<Ref<Translation>> translations;
	Vector<Vector<String>> translation_messages;
	Ref<Translation> default_translation;
	Vector<String> default_messages;
	String header = "key";
	Vector<String> keys;
	Ref<ExportReport> report = memnew(ExportReport(iinfo, get_name()));
	report->set_error(ERR_CANT_ACQUIRE_RESOURCE);
	for (String path : dest_files) {
		Ref<Translation> tr = ResourceCompatLoader::non_global_load(path, "", &export_err);
		ERR_FAIL_COND_V_MSG(export_err != OK, report, "Could not load translation file " + iinfo->get_path());
		ERR_FAIL_COND_V_MSG(!tr.is_valid(), report, "Translation file " + iinfo->get_path() + " was not valid");
		String locale = tr->get_locale();
		// TODO: put the default locale at the beginning
		header += "," + locale;
		if (tr->get_class_name() != "OptimizedTranslation") {
			// We have a real translation class, get the keys
			if (keys.size() == 0 && (!has_default_translation || locale.to_lower() == default_locale.to_lower())) {
				List<StringName> key_list;
				tr->get_message_list(&key_list);
				for (auto key : key_list) {
					keys.push_back(key);
				}
			}
		}
		Vector<String> messages = tr->get_translated_message_list();
		if (locale.to_lower() == default_locale.to_lower() ||
				// Some translations don't have the locale set, so we have to check the file name
				(locale == "en" && path.get_basename().get_extension().to_lower() == default_locale.to_lower())) {
			default_messages = messages;
			default_translation = tr;
		}
		translation_messages.push_back(messages);
		translations.push_back(tr);
	}

	if (default_translation.is_null()) {
		default_translation = translations[0];
		default_messages = translation_messages[0];
	}
	// check default_messages for empty strings
	size_t empty_strings = 0;
	for (auto &message : default_messages) {
		if (message.is_empty()) {
			empty_strings++;
		}
	}
	// if >20% of the strings are empty, this probably isn't the default translation; search the rest of the translations for a non-empty string
	if (empty_strings > default_messages.size() * 0.2) {
		size_t best_empty_strings = empty_strings;
		for (int i = 0; i < translations.size(); i++) {
			size_t empties = 0;
			for (auto &message : translation_messages[i]) {
				if (message.is_empty()) {
					empties++;
				}
			}
			if (empties < best_empty_strings) {
				best_empty_strings = empties;
				default_translation = translations[i];
				default_messages = translation_messages[i];
			}
		}
	}
	// We can't recover the keys from Optimized translations, we have to guess
	int missing_keys = 0;
	bool is_optimized = keys.size() == 0;
	if (is_optimized) {
		KeyWorker kw(default_translation, all_keys_found);
		kw.output_dir = output_dir;
		kw.translation_messages = translation_messages;
		kw.path = iinfo->get_path();
		missing_keys = kw.run();
		keys = kw.keys;
		for (auto &key : keys) {
			if (!key.is_empty() && !String(key).begins_with(MISSING_KEY_PREFIX)) {
				all_keys_found.insert(key);
			}
		}
	}
	header += "\n";
	String export_dest = iinfo->get_export_dest();
	// If greater than 15% of the keys are missing, we save the file to the export directory.
	// The reason for this threshold is that the translations may contain keys that are not currently in use in the project.
	bool resave = missing_keys > (default_messages.size() * threshold);
	if (resave) {
		if (!export_dest.begins_with("res://.assets/")) {
			iinfo->set_export_dest("res://.assets/" + iinfo->get_export_dest().replace("res://", ""));
		}
	}
	String output_path = output_dir.simplify_path().path_join(iinfo->get_export_dest().replace("res://", ""));
	export_err = gdre::ensure_dir(output_path.get_base_dir());
	ERR_FAIL_COND_V(export_err, report);
	Ref<FileAccess> f = FileAccess::open(output_path, FileAccess::WRITE, &export_err);
	ERR_FAIL_COND_V(export_err, report);
	ERR_FAIL_COND_V(f.is_null(), report);
	// Set UTF-8 BOM (required for opening with Excel in UTF-8 format, works with all Godot versions)
	f->store_8(0xef);
	f->store_8(0xbb);
	f->store_8(0xbf);
	f->store_string(header);
	for (int i = 0; i < keys.size(); i++) {
		Vector<String> line_values;
		line_values.push_back(keys[i]);
		for (auto &messages : translation_messages) {
			if (i >= messages.size()) {
				line_values.push_back("");
			} else {
				line_values.push_back(messages[i]);
			}
		}
		f->store_csv_line(line_values, ",");
	}
	f->flush();
	f->close();
	report->set_error(OK);
	Dictionary extra_info;
	extra_info["missing_keys"] = missing_keys;
	extra_info["total_keys"] = default_messages.size();
	report->set_extra_info(extra_info);
	if (missing_keys) {
		String translation_export_message = "Could not recover " + itos(missing_keys) + "/" + itos(default_messages.size()) + " keys for " + iinfo->get_source_file() + "\n";
		if (resave) {
			translation_export_message += "Too inaccurate, saved " + iinfo->get_source_file().get_file() + " to " + iinfo->get_export_dest() + "\n";
		}
		report->set_message(translation_export_message);
	}
	if (iinfo->get_ver_major() >= 4) {
		iinfo->set_param("compress", is_optimized);
		iinfo->set_param("delimiter", 0);
	}
	report->set_new_source_path(iinfo->get_export_dest());
	report->set_saved_path(output_path);
	return report;
}

void TranslationExporter::get_handled_types(List<String> *out) const {
	// Add the types of resources that this exporter can handle
	out->push_back("Translation");
	out->push_back("PHashTranslation");
	out->push_back("OptimizedTranslation");
}

void TranslationExporter::get_handled_importers(List<String> *out) const {
	// Add the importers that this exporter can handle
	out->push_back("csv_translation");
	out->push_back("translation_csv");
	out->push_back("translation");
}

String TranslationExporter::get_name() const {
	return EXPORTER_NAME;
}

String TranslationExporter::get_default_export_extension(const String &res_path) const {
	return "csv";
}
