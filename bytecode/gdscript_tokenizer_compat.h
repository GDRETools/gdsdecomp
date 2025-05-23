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

#include "utility/godotver.h"

typedef char32_t CharType;

// NOTE: This only supports up to Godot 4.0-dev2; does not support any 4.x releases.
class GDScriptTokenizerTextCompat {
public:
	using Token = GDScriptDecomp::GlobalToken;
	using T = Token;

	enum {
		MAX_LOOKAHEAD = 4,
		TK_RB_SIZE = MAX_LOOKAHEAD * 2 + 1

	};
	enum StringMode {
		STRING_SINGLE_QUOTE,
		STRING_DOUBLE_QUOTE,
		STRING_MULTILINE
	};

	struct TokenData {
		Token type;
		StringName identifier; //for identifier types
		Variant constant; //for constant types
		union {
			Variant::Type vtype; //for type types
			int func; //function for built in functions
			int warning_code; //for warning skip
		};
		int line, col;
		TokenData() {
			type = T::G_TK_EMPTY;
			line = col = 0;
			vtype = Variant::NIL;
		}
	};

private:
	void _make_token(Token p_type);
	void _make_newline(int p_indentation = 0, int p_tabs = 0);
	void _make_identifier(const StringName &p_identifier);
	void _make_built_in_func(int p_func);
	void _make_constant(const Variant &p_constant);
	void _make_type(const Variant::Type &p_type);
	void _make_error(const String &p_error);

	String code;
	int len = 0;
	int code_pos = 0;
	const CharType *_code = nullptr;
	int line = 0;
	int column = 0;
	TokenData tk_rb[TK_RB_SIZE * 2 + 1];
	int tk_rb_pos = 0;
	String last_error;
	bool error_flag = 0;

	const Ref<GodotVer> engine_ver;
	const GDScriptDecomp *decomp;

	bool compat_newline_after_string_debug_fix = false;
	bool compat_bin_consts = false;
	bool compat_no_mixed_spaces = false;
	bool compat_underscore_num_consts = false;
	bool compat_gdscript_2_0 = false;

#ifdef DEBUG_ENABLED
	Vector<Pair<int, String>> warning_skips;
	RBSet<String> warning_global_skips;
	bool ignore_warnings = true;
#endif // DEBUG_ENABLED

	void _advance();

public:
	static const char *token_names[T::G_TK_MAX];

	static const char *get_token_name(Token p_token);

	// bool is_token_literal(int p_offset = 0, bool variable_safe = false) const;
	// StringName get_token_literal(int p_offset = 0) const;

	void set_code(const String &p_code);
	virtual GDScriptDecomp::GlobalToken get_token(int p_offset = 0) const;
	virtual StringName get_token_identifier(int p_offset = 0) const;
	virtual int get_token_built_in_func(int p_offset = 0) const;
	virtual Variant::Type get_token_type(int p_offset = 0) const;
	virtual int get_token_line(int p_offset = 0) const;
	virtual int get_token_column(int p_offset = 0) const;
	virtual int get_token_line_indent(int p_offset = 0) const;
	virtual int get_token_line_tab_indent(int p_offset = 0) const;
	virtual const Variant &get_token_constant(int p_offset = 0) const;
	virtual String get_token_error(int p_offset = 0) const;
	virtual void advance(int p_amount = 1);
#ifdef DEBUG_ENABLED
	virtual const Vector<Pair<int, String>> &get_warning_skips() const { return warning_skips; }
	virtual const RBSet<String> &get_warning_global_skips() const { return warning_global_skips; }
	virtual bool is_ignoring_warnings() const { return ignore_warnings; }
#endif // DEBUG_ENABLED

	GDScriptTokenizerTextCompat() = delete;
	explicit GDScriptTokenizerTextCompat(const GDScriptDecomp *p_decomp);
};
