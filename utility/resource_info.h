#pragma once
#include "compat/resource_import_metadatav2.h"

#include "core/variant/dictionary.h"
#define META_PROPERTY_COMPAT_DATA "metadata/compat"
#define META_COMPAT "compat"

class ResourceInfo : public RefCounted {
	GDCLASS(ResourceInfo, RefCounted);

public:
	enum LoadType {
		ERR = -1,
		FAKE_LOAD,
		NON_GLOBAL_LOAD,
		GLTF_LOAD,
		REAL_LOAD
	};

	enum ResTopologyType {
		MAIN_RESOURCE,
		INTERNAL_RESOURCE,
		UNLOADED_EXTERNAL_RESOURCE
	};
	ResourceUID::ID uid = ResourceUID::INVALID_ID;
	int ver_format = 0;
	int ver_major = 0; //2, 3, 4
	int ver_minor = 0;
	int packed_scene_version = -1;
	LoadType load_type = ERR;
	String original_path;
	String resource_name;
	String type;
	String resource_format;
	String script_class;
	String cached_id;
	Ref<ResourceImportMetadatav2> v2metadata = nullptr;
	ResTopologyType topology_type = MAIN_RESOURCE;
	bool suspect_version = false;
	bool using_real_t_double = false;
	bool using_named_scene_ids = false;
	bool stored_use_real64 = false;
	bool using_uids = false;
	bool stored_big_endian = false;
	bool is_compressed = false;
	Dictionary extra;

	bool using_script_class() const;
	static Ref<ResourceInfo> from_dict(const Dictionary &dict);
	Dictionary to_dict() const;
	void set_on_resource(Ref<Resource> res) const;
	void _set_on_resource(Resource *res) const;
	static Ref<ResourceInfo> get_info_from_resource(Ref<Resource> res);
	static bool resource_has_info(Ref<Resource> res);

	int get_ver_major() const;
	int get_ver_minor() const;
	int get_ver_format() const;
	int get_packed_scene_version() const;
	LoadType get_load_type() const;
	String get_original_path() const;
	String get_resource_name() const;
	String get_type() const;
	String get_resource_format() const;
	String get_script_class() const;
	String get_cached_id() const;
	Ref<ResourceImportMetadatav2> get_v2metadata() const;
	ResTopologyType get_topology_type() const;
	bool get_suspect_version() const;
	bool get_using_real_t_double() const;
	bool get_using_named_scene_ids() const;
	bool get_stored_use_real64() const;
	bool get_using_uids() const;
	bool get_stored_big_endian() const;
	bool get_is_compressed() const;
	Dictionary get_extra() const;
	String _to_string() override;

protected:
	static void _bind_methods();
};

VARIANT_ENUM_CAST(ResourceInfo::LoadType);
VARIANT_ENUM_CAST(ResourceInfo::ResTopologyType);
