#include "fake_script.h"

#include "compat/resource_loader_compat.h"
#include "compat/variant_decoder_compat.h"
#include "core/io/missing_resource.h"
#include "core/object/object.h"
#include "core/string/ustring.h"
#include "modules/gdscript/gdscript.h"
#include "utility/resource_info.h"
#include <utility/gdre_settings.h>

// FakeEmbeddedScript

String FakeScript::_get_normalized_path() const {
	String path = get_path();
	if (path.is_empty() || !path.is_resource_file()) {
		return "";
	}
	String ext = path.get_extension().to_lower();
	if (ext == "gdc" || ext == "gde") {
		path = path.get_basename() + ".gd";
	}
	return path;
}

bool FakeScript::_get(const StringName &p_name, Variant &r_ret) const {
	if (p_name == "script/source") {
		r_ret = get_source_code();
		return true;
	}
	if (!properties.has(p_name)) {
		return false;
	}
	r_ret = properties[p_name];
	return true;
}

bool FakeScript::_set(const StringName &p_name, const Variant &p_value) {
	if (p_name == "script/source") {
		set_source_code(p_value);
		return true;
	}

	if (!properties.has(p_name)) {
		properties.insert(p_name, p_value);
		return true;
	}

	properties[p_name] = p_value;
	return true;
}

void FakeScript::_get_property_list(List<PropertyInfo> *p_list) const {
	p_list->push_back(PropertyInfo(Variant::STRING, "script/source", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
	for (const KeyValue<StringName, Variant> &E : properties) {
		p_list->push_back(PropertyInfo(E.value.get_type(), E.key));
	}
}

StringName FakeScript::get_global_name() const {
	return GDRESettings::get_singleton()->get_cached_script_class(_get_normalized_path());
}

bool FakeScript::inherits_script(const Ref<Script> &p_script) const {
	return true;
}

StringName FakeScript::get_instance_base_type() const {
	StringName base = GDRESettings::get_singleton()->get_cached_script_base(_get_normalized_path());
	if (!base.is_empty()) {
		String path = GDRESettings::get_singleton()->get_path_for_script_class(base);
		while (!path.is_empty()) {
			base = GDRESettings::get_singleton()->get_cached_script_class(path);
			path = GDRESettings::get_singleton()->get_path_for_script_class(base);
		}
	}
	return base;
}

bool FakeScript::can_instantiate() const {
	return can_instantiate_instance && GDRESettings::get_singleton()->get_ver_major() >= 4;
}

void FakeScript::set_can_instantiate(bool p_can_instantiate) {
	can_instantiate_instance = p_can_instantiate;
}

ScriptInstance *FakeScript::instance_create(Object *p_this) {
	if (!can_instantiate()) {
		return nullptr;
	}
	auto instance = memnew(FakeScriptInstance());
	instance->script = Ref<FakeScript>(this);
	instance->owner = p_this;
	instance->is_fake_embedded = true;
	return instance;
}

PlaceHolderScriptInstance *FakeScript::placeholder_instance_create(Object *p_this) {
	PlaceHolderScriptInstance *si = memnew(PlaceHolderScriptInstance(/*GDScriptLanguage::get_singleton()*/ nullptr, Ref<Script>(this), p_this));
	return si;
}

bool FakeScript::instance_has(const Object *p_this) const {
	return true;
}

bool FakeScript::has_source_code() const {
	return !source.is_empty();
}

void FakeScript::set_source_code(const String &p_code) {
	source = p_code;
}

String FakeScript::get_source_code() const {
	return source;
}

String FakeScript::get_script_path() const {
	return get_path();
}

Error FakeScript::load_source_code(const String &p_path) {
	// should never be called on FakeScript
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "Not implemented!!!!");
}

String FakeScript::get_error_message() const {
	return error_message;
}

bool FakeScript::is_loaded() const {
	return false;
}

void FakeScript::set_original_class(const String &p_class) {
	original_class = p_class;
}

String FakeScript::get_original_class() const {
	return original_class;
}

void FakeScript::set_load_type(ResourceInfo::LoadType p_load_type) {
	load_type = p_load_type;
}

ResourceInfo::LoadType FakeScript::get_load_type() const {
	return load_type;
}

Ref<GDScriptNativeClass> FakeScript::get_native_script(StringName base_type) {
	StringName class_name = ClassDB::get_compatibility_remapped_class(base_type);
	if (!class_name.is_empty()) {
		if (GDScriptLanguage::get_singleton()->get_global_map().has(class_name)) {
			int native_idx = GDScriptLanguage::get_singleton()->get_global_map()[class_name];
			if (native_idx >= 0) {
				return GDScriptLanguage::get_singleton()->get_global_array()[native_idx];
			}
		}
	}
	return nullptr;
}

StringName FakeScript::get_direct_base_type() const {
	return base_type;
}

void FakeScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_script_path"), &FakeScript::get_script_path);
	ClassDB::bind_method(D_METHOD("load_source_code", "path"), &FakeScript::load_source_code);
	ClassDB::bind_method(D_METHOD("is_loaded"), &FakeScript::is_loaded);
	ClassDB::bind_method(D_METHOD("set_original_class", "class_name"), &FakeScript::set_original_class);
	ClassDB::bind_method(D_METHOD("get_original_class"), &FakeScript::get_original_class);
	ClassDB::bind_method(D_METHOD("get_error_message"), &FakeScript::get_error_message);
	ClassDB::bind_method(D_METHOD("get_direct_base_type"), &FakeScript::get_direct_base_type);
}

#undef FAKEGDSCRIPT_FAIL_COND_V_MSG
#undef FAKEGDSCRIPT_FAIL_V_MSG
#undef FAKEGDSCRIPT_FAIL_COND_MSG

// FakeGDScriptInstance implementations

void FakeScriptInstance::update_cached_prop_names() {
	if (!_cached_prop_names_valid) {
		_cached_prop_info.clear();
		List<PropertyInfo> members;
		script->get_script_property_list(&members);
		for (const PropertyInfo &E : members) {
			Variant def;
			_cached_prop_info.insert(E.name, E);
		}
		for (const auto &E : _cached_prop_info) {
			if (properties.has(E.key)) {
				continue;
			}
			Variant def;
			if (script->get_property_default_value(E.key, def)) {
				properties.insert(E.key, def);
			}
		}
		_cached_prop_names_valid = true;
	}
}

bool FakeScriptInstance::has_cached_prop_name(const StringName &p_name) const {
	return _cached_prop_info.has(p_name);
}

bool FakeScriptInstance::set(const StringName &p_name, const Variant &p_value) {
	if (!owner) {
		return false;
	}
	if (is_fake_embedded) {
		MissingResource *mres = Object::cast_to<MissingResource>(owner);
		if (mres && mres->is_recording_properties()) {
			// let mres handle it
			return false;
		}
		if (!properties.has(p_name)) {
			// check if it's a property of the owning object
			List<PropertyInfo> owner_properties;
			owner->get_property_list(&owner_properties);
			for (const PropertyInfo &pi : owner_properties) {
				if (pi.name == p_name) {
					return false;
				}
			}
		}
		properties[p_name] = p_value;
		return true;
	} // else
	if (!has_cached_prop_name(p_name)) {
		return false;
	}
	properties[p_name] = p_value;
	return true;
}

bool FakeScriptInstance::get(const StringName &p_name, Variant &r_ret) const {
	if (!properties.has(p_name)) {
		if (!is_fake_embedded && has_cached_prop_name(p_name)) {
			r_ret = Variant();
			return true;
		}
		return false;
	}
	r_ret = properties[p_name];
	return true;
}

void FakeScriptInstance::get_property_list(List<PropertyInfo> *p_properties) const {
	for (const auto &E : _cached_prop_info) {
		p_properties->push_back(E.value);
	}
	for (const KeyValue<StringName, Variant> &E : properties) {
		if (!_cached_prop_info.has(E.key)) {
			p_properties->push_back(PropertyInfo(E.value.get_type(), E.key));
		}
	}
}

Variant::Type FakeScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
	if (_cached_prop_info.has(p_name)) {
		if (r_is_valid) {
			*r_is_valid = true;
		}
		return _cached_prop_info[p_name].type;
	}
	if (properties.has(p_name)) {
		if (r_is_valid) {
			*r_is_valid = true;
		}
		return properties[p_name].get_type();
	}
	if (r_is_valid) {
		*r_is_valid = false;
	}
	return Variant::NIL;
}

void FakeScriptInstance::validate_property(PropertyInfo &p_property) const {
	// No validation needed for fake script
}

bool FakeScriptInstance::property_can_revert(const StringName &p_name) const {
	return false;
}

bool FakeScriptInstance::property_get_revert(const StringName &p_name, Variant &r_ret) const {
	return false;
}

Object *FakeScriptInstance::get_owner() {
	return owner;
}

void FakeScriptInstance::get_property_state(List<Pair<StringName, Variant>> &state) {
	for (const auto &E : _cached_prop_info) {
		if (!properties.has(E.key)) {
			state.push_back(Pair<StringName, Variant>(E.key, Variant()));
		} else {
			state.push_back(Pair<StringName, Variant>(E.key, properties[E.key]));
		}
	}

	for (const KeyValue<StringName, Variant> &E : properties) {
		if (!_cached_prop_info.has(E.key)) {
			state.push_back(Pair<StringName, Variant>(E.key, E.value));
		}
	}
}

void FakeScriptInstance::get_method_list(List<MethodInfo> *p_list) const {
	script->get_script_method_list(p_list);
}

bool FakeScriptInstance::has_method(const StringName &p_method) const {
	Ref<Script> s = script;
	while (s.is_valid()) {
		if (s->has_method(p_method)) {
			return true;
		}
		s = s->get_base_script();
	}
	return false;
}

int FakeScriptInstance::get_method_argument_count(const StringName &p_method, bool *r_is_valid) const {
	Ref<Script> s = script;
	while (s.is_valid()) {
		bool valid = false;
		int ret = s->get_script_method_argument_count(p_method, &valid);
		if (valid) {
			if (r_is_valid) {
				*r_is_valid = true;
			}
			return ret;
		}

		s = s->get_base_script();
	}

	if (r_is_valid) {
		*r_is_valid = false;
	}
	return 0;
}

Variant FakeScriptInstance::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	if (has_method(p_method)) {
		return script->callp(p_method, p_args, p_argcount, r_error);
	}
	r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
	return {};
}

Variant FakeScriptInstance::call_const(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	return callp(p_method, p_args, p_argcount, r_error);
}

void FakeScriptInstance::notification(int p_notification, bool p_reversed) {
	// No notifications in fake script
}

void FakeScriptInstance::property_set_fallback(const StringName &p_name, const Variant &p_value, bool *r_valid) {
	if (r_valid) {
		*r_valid = true; // fake it
	}
}

Variant FakeScriptInstance::property_get_fallback(const StringName &p_name, bool *r_valid) {
	Variant ret;
	bool valid = get(p_name, ret);
	if (r_valid) {
		*r_valid = valid;
	}
	return ret;
}

const Variant FakeScriptInstance::get_rpc_config() const {
	return Variant();
}

ScriptLanguage *FakeScriptInstance::get_language() {
	return nullptr;
}

FakeScriptInstance::~FakeScriptInstance() {
	// Cleanup if needed
}

Ref<Script> FakeScriptInstance::get_script() const {
	return script;
}
