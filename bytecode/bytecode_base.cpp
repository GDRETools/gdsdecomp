/*************************************************************************/
/*  bytecode_base.cpp                                                    */
/*************************************************************************/

#include "bytecode_base.h"

#include "bytecode/bytecode_versions.h"
#include "bytecode/gdscript_tokenizer_compat.h"
#include "bytecode/gdscript_v2_tokenizer_buffer.h"
#include "compat/file_access_encrypted_v3.h"
#include "compat/variant_decoder_compat.h"
#include "compat/variant_writer_compat.h"
#include "utility/common.h"
#include "utility/gdre_settings.h"
#include "utility/godotver.h"

#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/io/file_access_encrypted.h"
#include "core/io/marshalls.h"
#include "core/object/class_db.h"
#include "core/variant/binder_common.h"

#define GDSDECOMP_FAIL_V_MSG(m_retval, m_msg) \
	error_message = RTR(m_msg);               \
	ERR_FAIL_V_MSG(m_retval, m_msg);

#define GDSDECOMP_FAIL_COND_V_MSG(m_cond, m_retval, m_msg) \
	if (unlikely(m_cond)) {                                \
		error_message = RTR(m_msg);                        \
		ERR_FAIL_V_MSG(m_retval, m_msg);                   \
	}
#define GDSDECOMP_FAIL_COND_V(m_cond, m_retval)                                                    \
	if (unlikely(m_cond)) {                                                                        \
		error_message = RTR("Condition \"" _STR(m_cond) "\" is true. Returning: " _STR(m_retval)); \
		ERR_FAIL_COND_V(m_cond, m_retval);                                                         \
	}

Vector<uint8_t> GDScriptDecomp::_get_buffer_encrypted(const String &p_path, int engine_ver_major, Vector<uint8_t> p_key) {
	Vector<uint8_t> bytecode;
	Error err = get_buffer_encrypted(p_path, engine_ver_major, p_key, bytecode);
	if (err != OK) {
		return {};
	}
	return bytecode;
}

void GDScriptDecomp::_bind_methods() {
	BIND_ENUM_CONSTANT(BYTECODE_TEST_CORRUPT);
	BIND_ENUM_CONSTANT(BYTECODE_TEST_FAIL);
	BIND_ENUM_CONSTANT(BYTECODE_TEST_PASS);
	ClassDB::bind_method(D_METHOD("decompile_byte_code", "path"), &GDScriptDecomp::decompile_byte_code);
	ClassDB::bind_method(D_METHOD("decompile_byte_code_encrypted", "path", "key"), &GDScriptDecomp::decompile_byte_code_encrypted);
	ClassDB::bind_method(D_METHOD("test_bytecode", "buffer", "print_verbose"), &GDScriptDecomp::test_bytecode, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("compile_code_string", "code"), &GDScriptDecomp::compile_code_string);

	ClassDB::bind_method(D_METHOD("get_script_text"), &GDScriptDecomp::get_script_text);
	ClassDB::bind_method(D_METHOD("get_error_message"), &GDScriptDecomp::get_error_message);
	ClassDB::bind_method(D_METHOD("get_bytecode_version"), &GDScriptDecomp::get_bytecode_version);
	ClassDB::bind_method(D_METHOD("get_bytecode_rev"), &GDScriptDecomp::get_bytecode_rev);
	ClassDB::bind_method(D_METHOD("get_engine_ver_major"), &GDScriptDecomp::get_engine_ver_major);
	ClassDB::bind_method(D_METHOD("get_variant_ver_major"), &GDScriptDecomp::get_variant_ver_major);
	ClassDB::bind_method(D_METHOD("get_function_count"), &GDScriptDecomp::get_function_count);
	// ClassDB::bind_method(D_METHOD("get_function_arg_count", "func_idx"), &GDScriptDecomp::get_function_arg_count);
	ClassDB::bind_method(D_METHOD("get_function_name", "func_idx"), &GDScriptDecomp::get_function_name);
	ClassDB::bind_method(D_METHOD("get_function_index", "func_name"), &GDScriptDecomp::get_function_index);
	ClassDB::bind_method(D_METHOD("get_token_max"), &GDScriptDecomp::get_token_max);

	ClassDB::bind_method(D_METHOD("get_engine_version"), &GDScriptDecomp::get_engine_version);
	ClassDB::bind_method(D_METHOD("get_max_engine_version"), &GDScriptDecomp::get_max_engine_version);
	ClassDB::bind_method(D_METHOD("get_godot_ver"), &GDScriptDecomp::get_godot_ver);
	ClassDB::bind_method(D_METHOD("get_parent"), &GDScriptDecomp::get_parent);

	ClassDB::bind_method(D_METHOD("to_json"), &GDScriptDecomp::to_json);

	ClassDB::bind_static_method("GDScriptDecomp", D_METHOD("get_buffer_encrypted", "path", "engine_ver_major", "key"), &GDScriptDecomp::_get_buffer_encrypted);
	ClassDB::bind_static_method("GDScriptDecomp", D_METHOD("create_decomp_for_commit", "commit_hash"), &GDScriptDecomp::create_decomp_for_commit);
	ClassDB::bind_static_method("GDScriptDecomp", D_METHOD("create_decomp_for_version", "ver", "p_force"), &GDScriptDecomp::create_decomp_for_version, DEFVAL(false));
	ClassDB::bind_static_method("GDScriptDecomp", D_METHOD("read_bytecode_version", "path"), &GDScriptDecomp::read_bytecode_version);
	ClassDB::bind_static_method("GDScriptDecomp", D_METHOD("read_bytecode_version_encrypted", "path", "engine_ver_major", "key"), &GDScriptDecomp::read_bytecode_version_encrypted);
	ClassDB::bind_static_method("GDScriptDecomp", D_METHOD("get_bytecode_versions"), &GDScriptDecomp::get_bytecode_versions);
	ClassDB::bind_static_method("GDScriptDecomp", D_METHOD("_create_custom_decomp", "custom_def", "derived_from"), &GDScriptDecomp::_create_custom_decomp, DEFVAL(0));
	ClassDB::bind_static_method("GDScriptDecomp", D_METHOD("register_decomp_version_custom", "custom_def", "derived_from"), &GDScriptDecomp::register_decomp_version_custom, DEFVAL(0));
	ClassDB::bind_static_method("GDScriptDecomp", D_METHOD("get_all_decomp_versions_json"), &GDScriptDecomp::get_all_decomp_versions_json);
}

void GDScriptDecomp::_ensure_space(String &p_code) {
	if (!p_code.ends_with(" ")) {
		p_code += String(" ");
	}
}

Error GDScriptDecomp::decompile_byte_code_encrypted(const String &p_path, Vector<uint8_t> p_key) {
	Vector<uint8_t> bytecode;
	Error err = get_buffer_encrypted(p_path, get_engine_ver_major(), p_key, bytecode);
	if (err != OK) {
		if (err == ERR_BUG) {
			error_message = RTR("FAE doesn't exist...???");
		} else if (err == ERR_UNAUTHORIZED) {
			error_message = RTR("Encryption Error");
		} else {
			error_message = RTR("File Error");
		}
		ERR_FAIL_V_MSG(err, error_message);
	}
	return decompile_buffer(bytecode);
}

Error GDScriptDecomp::decompile_byte_code(const String &p_path) {
	Vector<uint8_t> bytecode;

	if (p_path.get_extension().to_lower() == "gde") {
		return decompile_byte_code_encrypted(p_path, GDRESettings::get_singleton()->get_encryption_key());
	}
	bytecode = FileAccess::get_file_as_bytes(p_path);
	return decompile_buffer(bytecode);
}

Error GDScriptDecomp::get_buffer_encrypted(const String &p_path, int engine_ver_major, Vector<uint8_t> p_key, Vector<uint8_t> &bytecode) {
	Ref<FileAccess> fa = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_V(fa.is_null(), ERR_FILE_CANT_OPEN);

	// Godot v3 only encrypted the scripts and used a different format with different header fields,
	// So we need to use an older version of FAE to access them
	if (engine_ver_major <= 3) {
		Ref<FileAccessEncryptedv3> fae;
		fae.instantiate();
		ERR_FAIL_COND_V(fae.is_null(), ERR_BUG);

		Error err = fae->open_and_parse(fa, p_key, FileAccessEncryptedv3::MODE_READ);
		ERR_FAIL_COND_V(err != OK, ERR_UNAUTHORIZED);

		bytecode.resize(fae->get_length());
		fae->get_buffer(bytecode.ptrw(), bytecode.size());
	} else {
		Ref<FileAccessEncrypted> fae;
		fae.instantiate();
		ERR_FAIL_COND_V(fae.is_null(), ERR_BUG);

		Error err = fae->open_and_parse(fa, p_key, FileAccessEncrypted::MODE_READ);
		ERR_FAIL_COND_V(err != OK, ERR_UNAUTHORIZED);

		bytecode.resize(fae->get_length());
		fae->get_buffer(bytecode.ptrw(), bytecode.size());
	}
	return OK;
}

String GDScriptDecomp::get_script_text() {
	return script_text;
}

String GDScriptDecomp::get_error_message() {
	return error_message;
}

String GDScriptDecomp::get_constant_string(const Vector<Variant> &constants, uint32_t constId) {
	String constString;
	GDSDECOMP_FAIL_COND_V_MSG(constId >= constants.size(), "", "Invalid constant ID.");
	Error err = VariantWriterCompat::write_to_string_script(constants[constId], constString, get_variant_ver_major());
	GDSDECOMP_FAIL_COND_V_MSG(err, "", "Error when trying to encode Variant.");
	return constString;
}

int decompress_buf(const Vector<uint8_t> &p_buffer, Vector<uint8_t> &contents) {
	ERR_FAIL_COND_V(p_buffer.size() < 12, -1);
	const uint8_t *buf = p_buffer.ptr();
	int decompressed_size = decode_uint32(&buf[8]);
	int result = 0;
	if (decompressed_size == 0) {
		contents = p_buffer.slice(12);
	} else {
		contents.resize(decompressed_size);
		result = Compression::decompress(contents.ptrw(), contents.size(), &buf[12], p_buffer.size() - 12, Compression::MODE_ZSTD);
	}
	return result;
}

#define GDSC_HEADER "GDSC"
#define CHECK_GDSC_HEADER(p_buffer) _GDRE_CHECK_HEADER(p_buffer, GDSC_HEADER)

Error GDScriptDecomp::get_ids_consts_tokens_v2(const Vector<uint8_t> &p_buffer, Vector<StringName> &identifiers, Vector<Variant> &constants, Vector<uint32_t> &tokens, HashMap<uint32_t, uint32_t> &lines, HashMap<uint32_t, uint32_t> &end_lines, HashMap<uint32_t, uint32_t> &columns) {
	const uint8_t *buf = p_buffer.ptr();
	GDSDECOMP_FAIL_COND_V_MSG(p_buffer.size() < 12 || !CHECK_GDSC_HEADER(p_buffer), ERR_INVALID_DATA, "Invalid GDScript tokenizer buffer.");

	int version = decode_uint32(&buf[4]);
	GDSDECOMP_FAIL_COND_V_MSG(version > LATEST_GDSCRIPT_VERSION, ERR_INVALID_DATA, "Binary GDScript is too recent! Please use a newer engine version.");
	GDSDECOMP_FAIL_COND_V_MSG(version < GDSCRIPT_2_0_VERSION, ERR_INVALID_DATA, "Don't use this function for older versions of GDScript.");

	int decompressed_size = decode_uint32(&buf[8]);

	Vector<uint8_t> contents;
	GDSDECOMP_FAIL_COND_V_MSG(decompress_buf(p_buffer, contents) != decompressed_size, ERR_INVALID_DATA, "Error decompressing GDScript tokenizer buffer.");

	int total_len = contents.size();
	buf = contents.ptr();
	const int token_count_offset = version < CONTENT_HEADER_SIZE_CHANGED ? 16 : 12;
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
		GDSDECOMP_FAIL_COND_V_MSG((len * 4u) > (uint32_t)total_len, ERR_INVALID_DATA, "Invalid identifier length.");
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
		String s = String::utf32({ reinterpret_cast<const char32_t *>(cs.ptr()), len });
		b += len * 4;
		total_len -= len * 4;
		identifiers.write[i] = StringName(s);
	}

	constants.resize(constant_count);
	for (uint32_t i = 0; i < constant_count; i++) {
		Variant v;
		int len;
		Error err = VariantDecoderCompat::decode_variant_compat(get_variant_ver_major(), v, b, total_len, &len);
		if (err) {
			return err;
		}
		b += len;
		total_len -= len;
		constants.write[i] = v;
	}

	for (uint32_t i = 0; i < token_line_count; i++) {
		GDSDECOMP_FAIL_COND_V_MSG(total_len < 8, ERR_INVALID_DATA, "Invalid token line count.");
		uint32_t token_index = decode_uint32(b);
		b += 4;
		uint32_t line = decode_uint32(b);
		b += 4;
		total_len -= 8;
		lines[token_index] = line;
	}
	for (uint32_t i = 0; i < token_line_count; i++) {
		GDSDECOMP_FAIL_COND_V_MSG(total_len < 8, ERR_INVALID_DATA, "Invalid token column count.");
		uint32_t token_index = decode_uint32(b);
		b += 4;
		uint32_t column = decode_uint32(b);
		b += 4;
		total_len -= 8;
		columns[token_index] = column;
	}

	tokens.resize(token_count);

	for (uint32_t i = 0; i < token_count; i++) {
		int token_len = 5;
		if ((*b) & TOKEN_BYTE_MASK) { //BYTECODE_MASK, little endian always
			token_len = 8;
		}
		GDSDECOMP_FAIL_COND_V_MSG(total_len < token_len, ERR_INVALID_DATA, "Invalid token length.");

		if (token_len == 8) {
			tokens.write[i] = decode_uint32(b) & ~TOKEN_BYTE_MASK;
			b += 4;
		} else {
			tokens.write[i] = *b;
			b += 1;
		}
		auto end_line = decode_uint32(b);
		end_lines[i] = end_line;
		b += 4;
		total_len -= token_len;
	}

	GDSDECOMP_FAIL_COND_V_MSG(total_len > 0, ERR_INVALID_DATA, "Invalid token length.");

	return OK;
}

Error GDScriptDecomp::get_script_state(const Vector<uint8_t> &p_buffer, ScriptState &r_state) {
	const uint8_t *buf = p_buffer.ptr();
	GDSDECOMP_FAIL_COND_V_MSG(p_buffer.size() < 24 || !CHECK_GDSC_HEADER(p_buffer), ERR_INVALID_DATA, "Invalid GDScript tokenizer buffer.");
	r_state.bytecode_version = decode_uint32(&buf[4]);
	Error err;
	if (r_state.bytecode_version >= GDSCRIPT_2_0_VERSION) {
		err = get_ids_consts_tokens_v2(p_buffer, r_state.identifiers, r_state.constants, r_state.tokens, r_state.lines, r_state.end_lines, r_state.columns);
	} else {
		err = get_ids_consts_tokens(p_buffer, r_state.identifiers, r_state.constants, r_state.tokens, r_state.lines, r_state.columns);
	}
	if (err) {
		return err;
	}

	return OK;
}

Error GDScriptDecomp::get_ids_consts_tokens(const Vector<uint8_t> &p_buffer, Vector<StringName> &identifiers, Vector<Variant> &constants, Vector<uint32_t> &tokens, HashMap<uint32_t, uint32_t> &lines, HashMap<uint32_t, uint32_t> &columns) {
	const uint8_t *buf = p_buffer.ptr();
	uint64_t total_len = p_buffer.size();
	GDSDECOMP_FAIL_COND_V_MSG(p_buffer.size() < 24 || !CHECK_GDSC_HEADER(p_buffer), ERR_INVALID_DATA, "Invalid GDScript token buffer.");
	int version = decode_uint32(&buf[4]);
	ERR_FAIL_COND_V_MSG(version >= GDSCRIPT_2_0_VERSION, ERR_INVALID_DATA, "Wrong function!");
	const int contents_start = 8 + (version >= GDSCRIPT_2_0_VERSION ? 4 : 0);
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
		Error err = VariantDecoderCompat::decode_variant_compat(get_variant_ver_major(), v, b, total_len, &len);
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
		uint32_t token = decode_uint32(b);
		b += 4;
		uint32_t linecol = decode_uint32(b);
		b += 4;

		lines.insert(token, linecol);
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
	return OK;
}

Ref<GDScriptDecomp> GDScriptDecomp::create_decomp_for_commit(uint64_t p_commit_hash) {
	return Ref<GDScriptDecomp>(GDScriptDecompVersion::create_decomp_for_commit(p_commit_hash));
}

int GDScriptDecomp::read_bytecode_version(const String &p_path) {
	Vector<uint8_t> p_buffer;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), -1, "Cannot open file!");
	p_buffer = f->get_buffer(24);
	ERR_FAIL_COND_V_MSG(p_buffer.size() < 24 || p_buffer[0] != 'G' || p_buffer[1] != 'D' || p_buffer[2] != 'S' || p_buffer[3] != 'C', -1, "Invalid gdscript!");
	const uint8_t *buf = p_buffer.ptr();
	int version = decode_uint32(&buf[4]);
	return version;
}

int GDScriptDecomp::read_bytecode_version_encrypted(const String &p_path, int engine_ver_major, Vector<uint8_t> p_key) {
	Vector<uint8_t> p_buffer;
	Error err = get_buffer_encrypted(p_path, engine_ver_major, p_key, p_buffer);
	ERR_FAIL_COND_V_MSG(err == ERR_UNAUTHORIZED, -2, "Encryption error!!");
	ERR_FAIL_COND_V_MSG(err != OK, -1, "Cannot open file!");
	ERR_FAIL_COND_V_MSG(p_buffer.size() < 24 || p_buffer[0] != 'G' || p_buffer[1] != 'D' || p_buffer[2] != 'S' || p_buffer[3] != 'C', -1, "Invalid gdscript!");
	const uint8_t *buf = p_buffer.ptr();
	int version = decode_uint32(&buf[4]);
	return version;
}

// constant array of string literals of the global token enum values
const char *g_token_str[] = {
	"TK_EMPTY",
	"TK_IDENTIFIER",
	"TK_CONSTANT", // "TK_LITERAL" in 4.2
	"TK_SELF",
	"TK_BUILT_IN_TYPE",
	"TK_BUILT_IN_FUNC",
	"TK_OP_IN",
	"TK_OP_EQUAL", // "EQUAL_EQUAL" in 4.2
	"TK_OP_NOT_EQUAL", // "BANG_EQUAL" in 4.2
	"TK_OP_LESS",
	"TK_OP_LESS_EQUAL",
	"TK_OP_GREATER",
	"TK_OP_GREATER_EQUAL",
	"TK_OP_AND",
	"TK_OP_OR",
	"TK_OP_NOT",
	"TK_OP_ADD", // "PLUS" in 4.2
	"TK_OP_SUB", // "MINUS" in 4.2
	"TK_OP_MUL", // "STAR" in 4.2
	"TK_OP_DIV", // "SLASH" in 4.2
	"TK_OP_MOD", // "PERCENT" in 4.2
	"TK_OP_SHIFT_LEFT", // "LESS_LESS" in 4.2
	"TK_OP_SHIFT_RIGHT", // "GREATER_GREATER" in 4.2
	"TK_OP_ASSIGN", // "EQUAL" in 4.2
	"TK_OP_ASSIGN_ADD", // "PLUS_EQUAL" in 4.2
	"TK_OP_ASSIGN_SUB", // "MINUS_EQUAL" in 4.2
	"TK_OP_ASSIGN_MUL", // "STAR_EQUAL" in 4.2
	"TK_OP_ASSIGN_DIV", // "SLASH_EQUAL" in 4.2
	"TK_OP_ASSIGN_MOD", // "PERCENT_EQUAL" in 4.2
	"TK_OP_ASSIGN_SHIFT_LEFT", // "LESS_LESS_EQUAL" in 4.2
	"TK_OP_ASSIGN_SHIFT_RIGHT", // "GREATER_GREATER_EQUAL" in 4.2
	"TK_OP_ASSIGN_BIT_AND", // "AMPERSAND_EQUAL" in 4.2
	"TK_OP_ASSIGN_BIT_OR", // "PIPE_EQUAL" in 4.2
	"TK_OP_ASSIGN_BIT_XOR", // "CARET_EQUAL" in 4.2
	"TK_OP_BIT_AND", // "AMPERSAND" in 4.2
	"TK_OP_BIT_OR", // "PIPE" in 4.2
	"TK_OP_BIT_XOR", // "CARET" in 4.2
	"TK_OP_BIT_INVERT", // "TILDE" in 4.2
	"TK_CF_IF",
	"TK_CF_ELIF",
	"TK_CF_ELSE",
	"TK_CF_FOR",
	"TK_CF_WHILE",
	"TK_CF_BREAK",
	"TK_CF_CONTINUE",
	"TK_CF_PASS",
	"TK_CF_RETURN",
	"TK_CF_MATCH",
	"TK_PR_FUNCTION", // "FUNC" in 4.2
	"TK_PR_CLASS",
	"TK_PR_CLASS_NAME",
	"TK_PR_EXTENDS",
	"TK_PR_IS",
	"TK_PR_ONREADY",
	"TK_PR_TOOL",
	"TK_PR_STATIC",
	"TK_PR_EXPORT",
	"TK_PR_SETGET",
	"TK_PR_CONST",
	"TK_PR_VAR",
	"TK_PR_AS",
	"TK_PR_VOID",
	"TK_PR_ENUM",
	"TK_PR_PRELOAD",
	"TK_PR_ASSERT",
	"TK_PR_YIELD",
	"TK_PR_SIGNAL",
	"TK_PR_BREAKPOINT",
	"TK_PR_REMOTE",
	"TK_PR_SYNC",
	"TK_PR_MASTER",
	"TK_PR_SLAVE",
	"TK_PR_PUPPET",
	"TK_PR_REMOTESYNC",
	"TK_PR_MASTERSYNC",
	"TK_PR_PUPPETSYNC",
	"TK_BRACKET_OPEN",
	"TK_BRACKET_CLOSE",
	"TK_CURLY_BRACKET_OPEN",
	"TK_CURLY_BRACKET_CLOSE",
	"TK_PARENTHESIS_OPEN",
	"TK_PARENTHESIS_CLOSE",
	"TK_COMMA",
	"TK_SEMICOLON",
	"TK_PERIOD",
	"TK_QUESTION_MARK",
	"TK_COLON",
	"TK_DOLLAR",
	"TK_FORWARD_ARROW",
	"TK_NEWLINE",
	"TK_CONST_PI",
	"TK_CONST_TAU",
	"TK_WILDCARD",
	"TK_CONST_INF",
	"TK_CONST_NAN",
	"TK_ERROR",
	"TK_EOF",
	"TK_CURSOR",
	"TK_PR_SLAVESYNC", //renamed to puppet sync in most recent versions
	"TK_CF_DO", // removed in 3.1
	"TK_CF_CASE",
	"TK_CF_SWITCH",
	"TK_ANNOTATION", // added in 4.3
	"TK_AMPERSAND_AMPERSAND", // added in 4.3
	"TK_PIPE_PIPE", // added in 4.3
	"TK_BANG", // added in 4.3
	"TK_STAR_STAR", // added in 4.3
	"TK_STAR_STAR_EQUAL", // added in 4.3
	"TK_CF_WHEN", // added in 4.3
	"TK_PR_AWAIT", // added in 4.3
	"TK_PR_NAMESPACE", // added in 4.3
	"TK_PR_SUPER", // added in 4.3
	"TK_PR_TRAIT", // added in 4.3
	"TK_PERIOD_PERIOD", // added in 4.3
	"TK_UNDERSCORE", // added in 4.3
	"TK_INDENT", // added in 4.3
	"TK_DEDENT", // added in 4.3
	"TK_VCS_CONFLICT_MARKER", // added in 4.3
	"TK_BACKTICK", // added in 4.3
	"TK_ABSTRACT", // added in 4.5
	"TK_PERIOD_PERIOD_PERIOD", // added in 4.5
	"TK_MAX",
};
static_assert(sizeof(g_token_str) / sizeof(g_token_str[0]) == GDScriptDecomp::GlobalToken::G_TK_MAX + 1, "g_token_str size mismatch");

Error GDScriptDecomp::debug_print(Vector<uint8_t> p_buffer) {
	//Cleanup
	script_text = String();
	ScriptState script_state;
	//Load bytecode
	Error err = get_script_state(p_buffer, script_state);
	ERR_FAIL_COND_V(err != OK, err);
	Vector<StringName> &identifiers = script_state.identifiers;
	Vector<Variant> &constants = script_state.constants;
	Vector<uint32_t> &tokens = script_state.tokens;
	HashMap<uint32_t, uint32_t> &lines = script_state.lines;
	HashMap<uint32_t, uint32_t> &columns = script_state.columns;
	int version = script_state.bytecode_version;
	int bytecode_version = get_bytecode_version();
	int variant_ver_major = get_variant_ver_major();
	int FUNC_MAX = get_function_count();
	ERR_FAIL_COND_V(version != get_bytecode_version(), ERR_INVALID_DATA);
	//Decompile script
	String line;
	Ref<GodotVer> gv = get_godot_ver();
	print_line("Bytecode version: " + itos(bytecode_version));
	print_line("Variant version: " + itos(variant_ver_major));
	print_line("Godot version: " + gv->as_text());
	print_line("Function count: " + itos(FUNC_MAX));
	print_line("Identifiers count: " + itos(identifiers.size()));
	print_line("Constants count: " + itos(constants.size()));
	print_line("Tokens count: " + itos(tokens.size()));
	print_line("Lines count: " + itos(lines.size()));
	print_line("Columns count: " + itos(columns.size()));

	uint32_t max_line = 0;
	uint32_t max_column = 0;
	for (int i = 0; i < tokens.size(); i++) {
		max_line = MAX(max_line, lines[i]);
		if (columns.size() > 0) {
			max_column = MAX(max_column, columns[i]);
		}
	}
	print_line("Max line: " + uitos(max_line));
	print_line("Max column: " + uitos(max_column));

	print_line("\nIdentifiers:");
	for (int i = 0; i < identifiers.size(); i++) {
		print_line(itos(i) + ": " + String(identifiers[i]));
	}

	print_line("Constants:");
	for (int i = 0; i < constants.size(); i++) {
		print_line(itos(i) + ": " + get_constant_string(constants, i));
	}

	print_line("Tokens:");
	for (int i = 0; i < tokens.size(); i++) {
		GlobalToken curr_token = get_global_token(tokens[i]);
		int curr_line = lines[i];
		int curr_column = columns.size() > 0 ? columns[i] : 0;
		String tok_str = g_token_str[curr_token];
		if (curr_token == G_TK_IDENTIFIER) {
			tok_str += " (" + String(identifiers[tokens[i] >> TOKEN_BITS]) + ")";
		} else if (curr_token == G_TK_CONSTANT) {
			tok_str += " (" + get_constant_string(constants, tokens[i] >> TOKEN_BITS) + ")";
		}
		print_line(itos(i) + ": " + tok_str + " line: " + itos(curr_line) + " column: " + itos(curr_column));
	}
	return OK;
}

Error GDScriptDecomp::decompile_buffer(Vector<uint8_t> p_buffer) {
#if 0
	debug_print(p_buffer);
#endif
	//Cleanup
	script_text = String();

	ScriptState state;
	//Load bytecode
	Error err = get_script_state(p_buffer, state);
	ERR_FAIL_COND_V(err != OK, err);
	Vector<StringName> &identifiers = state.identifiers;
	Vector<Variant> &constants = state.constants;
	Vector<uint32_t> &tokens = state.tokens;
	HashMap<uint32_t, uint32_t> &lines = state.lines;
	HashMap<uint32_t, uint32_t> &columns = state.columns;
	int version = state.bytecode_version;

	int bytecode_version = get_bytecode_version();
	int variant_ver_major = get_variant_ver_major();
	uint32_t FUNC_MAX = get_function_count();
	GDSDECOMP_FAIL_COND_V(version != get_bytecode_version(), ERR_INVALID_DATA);

	//Decompile script
	String line;
	int indent = 0;

	GlobalToken prev_token = G_TK_NEWLINE;
	uint32_t prev_line = 1;
	uint32_t prev_line_start_column = 1;

	int tab_size = 1;
	bool use_spaces = false;
	if constexpr (FORCE_SPACES_FOR_2_0) {
		if (columns.size() > 0) {
			use_spaces = true;
		}
	}
	bool first_line = true;

	auto handle_newline = [&](int i, GlobalToken curr_token) {
		auto curr_line = state.get_token_line(i);
		auto curr_column = state.get_token_column(i);
		for (int j = 0; j < indent; j++) {
			script_text += use_spaces ? " " : "\t";
		}
		script_text += line;
		if (curr_line <= prev_line) {
			curr_line = prev_line + 1; // force new line
		}
		while (curr_line > prev_line) {
			if (curr_token != G_TK_NEWLINE && bytecode_version < GDSCRIPT_2_0_VERSION) {
				script_text += "\\"; // line continuation
			} else if (bytecode_version >= GDSCRIPT_2_0_VERSION && !lines.has(i)) {
				if (!first_line || (!gdre::remove_whitespace(line).is_empty())) {
					script_text += "\\";
				}
			}
			script_text += "\n";
			prev_line++;
		}
		first_line = false;
		line = String();
		if (curr_token == G_TK_NEWLINE) {
			indent = tokens[i] >> TOKEN_BITS;
		} else if (bytecode_version >= GDSCRIPT_2_0_VERSION) {
			prev_token = G_TK_NEWLINE;
			int col_diff = (int)curr_column - (int)prev_line_start_column;
			if (col_diff != 0) {
				int tabs = col_diff / tab_size;
				if (tabs == 0) {
					indent += (col_diff > 0 ? 1 : -1);
				} else {
					indent += tabs;
				}
			}
			prev_line_start_column = curr_column;
		}
		prev_token = G_TK_NEWLINE;
	};

	auto check_new_line = [&](int i) {
		auto ln = state.get_token_line(i);
		if (ln != prev_line && ln != 0) {
			return true;
		}
		return false;
	};

	auto ensure_space_func = [&]() {
		if (!line.ends_with(" ") && prev_token != G_TK_NEWLINE) {
			line += " ";
		}
	};

	auto ensure_ending_space_func([&](int idx, GlobalToken check_tk = G_TK_NEWLINE) {
		if (
				!line.ends_with(" ") && idx < tokens.size() - 1 &&
				(get_global_token(tokens[idx + 1]) != G_TK_NEWLINE &&
						!check_new_line(idx + 1)) &&
				(check_tk == G_TK_NEWLINE || get_global_token(tokens[idx + 1]) != check_tk)) {
			line += " ";
		}
	});

	for (int i = 0; i < tokens.size(); i++) {
		uint32_t local_token = tokens[i] & TOKEN_MASK;
		GlobalToken curr_token = get_global_token(local_token);
		if (curr_token != G_TK_NEWLINE && check_new_line(i)) {
			handle_newline(i, curr_token);
		}
		switch (curr_token) {
			case G_TK_EMPTY: {
				//skip
			} break;
			case G_TK_ANNOTATION: // fallthrough
			case G_TK_IDENTIFIER: {
				uint32_t identifier = tokens[i] >> TOKEN_BITS;
				GDSDECOMP_FAIL_COND_V(identifier >= (uint32_t)identifiers.size(), ERR_INVALID_DATA);
				line += String(identifiers[identifier]);
			} break;
			case G_TK_CONSTANT: {
				uint32_t constant = tokens[i] >> TOKEN_BITS;
				GDSDECOMP_FAIL_COND_V(constant >= (uint32_t)constants.size(), ERR_INVALID_DATA);
				// TODO: handle GDScript 2.0 multi-line strings: we have to check the number of newlines
				// in the string and if the next token has a line number difference >= the number of newlines
				line += get_constant_string(constants, constant);
			} break;
			case G_TK_SELF: {
				line += "self";
			} break;
			case G_TK_BUILT_IN_TYPE: {
				line += VariantDecoderCompat::get_variant_type_name(tokens[i] >> TOKEN_BITS, variant_ver_major);
			} break;
			case G_TK_BUILT_IN_FUNC: {
				GDSDECOMP_FAIL_COND_V(tokens[i] >> TOKEN_BITS >= FUNC_MAX, ERR_INVALID_DATA);
				line += get_function_name(tokens[i] >> TOKEN_BITS);
			} break;
			case G_TK_OP_IN: {
				ensure_space_func();
				line += "in ";
			} break;
			case G_TK_OP_EQUAL: {
				ensure_space_func();
				line += "== ";
			} break;
			case G_TK_OP_NOT_EQUAL: {
				ensure_space_func();
				line += "!= ";
			} break;
			case G_TK_OP_LESS: {
				ensure_space_func();
				line += "< ";
			} break;
			case G_TK_OP_LESS_EQUAL: {
				ensure_space_func();
				line += "<= ";
			} break;
			case G_TK_OP_GREATER: {
				ensure_space_func();
				line += "> ";
			} break;
			case G_TK_OP_GREATER_EQUAL: {
				ensure_space_func();
				line += ">= ";
			} break;
			case G_TK_OP_AND: {
				ensure_space_func();
				line += "and ";
			} break;
			case G_TK_OP_OR: {
				ensure_space_func();
				line += "or ";
			} break;
			case G_TK_OP_NOT: {
				ensure_space_func();
				line += "not ";
			} break;
			case G_TK_OP_ADD: {
				ensure_space_func();
				line += "+ ";
			} break;
			case G_TK_OP_SUB: {
				ensure_space_func();
				line += "- ";
				//TODO: do not add space after unary "-"
			} break;
			case G_TK_OP_MUL: {
				ensure_space_func();
				line += "* ";
			} break;
			case G_TK_OP_DIV: {
				ensure_space_func();
				line += "/ ";
			} break;
			case G_TK_OP_MOD: {
				ensure_space_func();
				line += "%";
				// if the previous token was a constant or an identifier, this is a modulo operation, add a space
				if (prev_token == G_TK_CONSTANT || prev_token == G_TK_IDENTIFIER || prev_token == G_TK_PARENTHESIS_CLOSE) {
					ensure_ending_space_func(i);
				}
			} break;
			case G_TK_OP_SHIFT_LEFT: {
				ensure_space_func();
				line += "<< ";
			} break;
			case G_TK_OP_SHIFT_RIGHT: {
				ensure_space_func();
				line += ">> ";
			} break;
			case G_TK_OP_ASSIGN: {
				ensure_space_func();
				line += "= ";
			} break;
			case G_TK_OP_ASSIGN_ADD: {
				ensure_space_func();
				line += "+= ";
			} break;
			case G_TK_OP_ASSIGN_SUB: {
				ensure_space_func();
				line += "-= ";
			} break;
			case G_TK_OP_ASSIGN_MUL: {
				ensure_space_func();
				line += "*= ";
			} break;
			case G_TK_OP_ASSIGN_DIV: {
				ensure_space_func();
				line += "/= ";
			} break;
			case G_TK_OP_ASSIGN_MOD: {
				ensure_space_func();
				line += "%= ";
			} break;
			case G_TK_OP_ASSIGN_SHIFT_LEFT: {
				ensure_space_func();
				line += "<<= ";
			} break;
			case G_TK_OP_ASSIGN_SHIFT_RIGHT: {
				ensure_space_func();
				line += ">>= ";
			} break;
			case G_TK_OP_ASSIGN_BIT_AND: {
				ensure_space_func();
				line += "&= ";
			} break;
			case G_TK_OP_ASSIGN_BIT_OR: {
				ensure_space_func();
				line += "|= ";
			} break;
			case G_TK_OP_ASSIGN_BIT_XOR: {
				ensure_space_func();
				line += "^= ";
			} break;
			case G_TK_OP_BIT_AND: {
				ensure_space_func();
				line += "& ";
			} break;
			case G_TK_OP_BIT_OR: {
				ensure_space_func();
				line += "| ";
			} break;
			case G_TK_OP_BIT_XOR: {
				ensure_space_func();
				line += "^ ";
			} break;
			case G_TK_OP_BIT_INVERT: {
				ensure_space_func();
				line += "~ ";
			} break;
			//case G_TK_OP_PLUS_PLUS: {
			//	line += "++";
			//} break;
			//case G_TK_OP_MINUS_MINUS: {
			//	line += "--";
			//} break;
			case G_TK_CF_IF: {
				ensure_space_func();
				line += "if ";
			} break;
			case G_TK_CF_ELIF: {
				line += "elif ";
			} break;
			case G_TK_CF_ELSE: {
				ensure_space_func();
				line += "else";
				ensure_ending_space_func(i, G_TK_COLON);
			} break;
			case G_TK_CF_FOR: {
				line += "for ";
			} break;
			case G_TK_CF_WHILE: {
				line += "while ";
			} break;
			case G_TK_CF_BREAK: {
				line += "break";
			} break;
			case G_TK_CF_CONTINUE: {
				line += "continue";
			} break;
			case G_TK_CF_PASS: {
				line += "pass";
			} break;
			case G_TK_CF_RETURN: {
				line += "return";
				ensure_ending_space_func(i);
			} break;
			case G_TK_CF_MATCH: {
				line += "match";
				ensure_ending_space_func(i);
			} break;
			case G_TK_PR_FUNCTION: {
				ensure_space_func();
				line += "func";
				ensure_ending_space_func(i, G_TK_PARENTHESIS_OPEN);
			} break;
			case G_TK_PR_CLASS: {
				ensure_space_func();
				line += "class ";
			} break;
			case G_TK_PR_CLASS_NAME: {
				ensure_space_func();
				line += "class_name ";
			} break;
			case G_TK_PR_EXTENDS: {
				ensure_space_func();
				line += "extends ";
			} break;
			case G_TK_PR_IS: {
				ensure_space_func();
				line += "is ";
			} break;
			case G_TK_PR_ONREADY: {
				line += "onready ";
			} break;
			case G_TK_PR_TOOL: {
				line += "tool ";
			} break;
			case G_TK_PR_STATIC: {
				line += "static ";
			} break;
			case G_TK_PR_EXPORT: {
				line += "export ";
			} break;
			case G_TK_PR_SETGET: {
				line += " setget ";
			} break;
			case G_TK_PR_CONST: {
				line += "const ";
			} break;
			case G_TK_PR_VAR: {
				if (line != String() && prev_token != G_TK_PR_ONREADY) {
					line += " ";
				}
				line += "var ";
			} break;
			case G_TK_PR_AS: {
				ensure_space_func();
				line += "as ";
			} break;
			case G_TK_PR_VOID: {
				line += "void ";
			} break;
			case G_TK_PR_ENUM: {
				line += "enum ";
			} break;
			case G_TK_PR_PRELOAD: {
				line += "preload";
			} break;
			case G_TK_PR_ASSERT: {
				line += "assert ";
			} break;
			case G_TK_PR_YIELD: {
				line += "yield";
				ensure_ending_space_func(i, G_TK_PARENTHESIS_OPEN);
			} break;
			case G_TK_PR_SIGNAL: {
				line += "signal ";
			} break;
			case G_TK_PR_BREAKPOINT: {
				line += "breakpoint";
				ensure_ending_space_func(i);
			} break;
			case G_TK_PR_REMOTE: {
				line += "remote ";
			} break;
			case G_TK_PR_SYNC: {
				line += "sync ";
			} break;
			case G_TK_PR_MASTER: {
				line += "master ";
			} break;
			case G_TK_PR_SLAVE: {
				line += "slave ";
			} break;
			case G_TK_PR_PUPPET: {
				line += "puppet ";
			} break;
			case G_TK_PR_REMOTESYNC: {
				line += "remotesync ";
			} break;
			case G_TK_PR_MASTERSYNC: {
				line += "mastersync ";
			} break;
			case G_TK_PR_PUPPETSYNC: {
				line += "puppetsync ";
			} break;
			case G_TK_BRACKET_OPEN: {
				line += "[";
			} break;
			case G_TK_BRACKET_CLOSE: {
				line += "]";
			} break;
			case G_TK_CURLY_BRACKET_OPEN: {
				line += "{";
			} break;
			case G_TK_CURLY_BRACKET_CLOSE: {
				line += "}";
			} break;
			case G_TK_PARENTHESIS_OPEN: {
				line += "(";
			} break;
			case G_TK_PARENTHESIS_CLOSE: {
				line += ")";
			} break;
			case G_TK_COMMA: {
				line += ", ";
			} break;
			case G_TK_SEMICOLON: {
				line += ";";
			} break;
			case G_TK_PERIOD: {
				line += ".";
			} break;
			case G_TK_QUESTION_MARK: {
				line += "?";
			} break;
			case G_TK_COLON: {
				line += ":";
				ensure_ending_space_func(i);
			} break;
			case G_TK_DOLLAR: {
				line += "$";
			} break;
			case G_TK_FORWARD_ARROW: {
				ensure_space_func();
				line += "->";
				ensure_ending_space_func(i);
			} break;
			case G_TK_INDENT:
			case G_TK_DEDENT:
			case G_TK_NEWLINE: {
				handle_newline(i, curr_token);
			} break;
			case G_TK_CONST_PI: {
				line += "PI";
			} break;
			case G_TK_CONST_TAU: {
				line += "TAU";
			} break;
			case G_TK_WILDCARD: {
				line += "_";
			} break;
			case G_TK_CONST_INF: {
				line += "INF";
			} break;
			case G_TK_CONST_NAN: {
				line += "NAN";
			} break;
			case G_TK_PR_SLAVESYNC: {
				line += "slavesync ";
			} break;
			case G_TK_CF_DO: {
				line += "do ";
			} break;
			case G_TK_CF_CASE: {
				line += "case ";
			} break;
			case G_TK_CF_SWITCH: {
				line += "switch ";
			} break;
			case G_TK_AMPERSAND_AMPERSAND: {
				ensure_space_func();
				line += "&& ";
			} break;
			case G_TK_PIPE_PIPE: {
				ensure_space_func();
				line += "|| ";
			} break;
			case G_TK_BANG: {
				ensure_space_func();
				line += "!";
			} break;
			case G_TK_STAR_STAR: {
				ensure_space_func();
				line += "** ";
			} break;
			case G_TK_STAR_STAR_EQUAL: {
				ensure_space_func();
				line += "**= ";
			} break;
			case G_TK_CF_WHEN: {
				ensure_space_func();
				line += "when ";
			} break;
			case G_TK_PR_AWAIT: {
				ensure_space_func();
				line += "await ";
			} break;
			case G_TK_PR_NAMESPACE: {
				ensure_space_func();
				line += "namespace ";
			} break;
			case G_TK_PR_SUPER: {
				ensure_space_func();
				line += "super";
				ensure_ending_space_func(i, G_TK_PERIOD);
			} break;
			case G_TK_PR_TRAIT: {
				ensure_space_func();
				line += "trait ";
			} break;
			case G_TK_PERIOD_PERIOD: {
				line += "..";
			} break;
			case G_TK_PERIOD_PERIOD_PERIOD: {
				line += "...";
			} break;
			case G_TK_UNDERSCORE: {
				line += "_";
			} break;
			case G_TK_BACKTICK: {
				line += "`";
			} break;
			case G_TK_ABSTRACT: {
				line += "abstract ";
			} break;
			case G_TK_ERROR: {
				//skip - invalid
			} break;
			case G_TK_EOF: {
				//skip - invalid
			} break;
			case G_TK_CURSOR: {
				//skip - invalid
			} break;
			case G_TK_VCS_CONFLICT_MARKER: {
				//skip - invalid
			} break;
			case G_TK_MAX: {
				GDSDECOMP_FAIL_V_MSG(ERR_INVALID_DATA, "Invalid token: TK_MAX (" + itos(local_token) + ")");
			} break;
			default: {
				GDSDECOMP_FAIL_V_MSG(ERR_INVALID_DATA, "Invalid token: " + itos(local_token));
			}
		}
		prev_token = curr_token;
	}

	if (!line.is_empty()) {
		for (int j = 0; j < indent; j++) {
			script_text += use_spaces ? " " : "\t";
		}
		script_text += line;
	}

	// GDScript 2.0 can have parsing errors if the script does not end with a newline
	if (bytecode_version >= GDSCRIPT_2_0_VERSION && !script_text.ends_with("\n")) {
		script_text += "\n";
	}

	if (script_text == String()) {
		if (identifiers.size() == 0 && constants.size() == 0 && tokens.size() == 0) {
			return OK;
		}
		error_message = RTR("Invalid token");
		return ERR_INVALID_DATA;
	}
	error_message = "";
	return OK;
}

GDScriptDecomp::BytecodeTestResult GDScriptDecomp::test_bytecode(Vector<uint8_t> p_buffer, bool print_verbose) {
	int p_token_max = 0;
	int p_func_max = 0;
	return _test_bytecode(p_buffer, p_token_max, p_func_max, print_verbose);
}

bool is_whitespace_or_ignorable(GDScriptDecomp::GlobalToken p_token) {
	switch (p_token) {
		case GDScriptDecomp::G_TK_INDENT:
		case GDScriptDecomp::G_TK_DEDENT:
		case GDScriptDecomp::G_TK_NEWLINE:
		case GDScriptDecomp::G_TK_CURSOR:
			return true;
		default:
			return false;
	}
}

bool GDScriptDecomp::check_prev_token(int p_pos, const Vector<uint32_t> &p_tokens, GlobalToken p_token) {
	if (p_pos == 0) {
		return false;
	}
	return get_global_token(p_tokens[p_pos - 1]) == p_token;
}

bool GDScriptDecomp::check_next_token(int p_pos, const Vector<uint32_t> &p_tokens, GlobalToken p_token) {
	if (p_pos + 1 >= p_tokens.size()) {
		return false;
	}
	return get_global_token(p_tokens[p_pos + 1]) == p_token;
}

bool GDScriptDecomp::is_token_func_call(int p_pos, const Vector<uint32_t> &p_tokens) {
	if (p_pos > 0 && get_global_token(p_tokens[p_pos - 1]) == G_TK_PR_FUNCTION) {
		return false;
	}
	// Godot 3.x's parser was VERY DUMB and emitted built-in function tokens for any identifier that shared
	// the same name as a built-in function, so we have to check if the next token is a parenthesis open
	if (p_pos + 1 >= p_tokens.size() || get_global_token(p_tokens[p_pos + 1]) != G_TK_PARENTHESIS_OPEN) {
		return false;
	}
	return true;
}

bool GDScriptDecomp::is_token_builtin_func(int p_pos, const Vector<uint32_t> &p_tokens) {
	auto curr_token = get_global_token(p_tokens[p_pos]);
	// TODO: Handle TK_PR_ASSERT, TK_PR_YIELD, TK_PR_SYNC, TK_PR_MASTER, TK_PR_SLAVE, TK_PR_PUPPET, TK_PR_REMOTESYNC, TK_PR_MASTERSYNC, TK_PR_PUPPETSYNC, TK_PR_SLAVESYNC
	switch (curr_token) {
		case G_TK_BUILT_IN_FUNC:
			break;
		default:
			return false;
	}
	// If the previous token is a period, then this is a member function call, not a built-in function call
	if (p_pos > 0 && get_global_token(p_tokens[p_pos - 1]) == G_TK_PERIOD) {
		return false;
	}
	return is_token_func_call(p_pos, p_tokens);
}

GDScriptDecomp::BytecodeTestResult GDScriptDecomp::_test_bytecode(Vector<uint8_t> p_buffer, int &r_tok_max, int &r_func_max, bool print_verbosely) {
#define ERR_TEST_FAILED(x)                                                \
	error_message = "Line " + String::num_int64(line) + ": " + String(x); \
	if (print_verbosely) {                                                \
		print_failed_verbose_func(x);                                     \
	}                                                                     \
	return BytecodeTestResult::BYTECODE_TEST_FAIL;

#define SIZE_CHECK(x)                                                     \
	if (i + x >= tokens.size()) {                                         \
		ERR_TEST_FAILED("Size check failed: " + itos(i) + " " + itos(x)); \
	}

	error_message = "";

	int line = 0;
	auto print_failed_verbose_func = [&](const String &p_str) {
		String prefix = vformat("Bytecode test for %s (%07x) failed on line %d: ", get_engine_version(), get_bytecode_rev(), line);
		print_line(prefix + p_str);
	};

	auto get_builtin_name = [&](int p_id) -> String {
		return p_id == -1 ? "preload" : get_function_name(p_id);
	};

	ScriptState script_state;
	//Load bytecode
	Error err = get_script_state(p_buffer, script_state);
	if (err) {
		if (print_verbosely) {
			print_failed_verbose_func("Failed to get identifiers, constants, and tokens");
		}
		return BytecodeTestResult::BYTECODE_TEST_CORRUPT;
	}
	Vector<uint32_t> &tokens = script_state.tokens;
	HashMap<uint32_t, uint32_t> &lines = script_state.lines;
	int version = script_state.bytecode_version;
	int bytecode_version = get_bytecode_version();
	int FUNC_MAX = get_function_count();

	if (version != bytecode_version) {
		ERR_TEST_FAILED("Bytecode version mismatch: " + itos(version) + " != " + itos(bytecode_version));
	}

	ERR_FAIL_COND_V_MSG(err != OK, BYTECODE_TEST_CORRUPT, "Failed to get identifiers, constants, and tokens");
	auto get_line_func([&](int i) {
		if (lines.has(i)) {
			return lines[i];
		}
		if (script_state.end_lines.has(i)) {
			return script_state.end_lines[i];
		}
		return 0U;
	});

	// reserved words can be used as member accessors in all versions of GDScript, and used as function names in GDScript 1.0
	auto is_not_actually_reserved_word = [&](int i) {
		return (check_prev_token(i, tokens, G_TK_PERIOD) ||
				(bytecode_version < GDSCRIPT_2_0_VERSION &&
						(check_prev_token(i, tokens, G_TK_PR_FUNCTION) ||
								is_token_func_call(i, tokens))));
	};

	for (int i = 0; i < tokens.size(); i++) {
		r_tok_max = MAX(r_tok_max, static_cast<int>(tokens[i] & TOKEN_MASK));
		GlobalToken curr_token = get_global_token(tokens[i]);
		Pair<int, int> arg_count;
		bool test_func = false;
		int func_id = -1;
		int cur_line = get_line_func(i);
		if (cur_line != 0) {
			line = cur_line;
		}

		// All of these assumptions should apply for all bytecodes that we have support for
		switch (curr_token) {
			// Functions go like this for GDScript 1.0 scripts:
			// `func <literally_fucking_anything_resembling_an_identifier_including_keywords_and_built-in_funcs>(<arguments>)`
			case G_TK_PR_FUNCTION: {
				if (is_not_actually_reserved_word(i)) {
					break;
				}
				SIZE_CHECK(2);
				GlobalToken next_token = get_global_token(tokens[i + 1]);
				GlobalToken nextnext_token = get_global_token(tokens[i + 2]);
				// GDScript Version 2.0+ requires the next token to be a parenthesis open (lambdas) or an identifier
				if (bytecode_version >= GDSCRIPT_2_0_VERSION && next_token != G_TK_PARENTHESIS_OPEN && !token_is_valid_v2_func_id(next_token)) {
					ERR_TEST_FAILED(vformat("Function declaration error: %s %s (expected %s [%s or %s])", g_token_str[curr_token], g_token_str[next_token], g_token_str[G_TK_PR_FUNCTION], g_token_str[G_TK_PARENTHESIS_OPEN], g_token_str[G_TK_IDENTIFIER]));
				}
				if (nextnext_token != G_TK_PARENTHESIS_OPEN && (bytecode_version < GDSCRIPT_2_0_VERSION || next_token != G_TK_PARENTHESIS_OPEN)) {
					ERR_TEST_FAILED(vformat("Function declaration error: %s %s %s (expected %s <identifier> %s)", g_token_str[curr_token], g_token_str[next_token], g_token_str[nextnext_token], g_token_str[G_TK_PR_FUNCTION], g_token_str[G_TK_PARENTHESIS_OPEN]));
				}
			} break;
			case G_TK_CF_PASS: {
				if (is_not_actually_reserved_word(i)) {
					break;
				}
				if (bytecode_version < GDSCRIPT_2_0_VERSION) {
					// next token has to be EOF, semicolon, or newline
					SIZE_CHECK(1);
					// function declaration with the same name
					if (check_prev_token(i, tokens, G_TK_PR_FUNCTION) && check_next_token(i, tokens, G_TK_PARENTHESIS_OPEN)) {
						break;
					}

					GlobalToken next_token = get_global_token(tokens[i + 1]);
					if (next_token != G_TK_NEWLINE && next_token != G_TK_SEMICOLON && next_token != G_TK_EOF) {
						ERR_TEST_FAILED(String("Pass statement error, next token isn't newline, semicolon, or EOF: ") + g_token_str[next_token]);
					}
				}
			} break;
			case G_TK_PR_STATIC: {
				if (is_not_actually_reserved_word(i)) {
					break;
				}

				SIZE_CHECK(1);
				// STATIC requires TK_PR_FUNCTION as the next token (GDScript 2.0 also allows TK_PR_VAR)
				GlobalToken next_token = get_global_token(tokens[i + 1]);
				if (next_token != G_TK_PR_FUNCTION && (bytecode_version < GDSCRIPT_2_0_VERSION || next_token != G_TK_PR_VAR)) {
					ERR_TEST_FAILED(String("Static declaration error, next token isn't function or var: ") + g_token_str[next_token]);
				}
			} break;
			case G_TK_PR_ENUM: { // not added until 2.1.3, but valid for all versions after
				if (is_not_actually_reserved_word(i)) {
					break;
				}

				SIZE_CHECK(1);
				// ENUM requires TK_IDENTIFIER or TK_CURLY_BRACKET_OPEN as the next token
				GlobalToken next_token = get_global_token(tokens[i + 1]);
				if (next_token != G_TK_IDENTIFIER && next_token != G_TK_CURLY_BRACKET_OPEN) {
					ERR_TEST_FAILED(String("Enum declaration error, next token isn't identifier or curly bracket open: ") + g_token_str[next_token]);
				}
			} break;
			case G_TK_BUILT_IN_FUNC: {
				if (!is_token_builtin_func(i, tokens)) {
					break;
				}

				func_id = tokens[i] >> TOKEN_BITS;
				r_func_max = MAX(r_func_max, func_id);
				if (func_id >= FUNC_MAX) {
					ERR_TEST_FAILED("Function ID out of range: " + itos(func_id) + " >= " + itos(FUNC_MAX));
				}
				arg_count = get_function_arg_count(func_id);
				test_func = true;
			} break;
			// TODO: handle YIELD, ASSERT
			case G_TK_PR_PRELOAD: // Preload is like a function with 1 argument
				// You can declare reserved words like `preload` as functions in GDScript 1.0, but you can't actually call them with the incorrect number of arguments
				if (check_prev_token(i, tokens, G_TK_PERIOD) ||
						(get_bytecode_version() < GDSCRIPT_2_0_VERSION && check_prev_token(i, tokens, G_TK_PR_FUNCTION))) {
					break;
				}
				arg_count = { 1, 1 };
				test_func = true;
				break;
			case G_TK_CURSOR:
			case G_TK_MAX:
				ERR_TEST_FAILED("Invalid token: " + String(g_token_str[curr_token]));
				return BytecodeTestResult::BYTECODE_TEST_FAIL;
			case G_TK_EOF: {
				if (tokens.size() != i + 1) {
					ERR_TEST_FAILED("Found EOF token not at end of tokens");
				}
			} break;
			default:
				if (curr_token > G_TK_MAX) {
					ERR_TEST_FAILED("Token > G_TK_MAX: " + itos(curr_token));
				}
				break;
		}
		if (test_func) { // we're at the function identifier, so check the argument count
			if (i + 2 >= tokens.size()) { // should at least have two more tokens for `()` after the function identifier
				ERR_TEST_FAILED(vformat("Built-in call '%s' error, not enough tokens following", get_builtin_name(func_id)));
			}
			if (curr_token == G_TK_PR_PRELOAD || bytecode_version < GDSCRIPT_2_0_VERSION) {
				Vector<Vector<uint32_t>> r_arguments;
				int cnt = get_func_arg_count_and_params(i, tokens, r_arguments);
				if (cnt < arg_count.first || cnt > arg_count.second) {
					ERR_TEST_FAILED(vformat("Built-in call '%s' error, incorrect number of arguments %d (min: %d, max %d)", get_builtin_name(func_id), cnt, arg_count.first, arg_count.second));
				}
			}
		}
	}

	return BYTECODE_TEST_PASS;
#undef SIZE_CHECK
#undef ERR_TEST_FAILED
#undef FAILED_PRINT
}

// We're at the identifier token
int GDScriptDecomp::get_func_arg_count_and_params(int curr_pos, const Vector<uint32_t> &tokens, Vector<Vector<uint32_t>> &r_arguments) {
	if (curr_pos + 2 >= tokens.size()) {
		return -1;
	}
	GlobalToken t = get_global_token(tokens[curr_pos + 1]);
	if (t != G_TK_PARENTHESIS_OPEN) {
		return -1;
	}
	int bracket_open = 0;
	// we should be just past the open parenthesis
	int pos = curr_pos + 2;

	Vector<uint32_t> curr_arg;
	// count the commas
	// at least in 3.x and below, the only time commas are allowed in function args are other expressions
	// This test is not applicable to GDScript 2.0 versions, as there are no bytecode-specific built-in functions.
	for (; pos < tokens.size(); pos++) {
		t = get_global_token(tokens[pos]);
		switch (t) {
			case G_TK_BRACKET_OPEN:
			case G_TK_CURLY_BRACKET_OPEN:
			case G_TK_PARENTHESIS_OPEN:
				bracket_open++;
				break;
			case G_TK_BRACKET_CLOSE:
			case G_TK_CURLY_BRACKET_CLOSE:
			case G_TK_PARENTHESIS_CLOSE:
				bracket_open--;
				break;
			case G_TK_COMMA:
				if (bracket_open == 0) {
					r_arguments.push_back(curr_arg);
				}
				break;
			default:
				break;
		}
		if (bracket_open == -1) {
			if (curr_arg.size() > 0) {
				r_arguments.push_back(curr_arg);
			}
			break;
		}
		if (!is_whitespace_or_ignorable(t)) {
			curr_arg.push_back(tokens[pos]);
		}
	}
	// trailing commas are not allowed after the last argument
	if (pos == tokens.size() || t != G_TK_PARENTHESIS_CLOSE) {
		r_arguments.clear();
		error_message = "Did not find close parenthesis before EOF";
		return -1;
	}
	return r_arguments.size();
}
Ref<GodotVer> GDScriptDecomp::get_godot_ver() const {
	return GodotVer::parse(get_engine_version());
}
Ref<GodotVer> GDScriptDecomp::get_max_godot_ver() const {
	auto max_ver = get_max_engine_version();
	if (max_ver.is_empty()) {
		return nullptr;
	}
	return GodotVer::parse(max_ver);
}
//	Error get_script_strings(Vector<String> &r_strings, bool include_identifiers = false);
//	Error get_script_strings(const Vector<uint8_t> &p_buffer, Vector<String> &r_strings, bool include_identifiers = false);

Error GDScriptDecomp::get_script_strings(const String &p_path, int bytecode_revision, Vector<String> &r_strings, bool p_include_identifiers) {
	Vector<uint8_t> p_buffer;
	Error err = OK;
	auto decomp = GDScriptDecomp::create_decomp_for_commit(bytecode_revision);
	if (decomp.is_null()) {
		return ERR_INVALID_PARAMETER;
	}
	if (p_path.get_extension().to_lower() == "gde") {
		err = get_buffer_encrypted(p_path, 3, GDRESettings::get_singleton()->get_encryption_key(), p_buffer);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Error reading encrypted file: " + p_path);
	} else if (p_path.get_extension().to_lower() == "gd") {
		String text = FileAccess::get_file_as_string(p_path, &err);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Error reading file: " + p_path);
		p_buffer = decomp->compile_code_string(text);
		ERR_FAIL_COND_V_MSG(p_buffer.size() == 0, ERR_PARSE_ERROR, "Error compiling code: " + p_path);
	} else {
		p_buffer = FileAccess::get_file_as_bytes(p_path, &err);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Error reading file: " + p_path);
	}
	return decomp->get_script_strings_from_buf(p_buffer, r_strings, p_include_identifiers);
}

Error GDScriptDecomp::get_script_strings_from_buf(const Vector<uint8_t> &p_buffer, Vector<String> &r_strings, bool p_include_identifiers) {
	ScriptState script_state;
	Error err = get_script_state(p_buffer, script_state);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Error parsing bytecode");
	const String engine_version = get_engine_version();
	for (int i = 0; i < script_state.constants.size(); i++) {
		gdre::get_strings_from_variant(script_state.constants[i], r_strings, engine_version);
	}
	if (p_include_identifiers) {
		for (int i = 0; i < script_state.identifiers.size(); i++) {
			r_strings.push_back(script_state.identifiers[i]);
		}
	}
	return OK;
}

Vector<String> GDScriptDecomp::get_compile_errors(const Vector<uint8_t> &p_buffer) {
	int bytecode_version = get_bytecode_version();
	const uint8_t *buf = p_buffer.ptr();
	ERR_FAIL_COND_V_MSG(p_buffer.size() < 24 || p_buffer[0] != 'G' || p_buffer[1] != 'D' || p_buffer[2] != 'S' || p_buffer[3] != 'C', Vector<String>(), "Corrupt bytecode");

	int version = decode_uint32(&buf[4]);
	ERR_FAIL_COND_V_MSG(version != bytecode_version, Vector<String>(), "Bytecode version mismatch");
	ScriptState state;
	Error err = get_script_state(p_buffer, state);
	Vector<Variant> &constants = state.constants;
	HashMap<uint32_t, uint32_t> &lines = state.lines;
	Vector<uint32_t> &tokens = state.tokens;

	ERR_FAIL_COND_V_MSG(err != OK, Vector<String>(), "Error parsing bytecode");
	Vector<String> errors;
	uint32_t prev_line = 1;
	auto push_error([&](const String &p_error) {
		errors.push_back(vformat("Line %d: %s", prev_line, p_error));
	});

	for (int64_t i = 0; i < tokens.size(); i++) {
		GlobalToken curr_token = get_global_token(tokens[i]);
		if (lines.has(i)) {
			if (lines[i] != prev_line && lines[i] != 0) {
				prev_line = lines[i];
			}
		}
		switch (curr_token) {
			case G_TK_ERROR: {
				String error = get_constant_string(constants, tokens[i] >> TOKEN_BITS);
				push_error(error);
			} break;
			case G_TK_CURSOR: {
				push_error("Cursor token found");
			} break;
			case G_TK_MAX: {
				push_error("Max token found");
			} break;
			case G_TK_EOF: {
				push_error("EOF token found");
			} break;
			default:
				break;
		}
	}
	return errors;
}

bool GDScriptDecomp::check_compile_errors(const Vector<uint8_t> &p_buffer) {
	Vector<String> errors = get_compile_errors(p_buffer);
	if (errors.size() > 0) {
		error_message = "Compile errors:\n";
		for (int i = 0; i < errors.size(); i++) {
			error_message += errors[i];
			if (i < errors.size() - 1) {
				error_message += "\n";
			}
		}
	}
	return errors.size() > 0 || !error_message.is_empty();
}

Vector<uint8_t> GDScriptDecomp::compile_code_string(const String &p_code) {
	error_message = "";
	if (get_bytecode_version() >= GDSCRIPT_2_0_VERSION) {
		auto buf = GDScriptV2TokenizerBufferCompat::parse_code_string(p_code, this, GDScriptV2TokenizerBufferCompat::CompressMode::COMPRESS_ZSTD);
		GDSDECOMP_FAIL_COND_V_MSG(buf.size() == 0, Vector<uint8_t>(), "Error parsing code");
		if (check_compile_errors(buf)) {
			return Vector<uint8_t>();
		}
		return buf;
	}
	Vector<uint8_t> buf;

	RBMap<StringName, int> identifier_map;
	HashMap<Variant, int> constant_map;
	RBMap<uint32_t, int> line_map;
	Vector<uint32_t> token_array;

	// compat: from 3.0 - 3.1.1, the tokenizer defaulted to storing full objects
	// e61a074, Mar 28, 2019
	Ref<GodotVer> NO_FULL_OBJ_VER = GodotVer::create(3, 2, 0, "dev1");
	Ref<GodotVer> godot_ver = get_godot_ver();
	bool encode_full_objects = godot_ver->lt(NO_FULL_OBJ_VER) && NO_FULL_OBJ_VER->get_major() == godot_ver->get_major();
	GDScriptV1TokenizerTextCompat tt(this);
	tt.set_code(p_code);
	int line = -1;

	while (true) {
		if (tt.get_token_line() != line) {
			line = tt.get_token_line();
			line_map[line] = token_array.size();
		}
		const GlobalToken g_token = tt.get_token();
		uint32_t local_token = get_local_token_val(g_token);
		switch (g_token) {
			case G_TK_IDENTIFIER: {
				StringName id = tt.get_token_identifier();
				if (!identifier_map.has(id)) {
					int idx = identifier_map.size();
					identifier_map[id] = idx;
				}
				local_token |= identifier_map[id] << TOKEN_BITS;
			} break;
			case G_TK_CONSTANT: {
				const Variant &c = tt.get_token_constant();
				if (!constant_map.has(c)) {
					int idx = constant_map.size();
					constant_map[c] = idx;
				}
				local_token |= constant_map[c] << TOKEN_BITS;
			} break;
			case G_TK_BUILT_IN_TYPE: {
				Variant::Type type = tt.get_token_type();
				int local_type = VariantDecoderCompat::convert_variant_type_to_old(type, get_variant_ver_major());
				local_token |= local_type << TOKEN_BITS;
			} break;
			case G_TK_BUILT_IN_FUNC: {
				// built-in function already has correct value
				local_token |= tt.get_token_built_in_func() << TOKEN_BITS;
			} break;
			case G_TK_NEWLINE: {
				local_token |= tt.get_token_line_indent() << TOKEN_BITS;
			} break;
			case G_TK_ERROR: {
				String err = tt.get_token_error();
				GDSDECOMP_FAIL_V_MSG(Vector<uint8_t>(), vformat("Compile error, line %d: %s", tt.get_token_line(), err));
			} break;
			default: {
			}
		};

		token_array.push_back(local_token);

		if (tt.get_token() == G_TK_EOF) {
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
	encode_uint32(get_bytecode_version(), &buf.write[4]);
	int content_start = 8;
	if (get_bytecode_version() >= GDSCRIPT_2_0_VERSION) {
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
		Error err = VariantDecoderCompat::encode_variant_compat(get_variant_ver_major(), E->get(), nullptr, len, encode_full_objects);
		GDSDECOMP_FAIL_COND_V_MSG(err != OK, Vector<uint8_t>(), "Error when trying to encode Variant.");
		int pos = buf.size();
		buf.resize_initialized(pos + len);
		VariantDecoderCompat::encode_variant_compat(get_variant_ver_major(), E->get(), &buf.write[pos], len, encode_full_objects);
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

Vector<String> GDScriptDecomp::get_bytecode_versions() {
	auto vers = GDScriptDecompVersion::get_decomp_versions();
	Vector<String> ret;
	for (auto &v : vers) {
		ret.push_back(v.name);
	}
	return ret;
}

// static Ref<GDScriptDecomp> create_decomp_for_version(String ver);
Ref<GDScriptDecomp> GDScriptDecomp::create_decomp_for_version(String str_ver, bool p_force) {
	bool include_dev = false;
	Ref<GodotVer> ver = GodotVer::parse(str_ver);
	ERR_FAIL_COND_V_MSG(ver.is_null() || ver->get_major() == 0, Ref<GDScriptDecomp>(), "Invalid version: " + str_ver);
	if (ver->get_prerelease().contains("dev")) {
		include_dev = true;
	}
	auto versions = GDScriptDecompVersion::get_decomp_versions(include_dev, ver->get_major());
	versions.reverse();
	// Exact match for dev versions
	if (include_dev) {
		str_ver = ver->as_tag();
		for (auto &v : versions) {
			bool has_max = !v.max_version.is_empty();
			if (v.min_version == str_ver || (has_max && v.max_version == str_ver)) {
				return Ref<GDScriptDecomp>(create_decomp_for_commit(v.commit));
			}
		}
		ERR_FAIL_COND_V_MSG(include_dev, Ref<GDScriptDecomp>(), "No version found for: " + str_ver);
	}
	if (p_force && ver->get_major() == 4 && ver->get_minor() < 3 && (ver->get_minor() != 0 || !ver->is_prerelease())) {
		return Ref<GDScriptDecomp>(create_decomp_for_version("4.3.0"));
	}
	Ref<GodotVer> prev_ver = nullptr;
	int prev_ver_commit = 0;
	for (auto &curr_version : versions) {
		Ref<GodotVer> min_ver = curr_version.get_min_version();
		if (ver->eq(min_ver)) {
			return Ref<GDScriptDecomp>(create_decomp_for_commit(curr_version.commit));
		}

		if (ver->gt(min_ver) && !curr_version.max_version.is_empty()) {
			Ref<GodotVer> max_ver = curr_version.get_max_version();
			if (ver->lte(max_ver)) {
				return Ref<GDScriptDecomp>(create_decomp_for_commit(curr_version.commit));
			}
		}
		if (prev_ver_commit > 0 && ver->lt(min_ver) && ver->gte(prev_ver)) {
			// 3.4 -> 3.2
			if (ver->get_major() == prev_ver->get_major()) {
				return Ref<GDScriptDecomp>(create_decomp_for_commit(prev_ver_commit));
			}
		}
		prev_ver = min_ver;
		prev_ver_commit = curr_version.commit;
	}
	if (ver->get_major() == prev_ver->get_major() && ver->gte(prev_ver)) {
		return Ref<GDScriptDecomp>(create_decomp_for_commit(prev_ver_commit));
	}
	ERR_FAIL_V_MSG(Ref<GDScriptDecomp>(), "No version found for: " + str_ver);
}

template <typename T>
static int64_t continuity_tester(const Vector<T> &p_vector, const Vector<T> &p_other, String name, int pos = 0) {
	if (p_vector.is_empty() && p_other.is_empty()) {
		// return -1;
	}
	if (p_vector.is_empty() && !p_other.is_empty()) {
		// WARN_PRINT(name + " first is empty");
		return -1;
	}
	if (!p_vector.is_empty() && p_other.is_empty()) {
		// WARN_PRINT(name + " second is empty");
		return -1;
	}
	if (pos == 0) {
		if (p_vector.size() != p_other.size()) {
			// WARN_PRINT(name + " size mismatch: " + itos(p_vector.size()) + " != " + itos(p_other.size()));
		}
	}
	if (pos >= p_vector.size() || pos >= p_other.size()) {
		// WARN_PRINT(name + " pos out of range");
		return MIN(p_vector.size(), p_other.size());
	}
	for (int i = pos; i < p_vector.size(); i++) {
		if (i >= p_other.size()) {
			// WARN_PRINT(name + " discontinuity at index " + itos(i));
			return i;
		}
		if (p_vector[i] != p_other[i]) {
			// WARN_PRINT(name + " discontinuity at index " + itos(i));
			return i;
		}
	}
	return -1;
}

template <typename K, typename V>
static int64_t continuity_tester(const HashMap<K, V> &p_vector, const HashMap<K, V> &p_other, String name, int pos = 0) {
	if (p_vector.is_empty() && p_other.is_empty()) {
		return -1;
	}
	if (p_vector.is_empty() && !p_other.is_empty()) {
		// WARN_PRINT(name + " first is empty");
		return -1;
	}
	if (!p_vector.is_empty() && p_other.is_empty()) {
		// WARN_PRINT(name + " second is empty");
		return -1;
	}
	if (pos == 0) {
		if (p_vector.size() != p_other.size()) {
			// WARN_PRINT(name + " size mismatch: " + itos(p_vector.size()) + " != " + itos(p_other.size()));
		}
	}
	if (pos >= static_cast<int>(p_vector.size()) || pos >= static_cast<int>(p_other.size())) {
		// WARN_PRINT(name + " pos out of range");
		return MIN(static_cast<int>(p_vector.size()), static_cast<int>(p_other.size()));
	}

	Vector<Pair<K, V>> p_vector_arr;
	Vector<Pair<K, V>> p_other_arr;

	for (auto &it : p_vector) {
		p_vector_arr.push_back(Pair<K, V>(it.key, it.value));
	}
	for (auto &it : p_other) {
		p_other_arr.push_back(Pair<K, V>(it.key, it.value));
	}

	for (int64_t i = pos; i < p_vector.size(); i++) {
		if (i >= p_other.size()) {
			// WARN_PRINT(name + " discontinuity at index " + itos(i));
			return i;
		}
		if (p_vector_arr[i].first != p_other_arr[i].first) {
			// WARN_PRINT(name + " bytecode discontinuity at index " + itos(i));
			return i;
		}
		if (p_vector_arr[i].second != p_other_arr[i].second) {
			// WARN_PRINT(name + " bytecode discontinuity at index " + itos(i));
			return i;
		}
	}
	return -1;
}

Error GDScriptDecomp::test_bytecode_match(const Vector<uint8_t> &p_buffer1, const Vector<uint8_t> &p_buffer2, bool ignore_lines_cols, bool is_printing_verbose) {
	int64_t discontinuity = -1;
	if (p_buffer1 == p_buffer2) {
		return OK;
	}
	ScriptState state1;
	Error error = get_script_state(p_buffer1, state1);
	ERR_FAIL_COND_V_MSG(error, error, "Error reading first bytecode");
	ScriptState state2;
	error = get_script_state(p_buffer2, state2);
	ERR_FAIL_COND_V_MSG(error, error, "Error reading second bytecode");
	Error err = OK;
#define REPORT_DIFF(x)         \
	err = ERR_BUG;             \
	error_message += x + "\n"; \
	// WARN_PRINT(x)

#ifdef DEBUG_ENABLED
#define bl_print(...) print_line(__VA_ARGS__)
#else
#define bl_print(...) print_verbose(__VA_ARGS__)
#endif

	if (state1.bytecode_version != state2.bytecode_version) {
		REPORT_DIFF("Bytecode version mismatch: " + itos(state1.bytecode_version) + " != " + itos(state2.bytecode_version));
		return ERR_BUG;
	}
	if (state1.bytecode_version < GDSCRIPT_2_0_VERSION) {
		discontinuity = continuity_tester(p_buffer1, p_buffer2, "Bytecode");
	} else {
		auto decompressed_size1 = decode_uint32(&p_buffer1[8]);
		auto decompressed_size2 = decode_uint32(&p_buffer2[8]);
		if (decompressed_size1 != decompressed_size2) {
			REPORT_DIFF("Decompressed size mismatch: " + itos(decompressed_size1) + " != " + itos(decompressed_size2));
		}
		Vector<uint8_t> contents1;
		Vector<uint8_t> contents2;
		decompress_buf(p_buffer1, contents1);
		decompress_buf(p_buffer2, contents2);
		discontinuity = continuity_tester(contents1, contents2, "Decompressed Bytecode");
	}
	if (discontinuity == -1) {
		return OK;
	}

	// Ref<GDScriptDecomp> decomp = create_decomp_for_commit(get_bytecode_rev());
	error_message = "";
	discontinuity = continuity_tester(state1.identifiers, state2.identifiers, "Identifiers");
	if (discontinuity != -1) {
		REPORT_DIFF("Discontinuity in identifier at index " + itos(discontinuity));
		if (discontinuity < state1.identifiers.size() && discontinuity < state2.identifiers.size()) {
			REPORT_DIFF("Different identifiers: " + state1.identifiers[discontinuity] + " != " + state2.identifiers[discontinuity]);
		} else {
			REPORT_DIFF("Different identifier sizes: " + itos(state1.identifiers.size()) + " != " + itos(state2.identifiers.size()));
		}
	}
	discontinuity = continuity_tester(state1.constants, state2.constants, "Constants");
	if (discontinuity != -1) {
		REPORT_DIFF("Discontinuity in constants at index " + itos(discontinuity));
		if (discontinuity < state1.constants.size() && discontinuity < state2.constants.size()) {
			REPORT_DIFF("Different constants: " + state1.constants[discontinuity].operator String() + " != " + state2.constants[discontinuity].operator String());
		} else {
			REPORT_DIFF("Different constant sizes: " + itos(state1.constants.size()) + " != " + itos(state2.constants.size()));
		}
	}
	auto old_tokens_size = state1.tokens.size();
	auto new_tokens_size = state2.tokens.size();
	discontinuity = continuity_tester(state1.tokens, state2.tokens, "Tokens");
	if (discontinuity == -1 && ignore_lines_cols && err == OK) {
		return OK;
	}

	auto get_token_name_plus_value = [&](const ScriptState &p_state, int token) {
		auto g_token = get_global_token(token);
		String name = GDScriptTokenizerCompat::get_token_name(g_token);
		if (g_token == G_TK_IDENTIFIER || g_token == G_TK_ANNOTATION) {
			auto identifier_idx = token >> TOKEN_BITS;
			if (identifier_idx < p_state.identifiers.size()) {
				name = "Identifier " + itos(identifier_idx) + " (" + p_state.identifiers[identifier_idx] + ")";
			} else {
				name = "Identifier " + itos(identifier_idx) + " (OUT OF RANGE)";
			}
		} else if (g_token == G_TK_CONSTANT) {
			auto constant_idx = token >> TOKEN_BITS;
			if (constant_idx < p_state.constants.size()) {
				name = "Constant " + itos(constant_idx) + " (" + p_state.constants[constant_idx].operator String() + ")";
			} else {
				name = "Constant " + itos(constant_idx) + " (OUT OF RANGE)";
			}
		} else if (g_token == G_TK_BUILT_IN_TYPE) {
			auto built_in_type_idx = token >> TOKEN_BITS;
			if (built_in_type_idx < Variant::VARIANT_MAX) {
				name = "Built-In Type " + itos(built_in_type_idx) + " (" + get_token_text(p_state, token) + ")";
			} else {
				name = "Built-In Type " + itos(built_in_type_idx) + " (OUT OF RANGE)";
			}
		}
		return name;
	};

	if (is_printing_verbose && discontinuity != -1 && discontinuity < new_tokens_size && discontinuity < old_tokens_size) {
		// go through and print the ALL the tokens, "oldtoken (val)  ==  state2.token  (val)"
		bl_print("***START TOKEN PRINT");
		for (int i = 0; i < old_tokens_size; i++) {
			if (i >= new_tokens_size) {
				bl_print(String("Different Token sizes: ") + itos(old_tokens_size) + String(" != ") + itos(new_tokens_size));
				break;
			}
			auto old_token = state1.tokens[i];
			auto new_token = state2.tokens[i];
			if (get_global_token(old_token) != get_global_token(new_token)) {
				bl_print(String("Different Tokens: ") + get_token_name_plus_value(state1, old_token) + String(" != ") + get_token_name_plus_value(state2, new_token));
			} else {
				String old_token_name = GDScriptTokenizerCompat::get_token_name(get_global_token(old_token));
				int old_token_val = old_token >> TOKEN_BITS;
				int new_token_val = new_token >> TOKEN_BITS;
				String old_token_text = get_token_text(state1, i);
				String new_token_text = get_token_text(state2, i);
				if (old_token_val != new_token_val) {
					bl_print(String("Different Token Val for ") +
							GDScriptTokenizerCompat::get_token_name(get_global_token(old_token)) + ":" +
							vformat("%s (%s) != %s (%s)", old_token_text, itos(old_token_val), new_token_text, itos(new_token_val)));
				} else {
					bl_print(String("Same Token Val for ") + get_token_name_plus_value(state1, old_token));
				}
			}
		}
		bl_print("***END TOKEN PRINT");
	}

	if (discontinuity != -1) {
		REPORT_DIFF("Discontinuity in tokens at index " + itos(discontinuity));
		while (discontinuity < state1.tokens.size() && discontinuity < state2.tokens.size() && discontinuity != -1) {
			auto old_token = state1.tokens[discontinuity];
			auto new_token = state2.tokens[discontinuity];
			String old_token_name = GDScriptTokenizerCompat::get_token_name(get_global_token(old_token));
			String new_token_name = GDScriptTokenizerCompat::get_token_name(get_global_token(new_token));
			if (old_token_name != new_token_name) {
				REPORT_DIFF(String("Different Tokens: ") + get_token_name_plus_value(state1, old_token) + String(" != ") + get_token_name_plus_value(state2, new_token));
			} else {
				int old_token_val = old_token >> TOKEN_BITS;
				int new_token_val = new_token >> TOKEN_BITS;
				if (old_token_val != new_token_val) {
					REPORT_DIFF(String("Different Token Val for ") + old_token_name + ":" + itos(old_token_val) + String(" != ") + itos(new_token_val));
				}
			}
			discontinuity = continuity_tester(state1.tokens, state2.tokens, "Tokens", discontinuity + 1);
		}
		if (state1.tokens.size() != state2.tokens.size()) {
			REPORT_DIFF("Different Token sizes: " + itos(state1.tokens.size()) + " != " + itos(state2.tokens.size()));
		}
	}

	auto do_vmap_thing = [&](const String &name, const HashMap<uint32_t, uint32_t> &map1, const HashMap<uint32_t, uint32_t> &map2) {
		auto lines_Size = map1.size();
		auto new_lines_Size = map2.size();
		discontinuity = continuity_tester(map1, map2, name);
		if (discontinuity != -1) {
			REPORT_DIFF(vformat("** %s DIFFER", name));
		}
		while (discontinuity != -1) {
			REPORT_DIFF(vformat("Discontinuity in %s at index %d", name, discontinuity));
			if (discontinuity < lines_Size && discontinuity < new_lines_Size) {
				Vector<Pair<uint32_t, uint32_t>> p_vector_arr;
				Vector<Pair<uint32_t, uint32_t>> p_other_arr;

				for (auto &it : map1) {
					p_vector_arr.push_back(Pair<uint32_t, uint32_t>(it.key, it.value));
				}
				for (auto &it : map2) {
					p_other_arr.push_back(Pair<uint32_t, uint32_t>(it.key, it.value));
				}

				auto pair1 = p_vector_arr[discontinuity];
				auto pair2 = p_other_arr[discontinuity];

				auto token_at_key1 = state1.tokens.size() > pair1.first ? get_global_token(state1.tokens[pair1.first]) : G_TK_MAX;
				auto token_at_key2 = state2.tokens.size() > pair2.first ? get_global_token(state2.tokens[pair2.first]) : G_TK_MAX;
				auto token_name1 = token_at_key1 <= G_TK_MAX ? g_token_str[token_at_key1] : "INVALID";
				auto token_name2 = token_at_key2 <= G_TK_MAX ? g_token_str[token_at_key2] : "INVALID";
				auto token1_line = state1.get_token_line(pair1.first);
				auto token2_line = state2.get_token_line(pair2.first);
				REPORT_DIFF(vformat("Different %s @ idx %d: Token %s (line %d): %d != Token %s (line %d) :%d", name, discontinuity, token_name1, token1_line, pair1.second, token_name2, token2_line, pair2.second));
			} else if (lines_Size != new_lines_Size) {
				REPORT_DIFF("Different Column sizes: " + itos(lines_Size) + " != " + itos(new_lines_Size));
				break;
			}
			auto new_discontinuity = continuity_tester(map1, map2, name, discontinuity + 1);
			if (new_discontinuity == discontinuity) {
				break;
			}
			discontinuity = new_discontinuity;
		}
	};

	do_vmap_thing("Lines", state1.lines, state2.lines);
	do_vmap_thing("Columns", state1.columns, state2.columns);
	do_vmap_thing("End Lines", state1.end_lines, state2.end_lines);
	return err;
}

bool GDScriptDecomp::token_is_keyword(GlobalToken p_token) {
	// all the PR and CF tokens, + G_TK_OP_IN, G_TK_OP_AND, G_TK_OP_OR, G_TK_OP_NOT
	switch (p_token) {
		case G_TK_OP_IN:
		case G_TK_OP_AND:
		case G_TK_OP_OR:
		case G_TK_OP_NOT:
		case G_TK_CF_IF:
		case G_TK_CF_ELIF:
		case G_TK_CF_ELSE:
		case G_TK_CF_FOR:
		case G_TK_CF_WHILE:
		case G_TK_CF_BREAK:
		case G_TK_CF_CONTINUE:
		case G_TK_CF_PASS:
		case G_TK_CF_RETURN:
		case G_TK_CF_MATCH:
		case G_TK_PR_FUNCTION:
		case G_TK_PR_CLASS:
		case G_TK_PR_CLASS_NAME:
		case G_TK_PR_EXTENDS:
		case G_TK_PR_IS:
		case G_TK_PR_ONREADY:
		case G_TK_PR_TOOL:
		case G_TK_PR_STATIC:
		case G_TK_PR_EXPORT:
		case G_TK_PR_SETGET:
		case G_TK_PR_CONST:
		case G_TK_PR_VAR:
		case G_TK_PR_AS:
		case G_TK_PR_VOID:
		case G_TK_PR_ENUM:
		case G_TK_PR_PRELOAD:
		case G_TK_PR_ASSERT:
		case G_TK_PR_YIELD:
		case G_TK_PR_SIGNAL:
		case G_TK_PR_BREAKPOINT:
		case G_TK_PR_REMOTE:
		case G_TK_PR_SYNC:
		case G_TK_PR_MASTER:
		case G_TK_PR_SLAVE:
		case G_TK_PR_PUPPET:
		case G_TK_PR_REMOTESYNC:
		case G_TK_PR_MASTERSYNC:
		case G_TK_PR_PUPPETSYNC:
		case G_TK_PR_SLAVESYNC:
		case G_TK_CF_DO:
		case G_TK_CF_CASE:
		case G_TK_CF_SWITCH:
		case G_TK_CF_WHEN:
		case G_TK_PR_AWAIT:
		case G_TK_PR_NAMESPACE:
		case G_TK_PR_SUPER:
		case G_TK_PR_TRAIT:
		case G_TK_ABSTRACT:
		case G_TK_SELF:
		case G_TK_CONST_PI:
		case G_TK_CONST_TAU:
		case G_TK_CONST_INF:
		case G_TK_CONST_NAN:
			return true;
		default:
			return false;
	}
}

bool GDScriptDecomp::token_is_valid_v2_func_id(GlobalToken p_token) {
	switch (p_token) {
		case G_TK_IDENTIFIER:
		case G_TK_CF_MATCH: // For some godforsaken reason, the 4.x parser allows "match" and "when" as function identifiers
		case G_TK_CF_WHEN:
			return true;
		default:
			return false;
	}
}

bool GDScriptDecomp::token_is_control_flow_keyword(GlobalToken p_token) {
	switch (p_token) {
		case G_TK_CF_IF:
		case G_TK_CF_ELIF:
		case G_TK_CF_ELSE:
		case G_TK_CF_FOR:
		case G_TK_CF_WHILE:
		case G_TK_CF_BREAK:
		case G_TK_CF_CONTINUE:
		case G_TK_CF_PASS:
		case G_TK_CF_RETURN:
		case G_TK_CF_MATCH:
		case G_TK_CF_WHEN:
		case G_TK_CF_DO:
		case G_TK_CF_CASE:
		case G_TK_CF_SWITCH:
			return true;
		default:
			return false;
	}
}

bool GDScriptDecomp::token_is_constant(GlobalToken p_token) {
	switch (p_token) {
		case G_TK_CONST_PI:
		case G_TK_CONST_TAU:
		case G_TK_CONST_INF:
		case G_TK_CONST_NAN:
			return true;
		default:
			return false;
	}
}

bool GDScriptDecomp::token_is_operator_keyword(GlobalToken p_token) {
	switch (p_token) {
		case G_TK_OP_IN:
		case G_TK_OP_AND:
		case G_TK_OP_OR:
		case G_TK_OP_NOT:
			return true;
		default:
			return false;
	}
}

bool GDScriptDecomp::token_is_keyword_called_like_function(GlobalToken p_token) {
	switch (p_token) {
		case G_TK_PR_PRELOAD:
		case G_TK_PR_ASSERT:
		case G_TK_PR_YIELD:
		case G_TK_PR_AWAIT:
		case G_TK_PR_NAMESPACE:
		case G_TK_PR_SUPER:
			return true;
		default:
			return false;
	}
}

String GDScriptDecomp::get_global_token_name(GlobalToken p_token) {
	return GDScriptTokenizerCompat::get_token_name(p_token);
}

String GDScriptDecomp::get_token_text(const ScriptState &p_script_state, uint32_t i) {
	if (i >= p_script_state.tokens.size()) {
		return "ERROR: Invalid token index";
	}
	uint32_t local_token = p_script_state.tokens[i] & TOKEN_MASK;
	GlobalToken token_id = get_global_token(local_token);
	uint32_t token_val = p_script_state.tokens[i] >> TOKEN_BITS;
	switch (token_id) {
		case G_TK_EMPTY: {
			return "";
		} break;
		case G_TK_ANNOTATION: // fallthrough
		case G_TK_IDENTIFIER: {
			if (token_val >= (uint32_t)p_script_state.identifiers.size()) {
				return "ERROR: Invalid identifier index";
			}
			return String(p_script_state.identifiers[token_val]);
		} break;
		case G_TK_CONSTANT: {
			if (token_val >= (uint32_t)p_script_state.constants.size()) {
				return "ERROR: Invalid constant index";
			}
			return get_constant_string(p_script_state.constants, token_val);
		} break;
		case G_TK_BUILT_IN_TYPE: {
			return VariantDecoderCompat::get_variant_type_name(token_val, get_variant_ver_major());
		} break;
		case G_TK_BUILT_IN_FUNC: {
			if (token_val >= static_cast<uint32_t>(get_function_count())) {
				return "ERROR: Invalid function index";
			}
			return get_function_name(token_val);
		} break;
		default:
			break;
	}
	return get_global_token_text(token_id);
}

String GDScriptDecomp::get_global_token_text(GlobalToken p_token_id) {
	switch (p_token_id) {
		case G_TK_EMPTY: {
			//skip
			return "";
		} break;
		case G_TK_ANNOTATION: // fallthrough
		case G_TK_IDENTIFIER: {
			// uint32_t identifier = tokens[i] >> TOKEN_BITS;
			// if (identifier >= (uint32_t)identifiers.size()) {
			// 	return "ERROR: Invalid identifier index";
			// }
			// return String(identifiers[identifier]);
			return "";
		} break;
		case G_TK_CONSTANT: {
			// uint32_t constant = tokens[i] >> TOKEN_BITS;
			// if (constant >= (uint32_t)constants.size()) {
			// 	return "ERROR: Invalid constant index";
			// }
			// // TODO: handle GDScript 2.0 multi-line strings: we have to check the number of newlines
			// // in the string and if the next token has a line number difference >= the number of newlines
			// return get_constant_string(constants, constant);
			return "";
		} break;
		case G_TK_BUILT_IN_TYPE: {
			// line += VariantDecoderCompat::get_variant_type_name(tokens[i] >> TOKEN_BITS, variant_ver_major);
			return "";
		} break;
		case G_TK_BUILT_IN_FUNC: {
			// GDSDECOMP_FAIL_COND_V(tokens[i] >> TOKEN_BITS >= FUNC_MAX, ERR_INVALID_DATA);
			// line += get_function_name(tokens[i] >> TOKEN_BITS);
			return "";
		} break;

		case G_TK_SELF: {
			return "self";
		} break;
		case G_TK_OP_IN: {
			return "in";
		} break;
		case G_TK_OP_EQUAL: {
			return "==";
		} break;
		case G_TK_OP_NOT_EQUAL: {
			return "!=";
		} break;
		case G_TK_OP_LESS: {
			return "<";
		} break;
		case G_TK_OP_LESS_EQUAL: {
			return "<=";
		} break;
		case G_TK_OP_GREATER: {
			return ">";
		} break;
		case G_TK_OP_GREATER_EQUAL: {
			return ">=";
		} break;
		case G_TK_OP_AND: {
			return "and";
		} break;
		case G_TK_OP_OR: {
			return "or";
		} break;
		case G_TK_OP_NOT: {
			return "not";
		} break;
		case G_TK_OP_ADD: {
			return "+";
		} break;
		case G_TK_OP_SUB: {
			return "-";
			//TODO: do not add space after unary "-"
		} break;
		case G_TK_OP_MUL: {
			return "*";
		} break;
		case G_TK_OP_DIV: {
			return "/";
		} break;
		case G_TK_OP_MOD: {
			return "%";
		} break;
		case G_TK_OP_SHIFT_LEFT: {
			return "<<";
		} break;
		case G_TK_OP_SHIFT_RIGHT: {
			return ">>";
		} break;
		case G_TK_OP_ASSIGN: {
			return "=";
		} break;
		case G_TK_OP_ASSIGN_ADD: {
			return "+=";
		} break;
		case G_TK_OP_ASSIGN_SUB: {
			return "-=";
		} break;
		case G_TK_OP_ASSIGN_MUL: {
			return "*=";
		} break;
		case G_TK_OP_ASSIGN_DIV: {
			return "/=";
		} break;
		case G_TK_OP_ASSIGN_MOD: {
			return "%=";
		} break;
		case G_TK_OP_ASSIGN_SHIFT_LEFT: {
			return "<<=";
		} break;
		case G_TK_OP_ASSIGN_SHIFT_RIGHT: {
			return ">>=";
		} break;
		case G_TK_OP_ASSIGN_BIT_AND: {
			return "&=";
		} break;
		case G_TK_OP_ASSIGN_BIT_OR: {
			return "|=";
		} break;
		case G_TK_OP_ASSIGN_BIT_XOR: {
			return "^=";
		} break;
		case G_TK_OP_BIT_AND: {
			return "&";
		} break;
		case G_TK_OP_BIT_OR: {
			return "|";
		} break;
		case G_TK_OP_BIT_XOR: {
			return "^";
		} break;
		case G_TK_OP_BIT_INVERT: {
			return "~";
		} break;
		//case G_TK_OP_PLUS_PLUS: {
		//	return "++";
		//} break;
		//case G_TK_OP_MINUS_MINUS: {
		//	return "--";
		//} break;
		case G_TK_CF_IF: {
			return "if";
		} break;
		case G_TK_CF_ELIF: {
			return "elif";
		} break;
		case G_TK_CF_ELSE: {
			return "else";
		} break;
		case G_TK_CF_FOR: {
			return "for";
		} break;
		case G_TK_CF_WHILE: {
			return "while";
		} break;
		case G_TK_CF_BREAK: {
			return "break";
		} break;
		case G_TK_CF_CONTINUE: {
			return "continue";
		} break;
		case G_TK_CF_PASS: {
			return "pass";
		} break;
		case G_TK_CF_RETURN: {
			return "return";
		} break;
		case G_TK_CF_MATCH: {
			return "match";
		} break;
		case G_TK_PR_FUNCTION: {
			return "func";
		} break;
		case G_TK_PR_CLASS: {
			return "class";
		} break;
		case G_TK_PR_CLASS_NAME: {
			return "class_name";
		} break;
		case G_TK_PR_EXTENDS: {
			return "extends";
		} break;
		case G_TK_PR_IS: {
			return "is";
		} break;
		case G_TK_PR_ONREADY: {
			return "onready";
		} break;
		case G_TK_PR_TOOL: {
			return "tool";
		} break;
		case G_TK_PR_STATIC: {
			return "static";
		} break;
		case G_TK_PR_EXPORT: {
			return "export";
		} break;
		case G_TK_PR_SETGET: {
			return "setget";
		} break;
		case G_TK_PR_CONST: {
			return "const";
		} break;
		case G_TK_PR_VAR: {
			return "var";
		} break;
		case G_TK_PR_AS: {
			return "as";
		} break;
		case G_TK_PR_VOID: {
			return "void";
		} break;
		case G_TK_PR_ENUM: {
			return "enum";
		} break;
		case G_TK_PR_PRELOAD: {
			return "preload";
		} break;
		case G_TK_PR_ASSERT: {
			return "assert";
		} break;
		case G_TK_PR_YIELD: {
			return "yield";
		} break;
		case G_TK_PR_SIGNAL: {
			return "signal";
		} break;
		case G_TK_PR_BREAKPOINT: {
			return "breakpoint";
		} break;
		case G_TK_PR_REMOTE: {
			return "remote";
		} break;
		case G_TK_PR_SYNC: {
			return "sync";
		} break;
		case G_TK_PR_MASTER: {
			return "master";
		} break;
		case G_TK_PR_SLAVE: {
			return "slave";
		} break;
		case G_TK_PR_PUPPET: {
			return "puppet";
		} break;
		case G_TK_PR_REMOTESYNC: {
			return "remotesync";
		} break;
		case G_TK_PR_MASTERSYNC: {
			return "mastersync";
		} break;
		case G_TK_PR_PUPPETSYNC: {
			return "puppetsync";
		} break;
		case G_TK_BRACKET_OPEN: {
			return "[";
		} break;
		case G_TK_BRACKET_CLOSE: {
			return "]";
		} break;
		case G_TK_CURLY_BRACKET_OPEN: {
			return "{";
		} break;
		case G_TK_CURLY_BRACKET_CLOSE: {
			return "}";
		} break;
		case G_TK_PARENTHESIS_OPEN: {
			return "(";
		} break;
		case G_TK_PARENTHESIS_CLOSE: {
			return ")";
		} break;
		case G_TK_COMMA: {
			return ",";
		} break;
		case G_TK_SEMICOLON: {
			return ";";
		} break;
		case G_TK_PERIOD: {
			return ".";
		} break;
		case G_TK_QUESTION_MARK: {
			return "?";
		} break;
		case G_TK_COLON: {
			return ":";
		} break;
		case G_TK_DOLLAR: {
			return "$";
		} break;
		case G_TK_FORWARD_ARROW: {
			return "->";
		} break;
		case G_TK_INDENT:
		case G_TK_DEDENT:
			return "";
		case G_TK_NEWLINE: {
			return "\n";
		} break;
		case G_TK_CONST_PI: {
			return "PI";
		} break;
		case G_TK_CONST_TAU: {
			return "TAU";
		} break;
		case G_TK_WILDCARD: {
			return "_";
		} break;
		case G_TK_CONST_INF: {
			return "INF";
		} break;
		case G_TK_CONST_NAN: {
			return "NAN";
		} break;
		case G_TK_PR_SLAVESYNC: {
			return "slavesync";
		} break;
		case G_TK_CF_DO: {
			return "do";
		} break;
		case G_TK_CF_CASE: {
			return "case";
		} break;
		case G_TK_CF_SWITCH: {
			return "switch";
		} break;
		case G_TK_AMPERSAND_AMPERSAND: {
			return "&&";
		} break;
		case G_TK_PIPE_PIPE: {
			return "||";
		} break;
		case G_TK_BANG: {
			return "!";
		} break;
		case G_TK_STAR_STAR: {
			return "**";
		} break;
		case G_TK_STAR_STAR_EQUAL: {
			return "**=";
		} break;
		case G_TK_CF_WHEN: {
			return "when";
		} break;
		case G_TK_PR_AWAIT: {
			return "await";
		} break;
		case G_TK_PR_NAMESPACE: {
			return "namespace";
		} break;
		case G_TK_PR_SUPER: {
			return "super";
		} break;
		case G_TK_PR_TRAIT: {
			return "trait";
		} break;
		case G_TK_PERIOD_PERIOD: {
			return "..";
		} break;
		case G_TK_PERIOD_PERIOD_PERIOD: {
			return "...";
		} break;
		case G_TK_UNDERSCORE: {
			return "_";
		} break;
		case G_TK_BACKTICK: {
			return "`";
		} break;
		case G_TK_ABSTRACT: {
			return "abstract";
		} break;
		// case G_TK_ERROR: {
		// 	//skip - invalid
		// } break;
		// case G_TK_EOF: {
		// 	//skip - invalid
		// } break;
		// case G_TK_CURSOR: {
		// 	//skip - invalid
		// } break;
		// case G_TK_VCS_CONFLICT_MARKER: {
		// 	//skip - invalid
		// } break;
		// case G_TK_MAX: {
		// 	GDSDECOMP_FAIL_V_MSG(ERR_INVALID_DATA, "Invalid token: TK_MAX (" + itos(local_token) + ")");
		// } break;
		default: {
			return "";
		}
	}
}

Dictionary GDScriptDecomp::to_json() const {
	Dictionary json;
	String engine_version = get_engine_version();
	// bytecode_rev is a hex string without the 0x prefix
	json["bytecode_rev"] = String::num_int64(get_bytecode_rev(), 16).to_lower();
	json["bytecode_version"] = get_bytecode_version();
	json["date"] = get_date();
	json["engine_version"] = engine_version;
	json["max_engine_version"] = get_max_engine_version();
	json["engine_ver_major"] = get_engine_ver_major();
	json["variant_ver_major"] = get_variant_ver_major();
	json["parent"] = String::num_int64(get_parent(), 16).to_lower();
	json["is_dev"] = engine_version.contains("-dev") || is_custom();
	auto added_tokens = get_added_tokens();
	auto added_tokens_str = PackedStringArray();
	for (int i = 0; i < added_tokens.size(); i++) {
		added_tokens_str.append(g_token_str[added_tokens[i]]);
	}
	json["added_tokens"] = added_tokens_str;
	auto removed_tokens = get_removed_tokens();
	auto removed_tokens_str = PackedStringArray();
	for (int i = 0; i < removed_tokens.size(); i++) {
		removed_tokens_str.append(g_token_str[removed_tokens[i]]);
	}
	json["removed_tokens"] = removed_tokens_str;
	json["added_functions"] = get_added_functions();
	json["removed_functions"] = get_removed_functions();
	json["renamed_functions"] = get_renamed_functions();
	json["arg_count_changed"] = get_function_arg_count_changed();
	json["tokens_renamed"] = get_tokens_renamed();

	Vector<String> func_names;
	for (int i = 0; i < get_function_count(); i++) {
		func_names.append(get_function_name(i));
	}
	json["func_names"] = func_names;
	Vector<String> tk_names;
	for (int i = 0; i < get_token_max() + 1; i++) {
		auto val = get_global_token(i);
		tk_names.append(get_token_name(val));
	}
	json["tk_names"] = tk_names;
	return json;
}

TypedArray<Dictionary> GDScriptDecomp::get_all_decomp_versions_json() {
	TypedArray<Dictionary> ret;
	auto versions = GDScriptDecompVersion::get_decomp_versions(true, 0);
	for (int i = 0; i < versions.size(); i++) {
		Ref<GDScriptDecomp> decomp = versions[i].create_decomp();
		if (decomp.is_null()) {
			continue;
		}
		ret.append(decomp->to_json());
	}
	return ret;
}

String GDScriptDecomp::get_token_name(GlobalToken p_token) {
	if ((int)p_token <= (int)G_TK_MAX && (int)p_token >= 0) {
		return g_token_str[(int)p_token];
	}
	return "";
}

GDScriptDecomp::GlobalToken GDScriptDecomp::get_token_for_name(const String &p_name) {
	for (int i = 0; i <= G_TK_MAX; i++) {
		if (g_token_str[i] == p_name) {
			return (GlobalToken)i;
		}
	}
	return G_TK_MAX;
}

Ref<GDScriptDecomp> GDScriptDecomp::_create_custom_decomp(Dictionary p_custom_def, int p_derived_from) {
	GDScriptDecompVersion decomp;
	if (p_derived_from == 0) {
		decomp = GDScriptDecompVersion::create_version_from_custom_def(p_custom_def);
	} else {
		decomp = GDScriptDecompVersion::create_derived_version_from_custom_def(p_derived_from, p_custom_def);
	}
	return decomp.create_decomp();
}

int GDScriptDecomp::register_decomp_version_custom(Dictionary p_custom_def, int p_derived_from) {
	if (p_derived_from == 0) {
		return GDScriptDecompVersion::register_decomp_version_custom(p_custom_def);
	} else {
		return GDScriptDecompVersion::register_derived_decomp_version_custom(p_derived_from, p_custom_def);
	}
}
