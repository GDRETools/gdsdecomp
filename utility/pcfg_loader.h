//woop

#ifndef PCFG_LOADER_H
#define PCFG_LOADER_H

#include "core/io/file_access.h"
#include "core/object/object.h"
#include "core/object/ref_counted.h"

typedef RBMap<String, Variant> CustomMap;

class ProjectConfigLoader : public RefCounted {
	GDCLASS(ProjectConfigLoader, RefCounted);
	struct VariantContainer {
		int order;
		bool persist;
		Variant variant;
		Variant initial;
		bool hide_from_editor;
		bool overridden;
		bool restart_if_changed;
		VariantContainer() :
				order(0),
				persist(false),
				hide_from_editor(false),
				overridden(false),
				restart_if_changed(false) {
		}
		VariantContainer(const Variant &p_variant, int p_order, bool p_persist = false) :
				order(p_order),
				persist(p_persist),
				variant(p_variant),
				hide_from_editor(false),
				overridden(false),
				restart_if_changed(false) {
		}
	};
	RBMap<StringName, VariantContainer> props;
	RBMap<StringName, PropertyInfo> custom_prop_info;
	String cfb_path = "";
	int last_builtin_order;
	bool loaded = false;
	int config_version = 0;

public:
	static constexpr int CURRENT_CONFIG_VERSION = 5;

	Error load_cfb(const String path, uint32_t ver_major, uint32_t ver_minor);
	Error save_cfb(const String dir, uint32_t ver_major, uint32_t ver_minor);
	Error _load_settings_binary(Ref<FileAccess> f, const String &p_path, uint32_t ver_major);
	Error _load_settings_text(Ref<FileAccess> f, const String &p_path, uint32_t ver_major);

	Error save_custom(const String &p_path, const uint32_t ver_major, const uint32_t ver_minor);
	Error _save_settings_text(const String &p_file, const RBMap<String, List<String>> &props, const uint32_t ver_major, const uint32_t ver_minor);
	Error _save_settings_text(const String &p_file);
	bool is_loaded() const { return loaded; }
	bool has_setting(String p_var) const;
	Variant get_setting(String p_var, Variant default_value) const;
	Error remove_setting(String p_var);
	Error set_setting(String p_var, Variant value);
	String get_cfg_path() const { return cfb_path; }
	int get_config_version() const { return config_version; }
	Variant g_set(const String &p_var, const Variant &p_default, bool p_restart_if_changed = false);
	ProjectConfigLoader();
	~ProjectConfigLoader();
};
#endif
