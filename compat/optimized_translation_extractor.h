#ifndef __OPTIMIZED_TRANSLATION_EXTRACTOR_H__
#define __OPTIMIZED_TRANSLATION_EXTRACTOR_H__

#include "core/string/optimized_translation.h"

class OptimizedTranslationExtractor : public Translation {
	GDCLASS(OptimizedTranslationExtractor, Translation);
	Vector<int> hash_table;
	Vector<int> bucket_table;
	Vector<uint8_t> strings;

	struct Bucket {
		int size;
		uint32_t func;

		struct Elem {
			uint32_t key;
			uint32_t str_offset;
			uint32_t comp_size;
			uint32_t uncomp_size;
		};

		Elem elem[1];
	};

	_FORCE_INLINE_ uint32_t hash(uint32_t d, const char *p_str) const {
		if (d == 0) {
			d = 0x1000193;
		}
		while (*p_str) {
			d = (d * 0x1000193) ^ uint32_t(*p_str);
			p_str++;
		}

		return d;
	}

protected:
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
	static void _bind_methods();

public:
	StringName get_message(const char *p_src_text, const StringName &p_context = "") const; //overridable for other implementations
	StringName get_message(const String &p_src_text, const StringName &p_context = "") const; //overridable for other implementations
	virtual StringName get_message(const StringName &p_src_text, const StringName &p_context = "") const override; //overridable for other implementations
	uint32_t hash_multipart(uint32_t d, const char *part1, const char *part2, const char *part3, const char *part4, const char *part5, const char *part6) const;
	virtual StringName get_plural_message(const StringName &p_src_text, const StringName &p_plural_text, int p_n, const StringName &p_context = "") const override;
	virtual Vector<String> get_translated_message_list() const override;
	void generate(const Ref<Translation> &p_from);

	StringName get_message_multipart(const char *part1, const char *part2 = nullptr, const char *part3 = nullptr, const char *part4 = nullptr, const char *part5 = nullptr, const char *part6 = nullptr) const;
	HashSet<uint32_t> get_message_hash_set() const;
	void get_message_value_list(List<StringName> *r_messages) const;
	String get_message_multipart_str(const char *part1, const char *part2, const char *part3, const char *part4, const char *part5, const char *part6) const;
	String get_message_str(const StringName &p_src_text) const;
	String get_message_str(const String &p_src_text) const;
	String get_message_str(const char *p_src_text) const;
	static Ref<OptimizedTranslationExtractor> create_from(const Ref<OptimizedTranslation> &p_otr);
	OptimizedTranslationExtractor() {}
};
static_assert(sizeof(OptimizedTranslationExtractor) == sizeof(OptimizedTranslation), "OptimizedTranslationExtractor should have the same size as OptimizedTranslation");

#endif // __OPTIMIZED_TRANSLATION_EXTRACTOR_H__