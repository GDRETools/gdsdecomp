/**************************************************************************/
/*  optimized_translation.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "optimized_translation_extractor.h"

#include "core/templates/pair.h"

extern "C" {
#include "thirdparty/misc/smaz.h"
}

struct CompressedString {
	int orig_len = 0;
	CharString compressed;
	int offset = 0;
};

void OptimizedTranslationExtractor::generate(const Ref<Translation> &p_from) {
	// This method compresses a Translation instance.
	// Right now, it doesn't handle context or plurals, so Translation subclasses using plurals or context (i.e TranslationPO) shouldn't be compressed.
#ifdef TOOLS_ENABLED
	ERR_FAIL_COND(p_from.is_null());
	List<StringName> keys;
	p_from->get_message_list(&keys);

	int size = Math::larger_prime(keys.size());

	Vector<Vector<Pair<int, CharString>>> buckets;
	Vector<HashMap<uint32_t, int>> table;
	Vector<uint32_t> hfunc_table;
	Vector<CompressedString> compressed;

	table.resize(size);
	hfunc_table.resize(size);
	buckets.resize(size);
	compressed.resize(keys.size());

	int idx = 0;
	int total_compression_size = 0;

	for (const StringName &E : keys) {
		//hash string
		CharString cs = E.operator String().utf8();
		uint32_t h = hash(0, cs.get_data());
		Pair<int, CharString> p;
		p.first = idx;
		p.second = cs;
		buckets.write[h % size].push_back(p);

		//compress string
		CharString src_s = p_from->get_message(E).operator String().utf8();
		CompressedString ps;
		ps.orig_len = src_s.size();
		ps.offset = total_compression_size;

		if (ps.orig_len != 0) {
			CharString dst_s;
			dst_s.resize_uninitialized(src_s.size());
			int ret = smaz_compress(src_s.get_data(), src_s.size(), dst_s.ptrw(), src_s.size());
			if (ret >= src_s.size()) {
				//if compressed is larger than original, just use original
				ps.orig_len = src_s.size();
				ps.compressed = src_s;
			} else {
				dst_s.resize_uninitialized(ret);
				//ps.orig_len=;
				ps.compressed = dst_s;
			}
		} else {
			ps.orig_len = 1;
			ps.compressed.resize_uninitialized(1);
			ps.compressed[0] = 0;
		}

		compressed.write[idx] = ps;
		total_compression_size += ps.compressed.size();
		idx++;
	}

	int bucket_table_size = 0;

	for (int i = 0; i < size; i++) {
		const Vector<Pair<int, CharString>> &b = buckets[i];
		HashMap<uint32_t, int> &t = table.write[i];

		if (b.size() == 0) {
			continue;
		}

		int d = 1;
		int item = 0;

		while (item < b.size()) {
			uint32_t slot = hash(d, b[item].second.get_data());
			if (t.has(slot)) {
				item = 0;
				d++;
				t.clear();
			} else {
				t[slot] = b[item].first;
				item++;
			}
		}

		hfunc_table.write[i] = d;
		bucket_table_size += 2 + b.size() * 4;
	}

	ERR_FAIL_COND(bucket_table_size == 0);

	hash_table.resize(size);
	bucket_table.resize(bucket_table_size);

	int *htwb = hash_table.ptrw();
	int *btwb = bucket_table.ptrw();

	uint32_t *htw = (uint32_t *)&htwb[0];
	uint32_t *btw = (uint32_t *)&btwb[0];

	int btindex = 0;

	for (int i = 0; i < size; i++) {
		const HashMap<uint32_t, int> &t = table[i];
		if (t.size() == 0) {
			htw[i] = 0xFFFFFFFF; //nothing
			continue;
		}

		htw[i] = btindex;
		btw[btindex++] = t.size();
		btw[btindex++] = hfunc_table[i];

		for (const KeyValue<uint32_t, int> &E : t) {
			btw[btindex++] = E.key;
			btw[btindex++] = compressed[E.value].offset;
			btw[btindex++] = compressed[E.value].compressed.size();
			btw[btindex++] = compressed[E.value].orig_len;
		}
	}

	strings.resize(total_compression_size);
	uint8_t *cw = strings.ptrw();

	for (int i = 0; i < compressed.size(); i++) {
		memcpy(&cw[compressed[i].offset], compressed[i].compressed.get_data(), compressed[i].compressed.size());
	}

	ERR_FAIL_COND(btindex != bucket_table_size);
	set_locale(p_from->get_locale());

#endif
}

bool OptimizedTranslationExtractor::_set(const StringName &p_name, const Variant &p_value) {
	String prop_name = p_name.operator String();
	if (prop_name == "hash_table") {
		hash_table = p_value;
	} else if (prop_name == "bucket_table") {
		bucket_table = p_value;
	} else if (prop_name == "strings") {
		strings = p_value;
	} else if (prop_name == "load_from") {
		generate(p_value);
	} else {
		return false;
	}

	return true;
}

bool OptimizedTranslationExtractor::_get(const StringName &p_name, Variant &r_ret) const {
	String prop_name = p_name.operator String();
	if (prop_name == "hash_table") {
		r_ret = hash_table;
	} else if (prop_name == "bucket_table") {
		r_ret = bucket_table;
	} else if (prop_name == "strings") {
		r_ret = strings;
	} else {
		return false;
	}

	return true;
}
StringName OptimizedTranslationExtractor::get_message(const String &p_src_text, const StringName &p_context) const {
	return get_message(p_src_text.utf8().get_data(), p_context);
}

StringName OptimizedTranslationExtractor::get_message(const StringName &p_src_text, const StringName &p_context) const {
	return get_message(p_src_text.operator String().utf8().get_data(), p_context);
}

uint32_t OptimizedTranslationExtractor::hash_multipart(uint32_t d, const char *part1, const char *part2, const char *part3, const char *part4, const char *part5, const char *part6) const {
	uint32_t h = hash(d, part1);
	if (part2) {
		h = hash(h, part2);
	}
	if (part3) {
		h = hash(h, part3);
	}
	if (part4) {
		h = hash(h, part4);
	}
	if (part5) {
		h = hash(h, part5);
	}
	if (part6) {
		h = hash(h, part6);
	}
	return h;
}

StringName OptimizedTranslationExtractor::get_message_multipart(const char *part1, const char *part2, const char *part3, const char *part4, const char *part5, const char *part6) const {
	return get_message_multipart_str(part1, part2, part3, part4, part5, part6);
}

StringName OptimizedTranslationExtractor::get_message(const char *p_src_text, const StringName &p_context) const {
	return get_message_str(p_src_text);
}

Vector<String> OptimizedTranslationExtractor::get_translated_message_list() const {
	Vector<String> msgs;

	const int *htr = hash_table.ptr();
	const uint32_t *htptr = (const uint32_t *)&htr[0];
	const int *btr = bucket_table.ptr();
	const uint32_t *btptr = (const uint32_t *)&btr[0];
	const uint8_t *sr = strings.ptr();
	const char *sptr = (const char *)&sr[0];

	for (int i = 0; i < hash_table.size(); i++) {
		uint32_t p = htptr[i];
		if (p != 0xFFFFFFFF) {
			const Bucket &bucket = *(const Bucket *)&btptr[p];
			for (int j = 0; j < bucket.size; j++) {
				if (bucket.elem[j].comp_size == bucket.elem[j].uncomp_size) {
					String rstr;
					rstr.append_utf8(&sptr[bucket.elem[j].str_offset], bucket.elem[j].uncomp_size);
					msgs.push_back(rstr);
				} else {
					CharString uncomp;
					uncomp.resize_uninitialized(bucket.elem[j].uncomp_size + 1);
					smaz_decompress(&sptr[bucket.elem[j].str_offset], bucket.elem[j].comp_size, uncomp.ptrw(), bucket.elem[j].uncomp_size);
					String rstr;
					rstr.append_utf8(uncomp.get_data());
					msgs.push_back(rstr);
				}
			}
		}
	}
	return msgs;
}

StringName OptimizedTranslationExtractor::get_plural_message(const StringName &p_src_text, const StringName &p_plural_text, int p_n, const StringName &p_context) const {
	// The use of plurals translation is not yet supported in OptimizedTranslationExtractor.
	return get_message(p_src_text, p_context);
}

void OptimizedTranslationExtractor::_get_property_list(List<PropertyInfo> *p_list) const {
	p_list->push_back(PropertyInfo(Variant::PACKED_INT32_ARRAY, "hash_table"));
	p_list->push_back(PropertyInfo(Variant::PACKED_INT32_ARRAY, "bucket_table"));
	p_list->push_back(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "strings"));
	p_list->push_back(PropertyInfo(Variant::OBJECT, "load_from", PROPERTY_HINT_RESOURCE_TYPE, "Translation", PROPERTY_USAGE_EDITOR));
}

void OptimizedTranslationExtractor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "from"), &OptimizedTranslationExtractor::generate);
}

HashSet<uint32_t> OptimizedTranslationExtractor::get_message_hash_set() const {
	Variant r_ret;

	int htsize = hash_table.size();

	if (htsize == 0) {
		return {};
	}

	const int *htr = hash_table.ptr();
	const uint32_t *htptr = (const uint32_t *)&htr[0];
	const int *btr = bucket_table.ptr();
	const uint32_t *btptr = (const uint32_t *)&btr[0];

	HashSet<uint32_t> ret;
	for (int i = 0; i < htsize; i++) {
		uint32_t p = htptr[i];
		if (p == -1) {
			continue;
		}
		const Bucket &bucket = *(const Bucket *)&btptr[p];
		for (int j = 0; j < bucket.size; j++) {
			String rstr;
			// bucket.elem[j].key
			ret.insert(bucket.elem[j].key);
		}
	}
	return ret;
}

void OptimizedTranslationExtractor::get_message_value_list(List<StringName> *r_messages) const {
	int htsize = hash_table.size();

	if (htsize == 0) {
		return;
	}

	const int *htr = hash_table.ptr();
	const uint32_t *htptr = (const uint32_t *)&htr[0];
	const int *btr = bucket_table.ptr();
	const uint32_t *btptr = (const uint32_t *)&btr[0];
	const uint8_t *sr = strings.ptr();
	const char *sptr = (const char *)&sr[0];

	for (int i = 0; i < htsize; i++) {
		uint32_t p = htptr[i];
		if (p == -1) {
			continue;
		}
		const Bucket &bucket = *(const Bucket *)&btptr[p];
		for (int j = 0; j < bucket.size; j++) {
			String rstr;
			if (bucket.elem[j].comp_size == bucket.elem[j].uncomp_size) {
				rstr.append_utf8(&sptr[bucket.elem[j].str_offset], bucket.elem[j].uncomp_size);
			} else {
				CharString uncomp;
				uncomp.resize_uninitialized(bucket.elem[j].uncomp_size + 1);
				smaz_decompress(&sptr[bucket.elem[j].str_offset], bucket.elem[j].comp_size, uncomp.ptrw(), bucket.elem[j].uncomp_size);
				rstr.append_utf8(uncomp.get_data());
			}
			r_messages->push_back(rstr);
		}
	}
}

String OptimizedTranslationExtractor::get_message_multipart_str(const char *part1, const char *part2, const char *part3, const char *part4, const char *part5, const char *part6) const {
	int htsize = hash_table.size();

	if (htsize == 0) {
		return StringName();
	}
	uint32_t h = hash_multipart(0, part1, part2, part3, part4, part5, part6);
	const int *htr = hash_table.ptr();
	const uint32_t *htptr = (const uint32_t *)&htr[0];
	const int *btr = bucket_table.ptr();
	const uint32_t *btptr = (const uint32_t *)&btr[0];
	const uint8_t *sr = strings.ptr();
	const char *sptr = (const char *)&sr[0];

	uint32_t p = htptr[h % htsize];

	if (p == 0xFFFFFFFF) {
		return StringName(); //nothing
	}

	const Bucket &bucket = *(const Bucket *)&btptr[p];

	h = hash_multipart(bucket.func, part1, part2, part3, part4, part5, part6);

	int idx = -1;

	for (int i = 0; i < bucket.size; i++) {
		if (bucket.elem[i].key == h) {
			idx = i;
			break;
		}
	}

	if (idx == -1) {
		return StringName();
	}

	if (bucket.elem[idx].comp_size == bucket.elem[idx].uncomp_size) {
		String rstr;
		rstr.append_utf8(&sptr[bucket.elem[idx].str_offset], bucket.elem[idx].uncomp_size);

		return rstr;
	} else {
		CharString uncomp;
		uncomp.resize_uninitialized(bucket.elem[idx].uncomp_size + 1);
		smaz_decompress(&sptr[bucket.elem[idx].str_offset], bucket.elem[idx].comp_size, uncomp.ptrw(), bucket.elem[idx].uncomp_size);
		String rstr;
		rstr.append_utf8(uncomp.get_data());
		return rstr;
	}
}

String OptimizedTranslationExtractor::get_message_str(const StringName &p_src_text) const {
	return get_message_str(p_src_text.operator String().utf8().get_data());
}

String OptimizedTranslationExtractor::get_message_str(const String &p_src_text) const {
	return get_message_str(p_src_text.utf8().get_data());
}

String OptimizedTranslationExtractor::get_message_str(const char *p_src_text) const {
	// p_context passed in is ignore. The use of context is not yet supported in OptimizedTranslationExtractor.

	int htsize = hash_table.size();

	if (htsize == 0) {
		return StringName();
	}

	uint32_t h = hash(0, p_src_text);

	const int *htr = hash_table.ptr();
	const uint32_t *htptr = (const uint32_t *)&htr[0];
	const int *btr = bucket_table.ptr();
	const uint32_t *btptr = (const uint32_t *)&btr[0];
	const uint8_t *sr = strings.ptr();
	const char *sptr = (const char *)&sr[0];

	uint32_t p = htptr[h % htsize];

	if (p == 0xFFFFFFFF) {
		return StringName(); //nothing
	}

	const Bucket &bucket = *(const Bucket *)&btptr[p];

	h = hash(bucket.func, p_src_text);

	int idx = -1;

	for (int i = 0; i < bucket.size; i++) {
		if (bucket.elem[i].key == h) {
			idx = i;
			break;
		}
	}

	if (idx == -1) {
		return StringName();
	}

	if (bucket.elem[idx].comp_size == bucket.elem[idx].uncomp_size) {
		String rstr;
		rstr.append_utf8(&sptr[bucket.elem[idx].str_offset], bucket.elem[idx].uncomp_size);

		return rstr;
	} else {
		CharString uncomp;
		uncomp.resize_uninitialized(bucket.elem[idx].uncomp_size + 1);
		smaz_decompress(&sptr[bucket.elem[idx].str_offset], bucket.elem[idx].comp_size, uncomp.ptrw(), bucket.elem[idx].uncomp_size);
		String rstr;
		rstr.append_utf8(uncomp.get_data());
		return rstr;
	}
}

Ref<OptimizedTranslationExtractor> OptimizedTranslationExtractor::create_from(const Ref<OptimizedTranslation> &p_otr) {
	Ref<OptimizedTranslationExtractor> ote;
	ote.instantiate();
	ote->set("locale", p_otr->get("locale"));
	ote->set("hash_table", p_otr->get("hash_table"));
	ote->set("bucket_table", p_otr->get("bucket_table"));
	ote->set("strings", p_otr->get("strings"));
	return ote;
}
