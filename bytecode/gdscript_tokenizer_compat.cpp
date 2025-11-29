/**************************************************************************/
/*  gdscript_tokenizer_compat.cpp                                         */
/*  Modified from Godot 3.5.3											  */
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

#include "gdscript_tokenizer_compat.h"
#include "gdscript_v1_tokenizer_compat.h"
#include "gdscript_v2_tokenizer_buffer.h"
#include "gdscript_v2_tokenizer_compat.h"

#include "bytecode/bytecode_base.h"
#include "compat/variant_decoder_compat.h"
#include "core/templates/rb_map.h"

const char *GDScriptTokenizerCompat::token_names[] = {
	"Empty",
	"Identifier",
	"Constant",
	"Self",
	"Built-In Type",
	"Built-In Func",
	"in",
	"==",
	"!=",
	"<",
	"<=",
	">",
	">=",
	"and",
	"or",
	"not",
	"+",
	"-",
	"*",
	"/",
	"%",
	"<<",
	">>",
	"=",
	"+=",
	"-=",
	"*=",
	"/=",
	"%=",
	"<<=",
	">>=",
	"&=",
	"|=",
	"^=",
	"&",
	"|",
	"^",
	"~",
	//"Plus Plus",
	//"Minus Minus",
	"if",
	"elif",
	"else",
	"for",
	"while",
	"break",
	"continue",
	"pass",
	"return",
	"match",
	"func",
	"class",
	"class_name",
	"extends",
	"is",
	"onready",
	"tool",
	"static",
	"export",
	"setget",
	"const",
	"var",
	"as",
	"void",
	"enum",
	"preload",
	"assert",
	"yield",
	"signal",
	"breakpoint",
	"remote",
	"sync",
	"master",
	"puppet",
	"slave",
	"remotesync",
	"mastersync",
	"puppetsync",
	"[",
	"]",
	"{",
	"}",
	"(",
	")",
	",",
	";",
	".",
	"?",
	":",
	"$",
	"->",
	"\n",
	"PI",
	"TAU",
	"_",
	"INF",
	"NAN",
	"Error",
	"EOF",
	"Cursor",
	"slavesync",
	"do",
	"case",
	"switch",
	"Annotation",
	"Literal",
	"&&",
	"||",
	"!",
	"**", // STAR_STAR,
	"**=", // STAR_STAR_EQUAL,
	"when",
	"await",
	"namespace",
	"super",
	"trait",
	"..",
	"_",
	"VCS conflict marker", // VCS_CONFLICT_MARKER,
	"abstract", // added in 4.5
	"...", // added in 4.5
	"`", // BACKTICK,
};

static_assert((sizeof(GDScriptTokenizerCompat::token_names) / sizeof(GDScriptTokenizerCompat::token_names[0])) == GDScriptTokenizerCompat::Token::Type::G_TK_MAX, "Amount of token names don't match the amount of token types.");

String GDScriptTokenizerCompat::get_token_name(Token::Type p_token_type) {
	ERR_FAIL_INDEX_V_MSG(p_token_type, Token::Type::G_TK_MAX, "<error>", "Using token type out of the enum.");
	return token_names[(int)p_token_type];
}

const char *GDScriptTokenizerCompat::Token::get_name() const {
	return GDScriptTokenizerCompat::token_names[(int)type];
}

String GDScriptTokenizerCompat::Token::get_debug_name() const {
	switch (type) {
		case Type::G_TK_IDENTIFIER:
			return vformat(R"(identifier "%s")", source);
		default:
			return vformat(R"("%s")", get_name());
	}
}

bool GDScriptTokenizerCompat::Token::can_precede_bin_op() const {
	switch (type) {
		case Type::G_TK_IDENTIFIER:
		case Type::G_TK_CONSTANT:
		case Type::G_TK_SELF:
		case Type::G_TK_BRACKET_CLOSE:
		case Type::G_TK_CURLY_BRACKET_CLOSE:
		case Type::G_TK_PARENTHESIS_CLOSE:
		case Type::G_TK_CONST_PI:
		case Type::G_TK_CONST_TAU:
		case Type::G_TK_CONST_INF:
		case Type::G_TK_CONST_NAN:
			return true;
		default:
			return false;
	}
}

bool GDScriptTokenizerCompat::Token::is_identifier() const {
	// Note: Most keywords should not be recognized as identifiers.
	// These are only exceptions for stuff that already is on the engine's API.
	switch (type) {
		case Type::G_TK_IDENTIFIER:
		case Type::G_TK_CF_MATCH: // Used in String.match().
		case Type::G_TK_CF_WHEN: // New keyword, avoid breaking existing code.
		case Type::G_TK_ABSTRACT:
		// Allow constants to be treated as regular identifiers.
		case Type::G_TK_CONST_PI:
		case Type::G_TK_CONST_INF:
		case Type::G_TK_CONST_NAN:
		case Type::G_TK_CONST_TAU:
			return true;
		default:
			return false;
	}
}

bool GDScriptTokenizerCompat::Token::is_node_name() const {
	// This is meant to allow keywords with the $ notation, but not as general identifiers.
	switch (type) {
		case Type::G_TK_IDENTIFIER:
		case Type::G_TK_ABSTRACT:
		case Type::G_TK_OP_AND:
		case Type::G_TK_PR_AS:
		case Type::G_TK_PR_ASSERT:
		case Type::G_TK_PR_AWAIT:
		case Type::G_TK_CF_BREAK:
		case Type::G_TK_PR_BREAKPOINT:
		case Type::G_TK_PR_CLASS_NAME:
		case Type::G_TK_PR_CLASS:
		case Type::G_TK_PR_CONST:
		case Type::G_TK_CONST_PI:
		case Type::G_TK_CONST_INF:
		case Type::G_TK_CONST_NAN:
		case Type::G_TK_CONST_TAU:
		case Type::G_TK_CF_CONTINUE:
		case Type::G_TK_CF_ELIF:
		case Type::G_TK_CF_ELSE:
		case Type::G_TK_PR_ENUM:
		case Type::G_TK_PR_EXTENDS:
		case Type::G_TK_CF_FOR:
		case Type::G_TK_PR_FUNCTION:
		case Type::G_TK_CF_IF:
		case Type::G_TK_OP_IN:
		case Type::G_TK_PR_IS:
		case Type::G_TK_CF_MATCH:
		case Type::G_TK_PR_NAMESPACE:
		case Type::G_TK_OP_NOT:
		case Type::G_TK_OP_OR:
		case Type::G_TK_CF_PASS:
		case Type::G_TK_PR_PRELOAD:
		case Type::G_TK_CF_RETURN:
		case Type::G_TK_SELF:
		case Type::G_TK_PR_SIGNAL:
		case Type::G_TK_PR_STATIC:
		case Type::G_TK_PR_SUPER:
		case Type::G_TK_PR_TRAIT:
		case Type::G_TK_UNDERSCORE:
		case Type::G_TK_PR_VAR:
		case Type::G_TK_PR_VOID:
		case Type::G_TK_CF_WHILE:
		case Type::G_TK_CF_WHEN:
		case Type::G_TK_PR_YIELD:
			return true;
		default:
			return false;
	}
}

// static Vector<uint8_t> parse_code_string(const String &p_code, const GDScriptDecomp *p_decomp, String &error_message);
// static Ref<GDScriptTokenizerCompat> create_buffer_tokenizer(const Ref<GDScriptDecomp> &p_decomp, const Vector<uint8_t> &p_buffer);
// static Ref<GDScriptTokenizerCompat> create_text_tokenizer(const Ref<GDScriptDecomp> &p_decomp, const String &p_code);

Vector<uint8_t> GDScriptTokenizerCompat::parse_code_string(const String &p_code, const GDScriptDecomp *p_decomp, String &error_message) {
	if (p_decomp->get_bytecode_version() >= GDScriptDecomp::GDSCRIPT_2_0_VERSION) {
		return GDScriptV2TokenizerBufferCompat::parse_code_string(p_code, p_decomp, GDScriptV2TokenizerBufferCompat::CompressMode::COMPRESS_ZSTD);
	}

	return GDScriptV1TokenizerBufferCompat::parse_code_string(p_code, p_decomp, error_message);
}

Ref<GDScriptTokenizerCompat> GDScriptTokenizerCompat::create_buffer_tokenizer(const GDScriptDecomp *p_decomp, const Vector<uint8_t> &p_buffer) {
	if (p_decomp->get_bytecode_version() >= GDScriptDecomp::GDSCRIPT_2_0_VERSION) {
		auto tokenizer = Ref<GDScriptV2TokenizerBufferCompat>(memnew(GDScriptV2TokenizerBufferCompat(p_decomp)));
		tokenizer->set_code_buffer(p_buffer);
		return tokenizer;
	}

	auto tokenizer = Ref<GDScriptV1TokenizerBufferCompat>(memnew(GDScriptV1TokenizerBufferCompat(p_decomp)));
	tokenizer->set_code_buffer(p_buffer);
	return tokenizer;
}

Ref<GDScriptTokenizerCompat> GDScriptTokenizerCompat::create_text_tokenizer(const GDScriptDecomp *p_decomp, const String &p_code) {
	Ref<GDScriptTokenizerCompat> r_tokenizer;
	if (p_decomp->get_bytecode_version() >= GDScriptDecomp::GDSCRIPT_2_0_VERSION) {
		Ref<GDScriptV2TokenizerCompatText> tokenizer = Ref<GDScriptV2TokenizerCompatText>(memnew(GDScriptV2TokenizerCompatText(p_decomp)));
		tokenizer->set_source_code(p_code);
		r_tokenizer = tokenizer;
	} else {
		Ref<GDScriptV1TokenizerTextCompat> tokenizer = Ref<GDScriptV1TokenizerTextCompat>(memnew(GDScriptV1TokenizerTextCompat(p_decomp)));
		tokenizer->set_code(p_code);
		r_tokenizer = tokenizer;
	}
	return r_tokenizer;
}
