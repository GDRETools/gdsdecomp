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
	int last_builtin_order = 0;
	bool loaded = false;
	int config_version = 0;
	uint32_t major = 0;
	uint32_t minor = 0;

protected:
	static void _bind_methods();

	RBMap<String, List<String>> get_save_proops() const;
	Error _save_settings_text_file(const Ref<FileAccess> &file, const RBMap<String, List<String>> &props, const uint32_t ver_major, const uint32_t ver_minor);

	bool _check_property_type(const String &property, Variant::Type type, Variant::Type element_type = Variant::Type::VARIANT_MAX) const;
	int _detect_ver_major_v3_or_v4(int loaded_as_ver_major) const;

	Error _try_load_binary_v3_or_v4(const String &path, uint32_t &ver_major);

	static constexpr uint32_t get_config_version_for_version(uint32_t ver_major, uint32_t ver_minor) {
		uint32_t text_config_version = 2;
		switch (ver_major) {
			case 4: {
				text_config_version = 5;
			} break;
			case 3: {
				if (ver_minor == 0) {
					text_config_version = 3;
				} else {
					text_config_version = 4;
				}
			} break;
			case 2: {
				text_config_version = 2;
			} break;
			case 1:
			default:
				text_config_version = 1;
				break;
		}
		return text_config_version;
	}

	static constexpr Pair<uint32_t, uint32_t> get_ver_major_and_minor_for_config_version(uint32_t config_version) {
		switch (config_version) {
			case 5:
				return Pair<uint32_t, uint32_t>(4, 0);
			case 4:
				return Pair<uint32_t, uint32_t>(3, 1);
			case 3:
				return Pair<uint32_t, uint32_t>(3, 0);
			case 2:
				return Pair<uint32_t, uint32_t>(2, 0);
			case 1:
				return Pair<uint32_t, uint32_t>(1, 0);
			default:
				return Pair<uint32_t, uint32_t>(0, 0);
		}
		return Pair<uint32_t, uint32_t>(0, 0);
	}

	Error _load_settings_binary(Ref<FileAccess> f, const String &p_path, uint32_t ver_major, bool fail_on_corrupt);
	Error _load_settings_text(Ref<FileAccess> f, const String &p_path, uint32_t ver_major);

	Error _save_settings_text(const String &p_file, const RBMap<String, List<String>> &props, const uint32_t ver_major, const uint32_t ver_minor);
	Error _save_settings_binary(const String &p_file, const RBMap<String, List<String>> &props, const uint32_t ver_major, const uint32_t ver_minor, const CustomMap &p_custom = CustomMap(), const String &p_custom_features = String());

public:
	static constexpr int CURRENT_CONFIG_VERSION = 5;

	static String get_project_settings_as_string(const String &p_path);

	Error load_cfb(const String path, uint32_t ver_major = 0, uint32_t ver_minor = 0);
	Error save_cfb(const String dir, uint32_t ver_major = 0, uint32_t ver_minor = 0);
	Error save_cfb_binary(const String dir, uint32_t ver_major = 0, uint32_t ver_minor = 0);
	Error save_custom(const String &p_path, const uint32_t ver_major = 0, const uint32_t ver_minor = 0);
	String get_as_text();
	bool is_loaded() const { return loaded; }
	bool has_setting(String p_var) const;
	Variant get_setting(String p_var, Variant default_value) const;
	Error remove_setting(String p_var);
	Error set_setting(String p_var, Variant value);
	String get_cfg_path() const { return cfb_path; }
	int get_config_version() const { return config_version; }
	Variant g_set(const String &p_var, const Variant &p_default, bool p_restart_if_changed = false);
	Dictionary get_section(const String &p_var) const;
	ProjectConfigLoader();
	~ProjectConfigLoader();
};
#endif
