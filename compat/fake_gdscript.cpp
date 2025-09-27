#include "fake_gdscript.h"
#include "fake_script.h"

#include "compat/resource_loader_compat.h"
#include "compat/variant_decoder_compat.h"
#include "core/io/missing_resource.h"
#include "core/object/object.h"
#include "core/string/ustring.h"
#include "modules/gdscript/gdscript.h"
#include "utility/resource_info.h"
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
	loaded = false;
	FAKEGDSCRIPT_FAIL_COND_V_MSG(script_path.is_empty(), ERR_FILE_NOT_FOUND, "Script path is empty");
	Error err = OK;
	// check the first four bytes to see if it's a binary file
	is_binary = false;
	String actual_path = GDRESettings::get_singleton()->get_mapped_path(script_path);
	auto ext = actual_path.get_extension().to_lower();
	if (!FileAccess::exists(actual_path)) {
		FAKEGDSCRIPT_FAIL_COND_V_MSG(true, ERR_FILE_NOT_FOUND, vformat("File does not exist: %s (remapped to %s)", script_path, actual_path));
	}

	if (ext == "gde") {
		is_binary = true;
		err = GDScriptDecomp::get_buffer_encrypted(actual_path, 3, GDRESettings::get_singleton()->get_encryption_key(), binary_buffer);
		FAKEGDSCRIPT_FAIL_COND_V_MSG(err != OK, err, "Error reading encrypted file: " + script_path);
	} else {
		binary_buffer = FileAccess::get_file_as_bytes(actual_path, &err);
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
	return true;
}

Ref<Script> FakeGDScript::load_base_script() const {
	Ref<Script> base_script;
	size_t len = base_type.length();
	auto data = base_type.get_data();
	String path;
	if (len > 3 && data[len - 3] == '.' && data[len - 2] == 'g' && data[len - 1] == 'd') {
		path = base_type;
	} else {
		path = GDRESettings::get_singleton()->get_path_for_script_class(base_type);
	}
	if (path.is_empty()) {
		return {};
	}
	base_script = ResourceCompatLoader::custom_load(path, "", ResourceCompatLoader::get_default_load_type());
	return base_script;
}

Ref<Script> FakeGDScript::get_base_script() const {
	if (base.is_null()) {
		return load_base_script();
	}
	return base;
}

StringName FakeGDScript::get_global_name() const {
	return global_name;
}

bool FakeGDScript::inherits_script(const Ref<Script> &p_script) const {
	// TODO?
	return true;
}

StringName FakeGDScript::get_instance_base_type() const {
	auto s = get_base_script();
	if (s.is_valid()) {
		return s->get_instance_base_type();
	}
	return base_type;
}

ScriptInstance *FakeGDScript::instance_create(Object *p_this) {
	if (!can_instantiate()) {
		return nullptr;
	}
	auto instance = memnew(FakeScriptInstance());
	instance->script = Ref<FakeGDScript>(this);
	instance->owner = p_this;
	instance->update_cached_prop_names();
	return instance;
}

PlaceHolderScriptInstance *FakeGDScript::placeholder_instance_create(Object *p_this) {
	PlaceHolderScriptInstance *si = memnew(PlaceHolderScriptInstance(/*GDScriptLanguage::get_singleton()*/ nullptr, Ref<Script>(this), p_this));
	return si;
}

bool FakeGDScript::instance_has(const Object *p_this) const {
	// TODO?
	return true;
}

void FakeGDScript::set_source_code(const String &p_code) {
	is_binary = false;
	source = p_code;
	loaded = false;
	if (autoload) {
		reload(false);
	}
}

Error FakeGDScript::reload(bool p_keep_state) {
	error_message.clear();
	auto revision = override_bytecode_revision != 0 ? override_bytecode_revision : GDRESettings::get_singleton()->get_bytecode_revision();
	FAKEGDSCRIPT_FAIL_COND_V_MSG(!revision, ERR_UNCONFIGURED, "No bytecode revision set");
	loaded = false;

	decomp = GDScriptDecomp::create_decomp_for_commit(revision);
	FAKEGDSCRIPT_FAIL_COND_V_MSG(decomp.is_null(), ERR_FILE_UNRECOGNIZED, "Unknown version, failed to decompile");
	ver_major = decomp->get_engine_ver_major();

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
	FAKEGDSCRIPT_FAIL_COND_V_MSG(err != OK, err, "Error loading script state");
	loaded = true;

	err = parse_script();
	ERR_FAIL_COND_V_MSG(err, err, decomp->get_error_message());

	if (GDRESettings::get_singleton()->is_pack_loaded()) {
		ensure_base_and_global_name();
	}

	bool is_real_load = get_load_type() == ResourceInfo::LoadType::REAL_LOAD || get_load_type() == ResourceInfo::LoadType::GLTF_LOAD;
	if (base.is_null() && is_real_load) {
		base = load_base_script();
	}

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
	return _methods.has(p_method);
}

bool FakeGDScript::has_static_method(const StringName &p_method) const {
	return _methods[p_method].flags & METHOD_FLAG_STATIC;
}

int FakeGDScript::get_script_method_argument_count(const StringName &p_method, bool *r_is_valid) const {
	if (_methods.has(p_method)) {
		if (r_is_valid) {
			*r_is_valid = true;
		}
		return _methods[p_method].arguments.size();
	}
	if (r_is_valid) {
		*r_is_valid = false;
	}
	return -1;
}

MethodInfo FakeGDScript::get_method_info(const StringName &p_method) const {
	if (_methods.has(p_method)) {
		return _methods[p_method];
	}
	return {};
}

bool FakeGDScript::is_tool() const {
	return tool;
}

bool FakeGDScript::is_valid() const {
	return valid;
}

bool FakeGDScript::is_abstract() const {
	return abstract;
}

ScriptLanguage *FakeGDScript::get_language() const {
	return nullptr;
}

bool FakeGDScript::has_script_signal(const StringName &p_signal) const {
	if (_signals.has(p_signal)) {
		return true;
	}
	auto parent = get_base_script();
	if (parent.is_valid()) {
		return parent->has_script_signal(p_signal);
	}
	return false;
}

void FakeGDScript::get_script_signal_list(List<MethodInfo> *r_signals) const {
	for (const KeyValue<StringName, MethodInfo> &E : _signals) {
		r_signals->push_back(E.value);
	}
	auto parent = get_base_script();
	if (parent.is_valid()) {
		parent->get_script_signal_list(r_signals);
	}
}

bool FakeGDScript::get_property_default_value(const StringName &p_property, Variant &r_value) const {
	return false;
}

void FakeGDScript::update_exports() {
}

void FakeGDScript::get_script_method_list(List<MethodInfo> *p_list) const {
	for (const KeyValue<StringName, MethodInfo> &E : _methods) {
		p_list->push_back(E.value);
	}
	auto parent_script = get_base_script();
	if (parent_script.is_valid()) {
		parent_script->get_script_method_list(p_list);
	}
}

void FakeGDScript::get_script_property_list(List<PropertyInfo> *p_list) const {
	// TODO: Parse types, default values, etc.
	for (const auto &E : export_vars) {
		p_list->push_back(PropertyInfo(Variant::NIL, E));
	}
	auto parent_script = get_base_script();
	if (parent_script.is_valid()) {
		parent_script->get_script_property_list(p_list);
	}
}

int FakeGDScript::get_member_line(const StringName &p_member) const {
	return -1;
}

void FakeGDScript::get_constants(HashMap<StringName, Variant> *p_constants) {
}

void FakeGDScript::get_members(HashSet<StringName> *p_members) {
	if (!p_members) {
		return;
	}
	for (const StringName &E : export_vars) {
		p_members->insert(E);
	}
}

bool FakeGDScript::is_placeholder_fallback_enabled() const {
	return false;
}

const Variant FakeGDScript::get_rpc_config() const {
	return {};
}

void FakeGDScript::ensure_base_and_global_name() {
	String base_type_str = base_type.get_data();
	// GDScript 1.x allowed paths to be used with the "extends" keyword
	if (base_type_str.to_lower().ends_with(".gd")) {
		if (base_type_str.is_relative_path()) {
			base_type_str = GDRESettings::get_singleton()->localize_path(script_path.get_base_dir().path_join(base_type_str));
		}
		StringName found_class = GDRESettings::get_singleton()->get_cached_script_class(base_type_str);
		if (!found_class.is_empty()) {
			base_type = found_class;
		} else if (GDRESettings::get_singleton()->is_pack_loaded()) { // During the initial cache; we'll just have to load it ourselves
			Ref<Script> base_script = ResourceCompatLoader::custom_load(base_type_str, "", ResourceInfo::LoadType::GLTF_LOAD, nullptr, false, ResourceFormatLoader::CACHE_MODE_IGNORE);
			if (base.is_null()) {
				base = base_script;
			}
			if (base_script.is_valid()) {
				base_type = base_script->get_global_name();
			}
		} else {
			base_type = base_type_str;
		}
	}
	// If it doesn't use class_name, it's a script without a global identifier; for our sake, we'll just use the path as the global name
	if (global_name.is_empty() && !script_path.is_empty()) {
		global_name = GDRESettings::get_singleton()->localize_path(script_path.get_basename() + ".gd");
		local_name = global_name;
	}
}

Error FakeGDScript::parse_script() {
	using GT = GlobalToken;

	FAKEGDSCRIPT_FAIL_COND_V_MSG(script_state.bytecode_version == -1, ERR_INVALID_DATA, "Bytecode version is invalid");
	Vector<StringName> &identifiers = script_state.identifiers;
	Vector<Variant> &constants = script_state.constants;
	Vector<uint32_t> &tokens = script_state.tokens;
	int variant_ver_major = decomp->get_variant_ver_major();

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
	export_vars.clear();
	int indent = 0;
	uint32_t prev_line = 1;
	uint32_t prev_line_start_column = 1;
	GT prev_token = GT::G_TK_NEWLINE;
	int tab_size = 1;

	// We should only fail when there's something that the decompiler should have already caught; otherwise we'll just warn.
#define FAKEGDSCRIPT_PARSE_FAIL_COND_V_MSG(cond, msg)                                   \
	if (unlikely(cond)) {                                                               \
		error_message = "Failed to parse script: Line " + itos(prev_line) + ": " + msg; \
		return ERR_PARSE_ERROR;                                                         \
	}

	auto handle_newline = [&](int i, GlobalToken curr_token) {
		auto curr_line = script_state.get_token_line(i);
		auto curr_column = script_state.get_token_column(i);
		if (curr_line <= prev_line) {
			curr_line = prev_line + 1; // force new line
		}
		while (curr_line > prev_line) {
			prev_line++;
		}
		if (curr_token == GT::G_TK_NEWLINE) {
			indent = tokens[i] >> GDScriptDecomp::TOKEN_BITS;
		} else if (script_state.bytecode_version >= GDScriptDecomp::GDSCRIPT_2_0_VERSION) {
			prev_token = GT::G_TK_NEWLINE;
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
		prev_token = GT::G_TK_NEWLINE;
	};

	auto get_export_var = [&](int i) {
		while (!decomp->check_next_token(i, tokens, GT::G_TK_PR_VAR) && i < tokens.size()) {
			i++;
		}
		if (i >= tokens.size()) {
			WARN_PRINT(vformat("Line %d: Unexpected end of file while parsing @export", prev_line));
			return OK;
		}
		if (decomp->check_next_token(i, tokens, GT::G_TK_PR_VAR) && decomp->check_next_token(i + 1, tokens, GT::G_TK_IDENTIFIER)) {
			uint32_t identifier = tokens[i + 2] >> GDScriptDecomp::TOKEN_BITS;
			FAKEGDSCRIPT_PARSE_FAIL_COND_V_MSG(identifier >= (uint32_t)identifiers.size(), "Invalid identifier index");
			export_vars.push_back(identifiers[identifier]);
		}
		return OK;
	};

	auto set_abstract = [&](int i) {
		// has to be before the script body and not annotating an inner class
		if (!class_name_used && !extends_used && !func_used && !var_used && !const_used && !decomp->check_next_token(i, tokens, GT::G_TK_PR_CLASS)) {
			abstract = true;
		}
	};
	auto check_new_line = [&](int i) {
		auto ln = script_state.get_token_line(i);
		if (ln != prev_line && ln != 0) {
			return true;
		}
		return false;
	};

	for (int i = 0; i < tokens.size(); i++) {
		uint32_t local_token = tokens[i] & GDScriptDecomp::TOKEN_MASK;
		GlobalToken curr_token = decomp->get_global_token(local_token);
		if (curr_token != GT::G_TK_NEWLINE && check_new_line(i)) {
			handle_newline(i, curr_token);
		}
		switch (curr_token) {
			case GT::G_TK_NEWLINE: {
				handle_newline(i, curr_token);
			} break;
			case GT::G_TK_ANNOTATION: {
				// in GDScript 2.0, the "@tool" annotation has to be the first expression in the file
				// (i.e. before the class body and 'extends' or 'class_name' keywords)
				uint32_t a_id = tokens[i] >> GDScriptDecomp::TOKEN_BITS;
				FAKEGDSCRIPT_PARSE_FAIL_COND_V_MSG(a_id >= (uint32_t)identifiers.size(), "Invalid annotation index");

				const StringName &annotation = identifiers[a_id];
				const String annostr = annotation.get_data();
				bool nothing_used = !func_used && !class_used && !var_used && !const_used && !extends_used && !class_name_used;

				if (nothing_used && annostr == "@tool") {
					tool = true;
				} else if (nothing_used && annostr == "@icon") {
					// @icon("res://path/to/icon.svg")
					if (decomp->check_next_token(i, tokens, GT::G_TK_PARENTHESIS_OPEN) &&
							decomp->check_next_token(i + 1, tokens, GT::G_TK_CONSTANT)) {
						uint32_t string_id = tokens[i + 2] >> GDScriptDecomp::TOKEN_BITS;
						FAKEGDSCRIPT_PARSE_FAIL_COND_V_MSG(string_id >= (uint32_t)identifiers.size(), "Invalid string index");
						icon_path = identifiers[string_id];
						if (icon_path.is_relative_path()) {
							icon_path = script_path.get_base_dir().path_join(icon_path);
						}
					}
				} else if (annostr.contains("@export") && !annostr.ends_with("group") && !annostr.ends_with("category")) {
					Error err = get_export_var(i);
					if (err) {
						return err;
					}
				} else if (annostr == "@abstract") {
					set_abstract(i);
				}
			} break;
			case GT::G_TK_PR_SIGNAL: {
				// only signals at the top level
				if (indent == 0) {
					if (decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
						uint32_t identifier = tokens[i + 1] >> GDScriptDecomp::TOKEN_BITS;
						FAKEGDSCRIPT_PARSE_FAIL_COND_V_MSG(identifier >= (uint32_t)identifiers.size(), "After signal: Invalid identifier index");
						const StringName &signal_name = identifiers[identifier];
						int arg_count = 0;
						if (decomp->check_next_token(i + 1, tokens, GT::G_TK_PARENTHESIS_OPEN)) {
							Vector<Vector<uint32_t>> args;
							arg_count = decomp->get_func_arg_count_and_params(i + 1, tokens, args);
						}
						if (arg_count >= 0) {
							MethodInfo mi = MethodInfo(signal_name);
							for (int64_t j = 0; j < arg_count; j++) {
								mi.arguments.push_back(PropertyInfo(Variant::NIL, "arg" + itos(j)));
							}
							_signals[signal_name] = mi;
						} else {
							WARN_PRINT(vformat("Line %d: Failed to parse signal %s arguments", prev_line, signal_name));
						}
					}
				}
			} break;
			case GT::G_TK_ABSTRACT: {
				if (!is_not_actually_reserved_word(i)) {
					set_abstract(i);
				}
			} break;
			case GT::G_TK_PR_FUNCTION: {
				if (!is_not_actually_reserved_word(i)) {
					func_used = true;

					// only methods at the top level
					if (indent == 0) {
						StringName method_name;
						if (i + 1 < tokens.size() && (script_state.bytecode_version < GDScriptDecomp::GDSCRIPT_2_0_VERSION || (GDScriptDecomp::token_is_valid_v2_func_id(decomp->get_global_token(tokens[i + 1]))))) {
							String method = decomp->get_token_text(script_state, i + 1);
							if (method.is_empty() || method.begins_with("ERROR:")) {
								WARN_PRINT(vformat("Line %d: Failed to parse method:%s", prev_line, method.trim_prefix("ERROR:")));
								continue;
							}
							method_name = method;
						} else {
							// Only warn if this isn't a lambda
							if (!decomp->check_next_token(i, tokens, GT::G_TK_PARENTHESIS_OPEN)) {
								WARN_PRINT(vformat("Line %d: Failed to parse method name", prev_line));
							}
							continue;
						}
						bool is_static = decomp->check_prev_token(i, tokens, GT::G_TK_PR_STATIC);
						bool is_abstract = false;
						if (!is_static && script_state.bytecode_version >= GDScriptDecomp::GDSCRIPT_2_0_VERSION) {
							int j = i;
							while (j < tokens.size() && j > 0) {
								if (decomp->check_prev_token(j, tokens, GT::G_TK_ANNOTATION)) {
									uint32_t a_id = tokens[j - 1] >> GDScriptDecomp::TOKEN_BITS;
									FAKEGDSCRIPT_PARSE_FAIL_COND_V_MSG(a_id >= (uint32_t)identifiers.size(), "Invalid annotation index");
									const StringName &annotation = identifiers[a_id];
									if (annotation == "@abstract") {
										is_abstract = true;
										break;
									}
								} else {
									break;
								}
								j++;
							}
						}
						int arg_count = 0;
						bool is_var_arg = false;
						if (decomp->check_next_token(i + 1, tokens, GT::G_TK_PARENTHESIS_OPEN)) {
							Vector<Vector<uint32_t>> args;
							arg_count = decomp->get_func_arg_count_and_params(i + 1, tokens, args);
							if (!args.is_empty() && args[args.size() - 1].size() > 0) {
								uint32_t first_last_arg_token = args[args.size() - 1].get(0);
								if (decomp->get_global_token(first_last_arg_token) == GT::G_TK_PERIOD_PERIOD_PERIOD) {
									is_var_arg = true;
								}
							}
						} else {
							arg_count = -1;
						}
						if (arg_count >= 0) {
							MethodInfo mi = MethodInfo(method_name);
							for (int64_t j = 0; j < arg_count; j++) {
								// TODO: parse argument types and names
								mi.arguments.push_back(PropertyInfo(Variant::NIL, "arg" + itos(j)));
							}
							mi.flags = is_static ? METHOD_FLAG_STATIC : 0;
							if (is_abstract) {
								mi.flags |= METHOD_FLAG_VIRTUAL_REQUIRED;
							}
							if (is_var_arg) {
								mi.flags |= METHOD_FLAG_VARARG;
							}
							_methods[method_name] = mi;
						} else {
							WARN_PRINT(vformat("Line %d: Failed to parse method %s arguments", prev_line, method_name));
						}
					}
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
				Error err = get_export_var(i);
				if (err) {
					return err;
				}
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
				if (is_not_actually_reserved_word(i)) {
					break;
				}
				// "class_name" can be used literally anywhere in GDScript 1, so we only check it if it's actually a reserved word
				if (global_name.is_empty() && decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
					uint32_t identifier = tokens[i + 1] >> GDScriptDecomp::TOKEN_BITS;
					FAKEGDSCRIPT_PARSE_FAIL_COND_V_MSG(identifier >= (uint32_t)identifiers.size(), "After class_name: Invalid identifier index");
					global_name = identifiers[identifier];
					local_name = global_name;
					globally_available = true;
				}
				class_name_used = true;
			} break;
			case GT::G_TK_PR_EXTENDS: {
				// "extends" is only valid for the global class if it's not in the body (class_name and tool can be used before it)
				// This applies to all versions of GDScript
				if (base_type.is_empty() && !func_used && !class_used && !var_used && !const_used && !extends_used && !is_not_actually_reserved_word(i)) {
					if (decomp->check_next_token(i, tokens, GT::G_TK_CONSTANT)) {
						uint32_t constant = tokens[i + 1] >> GDScriptDecomp::TOKEN_BITS;
						FAKEGDSCRIPT_PARSE_FAIL_COND_V_MSG(constant >= (uint32_t)constants.size(), "After extends: Invalid constant index");
						base_type = constants[constant];
					} else if (decomp->check_next_token(i, tokens, GT::G_TK_IDENTIFIER)) {
						uint32_t identifier = tokens[i + 1] >> GDScriptDecomp::TOKEN_BITS;
						FAKEGDSCRIPT_PARSE_FAIL_COND_V_MSG(identifier >= (uint32_t)identifiers.size(), "After extends: Invalid identifier index");
						base_type = identifiers[identifier];
					} else if (decomp->check_next_token(i, tokens, GT::G_TK_BUILT_IN_TYPE)) {
						base_type = VariantDecoderCompat::get_variant_type_name(tokens[i + 1] >> GDScriptDecomp::TOKEN_BITS, variant_ver_major);
					} else {
						String next_token_name = i + 1 < tokens.size() ? decomp->get_global_token_name(decomp->get_global_token(tokens[i + 1])) : "end of file";
						WARN_PRINT(vformat("Line %d: Invalid extends keyword, next token is %s", prev_line, next_token_name));
					}
				}
			} break;
			default: {
				break;
			}
		}
	}

	if (base_type.is_empty()) {
		base_type = decomp->get_variant_ver_major() < 4 ? "Reference" : "RefCounted";
	}
#if 0 // debug
	print_line(vformat("number of export vars for script %s: %d", script_path, export_vars.size()));
	for (const StringName &E : export_vars) {
		print_line(vformat("Export var: %s", E));
	}
#endif

	return OK;
}

Variant FakeGDScript::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	return {};
}

void FakeGDScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_binary_tokens", "binary_tokens"), &FakeGDScript::load_binary_tokens);
	ClassDB::bind_method(D_METHOD("set_override_bytecode_revision", "revision"), &FakeGDScript::set_override_bytecode_revision);
	ClassDB::bind_method(D_METHOD("get_override_bytecode_revision"), &FakeGDScript::get_override_bytecode_revision);
}

String FakeGDScript::get_script_path() const {
	return script_path;
}

Error FakeGDScript::load_source_code(const String &p_path) {
	script_path = p_path;
	loaded = false;
	if (autoload) {
		return _reload_from_file();
	}
	return OK;
}

String FakeGDScript::get_icon_path() const {
	return icon_path;
}

Error FakeGDScript::load_binary_tokens(const Vector<uint8_t> &p_binary_tokens) {
	is_binary = true;
	loaded = false;
	binary_buffer = p_binary_tokens;
	if (autoload) {
		return reload(false);
	}
	return OK;
}

int FakeGDScript::get_override_bytecode_revision() const {
	return override_bytecode_revision;
}

void FakeGDScript::set_override_bytecode_revision(int p_revision) {
	override_bytecode_revision = p_revision;
}

void FakeGDScript::set_autoload(bool p_autoload) {
	autoload = p_autoload;
}

bool FakeGDScript::is_autoload() const {
	return autoload;
}

bool FakeGDScript::is_loaded() const {
	return loaded;
}

bool FakeGDScript::is_global_class() const {
	return globally_available;
}

FakeGDScript::FakeGDScript() {
	set_original_class("GDScript");
	set_can_instantiate(true);
}
