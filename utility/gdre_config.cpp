#include "gdre_config.h"
#include "bytecode/bytecode_base.h"
#include "bytecode/bytecode_versions.h"
#include "common.h"
#include "core/io/json.h"
#include "gdre_settings.h"

GDREConfig *GDREConfig::singleton = nullptr;

GDREConfig *GDREConfig::get_singleton() {
	if (!singleton) {
		singleton = memnew(GDREConfig);
	}
	return singleton;
}

class GDREConfigSetting_LoadCustomBytecode : public GDREConfigSetting {
	GDSOFTCLASS(GDREConfigSetting_LoadCustomBytecode, GDREConfigSetting);

	String error_message;

public:
	GDREConfigSetting_LoadCustomBytecode() :
			GDREConfigSetting(
					"Bytecode/load_custom_bytecode",
					"Load Custom Bytecode",
					"Load a custom bytecode file.",
					"",
					false,
					true) {
	}

	virtual bool is_filepicker() const override { return true; }
	virtual bool is_virtual_setting() const override { return true; }
	virtual String get_error_message() const override { return error_message; }
	virtual void clear_error_message() override { error_message = ""; }
	virtual Variant get_value() const override {
		return "";
	}
	virtual void set_value(const Variant &p_value, bool p_force_ephemeral = false) override {
		String path = p_value;
		if (path.is_empty()) {
			return;
		}
		if (!FileAccess::exists(path)) {
			WARN_PRINT("Custom bytecode file does not exist: " + path);
			error_message = "Custom bytecode file does not exist";
			return;
		}
		String file_contents = FileAccess::get_file_as_string(path);
		if (file_contents.is_empty()) {
			WARN_PRINT("Custom bytecode file is empty: " + path);
			error_message = "Custom bytecode file is empty";
			return;
		}
		Dictionary json = JSON::parse_string(file_contents);
		if (json.is_empty()) {
			WARN_PRINT("Custom bytecode file is not valid JSON: " + path);
			error_message = "Custom bytecode file is not valid JSON";
			return;
		}

		// clears errors
		GDRESettings::get_singleton()->get_errors();
		int commit = GDScriptDecomp::register_decomp_version_custom(json);
		if (commit == 0) {
			WARN_PRINT("Failed to register custom bytecode file: " + path);
			error_message = "Failed to register custom bytecode file: \n" + String("\n").join(GDRESettings::get_singleton()->get_errors());
			return;
		}
		GDREConfig::get_singleton()->set_setting("Bytecode/force_bytecode_revision", commit, true);
		error_message = "";
	}
};
class GDREConfigSetting_BytecodeForceBytecodeRevision : public GDREConfigSetting {
	GDSOFTCLASS(GDREConfigSetting_BytecodeForceBytecodeRevision, GDREConfigSetting);

public:
	GDREConfigSetting_BytecodeForceBytecodeRevision() :
			GDREConfigSetting(
					"Bytecode/force_bytecode_revision",
					"Force Bytecode Revision",
					"Forces the bytecode revision to be the specified value.",
					0,
					false,
					true) {}

	virtual bool has_special_value() const override { return true; }
	virtual Dictionary get_list_of_possible_values() const override {
		Dictionary ret;
		int ver_major = 0;
		if (GDRESettings::get_singleton() && GDRESettings::get_singleton()->is_pack_loaded()) {
			ver_major = GDRESettings::get_singleton()->get_ver_major();
		}
		int current_setting = get_value();
		auto versions = get_decomp_versions(true, 0);
		ret[0] = "Auto-detect";
		for (const auto &version : versions) {
			if ((ver_major > 0 && version.get_major_version() != ver_major)) {
				if (version.commit != current_setting) {
					continue;
				}
			}
			String short_name = version.name.split("/")[0].strip_edges() + ")";
			ret[version.commit] = short_name;
		}
		return ret;
	}
};

class GDREConfigSetting_TranslationExporter_LoadKeyHintFile : public GDREConfigSetting {
	GDSOFTCLASS(GDREConfigSetting_TranslationExporter_LoadKeyHintFile, GDREConfigSetting);

public:
	GDREConfigSetting_TranslationExporter_LoadKeyHintFile() :
			GDREConfigSetting(
					"Exporter/Translation/load_key_hint_file",
					"Load key hint file",
					"Load a key hint file to use for the translation exporter",
					"",
					false,
					false) {}
	String error_message;

	virtual bool is_filepicker() const override { return true; }
	virtual bool is_virtual_setting() const override { return true; }
	virtual String get_error_message() const override { return error_message; }
	virtual void clear_error_message() override { error_message = ""; }
	virtual Variant get_value() const override {
		return "";
	}
	virtual void set_value(const Variant &p_value, bool p_force_ephemeral = false) override {
		String path = p_value;
		if (path.is_empty()) {
			return;
		}
		Error err = GDRESettings::get_singleton()->load_translation_key_hint_file(path);
		if (err != OK) {
			WARN_PRINT("Failed to load key hint file: " + path);
			error_message = "Failed to load key hint file: " + path;
			return;
		}
		error_message = "";
	}
};

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
		memnew(GDREConfigSetting_BytecodeForceBytecodeRevision()),
		memnew(GDREConfigSetting_LoadCustomBytecode()),
		memnew(GDREConfigSetting(
				"CSharp/write_nuget_package_references",
				"Write NuGet package references",
				"Detect and write NuGet package references to the project file instead of assembly references.",
				true)),
		memnew(GDREConfigSetting(
				"CSharp/copy_out_of_tree_references",
				"Copy out of tree references",
				"Copy referenced assemblies to the project directory.",
				true)),
		memnew(GDREConfigSetting(
				"CSharp/verify_nuget_package_is_from_nuget_org",
				"Verify NuGet package is from NuGet.org",
				"Verify that the NuGet package is from NuGet.org before writing it to the project file.\nWARNING: This involves downloading the package from nuget.org and checking the hash of the downloaded package.",
				false)),
		memnew(GDREConfigSetting(
				"CSharp/create_additional_projects_for_project_references",
				"Create additional projects for project references",
				"If a project reference is detected, create an additional project and add it to the solution.",
				true)),
		memnew(GDREConfigSetting(
				"Exporter/Scene/GLTF/force_lossless_images",
				"Force lossless images",
				"Forces images to be saved as lossless PNGs when exporting to GLTF, regardless of the original image format",
				false)),
		// TODO: This isn't viable yet, as the gltf document converter doesn't write double precision values
		memnew(GDREConfigSetting(
				"Exporter/Scene/GLTF/use_double_precision",
				"Use double precision",
				"Uses double precision for all floating-point values in the GLTF document",
				false,
				true)),
		memnew(GDREConfigSetting(
				"Exporter/Scene/GLTF/force_export_multi_root",
				"Force export multi root",
				"Forces the export to export in multi-root mode, even if the scene is a single root",
				false)),
		memnew(GDREConfigSetting(
				"Exporter/Scene/GLTF/force_require_KHR_node_visibility",
				"Force require KHR_node_visibility extension",
				"By default, the exporter will only require the KHR_node_visibility extension if the scenes are from Godot 4.5 or later.\nThis setting forces the exporter to require the extension regardless of engine version.",
				false)),
		memnew(GDREConfigSetting(
				"Exporter/Scene/GLTF/replace_shader_materials",
				"Replace shader materials",
				"Replaces shader materials with their referenced materials when exporting the scene (this may result in inaccurate exports)",
				false)),
		memnew(GDREConfigSetting_TranslationExporter_LoadKeyHintFile()),
		memnew(GDREConfigSetting(
				"Exporter/Translation/skip_loading_resource_strings",
				"Skip loading resource strings",
				"Skip loading resource strings from all resources during translation recovery.\nDon't enable this without a key hint file.",
				false,
				false,
				true)),
		memnew(GDREConfigSetting(
				"Exporter/Translation/dump_resource_strings",
				"Dump resource strings",
				"Dump resource strings to a file",
				false,
				true,
				true)),
	};
}

GDREConfig::GDREConfig() {
	singleton = this;
	default_settings = _init_default_settings();
	load_config();
}

GDREConfig::~GDREConfig() {
	save_config();
	if (singleton == this) {
		singleton = nullptr;
	}
	default_settings.clear();
}

TypedArray<GDREConfigSetting> GDREConfig::get_all_settings() const {
	return gdre::vector_to_typed_array(default_settings);
}

void GDREConfig::load_config() {
	settings.clear();

	for (const auto &setting : default_settings) {
		if (setting->is_ephemeral()) {
			ephemeral_settings.try_emplace_l(setting->get_full_name(), [=](auto &v) { v.second = setting->get_default_value(); }, setting->get_default_value());
		} else {
			set_setting(setting->get_full_name(), setting->get_default_value());
		}
	}

	auto cfg_path = get_config_path();
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
	}
}

String GDREConfig::get_config_path() {
	return GDRESettings::get_singleton()->get_gdre_user_path().path_join("gdre_settings.cfg");
}

void GDREConfig::save_config() {
	auto cfg_path = get_config_path();
	Ref<ConfigFile> config = memnew(ConfigFile);
	for (const auto &[key, value] : settings) {
		config->set_value(get_section_from_key(key), get_name_from_key(key), value);
	}
	gdre::ensure_dir(cfg_path.get_base_dir());
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

void GDREConfig::set_setting(const String &p_setting, const Variant &p_value, bool p_force_ephemeral) {
	if (p_force_ephemeral || ephemeral_settings.contains(get_full_name(p_setting))) {
		ephemeral_settings.try_emplace_l(get_full_name(p_setting), [=](auto &v) { v.second = p_value; }, p_value);
		return;
	}
	settings.try_emplace_l(get_full_name(p_setting), [=](auto &v) { v.second = p_value; }, p_value);
}

bool GDREConfig::has_setting(const String &p_setting) const {
	return settings.contains(get_full_name(p_setting)) || ephemeral_settings.contains(get_full_name(p_setting));
}

Variant GDREConfig::get_setting(const String &p_setting, const Variant &p_default_value) const {
	Variant ret = p_default_value;
	if (ephemeral_settings.if_contains(get_full_name(p_setting), [&](const auto &v) { ret = v.second; })) {
		return ret;
	}
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

Variant GDREConfig::get_default_value(const String &p_setting) const {
	String full_name = get_full_name(p_setting);
	for (const auto &setting : default_settings) {
		if (setting->get_full_name() == full_name) {
			return setting->get_default_value();
		}
	}
	return Variant();
}

void GDREConfig::reset_ephemeral_settings() {
	ephemeral_settings.clear();
	for (const auto &setting : default_settings) {
		if (setting->is_ephemeral()) {
			Variant default_value = setting->get_default_value();
			settings.if_contains(setting->get_full_name(), [&](const auto &v) { default_value = v.second; });
			ephemeral_settings.try_emplace_l(setting->get_full_name(), [=](auto &v) { v.second = default_value; }, default_value);
		}
	}
}

GDREConfigSetting::GDREConfigSetting(const String &p_full_name, const String &p_brief, const String &p_description, const Variant &p_default_value, bool p_hidden, bool p_ephemeral) {
	full_name = p_full_name;
	brief_description = p_brief;
	description = p_description;
	default_value = p_default_value;
	hidden = p_hidden;
	ephemeral = p_ephemeral;
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
	GDREConfig::get_singleton()->set_setting(full_name, default_value, ephemeral);
}

void GDREConfigSetting::set_value(const Variant &p_value, bool p_force_ephemeral) {
	GDREConfig::get_singleton()->set_setting(full_name, p_value, p_force_ephemeral || ephemeral);
}

bool GDREConfigSetting::is_hidden() const {
	return hidden;
}

bool GDREConfigSetting::is_ephemeral() const {
	return ephemeral;
}

void GDREConfigSetting::_bind_methods() {
	ClassDB::bind_method(D_METHOD("reset"), &GDREConfigSetting::reset);
	ClassDB::bind_method(D_METHOD("set_value", "value", "force_ephemeral"), &GDREConfigSetting::set_value, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_description"), &GDREConfigSetting::get_description);
	ClassDB::bind_method(D_METHOD("get_value"), &GDREConfigSetting::get_value);
	ClassDB::bind_method(D_METHOD("get_default_value"), &GDREConfigSetting::get_default_value);
	ClassDB::bind_method(D_METHOD("get_name"), &GDREConfigSetting::get_name);
	ClassDB::bind_method(D_METHOD("get_full_name"), &GDREConfigSetting::get_full_name);
	ClassDB::bind_method(D_METHOD("get_brief_description"), &GDREConfigSetting::get_brief_description);
	ClassDB::bind_method(D_METHOD("get_type"), &GDREConfigSetting::get_type);
	ClassDB::bind_method(D_METHOD("is_hidden"), &GDREConfigSetting::is_hidden);
	ClassDB::bind_method(D_METHOD("is_ephemeral"), &GDREConfigSetting::is_ephemeral);
	ClassDB::bind_method(D_METHOD("is_filepicker"), &GDREConfigSetting::is_filepicker);
	ClassDB::bind_method(D_METHOD("is_virtual_setting"), &GDREConfigSetting::is_virtual_setting);
	ClassDB::bind_method(D_METHOD("get_error_message"), &GDREConfigSetting::get_error_message);
	ClassDB::bind_method(D_METHOD("clear_error_message"), &GDREConfigSetting::clear_error_message);
	ClassDB::bind_method(D_METHOD("has_special_value"), &GDREConfigSetting::has_special_value);
	ClassDB::bind_method(D_METHOD("get_list_of_possible_values"), &GDREConfigSetting::get_list_of_possible_values);
}

void GDREConfig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_all_settings"), &GDREConfig::get_all_settings);
	ClassDB::bind_method(D_METHOD("load_config"), &GDREConfig::load_config);
	ClassDB::bind_method(D_METHOD("save_config"), &GDREConfig::save_config);
	ClassDB::bind_method(D_METHOD("set_setting", "setting", "value", "force_ephemeral"), &GDREConfig::set_setting, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("has_setting", "setting"), &GDREConfig::has_setting);
	ClassDB::bind_method(D_METHOD("get_setting", "setting", "default_value"), &GDREConfig::get_setting, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("get_default_value", "setting"), &GDREConfig::get_default_value);
	ClassDB::bind_method(D_METHOD("reset_ephemeral_settings"), &GDREConfig::reset_ephemeral_settings);
}
