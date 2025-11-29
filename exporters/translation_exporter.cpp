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
#include "utility/glob.h"
#include "utility/pcfg_loader.h"
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
static const Vector<String> STANDARD_SUFFIXES = {
	"Name",
	"Settings",
	"Nav",
	"Puzzle",
	"Path",
	"Text",
	"Title",
	"Description",
	"Label",
	"Button",
	"Speech",
	"Tooltip",
	"Legend",
	"Body",
	"Content",
	"Hint",
	"Desc",
	"ID",
	"UI",
	"DB",
	"Menu",
	"Dialog",
	"Dialogue",
	"Intro",
	"Evidence",
	"Clue",
	"Quote",
	"Event",
	"Tutorial",
	"Item"
};

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

bool all_messages_at_index_are_blank(const Vector<Vector<String>> &translation_messages, int index) {
	for (const Vector<String> &messages : translation_messages) {
		if (index < messages.size() && !messages[index].is_empty()) {
			return false;
		}
	}
	return true;
}

struct KeyWorker {
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

	bool more_thorough_recovery = false;

	enum class Stage {
		RESOURCE_STRINGS_AND_MESSAGES,
		PARTIALS,
		DYNAMIC_RGI_HACK,
		NUM_SUFFIXES,
		NUM_SUFFIXES_KEYS_ONLY,
		COMMON_PREFIX_SUFFIX,
		COMMON_PREFIX_SUFFIX_COMBINED,
		DETECTED_PREFIX_SUFFIX,
		DETECTED_PREFIX_SUFFIX_COMBINED,
	};

	static constexpr int THOROUGH_MAX = 100000;
	static constexpr Stage START_OF_LONG_RUNNING_STAGES = Stage::COMMON_PREFIX_SUFFIX;

	HashMap<Stage, int64_t> step_to_max_filt_res_strings = {
		{ Stage::COMMON_PREFIX_SUFFIX, 10000 },
		{ Stage::COMMON_PREFIX_SUFFIX_COMBINED, 5000 },
		{ Stage::DETECTED_PREFIX_SUFFIX, 7500 },
		{ Stage::DETECTED_PREFIX_SUFFIX_COMBINED, 5000 },
	};

	String output_dir;
	Mutex mutex;
	KeyMessageMap key_to_message;
	HashSet<String> resource_strings;
	HashSet<String> filtered_resource_strings;
	Vector<CharString> filtered_resource_strings_t;

	size_t working_set_size = 0;
	String working_set_size_str;
	char32_t most_popular_punct = 0;
	String most_popular_punct_str;
	CharString most_popular_punct_str_cs;

	const Ref<OptimizedTranslationExtractor> default_translation;
	const Vector<String> default_messages;
	Vector<Vector<String>> translation_messages;
	const HashSet<String> previous_keys_found;

	Vector<String> keys;
	int64_t dupe_keys = 0;
	int64_t non_blank_keys = 0;
	bool use_multithread = true;
	std::atomic<bool> keys_have_whitespace = false;
	std::atomic<bool> keys_are_all_upper = true;
	std::atomic<bool> keys_are_all_lower = true;
	std::atomic<bool> keys_are_all_ascii = true;
	bool has_common_prefix = false;
	bool do_combine_all = false; // disabled for now, it's too slow
	size_t max_keys_before_done_setting_key_stats = 0;
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

	Vector<String> found_prefixes;
	Vector<String> found_suffixes;
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
	HashMap<String, HashSet<String>> stage_keys_found;
	HashMap<String, Pair<uint64_t, uint64_t>> stage_time_and_keys_total;
	// 30 seconds in msec
	uint64_t start_time = OS::get_singleton()->get_ticks_usec();
	String path;
	String current_stage;
	//default_translation,  default_messages;
	KeyWorker(const Ref<OptimizedTranslation> &p_default_translation,
			const HashSet<String> &p_previous_keys_found,
			const Vector<Vector<String>> &p_translation_messages) :
			default_translation(OptimizedTranslationExtractor::create_from(p_default_translation)),
			default_messages(default_translation->get_translated_message_list()),
			translation_messages(p_translation_messages),
			previous_keys_found(p_previous_keys_found) {
		gd_format_regex.instantiate();
		gd_format_regex->compile(GD_FORMAT_REGEX);

		more_thorough_recovery = GDREConfig::get_singleton()->get_setting("Exporter/Translation/more_thorough_recovery", false);

		non_blank_keys = 0;
		for (int64_t i = 0; i < default_messages.size(); i++) {
			if (!all_messages_at_index_are_blank(translation_messages, i)) {
				non_blank_keys++;
			}
		}
		set_step_limits();
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
	void find_common_prefixes_and_suffixes(const Vector<T> &res_strings, float threshold = 0.01f, bool clear = false) {
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
		int64_t count_threshold = key_to_message.size() * threshold;
		for (const auto &E : prefix_counts) {
			const String &key = get_key(E);
			if (!key.is_empty() && get_value(E) >= count_threshold && !common_prefixes.has(key)) {
				common_prefixes.push_back(key);
			}
		}
		for (const auto &E : suffix_counts) {
			const String &key = get_key(E);
			if (!key.is_empty() && get_value(E) >= count_threshold && !common_suffixes.has(key)) {
				common_suffixes.push_back(key);
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
				reg_successful_prefix(prefix);
			}
			return true;
		}
		for (auto p : punctuation_str) {
			if (try_key_multipart(prefix, p.get_data(), suffix)) {
				if constexpr (!dont_register_success) {
					reg_successful_prefix(prefix);
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
			if (try_key_multipart(prefix, p.get_data(), suffix, p.get_data(), suffix2)) {
				if constexpr (!dont_register_success) {
					reg_successful_suffix(combine_string(prefix, p.get_data(), suffix, p.get_data(), suffix2));
				}
				return true;
			}
			if (try_key_multipart(prefix, suffix, p.get_data(), suffix2)) {
				if constexpr (!dont_register_success) {
					reg_successful_suffix(combine_string(suffix, p.get_data(), suffix2));
				}
				return true;
			}
			if (try_key_multipart(prefix, p.get_data(), suffix, suffix2)) {
				if constexpr (!dont_register_success) {
					reg_successful_suffix(combine_string(prefix, p.get_data(), suffix, suffix2));
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

	static String strip_numeric_suffix(const String &str) {
		auto new_len = str.length();
		for (int64_t i = str.length() - 1; i >= 0; i--) {
			if (str[i] >= '0' && str[i] <= '9') {
				new_len--;
			} else {
				break;
			}
		}
		return str.substr(0, new_len);
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

	template <bool try_prefix_suffix = false>
	void prefix_suffix_task_2(uint32_t i, CharString *res_strings) {
		const CharString &res_s = res_strings[i];
		try_num_suffix(res_s.get_data());

		for (const auto &E : common_suffixes_t) {
			try_key_suffix(res_s.get_data(), E.get_data());
			try_num_suffix(res_s.get_data(), E.get_data());
		}
		for (const auto &E : common_prefixes_t) {
			try_key_prefix(E.get_data(), res_s.get_data());
			try_num_suffix(E.get_data(), res_s.get_data());
		}
		if (try_prefix_suffix) {
			prefix_and_suffix_task(i, res_strings);
		}
	}

	void prefix_and_suffix_task(uint32_t i, CharString *res_strings) {
		const CharString &res_s = res_strings[i];
		for (const auto &E : common_prefixes_t) {
			for (const auto &E2 : common_suffixes_t) {
				try_key_prefix_suffix(E.get_data(), res_s.get_data(), E2.get_data());
				std::string f = E.get_data();
				// check if f ends with most_popular_punct_str
				if (f.back() != most_popular_punct) {
					f += most_popular_punct_str_cs.get_data();
				}
				f += res_s.get_data();
				try_num_suffix(f.c_str(), E2.get_data());
			}
		}
	}
	template <int64_t max_num>
	void num_suffix_task(uint32_t i, Pair<CharString, int> *res_strings) {
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
		for (uint32_t j = 0; j < working_set_size; j++) {
			const CharString &res_s2 = res_strings[j];
			try_key_suffix<true>(res_s.get_data(), res_s2.get_data());
			for (const auto &E : common_suffixes_t) {
				try_key_suffixes(res_s.get_data(), res_s2.get_data(), E.get_data());
			}
			for (const auto &E : common_prefixes_t) {
				try_key_suffixes(E.get_data(), res_s.get_data(), res_s2.get_data());
			}

			// std::string f = res_s.get_data();
			// if (f.back() != most_popular_punct) {
			// 	f += most_popular_punct_str_cs.get_data();
			// }
			// f += res_s2.get_data();
			// CharString f_cs = f.c_str();
			// prefix_suffix_task_2<true>(0, &f_cs);
		}
	}

	void end_stage() {
		HashSet<String> hs;
		for (const auto &E : current_stage_keys_found) {
			hs.insert(E);
		}
		stage_keys_found.insert(current_stage, hs);
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

	inline bool should_filter(const String &res_s, bool ignore_spaces = false) {
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
			return msg_str.replace(" ", get_most_popular_punctuation_str());
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

	char32_t get_most_popular_punctuation() {
		char32_t punct = 0;
		int64_t max_count = 0;
		for (auto kv : punctuation_counts) {
			if (kv.value > max_count) {
				max_count = kv.value;
				punct = kv.key;
			}
		}
		return punct;
	}

	String get_most_popular_punctuation_str() {
		auto ch = get_most_popular_punctuation();
		return ch != 0 ? String::chr(ch) : "";
	}

	void extract_middles(const Vector<String> &frs, HashSet<String> &middles) {
		auto old_hshset = gdre::vector_to_hashset(frs);
		auto insert_into_hashset = [&](const String &s) {
			if (s.is_numeric() || s.is_empty()) {
				return false;
			}
			if (middles.has(s)) {
				return false;
			}
			middles.insert(s);
			return true;
		};
		set_most_popular_punctuation();
		bool has_punct = most_popular_punct != 0;
		for (auto &res_s_f : frs) {
			String res_s = trim_punctuation(strip_numeric_suffix(res_s_f));
			if (res_s.is_empty()) {
				continue;
			}
			for (auto &prefix : common_prefixes) {
				if (prefix.length() != res_s.length() && res_s.begins_with(prefix)) {
					auto s = res_s.substr(prefix.length());
					if (has_punct && !starts_with_punctuation(s)) {
						continue;
					}
					s = trim_punctuation(s);
					if (!insert_into_hashset(s)) {
						continue;
					}
					for (auto &suffix : common_suffixes) {
						if (suffix.length() != s.length() && s.ends_with(suffix)) {
							auto t = s.substr(0, s.length() - suffix.length());
							if (has_punct && !ends_with_punctuation(t)) {
								continue;
							}
							t = trim_punctuation(t);
							insert_into_hashset(t);
						}
					}
				}
			}
			for (auto &suffix : common_suffixes) {
				if (suffix.length() != res_s.length() && res_s.ends_with(suffix)) {
					auto s = res_s.substr(0, res_s.length() - suffix.length());
					if (has_punct && !ends_with_punctuation(s)) {
						continue;
					}
					s = trim_punctuation(s);
					insert_into_hashset(s);
				}
			}
		}
		for (auto &s : gdre::hashset_to_vector(middles)) {
			if (s.contains(most_popular_punct_str)) {
				auto split = s.split(most_popular_punct_str);
				for (auto &part : split) {
					if (part.is_empty()) {
						continue;
					}
					insert_into_hashset(part);
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

	template <typename T>
	String get_step_desc(uint32_t i, void *userdata) {
		return vformat("%d / %s", (int64_t)i, working_set_size_str);
	}

	void set_most_popular_punctuation() {
		most_popular_punct = get_most_popular_punctuation();
		most_popular_punct_str = get_most_popular_punctuation_str();
		most_popular_punct_str_cs = most_popular_punct_str.utf8();
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
		working_set_size = p_userdata.size();
		working_set_size_str = String::num_uint64(working_set_size);
		set_most_popular_punctuation();
		String label = "Key search: " + stage_name;

		Error err = TaskManager::get_singleton()->run_multithreaded_group_task(
				this,
				p_multi_method,
				p_userdata.ptrw(),
				p_userdata.size(),
				&KeyWorker::get_step_desc<VE>,
				desc,
				label, true, tasks, true);

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

	template <typename T>
	static const Vector<CharString> iter_string_to_charstring(const T &iter) {
		Vector<CharString> ret;
		for (const auto &E : iter) {
			ret.push_back(E.utf8());
		}
		return ret;
	}

	void pop_charstr_vectors() {
		filtered_resource_strings_t.clear();
		common_prefixes_t.clear();
		common_suffixes_t.clear();
		filtered_resource_strings_t = iter_string_to_charstring(filtered_resource_strings);
		common_prefixes_t = iter_string_to_charstring(common_prefixes);
		common_suffixes_t = iter_string_to_charstring(common_suffixes);
	}

	void stage_1(uint32_t i, String *input_resource_strings) {
		const String &key = input_resource_strings[i];
		try_key(key);
	}

	int64_t pop_keys(bool quiet = false) {
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
						if (!quiet) {
							WARN_PRINT(vformat("Found matching key '%s' for message '%s' but key is used for message '%s'", matching_key, msg, matching_message));
						}
						dupe_keys++;
						dupe_keys_v.push_back(matching_key);
					} else {
						// This usually means that the same message is present multiple times with different keys and we failed to find the others
						if (!quiet) {
							print_verbose(vformat("Found duplicate key '%s' for message '%s'", matching_key, msg));
						}
						dupe_keys++;
						dupe_keys_v.push_back(matching_key);
						keys.push_back(matching_key);
						continue;
					}
				} else if (!quiet) {
					print_verbose(vformat("Could not find key for message '%s'", msg));
				}
				missing_keys++;
				keys.push_back(vformat(TranslationExporter::MISSING_KEY_FORMAT, i, String(msg).split("\n")[0]));
			}
		}
		if (dupe_keys > 0 && !quiet) {
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

		if (!str.contains("%")) {
			return { str };
		}

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

	bool starts_with_punctuation(const String &str) {
		for (const auto &punct : punctuation) {
			if (str.begins_with(String::chr(punct))) {
				return true;
			}
		}
		return false;
	}

	bool ends_with_punctuation(const String &str) {
		for (const auto &punct : punctuation) {
			if (str.ends_with(String::chr(punct))) {
				return true;
			}
		}
		return false;
	}

	String trim_punctuation(const String &str) {
		String result = str;
		for (const auto &punct : punctuation) {
			String punct_str = String::chr(punct);
			while (result.begins_with(punct_str)) {
				result = result.trim_prefix(punct_str);
			}
			while (result.ends_with(punct_str)) {
				result = result.trim_suffix(punct_str);
			}
		}
		return result;
	}

	void trim_punctuation_vec(Vector<String> &iter) {
		for (int64_t i = 0; i < iter.size(); i++) {
			iter.write[i] = trim_punctuation(iter[i]);
		}
	}

	void prep_common_prefix_suffix() {
		trim_punctuation_vec(common_prefixes);
		trim_punctuation_vec(common_suffixes);
		dedupe(common_prefixes);
		dedupe(common_suffixes);
	}

	void dedupe(Vector<String> &iter) {
		for (int64_t i = iter.size() - 1; i >= 0; i--) {
			if (iter[i].is_empty()) {
				iter.remove_at(i);
			}
		}
		iter = gdre::hashset_to_vector(gdre::vector_to_hashset(iter));
	}

	template <typename T, typename R>
	static Vector<R> transform_vector(const Vector<T> &iter, std::function<R(const T &)> func) {
		Vector<R> result;
		for (const auto &E : iter) {
			result.push_back(func(E));
		}
		return result;
	}

	template <typename T>
	static Vector<T> filter_vector(const Vector<T> &iter, std::function<bool(const T &)> func) {
		Vector<T> result;
		for (const auto &E : iter) {
			if (func(E)) {
				result.push_back(E);
			}
		}
		return result;
	}

	HashSet<String> get_prefix_suffix_working_set() {
		HashSet<String> working_set;
		extract_middles(gdre::hashset_to_vector(filtered_resource_strings), working_set);
		extract_middles(get_keys(key_to_message), working_set);
		gdre::hashset_insert_iterable(working_set, filtered_resource_strings);
		gdre::hashset_insert_iterable(working_set, get_sanitized_strings(default_messages));
		gdre::hashset_insert_iterable(working_set, transform_vector<String, String>(get_keys(key_to_message), [&](const String &str) -> String {
			return trim_punctuation(strip_numeric_suffix(str));
		}));
		return working_set;
	}

	bool normalize_key_characteristics(double p_threshold = 0.95) {
		auto threshold = p_threshold;
		bool changed = false;
		double size = !done_setting_key_stats ? key_to_message.size() : max_keys_before_done_setting_key_stats;
		if (!keys_are_all_upper && (double)keys_that_are_all_upper / size > threshold) {
			// if so, we can safely assume that the keys are all upper case
			keys_are_all_upper = true;
			changed = true;
		} else if (!keys_are_all_lower && (double)keys_that_are_all_lower / size > threshold) {
			// if so, we can safely assume that the keys are all lower case
			keys_are_all_lower = true;
			changed = true;
		}
		if (!keys_are_all_ascii && (double)keys_that_are_all_ascii / size > threshold) {
			// if so, we can safely assume that the keys are all ascii
			keys_are_all_ascii = true;
			changed = true;
		}
		// if less than 20% (or 5%) of the keys have whitespace, we can safely assume that the keys don't have whitespace
		if (keys_have_whitespace && (double)keys_that_have_whitespace / size < (1.0 - threshold)) {
			keys_have_whitespace = false;
			changed = true;
			for (const auto p : gdre::hashset_to_vector(punctuation)) {
				if (is_whitespace(p)) {
					punctuation_counts.erase(p);
					punctuation.erase(p);
					punctuation_str.erase(String::chr(p).utf8());
					changed = true;
				}
			}
		}

		if (punctuation_counts.size() > 1) {
			for (const auto p : gdre::hashset_to_vector(punctuation)) {
				auto count = punctuation_counts.has(p) ? punctuation_counts[p] : 0;
				// if it's used in less than 1% of the keys, we can safely assume that it's not a punctuation mark we have to worry about
				if ((double)count / (double)key_to_message.size() < 0.01) {
					punctuation_counts.erase(p);
					punctuation.erase(p);
					punctuation_str.erase(String::chr(p).utf8());
					changed = true;
				}
			}
		}
		return changed;
	}

	void filter_resource_strings(int64_t size_min = -1, int64_t size_max = INT64_MAX) {
		bool no_filter_size_limits = size_min == -1 && size_max == INT64_MAX;
		filtered_resource_strings.clear();

		for (String res_s : resource_strings) {
			auto splits = split_on_godot_format_placeholders(res_s);
			bool all_non_placeholder_strings_are_valid = true;
			for (int i = 0; i < splits.size(); i++) {
				String &split = splits.write[i];
				split = remove_all_godot_format_placeholders(split);
				if (split.is_empty()) {
					continue;
				}
				if (!should_filter(split)) {
					if (!no_filter_size_limits && (split.length() < size_min || split.length() > size_max)) {
						all_non_placeholder_strings_are_valid = false;
						continue;
					}
					filtered_resource_strings.insert(split);
				} else {
					all_non_placeholder_strings_are_valid = false;
				}
			}
			if (more_thorough_recovery && splits.size() > 1 && all_non_placeholder_strings_are_valid) {
				auto &first = splits[0];
				auto &last = splits[splits.size() - 1];
				if (!should_filter(first) && !common_prefixes.has(first) && first.length() > 1) {
					found_prefixes.push_back(splits[0]);
				}
				if (!should_filter(last) && !common_suffixes.has(last) && last.length() > 1) {
					found_suffixes.push_back(last);
				}
			}
		}
	}

	bool all_keys_present() {
		return non_blank_keys == key_to_message.size();
	}

	void set_step_limits() {
		if (more_thorough_recovery) {
			for (auto &step : step_to_max_filt_res_strings) {
				step.value = THOROUGH_MAX;
			}
		}
	}

	bool step_too_long(Stage step) {
		if (!step_to_max_filt_res_strings.has(step)) {
			return false;
		}
		auto found_key_ratio = (double)key_to_message.size() / (double)non_blank_keys;
		double adjustment = MAX(found_key_ratio, 0.70); // don't adjust more than 30%
		auto adjusted_frs_size = filtered_resource_strings.size() * adjustment;
		if (adjusted_frs_size > step_to_max_filt_res_strings[step]) {
			return true;
		}
		return false;
	}

	bool should_run_step(Stage step) {
		if (all_keys_present()) {
			return false;
		}
		if (!more_thorough_recovery && step >= START_OF_LONG_RUNNING_STAGES && (double)key_to_message.size() / (double)non_blank_keys >= 0.99) {
			return false;
		}
		return !step_too_long(step);
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

		// Stage 1.25: try the messages themselves; normalize the key characteristics first so that the transformations are consistent
		normalize_key_characteristics(0.99);

		if (should_run_step(Stage::RESOURCE_STRINGS_AND_MESSAGES)) {
			for (const Vector<String> &messages : translation_messages) {
				for (const String &message : messages) {
					stage_1_try_key(message);
				}
			}
		}

		// try the basenames of all files in the pack, as filenames can correspond to keys
		if (should_run_step(Stage::RESOURCE_STRINGS_AND_MESSAGES)) {
			auto file_list = GDRESettings::get_singleton()->get_file_info_list();
			for (auto &file : file_list) {
				String key = file->get_path().get_file().get_basename();
				stage_1_try_key(key);
			}
		}

		// Stage 1.5: Previous keys found
		if (should_run_step(Stage::RESOURCE_STRINGS_AND_MESSAGES)) {
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
		max_keys_before_done_setting_key_stats = key_to_message.size();
		done_setting_key_stats = true;

		// Stage 2: Partial resource strings
		// look for keys in every PART of the resource strings
		// Only do this if no keys have spaces or punctuation is only one character, otherwise it's practically useless
		if (should_run_step(Stage::PARTIALS) && (!keys_have_whitespace || punctuation.size() == 1)) {
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
		if (should_run_step(Stage::DYNAMIC_RGI_HACK)) {
			dynamic_rgi_hack();
		}

		// filter resource strings before subsequent stages, as they can be very large
		if (should_run_step(Stage::NUM_SUFFIXES)) {
			normalize_key_characteristics();
			filter_resource_strings();
		}

		// Stage 3: Try to find keys with numeric suffixes
		if (should_run_step(Stage::NUM_SUFFIXES)) {
			auto stripped_res_string = get_strings_without_numeric_suffix(filtered_resource_strings);
			stripped_res_string.append_array(get_strings_without_numeric_suffix(get_sanitized_strings(default_messages)));
			Error stage4_err = run_stage(&KeyWorker::num_suffix_task<10>, stripped_res_string, "Numeric suffixes", true);
			RET_ON_ERROR(stage4_err);
		} else {
			skip_stage("Numeric suffixes");
		}
		// Stage 3.5: Try to find keys with numeric suffixes (keys only, with max num 1000)
		if (should_run_step(Stage::NUM_SUFFIXES_KEYS_ONLY)) {
			// try the same thing but with just the already found keys, and set the max num to try to 1000
			auto stripped_keys = get_strings_without_numeric_suffix(get_keys(key_to_message));
			Error stage4_5_err = run_stage(&KeyWorker::num_suffix_task<1000>, stripped_keys, "Numeric suffixes (keys only)", true);
			RET_ON_ERROR(stage4_5_err);
		} else {
			skip_stage("Numeric suffixes (keys only)");
		}

		// looking for format strings; eg "${foo}_DESC"

		if (step_too_long(Stage::COMMON_PREFIX_SUFFIX)) {
			// drop the normalization threshold way down
			normalize_key_characteristics(0.70);
			filter_resource_strings();
		}

		if (!keys_have_whitespace && punctuation.size() > 0 && punctuation.size() <= 2) {
			for (const auto &str : filtered_resource_strings) {
				for (const auto &punct : punctuation) {
					if (str == String::chr(punct)) {
						break;
					}
					// _ prefix is unfortunately way too common, used in function names a lot, we have to filter them out
					if (punct != '_' && str.begins_with(String::chr(punct))) {
						found_suffixes.append(str);
						break;
					} else if (str.ends_with(String::chr(punct))) {
						found_prefixes.append(str);
						break;
					}
				}
			}
		}
		trim_punctuation_vec(found_prefixes);
		trim_punctuation_vec(found_suffixes);

		if (step_too_long(Stage::COMMON_PREFIX_SUFFIX)) {
			auto [smallest_key_len, largest_key_len] = find_smallest_and_largest_string_lengths(get_keys(key_to_message));
			auto min_filter_size = smallest_key_len - 1;
			auto max_filter_size = largest_key_len + 1;
			filter_resource_strings(min_filter_size, max_filter_size);
		}

		// Stage 4: commonly known suffixes
		if (should_run_step(Stage::COMMON_PREFIX_SUFFIX)) {
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
			common_prefixes.append_array(found_prefixes);
			common_suffixes.append_array(found_suffixes);
			prep_common_prefix_suffix();

			if ((common_prefixes.size() == 0 && common_suffixes.size() == 0)) {
				skip_stage("Common prefix/suffix");
			} else {
				HashSet<String> working_set = get_prefix_suffix_working_set();
				pop_charstr_vectors();
				Vector<CharString> working_set_t = iter_string_to_charstring(working_set);
				String stage_name = "Common prefix/suffix";
				Error stage3_err = run_stage(&KeyWorker::prefix_suffix_task_2<false>, working_set_t, "Common prefix/suffix", true);

				RET_ON_ERROR(stage3_err);
				// stage 5: test prefixes AND suffixes together
				if (should_run_step(Stage::COMMON_PREFIX_SUFFIX_COMBINED)) {
					common_prefixes = found_prefixes;
					common_suffixes = found_suffixes;
					for (const auto &prefix : successful_prefixes) {
						common_prefixes.append(prefix);
					}
					for (const auto &suffix : successful_suffixes) {
						common_suffixes.append(suffix);
					}
					prep_common_prefix_suffix();
					stage3_err = run_stage(&KeyWorker::prefix_suffix_task_2<true>, working_set_t, "Common prefix/suffix (combined)", true);
					RET_ON_ERROR(stage3_err);
				}
			}
		} else {
			if (step_too_long(Stage::COMMON_PREFIX_SUFFIX)) {
				bl_debug("Skipping stage 4 because there are too many resource strings");
			}
			skip_stage("Common prefix/suffix");
		}

		// Stage 5: Combine resource strings with detected prefixes and suffixes
		// If we're still missing keys and no keys have spaces, we try combining every string with every other string
		if (!all_keys_present()) {
			current_stage = "Detected prefix/suffix";
			auto curr_keys = get_keys(key_to_message);
			auto old_common_prefixes = common_prefixes;
			auto old_common_suffixes = common_suffixes;
			find_common_prefixes_and_suffixes(curr_keys);

			common_prefixes.append_array(old_common_prefixes);
			common_suffixes.append_array(old_common_suffixes);
			prep_common_prefix_suffix();

			common_prefixes = filter_vector<String>(common_prefixes, [](const String &str) {
				return !str.is_empty() && !str.is_numeric();
			});
			common_suffixes = filter_vector<String>(common_suffixes, [](const String &str) {
				return !str.is_empty() && !str.is_numeric();
			});

			pop_charstr_vectors();
			// stage 4.1: try to find keys with just prefixes and suffixes
			for (const auto &prefix : common_prefixes_t) {
				for (const auto &suffix : common_suffixes_t) {
					String combined = String::utf8(prefix.get_data()) + String::utf8(suffix.get_data());
					if (try_key_suffix<true>(prefix.get_data(), suffix.get_data())) {
						// if (!key_to_message.has(combined)) {
						// 	reg_successful_prefix(prefix.get_data());
						// 	found_prefix_suffix = true;
						// }
					}
					try_num_suffix(prefix.get_data(), suffix.get_data());
				}
			}

			if (should_run_step(Stage::DETECTED_PREFIX_SUFFIX)) {
				HashSet<String> working_set = get_prefix_suffix_working_set();
				pop_charstr_vectors();
				Vector<CharString> working_set_t = iter_string_to_charstring(working_set);

				Error stage4_err = run_stage(&KeyWorker::prefix_suffix_task_2<false>, working_set_t, "Detected prefix/suffix", true);
				RET_ON_ERROR(stage4_err);
				if (should_run_step(Stage::DETECTED_PREFIX_SUFFIX_COMBINED)) {
					auto check_prefixes = common_prefixes;
					auto check_suffixes = common_suffixes;
					common_prefixes = found_prefixes;
					common_suffixes = found_suffixes;
					auto keys_found = stage_keys_found.get("Detected prefix/suffix");
					if (stage_keys_found.has("Common prefix/suffix")) {
						gdre::hashset_insert_iterable(keys_found, stage_keys_found.get("Common prefix/suffix"));
					}
					if (stage_keys_found.has("Common prefix/suffix (combined)")) {
						gdre::hashset_insert_iterable(keys_found, stage_keys_found.get("Common prefix/suffix (combined)"));
					}
					for (const auto &prefix : check_prefixes) {
						for (const auto &key : keys_found) {
							if (key.begins_with(prefix)) {
								common_prefixes.append(prefix);
								break;
							}
						}
					}
					for (const auto &suffix : check_suffixes) {
						for (const auto &key : keys_found) {
							if (key.ends_with(suffix)) {
								common_suffixes.append(suffix);
								break;
							}
						}
					}
					trim_punctuation_vec(common_prefixes);
					trim_punctuation_vec(common_suffixes);
					pop_charstr_vectors();

					stage4_err = run_stage(&KeyWorker::prefix_suffix_task_2<true>, working_set_t, "Detected prefix/suffix (combined)", true);
					RET_ON_ERROR(stage4_err);
				}
			} else {
				bl_debug("Skipping stage 5 because there are too many resource strings");
				skip_stage("Detected prefix/suffix");
			}
		} else {
			skip_stage("Detected prefix/suffix");
		}

		do_combine_all = do_combine_all && !skipped_last_stage() && key_to_message.size() != default_messages.size();
		if (do_combine_all) {
			HashSet<String> working_set = get_prefix_suffix_working_set();
			gdre::hashset_insert_iterable(working_set, common_prefixes);
			gdre::hashset_insert_iterable(working_set, common_suffixes);
			Vector<CharString> working_set_t = iter_string_to_charstring(working_set);
			Error stage5_err = run_stage(&KeyWorker::stage_6_task_2, working_set_t, "Combine all");
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
		HashSet<String> succcess_pref;
		gdre::hashset_insert_iterable(succcess_pref, successful_prefixes);
		HashSet<String> succcess_suff;
		gdre::hashset_insert_iterable(succcess_suff, successful_suffixes);
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

		bl_debug(vformat("Total found: %d/%d", non_blank_keys - missing_keys, non_blank_keys));
		bl_debug("-----------------------------------------------------------\n");
		return missing_keys;
	}
};

inline String get_translation_locale(const Ref<Translation> &tr) {
	String locale = tr->get_locale();
	if (locale.is_empty() && !tr->get_path().contains("::")) {
		locale = tr->get_path().get_basename().get_extension().to_lower();
	}
	return locale;
}

inline void get_keys_from_translation(const Ref<Translation> &tr, Vector<String> &keys) {
	List<StringName> key_list;
	tr->get_message_list(&key_list);
	for (auto key : key_list) {
		keys.push_back(key);
	}
}

Error TranslationExporter::get_translations(Ref<ImportInfo> iinfo, String &default_locale, Ref<Translation> &default_translation, Vector<Ref<Translation>> &translations, Vector<String> &keys) {
	Error export_err = OK;

	const String locale_setting_key = GDRESettings::get_singleton()->get_ver_major() >= 4 ? "internationalization/locale/fallback" : "locale/fallback";
	default_locale = GDRESettings::get_singleton()->pack_has_project_config() && GDRESettings::get_singleton()->has_project_setting(locale_setting_key)
			? GDRESettings::get_singleton()->get_project_setting(locale_setting_key)
			: "en";
	auto dest_files = iinfo->get_dest_files();
	Vector<Vector<String>> all_keys;
	if (iinfo->get_importer() == "tccoxon.translation_plus") {
		for (const String &path : dest_files) {
			Ref<Resource> res = ResourceCompatLoader::custom_load(path, "", ResourceInfo::GLTF_LOAD, &export_err, false, ResourceFormatLoader::CACHE_MODE_IGNORE);
			Dictionary d = res->get("translations");
			for (auto &locale : d.keys()) {
				Ref<Translation> tr = d.get(locale, Ref<Translation>());
				if (tr.is_valid()) {
					if (default_translation.is_null() && locale.operator String().to_lower() == default_locale.to_lower()) {
						default_translation = tr;
					}
					translations.push_back(tr);
				}
			}
		}
	} else {
		for (const String &path : dest_files) {
			Ref<Translation> tr = ResourceCompatLoader::non_global_load(path, "", &export_err);
			ERR_CONTINUE_MSG(export_err != OK, "Could not load translation file " + iinfo->get_path());
			ERR_CONTINUE_MSG(!tr.is_valid(), "Translation file " + iinfo->get_path() + " was not valid");
			String locale = get_translation_locale(tr);
			if (default_translation.is_null() && (locale.to_lower() == default_locale.to_lower() ||
														 // Some translations don't have the locale set, so we have to check the file name
														 (locale == "en" && tr->get_path().get_basename().get_extension().to_lower() == default_locale.to_lower()))) {
				default_translation = tr;
			}
			translations.push_back(tr);
		}
	}

	ERR_FAIL_COND_V_MSG(translations.size() == 0, ERR_CANT_ACQUIRE_RESOURCE, "No translations found");
	if (default_translation.is_null()) {
		default_translation = translations[0];
		default_locale = get_translation_locale(default_translation);
	}
	for (Ref<Translation> &tr : translations) {
		if (tr->get_class_name() != "OptimizedTranslation") {
			Vector<String> tr_keys;
			get_keys_from_translation(tr, tr_keys);
			all_keys.push_back(tr_keys);
			break;
		}
	}
	if (default_translation->get_class_name() != "OptimizedTranslation") {
		get_keys_from_translation(default_translation, keys);
	} else if (all_keys.size() > 0) {
		keys = all_keys[0];
	}
	if (default_translation.is_null()) {
		default_translation = translations[0];
	}
	// check default_messages for empty strings
	size_t empty_strings = 0;
	Vector<String> default_messages = default_translation->get_translated_message_list();
	for (auto &message : default_messages) {
		if (message.is_empty()) {
			empty_strings++;
		}
	}
	// if >20% of the strings are empty, this probably isn't the default translation; search the rest of the translations for non-empty strings
	if (empty_strings > default_messages.size() * 0.2) {
		size_t best_empty_strings = empty_strings;
		for (int i = 0; i < translations.size(); i++) {
			size_t empties = 0;
			for (auto &message : translations[i]->get_translated_message_list()) {
				if (message.is_empty()) {
					empties++;
				}
			}
			if (empties < best_empty_strings) {
				best_empty_strings = empties;
				default_translation = translations[i];
			}
		}
		if (default_translation->get_class_name() != "OptimizedTranslation") {
			// re-get the keys
			keys.clear();
			get_keys_from_translation(default_translation, keys);
		}
	}
	translations.erase(default_translation);
	translations.insert(0, default_translation);

	return OK;
}

HashSet<int> get_translations_that_are_out_of_sync(const Ref<Translation> &default_translation, const Vector<Ref<Translation>> &translations, const Vector<String> &keys) {
	HashSet<int> out_of_sync_translations;
	size_t default_messages_size = default_translation->get_translated_message_list().size();
	for (int i = 0; i < translations.size(); i++) {
		auto translation = translations[i];
		if (translation == default_translation || translation->get_class_name() != "OptimizedTranslation") {
			continue;
		}
		if (translation->get_translated_message_list().size() != default_messages_size) {
			out_of_sync_translations.insert(i);
		}
	}
	return out_of_sync_translations;
}

Ref<ExportReport> TranslationExporter::export_resource(const String &output_dir, Ref<ImportInfo> iinfo) {
	Ref<ExportReport> report = memnew(ExportReport(iinfo, get_name()));
	report->set_error(ERR_CANT_ACQUIRE_RESOURCE);

	bl_debug("\n\n-----------------------------------------------------------");
	bl_debug("Exporting translation file " + iinfo->get_export_dest());
	String default_locale;
	Vector<Ref<Translation>> translations;
	Vector<String> keys;
	Ref<Translation> default_translation;
	report->set_error(get_translations(iinfo, default_locale, default_translation, translations, keys));
	ERR_FAIL_COND_V_MSG(report->get_error() != OK, report, "Could not get translations");

	Vector<String> default_messages = default_translation->get_translated_message_list();
	Vector<Vector<String>> translation_messages;
	for (auto &translation : translations) {
		translation_messages.push_back(translation->get_translated_message_list());
	}

	// We can't recover the keys from Optimized translations, we have to guess
	int missing_keys = 0;
	int non_blank_keys = 0;
	bool is_optimized = keys.size() == 0 && Object::cast_to<OptimizedTranslation>(default_translation.ptr()) != nullptr;
	if (GDREConfig::get_singleton()->get_setting("Exporter/Translation/disable_key_recovery", false)) {
		for (int i = 0; i < default_messages.size(); i++) {
			if (!all_messages_at_index_are_blank(translation_messages, i)) {
				missing_keys++;
				non_blank_keys++;
				keys.push_back(vformat(TranslationExporter::MISSING_KEY_FORMAT, i, String(default_messages[i]).split("\n")[0]));
			} else {
				keys.push_back("");
			}
		}
	} else if (is_optimized) {
		KeyWorker kw(default_translation, all_keys_found, translation_messages);
		kw.output_dir = output_dir;
		kw.path = iinfo->get_export_dest();
		missing_keys = kw.run();
		keys = kw.keys;
		non_blank_keys = kw.non_blank_keys;
		for (auto &key : keys) {
			if (!key.is_empty() && !String(key).begins_with(MISSING_KEY_PREFIX)) {
				all_keys_found.insert(key);
			}
		}
	}
	String header = "key";
	for (auto &translation : translations) {
		header += "," + translation->get_locale();
	}
	header += "\n";
	String export_dest = iinfo->get_export_dest();
	// If greater than 15% of the keys are missing, we save the file to the export directory.
	// The reason for this threshold is that the translations may contain keys that are not currently in use in the project.
	bool resave = missing_keys > (non_blank_keys * threshold);
	auto out_of_sync_translations = get_translations_that_are_out_of_sync(default_translation, translations, keys);
	String sync_message;
	if (out_of_sync_translations.size() > 0 && missing_keys > 0) {
		sync_message += "Some locales are out of sync and won't have correct messages in the csv file: ";
		bool first = true;
		for (auto &index : out_of_sync_translations) {
			if (!first) {
				sync_message += ", ";
			}
			sync_message += translations[index]->get_locale();
			first = false;
		}
		sync_message += "\n";
	}

	if (resave) {
		if (!export_dest.begins_with("res://.assets/")) {
			iinfo->set_export_dest("res://.assets/" + iinfo->get_export_dest().replace("res://", ""));
		}
	}

	report->set_resources_used(iinfo->get_dest_files());

	String output_path = output_dir.simplify_path().path_join(iinfo->get_export_dest().replace("res://", ""));
	Error export_err = gdre::ensure_dir(output_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(export_err != OK, report, "Could not create directory " + output_path.get_base_dir());
	Ref<FileAccess> f = FileAccess::open(output_path, FileAccess::WRITE, &export_err);
	ERR_FAIL_COND_V_MSG(export_err != OK, report, "Could not open file " + output_path);
	ERR_FAIL_COND_V_MSG(f.is_null(), report, "Could not open file " + output_path);
	// Set UTF-8 BOM (required for opening with Excel in UTF-8 format, works with all Godot versions)
	f->store_8(0xef);
	f->store_8(0xbb);
	f->store_8(0xbf);
	f->store_string(header);
	const String missing_key_prefix = MISSING_KEY_PREFIX;
	for (int i = 0; i < keys.size(); i++) {
		Vector<String> line_values;
		line_values.push_back(keys[i]);
		for (int j = 0; j < translation_messages.size(); j++) {
			auto &messages = translation_messages[j];
			if (out_of_sync_translations.has(j) && !keys[i].begins_with(missing_key_prefix)) {
				line_values.push_back(translations[j]->get_message(keys[i]));
			} else if (i >= messages.size()) {
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
		String translation_export_message = "Could not recover " + itos(missing_keys) + "/" + itos(non_blank_keys) + " keys for " + iinfo->get_source_file() + "\n";
		translation_export_message += sync_message;
		if (resave) {
			translation_export_message += "Too inaccurate, saved " + iinfo->get_source_file().get_file() + " to " + iinfo->get_export_dest() + "\n";
			// Ensure metadata is not rewritten
			if (iinfo->get_ver_major() <= 2) {
				report->set_rewrote_metadata(ExportReport::NOT_IMPORTABLE);
			}
		}
		report->set_message(translation_export_message);
	}
	if (iinfo->get_ver_major() >= 4) {
		iinfo->set_param("compress", is_optimized);
		iinfo->set_param("delimiter", 0);
	}
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
	out->push_back("tccoxon.translation_plus");
}

String TranslationExporter::get_name() const {
	return EXPORTER_NAME;
}

String TranslationExporter::get_default_export_extension(const String &res_path) const {
	return "csv";
}

Error TranslationExporter::parse_csv(const String &csv_path, HashMap<String, Vector<String>> &new_messages, int64_t &missing_keys, bool &has_non_empty_lines_without_key, int64_t &non_empty_line_count) {
	Ref<FileAccess> f = FileAccess::open(csv_path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_ACQUIRE_RESOURCE, "Could not open file " + csv_path);
	Vector<String> header = f->get_csv_line();
	HashMap<String, int64_t> locales_in_csv;
	int64_t key_id = -1;
	for (int i = 0; i < header.size(); i++) {
		auto &locale = header[i];
		if (locale == "key") {
			key_id = i;
			continue;
		}
		if (!locale.is_empty()) {
			locales_in_csv[locale] = i;
		}
	}
	ERR_FAIL_COND_V_MSG(key_id == -1, ERR_INVALID_PARAMETER, "CSV file does not have a 'key' column");
	// locale to key to value
	new_messages["key"] = Vector<String>();
	while (!f->eof_reached()) {
		Vector<String> line = f->get_csv_line();
		if (line.is_empty()) {
			continue;
		}
		if (key_id >= line.size()) {
			WARN_PRINT("CSV file has no value for key column, skipping line " + itos(non_empty_line_count));
			has_non_empty_lines_without_key = true;
			continue;
		}
		if (!line[key_id].is_empty()) {
			non_empty_line_count++;
			if (line[key_id].begins_with(TranslationExporter::MISSING_KEY_PREFIX)) {
				missing_keys++;
			}
		}
		new_messages["key"].push_back(line[key_id]);
		for (auto &locale : locales_in_csv) {
			if (locale.value < line.size()) {
				if (!new_messages.has(locale.key)) {
					new_messages[locale.key] = {};
				}
				new_messages[locale.key].push_back(line[locale.value]);
			} else {
				new_messages[locale.key].push_back("");
			}
		}
	}
	f->close();
	return OK;
}

int64_t TranslationExporter::_count_non_empty_messages(const Vector<Vector<String>> &translation_messages) {
	int64_t max_size = 0;
	int64_t count = 0;
	for (auto &messages : translation_messages) {
		max_size = MAX(max_size, messages.size());
	}
	for (int i = 0; i < max_size; i++) {
		for (auto &messages : translation_messages) {
			if (i < messages.size() && !messages[i].is_empty()) {
				count++;
				break;
			}
		}
	}
	return count;
}

Error TranslationExporter::patch_translations(const String &output_dir, const String &csv_path, Ref<ImportInfo> translation_info, const Vector<String> &p_locales_to_patch, Dictionary r_file_map) {
	String default_locale;
	Vector<Ref<Translation>> translations;
	Vector<String> keys;
	Ref<Translation> default_translation;
	Error err = get_translations(translation_info, default_locale, default_translation, translations, keys);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not get translations");

	Vector<String> default_messages = default_translation->get_translated_message_list();
	HashMap<String, Pair<Ref<Translation>, Vector<String>>> translation_messages;
	for (auto &translation : translations) {
		translation_messages[get_translation_locale(translation)] = Pair<Ref<Translation>, Vector<String>>(translation, translation->get_translated_message_list());
	}
	bool has_lines_without_key = false;
	int64_t missing_keys = 0;
	int64_t non_empty_line_count = 0;
	HashMap<String, Vector<String>> new_messages;
	err = parse_csv(csv_path, new_messages, missing_keys, has_lines_without_key, non_empty_line_count);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not parse CSV file");

	int64_t max_size = 0;
	int64_t non_empty_message_count = 0;
	for (auto &messages : translation_messages) {
		max_size = MAX(max_size, messages.value.second.size());
	}
	for (int i = 0; i < max_size; i++) {
		for (auto &messages : translation_messages) {
			if (i < messages.value.second.size() && !messages.value.second[i].is_empty()) {
				non_empty_message_count++;
				break;
			}
		}
	}
	if (non_empty_line_count != non_empty_message_count) {
		print_line("CSV file has " + itos(non_empty_line_count) + " lines, but translations have " + itos(non_empty_message_count) + " messages");
		if (missing_keys > 0) {
			WARN_PRINT("Messages with missing keys may be patched to incorrect indices!!!");
		}
	}

	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not parse CSV file");

	Vector<String> locales_to_patch = p_locales_to_patch;
	// default to only new locales
	if (locales_to_patch.size() == 0) {
		for (auto &kv : new_messages) {
			if (kv.key != "key" && !translation_messages.has(kv.key)) {
				locales_to_patch.push_back(kv.key);
			}
		}
	}

	ERR_FAIL_COND_V_MSG(locales_to_patch.size() == 0, ERR_INVALID_PARAMETER, "No locales to patch");

	Vector<String> dest_files = translation_info->get_dest_files();

	for (auto &locale : locales_to_patch) {
		Ref<OptimizedTranslationExtractor> extractor;
		String dest_path;
		if (translation_messages.has(locale)) {
			auto translation = translation_messages[locale].first;
			dest_path = translation->get_path();
			extractor = OptimizedTranslationExtractor::create_from(translation);
		} else {
			extractor = OptimizedTranslationExtractor::create_from(default_translation);
			dest_path = default_translation->get_path().get_basename().get_basename() + "." + locale + ".translation";
		}
		ERR_FAIL_COND_V_MSG(extractor.is_null(), ERR_INVALID_PARAMETER, "Could not create extractor for " + locale);
		extractor->set_locale(locale);

		Vector<Pair<String, String>> to_add;
		for (int i = 0; i < new_messages[locale].size(); i++) {
			auto &key = new_messages["key"][i];
			auto &message = new_messages[locale][i];
			if (key.begins_with(MISSING_KEY_PREFIX)) {
				int64_t idx = key.get_slice(":", 1).to_int();
				if (idx < 0) {
					WARN_PRINT("Mangled missing key index for key " + key + " in " + locale + ", not setting...");
					continue;
				}
				Error err = extractor->replace_message_at_index(idx, message);
				if (err != OK) {
					WARN_PRINT("Could not replace message with key " + key + " in " + locale + ", not setting...");
				}
			} else {
				if (extractor->get_message(key) != message) {
					Error err = extractor->replace_message(key, message);
					if (err != OK) {
						WARN_PRINT("Could not replace message with key " + key + " in " + locale + ", setting new message...");
						// we have to add them at the end so as not to throw off the indices
						to_add.push_back({ key, message });
					}
				}
			}
		}
		for (auto &key_to_message : to_add) {
			extractor->add_message(key_to_message.first, key_to_message.second);
		}
		if (!dest_files.has(dest_path)) {
			dest_files.push_back(dest_path);
		}

		String output_path = output_dir.path_join(dest_path.trim_prefix("res://"));

		int ver_major = translation_info->get_ver_major();
		int ver_minor = translation_info->get_ver_minor();
		if (ver_major >= 4) {
			extractor->set_original_class("OptimizedTranslation");
		} else {
			extractor->set_original_class("PHashTranslation");
		}
		err = gdre::ensure_dir(output_path.get_base_dir());
		ERR_FAIL_COND_V_MSG(err != OK, err, "Could not ensure directory for " + output_path);
		err = ResourceCompatLoader::save_custom(extractor, output_path, ver_major, ver_minor);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Could not save translation file for " + locale);
		r_file_map[output_path] = dest_path;
#if DEBUG_ENABLED
		err = ResourceCompatLoader::save_custom(extractor, output_path.get_basename() + ".tres", ver_major, ver_minor);
#endif
	}
	translation_info->set_dest_files(dest_files);

	return OK;
}

Error TranslationExporter::patch_project_config(const String &output_dir, Dictionary file_map) {
	Vector<String> new_translation_files;
	for (auto &kv : file_map) {
		new_translation_files.push_back(kv.value);
	}
	if (GDRESettings::get_singleton()->is_project_config_loaded()) {
		String project_config_path = GDRESettings::get_singleton()->get_project_config_path();
		Ref<ProjectConfigLoader> loader = memnew(ProjectConfigLoader);
		int ver_major = GDRESettings::get_singleton()->get_ver_major();
		int ver_minor = GDRESettings::get_singleton()->get_ver_minor();
		const String locale_setting_key = GDRESettings::get_singleton()->get_ver_major() >= 4 ? "internationalization/locale/translations" : "locale/translations";
		Error err = loader->load_cfb(project_config_path, ver_major, ver_minor);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Could not load project config");
		Vector<String> curr_translations = loader->get_setting(locale_setting_key, Vector<String>());
		for (auto &locale : new_translation_files) {
			if (!curr_translations.has(locale)) {
				curr_translations.push_back(locale);
			}
		}
		curr_translations.sort_custom<FileNoCaseComparator>();
		loader->set_setting(locale_setting_key, curr_translations);
		String output_path = output_dir.path_join(project_config_path.trim_prefix("res://"));
		gdre::ensure_dir(output_path.get_base_dir());
		err = loader->save_custom(output_path, ver_major, ver_minor);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Could not save project config");
#if DEBUG_ENABLED
		err = loader->save_custom(output_path.get_basename() + ".godot", ver_major, ver_minor);
#endif
		file_map[output_path] = project_config_path;
	}
	return OK;
}

TypedDictionary<String, Vector<String>> TranslationExporter::get_messages_from_translation(Ref<ImportInfo> translation_info) {
	String default_locale;
	Vector<Ref<Translation>> translations;
	Vector<String> keys;
	Ref<Translation> default_translation;
	Error err = get_translations(translation_info, default_locale, default_translation, translations, keys);
	ERR_FAIL_COND_V_MSG(err != OK, {}, "Could not get translations");
	TypedDictionary<String, Vector<String>> messages;
	for (auto &translation : translations) {
		messages[get_translation_locale(translation)] = translation->get_translated_message_list();
	}
	return messages;
}

int64_t TranslationExporter::count_non_empty_messages_from_info(Ref<ImportInfo> translation_info) {
	String default_locale;
	Vector<Ref<Translation>> translations;
	Vector<String> keys;
	Ref<Translation> default_translation;
	Error err = get_translations(translation_info, default_locale, default_translation, translations, keys);
	ERR_FAIL_COND_V_MSG(err != OK, 0, "Could not get translations");

	Vector<Vector<String>> translation_messages;
	for (auto &translation : translations) {
		translation_messages.push_back(translation->get_translated_message_list());
	}
	return _count_non_empty_messages(translation_messages);
}

int64_t TranslationExporter::count_non_empty_messages(const TypedDictionary<String, Vector<String>> &translation_messages) {
	Vector<Vector<String>> messages;
	for (auto &kv : translation_messages) {
		messages.push_back(kv.value);
	}
	return _count_non_empty_messages(messages);
}

TypedDictionary<String, Vector<String>> TranslationExporter::get_csv_messages(const String &csv_path, Dictionary ret_info) {
	HashMap<String, Vector<String>> new_messages;
	int64_t missing_keys = 0;
	int64_t non_empty_line_count = 0;
	bool has_non_empty_lines_without_key = false;
	Error err = parse_csv(csv_path, new_messages, missing_keys, has_non_empty_lines_without_key, non_empty_line_count);

	ret_info["error"] = err;
	ret_info["missing_keys"] = missing_keys;
	ret_info["new_non_empty_count"] = non_empty_line_count;
	ret_info["has_non_empty_lines_without_key"] = has_non_empty_lines_without_key;
	ERR_FAIL_COND_V_MSG(err != OK, {}, "Could not parse CSV file");

	return gdre::hashmap_to_typed_dict(new_messages);
}

void TranslationExporter::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("patch_translations", "output_dir", "csv_path", "translation_info", "locales_to_patch", "file_map"), &TranslationExporter::patch_translations);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("patch_project_config", "output_dir", "file_map"), &TranslationExporter::patch_project_config);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_messages_from_translation", "translation_info"), &TranslationExporter::get_messages_from_translation);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("count_non_empty_messages_from_info", "translation_info"), &TranslationExporter::count_non_empty_messages_from_info);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("count_non_empty_messages", "translation_messages"), &TranslationExporter::count_non_empty_messages);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_csv_messages", "csv_path", "ret_info"), &TranslationExporter::get_csv_messages);
}
