#include "resource_info.h"

bool ResourceInfo::using_script_class() const {
	return !script_class.is_empty();
}
Ref<ResourceInfo> ResourceInfo::from_dict(const Dictionary &dict) {
	Ref<ResourceInfo> ri = memnew(ResourceInfo);
	ri->uid = dict.get("uid", ResourceUID::INVALID_ID);
	ri->original_path = dict.get("original_path", "");
	ri->resource_name = dict.get("resource_name", "");
	ri->ver_format = dict.get("ver_format", 0);
	ri->ver_major = dict.get("ver_major", 0);
	ri->ver_minor = dict.get("ver_minor", 0);
	ri->packed_scene_version = dict.get("packed_scene_version", 0);
	ri->load_type = static_cast<LoadType>(int(dict.get("load_type", FAKE_LOAD)));
	ri->type = dict.get("type", "");
	ri->resource_format = dict.get("format_type", "");
	ri->script_class = dict.get("script_class", "");
	ri->cached_id = dict.get("cached_id", "");
	ri->v2metadata = dict.get("v2metadata", Ref<ResourceImportMetadatav2>());
	ri->topology_type = static_cast<ResTopologyType>(int(dict.get("topology_type", MAIN_RESOURCE)));
	ri->suspect_version = dict.get("suspect_version", false);
	ri->using_real_t_double = dict.get("using_real_t_double", false);
	ri->using_named_scene_ids = dict.get("using_named_scene_ids", false);
	ri->stored_use_real64 = dict.get("stored_use_real64", false);
	ri->stored_big_endian = dict.get("stored_big_endian", false);
	ri->using_uids = dict.get("using_uids", false);
	ri->is_compressed = dict.get("is_compressed", false);
	ri->extra = dict.get("extra", Dictionary());
	return ri;
}

Dictionary ResourceInfo::to_dict() const {
	Dictionary dict;
	dict["uid"] = uid;
	dict["original_path"] = original_path;
	dict["resource_name"] = resource_name;
	dict["ver_format"] = ver_format;
	dict["ver_major"] = ver_major;
	dict["ver_minor"] = ver_minor;
	dict["packed_scene_version"] = packed_scene_version;
	dict["load_type"] = load_type;
	dict["type"] = type;
	dict["format_type"] = resource_format;
	dict["script_class"] = script_class;
	dict["cached_id"] = cached_id;
	dict["v2metadata"] = v2metadata;
	dict["topology_type"] = int(topology_type);
	dict["suspect_version"] = suspect_version;
	dict["using_real_t_double"] = using_real_t_double;
	dict["using_named_scene_ids"] = using_named_scene_ids;
	dict["stored_use_real64"] = stored_use_real64;
	dict["stored_big_endian"] = stored_big_endian;
	dict["using_uids"] = using_uids;
	dict["is_compressed"] = is_compressed;
	dict["extra"] = extra;
	return dict;
}
void ResourceInfo::set_on_resource(Ref<Resource> res) const {
	res->set_meta(META_COMPAT, this);
}
void ResourceInfo::_set_on_resource(Resource *res) const {
	res->set_meta(META_COMPAT, this);
}
Ref<ResourceInfo> ResourceInfo::get_info_from_resource(Ref<Resource> res) {
	return res->get_meta(META_COMPAT, Ref<ResourceInfo>());
}

bool ResourceInfo::resource_has_info(Ref<Resource> res) {
	return ((Ref<ResourceInfo>)res->get_meta(META_COMPAT, Ref<ResourceInfo>())).is_valid();
}

int ResourceInfo::get_ver_major() const {
	return ver_major;
}

int ResourceInfo::get_ver_minor() const {
	return ver_minor;
}

int ResourceInfo::get_ver_format() const {
	return ver_format;
}

int ResourceInfo::get_packed_scene_version() const {
	return packed_scene_version;
}

ResourceInfo::LoadType ResourceInfo::get_load_type() const {
	return load_type;
}

String ResourceInfo::get_original_path() const {
	return original_path;
}

String ResourceInfo::get_resource_name() const {
	return resource_name;
}

String ResourceInfo::get_type() const {
	return type;
}

String ResourceInfo::get_resource_format() const {
	return resource_format;
}

String ResourceInfo::get_script_class() const {
	return script_class;
}

String ResourceInfo::get_cached_id() const {
	return cached_id;
}

Ref<ResourceImportMetadatav2> ResourceInfo::get_v2metadata() const {
	return v2metadata;
}

ResourceInfo::ResTopologyType ResourceInfo::get_topology_type() const {
	return topology_type;
}

bool ResourceInfo::get_suspect_version() const {
	return suspect_version;
}

bool ResourceInfo::get_using_real_t_double() const {
	return using_real_t_double;
}

bool ResourceInfo::get_using_named_scene_ids() const {
	return using_named_scene_ids;
}

bool ResourceInfo::get_stored_use_real64() const {
	return stored_use_real64;
}

bool ResourceInfo::get_using_uids() const {
	return using_uids;
}

bool ResourceInfo::get_stored_big_endian() const {
	return stored_big_endian;
}

bool ResourceInfo::get_is_compressed() const {
	return is_compressed;
}

Dictionary ResourceInfo::get_extra() const {
	return extra;
}

void ResourceInfo::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("from_dict", "dict"), &ResourceInfo::from_dict);
	ClassDB::bind_method(D_METHOD("to_dict"), &ResourceInfo::to_dict);
	ClassDB::bind_method(D_METHOD("set_on_resource", "res"), &ResourceInfo::set_on_resource);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_info_from_resource", "res"), &ResourceInfo::get_info_from_resource);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("resource_has_info", "res"), &ResourceInfo::resource_has_info);
	ClassDB::bind_method(D_METHOD("using_script_class"), &ResourceInfo::using_script_class);
	ClassDB::bind_method(D_METHOD("get_ver_major"), &ResourceInfo::get_ver_major);
	ClassDB::bind_method(D_METHOD("get_ver_minor"), &ResourceInfo::get_ver_minor);
	ClassDB::bind_method(D_METHOD("get_ver_format"), &ResourceInfo::get_ver_format);
	ClassDB::bind_method(D_METHOD("get_packed_scene_version"), &ResourceInfo::get_packed_scene_version);
	ClassDB::bind_method(D_METHOD("get_load_type"), &ResourceInfo::get_load_type);
	ClassDB::bind_method(D_METHOD("get_original_path"), &ResourceInfo::get_original_path);
	ClassDB::bind_method(D_METHOD("get_resource_name"), &ResourceInfo::get_resource_name);
	ClassDB::bind_method(D_METHOD("get_type"), &ResourceInfo::get_type);
	ClassDB::bind_method(D_METHOD("get_resource_format"), &ResourceInfo::get_resource_format);
	ClassDB::bind_method(D_METHOD("get_script_class"), &ResourceInfo::get_script_class);
	ClassDB::bind_method(D_METHOD("get_cached_id"), &ResourceInfo::get_cached_id);
	ClassDB::bind_method(D_METHOD("get_v2metadata"), &ResourceInfo::get_v2metadata);
	ClassDB::bind_method(D_METHOD("get_topology_type"), &ResourceInfo::get_topology_type);
	ClassDB::bind_method(D_METHOD("get_suspect_version"), &ResourceInfo::get_suspect_version);
	ClassDB::bind_method(D_METHOD("get_using_real_t_double"), &ResourceInfo::get_using_real_t_double);
	ClassDB::bind_method(D_METHOD("get_using_named_scene_ids"), &ResourceInfo::get_using_named_scene_ids);
	ClassDB::bind_method(D_METHOD("get_stored_use_real64"), &ResourceInfo::get_stored_use_real64);
	ClassDB::bind_method(D_METHOD("get_using_uids"), &ResourceInfo::get_using_uids);
	ClassDB::bind_method(D_METHOD("get_stored_big_endian"), &ResourceInfo::get_stored_big_endian);
	ClassDB::bind_method(D_METHOD("get_is_compressed"), &ResourceInfo::get_is_compressed);
	ClassDB::bind_method(D_METHOD("get_extra"), &ResourceInfo::get_extra);
}