#include "fontfile_exporter.h"
#include "compat/resource_loader_compat.h"
#include "exporters/export_report.h"
#include "utility/common.h"
#include "utility/image_saver.h"

static const HashSet<String> dynamic_font_supported_extensions = { "otf", "ttf", "woff", "woff2", "ttc", "otc", "pfb", "pfm" };

Error FontFileExporter::export_file(const String &p_dest_path, const String &p_src_path) {
	String ext = p_dest_path.get_extension().to_lower();

	if (dynamic_font_supported_extensions.has(ext)) {
		return _export_font_data_dynamic(p_dest_path, p_src_path);
	}
	Ref<Image> r_image;
	return _export_image(p_dest_path, p_src_path, r_image);
}

Error FontFileExporter::_export_font_data_dynamic(const String &p_dest_path, const String &p_src_path) {
	Error err;
	Ref<Resource> fontfile = ResourceCompatLoader::fake_load(p_src_path, "", &err);
	ERR_FAIL_COND_V_MSG(err, err, "Failed to load font file " + p_src_path);
	PackedByteArray data = fontfile->get("data");
	ERR_FAIL_COND_V_MSG(data.size() == 0, ERR_FILE_CORRUPT, "Font file " + p_src_path + " is empty");
	return write_to_file(p_dest_path, data);
}

Error FontFileExporter::_export_image(const String &p_dest_path, const String &p_src_path, Ref<Image> &r_image) {
	Error err;
	Ref<FontFile> fontfile = ResourceCompatLoader::non_global_load(p_src_path, "", &err);
	ERR_FAIL_COND_V_MSG(err, err, "Failed to load font file " + p_src_path);
	r_image = fontfile->get_texture_image(0, {}, 0);
	ERR_FAIL_COND_V_MSG(r_image.is_null(), ERR_FILE_CORRUPT, "Font file " + p_src_path + " is not an image");
	return ImageSaver::save_image(p_dest_path, r_image, false);
}

Ref<ExportReport> FontFileExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	// Check if the exporter can handle the given importer and resource type
	String src_path = import_infos->get_path();
	String dst_path = output_dir.path_join(import_infos->get_export_dest().replace("res://", ""));
	Ref<ExportReport> report = memnew(ExportReport(import_infos, get_name()));
	if (import_infos->get_importer() == "font_data_dynamic") {
		Error err = _export_font_data_dynamic(dst_path, src_path);
		report->set_error(err);
		report->set_saved_path(dst_path);
		if (err == OK && import_infos->get_ver_major() >= 4) {
			Ref<FontFile> fontfile = ResourceCompatLoader::gltf_load(src_path, "", &err);
			if (!(err || fontfile.is_null())) {
				auto res_info = ResourceInfo::get_info_from_resource(fontfile);
				Dictionary params;
				// r_options->push_back(ImportOption(PropertyInfo(Variant::NIL, "Rendering", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_GROUP), Variant()));

				// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "antialiasing", PROPERTY_HINT_ENUM, "None,Grayscale,LCD Subpixel"), 1));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "generate_mipmaps"), false));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "disable_embedded_bitmaps"), true));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "multichannel_signed_distance_field", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), (msdf) ? true : false));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "msdf_pixel_range", PROPERTY_HINT_RANGE, "1,100,1"), 8));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "msdf_size", PROPERTY_HINT_RANGE, "1,250,1"), 48));

				// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "allow_system_fallback"), true));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "force_autohinter"), false));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "modulate_color_glyphs"), false));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "hinting", PROPERTY_HINT_ENUM, "None,Light,Normal"), 1));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "subpixel_positioning", PROPERTY_HINT_ENUM, "Disabled,Auto,One Half of a Pixel,One Quarter of a Pixel,Auto (Except Pixel Fonts)"), 4));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "keep_rounding_remainders"), true));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::FLOAT, "oversampling", PROPERTY_HINT_RANGE, "0,10,0.1"), 0.0));

				// ConfigFile interprets setting a key to null as erasing the key, so we have to use a special value that'll get replaced when saving.
				params["Rendering"] = ImportInfo::NULL_REPLACEMENT;
				params["antialiasing"] = fontfile->get_antialiasing();
				params["generate_mipmaps"] = fontfile->get_generate_mipmaps();
				if (import_infos->get_ver_minor() >= 3) {
					params["disable_embedded_bitmaps"] = fontfile->get_disable_embedded_bitmaps();
				}
				params["multichannel_signed_distance_field"] = fontfile->is_multichannel_signed_distance_field();
				params["msdf_pixel_range"] = fontfile->get_msdf_pixel_range();
				params["msdf_size"] = fontfile->get_msdf_size();
				params["allow_system_fallback"] = fontfile->is_allow_system_fallback();
				params["force_autohinter"] = fontfile->is_force_autohinter();
				if (import_infos->get_ver_minor() >= 5) {
					params["modulate_color_glyphs"] = fontfile->is_modulate_color_glyphs();
				}
				params["hinting"] = fontfile->get_hinting();
				if (import_infos->get_ver_minor() >= 4) {
					params["subpixel_positioning"] = fontfile->get_subpixel_positioning();
					params["keep_rounding_remainders"] = fontfile->get_keep_rounding_remainders();
				}
				params["oversampling"] = fontfile->get_oversampling();

				// r_options->push_back(ImportOption(PropertyInfo(Variant::NIL, "Fallbacks", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_GROUP), Variant()));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::ARRAY, "fallbacks", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("Font")), Array()));
				params["Fallbacks"] = ImportInfo::NULL_REPLACEMENT;
				params["fallbacks"] = gdre::array_from_typed_array(fontfile->get_fallbacks());

				// options_general.push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::NIL, "Compress", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_GROUP), Variant()));
				// options_general.push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "compress", PROPERTY_HINT_NONE, ""), false));
				params["Compress"] = ImportInfo::NULL_REPLACEMENT;
				params["compress"] = res_info.is_valid() ? res_info->is_compressed : true;

				// // Hide from the main UI, only for advanced import dialog.
				// r_options->push_back(ImportOption(PropertyInfo(Variant::ARRAY, "preload", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), Array()));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::DICTIONARY, "language_support", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), Dictionary()));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::DICTIONARY, "script_support", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), Dictionary()));
				// r_options->push_back(ImportOption(PropertyInfo(Variant::DICTIONARY, "opentype_features", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), Dictionary()));

				// TODO? FontFile doesn't seem to store anything that would indicate that there were preload configurations set
				params["preload"] = Array();
				Dictionary language_support;
				for (auto &language : fontfile->get_language_support_overrides()) {
					language_support[language] = fontfile->get_language_support_override(language);
				}
				params["language_support"] = language_support;
				Dictionary script_support;
				for (auto &script : fontfile->get_script_support_overrides()) {
					script_support[script] = fontfile->get_script_support_override(script);
				}
				params["script_support"] = script_support;
				params["opentype_features"] = fontfile->get_opentype_features();
				import_infos->set_params(params);
			}
		}
	} else if (import_infos->get_importer() == "font_data_image") {
		Ref<Image> r_image;
		Error err = _export_image(dst_path, src_path, r_image);
		report->set_error(err);
		report->set_saved_path(dst_path);
		if (err == OK && import_infos->get_ver_major() >= 4) {
			_set_image_import_info(import_infos, r_image);
		}
	}
	return report;
}

struct GlyphInfo {
	int glyph_index;
	Vector2 advance;
	Vector2 offset;
	Vector2 size;
	Rect2 uv_rect;
	int texture_idx;

	String to_string() const {
		int advance_value = advance.x - size.x;
		Vector2i offset_value = offset - Vector2(0, -(size.y / 2));
		if (offset_value == Vector2i() && advance_value == 0) {
			return String::num_int64(glyph_index);
		}
		return vformat("%d %d %d %d", glyph_index, advance_value, offset_value.x, offset_value.y);
	}
};

Vector<GlyphInfo> _get_character_ranges(Ref<Resource> fontfile) {
	// the reason we got it as a fake resource; we need to get the glyphs in the order that they are in the fontfile
	// 	[resource]
	// antialiasing = 0
	// subpixel_positioning = 0
	// allow_system_fallback = false
	// hinting = 0
	// oversampling = 1.0
	// fixed_size = 16
	// fixed_size_scale_mode = 2
	// cache/0/16/0/ascent = 8.0
	// cache/0/16/0/descent = 8.0
	// cache/0/16/0/underline_position = 0.0
	// cache/0/16/0/underline_thickness = 0.0
	// cache/0/16/0/scale = 1.0
	// cache/0/16/0/textures/0/offsets = PackedInt32Array()
	// cache/0/16/0/textures/0/image = SubResource("Image_uer0y")
	// cache/0/16/0/glyphs/65/advance = Vector2(10, 0)
	// cache/0/16/0/glyphs/65/offset = Vector2(0, -8)
	// cache/0/16/0/glyphs/65/size = Vector2(10, 16)
	// cache/0/16/0/glyphs/65/uv_rect = Rect2(0, 0, 10, 16)
	// cache/0/16/0/glyphs/65/texture_idx = 0
	// cache/0/16/0/glyphs/66/advance = Vector2(10, 0)
	// cache/0/16/0/glyphs/66/offset = Vector2(0, -8)
	// cache/0/16/0/glyphs/66/size = Vector2(10, 16)
	// cache/0/16/0/glyphs/66/uv_rect = Rect2(10, 0, 10, 16)
	// cache/0/16/0/glyphs/66/texture_idx = 0

	HashMap<int, GlyphInfo> glyph_map;
	Vector<int> glyph_list;

	List<PropertyInfo> property_list;
	fontfile->get_property_list(&property_list);
	for (auto &property : property_list) {
		if (property.name.begins_with("cache/") && property.name.contains("/glyphs/")) {
			PackedStringArray tokens = property.name.split("/");
			if (tokens.size() >= 6) {
				int glyph_index = tokens[5].to_int();
				if (!glyph_map.has(glyph_index)) {
					glyph_map[glyph_index] = GlyphInfo();
					glyph_map[glyph_index].glyph_index = glyph_index;
					glyph_list.push_back(glyph_index);
				}
				if (tokens[6] == "advance") {
					glyph_map[glyph_index].advance = fontfile->get(property.name);
				} else if (tokens[6] == "offset") {
					glyph_map[glyph_index].offset = fontfile->get(property.name);
				} else if (tokens[6] == "size") {
					glyph_map[glyph_index].size = fontfile->get(property.name);
				} else if (tokens[6] == "uv_rect") {
					glyph_map[glyph_index].uv_rect = fontfile->get(property.name);
				} else if (tokens[6] == "texture_idx") {
					glyph_map[glyph_index].texture_idx = fontfile->get(property.name);
				}
			}
		}
	}
	Vector<String> character_ranges;
	Vector<GlyphInfo> glyph_infos;
	// to ensure order is preserved
	for (auto &glyph : glyph_list) {
		glyph_infos.push_back(glyph_map[glyph]);
	}
	return glyph_infos;
}

void FontFileExporter::_set_image_import_info(Ref<ImportInfo> import_infos, Ref<Image> image) {
	// we have to set the parameters of the image import info

	Error err;
	Ref<Resource> fontfile = ResourceCompatLoader::fake_load(import_infos->get_path(), "", &err);
	ERR_FAIL_COND_MSG(err, "Failed to load font file " + import_infos->get_path());

	Vector<GlyphInfo> glyph_infos = _get_character_ranges(fontfile);
	ERR_FAIL_COND_MSG(glyph_infos.is_empty(), "No glyphs found in font file " + import_infos->get_path());
	PackedStringArray character_ranges;
	Vector<Vector2> sizes;

	HashSet<int> y_rect_set;
	Array fallbacks = fontfile->get("fallbacks");
	int x_rect_idx = 0;
	int max_x_rect_idx = 0;
	int chr_width = glyph_infos[0].size.x;
	int chr_height = glyph_infos[0].size.y;
	int chr_cell_width = 0;
	int chr_cell_height = 0;
	for (auto &glyph : glyph_infos) {
		// font->set_glyph_uv_rect(0, Vector2i(chr_height, 0), idx, Rect2(img_margin.position.x + chr_cell_width * x + char_margin.position.x, img_margin.position.y + chr_cell_height * y + char_margin.position.y, chr_width, chr_height));
		if (!y_rect_set.has((int)(glyph.uv_rect.position.y))) {
			if (y_rect_set.size() == 1 && chr_cell_height == 0) {
				chr_cell_height = glyph.uv_rect.position.y - glyph_infos[0].uv_rect.position.y;
			}
			y_rect_set.insert((int)(glyph.uv_rect.position.y));
			// new row
			max_x_rect_idx = MAX(max_x_rect_idx, x_rect_idx);
			x_rect_idx = 0;
		}
		if (x_rect_idx == 1 && chr_cell_width == 0) {
			chr_cell_width = glyph.uv_rect.position.x - glyph_infos[0].uv_rect.position.x;
		}
		character_ranges.push_back(glyph.to_string());
		sizes.push_back(glyph.size);
		x_rect_idx++;
	}
	int rows = y_rect_set.size();
	int columns = max_x_rect_idx;

	// font->set_glyph_uv_rect(0, Vector2i(chr_height, 0), idx, Rect2(img_margin.position.x + chr_cell_width * x + char_margin.position.x, img_margin.position.y + chr_cell_height * y + char_margin.position.y, chr_width, chr_height));
	int img_margin_pos_x_plus_char_margin_pos_x = glyph_infos[0].uv_rect.position.x; // x = 0
	int img_margin_pos_y_plus_char_margin_pos_y = glyph_infos[0].uv_rect.position.y; // y = 0
	// int chr_width = chr_cell_width - char_margin.position.x - char_margin.size.x;
	// int chr_height = chr_cell_height - char_margin.position.y - char_margin.size.y;
	int char_margin_pos_x_plus_size_x = -(chr_width - chr_cell_width);
	int char_margin_pos_y_plus_size_y = -(chr_height - chr_cell_height);
	// int chr_cell_width = (img->get_width() - img_margin.position.x - img_margin.size.x) / columns;
	// int chr_cell_height = (img->get_height() - img_margin.position.y - img_margin.size.y) / rows;
	int img_margin_pos_x_plus_size_x = -(columns * chr_cell_width - image->get_width());
	int img_margin_pos_y_plus_size_y = -(rows * chr_cell_height - image->get_height());
	int img_margin_size_x_plus_char_margin_size_x = (img_margin_pos_x_plus_size_x + char_margin_pos_x_plus_size_x) - img_margin_pos_x_plus_char_margin_pos_x;
	int img_margin_size_y_plus_char_margin_size_y = (img_margin_pos_y_plus_size_y + char_margin_pos_y_plus_size_y) - img_margin_pos_y_plus_char_margin_pos_y;

	int char_margin_size_x = 0; // TODO!
	int char_margin_size_y = 0; // TODO!
	int char_margin_pos_x = 0; // TODO!
	int char_margin_pos_y = 0; // TODO!
	int img_margin_pos_x = 0; // TODO!
	int img_margin_pos_y = 0; // TODO!
	int img_margin_size_x = 0; // TODO!
	int img_margin_size_y = 0; // TODO!

	if (img_margin_pos_x_plus_size_x != 0 || char_margin_pos_x_plus_size_x != 0) {
		for (int i = 0; i < chr_cell_width; i++) {
			for (int j = 0; j < chr_cell_width; j++) {
				char_margin_size_x = i;
				char_margin_pos_x = j;

				if (char_margin_size_x + char_margin_pos_x != char_margin_pos_x_plus_size_x) {
					continue;
				}

				img_margin_pos_x = img_margin_pos_x_plus_char_margin_pos_x - char_margin_pos_x;
				img_margin_size_x = img_margin_size_x_plus_char_margin_size_x - char_margin_size_x;

				if (img_margin_pos_x + img_margin_size_x == img_margin_pos_x_plus_size_x) {
					break;
				}
			}
		}
		// do the same for the y axis
		for (int i = 0; i < chr_cell_height; i++) {
			for (int j = 0; j < chr_cell_height; j++) {
				char_margin_size_y = i;
				char_margin_pos_y = j;

				if (char_margin_size_y + char_margin_pos_y != char_margin_pos_y_plus_size_y) {
					continue;
				}

				img_margin_pos_y = img_margin_pos_y_plus_char_margin_pos_y - char_margin_pos_y;
				img_margin_size_y = img_margin_size_y_plus_char_margin_size_y - char_margin_size_y;

				if (img_margin_pos_y + img_margin_size_y == img_margin_pos_y_plus_size_y) {
					break;
				}
			}
		}
	}

	List<PropertyInfo> property_list;
	fontfile->get_property_list(&property_list);

	int ascent = 0;
	int descent = 0;
	// float scale = 0;
	int fixed_size_scale_mode = fontfile->get("fixed_size_scale_mode");
	for (auto &property : property_list) {
		if (property.name.ends_with("/ascent")) {
			ascent = fontfile->get(property.name);
		} else if (property.name.ends_with("/descent")) {
			descent = fontfile->get(property.name);
			// } else if (property.name.ends_with("/scale")) {
			// 	scale = fontfile->get(property.name);
		}
	}

	// r_options->push_back(ImportOption(PropertyInfo(Variant::PACKED_STRING_ARRAY, "character_ranges"), Vector<String>()));
	// r_options->push_back(ImportOption(PropertyInfo(Variant::PACKED_STRING_ARRAY, "kerning_pairs"), Vector<String>()));
	// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "columns", PROPERTY_HINT_RANGE, "1,1024,1,or_greater"), 1));
	// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "rows", PROPERTY_HINT_RANGE, "1,1024,1,or_greater"), 1));
	// r_options->push_back(ImportOption(PropertyInfo(Variant::RECT2I, "image_margin"), Rect2i()));
	// r_options->push_back(ImportOption(PropertyInfo(Variant::RECT2I, "character_margin"), Rect2i()));
	// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "ascent"), 0));
	// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "descent"), 0));

	// r_options->push_back(ImportOption(PropertyInfo(Variant::ARRAY, "fallbacks", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("Font")), Array()));

	// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "compress"), true));
	// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "scaling_mode", PROPERTY_HINT_ENUM, "Disabled,Enabled (Integer),Enabled (Fractional)"), TextServer::FIXED_SIZE_SCALE_ENABLED));

	Dictionary params;
	params["character_ranges"] = character_ranges;
	params["kerning_pairs"] = PackedStringArray(); // TODO
	params["columns"] = columns;
	params["rows"] = rows;
	params["image_margin"] = Rect2i(img_margin_pos_x, img_margin_pos_y, img_margin_size_x, img_margin_size_y);
	params["character_margin"] = Rect2i(char_margin_pos_x, char_margin_pos_y, char_margin_size_x, char_margin_size_y);
	params["ascent"] = ascent;
	params["descent"] = descent;
	params["fallbacks"] = fallbacks;
	params["compress"] = true;
	params["scaling_mode"] = fixed_size_scale_mode;
	import_infos->set_params(params);
}

void FontFileExporter::get_handled_types(List<String> *out) const {
	out->push_back("FontFile");
}

void FontFileExporter::get_handled_importers(List<String> *out) const {
	out->push_back("font_data_dynamic");
	out->push_back("font_data_image");
}

String FontFileExporter::get_name() const {
	return EXPORTER_NAME;
}

String FontFileExporter::get_default_export_extension(const String &res_path) const {
	return "ttf";
}
