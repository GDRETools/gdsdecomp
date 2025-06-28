#include "fake_script.h"

#include "core/string/ustring.h"
#include <utility/gdre_settings.h>

#define FAKEGDSCRIPT_FAIL_COND_V_MSG(cond, val, msg) \
	if (unlikely(cond)) {                            \
		error_message = msg;                         \
		ERR_FAIL_V_MSG(val, msg);                    \
	}

#define FAKEGDSCRIPT_FAIL_V_MSG(val, msg) \
	error_message = msg;                  \
	ERR_FAIL_V_MSG(val, msg);

#define FAKEGDSCRIPT_FAIL_COND_MSG(cond, msg) \
	if (unlikely(cond)) {                     \
		error_message = msg;                  \
		ERR_FAIL_MSG(msg);                    \
	}

Error FakeGDScript::_reload_from_file() {
	error_message.clear();
	source.clear();
	binary_buffer.clear();
	FAKEGDSCRIPT_FAIL_COND_V_MSG(script_path.is_empty(), ERR_FILE_NOT_FOUND, "Script path is empty");
	Error err = OK;
	// check the first four bytes to see if it's a binary file
	auto ext = script_path.get_extension().to_lower();
	is_binary = false;
	if (ext == "gde") {
		is_binary = true;
		err = GDScriptDecomp::get_buffer_encrypted(script_path, 3, GDRESettings::get_singleton()->get_encryption_key(), binary_buffer);
		FAKEGDSCRIPT_FAIL_COND_V_MSG(err != OK, err, "Error reading encrypted file: " + script_path);
	} else {
		binary_buffer = FileAccess::get_file_as_bytes(script_path, &err);
		FAKEGDSCRIPT_FAIL_COND_V_MSG(err != OK, err, "Error reading file: " + script_path);
		is_binary = binary_buffer.size() >= 4 && binary_buffer[0] == 'G' && binary_buffer[1] == 'D' && binary_buffer[2] == 'S' && binary_buffer[3] == 'C';
		if (!is_binary) {
			err = source.append_utf8(reinterpret_cast<const char *>(binary_buffer.ptr()), binary_buffer.size());
			FAKEGDSCRIPT_FAIL_COND_V_MSG(err != OK, err, "Error reading file: " + script_path);
			binary_buffer.clear();
		}
	}
	return reload(false);
}

void FakeGDScript::reload_from_file() {
	Error err = _reload_from_file();
	FAKEGDSCRIPT_FAIL_COND_MSG(err != OK, "Error reloading script: " + script_path);
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
	is_binary = false;
	source = p_code;
	reload(false);
}

Error FakeGDScript::reload(bool p_keep_state) {
	error_message.clear();
	auto revision = override_bytecode_revision != 0 ? override_bytecode_revision : GDRESettings::get_singleton()->get_bytecode_revision();
	FAKEGDSCRIPT_FAIL_COND_V_MSG(!revision, ERR_UNCONFIGURED, "No bytecode revision set");

	decomp = GDScriptDecomp::create_decomp_for_commit(revision);
	FAKEGDSCRIPT_FAIL_COND_V_MSG(decomp.is_null(), ERR_FILE_UNRECOGNIZED, "Unknown version, failed to decompile");

	Error err = OK;
	if (is_binary) {
		err = decomp->decompile_buffer(binary_buffer);
		if (err) {
			error_message = "Error decompiling code: " + decomp->get_error_message();
			ERR_FAIL_V_MSG(err, "Error decompiling code " + script_path + ": " + decomp->get_error_message());
		}
		source = decomp->get_script_text();
	} else {
		binary_buffer = decomp->compile_code_string(source);
		if (binary_buffer.size() == 0) {
			error_message = "Error compiling code: " + decomp->get_error_message();
			ERR_FAIL_V_MSG(ERR_PARSE_ERROR, "Error compiling code " + script_path + ": " + decomp->get_error_message());
		}
	}
	err = decomp->get_script_state(binary_buffer, script_state);
	FAKEGDSCRIPT_FAIL_COND_V_MSG(err != OK, err, "Error parsing bytecode");
	err = parse_script();
	FAKEGDSCRIPT_FAIL_COND_V_MSG(err != OK, err, "Error parsing script");
	valid = true;
	return OK;
}

#ifdef TOOLS_ENABLED
StringName FakeGDScript::get_doc_class_name() const {
	return global_name;
}

Vector<DocData::ClassDoc> FakeGDScript::get_documentation() const {
	return {};
}

String FakeGDScript::get_class_icon_path() const {
	return {};
}

PropertyInfo FakeGDScript::get_class_category() const {
	return {};
}
#endif

bool FakeGDScript::has_method(const StringName &p_method) const {
	return false;
}

bool FakeGDScript::has_static_method(const StringName &p_method) const {
	return false;
}

int FakeGDScript::get_script_method_argument_count(const StringName &p_method, bool *r_is_valid) const {
	return 0;
}

MethodInfo FakeGDScript::get_method_info(const StringName &p_method) const {
	return {};
}

bool FakeGDScript::is_tool() const {
	return tool;
}

bool FakeGDScript::is_valid() const {
	return valid;
}

bool FakeGDScript::is_abstract() const {
	return false;
}

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

int FakeGDScript::get_member_line(const StringName &p_member) const {
	return -1;
}

void FakeGDScript::get_constants(HashMap<StringName, Variant> *p_constants) {
}

void FakeGDScript::get_members(HashSet<StringName> *p_members) {
}

bool FakeGDScript::is_placeholder_fallback_enabled() const {
	return false;
}

const Variant FakeGDScript::get_rpc_config() const {
	return {};
}

Error FakeGDScript::parse_script() {
	using GT = GlobalToken;
	ERR_FAIL_COND_V(script_state.bytecode_version == -1, ERR_PARSE_ERROR);
	Vector<StringName> &identifiers = script_state.identifiers;
	Vector<Variant> &constants = script_state.constants;
	Vector<uint32_t> &tokens = script_state.tokens;

	// reserved words can be used as class members in GDScript. Hooray.
	auto is_not_actually_reserved_word = [&](int i) {
		return (decomp->check_prev_token(i, tokens, GT::G_TK_PERIOD) ||
				(script_state.bytecode_version < GDScriptDecomp::GDSCRIPT_2_0_VERSION &&
						(decomp->check_prev_token(i, tokens, GT::G_TK_PR_FUNCTION) ||
								decomp->is_token_func_call(i, tokens))));
	};
	bool func_used = false;
	bool class_used = false;
	bool var_used = false;
	bool const_used = false;
	bool extends_used = false;
	bool class_name_used = false;

	for (int i = 0; i < tokens.size(); i++) {
		uint32_t local_token = tokens[i] & GDScriptDecomp::TOKEN_MASK;
		GlobalToken curr_token = decomp->get_global_token(local_token);
		switch (curr_token) {
			case GT::G_TK_ANNOTATION: {
				// in GDScript 2.0, the "@tool" annotation has to be the first expression in the file
				// (i.e. before the class body and 'extends' or 'class_name' keywords)
				if (!func_used && !class_used && !var_used && !const_used && !extends_used && !class_name_used) {
					uint32_t a_id = tokens[i] >> GDScriptDecomp::TOKEN_BITS;
					ERR_FAIL_COND_V(a_id >= (uint32_t)identifiers.size(), ERR_INVALID_DATA);

					const StringName &annotation = identifiers[a_id];
					if (annotation == "@tool") {
						tool = true;
					}
				}
			} break;
			case GT::G_TK_PR_FUNCTION: {
				if (!is_not_actually_reserved_word(i)) {
					func_used = true;
				}
			} break;
			case GT::G_TK_PR_VAR: {
				if (!is_not_actually_reserved_word(i)) {
					var_used = true;
				}
			} break;
			case GT::G_TK_PR_CONST: {
				if (!is_not_actually_reserved_word(i)) {
					const_used = true;
				}
			} break;
			case GT::G_TK_PR_EXPORT: {
			} break;
			case GT::G_TK_PR_TOOL: {
				// "tool" can be used literally anywhere in GDScript 1, so we only check it if it's actually a reserved word
				if (!is_not_actually_reserved_word(i)) {
					tool = true;
				}
			} break;
			case GT::G_TK_PR_CLASS: {
				if (!is_not_actually_reserved_word(i)) {
					class_used = true;
				}
				// if (decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
				// 	curr_class = get_identifier_func(i + 1);
				// 	curr_class_indent = indent;
				// 	curr_class_start_idx = i;
				// 	curr_class_start_line = prev_line;
				// }
			} break;
			case GT::G_TK_PR_CLASS_NAME: {
				// "class_name" can be used literally anywhere in GDScript 1, so we only check it if it's actually a reserved word
				class_name_used = true;
				if (global_name.is_empty() && !is_not_actually_reserved_word(i) && decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
					uint32_t identifier = tokens[i + 1] >> GDScriptDecomp::TOKEN_BITS;
					ERR_FAIL_COND_V(identifier >= (uint32_t)identifiers.size(), ERR_INVALID_DATA);
					global_name = identifiers[identifier];
					local_name = global_name;
				}
			} break;
			case GT::G_TK_PR_EXTENDS: {
				// "extends" is only valid for the global class if it's not in the body (class_name and tool can be used before it)
				// This applies to all versions of GDScript
				if (base_type.is_empty() && !func_used && !class_used && !var_used && !const_used && !extends_used && !is_not_actually_reserved_word(i)) {
					if (decomp->check_next_token(i, tokens, GT::G_TK_CONSTANT)) {
						uint32_t constant = tokens[i + 1] >> GDScriptDecomp::TOKEN_BITS;
						ERR_FAIL_COND_V(constant >= (uint32_t)constants.size(), ERR_INVALID_DATA);
						base_type = constants[constant];
					} else if (decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
						uint32_t identifier = tokens[i + 1] >> GDScriptDecomp::TOKEN_BITS;
						ERR_FAIL_COND_V(identifier >= (uint32_t)identifiers.size(), ERR_INVALID_DATA);
						base_type = identifiers[identifier];
					} else {
						// TODO: something?
					}
				}
			} break;
			default: {
				break;
			}
		}
		// prev_token = curr_token;
	}

	if (base_type.is_empty()) {
		base_type = decomp->get_variant_ver_major() < 4 ? "Reference" : "RefCounted";
	}

	return OK;
}

bool FakeGDScript::_get(const StringName &p_name, Variant &r_ret) const {
	if (p_name == "script/source") {
		r_ret = get_source_code();
		return true;
	}
	return false;
}

bool FakeGDScript::_set(const StringName &p_name, const Variant &p_value) {
	if (p_name == "script/source") {
		set_source_code(p_value);
		return true;
	}
	return false;
}

void FakeGDScript::_get_property_list(List<PropertyInfo> *p_properties) const {
	p_properties->push_back(PropertyInfo(Variant::STRING, "script/source", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
}

Variant FakeGDScript::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	return {};
}

void FakeGDScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_script_path"), &FakeGDScript::get_script_path);
	ClassDB::bind_method(D_METHOD("load_source_code", "path"), &FakeGDScript::load_source_code);
	ClassDB::bind_method(D_METHOD("get_error_message"), &FakeGDScript::get_error_message);
	ClassDB::bind_method(D_METHOD("set_override_bytecode_revision", "revision"), &FakeGDScript::set_override_bytecode_revision);
	ClassDB::bind_method(D_METHOD("get_override_bytecode_revision"), &FakeGDScript::get_override_bytecode_revision);
}

String FakeGDScript::get_script_path() const {
	return script_path;
}

Error FakeGDScript::load_source_code(const String &p_path) {
	script_path = p_path;
	return _reload_from_file();
}

String FakeGDScript::get_error_message() const {
	return error_message;
}

int FakeGDScript::get_override_bytecode_revision() const {
	return override_bytecode_revision;
}

void FakeGDScript::set_override_bytecode_revision(int p_revision) {
	override_bytecode_revision = p_revision;
}

// FakeEmbeddedScript

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

StringName FakeEmbeddedScript::get_global_name() const {
	auto path = get_path();
	if (path.is_empty() || !path.is_resource_file()) {
		return "";
	}
	return GDRESettings::get_singleton()->get_cached_script_class(path);
}

bool FakeEmbeddedScript::inherits_script(const Ref<Script> &p_script) const {
	return true;
}

StringName FakeEmbeddedScript::get_instance_base_type() const {
	auto path = get_path();
	if (path.is_empty() || !path.is_resource_file()) {
		return "";
	}
	return GDRESettings::get_singleton()->get_cached_script_base(path);
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

#undef FAKEGDSCRIPT_FAIL_COND_V_MSG
#undef FAKEGDSCRIPT_FAIL_V_MSG
#undef FAKEGDSCRIPT_FAIL_COND_MSG
