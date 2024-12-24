/**************************************************************************/
/*  resource_format_binary.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef RESOURCE_COMPAT_BINARY_H
#define RESOURCE_COMPAT_BINARY_H

#include "compat/resource_import_metadatav2.h"
#include "compat/resource_loader_compat.h"

#include "core/io/file_access.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "scene/resources/packed_scene.h"
#include "utility/resource_info.h"

class ResourceLoaderCompatBinary {
	bool translation_remapped = false;
	String local_path;
	String res_path;
	String type;
	Ref<Resource> resource;
	uint32_t ver_format = 0;

	Ref<FileAccess> f;

	uint64_t importmd_ofs = 0;

	ResourceUID::ID uid = ResourceUID::INVALID_ID;

	Vector<char> str_buf;
	List<Ref<Resource>> resource_cache;

	Vector<StringName> string_map;

	StringName _get_string();

	struct ExtResource {
		String path;
		String type;
		ResourceUID::ID uid = ResourceUID::INVALID_ID;
		Ref<ResourceLoader::LoadToken> load_token;
		Ref<Resource> fallback;
	};

	Ref<ResourceImportMetadatav2> imd;

	uint32_t ver_major;
	uint32_t ver_minor;
	int packed_scene_version = -1;
	bool suspect_version = false;

	bool using_script_class = false;
	bool using_real_t_double = false;
	bool stored_use_real64 = false;
	bool stored_big_endian = false;
	bool using_named_scene_ids = false;
	bool using_uids = false;
	bool is_compressed = false;
	String script_class;
	bool use_sub_threads = false;
	float *progress = nullptr;
	Vector<ExtResource> external_resources;

	ResourceInfo::LoadType load_type = ResourceInfo::FAKE_LOAD;

	struct IntResource {
		String path;
		uint64_t offset;
	};

	Vector<IntResource> internal_resources;
	HashMap<String, Ref<Resource>> internal_index_cache;

	String get_unicode_string();
	void _advance_padding(uint32_t p_len);

	HashMap<String, String> remaps;
	Error error = OK;

	ResourceFormatLoader::CacheMode cache_mode = ResourceFormatLoader::CACHE_MODE_REUSE;
	ResourceFormatLoader::CacheMode cache_mode_for_external = ResourceFormatLoader::CACHE_MODE_REUSE;

	friend class ResourceFormatLoaderCompatBinary;

	Error parse_variant(Variant &r_v);

	HashMap<String, Ref<Resource>> dependency_cache;
	void set_compat_meta(Ref<Resource> &r_res);
	ResourceInfo get_resource_info();
	uint64_t get_metadata_size();
	Error load_import_metadata(bool p_return_to_pos = false);
	bool is_real_load() const { return load_type == ResourceInfo::REAL_LOAD || load_type == ResourceInfo::GLTF_LOAD; }

	Ref<ResourceLoader::LoadToken> start_ext_load(const String &p_path, const String &p_type_hint, const ResourceUID::ID uid, const int er_idx);
	Ref<Resource> finish_ext_load(Ref<ResourceLoader::LoadToken> &load_token, Error *r_err);

public:
	bool should_threaded_load() const;
	Ref<Resource> get_resource();
	Error load();
	void set_translation_remapped(bool p_remapped);

	void set_remaps(const HashMap<String, String> &p_remaps) { remaps = p_remaps; }
	void open(Ref<FileAccess> p_f, bool p_no_resources = false, bool p_keep_uuid_paths = false);
	String recognize(Ref<FileAccess> p_f);
	String recognize_script_class(Ref<FileAccess> p_f);
	void get_dependencies(Ref<FileAccess> p_f, List<String> *p_dependencies, bool p_add_types);
	void get_classes_used(Ref<FileAccess> p_f, HashSet<StringName> *p_classes);

	ResourceLoaderCompatBinary() {}
};

class ResourceFormatLoaderCompatBinary : public CompatFormatLoader {
	GDCLASS(ResourceFormatLoaderCompatBinary, CompatFormatLoader);

public:
	static Error get_ver_major_minor(const String &p_path, uint32_t &r_ver_major, uint32_t &r_ver_minor, bool &r_suspicious);
	virtual Ref<Resource> custom_load(const String &p_path, const String& p_original_path, ResourceInfo::LoadType p_type, Error *r_error = nullptr, bool use_threads = true, ResourceFormatLoader::CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual ResourceInfo get_resource_info(const String &p_path, Error *r_error) const override;
	virtual bool handles_fake_load() const override { return true; }

	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions_for_type(const String &p_type, List<String> *p_extensions) const override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;
	virtual String get_resource_script_class(const String &p_path) const override;
	virtual void get_classes_used(const String &p_path, HashSet<StringName> *r_classes) override;
	virtual ResourceUID::ID get_resource_uid(const String &p_path) const override;
	virtual bool has_custom_uid_support() const override;
	virtual void get_dependencies(const String &p_path, List<String> *p_dependencies, bool p_add_types = false) override;
	virtual Error rename_dependencies(const String &p_path, const HashMap<String, String> &p_map) override;

	static Error rewrite_v2_import_metadata(const String &p_path, const String &p_dst, Ref<ResourceImportMetadatav2> imd);
};

class ResourceFormatSaverCompatBinaryInstance {
	String local_path;
	String path;

	bool relative_paths = false;
	bool bundle_resources = false;
	bool skip_editor = false;
	bool big_endian = false;
	bool takeover_paths = false;
	String magic;
	HashSet<Ref<Resource>> resource_set;

	String output_dir;

	uint32_t ver_format;
	uint32_t ver_major;
	uint32_t ver_minor;
	bool using_uids = false;
	bool using_named_scene_ids = false;
	bool using_script_class = false;
	bool using_real_t_double = false;
	bool stored_use_real64 = false;
	bool stored_big_endian = false;
	String script_class;

	struct NonPersistentKey { //for resource properties generated on the fly
		Ref<Resource> base;
		StringName property;
		bool operator<(const NonPersistentKey &p_key) const { return base == p_key.base ? property < p_key.property : base < p_key.base; }
	};

	RBMap<NonPersistentKey, Variant> non_persistent_map;
	HashMap<StringName, int> string_map;
	Vector<StringName> strings;

	HashMap<Ref<Resource>, int> external_resources;
	List<Ref<Resource>> saved_resources;

	struct Property {
		int name_idx;
		Variant value;
		PropertyInfo pi;
	};

	struct ResourceData {
		String type;
		List<Property> properties;
	};

	static void _pad_buffer(Ref<FileAccess> f, int p_bytes);
	void _find_resources(const Variant &p_variant, bool p_main = false);
	static void save_unicode_string(Ref<FileAccess> f, const String &p_string, bool p_bit_on_len = false);
	int get_string_index(const String &p_string);
	Dictionary fix_scene_bundle(const Ref<PackedScene> &p_scene, int original_version);

public:
	enum {
		FORMAT_FLAG_NAMED_SCENE_IDS = 1,
		FORMAT_FLAG_UIDS = 2,
		FORMAT_FLAG_REAL_T_IS_DOUBLE = 4,
		FORMAT_FLAG_HAS_SCRIPT_CLASS = 8,

		// Amount of reserved 32-bit fields in resource header
		RESERVED_FIELDS = 11
	};
	Error save(const String &p_path, const Ref<Resource> &p_resource, uint32_t p_flags = 0);
	Error set_uid(const String &p_path, ResourceUID::ID p_uid);
	void write_variant(Ref<FileAccess> f, const Variant &p_property, HashMap<Ref<Resource>, int> &resource_map, HashMap<Ref<Resource>, int> &external_resources, HashMap<StringName, int> &string_map, const PropertyInfo &p_hint = PropertyInfo());
};

class ResourceFormatSaverCompatBinary : public ResourceFormatSaver {
public:
	static ResourceFormatSaverCompatBinary *singleton;
	virtual Error save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags = 0) override;
	virtual Error set_uid(const String &p_path, ResourceUID::ID p_uid) override;
	virtual bool recognize(const Ref<Resource> &p_resource) const override;
	virtual void get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const override;

	ResourceFormatSaverCompatBinary();
};

#endif // RESOURCE_COMPAT_BINARY_H
