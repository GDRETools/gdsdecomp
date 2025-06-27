#include "texture_exporter.h"

#include "compat/resource_compat_binary.h"
#include "compat/resource_loader_compat.h"
#include "utility/common.h"

#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/io/image_loader.h"
#include "core/io/resource_loader.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/compressed_texture.h"
#include "utility/resource_info.h"
#include <cstdint>
namespace {
bool get_bit(const Vector<uint8_t> &bitmask, int width, int p_x, int p_y) {
	int ofs = width * p_y + p_x;
	int bbyte = ofs / 8;
	int bbit = ofs % 8;

	return (bitmask[bbyte] & (1 << bbit)) != 0;
}
} //namespace

// Format is the same on V2 - V4
Ref<Image> TextureExporter::load_image_from_bitmap(const String p_path, Error *r_err) {
	Error err = OK;
	if (!r_err) {
		r_err = &err;
	}
	Ref<Image> image;
	image.instantiate();
	ResourceFormatLoaderCompatBinary rlcb;
	auto res = ResourceCompatLoader::fake_load(p_path, "", r_err);
	ERR_FAIL_COND_V_MSG(*r_err != OK, Ref<Image>(), "Cannot open resource '" + p_path + "'.");

	String name;
	Vector2 size;
	Dictionary data;
	PackedByteArray bitmask;
	int width;
	int height;

	// Load the main resource, which should be the ImageTexture
	name = ResourceCompatConverter::get_resource_name(res, 0);
	data = res->get("data");
	bitmask = data.get("data", PackedByteArray());
	size = data.get("size", Vector2());
	width = size.width;
	height = size.height;
	image->initialize_data(width, height, false, Image::FORMAT_L8);

	if (!name.is_empty()) {
		image->set_name(name);
	}
	for (int i = 0; i < width; i++) {
		for (int j = 0; j < height; j++) {
			image->set_pixel(i, j, get_bit(bitmask, width, i, j) ? Color(1, 1, 1) : Color(0, 0, 0));
		}
	}
	ERR_FAIL_COND_V_MSG(image.is_null() || image->is_empty(), Ref<Image>(), "Failed to load image from " + p_path);
	*r_err = OK;
	return image;
}

Error TextureExporter::_convert_bitmap(const String &p_path, const String &dest_path, bool lossy, Ref<ExportReport> report) {
	String dst_dir = dest_path.get_base_dir();
	Error err = OK;
	Ref<Image> img = load_image_from_bitmap(p_path, &err);
	// deprecated format
	if (err == ERR_UNAVAILABLE) {
		// TODO: Not reporting here because we can't get the deprecated format type yet,
		// implement functionality to pass it back
		print_line("Did not convert deprecated Bitmap resource " + p_path);
		return err;
	}
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to load bitmap " + p_path);
	err = gdre::ensure_dir(dst_dir);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to create dirs for " + dest_path);
	err = save_image(dest_path, img, lossy);
	if (err == ERR_UNAVAILABLE) {
		return err;
	}
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to save image " + dest_path + " from texture " + p_path);

	print_verbose("Converted " + p_path + " to " + dest_path);
	return OK;
}

bool dest_format_supports_mipmaps(const String &ext) {
	return ext == "dds" || ext == "exr";
}

Error TextureExporter::save_image(const String &dest_path, const Ref<Image> &img, bool lossy) {
	ERR_FAIL_COND_V_MSG(img->is_empty(), ERR_FILE_EOF, "Image data is empty for texture " + dest_path + ", not saving");
	Error err = gdre::ensure_dir(dest_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to create dirs for " + dest_path);
	String dest_ext = dest_path.get_extension().to_lower();
	if (!dest_format_supports_mipmaps(dest_ext) && img->is_compressed() && img->has_mipmaps()) {
		img->clear_mipmaps();
	}
	GDRE_ERR_DECOMPRESS_OR_FAIL(img);
	if (dest_ext == "jpg" || dest_ext == "jpeg") {
		err = img->save_jpg(dest_path, 1.0);
	} else if (dest_ext == "webp") {
		err = img->save_webp(dest_path, lossy, 1.0);
	} else if (dest_ext == "png") {
		err = img->save_png(dest_path);
	} else if (dest_ext == "tga") {
		err = gdre::save_image_as_tga(dest_path, img);
	} else if (dest_ext == "svg") {
		err = gdre::save_image_as_svg(dest_path, img);
	} else if (dest_ext == "dds") {
		err = img->save_dds(dest_path);
	} else if (dest_ext == "exr") {
		err = img->save_exr(dest_path);
	} else if (dest_ext == "bmp") {
		err = gdre::save_image_as_bmp(dest_path, img);
	} else {
		ERR_FAIL_V_MSG(ERR_FILE_BAD_PATH, "Invalid file name: " + dest_path);
	}
	return err;
}
enum V4ImporterCompressMode {
	COMPRESS_LOSSLESS,
	COMPRESS_LOSSY,
	COMPRESS_VRAM_COMPRESSED,
	COMPRESS_VRAM_UNCOMPRESSED,
	COMPRESS_BASIS_UNIVERSAL
};

enum ttttype {
	TEXTURE_2D,
	TEXTURE_3D,
	TEXTURE_LAYERED
};

void set_tex_params(Ref<ImportInfo> p_import_info, Ref<Resource> p_tex, Ref<Image> p_img, int ver_major, ttttype p_type) {
	// Import options reference:
	// compress/mode (int): Controls how the texture is compressed
	//   0: Lossless (PNG/WebP)
	//   1: Lossy (WebP)
	//   2: VRAM Compressed (S3TC/BPTC, ETC2/ASTC)
	//   3: VRAM Uncompressed
	//   4: Basis Universal
	//
	// compress/high_quality (bool): When true, uses higher quality compression formats
	//   - BPTC instead of S3TC
	//   - ASTC instead of ETC2
	//
	// compress/lossy_quality (float): Controls quality of lossy compression (0-1)
	//   - Higher values = better quality but larger file size
	//   - Default: 0.7
	//
	// compress/uastc_level (int): Controls Basis Universal UASTC compression quality/speed
	//   0: Fastest
	//   1: Faster
	//   2: Medium
	//   3: Slower
	//   4: Slowest
	//
	// compress/rdo_quality_loss (float): Rate-distortion optimization quality for Basis Universal (0-10)
	//   - Higher values = better compression but more quality loss
	//
	// compress/hdr_compression (int): Controls how HDR textures are handled
	//   0: Disabled
	//   1: Opaque Only
	//   2: Always
	//
	// compress/normal_map (int): Controls normal map compression
	//   0: Detect
	//   1: Enable
	//   2: Disabled
	//
	// compress/channel_pack (int): Controls how color channels are packed
	//   0: sRGB Friendly
	//   1: Optimized
	//
	// mipmaps/generate (bool): Whether to generate mipmaps
	//   - Default: true for 3D textures, false for 2D
	//
	// mipmaps/limit (int): Limits the number of mipmap levels (-1 to 256)
	//   - -1 means no limit
	//
	// roughness/mode (int): Controls how roughness is handled
	//   0: Detect
	//   1: Disabled
	//   2: Red
	//   3: Green
	//   4: Blue
	//   5: Alpha
	//   6: Gray
	//
	// roughness/src_normal (string): Path to normal map used for roughness calculation
	//   - Supports: bmp, dds, exr, jpeg, jpg, hdr, png, svg, tga, webp
	//
	// process/fix_alpha_border (bool): Fixes alpha border artifacts
	//   - Default: true for 2D textures, false for 3D
	//
	// process/premult_alpha (bool): Premultiplies alpha channel
	//
	// process/normal_map_invert_y (bool): Inverts the Y channel of normal maps
	//
	// process/hdr_as_srgb (bool): Treats HDR textures as sRGB
	//
	// process/hdr_clamp_exposure (bool): Clamps HDR exposure to prevent artifacts
	//
	// process/size_limit (int): Maximum texture dimension (0-16383)
	//   - 0 means no limit
	//
	// detect_3d/compress_to (int): Controls how 3D textures are compressed
	//   0: Disabled
	//   1: VRAM Compressed
	//   2: Basis Universal
	//
	// SVG-specific options (only for SVG files):
	// svg/scale (float): Scale factor for SVG import (0.001-100)
	//   - Default: 1.0
	//
	// editor/scale_with_editor_scale (bool): Whether to scale SVG with editor scale
	//
	// editor/convert_colors_with_editor_theme (bool): Whether to convert SVG colors to match editor theme

	//CompressedTexture2D::DATA_FORMAT_WEBP

	if (!(p_import_info.is_valid() && p_tex.is_valid() && ver_major >= 4)) {
		return;
	}

	String ext = p_import_info->get_source_file().get_extension().to_lower();
	Dictionary params;
	Ref<ResourceInfo> info = ResourceInfo::get_info_from_resource(p_tex);
	ERR_FAIL_COND_MSG(!info.is_valid(), "TEXTURE LOADERS SHOULD HAVE SET THE RESOURCE INFO FOR THIS TEXTURE!!!!!!!!");
	// Get the image from the texture
	Ref<Image> img;

	if (p_type == TEXTURE_2D) {
		Ref<Texture2D> tex2d = p_tex;
		img = tex2d->get_image();
	} else if (p_type == TEXTURE_3D) {
		Ref<Texture3D> tex3d = p_tex;
		img = p_img;
	} else if (p_type == TEXTURE_LAYERED) {
		Ref<TextureLayered> texlay = p_tex;
		img = p_img;
	}

	if (!img.is_valid()) {
		return;
	}
	int compress_mode = -1;
	CompressedTexture2D::DataFormat data_format = CompressedTexture2D::DATA_FORMAT_IMAGE;
	ERR_FAIL_COND(info->extra.is_empty());
	Dictionary extra = info->extra;
	int df = extra.get("data_format", -1);
	int tf = extra.get("texture_flags", -1);
	ERR_FAIL_COND(df == -1 || tf == -1);
	data_format = CompressedTexture2D::DataFormat(df);
	int texture_flags = tf;
	switch (data_format) {
		case CompressedTexture2D::DATA_FORMAT_PNG:
		case CompressedTexture2D::DATA_FORMAT_WEBP: // force WEBP to lossless
			compress_mode = COMPRESS_LOSSLESS;
			break;
		case CompressedTexture2D::DATA_FORMAT_BASIS_UNIVERSAL:
			compress_mode = COMPRESS_BASIS_UNIVERSAL;
			break;
		case CompressedTexture2D::DATA_FORMAT_IMAGE:
			if (img->is_compressed()) {
				compress_mode = COMPRESS_VRAM_COMPRESSED;
			} else {
				compress_mode = COMPRESS_VRAM_UNCOMPRESSED;
			}
			break;
		default: //DATA_FORMAT_IMAGE, we need to check the img format
			compress_mode = 0;
			break;
	}
	// Check if the image is compressed
	params["compress/mode"] = compress_mode;
	// Set high quality flag for VRAM compression
	auto format = img->get_format();
	auto decompressed_fmt = format;
	Ref<Image> decompressed_img = img;
	if (img->is_compressed()) {
		decompressed_img = img->duplicate();
		decompressed_img->decompress();
		decompressed_fmt = decompressed_img->get_format();
	}
	bool is_hdr = (decompressed_fmt >= Image::FORMAT_RF && decompressed_fmt <= Image::FORMAT_RGBE9995);

	if (img->is_compressed() && (is_hdr || ((format >= Image::FORMAT_BPTC_RGBA && format <= Image::FORMAT_BPTC_RGBFU)) || (format >= Image::FORMAT_ASTC_4x4 && format <= Image::FORMAT_ASTC_8x8_HDR))) {
		params["compress/high_quality"] = true;
	} else {
		params["compress/high_quality"] = false;
	}

	// Set lossy quality if using lossy compression
	params["compress/lossy_quality"] = 1.0; // prevent generational loss

	// Set Basis Universal parameters if used
	if (p_import_info->get_ver_minor() >= 5) {
		params["compress/uastc_level"] = 2; // Default is Fastest, but force to Medium to prevent generational loss
		params["compress/rdo_quality_loss"] = 0; // Default
	}
	params["compress/hdr_compression"] = is_hdr && img->is_compressed() ? 1 : 0;

	// enum FormatBits {
	// 	FORMAT_BIT_STREAM = 1 << 22,
	// 	FORMAT_BIT_HAS_MIPMAPS = 1 << 23,
	// 	FORMAT_BIT_DETECT_3D = 1 << 24,
	// 	//FORMAT_BIT_DETECT_SRGB = 1 << 25,
	// 	FORMAT_BIT_DETECT_NORMAL = 1 << 26,
	// 	FORMAT_BIT_DETECT_ROUGNESS = 1 << 27,
	// };
	// set it to "Detect" if the flag is set; else use "Disabled"
	if (p_type == TEXTURE_2D) {
		params["compress/normal_map"] = ((texture_flags & CompressedTexture2D::FORMAT_BIT_DETECT_NORMAL) != 0) ? 0 : 2;
	}
	// Set channel packing

	int channel_pack = 0;
	auto used_channels = decompressed_img->detect_used_channels();
	if (used_channels == Image::USED_CHANNELS_R || used_channels == Image::USED_CHANNELS_RG) {
		channel_pack = 1; // not sRGB friendly
	}
	params["compress/channel_pack"] = channel_pack;

	// Set mipmap settings
	params["mipmaps/generate"] = (texture_flags & CompressedTexture2D::FORMAT_BIT_HAS_MIPMAPS) != 0 || (img->has_mipmaps());
	int limit = -1;
	if (img->has_mipmaps()) {
		int mipmap_count = img->get_mipmap_count();
		int min_width, min_height;
		img->get_format_min_pixel_size(img->get_format(), min_width, min_height);
		if (mipmap_count > 1) {
			int64_t offset, size;
			int last_width, last_height;
			img->get_mipmap_offset_size_and_dimensions(mipmap_count - 1, offset, size, last_width, last_height);
			// it may be a non-power of 2 last mipmap, so it's either min height or width
			if (last_width != min_width && last_height != min_height) {
				limit = mipmap_count;
			}
		}
	}

	params["mipmaps/limit"] = limit;

	if (p_type == TEXTURE_2D) {
		bool is_roughness = (texture_flags & CompressedTexture2D::FORMAT_BIT_DETECT_ROUGNESS) != 0;
		if (is_roughness) {
			// TODO: this puts it into "Detect", which will make the editor detect what it is; maybe we can do better??
			params["roughness/mode"] = 0;
		} else {
			params["roughness/mode"] = 1;
		}

		// TODO: This? Probably not though, it is a destructive process to apply the roughness map
		params["roughness/src_normal"] = "";

		// Set processing options
		params["process/fix_alpha_border"] = false; // default true, but forcing to false to prevent re-fixing
		params["process/premult_alpha"] = false; // default false, and forcing cuz destructive
		params["process/normal_map_invert_y"] = false; // default true, but forcing to false to prevent re-inverting

		// Set HDR settings
		params["process/hdr_as_srgb"] = false;
		params["process/hdr_clamp_exposure"] = false;

		// Set size limit
		params["process/size_limit"] = 0; // Default to no limit

		// Set 3D detection
		int detect_3d_compress_to = 0;
		if (texture_flags & CompressedTexture2D::FORMAT_BIT_DETECT_3D) {
			if (compress_mode == COMPRESS_VRAM_COMPRESSED) {
				detect_3d_compress_to = 1;
			} else if (compress_mode == COMPRESS_BASIS_UNIVERSAL) {
				detect_3d_compress_to = 2;
			}
		}
		params["detect_3d/compress_to"] = detect_3d_compress_to;

		if (ext == "svg") {
			params["svg/scale"] = 1.0;
			// No editor scaling; we want it to stay the same size
			params["editor/scale_with_editor_scale"] = false;
			params["editor/convert_colors_with_editor_theme"] = false;
		}
	}

	// Set the parameters in the import info
	p_import_info->set_params(params);
}

Error TextureExporter::_convert_tex(const String &p_path, const String &dest_path, bool lossy, String &image_format, Ref<ExportReport> report) {
	Error err;
	String dst_dir = dest_path.get_base_dir();
	Ref<Texture2D> tex;
	tex = ResourceCompatLoader::non_global_load(p_path, "", &err);

	if (err == ERR_UNAVAILABLE) {
		// TODO: Not reporting here because we can't get the deprecated format type yet,
		// implement functionality to pass it back
		image_format = "Unknown deprecated image format";
		print_line("Did not convert deprecated Texture resource " + p_path);
		return err;
	}
	ERR_FAIL_COND_V_MSG(err != OK || tex.is_null(), err, "Failed to load texture " + p_path);

	Ref<Image> img = tex->get_image();

	ERR_FAIL_COND_V_MSG(img.is_null(), ERR_PARSE_ERROR, "Failed to load image for texture " + p_path);
	if (report.is_valid()) {
		auto iinfo = report->get_import_info();
		if (iinfo.is_valid()) {
			set_tex_params(iinfo, tex, img, iinfo->get_ver_major(), TEXTURE_2D);
		}
	}
	image_format = Image::get_format_name(img->get_format());
	err = save_image(dest_path, img, lossy);
	if (err == ERR_UNAVAILABLE) {
		return err;
	}
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to save image " + dest_path + " from texture " + p_path);
	print_verbose("Converted " + p_path + " to " + dest_path);
	return OK;
}

Error TextureExporter::_convert_atex(const String &p_path, const String &dest_path, bool lossy, String &image_format, Ref<ExportReport> report) {
	Error err;
	String dst_dir = dest_path.get_base_dir();
	Ref<Texture2D> loaded_tex = ResourceCompatLoader::custom_load(p_path, "", ResourceInfo::GLTF_LOAD, &err, false, ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
	// deprecated format
	if (err == ERR_UNAVAILABLE) {
		// TODO: Not reporting here because we can't get the deprecated format type yet,
		// implement functionality to pass it back
		image_format = "Unknown deprecated image format";
		return err;
	}
	ERR_FAIL_COND_V_MSG(err != OK || loaded_tex.is_null(), err, "Failed to load texture " + p_path);
	Ref<AtlasTexture> atex = loaded_tex;
	if (atex.is_null()) {
		// this is not an AtlasTexture, return TextureExporter::_convert_tex
		return _convert_tex(p_path, dest_path, lossy, image_format, report);
	}
	Ref<Texture2D> tex = atex->get_atlas();
	ERR_FAIL_COND_V_MSG(tex.is_null(), ERR_PARSE_ERROR, "Failed to load atlas texture " + p_path);
	Ref<Image> img = tex->get_image();

	ERR_FAIL_COND_V_MSG(img.is_null(), ERR_PARSE_ERROR, "Failed to load image for texture " + p_path);
	ERR_FAIL_COND_V_MSG(img->is_empty(), ERR_FILE_EOF, "Image data is empty for texture " + p_path + ", not saving");
	image_format = Image::get_format_name(img->get_format());

	if (img->is_compressed() && img->has_mipmaps()) {
		img->clear_mipmaps();
	}

	// resize it according to the properties of the atlas
	GDRE_ERR_DECOMPRESS_OR_FAIL(img);
	if (img->get_format() != Image::FORMAT_RGBA8) {
		img->convert(Image::FORMAT_RGBA8);
	}

	auto margin = atex->get_margin();
	auto region = atex->get_region();

	// now we have to add the margin padding
	Ref<Image> new_img = Image::create_empty(atex->get_width(), atex->get_height(), false, img->get_format());
	new_img->blit_rect(img, region, Point2i(margin.position.x, margin.position.y));
	err = save_image(dest_path, new_img, lossy);
	if (err == ERR_UNAVAILABLE) {
		return err;
	}
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to save image " + dest_path + " from texture " + p_path);

	print_verbose("Converted " + p_path + " to " + dest_path);
	return OK;
}

Error TextureExporter::export_file(const String &out_path, const String &res_path) {
	Error err;
	auto res_info = ResourceCompatLoader::get_resource_info(res_path, "", &err);
	if (res_info.is_null() || !handles_import("", res_info->type)) {
		return ERR_FILE_UNRECOGNIZED;
	}
	if (res_info->type == "BitMap") {
		return _convert_bitmap(res_path, out_path, false, nullptr);
	}
	String fmt_name;
	if (res_info->type == "AtlasTexture") {
		return _convert_atex(res_path, out_path, false, fmt_name);
	}
	return _convert_tex(res_path, out_path, false, fmt_name);
}

Error preprocess_images(
		String p_path,
		String dest_path,
		int num_images_w,
		int num_images_h,
		bool lossy,
		Vector<Ref<Image>> &images,
		bool &had_mipmaps,
		bool &detected_alpha,
		bool ignore_dimensions = false) {
	ERR_FAIL_COND_V_MSG(images.size() == 0, ERR_PARSE_ERROR, "No images to concat");
	int layer_count = num_images_w * num_images_h;
	int width = images[0]->get_width();
	int height = images[0]->get_height();

	auto format = images[0]->get_format();
	auto decompressed_fmt = format;

	Ref<Image> ref_img;
	if (layer_count > 0) {
		ref_img = images[0];
		if (!ref_img.is_null()) {
			// dupe to avoid modifying the original
			if (ref_img->is_compressed()) {
				Ref<Image> dupe = ref_img->duplicate();
				GDRE_ERR_DECOMPRESS_OR_FAIL(dupe);
				decompressed_fmt = dupe->get_format();
			}
		}
	}
	if (ref_img.is_null()) {
		return ERR_PARSE_ERROR;
	}
	auto new_format = decompressed_fmt;
	had_mipmaps = layer_count != images.size();
	Vector<Vector<uint8_t>> images_data;
	bool is_hdr = decompressed_fmt >= Image::FORMAT_RF && decompressed_fmt <= Image::FORMAT_RGBE9995;

	Vector<Image::Format> formats;
	detected_alpha = false;
	for (int64_t i = 0; i < layer_count; i++) {
		Ref<Image> img = images[i];
		if (img.is_null()) {
			return ERR_PARSE_ERROR;
		}
		if (img->has_mipmaps()) {
			had_mipmaps = true;
			img->clear_mipmaps();
		}
		GDRE_ERR_DECOMPRESS_OR_FAIL(img);
		detected_alpha = detected_alpha || img->detect_alpha();
		if (is_hdr) {
			new_format = img->get_format();
			formats.push_back(img->get_format());
			images_data.push_back(img->get_data());
		}
		if (!ignore_dimensions) {
			ERR_FAIL_COND_V_MSG(img->get_width() != width || img->get_height() != height, ERR_PARSE_ERROR, "Image " + p_path + " has incorrect dimensions");
		}
	}

	if (!is_hdr || gdre::vector_to_hashset(formats).size() > 1) {
		if (!is_hdr) {
			if (!detected_alpha) {
				new_format = Image::FORMAT_RGB8;
			} else if (detected_alpha) {
				new_format = Image::FORMAT_RGBA8;
			}
		} else {
			new_format = gdre::get_most_popular_value(formats);
			// check if we've detected alpha and if this format supports it
			const bool supports_alpha = new_format == Image::FORMAT_RGBA8 || new_format == Image::FORMAT_RGBA4444 || new_format == Image::FORMAT_RGBAH || new_format == Image::FORMAT_RGBAF;
			if (detected_alpha && !supports_alpha) {
				new_format = Image::FORMAT_RGBA8;
			}
		}
		images_data.clear();
		for (int i = 0; i < layer_count; i++) {
			Ref<Image> img = images[i];
			if (img->get_format() != new_format) {
				img->convert(new_format);
			}
			images_data.push_back(img->get_data());
		}
	}
	return OK;
}

Error save_image_with_mipmaps(const String &dest_path, const Vector<Ref<Image>> &images, int num_images_w, int num_images_h, bool lossy, bool had_mipmaps, int override_width = -1, int override_height = -1) {
	auto new_format = images[0]->get_format();
	int pixel_size = Image::get_format_pixel_size(new_format);
	Vector<Vector<uint8_t>> images_data;
	int max_height = 0;
	for (int i = 0; i < images.size(); i++) {
		ERR_FAIL_COND_V_MSG(images[i].is_null(), ERR_PARSE_ERROR, "Image " + dest_path.get_file() + " is null");
		images_data.push_back(images[i]->get_data());
		max_height = MAX(max_height, images[i]->get_height());
	}

	Vector<uint8_t> new_image_data;
	size_t new_width = override_width != -1 ? override_width : images[0]->get_width() * num_images_w;
	size_t new_height = override_height != -1 ? override_height : images[0]->get_height() * num_images_h;
	size_t new_data_size = Image::get_image_data_size(new_width, new_height, new_format, false);
	new_image_data.resize(new_data_size);
	size_t current_offset = 0;
	for (int row_idx = 0; row_idx < num_images_h; row_idx++) {
		for (int i = 0; i < max_height; i++) {
			for (int img_idx = row_idx * num_images_w; img_idx < (row_idx + 1) * num_images_w; img_idx++) {
				if (images[img_idx]->get_height() <= i) {
					continue;
				}
				size_t copy_size = images[img_idx]->get_width() * pixel_size;
				// We're concatenating the images horizontally; so we have to take a width-sized slice of the image
				// and copy it into the new image data
				size_t start_idx = i * copy_size;
				ERR_FAIL_COND_V(images_data[img_idx].size() < start_idx + copy_size, ERR_PARSE_ERROR);
				memcpy(new_image_data.ptrw() + current_offset, images_data[img_idx].ptr() + start_idx, copy_size);
				current_offset += copy_size;
			}
		}
	}
	DEV_ASSERT(Image::get_image_data_size(new_width, new_height, new_format, false) == new_image_data.size());
	Ref<Image> img = Image::create_from_data(new_width, new_height, false, new_format, new_image_data);
	ERR_FAIL_COND_V_MSG(img.is_null(), ERR_PARSE_ERROR, "Failed to create image for texture " + dest_path.get_file());
	if (had_mipmaps && dest_format_supports_mipmaps(dest_path.get_extension().to_lower())) {
		img->generate_mipmaps();
		DEV_ASSERT(Image::get_image_data_size(new_width, new_height, new_format, true) == img->get_data_size());
	}
	Error err = TextureExporter::save_image(dest_path, img, lossy);
	if (err == ERR_UNAVAILABLE) {
		return err;
	}
	return err;
}

Error TextureExporter::_convert_3d(const String &p_path, const String &dest_path, bool lossy, String &image_format, Ref<ExportReport> report) {
	Error err;
	String dst_dir = dest_path.get_base_dir();
	Ref<Texture3D> tex;
	tex = ResourceCompatLoader::non_global_load(p_path, "", &err);
	Ref<ImportInfo> iinfo;
	if (report.is_valid()) {
		iinfo = report->get_import_info();
	}

	// deprecated format
	if (err == ERR_UNAVAILABLE) {
		image_format = "Unknown deprecated image format";
		print_line("Did not convert deprecated Texture resource " + p_path);
		return err;
	}
	ERR_FAIL_COND_V_MSG(err != OK || tex.is_null(), err, "Failed to load texture " + p_path);
	ERR_FAIL_COND_V_MSG(tex->get_depth() <= 0, ERR_PARSE_ERROR, "Texture " + p_path + " has no layers");

	auto layer_count = tex->get_depth();
	Vector<Ref<Image>> images = tex->get_data();
	Ref<Image> ref_img = images[0]->duplicate();
	ERR_FAIL_COND_V_MSG(images.size() == 0, ERR_PARSE_ERROR, "No images to concat");

	int64_t num_images_w = -1;
	int64_t num_images_h = -1;
	Dictionary params;
	if (iinfo.is_valid()) {
		params = iinfo->get_params();
	}
	bool had_valid_params = false;
	if (iinfo.is_valid()) {
		num_images_w = params.get("slices/horizontal", -1);
		num_images_h = params.get("slices/vertical", -1);
		had_valid_params = num_images_w != -1 && num_images_h != -1;
	}
	if (!had_valid_params) {
		if (layer_count == 64) {
			num_images_w = 8;
			num_images_h = 8;
		} else if (layer_count == 16) {
			num_images_w = 4;
			num_images_h = 4;
		} else if (layer_count == 4) {
			num_images_w = 2;
			num_images_h = 2;
		} else {
			num_images_w = layer_count;
			num_images_h = 1;
		}
	}
	bool had_mipmaps = layer_count != images.size();
	bool detected_alpha = false;
	err = preprocess_images(p_path, dest_path, num_images_w, num_images_h, lossy, images, had_mipmaps, detected_alpha);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to preprocess images for texture " + p_path);
	err = save_image_with_mipmaps(dest_path, images, num_images_w, num_images_h, lossy, had_mipmaps);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to save image " + dest_path + " from texture " + p_path);
	set_tex_params(iinfo, tex, ref_img, iinfo->get_ver_major(), TEXTURE_3D);

	if (!had_valid_params && iinfo.is_valid()) {
		iinfo->set_param("slices/horizontal", num_images_w);
		iinfo->set_param("slices/vertical", num_images_h);
		iinfo->set_param("mipmaps/generate", had_mipmaps);
	}
	print_verbose("Converted " + p_path + " to " + dest_path);
	return OK;
}

enum CubemapFormat {
	CUBEMAP_FORMAT_1X6,
	CUBEMAP_FORMAT_2X3,
	CUBEMAP_FORMAT_3X2,
	CUBEMAP_FORMAT_6X1,
};

Ref<Image> crop_transparent(const Ref<Image> &img) {
	ERR_FAIL_COND_V(img.is_null(), img);
	int width = img->get_width();
	int height = img->get_height();

	// check if it's width-wise or height-wise based on the ratio of the width and height
	bool is_horizontal = width > height;
	int64_t num_parts = (is_horizontal ? width / height : height / width);

	int new_width = is_horizontal ? width / num_parts : width;
	int new_height = is_horizontal ? height : height / num_parts;

	// now we need to find the first non-transparent pixel in the image
	int min_x = 0;
	int width_region_start = 0;
	int min_y = 0;
	int height_region_start = 0;

	// first, check to see if the image is entirely transparent
	bool is_entirely_transparent = true;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (img->get_pixel(x, y).a > 0.0) {
				is_entirely_transparent = false;
			}
		}
	}
	if (is_entirely_transparent) {
		return Ref<Image>();
	}

	if (is_horizontal) {
		// the transparent pixels start at either at 0,0, new_width,0, or width - new_width,0
		if (img->get_pixel(0, 0).a == 0.0) {
			min_x = 0;
			width_region_start = new_width * (num_parts - 1);
		} else if (img->get_pixel(new_width, 0).a == 0.0) {
			min_x = new_width;
			width_region_start = 0;
		} else if (img->get_pixel(width - new_width, 0).a == 0.0) {
			min_x = width - new_width;
			width_region_start = width - (new_width * (num_parts - 1));
		}
		min_y = 0;
		height_region_start = 0;
	} else {
		// the transparent pixels start at either at 0,0, 0,new_height, or 0,height - new_height
		if (img->get_pixel(0, 0).a == 0.0) {
			min_y = 0;
			height_region_start = new_height * (num_parts - 1);
		} else if (img->get_pixel(0, new_height).a == 0.0) {
			min_y = new_height;
			height_region_start = 0;
		} else if (img->get_pixel(0, height - new_height).a == 0.0) {
			min_y = height - new_height;
			height_region_start = height - (new_height * (num_parts - 1));
		}
		min_x = 0;
		width_region_start = 0;
	}

	return img->get_region(Rect2i(width_region_start, height_region_start, new_width, new_height));
	;
}

Vector<Ref<Image>> fix_cross_cubemaps(const Vector<Ref<Image>> &images, int width, int height, int layer_count, bool detected_alpha) {
	// here is where we fix the "cross" style of cubemaps that got imported all funky
	// check if the images have the same width and height
	Vector<Ref<Image>> fixed_images;
	bool is_horizontal = width > height;
	int64_t num_parts = is_horizontal ? width / height : height / width;
	if (width != height) {
		if (detected_alpha) {
			// we need to fix the images
			for (int i = 0; i < layer_count; i++) {
				Ref<Image> img = images[i];
				if (img->detect_alpha()) {
					Ref<Image> cropped = crop_transparent(img);
					size_t new_width;
					size_t new_height;
					if (!cropped.is_null()) {
						new_width = cropped->get_width();
						new_height = cropped->get_height();
						fixed_images.push_back(cropped);
					} else {
						new_width = 0;
						new_height = 0;
					}
				} else {
					// otherwise, divide it into parts based on the ratio of the width and height
					for (int j = 0; j < num_parts; j++) {
						Rect2i rect;
						if (is_horizontal) {
							rect.position.x = j * width / num_parts;
							rect.size.width = width / num_parts;
							rect.position.y = 0;
							rect.size.height = height;
						} else {
							rect.position.x = 0;
							rect.size.width = width;
							rect.position.y = j * height / num_parts;
							rect.size.height = height / num_parts;
						}
						Ref<Image> part = img->get_region(rect);
						fixed_images.push_back(part);
					}
				}
			}
		}
	}
#if 0
	for (int i = 0; i < fixed_images.size(); i++) {
		Ref<Image> img = fixed_images[i];
		if (img.is_null()) {
			continue;
		}
		auto new_dest = dest_path.get_basename() + "_cropped_" + String::num_int64(i) + "." + dest_path.get_extension();
		Error err = TextureExporter::save_image(new_dest, img, lossy);
	}
#endif
	if (fixed_images.size() > 0) {
		Vector<Ref<Image>> images;
		images.resize(6);
		// X+, X-, Y+, Y-, Z+, Z-
		// this is upside down;
		images.write[0] = fixed_images[3];
		images.write[1] = fixed_images[1];
		images.write[2] = fixed_images[0];
		images.write[3] = fixed_images[5];
		images.write[4] = fixed_images[2];
		images.write[5] = fixed_images[4];

		// arrangement = is_horizontal ? CUBEMAP_FORMAT_6X1 : CUBEMAP_FORMAT_1X6;
		// num_images_w = is_horizontal ? 6 : 1;
		// num_images_h = is_horizontal ? 1 : 6;
		return images;
	} else {
		return {};
	}
}

Error TextureExporter::_convert_layered_2d(const String &p_path, const String &dest_path, bool lossy, String &image_format, Ref<ExportReport> report) {
	Error err;
	String dst_dir = dest_path.get_base_dir();
	Ref<TextureLayered> tex;
	tex = ResourceCompatLoader::non_global_load(p_path, "", &err);
	Ref<ImportInfo> iinfo;
	if (report.is_valid()) {
		iinfo = report->get_import_info();
	}

	if (err == ERR_UNAVAILABLE) {
		image_format = "Unknown deprecated image format";
		print_line("Did not convert deprecated Texture resource " + p_path);
		return err;
	}
	ERR_FAIL_COND_V_MSG(err != OK || tex.is_null(), err, "Failed to load texture " + p_path);

	auto layer_count = tex->get_layers();
	if (layer_count == 0) {
		return ERR_PARSE_ERROR;
	}
	Vector<Ref<Image>> images;
	Ref<Image> ref_img = tex->get_layer_data(0)->duplicate();
	for (int i = 0; i < layer_count; i++) {
		images.push_back(tex->get_layer_data(i));
	}
	ERR_FAIL_COND_V_MSG(images.size() == 0, ERR_PARSE_ERROR, "No images to concat");
#if 0
	for (int i = 0; i < layer_count; i++) {
		Ref<Image> img = images[i];
		auto new_dest = dest_path.get_basename() + "_" + String::num_int64(i) + "." + dest_path.get_extension();
		Error err = TextureExporter::save_image(new_dest, img, lossy);
	}
#endif

	int64_t num_images_w = -1;
	int64_t num_images_h = -1;
	int64_t arrangement = -1;
	int64_t layout = -1;
	int64_t amount = -1;
	int64_t override_width = -1;
	int64_t override_height = -1;
	auto mode = tex->get_layered_type();
	Dictionary params;
	if (iinfo.is_valid()) {
		params = iinfo->get_params();
	}
	// get the resource info
	Ref<ResourceInfo> res_info = ResourceInfo::get_info_from_resource(tex);
	bool had_valid_params = false;
	bool ignore_dimensions = res_info.is_valid() && res_info->get_ver_major() <= 2;

	if (mode == TextureLayered::LAYERED_TYPE_2D_ARRAY) {
		// get the square root of the number of layers; if it's a whole number, then we have a square
		if (iinfo.is_valid()) {
			num_images_w = params.get("slices/horizontal", -1);
			num_images_h = params.get("slices/vertical", -1);
			had_valid_params = num_images_w != -1 && num_images_h != -1;
		}
		if (res_info.is_valid() && res_info->get_type() == "LargeTexture") {
			Vector<Vector2> offsets = res_info->get_extra().get("offsets", Vector<Vector2>());
			Vector2 whole_size = res_info->get_extra().get("whole_size", Vector2(-1, -1));
			override_width = whole_size.x;
			override_height = whole_size.y;
			// get the number of unique individual x and y values
			HashSet<int64_t> unique_x;
			HashSet<int64_t> unique_y;
			for (int i = 0; i < offsets.size(); i++) {
				unique_x.insert(offsets[i].x);
				unique_y.insert(offsets[i].y);
			}
			num_images_w = unique_x.size();
			num_images_h = unique_y.size();
			had_valid_params = true;
		} else if (!had_valid_params) {
			if (layer_count == 64) {
				num_images_w = 8;
				num_images_h = 8;
			} else if (layer_count == 16) {
				num_images_w = 4;
				num_images_h = 4;
			} else if (layer_count == 4) {
				num_images_w = 2;
				num_images_h = 2;
			} else {
				num_images_w = tex->get_layers();
				num_images_h = 1;
			}
		}
	} else if (mode == TextureLayered::LAYERED_TYPE_CUBEMAP || mode == TextureLayered::LAYERED_TYPE_CUBEMAP_ARRAY) {
		if (iinfo.is_valid()) {
			arrangement = params.get("slices/arrangement", -1);
			if (arrangement != -1 && mode == TextureLayered::LAYERED_TYPE_CUBEMAP) {
				had_valid_params = true;
			} else if (arrangement != -1 && mode == TextureLayered::LAYERED_TYPE_CUBEMAP_ARRAY) {
				layout = params.get("slices/layout", -1);
				amount = params.get("slices/amount", -1);
				if (layout != -1 && amount != -1) {
					had_valid_params = true;
				}
			}
		}
		if (!had_valid_params) {
			arrangement = 1;
			layout = 1;
			amount = layer_count / 6;
		}
		if (arrangement == CUBEMAP_FORMAT_1X6) {
			num_images_w = 1;
			num_images_h = 6;
		} else if (arrangement == CUBEMAP_FORMAT_2X3) {
			num_images_w = 2;
			num_images_h = 3;
		} else if (arrangement == CUBEMAP_FORMAT_3X2) {
			num_images_w = 3;
			num_images_h = 2;
		} else if (arrangement == CUBEMAP_FORMAT_6X1) {
			num_images_w = 6;
			num_images_h = 1;
		}
		if (mode == TextureLayered::LAYERED_TYPE_CUBEMAP_ARRAY) {
			if (layout == 0) {
				num_images_w *= amount;
			} else if (layout == 1) {
				num_images_h *= amount;
			}
		}
	}
	bool had_mipmaps = layer_count != images.size();
	bool detected_alpha = false;
	err = preprocess_images(p_path, dest_path, num_images_w, num_images_h, lossy, images, had_mipmaps, detected_alpha, ignore_dimensions);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to preprocess images for texture " + p_path);
	int width = tex->get_width();
	int height = tex->get_height();
#if 0 // This was an attempt at fixing incorrectly imported cubemaps; if it was incorrectly imported by the original author, we should just leave it be.
	if (mode == TextureLayered::LAYERED_TYPE_CUBEMAP || mode == TextureLayered::LAYERED_TYPE_CUBEMAP_ARRAY) {
		Vector<Ref<Image>> fixed_images = fix_cross_cubemaps(images, width, height, layer_count, detected_alpha);
	}
#endif
	err = save_image_with_mipmaps(dest_path, images, num_images_w, num_images_h, lossy, had_mipmaps, override_width, override_height);
	image_format = Image::get_format_name(images[0]->get_format());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to concat images for texture " + p_path);
	set_tex_params(iinfo, tex, ref_img, iinfo->get_ver_major(), TEXTURE_LAYERED);

	if (!had_valid_params && iinfo.is_valid()) {
		if (mode == TextureLayered::LAYERED_TYPE_2D_ARRAY) {
			iinfo->set_param("slices/horizontal", num_images_w);
			iinfo->set_param("slices/vertical", num_images_h);
		} else if (mode == TextureLayered::LAYERED_TYPE_CUBEMAP) {
			// 1x6
			iinfo->set_param("slices/arrangement", arrangement);
		} else if (mode == TextureLayered::LAYERED_TYPE_CUBEMAP_ARRAY) {
			// 1x6
			iinfo->set_param("slices/arrangement", arrangement);
			iinfo->set_param("slices/layout", layout);
			iinfo->set_param("slices/amount", layer_count / 6);
		}
		iinfo->set_param("mipmaps/generate", had_mipmaps);
	}
	print_verbose("Converted " + p_path + " to " + dest_path);
	return OK;
}

Ref<ExportReport> TextureExporter::export_resource(const String &output_dir, Ref<ImportInfo> iinfo) {
	String path = iinfo->get_path();
	String source = iinfo->get_source_file();
	bool lossy = false;
	int ver_major = iinfo->get_ver_major();
	int ver_minor = iinfo->get_ver_minor();
	Ref<ExportReport> report = memnew(ExportReport(iinfo));

	// Sonic Colors Unlimited specific hack: We don't support atsc and nx-low formats, so we need to set the path to something else
	if (ver_major == 3 && ver_minor == 1) {
		String format_type = path.get_basename().get_extension();
		Vector<String> banned_types = { "atsc", "nx-low", "atsc-low" };
		if (banned_types.has(format_type)) {
			bool found = false;
			Vector<String> dest_files = iinfo->get_dest_files();
			if (dest_files.size() > 0) {
				String new_path = path.get_basename().get_basename() + ".s3tc" + path.get_extension();
				if (FileAccess::exists(new_path)) {
					path = new_path;
					found = true;
				}
				if (!found) {
					for (auto &dest : dest_files) {
						String fmt = dest.get_basename().get_extension();
						if (!banned_types.has(format_type) && FileAccess::exists(new_path)) {
							path = dest;
							found = true;
							break;
						}
					}
				}
			}
			if (!found) {
				report->set_error(ERR_UNAVAILABLE);
				report->set_message("Cannot convert custom SCU texture format");
				report->set_unsupported_format_type(format_type);
			}
		}
	}
	if (!FileAccess::exists(path)) {
		path = "";
		for (auto &dest : iinfo->get_dest_files()) {
			if (FileAccess::exists(dest)) {
				path = dest;
				break;
			}
		}
		if (path.is_empty()) {
			report->set_error(ERR_FILE_NOT_FOUND);
			report->set_message("No existing textures found for this import");
			report->append_message_detail({ "Possibles:" });
			report->append_message_detail(iinfo->get_dest_files());
			return report;
		}
	}
	// for Godot 2.x resources, we can easily rewrite the metadata to point to a renamed file with a different extension,
	// but this isn't the case for 3.x and greater, so we have to save in the original (lossy) format.
	String source_ext = source.get_extension().to_lower();
	if (source_ext != "png" || ver_major == 2) {
		if (ver_major > 2) {
			if ((source_ext == "jpg" || source_ext == "jpeg")) {
				lossy = true;
				report->set_loss_type(ImportInfo::STORED_LOSSY);
			} else if (source_ext == "webp") {
				// if the engine <3.4, it can't handle lossless encoded WEBPs
				if (ver_major < 4 && !(ver_major == 3 && ver_minor >= 4)) {
					lossy = true;
				}
				report->set_loss_type(ImportInfo::STORED_LOSSY);
			} else if (source_ext == "tga") {
				lossy = false;
			} else if (source_ext == "svg") {
				lossy = true;
				report->set_loss_type(ImportInfo::STORED_LOSSY);
			} else if (source_ext == "dds") {
				lossy = false;
			} else if (source_ext == "exr") {
				lossy = false;
			} else if (source_ext == "bmp") {
				lossy = false;
			} else {
				iinfo->set_export_dest(iinfo->get_export_dest().get_basename() + ".png");
				// If this is version 3-4, we need to rewrite the import metadata to point to the new resource name
				// save it under .assets, which won't be picked up for import by the godot editor
				if (false) {
					// disable this for now
					// iinfo->set_source_file(iinfo->get_export_dest());
				} else {
					if (!iinfo->get_export_dest().replace("res://", "").begins_with(".assets")) {
						String prefix = ".assets";
						if (iinfo->get_export_dest().begins_with("res://")) {
							prefix = "res://.assets";
						}
						iinfo->set_export_dest(prefix.path_join(iinfo->get_export_dest().replace("res://", "")));
					}
				}
			}
		} else { //version 2
			iinfo->set_export_dest(iinfo->get_export_dest().get_basename() + ".png");
		}
	}
	report->set_new_source_path(iinfo->get_export_dest());

	Error err = OK;
	String img_format = "bitmap";
	String importer = iinfo->get_importer();
	String dest_path = output_dir.path_join(iinfo->get_export_dest().replace("res://", ""));
	if (importer == "image") {
		ResourceFormatLoaderImage rli;
		Ref<Image> img = rli.load(path, "", &err, false, nullptr, ResourceFormatLoader::CACHE_MODE_IGNORE);
		if (!err && !img.is_null()) {
			img_format = Image::get_format_name(img->get_format());
			err = save_image(dest_path, img, lossy);
		}
	} else if (importer == "texture_atlas") {
		if (ver_major <= 2 && (iinfo->get_type() == "ImageTexture" || iinfo->get_additional_sources().size() > 0)) {
			// this is the sprite sheet for the texture atlas; we can't save it to the original sources, so we save it to another file
			auto new_dest = dest_path.get_base_dir().path_join(iinfo->get_path().get_file());
			new_dest = new_dest.get_basename() + ".ATLAS_SHEET." + dest_path.get_extension();
			iinfo->set_export_dest(new_dest);
			dest_path = new_dest;
			err = _convert_tex(path, dest_path, lossy, img_format, report);
			// Don't rewrite the metadata for this
			report->set_rewrote_metadata(ExportReport::NOT_IMPORTABLE);
		} else {
			err = _convert_atex(path, dest_path, lossy, img_format);
		}
	} else if (importer == "bitmap") {
		err = _convert_bitmap(path, dest_path, lossy);
	} else if (importer == "texture_large" || importer == "2d_array_texture" || importer == "cubemap_array_texture" || importer == "cubemap_texture" || importer == "texture_array") {
		err = _convert_layered_2d(path, dest_path, lossy, img_format, report);
	} else if (importer == "3d_texture" || importer == "texture_3d") {
		err = _convert_3d(path, dest_path, lossy, img_format, report);
	} else if (importer == "texture" || importer == "texture_2d") {
		err = _convert_tex(path, dest_path, lossy, img_format, report);
	} else {
		report->set_error(ERR_UNAVAILABLE);
		report->set_message("Unsupported texture importer: " + importer);
		return report;
	}
	report->set_error(err);
	if (err == ERR_UNAVAILABLE) {
		report->set_unsupported_format_type(img_format);
		report->set_message("Decompression not implemented yet for texture format " + img_format);
		// Already reported in export functions above
		return report;
	} else if (err) {
		return report;
	}
	report->set_saved_path(dest_path);
	// If lossy, also convert it as a png
	bool saving_lossless_copy = false; // TODO: add config option
	if (saving_lossless_copy && lossy && importer == "texture") {
		String dest = iinfo->get_export_dest().get_basename() + ".png";
		if (!dest.replace("res://", "").begins_with(".assets")) {
			String prefix = ".assets";
			if (dest.begins_with("res://")) {
				prefix = "res://.assets";
			}
			dest = prefix.path_join(dest.replace("res://", ""));
		}
		iinfo->set_export_lossless_copy(dest);
		dest_path = output_dir.path_join(dest.replace("res://", ""));
		err = _convert_tex(path, dest_path, false, img_format, nullptr);
		ERR_FAIL_COND_V(err != OK, report);
	}

	return report;
}

void TextureExporter::get_handled_types(List<String> *out) const {
	out->push_back("Texture");
	out->push_back("Texture2D");
	out->push_back("ImageTexture");
	out->push_back("StreamTexture");
	out->push_back("CompressedTexture2D");
	out->push_back("BitMap");
	out->push_back("LargeTexture");
	out->push_back("AtlasTexture");
	out->push_back("StreamTexture");
	out->push_back("StreamTexture3D");
	out->push_back("StreamTextureArray");
	out->push_back("CompressedTexture2D");
	out->push_back("CompressedTexture3D");
	out->push_back("CompressedTextureLayered");
	out->push_back("CompressedTexture2DArray");
	out->push_back("CompressedCubemap");
	out->push_back("CompressedCubemapArray");
	out->push_back("TextureArray");
}

void TextureExporter::get_handled_importers(List<String> *out) const {
	out->push_back("texture");
	out->push_back("texture_2d");
	out->push_back("bitmap");
	out->push_back("image");
	out->push_back("texture_atlas");
	out->push_back("texture_large");
	out->push_back("texture_array");
	out->push_back("cubemap_texture");
	out->push_back("2d_array_texture");
	out->push_back("cubemap_array_texture");
	out->push_back("texture_3d");
	out->push_back("3d_texture");
}

String TextureExporter::get_name() const {
	return "Texture";
}
