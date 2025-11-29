/**************************************************************************/
/*  gdscript_tokenizer_compat.h                                           */
/*  Modified from Godot 3.5.3                                             */
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

#pragma once

#include "bytecode_base.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/pair.h"
#include "core/templates/rb_set.h"
#include "core/variant/variant.h"

#include "core/templates/rb_map.h"
#include "utility/godotver.h"

typedef char32_t CharType;

class GDScriptTokenizerCompat : public RefCounted {
	GDCLASS(GDScriptTokenizerCompat, RefCounted);

public:
	enum CursorPlace {
		CURSOR_NONE,
		CURSOR_BEGINNING,
		CURSOR_MIDDLE,
		CURSOR_END,
	};

	struct Token {
		using Type = GDScriptDecomp::GlobalToken;

		Type type = Type::G_TK_EMPTY;
		Variant literal;
		int start_line = 0, end_line = 0, start_column = 0, end_column = 0;
		int leftmost_column = 0, rightmost_column = 0; // Column span for multiline tokens.
		int cursor_position = -1;
		CursorPlace cursor_place = CURSOR_NONE;
		String source;

		Variant::Type vtype = Variant::VARIANT_MAX;
		int func = -1;
		int current_indent = 0;

		const char *get_name() const;
		String get_debug_name() const;
		bool can_precede_bin_op() const;
		bool is_identifier() const;
		bool is_node_name() const;
		StringName get_identifier() const { return literal; }
		String get_func_name() const { return literal; }
		String get_error() const { return literal; }

		Token(Type p_type) {
			type = p_type;
		}

		Token() {}
	};
	static const char *token_names[Token::Type::G_TK_MAX];

	static Vector<uint8_t> parse_code_string(const String &p_code, const GDScriptDecomp *p_decomp, String &error_message);
	static Ref<GDScriptTokenizerCompat> create_buffer_tokenizer(const GDScriptDecomp *p_decomp, const Vector<uint8_t> &p_buffer);
	static Ref<GDScriptTokenizerCompat> create_text_tokenizer(const GDScriptDecomp *p_decomp, const String &p_code);
	static String get_token_name(Token::Type p_token_type);

	virtual Token scan() = 0;

	virtual ~GDScriptTokenizerCompat() {}
};
