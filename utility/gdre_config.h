#pragma once

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/typed_dictionary.h"
#include "gd_parallel_hashmap.h"

class GDREConfig;

// this is mainly used by the GUI
class GDREConfigSetting : public RefCounted {
	GDCLASS(GDREConfigSetting, RefCounted);
	friend class GDREConfig;

protected:
	String full_name;
	String brief_description;
	String description;
	Variant default_value;
	bool hidden = false;
	bool ephemeral = false;

public:
	String get_full_name() const;
	String get_name() const;
	String get_section() const;
	String get_brief_description() const;
	String get_description() const;
	virtual Variant get_value() const;
	Variant get_default_value() const;
	bool is_hidden() const;
	Variant::Type get_type() const;
	bool is_ephemeral() const;
	virtual bool is_virtual_setting() const { return false; }
	virtual bool is_filepicker() const { return false; }
	virtual String get_error_message() const { return ""; }
	virtual void clear_error_message() {}

	virtual bool has_special_value() const { return false; }
	// get a list of possible values along with their descriptions
	virtual Dictionary get_list_of_possible_values() const { return Dictionary(); }

	// this calls GDREConfig::set_setting
	void reset();
	// this calls GDREConfig::set_setting
	virtual void set_value(const Variant &p_value, bool p_force_ephemeral = false);
	GDREConfigSetting(const String &p_full_name, const String &p_brief, const String &p_description, const Variant &p_default_value, bool p_hidden = false, bool p_ephemeral = false);

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
	ParallelFlatHashMap<String, Variant> ephemeral_settings;

	static Vector<Ref<GDREConfigSetting>> _init_default_settings();

	static String get_config_path();

public:
	static GDREConfig *get_singleton();

	GDREConfig();
	~GDREConfig();

	void load_config();
	void save_config();
	void set_setting(const String &p_setting, const Variant &p_value, bool p_force_ephemeral = false);
	bool has_setting(const String &string) const;
	static String get_section_from_key(const String &p_setting);
	static String get_name_from_key(const String &p_setting);
	Variant get_setting(const String &p_setting, const Variant &p_default_value = Variant()) const;
	Variant get_default_value(const String &p_setting) const;
	TypedArray<GDREConfigSetting> get_all_settings() const;
	bool is_ephemeral_setting(const String &p_setting) const;
	void reset_ephemeral_settings();

protected:
	static void _bind_methods();
};

#define CONFIG_GET(p_setting, p_default_value) GDREConfig::get_singleton()->get_setting(p_setting, p_default_value)
