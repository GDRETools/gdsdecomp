#pragma once

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "gd_parallel_hashmap.h"

class GDREConfig;

// this is mainly used by the GUI
class GDREConfigSetting : public RefCounted {
	GDCLASS(GDREConfigSetting, RefCounted);
	friend class GDREConfig;
	String full_name;
	String brief_description;
	String description;
	Variant default_value;
	bool hidden = false;

public:
	String get_full_name() const;
	String get_name() const;
	String get_section() const;
	String get_brief_description() const;
	String get_description() const;
	Variant get_value() const;
	Variant get_default_value() const;
	bool is_hidden() const;
	Variant::Type get_type() const;

	// this calls GDREConfig::set_setting
	void reset();
	// this calls GDREConfig::set_setting
	void set_value(const Variant &p_value);
	GDREConfigSetting(const String &p_full_name, const String &p_brief, const String &p_description, const Variant &p_default_value, bool p_hidden = false);

protected:
	static void _bind_methods();
	// Don't call this, this is just to make the class_db happy
	GDREConfigSetting() {}
};

class GDREConfig : public Object {
	GDCLASS(GDREConfig, Object);

	ParallelFlatHashMap<String, Variant> settings;
	static GDREConfig *singleton;
	Vector<Ref<GDREConfigSetting>> default_settings;

	static Vector<Ref<GDREConfigSetting>> _init_default_settings();

	static String get_config_path();

public:
	static GDREConfig *get_singleton();

	GDREConfig();
	~GDREConfig();

	void load_config();
	void save_config();
	void set_setting(const String &p_setting, const Variant &p_value);
	bool has_setting(const String &string) const;
	static String get_section_from_key(const String &p_setting);
	static String get_name_from_key(const String &p_setting);
	Variant get_setting(const String &p_setting, const Variant &p_default_value = Variant()) const;
	TypedArray<GDREConfigSetting> get_all_settings() const;

protected:
	static void _bind_methods();
};
