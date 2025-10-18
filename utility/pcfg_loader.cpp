#include "pcfg_loader.h"
#include "compat/variant_decoder_compat.h"
#include "compat/variant_writer_compat.h"

#include "core/io/file_access.h"
#include "core/variant/variant_parser.h"
#include "utility/file_access_buffer.h"
#include "utility/gdre_settings.h"
#include <core/config/project_settings.h>
#include <core/templates/rb_set.h>

static const HashMap<String, String> project_godot_renames_v3_to_v4{
	// Should be kept in sync with project_settings_renames.
	{ "channel_disable_threshold_db", "buses/channel_disable_threshold_db" },
	{ "channel_disable_time", "buses/channel_disable_time" },
	{ "default_bus_layout", "buses/default_bus_layout" },
	// { "driver", "driver/driver" }, -- Risk of conflicts.
	{ "enable_audio_input", "driver/enable_input" },
	// { "mix_rate", "driver/mix_rate" }, -- Risk of conflicts.
	{ "output_latency", "driver/output_latency" },
	{ "output_latency.web", "driver/output_latency.web" },
	{ "video_delay_compensation_ms", "video/video_delay_compensation_ms" },
	{ "window/size/width", "window/size/viewport_width" },
	{ "window/size/height", "window/size/viewport_height" },
	{ "window/size/test_width", "window/size/window_width_override" },
	{ "window/size/test_height", "window/size/window_height_override" },
	{ "window/vsync/use_vsync", "window/vsync/vsync_mode" },
	{ "main_run_args", "run/main_run_args" },
	{ "common/swap_ok_cancel", "common/swap_cancel_ok" },
	{ "limits/debugger_stdout/max_chars_per_second", "limits/debugger/max_chars_per_second" },
	{ "limits/debugger_stdout/max_errors_per_second", "limits/debugger/max_errors_per_second" },
	{ "limits/debugger_stdout/max_messages_per_frame", "limits/debugger/max_queued_messages" },
	{ "limits/debugger_stdout/max_warnings_per_second", "limits/debugger/max_warnings_per_second" },
	{ "ssl/certificates", "tls/certificate_bundle_override" },
	{ "2d/thread_model", "2d/run_on_thread" }, // TODO: Not sure.
	{ "environment/default_clear_color", "environment/defaults/default_clear_color" },
	{ "environment/default_environment", "environment/defaults/default_environment" },
	{ "quality/depth_prepass/disable_for_vendors", "driver/depth_prepass/disable_for_vendors" },
	{ "quality/depth_prepass/enable", "driver/depth_prepass/enable" },
	{ "quality/shading/force_blinn_over_ggx", "shading/overrides/force_blinn_over_ggx" },
	{ "quality/shading/force_blinn_over_ggx.mobile", "shading/overrides/force_blinn_over_ggx.mobile" },
	{ "quality/shading/force_lambert_over_burley", "shading/overrides/force_lambert_over_burley" },
	{ "quality/shading/force_lambert_over_burley.mobile", "shading/overrides/force_lambert_over_burley.mobile" },
	{ "quality/shading/force_vertex_shading", "shading/overrides/force_vertex_shading" },
	{ "quality/shadow_atlas/quadrant_0_subdiv", "lights_and_shadows/shadow_atlas/quadrant_0_subdiv" },
	{ "quality/shadow_atlas/quadrant_1_subdiv", "lights_and_shadows/shadow_atlas/quadrant_1_subdiv" },
	{ "quality/shadow_atlas/quadrant_2_subdiv", "lights_and_shadows/shadow_atlas/quadrant_2_subdiv" },
	{ "quality/shadow_atlas/quadrant_3_subdiv", "lights_and_shadows/shadow_atlas/quadrant_3_subdiv" },
	{ "quality/shadow_atlas/size", "lights_and_shadows/shadow_atlas/size" },
	{ "quality/shadow_atlas/size.mobile", "lights_and_shadows/shadow_atlas/size.mobile" },
	{ "vram_compression/import_etc2", "textures/vram_compression/import_etc2_astc" },
	{ "vram_compression/import_s3tc", "textures/vram_compression/import_s3tc_bptc" },
};

bool ProjectConfigLoader::_check_property_type(const String &property, Variant::Type type, Variant::Type element_type) const {
	const VariantContainer &prop = props[property];
	if (prop.variant.get_type() != type) {
		return false;
	}
	if (element_type != Variant::Type::VARIANT_MAX && type == Variant::Type::ARRAY) {
		for (const Variant &elem : prop.variant.operator Array()) {
			if (elem.get_type() != element_type) {
				return false;
			}
		}
	}
	return true;
}

int ProjectConfigLoader::_detect_ver_major_v3_or_v4(int loaded_as_ver_major) const {
	// What we're testing here is for the Variant::Type enum shift which occurs after Vector2
	auto _check_prop = [&](String property, Variant::Type type, Variant::Type element_type = Variant::Type::VARIANT_MAX) {
		if (_check_property_type(property, type, element_type)) {
			return loaded_as_ver_major;
		}
		return loaded_as_ver_major == 4 ? 3 : 4;
	};
	auto _check_has_prop = [&](String property) {
		if (!props.has(property)) {
			return false;
		}
		const auto &prop = props[property];
		if (prop.variant.get_type() == Variant::Type::NIL || prop.variant.is_null()) {
			return false;
		}
		return true;
	};
#define CHECK_PROP_WITH_ELEMENT_TYPE(property, type, element_type) \
	if (_check_has_prop(property)) {                               \
		return _check_prop(property, type, element_type);          \
	}
#define CHECK_PROP(property, type) CHECK_PROP_WITH_ELEMENT_TYPE(property, type, Variant::Type::VARIANT_MAX);

	// these are all internal properties, so they're unlikely to be modifed with by the user directly
	CHECK_PROP("application/config/tags", Variant::Type::PACKED_STRING_ARRAY);
	CHECK_PROP("application/config/features", Variant::Type::PACKED_STRING_ARRAY);
	CHECK_PROP("internationalization/locale/translation_remaps", Variant::Type::PACKED_STRING_ARRAY);
	CHECK_PROP("internationalization/locale/translations", Variant::Type::PACKED_STRING_ARRAY);
	CHECK_PROP("internationalization/locale/translations_pot_files", Variant::Type::PACKED_STRING_ARRAY);
	CHECK_PROP_WITH_ELEMENT_TYPE("_global_script_classes", Variant::Type::ARRAY, Variant::Type::DICTIONARY);
	// checking for existence of renamed props
	// we check this first because it's written by default in v3 and v4 and it's the most common case
	bool has_v3_environment = props.has("environment/default_environment");
	bool has_v4_environment = props.has("environment/defaults/default_environment");

	if (has_v3_environment && !has_v4_environment) {
		return 3;
	}

	if (has_v4_environment && !has_v3_environment) {
		return 4;
	}

	for (const auto &E : project_godot_renames_v3_to_v4) {
		bool has_v3 = props.has(E.key);
		bool has_v4 = props.has(E.value);
		if (has_v3 && !has_v4) {
			return 3;
		}
		if (has_v4 && !has_v3) {
			return 4;
		}
	}

	// last resort; this is written by default in v4, and if the project doesn't have it, it's definitely v3
	if (props.has("application/config/features")) {
		return 4;
	}

	return 3;
#undef CHECK_PROP_WITH_ELEMENT_TYPE
#undef CHECK_PROP
}

Error ProjectConfigLoader::_try_load_binary_v3_or_v4(const String &path, uint32_t &r_ver_major) {
	Error err = OK;
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ, &err);
	ERR_FAIL_COND_V_MSG(f.is_null(), err, "Could not open " + path);
	r_ver_major = 4;
	err = _load_settings_binary(f, path, r_ver_major, true);
	f->seek(0);
	if (err == OK) {
		int detected_ver_major = _detect_ver_major_v3_or_v4(r_ver_major);
		if (detected_ver_major == 0) {
			return ERR_PRINTER_ON_FIRE;
		}
		if (detected_ver_major != r_ver_major) {
			r_ver_major = detected_ver_major;
			err = ERR_PRINTER_ON_FIRE;
		}
	} else {
		r_ver_major = 3;
	}
	if (err != OK) {
		err = _load_settings_binary(f, path, r_ver_major, true);
		f->seek(0);
	}
	if (err != OK) {
		r_ver_major = 0;
	}
	return err;
}

Error ProjectConfigLoader::load_cfb(const String path, uint32_t ver_major, uint32_t ver_minor) {
	cfb_path = path;
	String ext = path.get_extension().to_lower();
	Error err = OK;
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ, &err);
	ERR_FAIL_COND_V_MSG(f.is_null(), err, "Could not open " + path);
	if (ext == "cfg" || ext == "godot") {
		err = _load_settings_text(f, path, ver_major);
		if (err == OK && ver_major == 0) {
			auto ret = get_ver_major_and_minor_for_config_version(config_version);
			ver_major = ret.first;
			ver_minor = ret.second;
		}
	} else {
		if (ver_major == 0 && ext == "cfb") {
			ver_major = 2;
		}
		if (ver_major == 0) {
			err = _try_load_binary_v3_or_v4(path, ver_major);
		} else {
			err = _load_settings_binary(f, path, ver_major, false);
		}
		if (err == OK) {
			config_version = get_config_version_for_version(ver_major, ver_minor);
		}
	}
	ERR_FAIL_COND_V(err, err);
	major = ver_major;
	minor = ver_minor;
	loaded = true;
	return OK;
}

Error ProjectConfigLoader::save_cfb(const String dir, uint32_t ver_major, uint32_t ver_minor) {
	ERR_FAIL_COND_V_MSG(!loaded, ERR_INVALID_DATA, "Attempted to save project config when not loaded!");
	String file;
	if (ver_major == 0) {
		ver_major = major;
		ver_minor = minor;
	}
	if (ver_major > 2) {
		file = "project.godot";
	} else {
		file = "engine.cfg";
	}

	return save_custom(dir.path_join(file).replace("res://", ""), ver_major, ver_minor);
}

Error ProjectConfigLoader::save_cfb_binary(const String dir, uint32_t ver_major, uint32_t ver_minor) {
	ERR_FAIL_COND_V_MSG(!loaded, ERR_INVALID_DATA, "Attempted to save project config when not loaded!");
	String file;
	if (ver_major == 0) {
		ver_major = major;
		ver_minor = minor;
	}
	if (ver_major > 2) {
		file = "project.binary";
	} else {
		file = "engine.cfb";
	}

	return save_custom(dir.path_join(file).replace("res://", ""), ver_major, ver_minor);
}

bool ProjectConfigLoader::has_setting(String p_var) const {
	return props.has(p_var);
}

Variant ProjectConfigLoader::get_setting(String p_var, Variant default_value) const {
	if (props.has(p_var)) {
		return props[p_var].variant;
	}
	return default_value;
}

Dictionary ProjectConfigLoader::get_section(const String &p_var) const {
	Dictionary section;
	String section_name = p_var;
	if (!section_name.ends_with("/")) {
		section_name += "/";
	}
	for (const auto &E : props) {
		String key = E.key;
		if (key.begins_with(section_name)) {
			section[key.trim_prefix(section_name)] = E.value.variant;
		}
	}
	return section;
}

Error ProjectConfigLoader::remove_setting(String p_var) {
	if (props.has(p_var)) {
		props.erase(p_var);
		return OK;
	}
	return ERR_FILE_NOT_FOUND;
}

Error ProjectConfigLoader::set_setting(String p_var, Variant value) {
	if (props.has(p_var)) {
		props[p_var].variant = value;
	} else {
		props[p_var] = VariantContainer(value, last_builtin_order++, true);
	}
	return OK;
}

Error ProjectConfigLoader::_load_settings_binary(Ref<FileAccess> f, const String &p_path, uint32_t ver_major, bool fail_on_corrupt) {
	Error err;
	uint8_t hdr[4];
	config_version = 0;
	int bytes_read = f->get_buffer(hdr, 4);
	if (hdr[0] != 'E' || hdr[1] != 'C' || hdr[2] != 'F' || hdr[3] != 'G') {
		ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, "Corrupted header in binary project.binary (not ECFG).");
	} else if (bytes_read < 4) {
		WARN_PRINT("Bytes read less than slen!");
	}

	uint32_t count = f->get_32();

	for (uint32_t i = 0; i < count; i++) {
		uint32_t slen = f->get_32();
		CharString cs;
		cs.resize_uninitialized(slen + 1);
		cs[slen] = 0;
		auto actual_bytes_read = f->get_buffer((uint8_t *)cs.ptr(), slen);
		if (actual_bytes_read < slen) {
			WARN_PRINT("Bytes read less than slen!");
		}
		String key;
		key.append_utf8(cs.ptr());

		uint32_t vlen = f->get_32();
		Vector<uint8_t> d;
		d.resize(vlen);
		f->get_buffer(d.ptrw(), vlen);
		Variant value;
		err = VariantDecoderCompat::decode_variant_compat(ver_major, value, d.ptr(), d.size(), NULL, true);
		if (err != OK && fail_on_corrupt) {
			return err;
		}
		ERR_CONTINUE_MSG(err != OK, "Error decoding property: " + key + ".");
		props[key] = VariantContainer(value, last_builtin_order++, true);
	}
	cfb_path = p_path;
	return OK;
}

Error ProjectConfigLoader::_load_settings_text(Ref<FileAccess> f, const String &p_path, uint32_t ver_major) {
	Error err;
	last_builtin_order = 0;

	if (f.is_null()) {
		// FIXME: Above 'err' error code is ERR_FILE_CANT_OPEN if the file is missing
		// This needs to be streamlined if we want decent error reporting
		return ERR_FILE_NOT_FOUND;
	}

	VariantParser::StreamFile stream;
	stream.f = f;

	String assign;
	Variant value;
	VariantParser::Tag next_tag;

	int lines = 0;
	String error_text;
	String section;
	config_version = 0;

	while (true) {
		assign = Variant();
		next_tag.fields.clear();
		next_tag.name = String();

		err = VariantParserCompat::parse_tag_assign_eof(&stream, lines, error_text, next_tag, assign, value, nullptr, true);
		if (err == ERR_FILE_EOF) {
			return OK;
		}
		ERR_FAIL_COND_V_MSG(err != OK, err, "Error parsing " + p_path + " at line " + itos(lines) + ": " + error_text + " File might be corrupted.");

		if (!assign.is_empty()) {
			if (section.is_empty() && assign == "config_version") {
				config_version = value;
				ERR_FAIL_COND_V_MSG(config_version > ProjectSettings::CONFIG_VERSION, ERR_FILE_CANT_OPEN, vformat("Can't open project at '%s', its `config_version` (%d) is from a more recent and incompatible version of the engine. Expected config version: %d.", p_path, config_version, ProjectSettings::CONFIG_VERSION));
			} else {
				if (section.is_empty()) {
					props[assign] = VariantContainer(value, last_builtin_order++, true);
				} else {
					props[section + "/" + assign] = VariantContainer(value, last_builtin_order++, true);
				}
			}
		} else if (!next_tag.name.is_empty()) {
			section = next_tag.name;
		}
	}
}

struct _VCSort {
	String name;
	Variant::Type type = Variant::VARIANT_MAX;
	int order = 0;
	int flags = 0;

	bool operator<(const _VCSort &p_vcs) const { return order == p_vcs.order ? name < p_vcs.name : order < p_vcs.order; }
};

RBMap<String, List<String>> ProjectConfigLoader::get_save_proops() const {
	RBSet<_VCSort> vclist;

	for (RBMap<StringName, VariantContainer>::Element *G = props.front(); G; G = G->next()) {
		const VariantContainer *v = &G->get();

		if (v->hide_from_editor) {
			continue;
		}

		_VCSort vc;
		vc.name = G->key(); //*k;
		vc.order = v->order;
		vc.type = v->variant.get_type();
		vc.flags = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
		if (v->variant == v->initial) {
			continue;
		}

		vclist.insert(vc);
	}
	RBMap<String, List<String>> proops;

	for (RBSet<_VCSort>::Element *E = vclist.front(); E; E = E->next()) {
		String category = E->get().name;
		String name = E->get().name;

		int div = category.find("/");

		if (div < 0) {
			category = "";
		} else {
			category = category.substr(0, div);
			name = name.substr(div + 1, name.size());
		}
		proops[category].push_back(name);
	}
	return proops;
}

Error ProjectConfigLoader::save_custom(const String &p_path, uint32_t ver_major, uint32_t ver_minor) {
	ERR_FAIL_COND_V_MSG(p_path == "", ERR_INVALID_PARAMETER, "Project settings save path cannot be empty.");

	RBMap<String, List<String>> proops = get_save_proops();
	String ext = p_path.get_extension().to_lower();

	if (ver_major == 0) {
		ver_major = major;
		ver_minor = minor;
	}

	if (ext == "godot" || ext == "cfg") {
		return _save_settings_text(p_path, proops, ver_major, ver_minor);
	} else if (ext == "binary" || ext == "cfb") {
		return _save_settings_binary(p_path, proops, ver_major, ver_minor);
	} else {
		ERR_FAIL_V_MSG(ERR_FILE_UNRECOGNIZED, vformat("Unknown config file format: '%s'.", p_path));
	}
}

Error ProjectConfigLoader::_save_settings_text(const String &p_file, const RBMap<String, List<String>> &proops, const uint32_t ver_major, const uint32_t ver_minor) {
	Error err;
	Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Couldn't save project.godot - " + p_file + ".");
	return _save_settings_text_file(file, proops, ver_major, ver_minor);
}

Error ProjectConfigLoader::_save_settings_text_file(const Ref<FileAccess> &file, const RBMap<String, List<String>> &proops, uint32_t ver_major, uint32_t ver_minor) {
	int text_config_version = get_config_version_for_version(ver_major, ver_minor);

	if (text_config_version > 2) {
		file->store_line("; Engine configuration file.");
		file->store_line("; It's best edited using the editor UI and not directly,");
		file->store_line("; since the parameters that go here are not all obvious.");
		file->store_line(";");
		file->store_line("; Format:");
		file->store_line(";   [section] ; section goes between []");
		file->store_line(";   param=value ; assign values to parameters");
		file->store_line("");

		file->store_string("config_version=" + itos(text_config_version) + "\n");
		file->store_string("\n");
	}

	for (RBMap<String, List<String>>::Element *E = proops.front(); E; E = E->next()) {
		if (E != proops.front()) {
			file->store_string("\n");
		}

		if (E->key() != "") {
			file->store_string("[" + E->key() + "]\n\n");
		}
		for (List<String>::Element *F = E->get().front(); F; F = F->next()) {
			String key = F->get();
			if (E->key() != "") {
				key = E->key() + "/" + key;
			}
			Variant value;
			value = props[key].variant;

			String vstr;
			VariantWriterCompat::write_to_string_pcfg(value, vstr, ver_major);
			file->store_string(F->get().property_name_encode() + "=" + vstr + "\n");
		}
	}
	return OK;
}

Error ProjectConfigLoader::_save_settings_binary(const String &p_file, const RBMap<String, List<String>> &proops, const uint32_t ver_major, const uint32_t ver_minor, const CustomMap &p_custom, const String &p_custom_features) {
	Error err;
	Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Couldn't save project.binary at '%s'.", p_file));

	uint8_t hdr[4] = { 'E', 'C', 'F', 'G' };
	file->store_buffer(hdr, 4);

	int count = 0;

	for (const KeyValue<String, List<String>> &E : proops) {
		count += E.value.size();
	}

	if (!p_custom_features.is_empty()) {
		// Store how many properties are saved, add one for custom features, which must always go first.
		file->store_32(uint32_t(count + 1));
		String key = CoreStringName(_custom_features);
		file->store_pascal_string(key);

		int len;
		err = VariantDecoderCompat::encode_variant_compat(ver_major, p_custom_features, nullptr, len, false);
		ERR_FAIL_COND_V(err != OK, err);

		Vector<uint8_t> buff;
		buff.resize(len);

		err = VariantDecoderCompat::encode_variant_compat(ver_major, p_custom_features, buff.ptrw(), len, false);
		ERR_FAIL_COND_V(err != OK, err);
		file->store_32(uint32_t(len));
		file->store_buffer(buff.ptr(), buff.size());

	} else {
		// Store how many properties are saved.
		file->store_32(uint32_t(count));
	}

	for (const KeyValue<String, List<String>> &E : proops) {
		for (const String &key : E.value) {
			String k = key;
			if (!E.key.is_empty()) {
				k = E.key + "/" + k;
			}
			Variant value;
			if (p_custom.has(k)) {
				value = p_custom[k];
			} else {
				value = props[k].variant;
			}

			file->store_pascal_string(k);

			int len;
			err = VariantDecoderCompat::encode_variant_compat(ver_major, value, nullptr, len, true);
			ERR_FAIL_COND_V_MSG(err != OK, ERR_INVALID_DATA, "Error when trying to encode Variant.");

			Vector<uint8_t> buff;
			buff.resize(len);

			err = VariantDecoderCompat::encode_variant_compat(ver_major, value, buff.ptrw(), len, true);
			ERR_FAIL_COND_V_MSG(err != OK, ERR_INVALID_DATA, "Error when trying to encode Variant.");
			file->store_32(uint32_t(len));
			file->store_buffer(buff.ptr(), buff.size());
		}
	}

	return OK;
}

String ProjectConfigLoader::get_as_text(bool p_skip_cr) {
	RBMap<String, List<String>> proops = get_save_proops();
	Ref<FileAccessBuffer> f;
	f.instantiate();
	f->open_new();

	Error err = _save_settings_text_file(f, proops, major, minor);
	ERR_FAIL_COND_V_MSG(err != OK, "", "Failed to save project.godot");
	return f->get_as_text(p_skip_cr);
}

String ProjectConfigLoader::get_project_settings_as_string(const String &p_path) {
	int ver_major = GDRESettings::get_singleton()->get_ver_major();
	int ver_minor = GDRESettings::get_singleton()->get_ver_minor();
	Error err;
	Ref<ProjectConfigLoader> loader = Ref<ProjectConfigLoader>(memnew(ProjectConfigLoader));
	err = loader->load_cfb(p_path, ver_major, ver_minor);
	ERR_FAIL_COND_V_MSG(err != OK, "", "Failed to load project.godot");
	return loader->get_as_text();
}

ProjectConfigLoader::ProjectConfigLoader() {
}

ProjectConfigLoader::~ProjectConfigLoader() {
}

void ProjectConfigLoader::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_cfb", "path", "ver_major", "ver_minor"), &ProjectConfigLoader::load_cfb, DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("save_cfb", "dir", "ver_major", "ver_minor"), &ProjectConfigLoader::save_cfb, DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("save_cfb_binary", "dir", "ver_major", "ver_minor"), &ProjectConfigLoader::save_cfb_binary, DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("save_custom", "path", "ver_major", "ver_minor"), &ProjectConfigLoader::save_custom, DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("has_setting", "var"), &ProjectConfigLoader::has_setting);
	ClassDB::bind_method(D_METHOD("get_setting", "var", "default_value"), &ProjectConfigLoader::get_setting);
	ClassDB::bind_method(D_METHOD("remove_setting", "var"), &ProjectConfigLoader::remove_setting);
	ClassDB::bind_method(D_METHOD("set_setting", "var", "value"), &ProjectConfigLoader::set_setting);
	ClassDB::bind_method(D_METHOD("get_config_version"), &ProjectConfigLoader::get_config_version);
}
