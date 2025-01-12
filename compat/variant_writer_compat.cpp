#include "variant_writer_compat.h"
#include "core/crypto/crypto_core.h"

#include "compat/resource_loader_compat.h"
#include "core/object/script_language.h"
#include "image_parser_v2.h"
#include "input_event_parser_v2.h"

String num_scientific(double p_num) {
	if (Math::is_nan(p_num)) {
		return "nan";
	}

	if (Math::is_inf(p_num)) {
		if (signbit(p_num)) {
			return "-inf";
		} else {
			return "inf";
		}
	}

	char buf[256];

#if defined(__GNUC__) || defined(_MSC_VER)

#if defined(__MINGW32__) && defined(_TWO_DIGIT_EXPONENT) && !defined(_UCRT)
	// MinGW requires _set_output_format() to conform to C99 output for printf
	unsigned int old_exponent_format = _set_output_format(_TWO_DIGIT_EXPONENT);
#endif
	// TODO: remove this when the PR about precision is merged
	snprintf(buf, 256, "%.16lg", p_num);

#if defined(__MINGW32__) && defined(_TWO_DIGIT_EXPONENT) && !defined(_UCRT)
	_set_output_format(old_exponent_format);
#endif

#else
	sprintf(buf, "%.16lg", p_num);
#endif

	buf[255] = 0;

	return buf;
}

static String rtosfix(double p_value) {
	if (p_value == 0.0) {
		return "0"; //avoid negative zero (-0) being written, which may annoy git, svn, etc. for changes when they don't exist.
	} else if (isnan(p_value)) {
		return "nan";
	} else if (isinf(p_value)) {
		if (p_value > 0) {
			return "inf";
		} else {
			return "inf_neg";
		}
	} else {
		return num_scientific(p_value);
	}
}

Error VariantParserCompat::_parse_array(Array &array, Stream *p_stream, int &line, String &r_err_str, ResourceParser *p_res_parser) {
	Token token;
	bool need_comma = false;

	while (true) {
		if (p_stream->is_eof()) {
			r_err_str = "Unexpected End of File while parsing array";
			return ERR_FILE_CORRUPT;
		}

		Error err = get_token(p_stream, token, line, r_err_str);
		if (err != OK) {
			return err;
		}

		if (token.type == TK_BRACKET_CLOSE) {
			return OK;
		}

		if (need_comma) {
			if (token.type != TK_COMMA) {
				r_err_str = "Expected ','";
				return ERR_PARSE_ERROR;
			} else {
				need_comma = false;
				continue;
			}
		}

		Variant v;
		err = VariantParserCompat::parse_value(token, v, p_stream, line, r_err_str, p_res_parser);
		if (err) {
			return err;
		}

		array.push_back(v);
		need_comma = true;
	}
}

Error VariantParserCompat::_parse_dictionary(Dictionary &object, Stream *p_stream, int &line, String &r_err_str, ResourceParser *p_res_parser) {
	bool at_key = true;
	Variant key;
	Token token;
	bool need_comma = false;

	while (true) {
		if (p_stream->is_eof()) {
			r_err_str = "Unexpected End of File while parsing dictionary";
			return ERR_FILE_CORRUPT;
		}

		if (at_key) {
			Error err = get_token(p_stream, token, line, r_err_str);
			if (err != OK) {
				return err;
			}

			if (token.type == TK_CURLY_BRACKET_CLOSE) {
				return OK;
			}

			if (need_comma) {
				if (token.type != TK_COMMA) {
					r_err_str = "Expected '}' or ','";
					return ERR_PARSE_ERROR;
				} else {
					need_comma = false;
					continue;
				}
			}

			err = parse_value(token, key, p_stream, line, r_err_str, p_res_parser);

			if (err) {
				return err;
			}

			err = get_token(p_stream, token, line, r_err_str);

			if (err != OK) {
				return err;
			}
			if (token.type != TK_COLON) {
				r_err_str = "Expected ':'";
				return ERR_PARSE_ERROR;
			}
			at_key = false;
		} else {
			Error err = get_token(p_stream, token, line, r_err_str);
			if (err != OK) {
				return err;
			}

			Variant v;
			err = parse_value(token, v, p_stream, line, r_err_str, p_res_parser);
			if (err && err != ERR_FILE_MISSING_DEPENDENCIES) {
				return err;
			}
			object[key] = v;
			need_comma = true;
			at_key = true;
		}
	}
}

// Primarily for parsing V3 input event objects stored in project.godot
// The only other Objects that get stored inline in text resources that we know of are Position3D objects,
// and those are unchanged from Godot 2.x
Error VariantParserCompat::parse_value(VariantParser::Token &token, Variant &r_value, VariantParser::Stream *p_stream, int &line, String &r_err_str, VariantParser::ResourceParser *p_res_parser) {
	// Since Arrays and Dictionaries can have Objects inside of them...
	if (token.type == TK_CURLY_BRACKET_OPEN) {
		Dictionary d;
		Error err = _parse_dictionary(d, p_stream, line, r_err_str, p_res_parser);
		if (err) {
			return err;
		}
		r_value = d;
		return OK;
	} else if (token.type == TK_BRACKET_OPEN) {
		Array a;
		Error err = _parse_array(a, p_stream, line, r_err_str, p_res_parser);
		if (err) {
			return err;
		}
		r_value = a;
		return OK;
	} else if (token.type == TK_IDENTIFIER) {
		String id = token.value;
		if (id == "Object") {
			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_PARENTHESIS_OPEN) {
				r_err_str = "Expected '('";
				return ERR_PARSE_ERROR;
			}

			get_token(p_stream, token, line, r_err_str);

			if (token.type != TK_IDENTIFIER) {
				r_err_str = "Expected identifier with type of object";
				return ERR_PARSE_ERROR;
			}

			String type = token.value;
			// hacks for v3 input_event
			bool v3_input_key_hacks = type == "InputEventKey";
			Object *obj = ClassDB::instantiate(type);

			if (!obj) {
				r_err_str = "Can't instantiate Object() of type: " + type;
				return ERR_PARSE_ERROR;
			}

			Ref<RefCounted> ref = Ref<RefCounted>(Object::cast_to<RefCounted>(obj));

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_COMMA) {
				r_err_str = "Expected ',' after object type";
				return ERR_PARSE_ERROR;
			}

			bool at_key = true;
			String key;
			Token token2;
			bool need_comma = false;

			while (true) {
				if (p_stream->is_eof()) {
					r_err_str = "Unexpected End of File while parsing Object()";
					return ERR_FILE_CORRUPT;
				}

				if (at_key) {
					Error err = get_token(p_stream, token2, line, r_err_str);
					if (err != OK) {
						return err;
					}

					if (token2.type == TK_PARENTHESIS_CLOSE) {
						r_value = ref.is_valid() ? Variant(ref) : Variant(obj);
						return OK;
					}

					if (need_comma) {
						if (token2.type != TK_COMMA) {
							r_err_str = "Expected '}' or ','";
							return ERR_PARSE_ERROR;
						} else {
							need_comma = false;
							continue;
						}
					}

					if (token2.type != TK_STRING) {
						r_err_str = "Expected property name as string";
						return ERR_PARSE_ERROR;
					}

					key = token2.value;

					err = get_token(p_stream, token2, line, r_err_str);

					if (err != OK) {
						return err;
					}
					if (token2.type != TK_COLON) {
						r_err_str = "Expected ':'";
						return ERR_PARSE_ERROR;
					}
					at_key = false;
				} else {
					Error err = get_token(p_stream, token2, line, r_err_str);
					if (err != OK) {
						return err;
					}

					Variant v;
					err = parse_value(token2, v, p_stream, line, r_err_str, p_res_parser);

					if (err) {
						return err;
					}

					// Hacks for v3 input events
					// "scancode" was renamed to "keycode"
					// We don't check for engine version
					// If this is a v4 input_event, it won't have these
					// v2 input_events were variants and are handled below
					if (v3_input_key_hacks) {
						if (key == "scancode") {
							key = "keycode";
						} else if (key == "physical_scancode") {
							key = "physical_keycode";
						}
					}
					obj->set(key, v);
					need_comma = true;
					at_key = true;
				}
			}
		} else if (id == "Resource" || id == "SubResource" || id == "ExtResource") {
			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_PARENTHESIS_OPEN) {
				r_err_str = "Expected '('";
				return ERR_PARSE_ERROR;
			}

			if (p_res_parser && id == "Resource" && p_res_parser->func) {
				Ref<Resource> res;
				Error err = p_res_parser->func(p_res_parser->userdata, p_stream, res, line, r_err_str);
				if (err) {
					return err;
				}

				r_value = res;
			} else if (p_res_parser && id == "ExtResource" && p_res_parser->ext_func) {
				Ref<Resource> res;
				Error err = p_res_parser->ext_func(p_res_parser->userdata, p_stream, res, line, r_err_str);
				if (err) {
					// If the file is missing, the error can be ignored.
					if (err != ERR_FILE_NOT_FOUND && err != ERR_CANT_OPEN && err != ERR_FILE_CANT_OPEN) {
						return err;
					}
				}

				r_value = res;
			} else if (p_res_parser && id == "SubResource" && p_res_parser->sub_func) {
				Ref<Resource> res;
				Error err = p_res_parser->sub_func(p_res_parser->userdata, p_stream, res, line, r_err_str);
				if (err) {
					return err;
				}

				r_value = res;
			} else {
				get_token(p_stream, token, line, r_err_str);
				if (token.type == TK_STRING) {
					String path = token.value;
					// If this is an old-style resource, it's probably going to fail to load anyway,
					// so just do a fake load.
					Ref<Resource> res = ResourceCompatLoader::fake_load(path);
					if (res.is_null()) {
						r_err_str = "Can't load resource at path: '" + path + "'.";
						return ERR_PARSE_ERROR;
					}

					get_token(p_stream, token, line, r_err_str);
					if (token.type != TK_PARENTHESIS_CLOSE) {
						r_err_str = "Expected ')'";
						return ERR_PARSE_ERROR;
					}

					r_value = res;
				} else {
					r_err_str = "Expected string as argument for Resource().";
					return ERR_PARSE_ERROR;
				}
			}
		} else if (id == "Dictionary") {
			Error err = OK;

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_BRACKET_OPEN) {
				r_err_str = "Expected '['";
				return ERR_PARSE_ERROR;
			}

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_IDENTIFIER) {
				r_err_str = "Expected type identifier for key";
				return ERR_PARSE_ERROR;
			}

			static HashMap<StringName, Variant::Type> builtin_types;
			if (builtin_types.is_empty()) {
				for (int i = 1; i < Variant::VARIANT_MAX; i++) {
					builtin_types[Variant::get_type_name((Variant::Type)i)] = (Variant::Type)i;
				}
			}

			Dictionary dict;
			Variant::Type key_type = Variant::NIL;
			StringName key_class_name;
			Variant key_script;
			bool got_comma_token = false;
			if (builtin_types.has(token.value)) {
				key_type = builtin_types.get(token.value);
			} else if (token.value == "Resource" || token.value == "SubResource" || token.value == "ExtResource") {
				Variant resource;
				err = parse_value(token, resource, p_stream, line, r_err_str, p_res_parser);
				if (err) {
					if (token.value == "Resource" && err == ERR_PARSE_ERROR && r_err_str == "Expected '('" && token.type == TK_COMMA) {
						err = OK;
						r_err_str = String();
						key_type = Variant::OBJECT;
						key_class_name = token.value;
						got_comma_token = true;
					} else {
						return err;
					}
				} else {
					Ref<Script> script = resource;
					if (script.is_valid() && script->is_valid()) {
						key_type = Variant::OBJECT;
						key_class_name = script->get_instance_base_type();
						key_script = script;
					}
				}
			} else if (ClassDB::class_exists(token.value)) {
				key_type = Variant::OBJECT;
				key_class_name = token.value;
			}

			if (!got_comma_token) {
				get_token(p_stream, token, line, r_err_str);
				if (token.type != TK_COMMA) {
					r_err_str = "Expected ',' after key type";
					return ERR_PARSE_ERROR;
				}
			}

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_IDENTIFIER) {
				r_err_str = "Expected type identifier for value";
				return ERR_PARSE_ERROR;
			}

			Variant::Type value_type = Variant::NIL;
			StringName value_class_name;
			Variant value_script;
			bool got_bracket_token = false;
			if (builtin_types.has(token.value)) {
				value_type = builtin_types.get(token.value);
			} else if (token.value == "Resource" || token.value == "SubResource" || token.value == "ExtResource") {
				Variant resource;
				err = parse_value(token, resource, p_stream, line, r_err_str, p_res_parser);
				if (err) {
					if (token.value == "Resource" && err == ERR_PARSE_ERROR && r_err_str == "Expected '('" && token.type == TK_BRACKET_CLOSE) {
						err = OK;
						r_err_str = String();
						value_type = Variant::OBJECT;
						value_class_name = token.value;
						got_comma_token = true;
					} else {
						return err;
					}
				} else {
					Ref<Script> script = resource;
					if (script.is_valid() && script->is_valid()) {
						value_type = Variant::OBJECT;
						value_class_name = script->get_instance_base_type();
						value_script = script;
					}
				}
			} else if (ClassDB::class_exists(token.value)) {
				value_type = Variant::OBJECT;
				value_class_name = token.value;
			}

			// if (key_type != Variant::NIL || value_type != Variant::NIL) {
			// 	dict.set_typed(key_type, key_class_name, key_script, value_type, value_class_name, value_script);
			// }

			if (!got_bracket_token) {
				get_token(p_stream, token, line, r_err_str);
				if (token.type != TK_BRACKET_CLOSE) {
					r_err_str = "Expected ']'";
					return ERR_PARSE_ERROR;
				}
			}

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_PARENTHESIS_OPEN) {
				r_err_str = "Expected '('";
				return ERR_PARSE_ERROR;
			}

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_CURLY_BRACKET_OPEN) {
				r_err_str = "Expected '{'";
				return ERR_PARSE_ERROR;
			}

			Dictionary values;
			err = _parse_dictionary(values, p_stream, line, r_err_str, p_res_parser);
			if (err) {
				return err;
			}

			// COMPAT: Don't assign type if we don't have real objects
			if (key_type != Variant::NIL || value_type != Variant::NIL) {
				bool contains_missing_resources = false;
				// check if key_script or value_script is not null and if the class name is empty;
				// if the script is set and type class name is empty (likely because it's a fake script) then type assignment will fail.
				if (((!key_script.is_null() && key_type == Variant::OBJECT && key_class_name.is_empty()) || (!value_script.is_null() && value_type == Variant::OBJECT && value_class_name.is_empty()))) {
					contains_missing_resources = true;
				} else if (ResourceLoader::is_creating_missing_resources_if_class_unavailable_enabled()) {
					if (key_type == Variant::OBJECT && key_class_name != "MissingResource") {
						for (const Variant &key : values.keys()) {
							if (key.get_type() == Variant::OBJECT) {
								Ref<Resource> res = key;
								if (res.is_null()) {
									continue;
								}
								if (res->get_class() == "MissingResource") {
									contains_missing_resources = true;
									break;
								}
							}
						}
					}
					if (value_type == Variant::OBJECT && value_class_name != "MissingResource") {
						for (const Variant &value : values.values()) {
							if (value.get_type() == Variant::OBJECT) {
								Ref<Resource> res = value;
								if (res.is_null()) {
									continue;
								}
								if (res->get_class() == "MissingResource") {
									contains_missing_resources = true;
									break;
								}
							}
						}
					}
				}
				if (!contains_missing_resources) {
					dict.set_typed(key_type, key_class_name, key_script, value_type, value_class_name, value_script);
				}
			}

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_PARENTHESIS_CLOSE) {
				r_err_str = "Expected ')'";
				return ERR_PARSE_ERROR;
			}

			dict.assign(values);

			r_value = dict;
		} else if (id == "Array") {
			Error err = OK;

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_BRACKET_OPEN) {
				r_err_str = "Expected '['";
				return ERR_PARSE_ERROR;
			}

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_IDENTIFIER) {
				r_err_str = "Expected type identifier";
				return ERR_PARSE_ERROR;
			}

			static HashMap<String, Variant::Type> builtin_types;
			if (builtin_types.is_empty()) {
				for (int i = 1; i < Variant::VARIANT_MAX; i++) {
					builtin_types[Variant::get_type_name((Variant::Type)i)] = (Variant::Type)i;
				}
			}

			Array array = Array();
			bool got_bracket_token = false;
			Variant::Type type = Variant::NIL;
			StringName type_class_name;
			Variant var_script;
			if (builtin_types.has(token.value)) {
				type = builtin_types.get(token.value);
				// array.set_typed(builtin_types.get(token.value), StringName(), Variant());
			} else if (token.value == "Resource" || token.value == "SubResource" || token.value == "ExtResource") {
				Variant resource;
				err = parse_value(token, resource, p_stream, line, r_err_str, p_res_parser);
				if (err) {
					if (token.value == "Resource" && err == ERR_PARSE_ERROR && r_err_str == "Expected '('" && token.type == TK_BRACKET_CLOSE) {
						err = OK;
						r_err_str = String();
						type = Variant::OBJECT;
						type_class_name = token.value;
						// array.set_typed(Variant::OBJECT, token.value, Variant());
						got_bracket_token = true;
					} else {
						return err;
					}
				} else {
					Ref<Script> script = resource;
					if (script.is_valid() && script->is_valid()) {
						type = Variant::OBJECT;
						type_class_name = script->get_instance_base_type();
						var_script = resource;
					}
				}
			} else if (ClassDB::class_exists(token.value)) {
				type = Variant::OBJECT;
				type_class_name = token.value;
				// array.set_typed(Variant::OBJECT, token.value, Variant());
			}

			if (!got_bracket_token) {
				get_token(p_stream, token, line, r_err_str);
				if (token.type != TK_BRACKET_CLOSE) {
					r_err_str = "Expected ']'";
					return ERR_PARSE_ERROR;
				}
			}

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_PARENTHESIS_OPEN) {
				r_err_str = "Expected '('";
				return ERR_PARSE_ERROR;
			}

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_BRACKET_OPEN) {
				r_err_str = "Expected '['";
				return ERR_PARSE_ERROR;
			}

			Array values;
			err = _parse_array(values, p_stream, line, r_err_str, p_res_parser);
			if (err) {
				return err;
			}

			get_token(p_stream, token, line, r_err_str);
			if (token.type != TK_PARENTHESIS_CLOSE) {
				r_err_str = "Expected ')'";
				return ERR_PARSE_ERROR;
			}
			// COMPAT: Don't assign type if we don't have real objects
			if (type != Variant::NIL && !array.is_typed()) {
				bool contains_missing_resources = false;
				if (type == Variant::OBJECT) {
					// if var_script is set and type_class_name is empty (likely because it's a fake script) then type assignment will fail.
					if (!var_script.is_null() && type_class_name.is_empty()) {
						contains_missing_resources = true;
					} else if (!type_class_name.is_empty() && ResourceLoader::is_creating_missing_resources_if_class_unavailable_enabled() && type_class_name != "MissingResource") {
						for (int i = 0; i < values.size(); i++) {
							if (values[i].get_type() == Variant::OBJECT) {
								Ref<Resource> res = values[i];
								if (res.is_null()) {
									continue;
								}
								if (res->get_class() == "MissingResource") {
									contains_missing_resources = true;
									break;
								}
							}
						}
					}
				}
				if (!contains_missing_resources) {
					array.set_typed(type, type_class_name, var_script);
				}
			}

			array.assign(values);

			r_value = array;
		} else {
			return VariantParser::parse_value(token, r_value, p_stream, line, r_err_str, p_res_parser);
		}
		return OK;
		// If it's an Object, the above will eventually call return
	}
	// If this wasn't an Object or a Variant that can have objects stored inside them...
	return VariantParser::parse_value(token, r_value, p_stream, line, r_err_str, p_res_parser);
}

Error VariantParserCompat::parse_tag(VariantParser::Stream *p_stream, int &line, String &r_err_str, Tag &r_tag, VariantParser::ResourceParser *p_res_parser, bool p_simple_tag) {
	return VariantParser::parse_tag(p_stream, line, r_err_str, r_tag, p_res_parser, p_simple_tag);
}

Error VariantParserCompat::parse_tag_assign_eof(VariantParser::Stream *p_stream, int &line, String &r_err_str, Tag &r_tag, String &r_assign, Variant &r_value, VariantParser::ResourceParser *p_res_parser, bool p_simple_tag) {
	// assign..
	r_assign = "";
	String what;

	while (true) {
		char32_t c;
		if (p_stream->saved) {
			c = p_stream->saved;
			p_stream->saved = 0;
		} else {
			c = p_stream->get_char();
		}

		if (p_stream->is_eof()) {
			return ERR_FILE_EOF;
		}

		if (c == ';') { // comment
			while (true) {
				char32_t ch = p_stream->get_char();
				if (p_stream->is_eof()) {
					return ERR_FILE_EOF;
				}
				if (ch == '\n') {
					break;
				}
			}
			continue;
		}

		if (c == '[' && what.length() == 0) {
			// it's a tag!
			p_stream->saved = '['; // go back one
			Error err = parse_tag(p_stream, line, r_err_str, r_tag, p_res_parser, p_simple_tag);
			return err;
		}

		if (c > 32) {
			if (c == '"') { // quoted
				p_stream->saved = '"';
				Token tk;
				Error err = get_token(p_stream, tk, line, r_err_str);
				if (err) {
					return err;
				}
				if (tk.type != TK_STRING) {
					r_err_str = "Error reading quoted string";
					return ERR_INVALID_DATA;
				}

				what = tk.value;

			} else if (c != '=') {
				what += String::chr(c);
			} else {
				r_assign = what;
				Token token;
				get_token(p_stream, token, line, r_err_str);
				Error err;
				// VariantParserCompat hacks for compatibility
				if (token.type == TK_IDENTIFIER) {
					String id = token.value;
					// Old V2 Image
					if (id == "Image") {
						err = ImageParserV2::parse_image_construct_v2(p_stream, r_value, true, line, r_err_str);
					} else if (id == "InputEvent") { // Old V2 InputEvent
						err = InputEventParserV2::parse_input_event_construct_v2(p_stream, r_value, line, r_err_str);
					} else if (id == "mbutton" || id == "key" || id == "jbutton" || id == "jaxis") { // Old V2 InputEvent in project.cfg
						err = InputEventParserV2::parse_input_event_construct_v2(p_stream, r_value, line, r_err_str, id);
					} else {
						// Our own compat parse_value
						err = parse_value(token, r_value, p_stream, line, r_err_str, p_res_parser);
					}
					return err;
				}
				// end hacks
				err = parse_value(token, r_value, p_stream, line, r_err_str, p_res_parser);
				return err;
			}
		} else if (c == '\n') {
			line++;
		}
	}
	return OK;
}
namespace {
const static String comma_string = ", ";

template <class T>
_ALWAYS_INLINE_ String _make_element_string(const T &el) {
	static_assert(true, "Unsupported _write_vector_element type");
	return {};
}

#define MAKE_WRITE_PACKED_ELEMENT(type, str)                      \
	template <>                                                   \
	_ALWAYS_INLINE_ String _make_element_string(const type &el) { \
		return str;                                               \
	}

MAKE_WRITE_PACKED_ELEMENT(uint8_t, itos(el));
MAKE_WRITE_PACKED_ELEMENT(int32_t, itos(el));
MAKE_WRITE_PACKED_ELEMENT(int64_t, itos(el));
MAKE_WRITE_PACKED_ELEMENT(float, rtosfix(el));
MAKE_WRITE_PACKED_ELEMENT(double, rtosfix(el));
MAKE_WRITE_PACKED_ELEMENT(String, "\"" + el.c_escape() + "\"");
MAKE_WRITE_PACKED_ELEMENT(Vector2, rtosfix(el.x) + ", " + rtosfix(el.y));
MAKE_WRITE_PACKED_ELEMENT(Vector3, rtosfix(el.x) + ", " + rtosfix(el.y) + ", " + rtosfix(el.z));
MAKE_WRITE_PACKED_ELEMENT(Vector4, rtosfix(el.x) + ", " + rtosfix(el.y) + ", " + rtosfix(el.z) + ", " + rtosfix(el.w));
MAKE_WRITE_PACKED_ELEMENT(Color, rtosfix(el.r) + ", " + rtosfix(el.g) + ", " + rtosfix(el.b) + ", " + rtosfix(el.a));

template <class T>
void _ALWAYS_INLINE_ _write_packed_elements_noninteger(const Vector<T> &data, VariantWriterCompat::StoreStringFunc p_store_string_func, void *p_store_string_ud) {
	int len = data.size();
	const T *ptr = data.ptr();
	if (len > 0) {
		p_store_string_func(p_store_string_ud, _make_element_string(ptr[0]));
	}
	for (int i = 1; i < len; i++) {
		p_store_string_func(p_store_string_ud, comma_string);
		p_store_string_func(p_store_string_ud, _make_element_string(ptr[i]));
	}
}

template <class T>
constexpr _ALWAYS_INLINE_ uint64_t max_integer_str_len() {
	// ensure that this is an integral type
	static_assert(std::is_integral_v<T>, "Unsupported max_scalar_str_len type");
	if constexpr (sizeof(T) == 1) {
		// 127, 255 = 3 chars
		return 3;
	} else if constexpr (sizeof(T) == 2) {
		return 5;
	} else if constexpr (sizeof(T) == 4) { // int
		// 2147483647, 4294967295 = 10 chars
		return 10;
	} else if constexpr (sizeof(T) == 8 && std::is_signed_v<T>) { // int64
		// 9223372036854775807 = 19 chars
		return 19;
	} else if constexpr (sizeof(T) == 8 && std::is_unsigned_v<T>) { // uint64
		// 18446744073709551615 = 20 chars
		return 20;
	} else {
		static_assert(true, "Unsupported max_scalar_str_len type");
	}
	return 0;
}

uint64_t unsafe_write_num64(int64_t p_num, char32_t *c, uint64_t idx) {
	static constexpr int base = 10;

	bool sign = p_num < 0;

	int64_t n = p_num;

	int64_t chars = 0;
	do {
		n /= base;
		chars++;
	} while (n);

	if (sign) {
		chars++;
	}
	const uint64_t ret = chars;
	// c[idx + chars] = 0;
	n = p_num;
	do {
		int mod = ABS(n % base);
		c[(--chars + idx)] = '0' + mod;
		n /= base;
	} while (n);

	if (sign) {
		c[idx] = '-';
	}

	return ret;
}
// If this fails, take out the `unlikely` part below
static_assert(std::is_same_v<decltype(&String::resize), Error (String::*)(int)>);

// significantly speeds up writing PackedByteArrays
template <class T>
void _ALWAYS_INLINE_ _write_packed_elements_integer(const Vector<T> &data, VariantWriterCompat::StoreStringFunc p_store_string_func, void *p_store_string_ud) {
	int len = data.size();
	const uint64_t str_size = len * (max_integer_str_len<T>() + 2) + 1;
	const T *ptr = data.ptr();

	// In the unlikely case that the projected size is larger than INT32_MAX
	// (strings cannot currently be resized to be greater than that),
	// we fall back to the slow path.
	if (unlikely(str_size > INT32_MAX)) {
		_write_packed_elements_noninteger(data, p_store_string_func, p_store_string_ud);
		return;
	}

	String s;
	s.resize(str_size);
	auto *ptr_s = s.ptrw();

	uint64_t idx = 0;
	if (len > 0) {
		idx += unsafe_write_num64(ptr[0], ptr_s, idx);
	}
	for (int i = 1; i < len; i++) {
		ptr_s[idx++] = ',';
		ptr_s[idx++] = ' ';
		idx += unsafe_write_num64(ptr[i], ptr_s, idx);
	}
	ptr_s[idx] = 0;
	s.resize(idx + 1);
	p_store_string_func(p_store_string_ud, s);
}

template <class T>
void _ALWAYS_INLINE_ write_packed_elements(const Vector<T> &data, VariantWriterCompat::StoreStringFunc p_store_string_func, void *p_store_string_ud) {
	_write_packed_elements_noninteger(data, p_store_string_func, p_store_string_ud);
}

template <>
void _ALWAYS_INLINE_ write_packed_elements(const Vector<uint8_t> &data, VariantWriterCompat::StoreStringFunc p_store_string_func, void *p_store_string_ud) {
	_write_packed_elements_integer(data, p_store_string_func, p_store_string_ud);
}

template <>
void _ALWAYS_INLINE_ write_packed_elements(const Vector<int32_t> &data, VariantWriterCompat::StoreStringFunc p_store_string_func, void *p_store_string_ud) {
	_write_packed_elements_integer(data, p_store_string_func, p_store_string_ud);
}

template <>
void _ALWAYS_INLINE_ write_packed_elements(const Vector<int64_t> &data, VariantWriterCompat::StoreStringFunc p_store_string_func, void *p_store_string_ud) {
	_write_packed_elements_integer(data, p_store_string_func, p_store_string_ud);
}
} //namespace

Error VariantWriterCompat::write_compat_v4(const Variant &p_variant, StoreStringFunc p_store_string_func, void *p_store_string_ud, EncodeResourceFunc p_encode_res_func, void *p_encode_res_ud, int p_recursion_count, bool is_pcfg, bool p_compat, bool is_script) {
	switch (p_variant.get_type()) {
		case Variant::NIL: {
			p_store_string_func(p_store_string_ud, "null");
		} break;
		case Variant::BOOL: {
			p_store_string_func(p_store_string_ud, p_variant.operator bool() ? "true" : "false");
		} break;
		case Variant::INT: {
			p_store_string_func(p_store_string_ud, itos(p_variant.operator int64_t()));
		} break;
		case Variant::FLOAT: {
			String s = rtosfix(p_variant.operator double());
			if (s != "inf" && s != "inf_neg" && s != "nan") {
				if (!s.contains(".") && !s.contains("e")) {
					s += ".0";
				}
			}
			p_store_string_func(p_store_string_ud, s);
		} break;
		case Variant::STRING: {
			String str = p_variant;
			str = "\"" + str.c_escape_multiline() + "\"";
			p_store_string_func(p_store_string_ud, str);
		} break;

		// Math types.
		case Variant::VECTOR2: {
			Vector2 v = p_variant;
			p_store_string_func(p_store_string_ud, "Vector2(" + rtosfix(v.x) + ", " + rtosfix(v.y) + ")");
		} break;
		case Variant::VECTOR2I: {
			Vector2i v = p_variant;
			p_store_string_func(p_store_string_ud, "Vector2i(" + itos(v.x) + ", " + itos(v.y) + ")");
		} break;
		case Variant::RECT2: {
			Rect2 aabb = p_variant;
			p_store_string_func(p_store_string_ud, "Rect2(" + rtosfix(aabb.position.x) + ", " + rtosfix(aabb.position.y) + ", " + rtosfix(aabb.size.x) + ", " + rtosfix(aabb.size.y) + ")");
		} break;
		case Variant::RECT2I: {
			Rect2i aabb = p_variant;
			p_store_string_func(p_store_string_ud, "Rect2i(" + itos(aabb.position.x) + ", " + itos(aabb.position.y) + ", " + itos(aabb.size.x) + ", " + itos(aabb.size.y) + ")");
		} break;
		case Variant::VECTOR3: {
			Vector3 v = p_variant;
			p_store_string_func(p_store_string_ud, "Vector3(" + rtosfix(v.x) + ", " + rtosfix(v.y) + ", " + rtosfix(v.z) + ")");
		} break;
		case Variant::VECTOR3I: {
			Vector3i v = p_variant;
			p_store_string_func(p_store_string_ud, "Vector3i(" + itos(v.x) + ", " + itos(v.y) + ", " + itos(v.z) + ")");
		} break;
		case Variant::VECTOR4: {
			Vector4 v = p_variant;
			p_store_string_func(p_store_string_ud, "Vector4(" + rtosfix(v.x) + ", " + rtosfix(v.y) + ", " + rtosfix(v.z) + ", " + rtosfix(v.w) + ")");
		} break;
		case Variant::VECTOR4I: {
			Vector4i v = p_variant;
			p_store_string_func(p_store_string_ud, "Vector4i(" + itos(v.x) + ", " + itos(v.y) + ", " + itos(v.z) + ", " + itos(v.w) + ")");
		} break;
		case Variant::PLANE: {
			Plane p = p_variant;
			p_store_string_func(p_store_string_ud, "Plane(" + rtosfix(p.normal.x) + ", " + rtosfix(p.normal.y) + ", " + rtosfix(p.normal.z) + ", " + rtosfix(p.d) + ")");
		} break;
		case Variant::AABB: {
			AABB aabb = p_variant;
			p_store_string_func(p_store_string_ud, "AABB(" + rtosfix(aabb.position.x) + ", " + rtosfix(aabb.position.y) + ", " + rtosfix(aabb.position.z) + ", " + rtosfix(aabb.size.x) + ", " + rtosfix(aabb.size.y) + ", " + rtosfix(aabb.size.z) + ")");
		} break;
		case Variant::QUATERNION: {
			Quaternion quaternion = p_variant;
			p_store_string_func(p_store_string_ud, "Quaternion(" + rtosfix(quaternion.x) + ", " + rtosfix(quaternion.y) + ", " + rtosfix(quaternion.z) + ", " + rtosfix(quaternion.w) + ")");
		} break;
		case Variant::TRANSFORM2D: {
			String s = "Transform2D(";
			Transform2D m3 = p_variant;
			for (int i = 0; i < 3; i++) {
				for (int j = 0; j < 2; j++) {
					if (i != 0 || j != 0) {
						s += ", ";
					}
					s += rtosfix(m3.columns[i][j]);
				}
			}

			p_store_string_func(p_store_string_ud, s + ")");
		} break;
		case Variant::BASIS: {
			String s = "Basis(";
			Basis m3 = p_variant;
			for (int i = 0; i < 3; i++) {
				for (int j = 0; j < 3; j++) {
					if (i != 0 || j != 0) {
						s += ", ";
					}
					s += rtosfix(m3.rows[i][j]);
				}
			}

			p_store_string_func(p_store_string_ud, s + ")");
		} break;
		case Variant::TRANSFORM3D: {
			String s = "Transform3D(";
			Transform3D t = p_variant;
			Basis &m3 = t.basis;
			for (int i = 0; i < 3; i++) {
				for (int j = 0; j < 3; j++) {
					if (i != 0 || j != 0) {
						s += ", ";
					}
					s += rtosfix(m3.rows[i][j]);
				}
			}

			s = s + ", " + rtosfix(t.origin.x) + ", " + rtosfix(t.origin.y) + ", " + rtosfix(t.origin.z);

			p_store_string_func(p_store_string_ud, s + ")");
		} break;
		case Variant::PROJECTION: {
			String s = "Projection(";
			Projection t = p_variant;
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					if (i != 0 || j != 0) {
						s += ", ";
					}
					s += rtosfix(t.columns[i][j]);
				}
			}

			p_store_string_func(p_store_string_ud, s + ")");
		} break;

		// Misc types.
		case Variant::COLOR: {
			Color c = p_variant;
			p_store_string_func(p_store_string_ud, "Color(" + rtosfix(c.r) + ", " + rtosfix(c.g) + ", " + rtosfix(c.b) + ", " + rtosfix(c.a) + ")");
		} break;
		case Variant::STRING_NAME: {
			String str = p_variant;
			str = "&\"" + str.c_escape() + "\"";
			p_store_string_func(p_store_string_ud, str);
		} break;
		case Variant::NODE_PATH: {
			String str = p_variant;
			// In scripts, use the shorthand syntax.
			if (is_script) {
				str = "^\"" + str.c_escape() + "\"";
			} else {
				str = "NodePath(\"" + str.c_escape() + "\")";
			}
			p_store_string_func(p_store_string_ud, str);
		} break;
		case Variant::RID: {
			RID rid = p_variant;
			if (rid == RID()) {
				p_store_string_func(p_store_string_ud, "RID()");
			} else {
				p_store_string_func(p_store_string_ud, "RID(" + itos(rid.get_id()) + ")");
			}
		} break;

		// Do not really store these, but ensure that assignments are not empty.
		case Variant::SIGNAL: {
			p_store_string_func(p_store_string_ud, "Signal()");
		} break;
		case Variant::CALLABLE: {
			p_store_string_func(p_store_string_ud, "Callable()");
		} break;

		case Variant::OBJECT: {
			if (unlikely(p_recursion_count > MAX_RECURSION)) {
				ERR_PRINT("Max recursion reached");
				p_store_string_func(p_store_string_ud, "null");
				return OK;
			}
			p_recursion_count++;

			Object *obj = p_variant.get_validated_object();

			if (!obj) {
				p_store_string_func(p_store_string_ud, "null");
				break; // don't save it
			}

			Ref<Resource> res = p_variant;
			if (res.is_valid()) {
				//is resource
				String res_text;

				//try external function
				if (p_encode_res_func) {
					res_text = p_encode_res_func(p_encode_res_ud, res);
				}

				//try path because it's a file
				if (res_text.is_empty() && res->get_path().is_resource_file()) {
					//external resource
					String path = res->get_path();
					res_text = "Resource(\"" + path + "\")";
				}

				//could come up with some sort of text
				if (!res_text.is_empty()) {
					p_store_string_func(p_store_string_ud, res_text);
					break;
				}
			}

			//store as generic object

			p_store_string_func(p_store_string_ud, "Object(" + obj->get_class() + ",");

			List<PropertyInfo> props;
			obj->get_property_list(&props);
			bool first = true;
			for (const PropertyInfo &E : props) {
				if (E.usage & PROPERTY_USAGE_STORAGE || E.usage & PROPERTY_USAGE_SCRIPT_VARIABLE) {
					//must be serialized

					if (first) {
						first = false;
					} else {
						p_store_string_func(p_store_string_ud, ",");
					}

					p_store_string_func(p_store_string_ud, "\"" + E.name + "\":");
					write_compat_v4(obj->get(E.name), p_store_string_func, p_store_string_ud, p_encode_res_func, p_encode_res_ud, p_recursion_count, is_pcfg, p_compat, is_script);
				}
			}

			p_store_string_func(p_store_string_ud, ")\n");
		} break;

		case Variant::DICTIONARY: {
			Dictionary dict = p_variant;

			if (dict.is_typed()) {
				p_store_string_func(p_store_string_ud, "Dictionary[");

				Variant::Type key_builtin_type = (Variant::Type)dict.get_typed_key_builtin();
				StringName key_class_name = dict.get_typed_key_class_name();
				Ref<Script> key_script = dict.get_typed_key_script();

				if (key_script.is_valid()) {
					String resource_text;
					if (p_encode_res_func) {
						resource_text = p_encode_res_func(p_encode_res_ud, key_script);
					}
					if (resource_text.is_empty() && key_script->get_path().is_resource_file()) {
						resource_text = "Resource(\"" + key_script->get_path() + "\")";
					}

					if (!resource_text.is_empty()) {
						p_store_string_func(p_store_string_ud, resource_text);
					} else {
						ERR_PRINT("Failed to encode a path to a custom script for a dictionary key type.");
						p_store_string_func(p_store_string_ud, key_class_name);
					}
				} else if (key_class_name != StringName()) {
					p_store_string_func(p_store_string_ud, key_class_name);
				} else if (key_builtin_type == Variant::NIL) {
					p_store_string_func(p_store_string_ud, "Variant");
				} else {
					p_store_string_func(p_store_string_ud, Variant::get_type_name(key_builtin_type));
				}

				p_store_string_func(p_store_string_ud, ", ");

				Variant::Type value_builtin_type = (Variant::Type)dict.get_typed_value_builtin();
				StringName value_class_name = dict.get_typed_value_class_name();
				Ref<Script> value_script = dict.get_typed_value_script();

				if (value_script.is_valid()) {
					String resource_text;
					if (p_encode_res_func) {
						resource_text = p_encode_res_func(p_encode_res_ud, value_script);
					}
					if (resource_text.is_empty() && value_script->get_path().is_resource_file()) {
						resource_text = "Resource(\"" + value_script->get_path() + "\")";
					}

					if (!resource_text.is_empty()) {
						p_store_string_func(p_store_string_ud, resource_text);
					} else {
						ERR_PRINT("Failed to encode a path to a custom script for a dictionary value type.");
						p_store_string_func(p_store_string_ud, value_class_name);
					}
				} else if (value_class_name != StringName()) {
					p_store_string_func(p_store_string_ud, value_class_name);
				} else if (value_builtin_type == Variant::NIL) {
					p_store_string_func(p_store_string_ud, "Variant");
				} else {
					p_store_string_func(p_store_string_ud, Variant::get_type_name(value_builtin_type));
				}

				p_store_string_func(p_store_string_ud, "](");
			}

			if (unlikely(p_recursion_count > MAX_RECURSION)) {
				ERR_PRINT("Max recursion reached");
				p_store_string_func(p_store_string_ud, "{}");
			} else {
				List<Variant> keys;
				dict.get_key_list(&keys);
				keys.sort_custom<StringLikeVariantOrder>();

				if (keys.is_empty()) {
					// Avoid unnecessary line break.
					p_store_string_func(p_store_string_ud, "{}");
				} else {
					p_recursion_count++;

					p_store_string_func(p_store_string_ud, "{\n");

					for (List<Variant>::Element *E = keys.front(); E; E = E->next()) {
						write_compat_v4(E->get(), p_store_string_func, p_store_string_ud, p_encode_res_func, p_encode_res_ud, p_recursion_count, is_pcfg, p_compat, is_script);
						p_store_string_func(p_store_string_ud, ": ");
						write_compat_v4(dict[E->get()], p_store_string_func, p_store_string_ud, p_encode_res_func, p_encode_res_ud, p_recursion_count, is_pcfg, p_compat, is_script);
						if (E->next()) {
							p_store_string_func(p_store_string_ud, ",\n");
						} else {
							p_store_string_func(p_store_string_ud, "\n");
						}
					}

					p_store_string_func(p_store_string_ud, "}");
				}
			}

			if (dict.is_typed()) {
				p_store_string_func(p_store_string_ud, ")");
			}
		} break;

		case Variant::ARRAY: {
			Array array = p_variant;

			if (array.is_typed()) {
				p_store_string_func(p_store_string_ud, "Array[");

				Variant::Type builtin_type = (Variant::Type)array.get_typed_builtin();
				StringName class_name = array.get_typed_class_name();
				Ref<Script> script = array.get_typed_script();

				if (script.is_valid()) {
					String resource_text = String();
					if (p_encode_res_func) {
						resource_text = p_encode_res_func(p_encode_res_ud, script);
					}
					if (resource_text.is_empty() && script->get_path().is_resource_file()) {
						resource_text = "Resource(\"" + script->get_path() + "\")";
					}

					if (!resource_text.is_empty()) {
						p_store_string_func(p_store_string_ud, resource_text);
					} else {
						ERR_PRINT("Failed to encode a path to a custom script for an array type.");
						p_store_string_func(p_store_string_ud, class_name);
					}
				} else if (class_name != StringName()) {
					p_store_string_func(p_store_string_ud, class_name);
				} else {
					p_store_string_func(p_store_string_ud, Variant::get_type_name(builtin_type));
				}

				p_store_string_func(p_store_string_ud, "](");
			}

			if (unlikely(p_recursion_count > MAX_RECURSION)) {
				ERR_PRINT("Max recursion reached");
				p_store_string_func(p_store_string_ud, "[]");
			} else {
				p_recursion_count++;

				p_store_string_func(p_store_string_ud, "[");

				bool first = true;
				for (const Variant &var : array) {
					if (first) {
						first = false;
					} else {
						p_store_string_func(p_store_string_ud, ", ");
					}
					write_compat_v4(var, p_store_string_func, p_store_string_ud, p_encode_res_func, p_encode_res_ud, p_recursion_count, is_pcfg, p_compat, is_script);
				}

				p_store_string_func(p_store_string_ud, "]");
			}

			if (array.is_typed()) {
				p_store_string_func(p_store_string_ud, ")");
			}
		} break;

		case Variant::PACKED_BYTE_ARRAY: {
			p_store_string_func(p_store_string_ud, "PackedByteArray(");
			Vector<uint8_t> data = p_variant;
			if (p_compat) {
				write_packed_elements(data, p_store_string_func, p_store_string_ud);
			} else if (data.size() > 0) {
				p_store_string_func(p_store_string_ud, "\"");
				p_store_string_func(p_store_string_ud, CryptoCore::b64_encode_str(data.ptr(), data.size()));
				p_store_string_func(p_store_string_ud, "\"");
			}
			p_store_string_func(p_store_string_ud, ")");
		} break;
		case Variant::PACKED_INT32_ARRAY: {
			p_store_string_func(p_store_string_ud, "PackedInt32Array(");
			Vector<int32_t> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, ")");
		} break;
		case Variant::PACKED_INT64_ARRAY: {
			p_store_string_func(p_store_string_ud, "PackedInt64Array(");
			Vector<int64_t> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, ")");
		} break;
		case Variant::PACKED_FLOAT32_ARRAY: {
			p_store_string_func(p_store_string_ud, "PackedFloat32Array(");
			Vector<float> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, ")");
		} break;
		case Variant::PACKED_FLOAT64_ARRAY: {
			p_store_string_func(p_store_string_ud, "PackedFloat64Array(");
			Vector<double> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, ")");
		} break;
		case Variant::PACKED_STRING_ARRAY: {
			p_store_string_func(p_store_string_ud, "PackedStringArray(");
			Vector<String> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, ")");
		} break;
		case Variant::PACKED_VECTOR2_ARRAY: {
			p_store_string_func(p_store_string_ud, "PackedVector2Array(");
			Vector<Vector2> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, ")");
		} break;
		case Variant::PACKED_VECTOR3_ARRAY: {
			p_store_string_func(p_store_string_ud, "PackedVector3Array(");
			Vector<Vector3> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, ")");
		} break;
		case Variant::PACKED_COLOR_ARRAY: {
			p_store_string_func(p_store_string_ud, "PackedColorArray(");
			Vector<Color> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, ")");
		} break;
		case Variant::PACKED_VECTOR4_ARRAY: {
			p_store_string_func(p_store_string_ud, "PackedVector4Array(");
			Vector<Vector4> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, ")");
		} break;

		default: {
			ERR_PRINT("Unknown variant type");
			return ERR_BUG;
		}
	}

	return OK;
}

Error VariantWriterCompat::write_compat(const Variant &p_variant, const uint32_t ver_major, StoreStringFunc p_store_string_func, void *p_store_string_ud, EncodeResourceFunc p_encode_res_func, void *p_encode_res_ud, bool is_pcfg, bool p_compat, bool is_script) {
	// use the v4 write function instead for v4
	if (ver_major == 4) {
		return VariantWriterCompat::write_compat_v4(p_variant, p_store_string_func, p_store_string_ud, p_encode_res_func, p_encode_res_ud, 0, is_pcfg, p_compat, is_script);
	}

	// for v2 and v3...
	switch ((Variant::Type)p_variant.get_type()) {
		case Variant::Type::NIL: {
			p_store_string_func(p_store_string_ud, "null");
		} break;
		case Variant::BOOL: {
			p_store_string_func(p_store_string_ud, p_variant.operator bool() ? "true" : "false");
		} break;
		case Variant::INT: {
			p_store_string_func(p_store_string_ud, itos(p_variant.operator int()));
		} break;
		case Variant::FLOAT: { // "REAL" in v2 and v3
			String s = rtosfix(p_variant.operator double());
			if (s.find(".") == -1 && s.find("e") == -1)
				s += ".0";
			p_store_string_func(p_store_string_ud, s);
		} break;
		case Variant::STRING: {
			String str = p_variant;

			str = "\"" + str.c_escape_multiline() + "\"";
			p_store_string_func(p_store_string_ud, str);
		} break;
		case Variant::VECTOR2: {
			Vector2 v = p_variant;
			p_store_string_func(p_store_string_ud, "Vector2( " + rtosfix(v.x) + ", " + rtosfix(v.y) + " )");
		} break;
		case Variant::RECT2: {
			Rect2 aabb = p_variant;
			p_store_string_func(p_store_string_ud, "Rect2( " + rtosfix(aabb.position.x) + ", " + rtosfix(aabb.position.y) + ", " + rtosfix(aabb.size.x) + ", " + rtosfix(aabb.size.y) + " )");

		} break;
		case Variant::VECTOR3: {
			Vector3 v = p_variant;
			p_store_string_func(p_store_string_ud, "Vector3( " + rtosfix(v.x) + ", " + rtosfix(v.y) + ", " + rtosfix(v.z) + " )");
		} break;
		case Variant::TRANSFORM2D: { // v2 Matrix32
			String s = ver_major == 2 ? "Matrix32( " : "Transform2D( ";
			Transform2D m3 = p_variant;
			for (int i = 0; i < 3; i++) {
				for (int j = 0; j < 2; j++) {
					if (i != 0 || j != 0)
						s += ", ";
					s += rtosfix(m3.columns[i][j]);
				}
			}

			p_store_string_func(p_store_string_ud, s + " )");

		} break;
		case Variant::PLANE: {
			Plane p = p_variant;
			p_store_string_func(p_store_string_ud, "Plane( " + rtosfix(p.normal.x) + ", " + rtosfix(p.normal.y) + ", " + rtosfix(p.normal.z) + ", " + rtosfix(p.d) + " )");

		} break;
		case Variant::AABB: { // _AABB in v2
			AABB aabb = p_variant;
			p_store_string_func(p_store_string_ud, "AABB( " + rtosfix(aabb.position.x) + ", " + rtosfix(aabb.position.y) + ", " + rtosfix(aabb.position.z) + ", " + rtosfix(aabb.size.x) + ", " + rtosfix(aabb.size.y) + ", " + rtosfix(aabb.size.z) + " )");

		} break;
		case Variant::QUATERNION: { // QUAT in v2 and v3
			Quaternion quat = p_variant;
			p_store_string_func(p_store_string_ud, "Quat( " + rtosfix(quat.x) + ", " + rtosfix(quat.y) + ", " + rtosfix(quat.z) + ", " + rtosfix(quat.w) + " )");

		} break;

		case Variant::BASIS: { // v2 Matrix3

			String s = ver_major == 2 ? "Matrix3( " : "Basis( ";
			Basis m3 = p_variant;
			for (int i = 0; i < 3; i++) {
				for (int j = 0; j < 3; j++) {
					if (i != 0 || j != 0)
						s += ", ";
					s += rtosfix(m3.rows[i][j]);
				}
			}

			p_store_string_func(p_store_string_ud, s + " )");

		} break;
		case Variant::TRANSFORM3D: { // TRANSFORM in v2 and v3
			String s = "Transform( ";
			Transform3D t = p_variant;
			Basis &m3 = t.basis;
			for (int i = 0; i < 3; i++) {
				for (int j = 0; j < 3; j++) {
					if (i != 0 || j != 0)
						s += ", ";
					s += rtosfix(m3.rows[i][j]);
				}
			}

			s = s + ", " + rtosfix(t.origin.x) + ", " + rtosfix(t.origin.y) + ", " + rtosfix(t.origin.z);

			p_store_string_func(p_store_string_ud, s + " )");
		} break;
		case Variant::COLOR: {
			Color c = p_variant;
			if (ver_major == 2 && is_pcfg) {
				p_store_string_func(p_store_string_ud, "#" + c.to_html());
			} else {
				p_store_string_func(p_store_string_ud, "Color( " + rtosfix(c.r) + ", " + rtosfix(c.g) + ", " + rtosfix(c.b) + ", " + rtosfix(c.a) + " )");
			}

		} break;
		case Variant::NODE_PATH: {
			String str = p_variant;
			// in code files, use the short form
			if (is_script) {
				str = "@\"" + str.c_escape() + "\"";
			} else {
				str = "NodePath(\"" + str.c_escape() + "\")";
			}
			p_store_string_func(p_store_string_ud, str);

		} break;

		case Variant::OBJECT: {
			Object *obj = p_variant;

			if (!obj) {
				p_store_string_func(p_store_string_ud, "null");
				break; // don't save it
			}
			bool v3_input_key_hacks = false;
			Ref<Resource> res = p_variant;
			if (res.is_valid()) {
				// is resource
				String res_text;
				// Hack for V2 Images
				if (ver_major == 2 && res->is_class("Image")) {
					res_text = ImageParserV2::image_v2_to_string(res, is_pcfg);
				} else if (ver_major == 2 && res->is_class("InputEvent")) {
					res_text = InputEventParserV2::v4_input_event_to_v2_string(res, is_pcfg);
				} else if (p_encode_res_func) {
					// try external function
					res_text = p_encode_res_func(p_encode_res_ud, res);
				}
				if (res_text.is_empty()) {
					if (ver_major == 3 && res->is_class("InputEventKey") && is_pcfg) {
						// Hacks for v3 InputEventKeys stored inline in project.godot
						v3_input_key_hacks = true;
					} else if (res->get_path().is_resource_file()) {
						// external resource
						String path = res->get_path();
						res_text = "Resource( \"" + path + "\")";
					}
				}

				// could come up with some sort of text
				if (!res_text.is_empty()) {
					p_store_string_func(p_store_string_ud, res_text);
					break;
				}
			}

			// store as generic object

			p_store_string_func(p_store_string_ud, "Object(" + obj->get_class() + ",");

			List<PropertyInfo> props;
			obj->get_property_list(&props);
			bool first = true;
			for (List<PropertyInfo>::Element *E = props.front(); E; E = E->next()) {
				if (E->get().usage & PROPERTY_USAGE_STORAGE || E->get().usage & PROPERTY_USAGE_SCRIPT_VARIABLE) {
					// must be serialized

					if (first) {
						first = false;
					} else {
						p_store_string_func(p_store_string_ud, ",");
					}
					// v3 InputEventKey hacks
					String compat_name = E->get().name;
					if (v3_input_key_hacks) {
						if (compat_name == "keycode") {
							compat_name = "scancode";
						} else if (compat_name == "physical_keycode") {
							compat_name = "physical_scancode";
						}
					}
					p_store_string_func(p_store_string_ud, "\"" + compat_name + "\":");
					write_compat(obj->get(E->get().name), ver_major, p_store_string_func, p_store_string_ud, p_encode_res_func, p_encode_res_ud, is_pcfg, p_compat, is_script);
				}
			}

			p_store_string_func(p_store_string_ud, ")\n");

		} break;
		//case Variant::INPUT_EVENT: {  only in V2, does not exist in v3 and v4
		// this is an object handled above.
		//	WARN_PRINT("Attempted to save Input Event!");
		//} break;
		case Variant::DICTIONARY: {
			Dictionary dict = p_variant;

			List<Variant> keys;
			dict.get_key_list(&keys);
			keys.sort();

			p_store_string_func(p_store_string_ud, "{\n");
			for (List<Variant>::Element *E = keys.front(); E; E = E->next()) {
				/*
				if (!_check_type(dict[E->get()]))
					continue;
				*/
				write_compat(E->get(), ver_major, p_store_string_func, p_store_string_ud, p_encode_res_func, p_encode_res_ud, is_pcfg, p_compat, is_script);
				p_store_string_func(p_store_string_ud, ": ");
				write_compat(dict[E->get()], ver_major, p_store_string_func, p_store_string_ud, p_encode_res_func, p_encode_res_ud, is_pcfg, p_compat, is_script);
				if (E->next())
					p_store_string_func(p_store_string_ud, ",\n");
			}

			p_store_string_func(p_store_string_ud, "\n}");

		} break;
		case Variant::ARRAY: {
			p_store_string_func(p_store_string_ud, "[ ");
			Array array = p_variant;
			int len = array.size();
			if (len > 0) {
				write_compat(array[0], ver_major, p_store_string_func, p_store_string_ud, p_encode_res_func, p_encode_res_ud, is_pcfg, p_compat, is_script);
			}
			for (int i = 1; i < len; i++) {
				p_store_string_func(p_store_string_ud, ", ");
				write_compat(array[i], ver_major, p_store_string_func, p_store_string_ud, p_encode_res_func, p_encode_res_ud, is_pcfg, p_compat, is_script);
			}
			p_store_string_func(p_store_string_ud, " ]");

		} break;

		case Variant::PACKED_BYTE_ARRAY: { // v2 ByteArray, v3 POOL_BYTE_ARRAY
			if (ver_major == 2 && is_pcfg) {
				p_store_string_func(p_store_string_ud, "[ ");
			} else {
				p_store_string_func(p_store_string_ud, ver_major == 2 ? "ByteArray( " : "PoolByteArray( ");
			}
			String s;
			Vector<uint8_t> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);
			if (ver_major == 2 && is_pcfg) {
				p_store_string_func(p_store_string_ud, " ]");
			} else {
				p_store_string_func(p_store_string_ud, " )");
			}
		} break;
		case Variant::PACKED_INT32_ARRAY: { // v2 IntArray, v3 POOL_INT_ARRAY
			if (ver_major == 2 && is_pcfg) {
				p_store_string_func(p_store_string_ud, "[ ");
			} else {
				p_store_string_func(p_store_string_ud, ver_major == 2 ? "IntArray( " : "PoolIntArray( ");
			}
			Vector<int> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			if (ver_major == 2 && is_pcfg) {
				p_store_string_func(p_store_string_ud, " ]");
			} else {
				p_store_string_func(p_store_string_ud, " )");
			}

		} break;
		//case Variant::PACKED_INT64_ARRAY: { // v2 and v3 did not have 64-bit ints
		case Variant::PACKED_FLOAT32_ARRAY: { // v2 FloatArray, v3 POOL_REAL_ARRAY
			if (ver_major == 2 && is_pcfg) {
				p_store_string_func(p_store_string_ud, "[ ");
			} else {
				p_store_string_func(p_store_string_ud, ver_major == 2 ? "FloatArray( " : "PoolRealArray( ");
			}
			// TODO: fix this for real_t is double builds
			Vector<real_t> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			if (ver_major == 2 && is_pcfg) {
				p_store_string_func(p_store_string_ud, " ]");
			} else {
				p_store_string_func(p_store_string_ud, " )");
			}

		} break;
		//case Variant::PACKED_FLOAT64_ARRAY: { // v2 and v3 did not have 64-bit floats
		case Variant::PACKED_STRING_ARRAY: { // v2 StringArray, v3 POOL_STRING_ARRAY
			if (ver_major == 2 && is_pcfg) {
				p_store_string_func(p_store_string_ud, "[ ");
			} else {
				p_store_string_func(p_store_string_ud, ver_major == 2 ? "StringArray( " : "PoolStringArray( ");
			}
			Vector<String> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			if (ver_major == 2 && is_pcfg) {
				p_store_string_func(p_store_string_ud, " ]");
			} else {
				p_store_string_func(p_store_string_ud, " )");
			}

		} break;
		// no pcfg variants for the rest of the arrays
		case Variant::PACKED_VECTOR2_ARRAY: { // v2 Vector2Array, v3 POOL_VECTOR2_ARRAY

			p_store_string_func(p_store_string_ud, ver_major == 2 ? "Vector2Array( " : "PoolVector2Array( ");
			Vector<Vector2> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, " )");

		} break;
		case Variant::PACKED_VECTOR3_ARRAY: { // v2 Vector3Array, v3 POOL_VECTOR3_ARRAY

			p_store_string_func(p_store_string_ud, ver_major == 2 ? "Vector3Array( " : "PoolVector3Array( ");
			Vector<Vector3> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, " )");

		} break;
		case Variant::PACKED_COLOR_ARRAY: { // v2 ColorArray, v3 POOL_COLOR_ARRAY

			p_store_string_func(p_store_string_ud, ver_major == 2 ? "ColorArray( " : "PoolColorArray( ");

			Vector<Color> data = p_variant;
			write_packed_elements(data, p_store_string_func, p_store_string_ud);

			p_store_string_func(p_store_string_ud, " )");

		} break;
		default: {
		}
	}
	return OK;
}

static Error _write_to_str(void *ud, const String &p_string) {
	String *str = (String *)ud;
	(*str) += p_string;
	return OK;
}

Error VariantWriterCompat::write_to_string_script(const Variant &p_variant, String &r_string, const uint32_t ver_major, EncodeResourceFunc p_encode_res_func, void *p_encode_res_ud, bool p_compat_4x_force_v3) {
	r_string = String();
	return write_compat(p_variant, ver_major, _write_to_str, &r_string, p_encode_res_func, p_encode_res_ud, false, p_compat_4x_force_v3, true);
}
// project.cfg variants are written differently than resource variants in Godot 2.x
Error VariantWriterCompat::write_to_string_pcfg(const Variant &p_variant, String &r_string, const uint32_t ver_major, EncodeResourceFunc p_encode_res_func, void *p_encode_res_ud, bool p_compat_4x_force_v3) {
	r_string = String();
	return write_compat(p_variant, ver_major, _write_to_str, &r_string, p_encode_res_func, p_encode_res_ud, true, p_compat_4x_force_v3, false);
}
Error VariantWriterCompat::write_to_string(const Variant &p_variant, String &r_string, const uint32_t ver_major, EncodeResourceFunc p_encode_res_func, void *p_encode_res_ud, bool p_compat_4x_force_v3) {
	r_string = String();
	return write_compat(p_variant, ver_major, _write_to_str, &r_string, p_encode_res_func, p_encode_res_ud, false, p_compat_4x_force_v3, false);
}
