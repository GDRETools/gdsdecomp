/*************************************************************************/
/*  resource_format_xml.h                                                */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef RESOURCE_FORMAT_XML_H
#define RESOURCE_FORMAT_XML_H

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/file_access.h"
#include "compat/resource_loader_compat.h"

// There's no need to load XML scenes during project recovery for v2 games, and I really don't want to try and test it.
#define GDRE_DISABLE_XML_LOADER 1

#if !GDRE_DISABLE_XML_LOADER

struct ExportData {

	struct Dependency {
		String path;
		String type;
	};

	HashMap<int, Dependency> dependencies;

	struct PropertyData {
		String name;
		Variant value;
	};

	struct ResourceData {

		String type;
		int index;
		List<PropertyData> properties;
	};

	Vector<ResourceData> resources;

	struct NodeData {

		bool text_data;
		bool instanced;
		String name;
		String type;
		String instance;
		//int info
		int owner_int; //depending type
		int parent_int;
		bool instance_is_placeholder;

		//text info
		NodePath parent;
		NodePath owner;
		String instance_placeholder;

		Vector<String> groups;
		List<PropertyData> properties;

		NodeData() {
			parent_int = 0;
			owner_int = 0;
			text_data = true;
			instanced = false;
		}
	};

	Vector<NodeData> nodes;

	struct Connection {

		bool text_data;

		int from_int;
		int to_int;

		NodePath from;
		NodePath to;
		String signal;
		String method;
		Array binds;
		int flags;

		Connection() { text_data = true; }
	};

	Vector<Connection> connections;
	Vector<NodePath> editables;

	Array node_paths; //for integer packed data
	Variant base_scene;
};

class ResourceInteractiveLoaderXML: public RefCounted {

	String local_path;
	String res_path;

	Ref<FileAccess> f;

	int version_major = 0;
	int version_minor = 0;
	ResourceInfo::LoadType load_type = ResourceInfo::LoadType::REAL_LOAD;
	ResourceFormatLoader::CacheMode cache_mode = ResourceFormatLoader::CACHE_MODE_REUSE;

	struct Tag {

		String name;
		HashMap<String, String> args;
	};

	_FORCE_INLINE_ Error _parse_array_element(Vector<char> &buff, bool p_number_only, Ref<FileAccess> f, bool *end);

	struct ExtResource {
		String path;
		String type;
		Ref<Resource> cached_resource;
	};

	HashMap<String, String> remaps;

	HashMap<int, ExtResource> ext_resources;

	int resources_total;
	int resource_current;
	String resource_type;

	mutable int lines;
	uint8_t get_char() const;
	int get_current_line() const;

	friend class ResourceFormatLoaderXML;
	List<Tag> tag_stack;

	List<Ref<Resource>> resource_cache;
	Tag *parse_tag(bool *r_exit = NULL, bool p_printerr = true, List<String> *r_order = NULL);
	Error close_tag(const String &p_name);
	_FORCE_INLINE_ void unquote(String &p_str);
	Error goto_end_of_tag();
	Error parse_property_data(String &r_data);
	Error parse_property(Variant &r_v, String &r_name, bool p_for_export_data = false);

	Ref<Resource> load_external_resource(const String &p_path, const String &p_type_hint, int p_index);

	bool is_real_load() const;
	void set_path_on_resource(Ref<Resource> &p_resource, const String &p_path);

	Error error = OK;

	Ref<Resource> resource;

public:
	virtual void set_local_path(const String &p_local_path);
	virtual Ref<Resource> get_resource();
	virtual Error poll();
	virtual int get_stage() const;
	virtual int get_stage_count() const;

	void open(Ref<FileAccess>p_f);
	String recognize(Ref<FileAccess>p_f);
	void get_dependencies(Ref<FileAccess>p_f, List<String> *p_dependencies, bool p_add_types);
	Error rename_dependencies(Ref<FileAccess>p_f, const String &p_path, const HashMap<String, String> &p_map);

	Error get_export_data(Ref<FileAccess>p_f, ExportData &r_export_data);

	~ResourceInteractiveLoaderXML();
};

class ResourceFormatLoaderXML : public CompatFormatLoader {
public:
	static ResourceFormatLoaderXML *singleton;

	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions_for_type(const String &p_type, List<String> *p_extensions) const override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;
	virtual void get_dependencies(const String &p_path, List<String> *p_dependencies, bool p_add_types = false) override;
	virtual Error rename_dependencies(const String &p_path, const HashMap<String, String> &p_map) override;
	virtual Error get_export_data(const String &p_path, ExportData &r_export_data);

	virtual Ref<Resource> custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error = nullptr, bool use_threads = true, ResourceFormatLoader::CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual Ref<ResourceInfo> get_resource_info(const String &p_path, Error *r_error) const override;
	virtual bool handles_fake_load() const override;


	ResourceFormatLoaderXML() { singleton = this; }
};
#endif // if 0
////////////////////////////////////////////////////////////////////////////////////////////

class ResourceFormatSaverXMLInstance {

	String local_path;

	int ver_major = 0;
	int ver_minor = 0;

	bool takeover_paths = false;
	bool relative_paths = false;
	bool bundle_resources = false;
	bool skip_editor = false;
	Ref<FileAccess> f;
	int depth = 0;
	HashSet<Ref<Resource>> resource_set;
	List<Ref<Resource>> saved_resources;
	HashMap<Ref<Resource>, int> external_resources;

	Error set_save_settings(const Ref<Resource> &p_resource, uint32_t p_flags);

	void enter_tag(const char *p_tag, const String &p_args = String());
	void exit_tag(const char *p_tag);

	void _find_resources(const Variant &p_variant, bool p_main = false);
	void write_property(const String &p_name, const Variant &p_property, bool *r_ok = NULL);

	void escape(String &p_str);
	void write_tabs(int p_diff = 0);
	void write_string(String p_str, bool p_escape = true);

public:
	Error save(const String &p_path, const Ref<Resource> &p_resource, uint32_t p_flags = 0);
	Error save_to_file(const Ref<FileAccess> &p_f, const String &p_path, const Ref<Resource> &p_resource, uint32_t p_flags = 0);
};

class ResourceFormatSaverXML : public ResourceFormatSaver {
public:
	static ResourceFormatSaverXML *singleton;
	virtual Error save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags = 0) override;
	virtual bool recognize(const Ref<Resource> &p_resource) const override;
	virtual void get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const override;

	ResourceFormatSaverXML();
};

#endif // RESOURCE_FORMAT_XML_H
