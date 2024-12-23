#include "fake_script.h"

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