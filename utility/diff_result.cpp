#include "diff_result.h"
#include "core/object/object.h"
#include "core/variant/variant.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

void DiffResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_file_diff", "path", "diff"), &DiffResult::set_file_diff);
	ClassDB::bind_method(D_METHOD("get_file_diff", "path"), &DiffResult::get_file_diff);
	ClassDB::bind_method(D_METHOD("get_file_diffs"), &DiffResult::get_file_diffs);

	// Add static method binding for deep_equals
	ClassDB::bind_static_method(get_class_static(), D_METHOD("deep_equals", "a", "b", "exclude_non_storage"), &DiffResult::deep_equals, DEFVAL(true));

	// Add static method binding for get_diff
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_diff", "changed_files_dict"), &DiffResult::get_diff);
}

void DiffResult::set_file_diff(const String &p_path, const Ref<FileDiffResult> &p_diff) {
	file_diffs[p_path] = p_diff;
}

Ref<FileDiffResult> DiffResult::get_file_diff(const String &p_path) const {
	if (file_diffs.has(p_path)) {
		return file_diffs[p_path];
	}
	return Ref<FileDiffResult>();
}

Dictionary DiffResult::get_file_diffs() const {
	Dictionary result;
	for (const auto &pair : file_diffs) {
		result[pair.key] = pair.value;
	}
	return result;
}

HashMap<String, Ref<FileDiffResult>> DiffResult::get_file_diff_map() const {
	return file_diffs;
}

void DiffResult::set_file_diff_map(const HashMap<String, Ref<FileDiffResult>> &p_diffs) {
	file_diffs = p_diffs;
}

void FileDiffResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_type", "type"), &FileDiffResult::set_type);
	ClassDB::bind_method(D_METHOD("get_type"), &FileDiffResult::get_type);
	ClassDB::bind_method(D_METHOD("set_res_old", "res"), &FileDiffResult::set_res_old);
	ClassDB::bind_method(D_METHOD("get_res_old"), &FileDiffResult::get_res_old);
	ClassDB::bind_method(D_METHOD("set_res_new", "res"), &FileDiffResult::set_res_new);
	ClassDB::bind_method(D_METHOD("get_res_new"), &FileDiffResult::get_res_new);
	ClassDB::bind_method(D_METHOD("set_props", "props"), &FileDiffResult::set_props);
	ClassDB::bind_method(D_METHOD("get_props"), &FileDiffResult::get_props);
	ClassDB::bind_method(D_METHOD("set_node_diffs", "diffs"), &FileDiffResult::set_node_diffs);
	ClassDB::bind_method(D_METHOD("get_node_diffs"), &FileDiffResult::get_node_diffs);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "type"), "set_type", "get_type");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "res_old", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), "set_res_old", "get_res_old");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "res_new", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), "set_res_new", "get_res_new");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "props"), "set_props", "get_props");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "node_diffs"), "set_node_diffs", "get_node_diffs");

	// Add static method binding for get_resource_diff
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_resource_diff", "res1", "res2", "structured_changes"), &FileDiffResult::get_resource_diff, DEFVAL(Dictionary()));

	// Add static method binding for get_file_diff
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_file_diff", "old_path", "new_path", "options"), &FileDiffResult::get_file_diff, DEFVAL(Dictionary()));
}

void FileDiffResult::set_type(const String &p_type) {
	type = p_type;
}

String FileDiffResult::get_type() const {
	return type;
}

void FileDiffResult::set_res_old(const Ref<Resource> &p_res) {
	res_old = p_res;
}

Ref<Resource> FileDiffResult::get_res_old() const {
	return res_old;
}

void FileDiffResult::set_res_new(const Ref<Resource> &p_res) {
	res_new = p_res;
}

Ref<Resource> FileDiffResult::get_res_new() const {
	return res_new;
}

void FileDiffResult::set_props(const Ref<ObjectDiffResult> &p_props) {
	props = p_props;
}

Ref<ObjectDiffResult> FileDiffResult::get_props() const {
	return props;
}

void FileDiffResult::set_node_diffs(const Dictionary &p_diffs) {
	node_diffs.clear();
	for (const Variant &key : p_diffs.keys()) {
		node_diffs[key] = p_diffs[key];
	}
}

void FileDiffResult::set_node_diff_map(const HashMap<String, Ref<NodeDiffResult>> &p_diffs) {
	node_diffs = p_diffs;
}

Dictionary FileDiffResult::get_node_diffs() const {
	Dictionary result;
	for (const auto &pair : node_diffs) {
		result[pair.key] = pair.value;
	}
	return result;
}

HashMap<String, Ref<NodeDiffResult>> FileDiffResult::get_node_diff_map() const {
	return node_diffs;
}

void FileDiffResult::set_node_diff(const Ref<NodeDiffResult> &p_diff) {
	node_diffs[String(p_diff->get_path())] = Variant(p_diff);
}

Ref<NodeDiffResult> FileDiffResult::get_node_diff(const String &p_path) const {
	if (node_diffs.has(p_path)) {
		return node_diffs[p_path];
	}
	return Ref<NodeDiffResult>();
}

Ref<FileDiffResult> FileDiffResult::get_resource_diff(Ref<Resource> p_res, Ref<Resource> p_res2, const Dictionary &p_structured_changes) {
	Ref<FileDiffResult> result;
	result.instantiate();
	result->set_res_old(p_res);
	result->set_res_new(p_res2);
	if (p_res.is_null() && p_res2.is_null()) {
		result->set_type("unchanged");
		return result;
	}
	if (p_res.is_null()) {
		result->set_type("added");
		return result;
	}
	if (p_res2.is_null()) {
		result->set_type("deleted");
		return result;
	}
	if (p_res->get_class() != p_res2->get_class()) {
		result->set_type("type_changed");
		return result;
	}
	if (p_res->get_class() != "PackedScene") {
		auto diff = ObjectDiffResult::get_diff_obj((Object *)p_res.ptr(), (Object *)p_res2.ptr(), true, p_structured_changes);
		result->set_type("resource_changed");
		result->set_props(diff);
		return result;
	}
	Ref<PackedScene> p_scene1 = p_res;
	Ref<PackedScene> p_scene2 = p_res2;
	auto scene1 = p_scene1->instantiate();
	auto scene2 = p_scene2->instantiate();
	HashSet<NodePath> paths;
	NodeDiffResult::get_child_node_paths(scene1, paths);
	NodeDiffResult::get_child_node_paths(scene2, paths);
	HashMap<String, Ref<NodeDiffResult>> node_diffs;
	for (auto &path : paths) {
		Ref<NodeDiffResult> value1 = NodeDiffResult::evaluate_node_differences(scene1, scene2, path, p_structured_changes);
		if (value1.is_valid()) {
			node_diffs[String(path)] = value1;
		}
	}
	result->set_type("scene_changed");
	result->set_node_diff_map(node_diffs);
	return result;
}

void ObjectDiffResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_old_object", "old_object"), &ObjectDiffResult::set_old_object);
	ClassDB::bind_method(D_METHOD("get_old_object"), &ObjectDiffResult::get_old_object);
	ClassDB::bind_method(D_METHOD("set_new_object", "new_object"), &ObjectDiffResult::set_new_object);
	ClassDB::bind_method(D_METHOD("get_new_object"), &ObjectDiffResult::get_new_object);
	ClassDB::bind_method(D_METHOD("set_property_diffs", "property_diffs"), &ObjectDiffResult::set_property_diffs);
	ClassDB::bind_method(D_METHOD("get_property_diffs"), &ObjectDiffResult::get_property_diffs);
	ClassDB::bind_method(D_METHOD("set_property_diff", "diff"), &ObjectDiffResult::set_property_diff);
	ClassDB::bind_method(D_METHOD("get_property_diff", "name"), &ObjectDiffResult::get_property_diff);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "old_object"), "set_old_object", "get_old_object");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "new_object"), "set_new_object", "get_new_object");
	// Add static method binding for get_diff_obj
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_diff_obj", "a", "b", "exclude_non_storage", "structured_changes"), &ObjectDiffResult::get_diff_obj, DEFVAL(true), DEFVAL(Dictionary()));
}

void ObjectDiffResult::set_old_object(Object *p_old_object) {
	old_object = p_old_object;
}

Object *ObjectDiffResult::get_old_object() const {
	return old_object;
}

void ObjectDiffResult::set_new_object(Object *p_new_object) {
	new_object = p_new_object;
}

Object *ObjectDiffResult::get_new_object() const {
	return new_object;
}

void ObjectDiffResult::set_property_diffs(const Dictionary &p_property_diffs) {
	property_diffs.clear();
	for (const Variant &key : p_property_diffs.keys()) {
		property_diffs[key] = p_property_diffs[key];
	}
}

void ObjectDiffResult::set_property_diff_map(const HashMap<String, Variant> &p_property_diffs) {
	property_diffs = p_property_diffs;
}

Dictionary ObjectDiffResult::get_property_diffs() const {
	Dictionary result;
	for (const auto &pair : property_diffs) {
		result[pair.key] = pair.value;
	}
	return result;
}

HashMap<String, Variant> ObjectDiffResult::get_property_diff_map() const {
	return property_diffs;
}

void ObjectDiffResult::set_property_diff(const Ref<PropertyDiffResult> &p_diff) {
	property_diffs[p_diff->get_name()] = Variant(p_diff);
}

Ref<PropertyDiffResult> ObjectDiffResult::get_property_diff(const String &p_name) const {
	if (property_diffs.has(p_name)) {
		return property_diffs[p_name];
	}
	return Ref<PropertyDiffResult>();
}

ObjectDiffResult::ObjectDiffResult() {
}

ObjectDiffResult::ObjectDiffResult(Object *p_old_object, Object *p_new_object, const Dictionary &p_property_diffs) {
	old_object = p_old_object;
	new_object = p_new_object;
	for (const auto &pair : p_property_diffs.keys()) {
		property_diffs[pair] = p_property_diffs[pair];
	}
}

Ref<ObjectDiffResult> ObjectDiffResult::get_diff_obj(Object *a, Object *b, bool exclude_non_storage, const Dictionary &p_structured_changes) {
	Ref<ObjectDiffResult> diff;
	diff.instantiate();
	List<PropertyInfo> p_list_a;
	List<PropertyInfo> p_list_b;
	bool has_script_instance = false;
	diff->set_old_object(a);
	diff->set_new_object(b);
	a->get_property_list(&p_list_a, false);
	b->get_property_list(&p_list_b, false);
	if (a->get_script_instance()) {
		a->get_script_instance()->get_property_list(&p_list_a);
		a->notification(Node::NOTIFICATION_READY);
		a->get_script_instance()->notification(Node::NOTIFICATION_READY);
	}
	if (b->get_script_instance()) {
		b->get_script_instance()->get_property_list(&p_list_b);
		b->notification(Node::NOTIFICATION_READY);
		b->get_script_instance()->notification(Node::NOTIFICATION_READY);
	}
	// diff is key: [old_value, new_value]
	HashSet<String> prop_names;
	// TODO: handle PROPERTY_USAGE_NO_EDITOR, PROPERTY_USAGE_INTERNAL, etc.
	for (auto &prop : p_list_a) {
		if (exclude_non_storage && !(prop.usage & PROPERTY_USAGE_STORAGE)) {
			continue;
		}
		prop_names.insert(prop.name);
	}
	for (auto &prop : p_list_b) {
		if (exclude_non_storage && !(prop.usage & PROPERTY_USAGE_STORAGE)) {
			continue;
		}
		prop_names.insert(prop.name);
	}
	for (auto &prop : prop_names) {
		bool a_valid = false;
		bool b_valid = false;
		auto prop_a = a->get(prop, &a_valid);
		auto prop_b = b->get(prop, &b_valid);
		if (!a_valid && !b_valid) {
			continue;
		}
		if (!a_valid) {
			diff->set_property_diff(memnew(PropertyDiffResult(prop, "deleted", Variant(), prop_b, a, b)));
		} else if (!b_valid) {
			diff->set_property_diff(memnew(PropertyDiffResult(prop, "added", prop_a, Variant(), a, b)));
		} else if (!DiffResult::deep_equals(prop_a, prop_b)) {
			diff->set_property_diff(memnew(PropertyDiffResult(prop, "changed", prop_a, prop_b, a, b)));
		}
	}
	return diff;
}

void NodeDiffResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_path", "path"), &NodeDiffResult::set_path);
	ClassDB::bind_method(D_METHOD("get_path"), &NodeDiffResult::get_path);
	ClassDB::bind_method(D_METHOD("set_type", "type"), &NodeDiffResult::set_type);
	ClassDB::bind_method(D_METHOD("get_type"), &NodeDiffResult::get_type);
	ClassDB::bind_method(D_METHOD("set_props", "props"), &NodeDiffResult::set_props);
	ClassDB::bind_method(D_METHOD("get_props"), &NodeDiffResult::get_props);
	ClassDB::bind_method(D_METHOD("set_old_object", "old_object"), &NodeDiffResult::set_old_object);
	ClassDB::bind_method(D_METHOD("get_old_object"), &NodeDiffResult::get_old_object);
	ClassDB::bind_method(D_METHOD("set_new_object", "new_object"), &NodeDiffResult::set_new_object);
	ClassDB::bind_method(D_METHOD("get_new_object"), &NodeDiffResult::get_new_object);

	// Add static method binding for evaluate_node_differences
	ClassDB::bind_static_method(get_class_static(), D_METHOD("evaluate_node_differences", "scene1", "scene2", "path", "structured_changes"), &NodeDiffResult::evaluate_node_differences, DEFVAL(Dictionary()));

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "path"), "set_path", "get_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "type"), "set_type", "get_type");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "props"), "set_props", "get_props");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "old_object"), "set_old_object", "get_old_object");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "new_object"), "set_new_object", "get_new_object");
}

void NodeDiffResult::set_path(const NodePath &p_path) {
	path = p_path;
}

NodePath NodeDiffResult::get_path() const {
	return path;
}

void NodeDiffResult::set_type(const String &p_type) {
	type = p_type;
}

String NodeDiffResult::get_type() const {
	return type;
}

void NodeDiffResult::set_props(const Ref<ObjectDiffResult> &p_props) {
	props = p_props;
}

Ref<ObjectDiffResult> NodeDiffResult::get_props() const {
	return props;
}

Object *NodeDiffResult::get_old_object() const {
	return old_object;
}

void NodeDiffResult::set_old_object(Object *p_old_object) {
	old_object = p_old_object;
}

Object *NodeDiffResult::get_new_object() const {
	return new_object;
}

void NodeDiffResult::set_new_object(Object *p_new_object) {
	new_object = p_new_object;
}

NodeDiffResult::NodeDiffResult() {
}

NodeDiffResult::NodeDiffResult(const NodePath &p_path, const String &p_type, Object *p_old_object, Object *p_new_object, const Ref<ObjectDiffResult> &p_props) {
	path = p_path;
	type = p_type;
	old_object = p_old_object;
	new_object = p_new_object;
	props = p_props;
}

void NodeDiffResult::get_child_node_paths(Node *node_a, HashSet<NodePath> &paths, const String &curr_path) {
	for (int i = 0; i < node_a->get_child_count(); i++) {
		auto child_a = node_a->get_child(i);
		auto new_path = curr_path.path_join(child_a->get_name());
		paths.insert(new_path);
		get_child_node_paths(child_a, paths, new_path);
	}
}

Ref<NodeDiffResult> NodeDiffResult::evaluate_node_differences(Node *scene1, Node *scene2, const NodePath &path, const Dictionary &p_structured_changes) {
	Ref<NodeDiffResult> result;
	result.instantiate();
	bool is_root = path == NodePath(".") || path.is_empty();
	Node *node1 = scene1;
	Node *node2 = scene2;
	if (!is_root) {
		if (node1->has_node(path)) {
			node1 = node1->get_node(path);
		} else {
			node1 = nullptr;
		}
		if (node2->has_node(path)) {
			node2 = node2->get_node(path);
		} else {
			node2 = nullptr;
		}
		result->set_path(path);
	} else {
		result->set_path({ "." });
	}
	result->set_old_object(node1);
	result->set_new_object(node2);
	if (node1 == nullptr) {
		result->set_type("node_added");
		return result;
	}
	if (node2 == nullptr) {
		result->set_type("node_deleted");
		return result;
	}

	// Pass options to get_diff_obj
	bool exclude_non_storage = p_structured_changes.has("exclude_non_storage") ? (bool)p_structured_changes["exclude_non_storage"] : true;
	auto diff = ObjectDiffResult::get_diff_obj(node1, node2, exclude_non_storage);

	if (diff->get_property_diffs().size() > 0) {
		result->set_type("node_changed");
		result->set_props(diff);
		return result;
	}
	return Ref<NodeDiffResult>();
}

void PropertyDiffResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_name", "name"), &PropertyDiffResult::set_name);
	ClassDB::bind_method(D_METHOD("get_name"), &PropertyDiffResult::get_name);
	ClassDB::bind_method(D_METHOD("set_change_type", "change_type"), &PropertyDiffResult::set_change_type);
	ClassDB::bind_method(D_METHOD("get_change_type"), &PropertyDiffResult::get_change_type);
	ClassDB::bind_method(D_METHOD("set_old_value", "old_value"), &PropertyDiffResult::set_old_value);
	ClassDB::bind_method(D_METHOD("get_old_value"), &PropertyDiffResult::get_old_value);
	ClassDB::bind_method(D_METHOD("set_new_value", "new_value"), &PropertyDiffResult::set_new_value);
	ClassDB::bind_method(D_METHOD("get_new_value"), &PropertyDiffResult::get_new_value);
	ClassDB::bind_method(D_METHOD("set_old_object", "old_object"), &PropertyDiffResult::set_old_object);
	ClassDB::bind_method(D_METHOD("get_old_object"), &PropertyDiffResult::get_old_object);
	ClassDB::bind_method(D_METHOD("set_new_object", "new_object"), &PropertyDiffResult::set_new_object);
	ClassDB::bind_method(D_METHOD("get_new_object"), &PropertyDiffResult::get_new_object);
}

PropertyDiffResult::PropertyDiffResult() {
}

void PropertyDiffResult::set_name(const String &p_name) {
	name = p_name;
}

String PropertyDiffResult::get_name() const {
	return name;
}

void PropertyDiffResult::set_change_type(const String &p_change_type) {
	change_type = p_change_type;
}

String PropertyDiffResult::get_change_type() const {
	return change_type;
}

void PropertyDiffResult::set_old_value(const Variant &p_old_value) {
	old_value = p_old_value;
}

Variant PropertyDiffResult::get_old_value() const {
	return old_value;
}

void PropertyDiffResult::set_new_value(const Variant &p_new_value) {
	new_value = p_new_value;
}

Variant PropertyDiffResult::get_new_value() const {
	return new_value;
}

void PropertyDiffResult::set_old_object(Object *p_old_object) {
	old_object = p_old_object;
}

Object *PropertyDiffResult::get_old_object() const {
	return old_object;
}

void PropertyDiffResult::set_new_object(Object *p_new_object) {
	new_object = p_new_object;
}

Object *PropertyDiffResult::get_new_object() const {
	return new_object;
}

PropertyDiffResult::PropertyDiffResult(const String &p_name, const String &p_change_type, const Variant &p_old_value, const Variant &p_new_value, Object *p_old_object, Object *p_new_object) {
	name = p_name;
	change_type = p_change_type;
	old_value = p_old_value;
	new_value = p_new_value;
	old_object = p_old_object;
	new_object = p_new_object;
}

static Vector<Variant> get_subresources(Ref<Resource> res) {
	Vector<Variant> subresources;
}

bool DiffResult::deep_equals(Variant a, Variant b, bool exclude_non_storage) {
	if (a.get_type() != b.get_type()) {
		return false;
	}
	// we only check for Arrays, Objects, and Dicts; the rest have the overloaded == operator
	switch (a.get_type()) {
		case Variant::NIL: {
			return true;
		}
		case Variant::ARRAY: {
			Array arr_a = a;
			Array arr_b = b;
			if (arr_a.size() != arr_b.size()) {
				return false;
			}
			for (int i = 0; i < arr_a.size(); i++) {
				if (!deep_equals(arr_a[i], arr_b[i], exclude_non_storage)) {
					return false;
				}
			}
			break;
		}
		case Variant::DICTIONARY: {
			Dictionary dict_a = a;
			Dictionary dict_b = b;
			if (dict_a.size() != dict_b.size()) {
				return false;
			}
			for (const Variant &key : dict_a.keys()) {
				if (!dict_b.has(key)) {
					return false;
				}
				if (!deep_equals(dict_a[key], dict_b[key], exclude_non_storage)) {
					return false;
				}
			}
			break;
		}
		case Variant::OBJECT: {
			Object *obj_a = a;
			Object *obj_b = b;
			if (obj_a == obj_b) {
				return true;
			}
			if (obj_a == nullptr || obj_b == nullptr) {
				return false;
			}
			if (obj_a->get_class() != obj_b->get_class()) {
				return false;
			}
			List<PropertyInfo> p_list_a;
			List<PropertyInfo> p_list_b;
			obj_a->get_property_list(&p_list_a, false);
			obj_b->get_property_list(&p_list_b, false);
			if (p_list_a.size() != p_list_b.size()) {
				return false;
			}
			for (auto &prop : p_list_a) {
				if (exclude_non_storage && !(prop.usage & PROPERTY_USAGE_STORAGE)) {
					continue;
				}
				auto prop_name = prop.name;
				if (!deep_equals(obj_a->get(prop_name), obj_b->get(prop_name), exclude_non_storage)) {
					return false;
				}
			}
			break;
		}
		default: {
			return a == b;
		}
	}
	return true;
}

static Error load_resource(const String &p_path, Ref<Resource> &res) {
	if (!FileAccess::exists(p_path)) {
		return ERR_FILE_NOT_FOUND;
	}
	{
		Ref<FileAccess> fa = FileAccess::open(p_path, FileAccess::READ);
		if (fa.is_null() || fa->get_length() < 4) {
			return ERR_FILE_NOT_FOUND;
		}
	}
	Error error;
	res = ResourceLoader::load(p_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &error);
	if (error == OK && res.is_null()) {
		return ERR_FILE_CORRUPT;
	}
	return error;
}

Ref<FileDiffResult> FileDiffResult::get_file_diff(const String &p_path, const String &p_path2, const Dictionary &p_options) {
	Ref<Resource> res1;
	Ref<Resource> res2;
	Error error1 = load_resource(p_path, res1);
	Error error2 = load_resource(p_path2, res2);
	Ref<FileDiffResult> result;
	ERR_FAIL_COND_V_MSG(error1 != OK && error2 != OK, result, "Failed to load resources at path " + p_path + " and " + p_path2);
	if (error1 != OK) {
		result.instantiate();
		result->set_type("added");
		result->set_res_new(res2);
	} else if (error2 != OK) {
		result.instantiate();
		result->set_type("deleted");
		result->set_res_old(res1);
	} else {
		result = get_resource_diff(res1, res2, p_options);
	}
	return result;
}

Ref<DiffResult> DiffResult::get_diff(Dictionary changed_files_dict) {
	Ref<DiffResult> result;
	result.instantiate();
	Array files = changed_files_dict["files"];
	for (const auto &d : files) {
		Dictionary dict = d;
		if (dict.size() == 0) {
			continue;
		}
		String change_type = dict["change"];
		String path = dict["path"];
		auto old_content = dict["old_content"];
		auto new_content = dict["new_content"];
		auto structured_changes = dict["scene_changes"];
		if (change_type == "modified") {
			// check both the old and the new content to see what the file sizes are
			auto faold = FileAccess::open(old_content, FileAccess::READ);
			auto fanew = FileAccess::open(new_content, FileAccess::READ);
			if (faold.is_null() || fanew.is_null()) {
				continue;
			}
			auto old_size = faold->get_length();
			auto new_size = fanew->get_length();
			if (old_size < 4 && new_size < 4) {
				ERR_FAIL_COND_V(old_size < 4 && new_size < 4, result);
			}
			if (old_size < 4) {
				change_type = "added";
			} else if (new_size < 4) {
				change_type = "deleted";
			} else {
				auto diff = FileDiffResult::get_file_diff(old_content, new_content, structured_changes);
				if (diff.is_null()) {
					continue;
				}
				result->set_file_diff(path, diff);
			}
		}

		if (change_type == "added" || change_type == "deleted") {
			Ref<FileDiffResult> file_diff;
			file_diff.instantiate();
			file_diff->set_type(change_type);
			Error error = OK;
			if (change_type == "added") {
				file_diff->set_res_new(ResourceLoader::load(new_content, "", ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &error));
			} else {
				file_diff->set_res_old(ResourceLoader::load(old_content, "", ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP, &error));
			}
			if (error != OK) {
				print_error("Failed to load resource at path " + path);
				continue;
			}
			result->set_file_diff(path, file_diff);
		}
	}
	return result;
}

Ref<DiffResult> DiffResult::get_diff_from_list(const HashMap<String, String> &p_files) {
	Ref<DiffResult> result;
	result.instantiate();
	for (const auto &d : p_files) {
		result = FileDiffResult::get_file_diff(d.key, d.value);
		if (result.is_null()) {
			continue;
		}
		result->set_file_diff(d.key, result);
	}
	return result;
}
