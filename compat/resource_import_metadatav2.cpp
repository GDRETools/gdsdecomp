#include "resource_import_metadatav2.h"

#include "core/object/class_db.h"

void ResourceImportMetadatav2::set_editor(const String &p_editor) {
	editor = p_editor;
}

String ResourceImportMetadatav2::get_editor() const {
	return editor;
}
void ResourceImportMetadatav2::add_source(const String &p_path, const String &p_md5) {
	add_source_at(p_path, p_md5, sources.size());
}
void ResourceImportMetadatav2::add_source_at(const String &p_path, const String &p_md5, int p_idx) {
	Source s;
	s.md5 = p_md5;
	s.path = p_path;
	ERR_FAIL_COND(p_idx < 0 || p_idx > sources.size());
	if (p_idx == sources.size()) {
		sources.push_back(s);
	} else {
		sources.insert(p_idx, s);
	}
}

String ResourceImportMetadatav2::get_source_path(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, sources.size(), String());
	return sources[p_idx].path;
}
String ResourceImportMetadatav2::get_source_md5(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, sources.size(), String());
	return sources[p_idx].md5;
}

void ResourceImportMetadatav2::set_source_md5(int p_idx, const String &p_md5) {
	ERR_FAIL_INDEX(p_idx, sources.size());
	sources.write[p_idx].md5 = p_md5;
}

void ResourceImportMetadatav2::remove_source(int p_idx) {
	ERR_FAIL_INDEX(p_idx, sources.size());
	sources.remove_at(p_idx);
}

int ResourceImportMetadatav2::get_source_count() const {
	// size was 32-bit in 2.0, so we need to cast to int
	return static_cast<int>(sources.size());
}
void ResourceImportMetadatav2::set_option(const String &p_key, const Variant &p_value) {
	if (p_value.get_type() == Variant::NIL) {
		options.erase(p_key);
		return;
	}

	ERR_FAIL_COND(p_value.get_type() == Variant::OBJECT);
	ERR_FAIL_COND(p_value.get_type() == Variant::RID);

	options[p_key] = p_value;
}

bool ResourceImportMetadatav2::has_option(const String &p_key) const {
	return options.has(p_key);
}

Variant ResourceImportMetadatav2::get_option(const String &p_key) const {
	ERR_FAIL_COND_V(!options.has(p_key), Variant());

	return options[p_key];
}

void ResourceImportMetadatav2::get_options(List<String> *r_options) const {
	for (RBMap<String, Variant>::Element *E = options.front(); E; E = E->next()) {
		r_options->push_back(E->key());
	}
}

PackedStringArray ResourceImportMetadatav2::_get_options() const {
	PackedStringArray option_names;
	option_names.resize(options.size());
	int i = 0;
	for (RBMap<String, Variant>::Element *E = options.front(); E; E = E->next()) {
		option_names.set(i++, E->key());
	}

	return option_names;
}

Dictionary ResourceImportMetadatav2::get_options_as_dictionary() const {
	Dictionary opts;
	for (auto E = options.front(); E; E = E->next()) {
		opts[E->key()] = E->value();
	}
	return opts;
}

Dictionary ResourceImportMetadatav2::to_json() const {
	Dictionary ret;
	Array srcs;
	for (int i = 0; i < sources.size(); i++) {
		Dictionary src;
		src["path"] = sources[i].path;
		src["md5"] = sources[i].md5;
		srcs.push_back(src);
	}
	ret["sources"] = srcs;
	ret["editor"] = editor;

	ret["options"] = get_options_as_dictionary();
	return ret;
}

void ResourceImportMetadatav2::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_editor", "name"), &ResourceImportMetadatav2::set_editor);
	ClassDB::bind_method(D_METHOD("get_editor"), &ResourceImportMetadatav2::get_editor);
	ClassDB::bind_method(D_METHOD("add_source", "path", "md5"), &ResourceImportMetadatav2::add_source, "");
	ClassDB::bind_method(D_METHOD("add_source_at", "path", "md5", "p_idx"), &ResourceImportMetadatav2::add_source_at);
	ClassDB::bind_method(D_METHOD("get_source_path", "idx"), &ResourceImportMetadatav2::get_source_path);
	ClassDB::bind_method(D_METHOD("get_source_md5", "idx"), &ResourceImportMetadatav2::get_source_md5);
	ClassDB::bind_method(D_METHOD("set_source_md5", "idx", "md5"), &ResourceImportMetadatav2::set_source_md5);
	ClassDB::bind_method(D_METHOD("remove_source", "idx"), &ResourceImportMetadatav2::remove_source);
	ClassDB::bind_method(D_METHOD("get_source_count"), &ResourceImportMetadatav2::get_source_count);
	ClassDB::bind_method(D_METHOD("set_option", "key", "value"), &ResourceImportMetadatav2::set_option);
	ClassDB::bind_method(D_METHOD("get_option", "key"), &ResourceImportMetadatav2::get_option);
	ClassDB::bind_method(D_METHOD("get_options"), &ResourceImportMetadatav2::_get_options);
	ClassDB::bind_method(D_METHOD("to_json"), &ResourceImportMetadatav2::to_json);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("from_json", "dict"), &ResourceImportMetadatav2::from_json);
}

ResourceImportMetadatav2::ResourceImportMetadatav2() {
}

Ref<ResourceImportMetadatav2> ResourceImportMetadatav2::from_json(const Dictionary &p_dict) {
	Ref<ResourceImportMetadatav2> v2metadata = memnew(ResourceImportMetadatav2);
	v2metadata->editor = p_dict.get("editor", "");
	Array sources = p_dict.get("sources", Array());
	for (int i = 0; i < sources.size(); i++) {
		v2metadata->add_source(sources[i].operator Dictionary().get("path", ""), sources[i].operator Dictionary().get("md5", ""));
	}
	Dictionary options = p_dict.get("options", Dictionary());
	for (auto &E : options) {
		v2metadata->set_option(E.key, E.value);
	}
	return v2metadata;
}
