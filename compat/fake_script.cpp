#include "fake_script.h"

#include "core/string/ustring.h"
#include "variant_decoder_compat.h"
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

Variant FakeGDScript::get_rpc_config() const {
	return {};
}

Error FakeGDScript::parse_script() {
	using GT = GlobalToken;
	Vector<StringName> &identifiers = script_state.identifiers;
	Vector<Variant> &constants = script_state.constants;
	Vector<uint32_t> &tokens = script_state.tokens;
	auto bytecode_version = script_state.bytecode_version;

	bool first_constant = true;
	// reserved words can be used as class members in GDScript 2.0. Hooray.
	auto is_gdscript20_accessor = [&](int i) {
		return bytecode_version >= GDScriptDecomp::GDSCRIPT_2_0_VERSION && decomp->check_prev_token(i, tokens, GT::G_TK_PERIOD);
	};

	for (int i = 0; i < tokens.size(); i++) {
		uint32_t local_token = tokens[i] & GDScriptDecomp::TOKEN_MASK;
		GlobalToken curr_token = decomp->get_global_token(local_token);
		switch (curr_token) {
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
			} break;
			case GT::G_TK_PR_CLASS: {
				// if (decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
				// 	curr_class = get_identifier_func(i + 1);
				// 	curr_class_indent = indent;
				// 	curr_class_start_idx = i;
				// 	curr_class_start_line = prev_line;
				// }
			} break;
			case GT::G_TK_PR_CLASS_NAME: {
				if (!is_gdscript20_accessor(i) && global_name.is_empty() && decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
					uint32_t identifier = tokens[i + 1] >> GDScriptDecomp::TOKEN_BITS;
					ERR_FAIL_COND_V(identifier >= (uint32_t)identifiers.size(), ERR_INVALID_DATA);
					global_name = identifiers[identifier];
					local_name = global_name;
				}
			} break;
			case GT::G_TK_PR_EXTENDS: {
				if (!is_gdscript20_accessor(i) && base_type.is_empty()) {
					if (decomp->check_next_token(i, tokens, GT::G_TK_CONSTANT)) {
						uint32_t constant = tokens[i] >> GDScriptDecomp::TOKEN_BITS;
						ERR_FAIL_COND_V(constant >= (uint32_t)constants.size(), ERR_INVALID_DATA);
						base_type = constants[constant];
					} else if (decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
						uint32_t identifier = tokens[i] >> GDScriptDecomp::TOKEN_BITS;
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
}

String FakeGDScript::get_script_path() const {
	return script_path;
}

Error FakeGDScript::load_source_code(const String &p_path) {
	script_path = p_path;
	return reload();
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