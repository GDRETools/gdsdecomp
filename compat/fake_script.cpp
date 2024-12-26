#include "fake_script.h"

#include "variant_decoder_compat.h"
#include "core/string/ustring.h"
#include <utility/gdre_settings.h>

void FakeGDScript::reload_from_file() {
	reload();
}

bool FakeGDScript::can_instantiate() const {
	return false;
}

Ref<Script> FakeGDScript::get_base_script() const {
	return nullptr;
}

StringName FakeGDScript::get_global_name() const {
	return global_name;
}

bool FakeGDScript::inherits_script(const Ref<Script> &p_script) const {
	// TODO?
	return true;
}

StringName FakeGDScript::get_instance_base_type() const {
	return base_type;
}

ScriptInstance *FakeGDScript::instance_create(Object *p_this) {
	return nullptr;
}

PlaceHolderScriptInstance *FakeGDScript::placeholder_instance_create(Object *p_this) {
	return nullptr;
}

bool FakeGDScript::instance_has(const Object *p_this) const {
	// TODO?
	return true;
}

bool FakeGDScript::has_source_code() const {
	return !source.is_empty();
}

String FakeGDScript::get_source_code() const {
	return source;
}

void FakeGDScript::set_source_code(const String &p_code) {
	if (decomp.is_null()) {
		decomp = GDScriptDecomp::create_decomp_for_commit(GDRESettings::get_singleton()->get_bytecode_revision());
		ERR_FAIL_COND_MSG(decomp.is_null(), "Unknown version, failed to decompile");
	}
	source = p_code;
	binary_buffer = decomp->compile_code_string(source);
	if (binary_buffer.size() == 0) {
		auto mst = decomp->get_error_message();
		ERR_FAIL_MSG("Error compiling code: " + mst);
	}
	auto err = decomp->get_script_state(binary_buffer, script_state);
	ERR_FAIL_COND_MSG(err != OK, "Error parsing bytecode");
	err = parse_script();
	ERR_FAIL_COND_MSG(err != OK, "Error parsing script");
}

Error FakeGDScript::reload(bool p_keep_state) {
	auto revision = GDRESettings::get_singleton()->get_bytecode_revision();
	ERR_FAIL_COND_V_MSG(!revision, ERR_UNCONFIGURED, "No bytecode revision set!");

	decomp = GDScriptDecomp::create_decomp_for_commit(revision);
	ERR_FAIL_COND_V_MSG(decomp.is_null(), ERR_FILE_UNRECOGNIZED, "Unknown version, failed to decompile");
	Error err = OK;
	// check the first four bytes to see if it's a binary file
	auto ext = script_path.get_extension().to_lower();
	bool is_binary = false;
	if (ext == "gde") {
		is_binary = true;
		err = decomp->get_buffer_encrypted(script_path, 3, GDRESettings::get_singleton()->get_encryption_key(), binary_buffer);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Error reading encrypted file: " + script_path);
	} else {
		binary_buffer = FileAccess::get_file_as_bytes(script_path);
	}
	is_binary = binary_buffer.size() >= 4 && binary_buffer[0] == 'G' && binary_buffer[1] == 'D' && binary_buffer[2] == 'S' && binary_buffer[3] == 'C';

	if (is_binary) {
		err = decomp->decompile_buffer(binary_buffer);
		if (err) {
			auto mst = decomp->get_error_message();
			ERR_FAIL_V_MSG(err, "Error decompiling code " + script_path + ": " + mst);
		}
		ERR_FAIL_COND_V_MSG(err != OK, err, "Error decompiling binary file: " + script_path);
		source = decomp->get_script_text();
	} else {
		source = String::utf8(reinterpret_cast<const char *>(binary_buffer.ptr()), binary_buffer.size());
		binary_buffer = decomp->compile_code_string(source);
		if (binary_buffer.size() == 0) {
			auto mst = decomp->get_error_message();
			ERR_FAIL_V_MSG(ERR_PARSE_ERROR, "Error compiling code " + script_path + ": " + mst);
		}
	}
	err = decomp->get_script_state(binary_buffer, script_state);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Error parsing bytecode");
	err = parse_script();
	ERR_FAIL_COND_V_MSG(err != OK, err, "Error parsing script");
	valid = true;
	return OK;
}

StringName FakeGDScript::get_doc_class_name() const {
	return global_name;
}

Vector<DocData::ClassDoc> FakeGDScript::get_documentation() const { return {}; }

String FakeGDScript::get_class_icon_path() const { return {}; }

PropertyInfo FakeGDScript::get_class_category() const { return {}; }

bool FakeGDScript::has_method(const StringName &p_method) const { return false; }

bool FakeGDScript::has_static_method(const StringName &p_method) const { return false; }

int FakeGDScript::get_script_method_argument_count(const StringName &p_method, bool *r_is_valid) const { return 0; }

MethodInfo FakeGDScript::get_method_info(const StringName &p_method) const { return {}; }

bool FakeGDScript::is_tool() const { return tool; }

bool FakeGDScript::is_valid() const { return valid; }

bool FakeGDScript::is_abstract() const { return false; }

ScriptLanguage *FakeGDScript::get_language() const {
	return nullptr;
}

bool FakeGDScript::has_script_signal(const StringName &p_signal) const {
	return false;
}

void FakeGDScript::get_script_signal_list(List<MethodInfo> *r_signals) const {
}

bool FakeGDScript::get_property_default_value(const StringName &p_property, Variant &r_value) const {
	return false;
}

void FakeGDScript::update_exports() {
}

void FakeGDScript::get_script_method_list(List<MethodInfo> *p_list) const {
}

void FakeGDScript::get_script_property_list(List<PropertyInfo> *p_list) const {
}

int FakeGDScript::get_member_line(const StringName &p_member) const { return -1; }

void FakeGDScript::get_constants(HashMap<StringName, Variant> *p_constants) {
}

void FakeGDScript::get_members(HashSet<StringName> *p_members) {
}

bool FakeGDScript::is_placeholder_fallback_enabled() const { return false; }

Variant FakeGDScript::get_rpc_config() const { return {}; }

Error FakeGDScript::parse_script() {
	using GT = GlobalToken;
	//Cleanup
	//Load bytecode
	Vector<StringName> &identifiers = script_state.identifiers;
	Vector<Variant> &constants = script_state.constants;
	Vector<uint32_t> &tokens = script_state.tokens;
	VMap<uint32_t, uint32_t> &lines = script_state.lines;
	VMap<uint32_t, uint32_t> &columns = script_state.columns;
	int bytecode_version = script_state.bytecode_version;
	int variant_ver_major = decomp->get_variant_ver_major();
	int FUNC_MAX = decomp->get_function_count();

	auto get_line_func([&](int i) {
		if (lines.has(i)) {
			return lines[i];
		}
		return 0U;
	});
	auto get_col_func([&](int i) {
		if (columns.has(i)) {
			return columns[i];
		}
		return 0U;
	});

	//Decompile script
	String line;
	int indent = 0;
	GlobalToken prev_token = GT::G_TK_NEWLINE;
	int prev_line = 1;
	int prev_line_start_column = 1;

	int tab_size = 1;
	bool use_spaces = false;
	if (columns.size() > 0) {
		use_spaces = true;
	}
	String temp_script_text;
	auto handle_newline = [&](int i, GlobalToken curr_token, int curr_line, int curr_column) {
		for (int j = 0; j < indent; j++) {
			temp_script_text += use_spaces ? " " : "\t";
		}
		temp_script_text += line;
		if (curr_line <= prev_line) {
			curr_line = prev_line + 1; // force new line
		}
		for (;prev_line < curr_line; prev_line++) {
			if (curr_token != GT::G_TK_NEWLINE && bytecode_version < GDScriptDecomp::GDSCRIPT_2_0_VERSION) {
				temp_script_text += "\\"; // line continuation
			}
			temp_script_text += "\n";
		}
		line = String();
		if (curr_token == GT::G_TK_NEWLINE) {
			indent = tokens[i] >> GDScriptDecomp::TOKEN_BITS;
		} else if (bytecode_version >= GDScriptDecomp::GDSCRIPT_2_0_VERSION) {
			prev_token = GT::G_TK_NEWLINE;
			int col_diff = curr_column - prev_line_start_column;
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
	};

	auto check_new_line = [&](int i) {
		if (get_line_func(i) != prev_line && get_line_func(i) != 0) {
			return true;
		}
		return false;
	};

	auto ensure_space_func = [&](bool only_if_not_newline = false) {
		if (!line.ends_with(" ") && (!only_if_not_newline || (prev_token != GT::G_TK_NEWLINE))) {
			line += " ";
		}
	};

	auto ensure_ending_space_func([&](int idx) {
		if (
				!line.ends_with(" ") && idx < tokens.size() - 1 &&
				(decomp->get_global_token(tokens[idx + 1]) != GT::G_TK_NEWLINE &&
						!check_new_line(idx + 1))) {
			line += " ";
		}
	});
	auto get_identifier_func = [&](int i) {
		uint32_t identifier = tokens[i] >> GDScriptDecomp::TOKEN_BITS;
		ERR_FAIL_COND_V(identifier >= (uint32_t)identifiers.size(), StringName());
		return identifiers[tokens[i] >> GDScriptDecomp::TOKEN_BITS];
	};
	bool first_constant = true;
	StringName curr_class;
	int curr_class_indent = -1;
	int curr_class_start_idx = -1;
	int curr_class_start_line = -1;
	for (int i = 0; i < tokens.size(); i++) {
		uint32_t local_token = tokens[i] & GDScriptDecomp::TOKEN_MASK;
		GlobalToken curr_token = decomp->get_global_token(local_token);
		int curr_line = get_line_func(i);
		int curr_column = get_col_func(i);
		if (curr_token != GT::G_TK_NEWLINE && curr_line != prev_line && curr_line != 0) {
			handle_newline(i, curr_token, curr_line, curr_column);
		}
		if (indent <= curr_class_indent && i > curr_class_start_idx + 2) {
			// end of class
			subclasses.insert(curr_class, {curr_class_start_line, prev_line - 1});
			curr_class_indent = -1;
			curr_class_start_idx = -1;
			curr_class_start_line = -1;
		}
		switch (curr_token) {
			case GT::G_TK_EMPTY: {
				//skip
			} break;
			case GT::G_TK_ANNOTATION: // fallthrough
			case GT::G_TK_IDENTIFIER: {
				ERR_FAIL_COND_V((tokens[i] >> GDScriptDecomp::TOKEN_BITS) >= identifiers.size(), ERR_INVALID_DATA);
				line += String(get_identifier_func(i));
			} break;
			case GT::G_TK_LITERAL: // fallthrough
			case GT::G_TK_CONSTANT: {
				uint32_t constant = tokens[i] >> GDScriptDecomp::TOKEN_BITS;
				ERR_FAIL_COND_V(constant >= (uint32_t)constants.size(), ERR_INVALID_DATA);
				
				// TODO: handle GDScript 2.0 multi-line strings: we have to check the number of newlines
				// in the string and if the next token has a line number difference >= the number of newlines
				if (first_constant) {
					first_constant = false;
					if (constants[constant] == "@tool") {
						tool = true;
					}
				}
				line += decomp->get_constant_string(constants, constant);
				ensure_space_func();
			} break;
			case GT::G_TK_SELF: {
				line += "self";
			} break;
			case GT::G_TK_BUILT_IN_TYPE: {
				line += VariantDecoderCompat::get_variant_type_name(tokens[i] >> GDScriptDecomp::TOKEN_BITS, variant_ver_major);
			} break;
			case GT::G_TK_BUILT_IN_FUNC: {
				ERR_FAIL_COND_V(tokens[i] >> GDScriptDecomp::TOKEN_BITS >= FUNC_MAX, ERR_INVALID_DATA);
				line += decomp->get_function_name(tokens[i] >> GDScriptDecomp::TOKEN_BITS);
			} break;
			case GT::G_TK_OP_IN: {
				ensure_space_func();
				line += "in ";
			} break;
			case GT::G_TK_OP_EQUAL: {
				ensure_space_func();
				line += "== ";
			} break;
			case GT::G_TK_OP_NOT_EQUAL: {
				ensure_space_func();
				line += "!= ";
			} break;
			case GT::G_TK_OP_LESS: {
				ensure_space_func();
				line += "< ";
			} break;
			case GT::G_TK_OP_LESS_EQUAL: {
				ensure_space_func();
				line += "<= ";
			} break;
			case GT::G_TK_OP_GREATER: {
				ensure_space_func();
				line += "> ";
			} break;
			case GT::G_TK_OP_GREATER_EQUAL: {
				ensure_space_func();
				line += ">= ";
			} break;
			case GT::G_TK_OP_AND: {
				ensure_space_func();
				line += "and ";
			} break;
			case GT::G_TK_OP_OR: {
				ensure_space_func();
				line += "or ";
			} break;
			case GT::G_TK_OP_NOT: {
				ensure_space_func();
				line += "not ";
			} break;
			case GT::G_TK_OP_ADD: {
				ensure_space_func();
				line += "+ ";
			} break;
			case GT::G_TK_OP_SUB: {
				ensure_space_func(true);
				line += "- ";
				//TODO: do not add space after unary "-"
			} break;
			case GT::G_TK_OP_MUL: {
				ensure_space_func();
				line += "* ";
			} break;
			case GT::G_TK_OP_DIV: {
				ensure_space_func();
				line += "/ ";
			} break;
			case GT::G_TK_OP_MOD: {
				ensure_space_func(true);
				line += "% ";
			} break;
			case GT::G_TK_OP_SHIFT_LEFT: {
				ensure_space_func();
				line += "<< ";
			} break;
			case GT::G_TK_OP_SHIFT_RIGHT: {
				ensure_space_func();
				line += ">> ";
			} break;
			case GT::G_TK_OP_ASSIGN: {
				ensure_space_func();
				line += "= ";
			} break;
			case GT::G_TK_OP_ASSIGN_ADD: {
				ensure_space_func();
				line += "+= ";
			} break;
			case GT::G_TK_OP_ASSIGN_SUB: {
				ensure_space_func();
				line += "-= ";
			} break;
			case GT::G_TK_OP_ASSIGN_MUL: {
				ensure_space_func();
				line += "*= ";
			} break;
			case GT::G_TK_OP_ASSIGN_DIV: {
				ensure_space_func();
				line += "/= ";
			} break;
			case GT::G_TK_OP_ASSIGN_MOD: {
				ensure_space_func();
				line += "%= ";
			} break;
			case GT::G_TK_OP_ASSIGN_SHIFT_LEFT: {
				ensure_space_func();
				line += "<<= ";
			} break;
			case GT::G_TK_OP_ASSIGN_SHIFT_RIGHT: {
				ensure_space_func();
				line += ">>= ";
			} break;
			case GT::G_TK_OP_ASSIGN_BIT_AND: {
				ensure_space_func();
				line += "&= ";
			} break;
			case GT::G_TK_OP_ASSIGN_BIT_OR: {
				ensure_space_func();
				line += "|= ";
			} break;
			case GT::G_TK_OP_ASSIGN_BIT_XOR: {
				ensure_space_func();
				line += "^= ";
			} break;
			case GT::G_TK_OP_BIT_AND: {
				ensure_space_func(true);
				line += "& ";
			} break;
			case GT::G_TK_OP_BIT_OR: {
				ensure_space_func(true);
				line += "| ";
			} break;
			case GT::G_TK_OP_BIT_XOR: {
				ensure_space_func(true);
				line += "^ ";
			} break;
			case GT::G_TK_OP_BIT_INVERT: {
				ensure_space_func(true);
				line += "~ ";
			} break;
			//case GT::G_TK_OP_PLUS_PLUS: {
			//	line += "++";
			//} break;
			//case GT::G_TK_OP_MINUS_MINUS: {
			//	line += "--";
			//} break;
			case GT::G_TK_CF_IF: {
				ensure_space_func(true);
				line += "if ";
			} break;
			case GT::G_TK_CF_ELIF: {
				line += "elif ";
			} break;
			case GT::G_TK_CF_ELSE: {
				ensure_space_func(true);
				line += "else";
				if (!decomp->check_next_token(i, tokens, GT::G_TK_COLON)) {
					line += " ";
				}
			} break;
			case GT::G_TK_CF_FOR: {
				line += "for ";
			} break;
			case GT::G_TK_CF_WHILE: {
				line += "while ";
			} break;
			case GT::G_TK_CF_BREAK: {
				line += "break";
			} break;
			case GT::G_TK_CF_CONTINUE: {
				line += "continue";
			} break;
			case GT::G_TK_CF_PASS: {
				line += "pass";
			} break;
			case GT::G_TK_CF_RETURN: {
				line += "return ";
			} break;
			case GT::G_TK_CF_MATCH: {
				line += "match ";
			} break;
			case GT::G_TK_PR_FUNCTION: {
				line += "func ";
			} break;
			case GT::G_TK_PR_CLASS: {
				if (decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
					curr_class = get_identifier_func(i + 1);
					curr_class_indent = indent;
					curr_class_start_idx = i;
					curr_class_start_line = prev_line;
				}
				line += "class ";
			} break;
			case GT::G_TK_PR_CLASS_NAME: {
				if (decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
					uint32_t identifier = tokens[i+1] >> GDScriptDecomp::TOKEN_BITS;
					ERR_FAIL_COND_V(identifier >= (uint32_t)identifiers.size(), ERR_INVALID_DATA);
					global_name = identifiers[identifier];
					local_name = global_name;
				}
				line += "class_name ";
			} break;
			case GT::G_TK_PR_EXTENDS: {
				if (base_type.is_empty()) {
					// TODO: remove G_TK_LITERAL, just set it to G_TK_CONSTANT
					if (decomp->check_next_token(i, tokens, GT::G_TK_LITERAL) || decomp->check_next_token(i, tokens, GT::G_TK_CONSTANT)) {
						uint32_t constant = tokens[i] >> GDScriptDecomp::TOKEN_BITS;
						ERR_FAIL_COND_V(constant >= (uint32_t)constants.size(), ERR_INVALID_DATA);
						base_type = constants[constant];
					} else if (decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)){
						uint32_t identifier = tokens[i] >> GDScriptDecomp::TOKEN_BITS;
						ERR_FAIL_COND_V(identifier >= (uint32_t)identifiers.size(), ERR_INVALID_DATA);
						base_type = identifiers[identifier];
					} else {
						int i = 0;
					}
				}
				line += "extends ";
			} break;
			case GT::G_TK_PR_IS: {
				ensure_space_func();
				line += "is ";
			} break;
			case GT::G_TK_PR_ONREADY: {
				line += "onready ";
			} break;
			case GT::G_TK_PR_TOOL: {
				tool = true;
				line += "tool ";
			} break;
			case GT::G_TK_PR_STATIC: {
				line += "static ";
			} break;
			case GT::G_TK_PR_EXPORT: {
				line += "export ";
			} break;
			case GT::G_TK_PR_SETGET: {
				line += " setget ";
			} break;
			case GT::G_TK_PR_CONST: {
				line += "const ";
			} break;
			case GT::G_TK_PR_VAR: {
				if (line != String() && prev_token != GT::G_TK_PR_ONREADY)
					line += " ";
				line += "var ";
			} break;
			case GT::G_TK_PR_AS: {
				ensure_space_func();
				line += "as ";
			} break;
			case GT::G_TK_PR_VOID: {
				line += "void ";
			} break;
			case GT::G_TK_PR_ENUM: {
				line += "enum ";
			} break;
			case GT::G_TK_PR_PRELOAD: {
				line += "preload";
			} break;
			case GT::G_TK_PR_ASSERT: {
				line += "assert ";
			} break;
			case GT::G_TK_PR_YIELD: {
				line += "yield ";
			} break;
			case GT::G_TK_PR_SIGNAL: {
				line += "signal ";
			} break;
			case GT::G_TK_PR_BREAKPOINT: {
				line += "breakpoint ";
			} break;
			case GT::G_TK_PR_REMOTE: {
				line += "remote ";
			} break;
			case GT::G_TK_PR_SYNC: {
				line += "sync ";
			} break;
			case GT::G_TK_PR_MASTER: {
				line += "master ";
			} break;
			case GT::G_TK_PR_SLAVE: {
				line += "slave ";
			} break;
			case GT::G_TK_PR_PUPPET: {
				line += "puppet ";
			} break;
			case GT::G_TK_PR_REMOTESYNC: {
				line += "remotesync ";
			} break;
			case GT::G_TK_PR_MASTERSYNC: {
				line += "mastersync ";
			} break;
			case GT::G_TK_PR_PUPPETSYNC: {
				line += "puppetsync ";
			} break;
			case GT::G_TK_BRACKET_OPEN: {
				line += "[";
			} break;
			case GT::G_TK_BRACKET_CLOSE: {
				line += "]";
			} break;
			case GT::G_TK_CURLY_BRACKET_OPEN: {
				line += "{";
			} break;
			case GT::G_TK_CURLY_BRACKET_CLOSE: {
				line += "}";
			} break;
			case GT::G_TK_PARENTHESIS_OPEN: {
				line += "(";
			} break;
			case GT::G_TK_PARENTHESIS_CLOSE: {
				line += ")";
			} break;
			case GT::G_TK_COMMA: {
				line += ", ";
			} break;
			case GT::G_TK_SEMICOLON: {
				line += ";";
			} break;
			case GT::G_TK_PERIOD: {
				line += ".";
			} break;
			case GT::G_TK_QUESTION_MARK: {
				line += "?";
			} break;
			case GT::G_TK_COLON: {
				line += ":";
				ensure_ending_space_func(i);
			} break;
			case GT::G_TK_DOLLAR: {
				line += "$";
			} break;
			case GT::G_TK_FORWARD_ARROW: {
				line += "->";
			} break;
			case GT::G_TK_INDENT:
			case GT::G_TK_DEDENT:
			case GT::G_TK_NEWLINE: {
				handle_newline(i, curr_token, curr_line, curr_column);
			} break;
			case GT::G_TK_CONST_PI: {
				line += "PI";
			} break;
			case GT::G_TK_CONST_TAU: {
				line += "TAU";
			} break;
			case GT::G_TK_WILDCARD: {
				line += "_";
			} break;
			case GT::G_TK_CONST_INF: {
				line += "INF";
			} break;
			case GT::G_TK_CONST_NAN: {
				line += "NAN";
			} break;
			case GT::G_TK_PR_SLAVESYNC: {
				line += "slavesync ";
			} break;
			case GT::G_TK_CF_DO: {
				line += "do ";
			} break;
			case GT::G_TK_CF_CASE: {
				line += "case ";
			} break;
			case GT::G_TK_CF_SWITCH: {
				line += "switch ";
			} break;
			case GT::G_TK_AMPERSAND_AMPERSAND: {
				ensure_space_func(true);
				line += "&& ";
			} break;
			case GT::G_TK_PIPE_PIPE: {
				ensure_space_func(true);
				line += "|| ";
			} break;
			case GT::G_TK_BANG: {
				ensure_space_func(true);
				line += "!";
			} break;
			case GT::G_TK_STAR_STAR: {
				ensure_space_func(true);
				line += "** ";
			} break;
			case GT::G_TK_STAR_STAR_EQUAL: {
				line += "**= ";
			} break;
			case GT::G_TK_CF_WHEN: {
				line += "when ";
			} break;
			case GT::G_TK_PR_AWAIT: {
				line += "await ";
			} break;
			case GT::G_TK_PR_NAMESPACE: {
				line += "namespace ";
			} break;
			case GT::G_TK_PR_SUPER: {
				line += "super ";
			} break;
			case GT::G_TK_PR_TRAIT: {
				line += "trait ";
			} break;
			case GT::G_TK_PERIOD_PERIOD: {
				line += "..";
			} break;
			case GT::G_TK_UNDERSCORE: {
				line += "_";
			} break;
			case GT::G_TK_ERROR: {
				//skip - invalid
			} break;
			case GT::G_TK_EOF: {
				//skip - invalid
			} break;
			case GT::G_TK_CURSOR: {
				//skip - invalid
			} break;
			case GT::G_TK_MAX: {
				ERR_FAIL_V_MSG(ERR_INVALID_DATA, "Invalid token: TK_MAX (" + itos(local_token) + ")");
			} break;
			default: {
				ERR_FAIL_V_MSG(ERR_INVALID_DATA, "Invalid token: " + itos(local_token));
			}
		}
		prev_token = curr_token;
	}

	return OK;
}


String FakeGDScript::get_script_path() const
{
	return script_path;
}

Error FakeGDScript::load_source_code(const String &p_path) {
	script_path = p_path;
	return reload();
}







bool FakeEmbeddedScript::_get(const StringName &p_name, Variant &r_ret) const {
	if (!properties.has(p_name)) {
		return false;
	}
	r_ret = properties[p_name];
	return true;
}

bool FakeEmbeddedScript::_set(const StringName &p_name, const Variant &p_value) {
	if (!properties.has(p_name)) {
		properties.insert(p_name, p_value);
		return true;
	}

	properties[p_name] = p_value;
	return true;
}

void FakeEmbeddedScript::_get_property_list(List<PropertyInfo> *p_list) const {
	for (const KeyValue<StringName, Variant> &E : properties) {
		p_list->push_back(PropertyInfo(E.value.get_type(), E.key));
	}
}

bool FakeEmbeddedScript::has_source_code() const {
	return properties.has("script/source");
}

void FakeEmbeddedScript::set_source_code(const String &p_code) {
	properties["script/source"] = p_code;
}

String FakeEmbeddedScript::get_source_code() const {
	if (!properties.has("script/source")) {
		return "";
	}
	return properties["script/source"];
}

void FakeEmbeddedScript::set_original_class(const String &p_class) {
	original_class = p_class;
}

String FakeEmbeddedScript::get_original_class() const {
	return original_class;
}