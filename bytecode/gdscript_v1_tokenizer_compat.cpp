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

#include "gdscript_v1_tokenizer_compat.h"

#include "bytecode/bytecode_base.h"
#include "compat/variant_decoder_compat.h"
#include "core/templates/rb_map.h"

struct _bit {
	Variant::Type type;
	const char *text;
};
//built in types

struct _kws {
	GDScriptTokenizerCompat::Token::Type token;
	const char *text;
};

static const _kws _keyword_list[] = {
	//ops
	{ GDScriptDecomp::G_TK_OP_IN, "in" },
	{ GDScriptDecomp::G_TK_OP_NOT, "not" },
	{ GDScriptDecomp::G_TK_OP_OR, "or" },
	{ GDScriptDecomp::G_TK_OP_AND, "and" },
	//func
	{ GDScriptDecomp::G_TK_PR_FUNCTION, "func" },
	{ GDScriptDecomp::G_TK_PR_CLASS, "class" },
	{ GDScriptDecomp::G_TK_PR_CLASS_NAME, "class_name" },
	{ GDScriptDecomp::G_TK_PR_EXTENDS, "extends" },
	{ GDScriptDecomp::G_TK_PR_IS, "is" },
	{ GDScriptDecomp::G_TK_PR_ONREADY, "onready" },
	{ GDScriptDecomp::G_TK_PR_TOOL, "tool" },
	{ GDScriptDecomp::G_TK_PR_STATIC, "static" },
	{ GDScriptDecomp::G_TK_PR_EXPORT, "export" },
	{ GDScriptDecomp::G_TK_PR_SETGET, "setget" },
	{ GDScriptDecomp::G_TK_PR_VAR, "var" },
	{ GDScriptDecomp::G_TK_PR_AS, "as" },
	{ GDScriptDecomp::G_TK_PR_VOID, "void" },
	{ GDScriptDecomp::G_TK_PR_PRELOAD, "preload" },
	{ GDScriptDecomp::G_TK_PR_ASSERT, "assert" },
	{ GDScriptDecomp::G_TK_PR_YIELD, "yield" },
	{ GDScriptDecomp::G_TK_PR_SIGNAL, "signal" },
	{ GDScriptDecomp::G_TK_PR_BREAKPOINT, "breakpoint" },
	{ GDScriptDecomp::G_TK_PR_REMOTE, "remote" },
	{ GDScriptDecomp::G_TK_PR_MASTER, "master" },
	{ GDScriptDecomp::G_TK_PR_SLAVE, "slave" },
	{ GDScriptDecomp::G_TK_PR_PUPPET, "puppet" },
	{ GDScriptDecomp::G_TK_PR_SYNC, "sync" },
	{ GDScriptDecomp::G_TK_PR_REMOTESYNC, "remotesync" },
	{ GDScriptDecomp::G_TK_PR_MASTERSYNC, "mastersync" },
	{ GDScriptDecomp::G_TK_PR_PUPPETSYNC, "puppetsync" },
	{ GDScriptDecomp::G_TK_PR_CONST, "const" },
	{ GDScriptDecomp::G_TK_PR_ENUM, "enum" },
	//controlflow
	{ GDScriptDecomp::G_TK_CF_IF, "if" },
	{ GDScriptDecomp::G_TK_CF_ELIF, "elif" },
	{ GDScriptDecomp::G_TK_CF_ELSE, "else" },
	{ GDScriptDecomp::G_TK_CF_FOR, "for" },
	{ GDScriptDecomp::G_TK_CF_WHILE, "while" },
	{ GDScriptDecomp::G_TK_CF_BREAK, "break" },
	{ GDScriptDecomp::G_TK_CF_CONTINUE, "continue" },
	{ GDScriptDecomp::G_TK_CF_RETURN, "return" },
	{ GDScriptDecomp::G_TK_CF_MATCH, "match" },
	{ GDScriptDecomp::G_TK_CF_PASS, "pass" },
	{ GDScriptDecomp::G_TK_SELF, "self" },
	{ GDScriptDecomp::G_TK_CONST_PI, "PI" },
	{ GDScriptDecomp::G_TK_CONST_TAU, "TAU" },
	{ GDScriptDecomp::G_TK_WILDCARD, "_" },
	{ GDScriptDecomp::G_TK_CONST_INF, "INF" },
	{ GDScriptDecomp::G_TK_CONST_NAN, "NAN" },

	// Removed tokens
	{ GDScriptDecomp::G_TK_PR_SLAVESYNC, "slavesync" },
	{ GDScriptDecomp::G_TK_CF_DO, "do" },
	{ GDScriptDecomp::G_TK_CF_CASE, "case" },
	{ GDScriptDecomp::G_TK_CF_SWITCH, "switch" },

	// 4.3 keywords
	{ GDScriptDecomp::G_TK_CF_WHEN, "when" },
	{ GDScriptDecomp::G_TK_PR_AWAIT, "await" },
	{ GDScriptDecomp::G_TK_PR_NAMESPACE, "namespace" },
	{ GDScriptDecomp::G_TK_PR_SUPER, "super" },
	{ GDScriptDecomp::G_TK_PR_TRAIT, "trait" },

	{ GDScriptDecomp::G_TK_ERROR, nullptr }
};

GDScriptTokenizerV1Compat::GDScriptTokenizerV1Compat(const GDScriptDecomp *p_decomp) :
		decomp(p_decomp) {
}
/* to delete

bool GDScriptTokenizerTextCompat::is_token_literal(int p_offset, bool variable_safe) const {
	switch (get_token(p_offset)) {
		// Can always be literal:
		case T::G_TK_IDENTIFIER:

		case T::G_TK_PR_ONREADY:
		case T::G_TK_PR_TOOL:
		case T::G_TK_PR_STATIC:
		case T::G_TK_PR_EXPORT:
		case T::G_TK_PR_SETGET:
		case T::G_TK_PR_SIGNAL:
		case T::G_TK_PR_REMOTE:
		case T::G_TK_PR_MASTER:
		case T::G_TK_PR_PUPPET:
		case T::G_TK_PR_SYNC:
		case T::G_TK_PR_REMOTESYNC:
		case T::G_TK_PR_MASTERSYNC:
		case T::G_TK_PR_PUPPETSYNC:
			return true;

		// Literal for non-variables only:
		case T::G_TK_BUILT_IN_TYPE:
		case T::G_TK_BUILT_IN_FUNC:

		case T::G_TK_OP_IN:
			//case T::G_TK_OP_NOT:
			//case T::G_TK_OP_OR:
			//case T::G_TK_OP_AND:

		case T::G_TK_PR_CLASS:
		case T::G_TK_PR_CONST:
		case T::G_TK_PR_ENUM:
		case T::G_TK_PR_PRELOAD:
		case T::G_TK_PR_FUNCTION:
		case T::G_TK_PR_EXTENDS:
		case T::G_TK_PR_ASSERT:
		case T::G_TK_PR_YIELD:
		case T::G_TK_PR_VAR:

		case T::G_TK_CF_IF:
		case T::G_TK_CF_ELIF:
		case T::G_TK_CF_ELSE:
		case T::G_TK_CF_FOR:
		case T::G_TK_CF_WHILE:
		case T::G_TK_CF_BREAK:
		case T::G_TK_CF_CONTINUE:
		case T::G_TK_CF_RETURN:
		case T::G_TK_CF_MATCH:
		case T::G_TK_CF_PASS:
		case T::G_TK_SELF:
		case T::G_TK_CONST_PI:
		case T::G_TK_CONST_TAU:
		case T::G_TK_WILDCARD:
		case T::G_TK_CONST_INF:
		case T::G_TK_CONST_NAN:
		case T::G_TK_ERROR:
			return !variable_safe;

		case T::G_TK_CONSTANT: {
			switch (get_token_constant(p_offset).get_type()) {
				case Variant::NIL:
				case Variant::BOOL:
					return true;
				default:
					return false;
			}
		}
		default:
			return false;
	}
}

StringName GDScriptTokenizerTextCompat::get_token_literal(int p_offset) const {
	Token token = get_token(p_offset);
	switch (token) {
		case T::G_TK_IDENTIFIER:
			return get_token_identifier(p_offset);
		case T::G_TK_BUILT_IN_TYPE: {
			Variant::Type type = get_token_type(p_offset);
			int converted_type = VariantDecoderCompat::convert_variant_type_to_old(type, decomp->get_variant_ver_major());
			int idx = 0;
			String ret = VariantDecoderCompat::get_variant_type_name(converted_type, decomp->get_variant_ver_major());
			if (!ret.is_empty()) {
				return ret;
			}
		} break; // Shouldn't get here, stuff happens
		case T::G_TK_BUILT_IN_FUNC:
			return GDScriptFunctions::get_func_name(get_token_built_in_func(p_offset));
		case T::G_TK_CONSTANT: {
			const Variant value = get_token_constant(p_offset);

			switch (value.get_type()) {
				case Variant::NIL:
					return "null";
				case Variant::BOOL:
					return value ? "true" : "false";
				default: {
				}
			}
		}
		case T::G_TK_OP_AND:
		case T::G_TK_OP_OR:
			break; // Don't get into default, since they can be non-literal
		default: {
			int idx = 0;

			while (_keyword_list[idx].text) {
				if (token == _keyword_list[idx].token) {
					return _keyword_list[idx].text;
				}
				idx++;
			}
		}
	}
	ERR_FAIL_V_MSG("", "Failed to get token literal.");
}
*/

static bool _is_text_char(CharType c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool _is_number(CharType c) {
	return (c >= '0' && c <= '9');
}

static bool _is_hex(CharType c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool _is_bin(CharType c) {
	return (c == '0' || c == '1');
}

void GDScriptV1TokenizerTextCompat::_make_token(TokenType p_type) {
	Token &tk = tk_rb[tk_rb_pos];

	tk.type = p_type;
	tk.start_line = line;
	tk.start_column = column;

	tk_rb_pos = (tk_rb_pos + 1) % TK_RB_SIZE;
}
void GDScriptV1TokenizerTextCompat::_make_identifier(const StringName &p_identifier) {
	Token &tk = tk_rb[tk_rb_pos];

	tk.type = T::G_TK_IDENTIFIER;
	tk.literal = p_identifier;
	tk.start_line = line;
	tk.start_column = column;

	tk_rb_pos = (tk_rb_pos + 1) % TK_RB_SIZE;
}

void GDScriptV1TokenizerTextCompat::_make_built_in_func(int p_func) {
	Token &tk = tk_rb[tk_rb_pos];

	tk.type = T::G_TK_BUILT_IN_FUNC;
	tk.func = p_func;
	tk.literal = decomp->get_function_name(p_func);
	tk.start_line = line;
	tk.start_column = column;

	tk_rb_pos = (tk_rb_pos + 1) % TK_RB_SIZE;
}
void GDScriptV1TokenizerTextCompat::_make_constant(const Variant &p_constant) {
	Token &tk = tk_rb[tk_rb_pos];

	tk.type = T::G_TK_CONSTANT;
	tk.literal = p_constant;
	tk.start_line = line;
	tk.start_column = column;

	tk_rb_pos = (tk_rb_pos + 1) % TK_RB_SIZE;
}

void GDScriptV1TokenizerTextCompat::_make_type(const Variant::Type &p_type) {
	Token &tk = tk_rb[tk_rb_pos];

	tk.type = T::G_TK_BUILT_IN_TYPE;
	tk.vtype = p_type;
	tk.start_line = line;
	tk.start_column = column;

	tk_rb_pos = (tk_rb_pos + 1) % TK_RB_SIZE;
}

void GDScriptV1TokenizerTextCompat::_make_error(const String &p_error) {
	error_flag = true;
	last_error = p_error;

	Token &tk = tk_rb[tk_rb_pos];
	tk.type = T::G_TK_ERROR;
	tk.literal = p_error;
	tk.start_line = line;
	tk.start_column = column;
	tk_rb_pos = (tk_rb_pos + 1) % TK_RB_SIZE;
}

void GDScriptV1TokenizerTextCompat::_make_newline(int p_indentation, int p_tabs) {
	Token &tk = tk_rb[tk_rb_pos];
	tk.type = T::G_TK_NEWLINE;
	tk.literal = Vector2(p_indentation, p_tabs);
	tk.start_line = line;
	tk.start_column = column;
	tk_rb_pos = (tk_rb_pos + 1) % TK_RB_SIZE;
}

void GDScriptV1TokenizerTextCompat::_advance() {
	if (error_flag) {
		//parser broke
		_make_error(last_error);
		return;
	}

	if (code_pos >= len) {
		_make_token(T::G_TK_EOF);
		return;
	}
#define GETCHAR(m_ofs) ((m_ofs + code_pos) >= len ? 0 : _code[m_ofs + code_pos])
#define INCPOS(m_amount)      \
	{                         \
		code_pos += m_amount; \
		column += m_amount;   \
	}
	while (true) {
		bool is_node_path = false;
		StringMode string_mode = STRING_DOUBLE_QUOTE;

		switch (GETCHAR(0)) {
			case 0:
				_make_token(T::G_TK_EOF);
				break;
			case '\\':
				INCPOS(1);
				if (GETCHAR(0) == '\r') {
					INCPOS(1);
				}

				if (GETCHAR(0) != '\n') {
					_make_error("Expected newline after '\\'.");
					return;
				}

				INCPOS(1);
				line++;

				while (GETCHAR(0) == ' ' || GETCHAR(0) == '\t') {
					INCPOS(1);
				}

				continue;
			case '\t':
			case '\r':
			case ' ':
				INCPOS(1);
				continue;
			case '#': { // line comment skip
#ifdef DEBUG_ENABLED
				String comment;
#endif // DEBUG_ENABLED
				while (GETCHAR(0) != '\n') {
#ifdef DEBUG_ENABLED
					comment += GETCHAR(0);
#endif // DEBUG_ENABLED
					code_pos++;
					if (GETCHAR(0) == 0) { //end of file
						//_make_error("Unterminated Comment");
						_make_token(T::G_TK_EOF);
						return;
					}
				}
#ifdef DEBUG_ENABLED
				String comment_content = comment.trim_prefix("#").trim_prefix(" ");
				if (comment_content.begins_with("warning-ignore:")) {
					String warning_code = comment_content.get_slice(":", 1);
					warning_skips.push_back(Pair<int, String>(line, warning_code.strip_edges().to_lower()));
				} else if (comment_content.begins_with("warning-ignore-all:")) {
					String warning_code = comment_content.get_slice(":", 1);
					warning_global_skips.insert(warning_code.strip_edges().to_lower());
				} else if (comment_content.strip_edges() == "warnings-disable") {
					ignore_warnings = true;
				}
#endif // DEBUG_ENABLED
				[[fallthrough]];
			}
			case '\n': {
				line++;
				INCPOS(1);
				bool used_spaces = false;
				int tabs = 0;
				column = 1;
				int i = 0;
				while (true) {
					if (GETCHAR(i) == ' ') {
						i++;
						used_spaces = true;
					} else if (GETCHAR(i) == '\t') {
						// compat
						if (used_spaces && compat_no_mixed_spaces) {
							_make_error("Spaces used before tabs on a line");
							return;
						}
						i++;
						tabs++;
					} else {
						break; // not indentation anymore
					}
				}

				_make_newline(i, tabs);
				return;
			}
			case '/': {
				switch (GETCHAR(1)) {
					case '=': { // diveq

						_make_token(T::G_TK_OP_ASSIGN_DIV);
						INCPOS(1);

					} break;
					default:
						_make_token(T::G_TK_OP_DIV);
				}
			} break;
			case '=': {
				if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_EQUAL);
					INCPOS(1);

				} else {
					_make_token(T::G_TK_OP_ASSIGN);
				}

			} break;
			case '<': {
				if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_LESS_EQUAL);
					INCPOS(1);
				} else if (GETCHAR(1) == '<') {
					if (GETCHAR(2) == '=') {
						_make_token(T::G_TK_OP_ASSIGN_SHIFT_LEFT);
						INCPOS(1);
					} else {
						_make_token(T::G_TK_OP_SHIFT_LEFT);
					}
					INCPOS(1);
				} else {
					_make_token(T::G_TK_OP_LESS);
				}

			} break;
			case '>': {
				if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_GREATER_EQUAL);
					INCPOS(1);
				} else if (GETCHAR(1) == '>') {
					if (GETCHAR(2) == '=') {
						_make_token(T::G_TK_OP_ASSIGN_SHIFT_RIGHT);
						INCPOS(1);

					} else {
						_make_token(T::G_TK_OP_SHIFT_RIGHT);
					}
					INCPOS(1);
				} else {
					_make_token(T::G_TK_OP_GREATER);
				}

			} break;
			case '!': {
				if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_NOT_EQUAL);
					INCPOS(1);
				} else {
					_make_token(T::G_TK_OP_NOT);
				}

			} break;
			//case '"' //string - no strings in shader
			//case '\'' //string - no strings in shader
			case '{':
				_make_token(T::G_TK_CURLY_BRACKET_OPEN);
				break;
			case '}':
				_make_token(T::G_TK_CURLY_BRACKET_CLOSE);
				break;
			case '[':
				_make_token(T::G_TK_BRACKET_OPEN);
				break;
			case ']':
				_make_token(T::G_TK_BRACKET_CLOSE);
				break;
			case '(':
				_make_token(T::G_TK_PARENTHESIS_OPEN);
				break;
			case ')':
				_make_token(T::G_TK_PARENTHESIS_CLOSE);
				break;
			case ',':
				_make_token(T::G_TK_COMMA);
				break;
			case ';':
				_make_token(T::G_TK_SEMICOLON);
				break;
			case '?':
				_make_token(T::G_TK_QUESTION_MARK);
				break;
			case ':':
				_make_token(T::G_TK_COLON); //for methods maybe but now useless.
				break;
			// compat, handled below
			// case '$':
			// 	_make_token(TK_DOLLAR); //for the get_node() shortener
			// 	break;
			case '^': {
				if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_ASSIGN_BIT_XOR);
					INCPOS(1);
				} else {
					_make_token(T::G_TK_OP_BIT_XOR);
				}

			} break;
			case '~':
				_make_token(T::G_TK_OP_BIT_INVERT);
				break;
			case '&': {
				if (GETCHAR(1) == '&') {
					if (compat_gdscript_2_0) {
						_make_token(T::G_TK_AMPERSAND_AMPERSAND);
					} else {
						_make_token(T::G_TK_OP_AND);
					}
					INCPOS(1);
				} else if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_ASSIGN_BIT_AND);
					INCPOS(1);
				} else {
					_make_token(T::G_TK_OP_BIT_AND);
				}
			} break;
			case '|': {
				if (GETCHAR(1) == '|') {
					if (compat_gdscript_2_0) {
						_make_token(T::G_TK_PIPE_PIPE);
					} else {
						_make_token(T::G_TK_OP_OR);
					}
					INCPOS(1);
				} else if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_ASSIGN_BIT_OR);
					INCPOS(1);
				} else {
					_make_token(T::G_TK_OP_BIT_OR);
				}
			} break;
			case '*': {
				if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_ASSIGN_MUL);
					INCPOS(1);
				} else {
					_make_token(T::G_TK_OP_MUL);
				}
			} break;
			case '+': {
				if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_ASSIGN_ADD);
					INCPOS(1);
					/*
				}  else if (GETCHAR(1)=='+') {
					_make_token(T::G_TK_OP_PLUS_PLUS);
					INCPOS(1);
				*/
				} else {
					_make_token(T::G_TK_OP_ADD);
				}

			} break;
			case '-': {
				if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_ASSIGN_SUB);
					INCPOS(1);
				} else if (GETCHAR(1) == '>') {
					_make_token(T::G_TK_FORWARD_ARROW);
					INCPOS(1);
				} else {
					_make_token(T::G_TK_OP_SUB);
				}
			} break;
			case '%': {
				if (GETCHAR(1) == '=') {
					_make_token(T::G_TK_OP_ASSIGN_MOD);
					INCPOS(1);
				} else {
					_make_token(T::G_TK_OP_MOD);
				}
			} break;
			case '@':
				if (CharType(GETCHAR(1)) != '"' && CharType(GETCHAR(1)) != '\'') {
					_make_error("Unexpected '@'");
					return;
				}
				INCPOS(1);
				is_node_path = true;
				[[fallthrough]];
			case '\'':
			case '"': {
				if (GETCHAR(0) == '\'') {
					string_mode = STRING_SINGLE_QUOTE;
				}

				int i = 1;
				if (string_mode == STRING_DOUBLE_QUOTE && GETCHAR(i) == '"' && GETCHAR(i + 1) == '"') {
					i += 2;
					string_mode = STRING_MULTILINE;
				}

				String str;
				while (true) {
					if (CharType(GETCHAR(i)) == 0) {
						_make_error("Unterminated String");
						return;
					} else if (string_mode == STRING_DOUBLE_QUOTE && CharType(GETCHAR(i)) == '"') {
						break;
					} else if (string_mode == STRING_SINGLE_QUOTE && CharType(GETCHAR(i)) == '\'') {
						break;
					} else if (string_mode == STRING_MULTILINE && CharType(GETCHAR(i)) == '\"' && CharType(GETCHAR(i + 1)) == '\"' && CharType(GETCHAR(i + 2)) == '\"') {
						i += 2;
						break;
					} else if (string_mode != STRING_MULTILINE && CharType(GETCHAR(i)) == '\n') {
						_make_error("Unexpected EOL at String.");
						return;
					} else if (CharType(GETCHAR(i)) == 0xFFFF) {
						//string ends here, next will be TK
						i--;
						break;
					} else if (CharType(GETCHAR(i)) == '\\') {
						//escaped characters...
						i++;
						CharType next = GETCHAR(i);
						if (next == 0) {
							_make_error("Unterminated String");
							return;
						}
						CharType res = 0;

						switch (next) {
							case 'a':
								res = 7;
								break;
							case 'b':
								res = 8;
								break;
							case 't':
								res = 9;
								break;
							case 'n':
								res = 10;
								break;
							case 'v':
								res = 11;
								break;
							case 'f':
								res = 12;
								break;
							case 'r':
								res = 13;
								break;
							case '\'':
								res = '\'';
								break;
							case '\"':
								res = '\"';
								break;
							case '\\':
								res = '\\';
								break;
							case '/':
								res = '/';
								break; //wtf

							case 'u': {
								//hexnumbarh - oct is deprecated
								i += 1;
								for (int j = 0; j < 4; j++) {
									CharType c = GETCHAR(i + j);
									if (c == 0) {
										_make_error("Unterminated String");
										return;
									}

									CharType v = 0;
									if (c >= '0' && c <= '9') {
										v = c - '0';
									} else if (c >= 'a' && c <= 'f') {
										v = c - 'a';
										v += 10;
									} else if (c >= 'A' && c <= 'F') {
										v = c - 'A';
										v += 10;
									} else {
										_make_error("Malformed hex constant in string");
										return;
									}

									res <<= 4;
									res |= v;
								}
								i += 3;

							} break;
							default: {
								_make_error("Invalid escape sequence");
								return;
							} break;
						}

						str += res;

					} else {
						// compat -- only introduced in 2.0.0-dev2, but it only affects debugging
						// Only checking for it so that we can repoduce scripts perfectly
						if (compat_newline_after_string_debug_fix && CharType(GETCHAR(i)) == '\n') {
							line++;
							column = 1;
						}

						str += CharType(GETCHAR(i));
					}
					i++;
				}
				INCPOS(i);

				if (is_node_path) {
					_make_constant(NodePath(str));
				} else {
					_make_constant(str);
				}

			} break;
			// compat - only used for code completion, ignore it
			// case 0xFFFF: {
			// 	_make_token(T::G_TK_CURSOR);
			// } break;
			case '$':
				// compat - added in 3.0-dev5
				if (decomp->get_local_token_val(T::G_TK_DOLLAR) != -1) {
					_make_token(T::G_TK_DOLLAR);
					break;
				}
				// otherwise, fall through
				[[fallthrough]];
			default: {
				if (_is_number(GETCHAR(0)) || (GETCHAR(0) == '.' && _is_number(GETCHAR(1)))) {
					// parse number
					bool period_found = false;
					bool exponent_found = false;
					bool hexa_found = false;
					bool bin_found = false; // compat - this should always remain false if we're not supporting bin constants
					bool sign_found = false;

					String str;
					int i = 0;

					while (true) {
						if (GETCHAR(i) == '.') {
							if (period_found || exponent_found) {
								_make_error("Invalid numeric constant at '.'");
								return;
							}
							if (compat_bin_consts) { // compat
								if (bin_found) {
									_make_error("Invalid binary constant at '.'");
									return;
								} else if (hexa_found) {
									_make_error("Invalid hexadecimal constant at '.'");
									return;
								}
							}
							period_found = true;
						} else if (GETCHAR(i) == 'x') {
							if (hexa_found || bin_found || str.length() != 1 || !((i == 1 && str[0] == '0') || (i == 2 && str[1] == '0' && str[0] == '-'))) {
								_make_error("Invalid numeric constant at 'x'");
								return;
							}
							hexa_found = true;
							// this fix was introduced in 4b9fd96 (which ended up in 3.2-stable), but it should only affect anything if we're supporting bin constants, which was introduced before this fix.
						} else if (hexa_found && _is_hex(GETCHAR(i))) {
						} else if (!hexa_found && GETCHAR(i) == 'b' && compat_bin_consts) { // compat
							if (bin_found || str.length() != 1 || !((i == 1 && str[0] == '0') || (i == 2 && str[1] == '0' && str[0] == '-'))) {
								_make_error("Invalid numeric constant at 'b'");
								return;
							}
							bin_found = true;
						} else if (!hexa_found && GETCHAR(i) == 'e') {
							if (exponent_found || bin_found) {
								_make_error("Invalid numeric constant at 'e'");
								return;
							}
							exponent_found = true;
						} else if (_is_number(GETCHAR(i))) {
							//all ok

						} else if (bin_found && _is_bin(GETCHAR(i))) {
						} else if ((GETCHAR(i) == '-' || GETCHAR(i) == '+') && exponent_found) {
							if (sign_found) {
								_make_error("Invalid numeric constant at '-'");
								return;
							}
							sign_found = true;
						} else if (GETCHAR(i) == '_' && compat_underscore_num_consts) { // compat
							i++;
							continue; // Included for readability, shouldn't be a part of the string
						} else {
							break;
						}

						str += CharType(GETCHAR(i));
						i++;
					}

					if (!(_is_number(str[str.length() - 1]) || (hexa_found && _is_hex(str[str.length() - 1])))) {
						_make_error("Invalid numeric constant: " + str);
						return;
					}

					INCPOS(i);
					if (hexa_found) {
						int64_t val = str.hex_to_int();
						_make_constant(val);
					} else if (bin_found) {
						int64_t val = str.bin_to_int();
						_make_constant(val);
					} else if (period_found || exponent_found) {
						double val = str.to_float();
						_make_constant(val);
					} else {
						int64_t val = str.to_int();
						_make_constant(val);
					}

					return;
				}

				if (GETCHAR(0) == '.') {
					if (compat_gdscript_2_0 && GETCHAR(1) == '.') {
						_make_token(T::G_TK_PERIOD_PERIOD);
						INCPOS(1);
					} else {
						_make_token(T::G_TK_PERIOD);
					}
					break;
				}

				if (_is_text_char(GETCHAR(0))) {
					// parse identifier
					String str;
					str += CharType(GETCHAR(0));

					int i = 1;
					while (_is_text_char(GETCHAR(i))) {
						str += CharType(GETCHAR(i));
						i++;
					}

					bool identifier = false;

					if (str == "null") {
						_make_constant(Variant());

					} else if (str == "true") {
						_make_constant(true);

					} else if (str == "false") {
						_make_constant(false);
					} else {
						bool found = false;

						{
							// compat
							int idx = VariantDecoderCompat::get_variant_type(str, decomp->get_variant_ver_major());
							if (idx != -1) {
								_make_type(VariantDecoderCompat::convert_variant_type_from_old(idx, decomp->get_variant_ver_major()));
								found = true;
							}
						}

						if (!found) {
							//built in func?
							// compat
							int idx = decomp->get_function_index(str);
							if (idx != -1) {
								_make_built_in_func(idx);
								found = true;
							}
						}

						if (!found) {
							//keyword

							int idx = 0;
							found = false;

							while (_keyword_list[idx].text) {
								// compat
								if (decomp->get_local_token_val(_keyword_list[idx].token) == -1) {
									idx++;
									continue;
								}
								if (str == _keyword_list[idx].text) {
									_make_token(_keyword_list[idx].token);
									found = true;
									break;
								}
								idx++;
							}
						}

						if (!found) {
							identifier = true;
						}
					}

					if (identifier) {
						_make_identifier(str);
					}
					INCPOS(str.length());
					return;
				}

				_make_error("Unknown character");
				return;

			} break;
		}

		INCPOS(1);
		break;
	}
}

void GDScriptV1TokenizerTextCompat::set_code(const String &p_code) {
	current_indent = 0;
	code = p_code;
	len = p_code.length();
	if (len) {
		_code = &code[0];
	} else {
		_code = nullptr;
	}
	code_pos = 0;
	line = 1; //it is stand-ar-ized that lines begin in 1 in code..
	column = 1; //the same holds for columns
	tk_rb_pos = 0;
	error_flag = false;
#ifdef DEBUG_ENABLED
	ignore_warnings = false;
#endif // DEBUG_ENABLED
	last_error = "";
	for (int i = 0; i < MAX_LOOKAHEAD + 1; i++) {
		_advance();
	}
}

GDScriptDecomp::GlobalToken GDScriptV1TokenizerTextCompat::get_token(int p_offset) const {
	ERR_FAIL_COND_V(p_offset <= -MAX_LOOKAHEAD, T::G_TK_ERROR);
	ERR_FAIL_COND_V(p_offset >= MAX_LOOKAHEAD, T::G_TK_ERROR);

	int ofs = (TK_RB_SIZE + tk_rb_pos + p_offset - MAX_LOOKAHEAD - 1) % TK_RB_SIZE;
	return tk_rb[ofs].type;
}

int GDScriptV1TokenizerTextCompat::get_token_line(int p_offset) const {
	ERR_FAIL_COND_V(p_offset <= -MAX_LOOKAHEAD, -1);
	ERR_FAIL_COND_V(p_offset >= MAX_LOOKAHEAD, -1);

	int ofs = (TK_RB_SIZE + tk_rb_pos + p_offset - MAX_LOOKAHEAD - 1) % TK_RB_SIZE;
	return tk_rb[ofs].start_line;
}

int GDScriptV1TokenizerTextCompat::get_token_column(int p_offset) const {
	ERR_FAIL_COND_V(p_offset <= -MAX_LOOKAHEAD, -1);
	ERR_FAIL_COND_V(p_offset >= MAX_LOOKAHEAD, -1);

	int ofs = (TK_RB_SIZE + tk_rb_pos + p_offset - MAX_LOOKAHEAD - 1) % TK_RB_SIZE;
	return tk_rb[ofs].start_column;
}

const Variant &GDScriptV1TokenizerTextCompat::get_token_constant(int p_offset) const {
	ERR_FAIL_COND_V(p_offset <= -MAX_LOOKAHEAD, tk_rb[0].literal);
	ERR_FAIL_COND_V(p_offset >= MAX_LOOKAHEAD, tk_rb[0].literal);

	int ofs = (TK_RB_SIZE + tk_rb_pos + p_offset - MAX_LOOKAHEAD - 1) % TK_RB_SIZE;
	ERR_FAIL_COND_V(tk_rb[ofs].type != T::G_TK_CONSTANT, tk_rb[0].literal);
	return tk_rb[ofs].literal;
}

StringName GDScriptV1TokenizerTextCompat::get_token_identifier(int p_offset) const {
	ERR_FAIL_COND_V(p_offset <= -MAX_LOOKAHEAD, StringName());
	ERR_FAIL_COND_V(p_offset >= MAX_LOOKAHEAD, StringName());

	int ofs = (TK_RB_SIZE + tk_rb_pos + p_offset - MAX_LOOKAHEAD - 1) % TK_RB_SIZE;
	ERR_FAIL_COND_V(tk_rb[ofs].type != T::G_TK_IDENTIFIER, StringName());
	return tk_rb[ofs].literal;
}

int GDScriptV1TokenizerTextCompat::get_token_built_in_func(int p_offset) const {
	ERR_FAIL_COND_V(p_offset <= -MAX_LOOKAHEAD, decomp->get_function_count());
	ERR_FAIL_COND_V(p_offset >= MAX_LOOKAHEAD, decomp->get_function_count());

	int ofs = (TK_RB_SIZE + tk_rb_pos + p_offset - MAX_LOOKAHEAD - 1) % TK_RB_SIZE;
	ERR_FAIL_COND_V(tk_rb[ofs].type != T::G_TK_BUILT_IN_FUNC, decomp->get_function_count());
	return tk_rb[ofs].func;
}

Variant::Type GDScriptV1TokenizerTextCompat::get_token_type(int p_offset) const {
	ERR_FAIL_COND_V(p_offset <= -MAX_LOOKAHEAD, Variant::NIL);
	ERR_FAIL_COND_V(p_offset >= MAX_LOOKAHEAD, Variant::NIL);

	int ofs = (TK_RB_SIZE + tk_rb_pos + p_offset - MAX_LOOKAHEAD - 1) % TK_RB_SIZE;
	ERR_FAIL_COND_V(tk_rb[ofs].type != T::G_TK_BUILT_IN_TYPE, Variant::NIL);
	return tk_rb[ofs].vtype;
}

int GDScriptV1TokenizerTextCompat::get_token_line_indent(int p_offset) const {
	ERR_FAIL_COND_V(p_offset <= -MAX_LOOKAHEAD, 0);
	ERR_FAIL_COND_V(p_offset >= MAX_LOOKAHEAD, 0);

	int ofs = (TK_RB_SIZE + tk_rb_pos + p_offset - MAX_LOOKAHEAD - 1) % TK_RB_SIZE;
	ERR_FAIL_COND_V(tk_rb[ofs].type != T::G_TK_NEWLINE, 0);
	return tk_rb[ofs].literal.operator Vector2().x;
}

int GDScriptV1TokenizerTextCompat::get_token_line_tab_indent(int p_offset) const {
	ERR_FAIL_COND_V(p_offset <= -MAX_LOOKAHEAD, 0);
	ERR_FAIL_COND_V(p_offset >= MAX_LOOKAHEAD, 0);

	int ofs = (TK_RB_SIZE + tk_rb_pos + p_offset - MAX_LOOKAHEAD - 1) % TK_RB_SIZE;
	ERR_FAIL_COND_V(tk_rb[ofs].type != T::G_TK_NEWLINE, 0);
	return tk_rb[ofs].literal.operator Vector2().y;
}

String GDScriptV1TokenizerTextCompat::get_token_error(int p_offset) const {
	ERR_FAIL_COND_V(p_offset <= -MAX_LOOKAHEAD, String());
	ERR_FAIL_COND_V(p_offset >= MAX_LOOKAHEAD, String());

	int ofs = (TK_RB_SIZE + tk_rb_pos + p_offset - MAX_LOOKAHEAD - 1) % TK_RB_SIZE;
	ERR_FAIL_COND_V(tk_rb[ofs].type != T::G_TK_ERROR, String());
	return tk_rb[ofs].literal;
}

void GDScriptV1TokenizerTextCompat::advance(int p_amount) {
	ERR_FAIL_COND(p_amount <= 0);
	for (int i = 0; i < p_amount; i++) {
		_advance();
	}
}

GDScriptV1TokenizerTextCompat::GDScriptV1TokenizerTextCompat(const GDScriptDecomp *p_decomp) :
		GDScriptTokenizerV1Compat(p_decomp), engine_ver(GodotVer::parse(p_decomp->get_engine_version())) {
	const Ref<GodotVer> NEWLINE_AFTER_STRING_DEBUG_FIX_VER = GodotVer::create(2, 0, 0, "dev2"); // 2.0.0-dev2, (actual commit: 8280bb0, Aug-4-2015)
	const Ref<GodotVer> BIN_CONSTS_VER = GodotVer::create(3, 2, 0, "dev1"); // 3.2.0-dev1, (actual commit: d3cc9c0, Apr-25-2019)
	const Ref<GodotVer> NO_MIXED_SPACES_VER = GodotVer::create(3, 2, 0); // 3.2.0-stable (actual commit: 4b9fd96, Nov-12-2019)
	const Ref<GodotVer> UNDERSCORE_NUM_CONSTS_VER = GodotVer::create(3, 0, 0); // 3.0.0-stable (actual commit: 443ce6f, November 15th, 2017)

	compat_newline_after_string_debug_fix = engine_ver->gte(NEWLINE_AFTER_STRING_DEBUG_FIX_VER);
	compat_bin_consts = engine_ver->gte(BIN_CONSTS_VER);
	compat_no_mixed_spaces = engine_ver->gte(NO_MIXED_SPACES_VER);
	compat_underscore_num_consts = engine_ver->gte(UNDERSCORE_NUM_CONSTS_VER);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////
#include "core/io/marshalls.h"

#define BYTECODE_VERSION 13

#define GDSDECOMP_FAIL_V_MSG(m_retval, m_msg) \
	error_message = RTR(m_msg);               \
	ERR_FAIL_V_MSG(m_retval, m_msg);

#define GDSDECOMP_FAIL_COND_V_MSG(m_cond, m_retval, m_msg) \
	if (unlikely(m_cond)) {                                \
		error_message = RTR(m_msg);                        \
		ERR_FAIL_V_MSG(m_retval, m_msg);                   \
	}
#define GDSC_HEADER "GDSC"
#define CHECK_GDSC_HEADER(p_buffer) _GDRE_CHECK_HEADER(p_buffer, GDSC_HEADER)
#include "utility/common.h"

Error GDScriptV1TokenizerBufferCompat::set_code_buffer(const Vector<uint8_t> &p_buffer) {
	const uint8_t *buf = p_buffer.ptr();
	uint64_t total_len = p_buffer.size();
	GDSDECOMP_FAIL_COND_V_MSG(p_buffer.size() < 24 || !CHECK_GDSC_HEADER(p_buffer), ERR_INVALID_DATA, "Invalid GDScript token buffer.");
	int version = decode_uint32(&buf[4]);
	ERR_FAIL_COND_V_MSG(version >= GDScriptDecomp::GDSCRIPT_2_0_VERSION, ERR_INVALID_DATA, "Wrong function!");
	const int contents_start = 8 + (version >= GDScriptDecomp::GDSCRIPT_2_0_VERSION ? 4 : 0);
	uint32_t identifier_count = decode_uint32(&buf[contents_start]);
	uint32_t constant_count = decode_uint32(&buf[contents_start + 4]);
	uint32_t line_count = decode_uint32(&buf[contents_start + 8]);
	uint32_t token_count = decode_uint32(&buf[contents_start + 12]);

	const uint8_t *b = &buf[24];
	total_len -= 24;

	identifiers.resize(identifier_count);
	for (uint32_t i = 0; i < identifier_count; i++) {
		uint32_t len = decode_uint32(b);
		GDSDECOMP_FAIL_COND_V_MSG(len > total_len, ERR_INVALID_DATA, "Invalid identifier length.");
		b += 4;
		Vector<uint8_t> cs;
		cs.resize(len);
		for (uint32_t j = 0; j < len; j++) {
			cs.write[j] = b[j] ^ 0xb6;
		}

		cs.write[cs.size() - 1] = 0;
		String s;
		s.append_utf8((const char *)cs.ptr());
		b += len;
		total_len -= len + 4;
		identifiers.write[i] = s;
	}

	constants.resize(constant_count);
	for (uint32_t i = 0; i < constant_count; i++) {
		Variant v;
		int len;
		Error err = VariantDecoderCompat::decode_variant_compat(decomp->get_variant_ver_major(), v, b, total_len, &len);
		if (err) {
			error_message = RTR("Invalid constant");
			return err;
		}
		b += len;
		total_len -= len;
		constants.write[i] = v;
	}

	GDSDECOMP_FAIL_COND_V_MSG(line_count * /*sizeof(HashMap<uint32_t, uint32_t>::Pair)*/ 8 > total_len, ERR_INVALID_DATA, "Invalid line count.");

	for (uint32_t i = 0; i < line_count; i++) {
		uint32_t token_index = decode_uint32(b);
		b += 4;
		uint32_t linecol = decode_uint32(b);
		b += 4;

		lines.insert(token_index, linecol);
		total_len -= 8;
	}
	tokens.resize(token_count);
	for (uint32_t i = 0; i < token_count; i++) {
		GDSDECOMP_FAIL_COND_V_MSG(total_len < 1, ERR_INVALID_DATA, "Invalid token length.");

		if ((*b) & TOKEN_BYTE_MASK) { //BYTECODE_MASK, little endian always
			GDSDECOMP_FAIL_COND_V_MSG(total_len < 4, ERR_INVALID_DATA, "Invalid token length.");

			tokens.write[i] = decode_uint32(b) & ~TOKEN_BYTE_MASK;
			b += 4;
		} else {
			tokens.write[i] = *b;
			b += 1;
			total_len--;
		}
	}

	token = 0;
	current_indent = 0;
	return OK;
}

Vector<uint8_t> GDScriptV1TokenizerBufferCompat::parse_code_string(const String &p_code, const GDScriptDecomp *p_decomp, String &error_message) {
	Vector<uint8_t> buf;

	RBMap<StringName, int> identifier_map;
	HashMap<Variant, int> constant_map;
	RBMap<uint32_t, int> line_map;
	Vector<uint32_t> token_array;

	int variant_ver_major = p_decomp->get_variant_ver_major();

	// compat: from 3.0 - 3.1.1, the tokenizer defaulted to storing full objects
	// e61a074, Mar 28, 2019
	Ref<GodotVer> NO_FULL_OBJ_VER = GodotVer::create(3, 2, 0, "dev1");
	Ref<GodotVer> godot_ver = p_decomp->get_godot_ver();
	bool encode_full_objects = godot_ver->lt(NO_FULL_OBJ_VER) && NO_FULL_OBJ_VER->get_major() == godot_ver->get_major();
	GDScriptV1TokenizerTextCompat tt(p_decomp);
	tt.set_code(p_code);
	int line = -1;

	while (true) {
		if (tt.get_token_line() != line) {
			line = tt.get_token_line();
			line_map[line] = token_array.size();
		}
		const Token g_token = tt.get_token();
		uint32_t local_token = p_decomp->get_local_token_val(g_token);
		switch (g_token) {
			case Token::G_TK_IDENTIFIER: {
				StringName id = tt.get_token_identifier();
				if (!identifier_map.has(id)) {
					int idx = identifier_map.size();
					identifier_map[id] = idx;
				}
				local_token |= identifier_map[id] << TOKEN_BITS;
			} break;
			case Token::G_TK_CONSTANT: {
				const Variant &c = tt.get_token_constant();
				if (!constant_map.has(c)) {
					int idx = constant_map.size();
					constant_map[c] = idx;
				}
				local_token |= constant_map[c] << TOKEN_BITS;
			} break;
			case Token::G_TK_BUILT_IN_TYPE: {
				Variant::Type type = tt.get_token_type();
				int local_type = VariantDecoderCompat::convert_variant_type_to_old(type, variant_ver_major);
				local_token |= local_type << TOKEN_BITS;
			} break;
			case Token::G_TK_BUILT_IN_FUNC: {
				// built-in function already has correct value
				local_token |= tt.get_token_built_in_func() << TOKEN_BITS;
			} break;
			case Token::G_TK_NEWLINE: {
				local_token |= tt.get_token_line_indent() << TOKEN_BITS;
			} break;
			case Token::G_TK_ERROR: {
				String err = tt.get_token_error();
				GDSDECOMP_FAIL_V_MSG(Vector<uint8_t>(), vformat("Compile error, line %d: %s", tt.get_token_line(), err));
			} break;
			default: {
			}
		};

		token_array.push_back(local_token);

		if (tt.get_token() == Token::G_TK_EOF) {
			break;
		}
		tt.advance();
	}

	//reverse maps

	RBMap<int, StringName> rev_identifier_map;
	for (RBMap<StringName, int>::Element *E = identifier_map.front(); E; E = E->next()) {
		rev_identifier_map[E->get()] = E->key();
	}

	RBMap<int, Variant> rev_constant_map;
	for (auto K : constant_map) {
		rev_constant_map[K.value] = K.key;
	}

	RBMap<int, uint32_t> rev_line_map;
	for (RBMap<uint32_t, int>::Element *E = line_map.front(); E; E = E->next()) {
		rev_line_map[E->get()] = E->key();
	}

	//save header
	buf.resize(24);
	buf.write[0] = 'G';
	buf.write[1] = 'D';
	buf.write[2] = 'S';
	buf.write[3] = 'C';
	encode_uint32(p_decomp->get_bytecode_version(), &buf.write[4]);
	int content_start = 8;
	if (p_decomp->get_bytecode_version() >= GDScriptDecomp::GDSCRIPT_2_0_VERSION) {
		encode_uint32(0, &buf.write[8]);
		content_start = 12;
	}
	encode_uint32(identifier_map.size(), &buf.write[content_start]);
	encode_uint32(constant_map.size(), &buf.write[content_start + 4]);
	encode_uint32(line_map.size(), &buf.write[content_start + 8]);
	encode_uint32(token_array.size(), &buf.write[content_start + 12]);

	//save identifiers

	for (RBMap<int, StringName>::Element *E = rev_identifier_map.front(); E; E = E->next()) {
		CharString cs = String(E->get()).utf8();
		int len = cs.length() + 1;
		int extra = 4 - (len % 4);
		if (extra == 4) {
			extra = 0;
		}

		uint8_t ibuf[4];
		encode_uint32(len + extra, ibuf);
		for (int i = 0; i < 4; i++) {
			buf.push_back(ibuf[i]);
		}
		for (int i = 0; i < len; i++) {
			buf.push_back(cs[i] ^ 0xb6);
		}
		for (int i = 0; i < extra; i++) {
			buf.push_back(0 ^ 0xb6);
		}
	}

	for (RBMap<int, Variant>::Element *E = rev_constant_map.front(); E; E = E->next()) {
		int len;
		Error err = VariantDecoderCompat::encode_variant_compat(variant_ver_major, E->get(), nullptr, len, encode_full_objects);
		GDSDECOMP_FAIL_COND_V_MSG(err != OK, Vector<uint8_t>(), "Error when trying to encode Variant.");
		int pos = buf.size();
		buf.resize_initialized(pos + len);
		VariantDecoderCompat::encode_variant_compat(variant_ver_major, E->get(), &buf.write[pos], len, encode_full_objects);
	}

	for (RBMap<int, uint32_t>::Element *E = rev_line_map.front(); E; E = E->next()) {
		uint8_t ibuf[8];
		encode_uint32(E->key(), &ibuf[0]);
		encode_uint32(E->get(), &ibuf[4]);
		for (int i = 0; i < 8; i++) {
			buf.push_back(ibuf[i]);
		}
	}

	for (int i = 0; i < token_array.size(); i++) {
		uint32_t token = token_array[i];

		if (token & ~TOKEN_MASK) {
			uint8_t buf4[4];
			encode_uint32(token_array[i] | TOKEN_BYTE_MASK, &buf4[0]);
			for (int j = 0; j < 4; j++) {
				buf.push_back(buf4[j]);
			}
		} else {
			buf.push_back(token);
		}
	}

	return buf;
}

GDScriptV1TokenizerBufferCompat::Token GDScriptV1TokenizerBufferCompat::get_token(int p_offset) const {
	int offset = token + p_offset;

	if (offset < 0 || offset >= tokens.size()) {
		return Token::G_TK_EOF;
	}

	return decomp->get_global_token((tokens[offset] & TOKEN_MASK));
}

StringName GDScriptV1TokenizerBufferCompat::get_token_identifier(int p_offset) const {
	int offset = token + p_offset;

	ERR_FAIL_INDEX_V(offset, tokens.size(), StringName());
	uint32_t identifier = tokens[offset] >> TOKEN_BITS;
	ERR_FAIL_UNSIGNED_INDEX_V(identifier, (uint32_t)identifiers.size(), StringName());

	return identifiers[identifier];
}

int GDScriptV1TokenizerBufferCompat::get_token_built_in_func(int p_offset) const {
	int offset = token + p_offset;
	ERR_FAIL_INDEX_V(offset, tokens.size(), decomp->get_function_count());
	return int(tokens[offset] >> TOKEN_BITS);
}

Variant::Type GDScriptV1TokenizerBufferCompat::get_token_type(int p_offset) const {
	int offset = token + p_offset;
	ERR_FAIL_INDEX_V(offset, tokens.size(), Variant::NIL);

	int local_type = tokens[offset] >> TOKEN_BITS;
	return VariantDecoderCompat::convert_variant_type_from_old(local_type, decomp->get_variant_ver_major());
}

int GDScriptV1TokenizerBufferCompat::get_token_line(int p_offset) const {
	int offset = token + p_offset;
	auto pos_it = lines.find_closest(offset);

	auto largest = lines.back();
	uint32_t l = 0;
	if (pos_it == nullptr) {
		if (offset > largest->key()) {
			l = largest->value();
		} else {
			return -1;
		}
	} else {
		l = pos_it->value();
	}

	return l & TOKEN_LINE_MASK;
}

int GDScriptV1TokenizerBufferCompat::get_token_column(int p_offset) const {
	int offset = token + p_offset;
	auto pos_it = lines.find_closest(offset);

	auto largest = lines.back();
	uint32_t l = 0;
	if (pos_it == nullptr) {
		if (offset > largest->key()) {
			l = largest->value();
		} else {
			return -1;
		}
	} else {
		l = pos_it->value();
	}
	return l >> TOKEN_LINE_BITS;
}
int GDScriptV1TokenizerBufferCompat::get_token_line_indent(int p_offset) const {
	int offset = token + p_offset;
	ERR_FAIL_INDEX_V(offset, tokens.size(), 0);
	return tokens[offset] >> TOKEN_BITS;
}
const Variant &GDScriptV1TokenizerBufferCompat::get_token_constant(int p_offset) const {
	int offset = token + p_offset;
	ERR_FAIL_INDEX_V(offset, tokens.size(), nil);
	uint32_t constant = tokens[offset] >> TOKEN_BITS;
	ERR_FAIL_UNSIGNED_INDEX_V(constant, (uint32_t)constants.size(), nil);
	return constants[constant];
}
String GDScriptV1TokenizerBufferCompat::get_token_error(int p_offset) const {
	ERR_FAIL_V(String());
}

void GDScriptV1TokenizerBufferCompat::advance(int p_amount) {
	ERR_FAIL_INDEX(p_amount + token, tokens.size());
	token += p_amount;
}

GDScriptTokenizerV1Compat::Token GDScriptTokenizerV1Compat::scan() {
	int line = get_token_line();
	const TokenType g_token = get_token();
	if (g_token == TokenType::G_TK_EOF) {
		Token data;
		data.type = TokenType::G_TK_EOF;
		data.start_line = line;
		data.start_column = get_token_column();
		return data;
	}
	StringName id;
	Variant c;
	String err;
	Token data;
	data.type = g_token;
	switch (g_token) {
		case TokenType::G_TK_IDENTIFIER: {
			data.literal = get_token_identifier();
		} break;
		case TokenType::G_TK_CONSTANT: {
			data.literal = get_token_constant();
		} break;
		case TokenType::G_TK_BUILT_IN_TYPE: {
			data.vtype = get_token_type();
		} break;
		case TokenType::G_TK_BUILT_IN_FUNC: {
			// built-in function already has correct value
			data.func = get_token_built_in_func();
			data.literal = decomp->get_function_name(data.func);
		} break;
		case TokenType::G_TK_NEWLINE: {
			current_indent = get_token_line_indent();
		} break;
		case TokenType::G_TK_ERROR: {
			data.literal = get_token_error();
		} break;
		default: {
		}
	};
	data.start_line = line;
	data.end_line = line;
	data.start_column = get_token_column();
	data.end_column = data.start_column;
	data.current_indent = current_indent;
	advance();
	return data;
}

GDScriptV1TokenizerBufferCompat::GDScriptV1TokenizerBufferCompat(const GDScriptDecomp *p_decomp) :
		GDScriptTokenizerV1Compat(p_decomp) {
	token = 0;
}
