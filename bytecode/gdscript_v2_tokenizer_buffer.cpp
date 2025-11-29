/**************************************************************************/
/*  gdscript_tokenizer_buffer.cpp                                         */
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

#include "gdscript_v2_tokenizer_buffer.h"

#include "core/io/compression.h"
#include "core/io/marshalls.h"

int GDScriptV2TokenizerBufferCompat::_token_to_binary(const Token &p_token, Vector<uint8_t> &r_buffer, int p_start, HashMap<StringName, uint32_t> &r_identifiers_map, HashMap<Variant, uint32_t> &r_constants_map, const GDScriptDecomp *p_decomp) {
	int pos = p_start;

	int token_type = p_decomp->get_local_token_val((GDScriptDecomp::GlobalToken)(p_token.type & TOKEN_MASK));

	switch (p_token.type) {
		case GDScriptV2TokenizerCompat::Token::Type::G_TK_ANNOTATION:
		case GDScriptV2TokenizerCompat::Token::Type::G_TK_IDENTIFIER: {
			// Add identifier to map.
			int identifier_pos;
			StringName id = p_token.get_identifier();
			if (r_identifiers_map.has(id)) {
				identifier_pos = r_identifiers_map[id];
			} else {
				identifier_pos = r_identifiers_map.size();
				r_identifiers_map[id] = identifier_pos;
			}
			token_type |= identifier_pos << TOKEN_BITS;
		} break;
		case GDScriptV2TokenizerCompat::Token::Type::G_TK_ERROR:
		case GDScriptV2TokenizerCompat::Token::Type::G_TK_CONSTANT: {
			// Add literal to map.
			int constant_pos;
			if (r_constants_map.has(p_token.literal)) {
				constant_pos = r_constants_map[p_token.literal];
			} else {
				constant_pos = r_constants_map.size();
				r_constants_map[p_token.literal] = constant_pos;
			}
			token_type |= constant_pos << TOKEN_BITS;
		} break;
		default:
			break;
	}

	// Encode token.
	int token_len;
	if (token_type & TOKEN_MASK) {
		token_len = 8;
		r_buffer.resize(pos + token_len);
		encode_uint32(token_type | TOKEN_BYTE_MASK, &r_buffer.write[pos]);
		pos += 4;
	} else {
		token_len = 5;
		r_buffer.resize(pos + token_len);
		r_buffer.write[pos] = token_type;
		pos++;
	}
	encode_uint32(p_token.start_line, &r_buffer.write[pos]);
	return token_len;
}

int get_token_line(const RBMap<int, int> &lines, int offset) {
	auto pos_it = lines.find_closest(offset);

	auto largest = lines.back();
	int l = 0;
	if (pos_it == nullptr) {
		if (offset > largest->key()) {
			l = largest->value();
		} else {
			return -1;
		}
	} else {
		l = pos_it->value();
	}

	return l;
}

GDScriptV2TokenizerCompat::Token GDScriptV2TokenizerBufferCompat::_binary_to_token(const uint8_t *p_buffer, int p_token_index) {
	Token token;
	const uint8_t *b = p_buffer;

	uint32_t token_type = decode_uint32(b);
	token.type = decomp->get_global_token((token_type & TOKEN_MASK));
	if (token_type & TOKEN_BYTE_MASK) {
		b += 4;
	} else {
		b++;
	}
	// token.start_line = decode_uint32(b);
	token.start_line = get_token_line(token_lines, p_token_index);
	token.end_line = token.start_line;
	token.start_column = get_token_line(token_columns, p_token_index);
	token.end_column = token.start_column;

	token.literal = token.get_name();
	if (token.type == Token::Type::G_TK_CONST_NAN) {
		token.literal = String("NAN"); // Special case since name and notation are different.
	}

	switch (token.type) {
		case GDScriptV2TokenizerCompat::Token::Type::G_TK_ANNOTATION:
		case GDScriptV2TokenizerCompat::Token::Type::G_TK_IDENTIFIER: {
			// Get name from map.
			int identifier_pos = token_type >> TOKEN_BITS;
			if (unlikely(identifier_pos >= identifiers.size())) {
				Token error;
				error.type = Token::Type::G_TK_ERROR;
				error.literal = "Identifier index out of bounds.";
				return error;
			}
			token.literal = identifiers[identifier_pos];
		} break;
		case GDScriptV2TokenizerCompat::Token::Type::G_TK_ERROR:
		case GDScriptV2TokenizerCompat::Token::Type::G_TK_CONSTANT: {
			// Get literal from map.
			int constant_pos = token_type >> TOKEN_BITS;
			if (unlikely(constant_pos >= constants.size())) {
				Token error;
				error.type = Token::Type::G_TK_ERROR;
				error.literal = "Constant index out of bounds.";
				return error;
			}
			token.literal = constants[constant_pos];
		} break;
		default:
			break;
	}

	return token;
}

Error GDScriptV2TokenizerBufferCompat::set_code_buffer(const Vector<uint8_t> &p_buffer) {
	const uint8_t *buf = p_buffer.ptr();
	ERR_FAIL_COND_V(p_buffer.size() < 12 || p_buffer[0] != 'G' || p_buffer[1] != 'D' || p_buffer[2] != 'S' || p_buffer[3] != 'C', ERR_INVALID_DATA);

	int version = decode_uint32(&buf[4]);
	ERR_FAIL_COND_V_MSG(version > decomp->get_bytecode_version(), ERR_INVALID_DATA, "Binary GDScript is too recent! Please use a newer engine version.");

	int decompressed_size = decode_uint32(&buf[8]);

	Vector<uint8_t> contents;
	if (decompressed_size == 0) {
		contents = p_buffer.slice(12);
	} else {
		contents.resize(decompressed_size);
		int result = Compression::decompress(contents.ptrw(), contents.size(), &buf[12], p_buffer.size() - 12, Compression::MODE_ZSTD);
		ERR_FAIL_COND_V_MSG(result != decompressed_size, ERR_INVALID_DATA, "Error decompressing GDScript tokenizer buffer.");
	}

	int total_len = contents.size();
	buf = contents.ptr();
	const int token_count_offset = version < GDScriptDecomp::CONTENT_HEADER_SIZE_CHANGED ? 16 : 12;
	const int content_header_size = token_count_offset + 4;
	uint32_t identifier_count = decode_uint32(&buf[0]);
	uint32_t constant_count = decode_uint32(&buf[4]);
	uint32_t token_line_count = decode_uint32(&buf[8]);
	uint32_t token_count = decode_uint32(&buf[token_count_offset]);

	const uint8_t *b = &buf[content_header_size];
	total_len -= content_header_size;

	identifiers.resize(identifier_count);
	for (uint32_t i = 0; i < identifier_count; i++) {
		uint32_t len = decode_uint32(b);
		total_len -= 4;
		ERR_FAIL_COND_V((len * 4u) > (uint32_t)total_len, ERR_INVALID_DATA);
		b += 4;
		Vector<uint32_t> cs;
		cs.resize(len);
		for (uint32_t j = 0; j < len; j++) {
			uint8_t tmp[4];
			for (uint32_t k = 0; k < 4; k++) {
				tmp[k] = b[j * 4 + k] ^ 0xb6;
			}
			cs.write[j] = decode_uint32(tmp);
		}

		String s = String::utf32(Span(reinterpret_cast<const char32_t *>(cs.ptr()), len));
		b += len * 4;
		total_len -= len * 4;
		identifiers.write[i] = s;
	}

	constants.resize(constant_count);
	for (uint32_t i = 0; i < constant_count; i++) {
		Variant v;
		int len;
		Error err = decode_variant(v, b, total_len, &len, false);
		if (err) {
			return err;
		}
		b += len;
		total_len -= len;
		constants.write[i] = v;
	}

	for (uint32_t i = 0; i < token_line_count; i++) {
		ERR_FAIL_COND_V(total_len < 8, ERR_INVALID_DATA);
		uint32_t token_index = decode_uint32(b);
		b += 4;
		uint32_t line = decode_uint32(b);
		b += 4;
		total_len -= 8;
		token_lines[token_index] = line;
	}
	for (uint32_t i = 0; i < token_line_count; i++) {
		ERR_FAIL_COND_V(total_len < 8, ERR_INVALID_DATA);
		uint32_t token_index = decode_uint32(b);
		b += 4;
		uint32_t column = decode_uint32(b);
		b += 4;
		total_len -= 8;
		token_columns[token_index] = column;
	}

	tokens.resize(token_count);
	for (uint32_t i = 0; i < token_count; i++) {
		int token_len = 5;
		if ((*b) & TOKEN_BYTE_MASK) {
			token_len = 8;
		}
		ERR_FAIL_COND_V(total_len < token_len, ERR_INVALID_DATA);
		tokens.write[i] = _binary_to_token(b, i);
		b += token_len;
		// ERR_FAIL_INDEX_V(token.type, Token::Type::G_TK_MAX, ERR_INVALID_DATA);

		total_len -= token_len;
	}

	ERR_FAIL_COND_V(total_len > 0, ERR_INVALID_DATA);

	return OK;
}

Vector<uint8_t> GDScriptV2TokenizerBufferCompat::parse_code_string(const String &p_code, const GDScriptDecomp *p_decomp, CompressMode p_compress_mode) {
	HashMap<StringName, uint32_t> identifier_map;
	HashMap<Variant, uint32_t> constant_map;
	Vector<uint8_t> token_buffer;
	HashMap<uint32_t, uint32_t> token_lines;
	HashMap<uint32_t, uint32_t> token_columns;

	GDScriptV2TokenizerCompatText tokenizer(p_decomp);
	tokenizer.set_source_code(p_code);
	tokenizer.set_multiline_mode(true); // Ignore whitespace tokens.

	Token current = tokenizer.scan();
	Vector<Token> tokens;
	while (current.type != Token::Type::G_TK_EOF) {
		tokens.push_back(current);
		current = tokenizer.scan();
	}
	int token_pos = 0;
	int last_token_line = 0;
	int token_counter = 0;

	for (const Token &token : tokens) {
		int token_len = _token_to_binary(token, token_buffer, token_pos, identifier_map, constant_map, p_decomp);
		token_pos += token_len;
		if (token_counter > 0 && token.start_line > last_token_line) {
			token_lines[token_counter] = token.start_line;
			token_columns[token_counter] = token.start_column;
		}
		last_token_line = token.end_line;

		// current = tokenizer.scan();
		token_counter++;
	}

	// Reverse maps.
	Vector<StringName> rev_identifier_map;
	rev_identifier_map.resize(identifier_map.size());
	for (const KeyValue<StringName, uint32_t> &E : identifier_map) {
		rev_identifier_map.write[E.value] = E.key;
	}
	Vector<Variant> rev_constant_map;
	rev_constant_map.resize(constant_map.size());
	for (const KeyValue<Variant, uint32_t> &E : constant_map) {
		rev_constant_map.write[E.value] = E.key;
	}
	HashMap<uint32_t, uint32_t> rev_token_lines;
	for (const KeyValue<uint32_t, uint32_t> &E : token_lines) {
		rev_token_lines[E.value] = E.key;
	}

	// Remove continuation lines from map.
	for (int line : tokenizer.get_continuation_lines()) {
		if (rev_token_lines.has(line)) {
			token_lines.erase(rev_token_lines[line]);
			token_columns.erase(rev_token_lines[line]);
		}
	}

	const int bytecode_version = p_decomp->get_bytecode_version();
	const int token_count_offset = bytecode_version < GDScriptDecomp::CONTENT_HEADER_SIZE_CHANGED ? 16 : 12;
	const int content_header_size = token_count_offset + 4;

	Vector<uint8_t> contents;
	contents.resize(content_header_size);
	encode_uint32(identifier_map.size(), &contents.write[0]);
	encode_uint32(constant_map.size(), &contents.write[4]);
	encode_uint32(token_lines.size(), &contents.write[8]);
	if (bytecode_version < GDScriptDecomp::CONTENT_HEADER_SIZE_CHANGED) {
		encode_uint32(0, &contents.write[12]); // Unused, only written in bytecode version 100
	}
	encode_uint32(token_counter, &contents.write[token_count_offset]);

	int buf_pos = content_header_size;

	// Save identifiers.
	for (const StringName &id : rev_identifier_map) {
		String s = id.operator String();
		int len = s.length();

		contents.resize(buf_pos + (len + 1) * 4);

		encode_uint32(len, &contents.write[buf_pos]);
		buf_pos += 4;

		for (int i = 0; i < len; i++) {
			uint8_t tmp[4];
			encode_uint32(s[i], tmp);

			for (int b = 0; b < 4; b++) {
				contents.write[buf_pos + b] = tmp[b] ^ 0xb6;
			}

			buf_pos += 4;
		}
	}

	// Save constants.
	for (const Variant &v : rev_constant_map) {
		int len;
		// Objects cannot be constant, never encode objects.
		Error err = encode_variant(v, nullptr, len, false);
		ERR_FAIL_COND_V_MSG(err != OK, Vector<uint8_t>(), "Error when trying to encode Variant.");
		contents.resize(buf_pos + len);
		encode_variant(v, &contents.write[buf_pos], len, false);
		buf_pos += len;
	}

	// Save lines and columns.
	contents.resize(buf_pos + token_lines.size() * 16);
	for (const KeyValue<uint32_t, uint32_t> &e : token_lines) {
		encode_uint32(e.key, &contents.write[buf_pos]);
		buf_pos += 4;
		encode_uint32(e.value, &contents.write[buf_pos]);
		buf_pos += 4;
	}
	for (const KeyValue<uint32_t, uint32_t> &e : token_columns) {
		encode_uint32(e.key, &contents.write[buf_pos]);
		buf_pos += 4;
		encode_uint32(e.value, &contents.write[buf_pos]);
		buf_pos += 4;
	}

	// Store tokens.
	contents.append_array(token_buffer);

	Vector<uint8_t> buf;

	// Save header.
	buf.resize(12); // 'GDSC', bytecode_version, decompressed_size
	buf.write[0] = 'G';
	buf.write[1] = 'D';
	buf.write[2] = 'S';
	buf.write[3] = 'C';
	encode_uint32(bytecode_version, &buf.write[4]);

	switch (p_compress_mode) {
		case COMPRESS_NONE:
			encode_uint32(0u, &buf.write[8]);
			buf.append_array(contents);
			break;

		case COMPRESS_ZSTD: {
			encode_uint32(contents.size(), &buf.write[8]);
			Vector<uint8_t> compressed;
			int max_size = Compression::get_max_compressed_buffer_size(contents.size(), Compression::MODE_ZSTD);
			compressed.resize(max_size);

			int compressed_size = Compression::compress(compressed.ptrw(), contents.ptr(), contents.size(), Compression::MODE_ZSTD);
			ERR_FAIL_COND_V_MSG(compressed_size < 0, Vector<uint8_t>(), "Error compressing GDScript tokenizer buffer.");
			compressed.resize(compressed_size);

			buf.append_array(compressed);
		} break;
	}

	return buf;
}

int GDScriptV2TokenizerBufferCompat::get_cursor_line() const {
	return 0;
}

int GDScriptV2TokenizerBufferCompat::get_cursor_column() const {
	return 0;
}

void GDScriptV2TokenizerBufferCompat::set_cursor_position(int p_line, int p_column) {
}

void GDScriptV2TokenizerBufferCompat::set_multiline_mode(bool p_state) {
	multiline_mode = p_state;
}

bool GDScriptV2TokenizerBufferCompat::is_past_cursor() const {
	return false;
}

void GDScriptV2TokenizerBufferCompat::push_expression_indented_block() {
	indent_stack_stack.push_back(indent_stack);
}

void GDScriptV2TokenizerBufferCompat::pop_expression_indented_block() {
	ERR_FAIL_COND(indent_stack_stack.is_empty());
	indent_stack = indent_stack_stack.back()->get();
	indent_stack_stack.pop_back();
}

GDScriptV2TokenizerCompat::Token GDScriptV2TokenizerBufferCompat::scan() {
	// Add final newline.
	if (current >= tokens.size() && !last_token_was_newline) {
		Token newline;
		newline.type = Token::Type::G_TK_NEWLINE;
		newline.start_line = current_line;
		newline.end_line = current_line;
		last_token_was_newline = true;
		return newline;
	}

	// Resolve pending indentation change.
	if (pending_indents > 0) {
		pending_indents--;
		Token indent;
		indent.type = Token::Type::G_TK_INDENT;
		indent.start_line = current_line;
		indent.end_line = current_line;
		return indent;
	} else if (pending_indents < 0) {
		pending_indents++;
		Token dedent;
		dedent.type = Token::Type::G_TK_DEDENT;
		dedent.start_line = current_line;
		dedent.end_line = current_line;
		return dedent;
	}

	if (current >= tokens.size()) {
		if (!indent_stack.is_empty()) {
			pending_indents -= indent_stack.size();
			indent_stack.clear();
			return scan();
		}
		Token eof;
		eof.type = Token::Type::G_TK_EOF;
		return eof;
	};

	if (!last_token_was_newline && token_lines.has(current)) {
		current_line = token_lines[current];
		uint32_t current_column = token_columns[current];

		// Check if there's a need to indent/dedent.
		if (!multiline_mode) {
			uint32_t previous_indent = 0;
			if (!indent_stack.is_empty()) {
				previous_indent = indent_stack.back()->get();
			}
			if (current_column - 1 > previous_indent) {
				pending_indents++;
				indent_stack.push_back(current_column - 1);
			} else {
				while (current_column - 1 < previous_indent) {
					pending_indents--;
					indent_stack.pop_back();
					if (indent_stack.is_empty()) {
						break;
					}
					previous_indent = indent_stack.back()->get();
				}
			}

			Token newline;
			newline.type = Token::Type::G_TK_NEWLINE;
			newline.start_line = current_line;
			newline.end_line = current_line;
			last_token_was_newline = true;

			return newline;
		}
	}

	last_token_was_newline = false;

	Token token = tokens[current++];
	return token;
}

GDScriptV2TokenizerBufferCompat::GDScriptV2TokenizerBufferCompat(const GDScriptDecomp *p_decomp) {
	decomp = p_decomp;
}
