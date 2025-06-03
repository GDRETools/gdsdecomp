#include "gdre_config.h"
#include "common.h"
#include "gdre_settings.h"

GDREConfig *GDREConfig::singleton = nullptr;

GDREConfig *GDREConfig::get_singleton() {
	if (!singleton) {
		singleton = memnew(GDREConfig);
	}
	return singleton;
}

Vector<Ref<GDREConfigSetting>> GDREConfig::_init_default_settings() {
	return {
		memnew(GDREConfigSetting(
				"download_plugins",
				"Download plugins",
				"Automatically detect binary plugin versions and download them from the asset library",
				false)),
		memnew(GDREConfigSetting(
				"force_single_threaded",
				"Force single-threaded mode",
				"Forces all tasks to run on the main thread",
				false)),
		memnew(GDREConfigSetting(
				"ask_for_download",
				"Ask for download",
				"",
				true,
				true)),
		memnew(GDREConfigSetting(
				"last_showed_disclaimer",
				"Last showed disclaimer",
				"",
				"<NONE>",
				true)),
		memnew(GDREConfigSetting(
				"Exporter/Scene/GLTF/force_lossless_images",
				"Force lossless images",
				"Forces images to be saved as lossless PNGs when exporting to GLTF, regardless of the original image format",
				false)),
		memnew(GDREConfigSetting(
				"Exporter/Scene/GLTF/force_single_precision",
				"Force single precision",
				"Forces all floating-point values to be saved as single precision to the GLTF document",
				false)),
		memnew(GDREConfigSetting(
				"Exporter/Scene/GLTF/force_export_multi_root",
				"Force export multi root",
				"Forces the export to export in multi-root mode, even if the scene is a single root",
				false)),
	};
}

GDREConfig::GDREConfig() {
	singleton = this;
	default_settings = _init_default_settings();
	load_config();
}

GDREConfig::~GDREConfig() {
	save_config();
	singleton = nullptr;
	default_settings.clear();
}

TypedArray<GDREConfigSetting> GDREConfig::get_all_settings() const {
	return gdre::vector_to_typed_array(default_settings);
}

void GDREConfig::load_config() {
	settings.clear();

	for (const auto &setting : default_settings) {
		set_setting(setting->get_full_name(), setting->get_default_value());
	}

	// set_setting("scene_export/force_export_multi_root", false);
	auto cfg_path = get_config_path();
	print_line("Loading config file from: " + cfg_path);
	if (FileAccess::exists(cfg_path)) {
		Ref<ConfigFile> config = memnew(ConfigFile);
		Error err = config->load(cfg_path);
		if (err != OK) {
			WARN_PRINT("Failed to load config file: " + cfg_path);
		}
		for (auto &section : config->get_sections()) {
			for (auto &key : config->get_section_keys(section)) {
				set_setting(section + "/" + key, config->get_value(section, key));
			}
		}
		HashMap<String, Variant> settings_copy;
		for (const auto &[key, value] : settings) {
			settings_copy[key] = value;
		}
		print_line("Settings");
	}
}

String GDREConfig::get_config_path() {
	return GDRESettings::get_singleton()->get_gdre_user_path().path_join("gdre_settings.cfg");
}

void GDREConfig::save_config() {
	auto cfg_path = get_config_path();
	print_line("Saving config file to: " + cfg_path);
	Ref<ConfigFile> config = memnew(ConfigFile);
	HashMap<String, Variant> settings_copy;
	for (const auto &[key, value] : settings) {
		settings_copy[key] = value;
		config->set_value(get_section_from_key(key), get_name_from_key(key), value);
	}
	Error err = config->save(cfg_path);
	if (err != OK) {
		WARN_PRINT("Failed to save config file: " + cfg_path);
	}
}

String get_full_name(const String &p_setting) {
	if (!p_setting.contains("/")) {
		return "General/" + p_setting;
	}
	return p_setting;
}

void GDREConfig::set_setting(const String &p_setting, const Variant &p_value) {
	settings.try_emplace_l(get_full_name(p_setting), [=](auto &v) { v.second = p_value; }, p_value);
}

bool GDREConfig::has_setting(const String &p_setting) const {
	return settings.contains(get_full_name(p_setting));
}

Variant GDREConfig::get_setting(const String &p_setting, const Variant &p_default_value) const {
	Variant ret = p_default_value;
	settings.if_contains(get_full_name(p_setting), [&](const auto &v) { ret = v.second; });
	return ret;
}

String GDREConfig::get_section_from_key(const String &p_setting) {
	auto parts = p_setting.split("/", true, 1);
	if (parts.size() == 1) {
		return "General";
	}
	return parts[0];
}

String GDREConfig::get_name_from_key(const String &p_setting) {
	auto parts = p_setting.split("/", true, 1);
	if (parts.size() == 1) {
		return p_setting;
	}
	return parts[1];
}

GDREConfigSetting::GDREConfigSetting(const String &p_full_name, const String &p_brief, const String &p_description, const Variant &p_default_value, bool p_hidden) {
	full_name = p_full_name;
	brief_description = p_brief;
	description = p_description;
	default_value = p_default_value;
	hidden = p_hidden;
}

String GDREConfigSetting::get_full_name() const {
	return full_name;
}

String GDREConfigSetting::get_name() const {
	return full_name.get_slice("/", 1);
}

String GDREConfigSetting::get_brief_description() const {
	if (brief_description.is_empty()) {
		return get_name().replace("_", " ").capitalize();
	}
	return brief_description;
}

Variant::Type GDREConfigSetting::get_type() const {
	return default_value.get_type();
}

String GDREConfigSetting::get_description() const {
	return description;
}

Variant GDREConfigSetting::get_default_value() const {
	return default_value;
}

Variant GDREConfigSetting::get_value() const {
	return GDREConfig::get_singleton()->get_setting(full_name, default_value);
}

void GDREConfigSetting::reset() {
	GDREConfig::get_singleton()->set_setting(full_name, default_value);
}

void GDREConfigSetting::set_value(const Variant &p_value) {
	GDREConfig::get_singleton()->set_setting(full_name, p_value);
}

bool GDREConfigSetting::is_hidden() const {
	return hidden;
}

void GDREConfigSetting::_bind_methods() {
	ClassDB::bind_method(D_METHOD("reset"), &GDREConfigSetting::reset);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &GDREConfigSetting::set_value);
	ClassDB::bind_method(D_METHOD("get_description"), &GDREConfigSetting::get_description);
	ClassDB::bind_method(D_METHOD("get_value"), &GDREConfigSetting::get_value);
	ClassDB::bind_method(D_METHOD("get_default_value"), &GDREConfigSetting::get_default_value);
	ClassDB::bind_method(D_METHOD("get_name"), &GDREConfigSetting::get_name);
	ClassDB::bind_method(D_METHOD("get_full_name"), &GDREConfigSetting::get_full_name);
	ClassDB::bind_method(D_METHOD("get_brief_description"), &GDREConfigSetting::get_brief_description);
	ClassDB::bind_method(D_METHOD("get_type"), &GDREConfigSetting::get_type);
	ClassDB::bind_method(D_METHOD("is_hidden"), &GDREConfigSetting::is_hidden);
}

void GDREConfig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_all_settings"), &GDREConfig::get_all_settings);
	ClassDB::bind_method(D_METHOD("load_config"), &GDREConfig::load_config);
	ClassDB::bind_method(D_METHOD("save_config"), &GDREConfig::save_config);
	ClassDB::bind_method(D_METHOD("set_setting", "setting", "value"), &GDREConfig::set_setting);
	ClassDB::bind_method(D_METHOD("has_setting", "setting"), &GDREConfig::has_setting);
	ClassDB::bind_method(D_METHOD("get_setting", "setting", "default_value"), &GDREConfig::get_setting, DEFVAL(Variant()));
}
