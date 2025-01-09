#include "translation_exporter.h"

#include "compat/optimized_translation_extractor.h"
#include "compat/resource_loader_compat.h"
#include "exporters/export_report.h"
#include "utility/common.h"
#include "utility/gdre_settings.h"

#include "core/error/error_list.h"
#include "core/object/worker_thread_pool.h"
#include "core/string/optimized_translation.h"
#include "core/string/translation.h"
#include "core/string/ustring.h"
#include "modules/regex/regex.h"

#include "utility/gd_parallel_hashmap.h"
#include <modules/gdsdecomp/utility/common.h>

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

static const HashSet<char32_t> ALL_PUNCTUATION = gdre::vector_to_hashset(Vector<char32_t>({ '.', '!', '?', ',', ';', ':', '(', ')', '[', ']', '{', '}', '<', '>', '/', '\\', '|', '`', '~', '@', '#', '$', '%', '^', '&', '*', '-', '_', '+', '=', '\'', '"', '\n', '\t', ' ' }));
static const HashSet<char32_t> REMOVABLE_PUNCTUATION = HashSet<char32_t>(gdre::vector_to_hashset(Vector<char32_t>{ '.', '!', '?', ',', ';', ':', '%' }));
static const Vector<String> STANDARD_SUFFIXES = { "Name", "Text", "Title", "Description", "Label", "Button", "Speech", "Tooltip", "Legend", "Body", "Content" };

static const char *MISSING_KEY_PREFIX = "<!MissingKey:";

template <class K, class V>
Vector<K> get_keys(const HashMap<K, V> &map) {
	Vector<K> ret;
	for (const auto &E : map) {
		ret.push_back(E.key);
	}
	return ret;
}
template <typename T>
void update_maximum(std::atomic<T> &maximum_value, T const &value) noexcept {
	T prev_value = maximum_value;
	while (prev_value < value &&
			!maximum_value.compare_exchange_weak(prev_value, value)) {
	}
}
struct KeyWorker {
	static constexpr int MAX_FILT_RES_STRINGS = 5000;
	static constexpr uint64_t MAX_STAGE_TIME = 30 * 1000ULL;

	Mutex mutex;
	HashMap<String, String> key_to_message;
	Vector<String> resource_strings;
	Vector<String> filtered_resource_strings;
	Vector<CharString> resource_strings_t;
	Vector<CharString> filtered_resource_strings_t;

	const Ref<OptimizedTranslationExtractor> default_translation;
	const Vector<String> default_messages;
	const HashSet<String> previous_keys_found;

	Vector<String> keys;
	bool use_multithread = true;
	std::atomic<bool> keys_have_whitespace = false;
	std::atomic<bool> keys_are_all_upper = true;
	std::atomic<bool> keys_are_all_lower = true;
	std::atomic<bool> keys_are_all_ascii = true;
	bool has_common_prefix = false;
	bool do_stage_4 = true;
	bool do_stage_5 = false; // disabled for now, it's too slow
	std::atomic<bool> cancel = false;
	HashSet<char32_t> punctuation;
	HashSet<CharString> punctuation_str;

	std::atomic<size_t> keys_that_are_all_upper = 0;
	std::atomic<size_t> keys_that_are_all_lower = 0;
	std::atomic<size_t> keys_that_are_all_ascii = 0;
	std::atomic<size_t> max_key_len = 0;
	String common_to_all_prefix;
	Vector<String> common_prefixes;
	Vector<String> common_suffixes;
	Vector<CharString> common_suffixes_t;
	Vector<CharString> common_prefixes_t;

	ParallelFlatHashSet<String> successful_suffixes;
	ParallelFlatHashSet<String> successful_prefixes;

	Ref<RegEx> word_regex;
	std::atomic<uint64_t> current_keys_found = 0;
	Vector<uint64_t> times;
	Vector<uint64_t> keys_found;
	ParallelFlatHashSet<String> current_stage_keys_found;
	Vector<ParallelFlatHashSet<String>> stages_keys_found;
	std::atomic<uint64_t> last_completed = 0;
	// 30 seconds in msec
	uint64_t start_time = OS::get_singleton()->get_ticks_usec();
	uint64_t start_of_multithread = start_time;
	//default_translation,  default_messages;
	KeyWorker(const Ref<OptimizedTranslation> &p_default_translation,
			const HashSet<String> &p_previous_keys_found) :
			default_translation(OptimizedTranslationExtractor::create_from(p_default_translation)),
			default_messages(default_translation->get_translated_message_list()),
			previous_keys_found(p_previous_keys_found) {
	}

	String sanitize_key(const String &s) {
		String str = s;
		str = str.replace("\n", "").replace(".", "").replace("…", "").replace("!", "").replace("?", "").strip_escapes().strip_edges();
		return str;
	}

	// make this a template that can take in either a HashMap or a HashMap
	//  use the is_flat_or_parallel_flat_hash_map trait
	static String find_common_prefix(const HashMap<String, String> &key_to_msg) {
		// among all the keys in the vector, find the common prefix
		if (key_to_msg.size() == 0) {
			return "";
		}
		String prefix;
		auto add_to_prefix_func = [&](int i) {
			char32_t candidate = 0;
			for (const auto &E : key_to_msg) {
				auto &s = E.key;
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
				auto &s = E.key;
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
			if (E.value >= count_threshold && !common_prefixes.has(E.key)) {
				common_prefixes.push_back(E.key);
			}
		}
		for (const auto &E : suffix_counts) {
			if (E.value >= count_threshold && !common_suffixes.has(E.key)) {
				common_suffixes.push_back(E.key);
			}
		}
		// sort the prefixes and suffixes by length

		common_prefixes.sort_custom<StringLengthCompare<true>>();
		common_suffixes.sort_custom<StringLengthCompare<true>>();
	}

	_FORCE_INLINE_ void _set_key_stuff(const String &key) {
		++current_keys_found;
		if (!keys_have_whitespace && gdre::string_has_whitespace(key)) {
			keys_have_whitespace = true;
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
		current_stage_keys_found.insert(key);
		update_maximum(max_key_len, (size_t)key.length());
		gdre::get_chars_in_set(key, ALL_PUNCTUATION, punctuation);
		for (char32_t p : punctuation) {
			punctuation_str.insert(String::chr(p).utf8());
		}
	}

	_FORCE_INLINE_ bool _set_key(const String &key, const String &msg) {
		MutexLock lock(mutex);
		if (key_to_message.has(key)) {
			return true;
		}
		_set_key_stuff(key);
		key_to_message[key] = msg;
		return true;
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

	void reg_successful_prefix(const String &prefix) {
		if (!prefix.is_empty()) {
			successful_prefixes.insert(prefix);
		}
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
			// if (key_to_message.contains(key)) {
			// 	return true;
			// }
			// key_to_message[key] = msg;
			// current_stage_keys_found.insert(key);
			// ++current_keys_found;
			_set_key(key, msg);
			return true;
		}
		return false;
	}

	bool try_key_prefix(const char *prefix, const char *suffix) {
		if (try_key_multipart(prefix, suffix)) {
			reg_successful_prefix(suffix);
			return true;
		}
		for (auto p : punctuation_str) {
			if (try_key_multipart(prefix, p.get_data(), suffix)) {
				reg_successful_prefix(suffix);
				return true;
			}
		}
		return false;
	}

	bool try_key_suffix(const char *prefix, const char *suffix) {
		if (try_key_multipart(prefix, suffix)) {
			reg_successful_suffix(suffix);
			return true;
		}
		for (auto p : punctuation_str) {
			if (try_key_multipart(prefix, p.get_data(), suffix)) {
				reg_successful_suffix(suffix);
				return true;
			}
		}
		return false;
	}

	bool try_key_suffixes(const char *prefix, const char *suffix, const char *suffix2) {
		bool suffix1_empty = !suffix || *suffix == 0;
		if (suffix1_empty) {
			return try_key_suffix(prefix, suffix2);
		}
		if (try_key_multipart(prefix, suffix, suffix2)) {
			reg_successful_suffix(combine_string(suffix, suffix2));
			return true;
		}
		for (auto p : punctuation_str) {
			if (try_key_multipart(prefix, suffix, p.get_data(), suffix2)) {
				reg_successful_suffix(combine_string(suffix, p.get_data(), suffix2));
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

	CharString cs_num(int64_t num) {
		CharString ret;
		ret.resize(32);
		int len = snprintf(ret.ptrw(), 31, "%lld", num);
		ret.resize(len + 1);
		return ret;
	}

	auto try_num_suffix(const char *res_s, const char *suffix = "") {
		bool found_num = try_key_suffixes(res_s, suffix, "1");
		if (found_num) {
			try_key_suffixes(res_s, suffix, "N");
			try_key_suffixes(res_s, suffix, "n");
			try_key_suffixes(res_s, suffix, "0");
			bool found_all = true;
			int min_num = 2;
			int max_num = 4;

			while (found_all) {
				int numbers_found = 0;
				for (int num = min_num; num < max_num; num++) {
					if (try_key_suffixes(res_s, suffix, cs_num(num).get_data())) {
						numbers_found++;
					}
				}
				if (numbers_found >= max_num - min_num) {
					found_all = true;
				} else {
					found_all = false;
				}
				min_num = max_num;
				max_num = max_num * 2;
			}
		}
	}

	void prefix_suffix_task_2(uint32_t i, CharString *res_strings) {
		if (unlikely(cancel)) {
			return;
		}
		const CharString &res_s = res_strings[i];
		try_num_suffix(res_s);

		for (const auto &E : common_suffixes_t) {
			try_key_suffix(res_s, E);
			try_num_suffix(res_s, E);
		}
		for (const auto &E : common_prefixes_t) {
			try_key_prefix(E, res_s);
			try_num_suffix(E, res_s);
		}
		last_completed++;
	}

	void partial_task(uint32_t i, String *res_strings) {
		if (unlikely(cancel)) {
			return;
		}
		const String &res_s = res_strings[i];
		if (!has_common_prefix || res_s.contains(common_to_all_prefix)) {
			auto matches = word_regex->search_all(res_s);
			for (const Ref<RegExMatch> match : matches) {
				for (const String &key : match->get_strings()) {
					try_key(key);
				}
			}
		}
		last_completed++;
	}

	void stage_5_task_2(uint32_t i, CharString *res_strings) {
		if (unlikely(cancel)) {
			return;
		}
		const CharString &res_s = res_strings[i];
		auto frs_size = filtered_resource_strings.size();
		for (uint32_t j = 0; j < frs_size; j++) {
			const CharString &res_s2 = res_strings[j];
			try_key_suffix(res_s, res_s2);
		}
		++last_completed;
	}

	void end_stage() {
		last_completed = 0;
		cancel = false;
		times.push_back(OS::get_singleton()->get_ticks_msec());
		keys_found.push_back(current_keys_found);
		stages_keys_found.push_back(current_stage_keys_found);
		current_keys_found = 0;
		current_stage_keys_found.clear();
	}

	static bool check_for_timeout(const uint64_t start_time, const uint64_t max_time) {
		if ((OS::get_singleton()->get_ticks_msec() - start_time) > max_time) {
			return true;
		}
		return false;
	}

	Error wait_for_task(WorkerThreadPool::GroupID group_task, const String &stage_name, size_t size, uint64_t max_time) {
		uint64_t next_report = 5000;
		uint64_t start_time = OS::get_singleton()->get_ticks_msec();
		while (!WorkerThreadPool::get_singleton()->is_group_task_completed(group_task)) {
			// wait 100ms
			OS::get_singleton()->delay_usec(100000);
			if (check_for_timeout(start_time, max_time)) {
				bl_debug("Timeout waiting for " + stage_name + " to complete...");
				cancel = true;
				WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);
				return ERR_TIMEOUT;
			}
			if (check_for_timeout(start_time, next_report)) {
				bl_debug("waiting for " + stage_name + " to complete... (" + itos(last_completed) + "/" + itos(size) + ")");
				next_report += 5000;
			}
		}

		// Always wait for completion; otherwise we leak memory.
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);
		bl_debug(stage_name + " completed!");
		return OK;
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
		if (res_s.size() > max_key_len) {
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

	String remove_removable_punct(const String &s) {
		String ret;
		for (int i = 0; i < s.length(); i++) {
			char32_t c = s.ptr()[i];
			if (!punctuation.has(c) && REMOVABLE_PUNCTUATION.has(c)) {
				ret += c;
			}
		}
		return ret;
	}

	template <class T>
	Vector<String> get_sanitized_strings(const Vector<T> &default_messages) {
		static_assert(std::is_same<T, String>::value || std::is_same<T, StringName>::value, "T must be either String or StringName");
		HashSet<String> new_strings;
		for (const T &msg : default_messages) {
			auto msg_str = remove_removable_punct(msg).strip_escapes().strip_edges();
			for (auto ch : punctuation) {
				// strip edges
				msg_str = msg_str.trim_suffix(String::chr(ch)).trim_prefix(String::chr(ch));
			}
			if (has_nonspace_and_std_punctuation(msg_str)) {
				continue;
			}
			if (keys_are_all_ascii && !gdre::string_is_ascii(msg_str)) {
				continue;
			}
			if (keys_are_all_upper) {
				msg_str = msg_str.to_upper();
			} else if (keys_are_all_lower) {
				msg_str = msg_str.to_lower();
			}
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
		auto hshset = gdre::vector_to_hashset(filtered_resource_strings);
		for (const auto &msg_str : get_sanitized_strings(default_messages)) {
			if (hshset.has(msg_str)) {
				continue;
			}
			hshset.insert(msg_str);
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

	// TODO: Rise of the Golden Idol specific hack, remove this
	void dynamic_rgi_hack() {
#if 0
		const String ITEM_TR_SEP = "|";
		const String ITEM_TR = "DB_%d";
		const String ITEM_TR_PREFIX_ARC = "ARC";
		int min_scenario_id = 0;
		int max_scenario_id = 100;
		int min_arc_id = 0;
		int max_arc_id = 10;
		int max_item_id = 10000;
		auto get_translation_id = [&](int id) {
			return vformat(ITEM_TR, id);
		};
		auto get_composite_translation_id = [&](int scenario_id, int item_id) {
			return vformat("%d%s%s", scenario_id, ITEM_TR_SEP, get_translation_id(item_id));
		};
		auto get_composite_arc_translation_id = [&](int arc_id, int item_id) {
			auto prefix = vformat("%s%d", ITEM_TR_PREFIX_ARC, arc_id);
			return vformat("%s%s%s", prefix, ITEM_TR_SEP, get_translation_id(item_id));
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
#endif
	}

	template <typename M, typename NM, class VE>
	void run_stage(M p_multi_method, NM non_multi_method, Vector<VE> p_userdata, const String &stage_name) {
		// assert that M is a method belonging to this class
		last_completed = 0;
		cancel = false;
		static_assert(std::is_member_function_pointer<M>::value, "M must be a method of this class");
		if (use_multithread) {
			last_completed = 0;
			auto desc = "TranslationExporter::find_missing_keys::" + stage_name;
			WorkerThreadPool::GroupID group_id = WorkerThreadPool::get_singleton()->add_template_group_task(
					this,
					p_multi_method,
					p_userdata.ptrw(),
					p_userdata.size(), -1, true, StringName(desc));
			wait_for_task(group_id, stage_name, p_userdata.size(), MAX_STAGE_TIME);
		} else {
			auto strt = OS::get_singleton()->get_ticks_usec();
			for (uint32_t i = 0; i < p_userdata.size(); i++) {
				(this->*non_multi_method)(i, p_userdata.ptrw());
				// if (i % 250 == 0 && check_for_timeout(strt, MAX_STAGE_TIME)) {
				// 	break;
				// }
			}
		}
		end_stage();
	}

	bool met_threshold() {
		return (double)default_messages.size() / (double)key_to_message.size() > ((double)1 - TranslationExporter::threshold);
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

	uint64_t run() {
		cancel = false;
		uint64_t missing_keys = 0;
		HashSet<String> res_strings;
		start_time = OS::get_singleton()->get_ticks_msec();

		// Stage 1: Unmodified resource strings
		// We need to load all the resource strings in all resources to find the keys
		if (!GDRESettings::get_singleton()->loaded_resource_strings()) {
			GDRESettings::get_singleton()->load_all_resource_strings();
		}
		GDRESettings::get_singleton()->get_resource_strings(res_strings);
		resource_strings = gdre::hashset_to_vector(res_strings);
		for (uint32_t i = 0; i < resource_strings.size(); i++) {
			const String &key = resource_strings[i];
			try_key(key);
		}
		// Stage 1.5: Previous keys found
		if (key_to_message.size() != default_messages.size()) {
			for (const String &key : previous_keys_found) {
				try_key(key);
			}
		}
		// Stage 1.75: dynamic_rgi_hack
		dynamic_rgi_hack();
		end_stage();
		common_to_all_prefix = find_common_prefix(key_to_message);
		has_common_prefix = !common_to_all_prefix.is_empty();

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

			run_stage(&KeyWorker::partial_task, &KeyWorker::partial_task, resource_strings, "Stage 2");
		} else {
			end_stage();
		}

		// Stage 3: commonly known suffixes
		// We first filter them according to common characteristics so that this doesn't take forever.
		if (key_to_message.size() != default_messages.size()) {
			auto filter_things = [&]() {
				filtered_resource_strings.clear();
				for (const String &res_s : res_strings) {
					if (should_filter(res_s)) {
						continue;
					}
					filtered_resource_strings.push_back(res_s);
				}
				return filtered_resource_strings.size();
			};
			filter_things();
			// check if upper case strings are >90% of the strings
			if (filtered_resource_strings.size() > MAX_FILT_RES_STRINGS && (!keys_are_all_upper || !keys_are_all_lower || !keys_are_all_ascii)) {
				if (!keys_are_all_upper && keys_that_are_all_upper / key_to_message.size() > 0.9) {
					// if so, we can safely assume that the keys are all upper case
					keys_are_all_upper = true;
				} else if (!keys_are_all_lower && keys_that_are_all_lower / key_to_message.size() > 0.9) {
					// if so, we can safely assume that the keys are all lower case
					keys_are_all_lower = true;
				} else if (!keys_are_all_ascii && keys_that_are_all_ascii / key_to_message.size() > 0.9) {
					// if so, we can safely assume that the keys are all ascii
					keys_are_all_ascii = true;
				}
				filter_things();
			}
			// add the message strings to the filtered resource strings
			Vector<String> new_strings;
			get_sanitized_message_strings(new_strings);
			filtered_resource_strings.append_array(new_strings);

			common_prefixes = get_sanitized_strings(STANDARD_SUFFIXES);
			common_suffixes = get_sanitized_strings(STANDARD_SUFFIXES);
			pop_charstr_vectors();
			run_stage(&KeyWorker::prefix_suffix_task_2, &KeyWorker::prefix_suffix_task_2, filtered_resource_strings_t, "Stage 3");
		}
		// Stage 4: Combine resource strings with detected prefixes and suffixes
		// If we're still missing keys and no keys have spaces, we try combining every string with every other string
		do_stage_4 = do_stage_4 && key_to_message.size() != default_messages.size();
		if (do_stage_4 && key_to_message.size() != default_messages.size()) {
			auto curr_keys = get_keys(key_to_message);
			find_common_prefixes_and_suffixes(curr_keys);

			Vector<String> middle_candidates;
			extract_middles(filtered_resource_strings, middle_candidates);
			Vector<String> str_keys;
			for (const auto &E : key_to_message) {
				str_keys.push_back(E.key);
			}
			extract_middles(str_keys, middle_candidates);
			Vector<String> new_strings;
			get_sanitized_message_strings(new_strings);
			middle_candidates.append_array(new_strings);
			middle_candidates = gdre::hashset_to_vector(gdre::vector_to_hashset(middle_candidates));
			auto thingy = gdre::vector_to_hashset(filtered_resource_strings);
			for (auto &middle : middle_candidates) {
				if (thingy.has(middle)) {
					continue;
				}
				filtered_resource_strings.push_back(middle);
			}

			start_of_multithread = OS::get_singleton()->get_ticks_usec();
			pop_charstr_vectors();
			for (const auto &prefix : common_prefixes_t) {
				for (const auto &suffix : common_suffixes_t) {
					if (try_key_suffix(prefix, suffix)) {
						reg_successful_prefix(prefix.get_data());
					}
					try_num_suffix(prefix, suffix);
				}
			}
			if (filtered_resource_strings.size() <= MAX_FILT_RES_STRINGS) {
				run_stage(&KeyWorker::prefix_suffix_task_2, &KeyWorker::prefix_suffix_task_2, filtered_resource_strings_t, "Stage 4");
				// Stage 5: Combine resource strings with every other string
				// If we're still missing keys, we try combining every string with every other string.
				do_stage_5 = do_stage_5 && key_to_message.size() != default_messages.size() && filtered_resource_strings.size() <= MAX_FILT_RES_STRINGS;
				if (do_stage_5) {
					run_stage(&KeyWorker::stage_5_task_2, &KeyWorker::stage_5_task_2, filtered_resource_strings_t, "Stage 5");
				}
			}
		}

		missing_keys = 0;
		keys.clear();

		for (int i = 0; i < default_messages.size(); i++) {
			auto &msg = default_messages[i];
			bool found = false;
			bool has_match = false;
			StringName matching_key;
			for (auto &E : key_to_message) {
				if (E.value == msg) {
					has_match = true;
					matching_key = E.key;
					if (!keys.has(E.key)) {
						keys.push_back(E.key);
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
						keys.push_back(matching_key);
						continue;
					}
				} else {
					print_verbose(vformat("Could not find key for message '%s'", msg));
				}
				missing_keys++;
				keys.push_back(MISSING_KEY_PREFIX + String(msg).split("\n")[0] + ">");
			}
		}

		// print out the times taken
		bl_debug("Key guessing took " + itos(OS::get_singleton()->get_ticks_msec() - start_time) + "ms");
		for (int i = 0; i < times.size(); i++) {
			auto num_keys = keys_found[i];
			if (i == 0) {
				bl_debug("Stage 1 took " + itos(times[i] - start_time) + "ms, found " + itos(num_keys) + " keys");
			} else {
				bl_debug("Stage " + itos(i + 1) + " took " + itos(times[i] - times[i - 1]) + "ms, found " + itos(num_keys) + " keys");
			}
			if (i >= 2 && num_keys > 0) {
				for (const auto &key : stages_keys_found[i]) {
					bl_debug("* Key found in stage " + itos(i + 1) + ": " + key);
				}
			}
		}
		bl_debug(vformat("Total found: %d/%d", default_messages.size() - missing_keys, default_messages.size()));
		return missing_keys;
	}
};

Ref<ExportReport> TranslationExporter::export_resource(const String &output_dir, Ref<ImportInfo> iinfo) {
	// Implementation for exporting resources related to translations
	Error err = OK;
	// translation files are usually imported from one CSV and converted to multiple "<LOCALE>.translation" files
	// TODO: make this also check for the first file in GDRESettings::get_singleton()->get_project_setting("internationalization/locale/translations")
	String default_locale = GDRESettings::get_singleton()->pack_has_project_config() && GDRESettings::get_singleton()->has_project_setting("locale/fallback")
			? GDRESettings::get_singleton()->get_project_setting("locale/fallback")
			: "en";
	if (iinfo->get_dest_files().size() == 1) {
		default_locale = iinfo->get_dest_files()[0].get_basename().get_extension();
	}
	bl_debug("Exporting translation file " + iinfo->get_export_dest());
	Vector<Ref<Translation>> translations;
	Vector<Vector<String>> translation_messages;
	Ref<Translation> default_translation;
	Vector<String> default_messages;
	String header = "key";
	Vector<String> keys;
	Ref<ExportReport> report = memnew(ExportReport(iinfo));
	report->set_error(ERR_CANT_ACQUIRE_RESOURCE);
	for (String path : iinfo->get_dest_files()) {
		Ref<Translation> tr = ResourceCompatLoader::non_global_load(path, "", &err);
		ERR_FAIL_COND_V_MSG(err != OK, report, "Could not load translation file " + iinfo->get_path());
		ERR_FAIL_COND_V_MSG(!tr.is_valid(), report, "Translation file " + iinfo->get_path() + " was not valid");
		String locale = tr->get_locale();
		// TODO: put the default locale at the beginning
		header += "," + locale;
		if (tr->get_class_name() != "OptimizedTranslation") {
			// We have a real translation class, get the keys
			if (locale.to_lower() == default_locale.to_lower()) {
				List<StringName> key_list;
				tr->get_message_list(&key_list);
				for (auto key : key_list) {
					keys.push_back(key);
				}
			}
		}
		Vector<String> messages = tr->get_translated_message_list();
		if (locale.to_lower() == default_locale.to_lower()) {
			default_messages = messages;
			default_translation = tr;
		}
		translation_messages.push_back(messages);
		translations.push_back(tr);
	}

	if (default_translation.is_null()) {
		report->set_error(ERR_FILE_MISSING_DEPENDENCIES);
		ERR_FAIL_V_MSG(report, "No default translation found for " + iinfo->get_path());
	}
	// We can't recover the keys from Optimized translations, we have to guess
	int missing_keys = 0;
	if (keys.size() == 0) {
		KeyWorker kw(default_translation, all_keys_found);
		missing_keys = kw.run();
		keys = kw.keys;
		for (auto &key : keys) {
			if (!String(key).begins_with(MISSING_KEY_PREFIX)) {
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
		iinfo->set_export_dest("res://.assets/" + iinfo->get_export_dest().replace("res://", ""));
	}
	String output_path = output_dir.simplify_path().path_join(iinfo->get_export_dest().replace("res://", ""));
	err = gdre::ensure_dir(output_path.get_base_dir());
	ERR_FAIL_COND_V(err, report);
	Ref<FileAccess> f = FileAccess::open(output_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V(err, report);
	ERR_FAIL_COND_V(f.is_null(), report);
	// Set UTF-8 BOM (required for opening with Excel in UTF-8 format, works with all Godot versions)
	f->store_8(0xef);
	f->store_8(0xbb);
	f->store_8(0xbf);
	f->store_string(header);
	for (int i = 0; i < keys.size(); i++) {
		Vector<String> line_values;
		line_values.push_back(keys[i]);
		for (auto messages : translation_messages) {
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
	if (missing_keys) {
		String translation_export_message = "WARNING: Could not recover " + itos(missing_keys) + " keys for " + iinfo->get_source_file() + "\n";
		if (resave) {
			translation_export_message += "Saved " + iinfo->get_source_file().get_file() + " to " + iinfo->get_export_dest() + "\n";
		}
		report->set_message(translation_export_message);
	}
	report->set_new_source_path(iinfo->get_export_dest());
	report->set_saved_path(output_path);
	return report;
}

bool TranslationExporter::handles_import(const String &importer, const String &resource_type) const {
	// Check if the exporter can handle the given importer and resource type
	return importer == "translation_csv" || resource_type == "Translation";
}

void TranslationExporter::get_handled_types(List<String> *out) const {
	// Add the types of resources that this exporter can handle
	out->push_back("Translation");
}

void TranslationExporter::get_handled_importers(List<String> *out) const {
	// Add the importers that this exporter can handle
	out->push_back("translation_csv");
}