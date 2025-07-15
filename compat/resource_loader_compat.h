#pragma once
#include "compat/fake_script.h"
#include "core/io/missing_resource.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"

#include "utility/resource_info.h"

class CompatFormatLoader;
class CompatFormatSaver;
class ResourceCompatConverter;
class ResourceCompatLoader : public Object {
	GDCLASS(ResourceCompatLoader, Object);

	enum {
		MAX_LOADERS = 64,
		MAX_CONVERTERS = 8192,
	};
	static Ref<CompatFormatLoader> loaders[MAX_LOADERS];
	static Ref<ResourceCompatConverter> converters[MAX_CONVERTERS];

	static int loader_count;
	static int converter_count;
	static bool doing_gltf_load;
	static bool globally_available;

protected:
	static Ref<Resource> _fake_load(const String &p_path, const String &p_type_hint = "");
	static Ref<Resource> _non_global_load(const String &p_path, const String &p_type_hint = "");
	static Ref<Resource> _gltf_load(const String &p_path, const String &p_type_hint = "");
	static Ref<Resource> _real_load(const String &p_path, const String &p_type_hint = "", ResourceFormatLoader::CacheMode p_cache_mode = ResourceFormatLoader::CACHE_MODE_REUSE);
	static Dictionary _get_resource_info(const String &p_path, const String &p_type_hint = "");
	static Vector<String> _get_dependencies(const String &p_path, bool p_add_types);

	static void _bind_methods();

public:
	static ResourceInfo::LoadType get_default_load_type();

	static Ref<Resource> fake_load(const String &p_path, const String &p_type_hint = "", Error *r_error = nullptr);
	static Ref<Resource> non_global_load(const String &p_path, const String &p_type_hint = "", Error *r_error = nullptr);
	static Ref<Resource> gltf_load(const String &p_path, const String &p_type_hint = "", Error *r_error = nullptr);
	static Ref<Resource> real_load(const String &p_path, const String &p_type_hint = "", Error *r_error = nullptr, ResourceFormatLoader::CacheMode p_cache_mode = ResourceFormatLoader::CACHE_MODE_REUSE);
	static Ref<Resource> custom_load(const String &p_path, const String &p_type_hint = "", ResourceInfo::LoadType p_type = ResourceInfo::LoadType::REAL_LOAD, Error *r_error = nullptr, bool use_threads = true, ResourceFormatLoader::CacheMode p_cache_mode = ResourceFormatLoader::CACHE_MODE_REUSE);
	static Ref<Resource> load_with_real_resource_loader(const String &p_path, const String &p_type_hint = "", Error *r_error = nullptr, bool use_threads = true, ResourceFormatLoader::CacheMode p_cache_mode = ResourceFormatLoader::CACHE_MODE_REUSE);
	static void add_resource_format_loader(Ref<CompatFormatLoader> p_format_loader, bool p_at_front = false);
	static void remove_resource_format_loader(Ref<CompatFormatLoader> p_format_loader);
	static void add_resource_object_converter(Ref<ResourceCompatConverter> p_converter, bool p_at_front = false);
	static void remove_resource_object_converter(Ref<ResourceCompatConverter> p_converter);
	static Ref<CompatFormatLoader> get_loader_for_path(const String &p_path, const String &p_type_hint);
	static Ref<ResourceCompatConverter> get_converter_for_type(const String &p_type, int ver_major);
	static Ref<ResourceInfo> get_resource_info(const String &p_path, const String &p_type_hint = "", Error *r_error = nullptr);
	static void get_dependencies(const String &p_path, List<String> *p_dependencies, bool p_add_types = false);
	static Error to_text(const String &p_path, const String &p_dst, uint32_t p_flags = 0, const String &original_path = {});
	static Error to_binary(const String &p_path, const String &p_dst, uint32_t p_flags = 0);
	static bool handles_resource(const String &p_path, const String &p_type_hint = "");
	static String get_resource_script_class(const String &p_path);
	static String get_resource_type(const String &p_path);

	static void set_default_gltf_load(bool p_enable);
	static bool is_default_gltf_load();
	static void make_globally_available();
	static void unmake_globally_available();
	static bool is_globally_available();

	static void get_base_extensions_for_type(const String &p_type, List<String> *p_extensions);
	static void get_base_extensions(List<String> *p_extensions, int ver_major = 0);
};

class CompatFormatLoader : public ResourceFormatLoader {
	GDCLASS(CompatFormatLoader, ResourceFormatLoader);

public:
	virtual Ref<Resource> custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error = nullptr, bool use_threads = true, ResourceFormatLoader::CacheMode p_cache_mode = CACHE_MODE_REUSE);
	virtual Ref<ResourceInfo> get_resource_info(const String &p_path, Error *r_error) const;
	virtual bool handles_fake_load() const;
	static ResourceInfo::LoadType get_default_real_load() {
		return ResourceCompatLoader::get_default_load_type();
	}

	static void move_script_property_to_top(List<PropertyInfo> *p_properties) {
		// TODO: remove this when we get proper script loaders
		ERR_FAIL_COND(!p_properties);
		for (List<PropertyInfo>::Element *E = p_properties->front(); E; E = E->next()) {
			if (E->get().name == "script") {
				p_properties->move_to_front(E);
				return;
			}
		}
	}

	static Resource *make_fakescript_or_mising_resource(const String &path, const String &type, const String &scene_id = "") {
		Resource *ret;
		if (type == "Script" || type == "GDScript" || type == "CSharpScript") {
			FakeEmbeddedScript *res{ memnew(FakeEmbeddedScript) };
			res->set_original_class(type);
			ret = res;
		} else {
			MissingResource *res{ memnew(MissingResource) };
			res->set_original_class(type);
			res->set_recording_properties(true);
			ret = res;
		}
		if (!path.is_empty()) {
			ret->set_path_cache(path);
		}
		if (!scene_id.is_empty()) {
			ret->set_scene_unique_id(scene_id);
		}
		return ret;
	}

	static Ref<Resource> create_missing_external_resource(const String &path, const String &type, const ResourceUID::ID uid, const String &scene_id = "") {
		Ref<Resource> res{ make_fakescript_or_mising_resource(path, type) };
		Ref<ResourceInfo> compat;
		compat.instantiate();
		compat->uid = uid;
		compat->type = type;
		compat->cached_id = scene_id;
		compat->topology_type = ResourceInfo::UNLOADED_EXTERNAL_RESOURCE;
		compat->set_on_resource(res);
		return res;
	}

	static Resource *create_missing_main_resource(const String &path, const String &type, const ResourceUID::ID uid) {
		Resource *res{ make_fakescript_or_mising_resource(path, type) };
		Ref<ResourceInfo> compat;
		compat.instantiate();
		compat->uid = uid;
		compat->type = type;
		compat->topology_type = ResourceInfo::MAIN_RESOURCE;
		compat->_set_on_resource(res);
		return res;
	}

	static Resource *create_missing_internal_resource(const String &path, const String &type, const String &scene_id) {
		Resource *res{ make_fakescript_or_mising_resource("", type, scene_id) };
		Ref<ResourceInfo> compat;
		compat.instantiate();
		compat->uid = ResourceUID::INVALID_ID;
		compat->type = type;
		compat->topology_type = ResourceInfo::INTERNAL_RESOURCE;
		compat->_set_on_resource(res);
		return res;
	}

	static bool resource_is_resource(Ref<Resource> p_res, int ver_major) {
		return p_res.is_valid() && !(ver_major <= 2 && (p_res->get_class() == "Image" || p_res->get_class() == "InputEvent"));
	}

	// virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_IGNORE);
	// virtual void get_recognized_extensions_for_type(const String &p_type, List<String> *p_extensions) const;
	// virtual void get_recognized_extensions(List<String> *p_extensions) const;
	// virtual bool handles_type(const String &p_type) const;
	// virtual String get_resource_type(const String &p_path) const;
	// virtual String get_resource_script_class(const String &p_path) const;
	// virtual void get_classes_used(const String &p_path, HashSet<StringName> *r_classes);
	// virtual ResourceUID::ID get_resource_uid(const String &p_path) const;
	// virtual void get_dependencies(const String &p_path, List<String> *p_dependencies, bool p_add_types = false);
	// virtual Error rename_dependencies(const String &p_path, const HashMap<String, String> &p_map);
};

class CompatFormatSaver : public ResourceFormatSaver {
public:
	virtual Error fake_save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags = 0) = 0;
	virtual Error non_global_save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags = 0) = 0;

	// virtual Error save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags = 0);
	// virtual Error set_uid(const String &p_path, ResourceUID::ID p_uid);
	// virtual bool recognize(const Ref<Resource> &p_resource) const;
	// virtual void get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const;
	// virtual bool recognize_path(const Ref<Resource> &p_resource, const String &p_path) const;
};

class ResourceCompatConverter : public RefCounted {
	GDCLASS(ResourceCompatConverter, RefCounted);

public:
	static String get_resource_name(const Ref<Resource> &res, int ver_major);
	virtual Ref<Resource> convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error = nullptr) = 0;
	virtual bool handles_type(const String &p_type, int ver_major) const = 0;
	static Ref<Resource> get_real_from_missing_resource(Ref<MissingResource> mr, ResourceInfo::LoadType load_type);
	static Ref<Resource> set_real_from_missing_resource(Ref<MissingResource> mr, Ref<Resource> res, ResourceInfo::LoadType load_type);
	static bool is_external_resource(Ref<MissingResource> mr);
};
