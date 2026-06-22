/**************************************************************************/
/*  config_file.cpp                                                       */
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

#include "config_file_compat.h"

#include "compat/variant_writer_compat.h"
#include "core/io/file_access_encrypted.h"
#include "core/object/class_db.h"
#include "core/string/string_builder.h"

#include "utility/gdre_settings.h"

void ConfigFileCompat::set_value(const String &p_section, const String &p_key, const Variant &p_value) {
	if (p_value.get_type() == Variant::NIL) { // Erase key.
		if (!values.has(p_section)) {
			return;
		}

		values[p_section].erase(p_key);
		if (values[p_section].is_empty()) {
			values.erase(p_section);
		}
	} else {
		if (!values.has(p_section)) {
			// Insert section-less keys at the beginning.
			values.insert(p_section, HashMap<String, Variant>(), p_section.is_empty());
		}

		values[p_section][p_key] = p_value;
	}
}

Variant ConfigFileCompat::get_value(const String &p_section, const String &p_key, const Variant &p_default) const {
	if (!values.has(p_section) || !values[p_section].has(p_key)) {
		ERR_FAIL_COND_V_MSG(p_default.get_type() == Variant::NIL, Variant(),
				vformat("Couldn't find the given section \"%s\" and key \"%s\", and no default was given.", p_section, p_key));
		return p_default;
	}

	return values[p_section][p_key];
}

bool ConfigFileCompat::has_section(const String &p_section) const {
	return values.has(p_section);
}

bool ConfigFileCompat::has_section_key(const String &p_section, const String &p_key) const {
	if (!values.has(p_section)) {
		return false;
	}
	return values[p_section].has(p_key);
}

Vector<String> ConfigFileCompat::get_sections() const {
	Vector<String> sections;
	sections.resize(values.size());

	int i = 0;
	String *sections_write = sections.ptrw();
	for (const KeyValue<String, HashMap<String, Variant>> &E : values) {
		sections_write[i++] = E.key;
	}

	return sections;
}

Vector<String> ConfigFileCompat::get_section_keys(const String &p_section) const {
	Vector<String> keys;
	ERR_FAIL_COND_V_MSG(!values.has(p_section), keys, vformat("Cannot get keys from nonexistent section \"%s\".", p_section));

	const HashMap<String, Variant> &keys_map = values[p_section];
	keys.resize(keys_map.size());

	int i = 0;
	String *keys_write = keys.ptrw();
	for (const KeyValue<String, Variant> &E : keys_map) {
		keys_write[i++] = E.key;
	}

	return keys;
}

Vector<Pair<String, Variant>> ConfigFileCompat::get_section_keys_with_values_beginning_with(const String &p_section, const String &p_prefix) const {
	Vector<Pair<String, Variant>> keys;
	ERR_FAIL_COND_V_MSG(!values.has(p_section), keys, vformat("Cannot get keys from nonexistent section \"%s\".", p_section));

	const HashMap<String, Variant> &keys_map = values[p_section];
	keys.reserve(keys_map.size());

	for (const KeyValue<String, Variant> &E : keys_map) {
		if (E.key.begins_with(p_prefix)) {
			keys.push_back({ E.key, E.value });
		}
	}

	return keys;
}

void ConfigFileCompat::erase_section(const String &p_section) {
	ERR_FAIL_COND_MSG(!values.has(p_section), vformat("Cannot erase nonexistent section \"%s\".", p_section));
	values.erase(p_section);
}

void ConfigFileCompat::erase_section_key(const String &p_section, const String &p_key) {
	ERR_FAIL_COND_MSG(!values.has(p_section), vformat("Cannot erase key \"%s\" from nonexistent section \"%s\".", p_key, p_section));
	ERR_FAIL_COND_MSG(!values[p_section].has(p_key), vformat("Cannot erase nonexistent key \"%s\" from section \"%s\".", p_key, p_section));

	values[p_section].erase(p_key);
	if (values[p_section].is_empty()) {
		values.erase(p_section);
	}
}

String ConfigFileCompat::encode_to_text() const {
	StringBuilder sb;
	bool first = true;
	for (const KeyValue<String, HashMap<String, Variant>> &E : values) {
		if (first) {
			first = false;
		} else {
			sb.append("\n");
		}
		if (!E.key.is_empty()) {
			sb.append("[" + E.key + "]\n\n");
		}

		for (const KeyValue<String, Variant> &F : E.value) {
			String vstr;
			VariantWriter::write_to_string(F.value, vstr);
			sb.append(F.key.property_name_encode() + "=" + vstr + "\n");
		}
	}
	return sb.as_string();
}

Error ConfigFileCompat::save(const String &p_path, int ver_major, int ver_minor) {
	Error err;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);

	if (err) {
		return err;
	}

	return _internal_save(file, ver_major, ver_minor);
}

Error ConfigFileCompat::save_encrypted(const String &p_path, const Vector<uint8_t> &p_key) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE, &err);

	if (err) {
		return err;
	}

	Ref<FileAccessEncrypted> fae;
	fae.instantiate();
	err = fae->open_and_parse(f, p_key, FileAccessEncrypted::MODE_WRITE_AES256);
	if (err) {
		return err;
	}
	return _internal_save(fae, 0, 0);
}

Error ConfigFileCompat::save_encrypted_pass(const String &p_path, const String &p_pass) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE, &err);

	if (err) {
		return err;
	}

	Ref<FileAccessEncrypted> fae;
	fae.instantiate();
	err = fae->open_and_parse_password(f, p_pass, FileAccessEncrypted::MODE_WRITE_AES256);
	if (err) {
		return err;
	}

	return _internal_save(fae, 0, 0);
}

Error ConfigFileCompat::_internal_save(Ref<FileAccess> file, int ver_major, int ver_minor) {
	bool first = true;
	for (const KeyValue<String, HashMap<String, Variant>> &E : values) {
		if (first) {
			first = false;
		} else {
			file->store_string("\n");
		}
		if (!E.key.is_empty()) {
			file->store_string("[" + E.key.replace("]", "\\]") + "]\n\n");
		}

		for (const KeyValue<String, Variant> &F : E.value) {
			auto val = F.value;
			if (val == NULL_REPLACEMENT) {
				val = Variant();
			}
			String vstr;
			VariantWriterCompat::write_to_string(val, vstr, ver_major, ver_minor, nullptr, nullptr, true);
			file->store_string(F.key.property_name_encode() + "=" + vstr + "\n");
		}
	}

	return OK;
}

Error ConfigFileCompat::load(const String &p_path) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);

	if (f.is_null()) {
		return err;
	}

	return _internal_load(p_path, f);
}

Error ConfigFileCompat::load_encrypted(const String &p_path, const Vector<uint8_t> &p_key) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);

	if (err) {
		return err;
	}

	Ref<FileAccessEncrypted> fae;
	fae.instantiate();
	err = fae->open_and_parse(f, p_key, FileAccessEncrypted::MODE_READ);
	if (err) {
		return err;
	}
	return _internal_load(p_path, fae);
}

Error ConfigFileCompat::load_encrypted_pass(const String &p_path, const String &p_pass) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);

	if (err) {
		return err;
	}

	Ref<FileAccessEncrypted> fae;
	fae.instantiate();
	err = fae->open_and_parse_password(f, p_pass, FileAccessEncrypted::MODE_READ);
	if (err) {
		return err;
	}

	return _internal_load(p_path, fae);
}

Error ConfigFileCompat::_internal_load(const String &p_path, Ref<FileAccess> f) {
	VariantParser::StreamFile stream;
	stream.f = f;

	Error err = _parse(p_path, &stream);

	return err;
}

Error ConfigFileCompat::parse(const String &p_data) {
	VariantParser::StreamString stream;
	stream.s = p_data;
	return _parse("<string>", &stream);
}

Error ConfigFileCompat::_parse(const String &p_path, VariantParser::Stream *p_stream) {
	String assign;
	Variant value;
	VariantParser::Tag next_tag;

	int lines = 0;
	String error_text;

	String section;

	VariantParser::ResourceParser parser;
	VariantParser::ResourceParser *parser_ptr = nullptr;
	if (GDRESettings::get_singleton() && !GDRESettings::get_singleton()->is_pack_loaded()) {
		parser_ptr = &parser;
		parser.func = VariantParserCompat::parse_and_create_missing_resource;
	}

	while (true) {
		assign = Variant();
		next_tag.fields.clear();
		next_tag.name = String();

		Error err = VariantParserCompat::parse_tag_assign_eof(p_stream, lines, error_text, next_tag, assign, value, parser_ptr, true);
		if (err == ERR_FILE_EOF) {
			return OK;
		} else if (err != OK) {
			ERR_PRINT(vformat("ConfigFileCompat parse error at %s:%d: %s.", p_path, lines, error_text));
			return err;
		}

		if (!assign.is_empty()) {
			if (value.get_type() == Variant::NIL) {
				value = NULL_REPLACEMENT;
			}
			set_value(section, assign, value);
		} else if (!next_tag.name.is_empty()) {
			section = next_tag.name.replace("\\]", "]");
		}
	}

	return OK;
}

void ConfigFileCompat::clear() {
	values.clear();
}

void ConfigFileCompat::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_value", "section", "key", "value"), &ConfigFileCompat::set_value);
	ClassDB::bind_method(D_METHOD("get_value", "section", "key", "default"), &ConfigFileCompat::get_value, DEFVAL(Variant()));

	ClassDB::bind_method(D_METHOD("has_section", "section"), &ConfigFileCompat::has_section);
	ClassDB::bind_method(D_METHOD("has_section_key", "section", "key"), &ConfigFileCompat::has_section_key);

	ClassDB::bind_method(D_METHOD("get_sections"), &ConfigFileCompat::get_sections);
	ClassDB::bind_method(D_METHOD("get_section_keys", "section"), &ConfigFileCompat::get_section_keys);

	ClassDB::bind_method(D_METHOD("erase_section", "section"), &ConfigFileCompat::erase_section);
	ClassDB::bind_method(D_METHOD("erase_section_key", "section", "key"), &ConfigFileCompat::erase_section_key);

	ClassDB::bind_method(D_METHOD("load", "path"), &ConfigFileCompat::load);
	ClassDB::bind_method(D_METHOD("parse", "data"), &ConfigFileCompat::parse);
	ClassDB::bind_method(D_METHOD("save", "path", "ver_major", "ver_minor"), &ConfigFileCompat::save, DEFVAL(0), DEFVAL(0));

	ClassDB::bind_method(D_METHOD("encode_to_text"), &ConfigFileCompat::encode_to_text);

	BIND_METHOD_ERR_RETURN_DOC("load", ERR_FILE_CANT_OPEN);

	ClassDB::bind_method(D_METHOD("load_encrypted", "path", "key"), &ConfigFileCompat::load_encrypted);
	ClassDB::bind_method(D_METHOD("load_encrypted_pass", "path", "password"), &ConfigFileCompat::load_encrypted_pass);

	ClassDB::bind_method(D_METHOD("save_encrypted", "path", "key"), &ConfigFileCompat::save_encrypted);
	ClassDB::bind_method(D_METHOD("save_encrypted_pass", "path", "password"), &ConfigFileCompat::save_encrypted_pass);

	ClassDB::bind_method(D_METHOD("clear"), &ConfigFileCompat::clear);
}
