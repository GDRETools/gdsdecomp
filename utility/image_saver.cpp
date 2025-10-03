#include "image_saver.h"

#include "compat/file_access_encrypted_v3.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"
#include "external/tga/tga.h"
#include "modules/tinyexr/image_saver_tinyexr.h"
#include "utility/common.h"
#include "vtracer/gifski.h"
#include "vtracer/vtracer.h"

bool ImageSaver::dest_format_supports_mipmaps(const String &ext) {
	return ext == "dds" || ext == "exr";
}

const Vector<String> ImageSaver::supported_extensions = { "tga", "svg", "bmp", "gif", "hdr", "exr", "dds", "png", "jpg", "jpeg", "webp" };

Vector<String> ImageSaver::get_supported_extensions() {
	return supported_extensions;
}

bool ImageSaver::is_supported_extension(const String &p_ext) {
	return supported_extensions.has(p_ext.to_lower());
}

bool is_supported_format_for_exr(Image::Format p_format) {
	switch (p_format) {
		case Image::FORMAT_RF:
		case Image::FORMAT_RGF:
		case Image::FORMAT_RGBF:
		case Image::FORMAT_RGBAF:
		case Image::FORMAT_RH:
		case Image::FORMAT_RGH:
		case Image::FORMAT_RGBH:
		case Image::FORMAT_RGBAH:
		case Image::FORMAT_R8:
		case Image::FORMAT_RG8:
		case Image::FORMAT_RGB8:
		case Image::FORMAT_RGBA8:
			return true;
		default:
			return false;
	}
}

Error ImageSaver::save_image(const String &dest_path, const Ref<Image> &img, bool lossy, float quality) {
	ERR_FAIL_COND_V_MSG(img->is_empty(), ERR_FILE_EOF, "Image data is empty for texture " + dest_path + ", not saving");
	Error err = gdre::ensure_dir(dest_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to create dirs for " + dest_path);
	String dest_ext = dest_path.get_extension().to_lower();
	if (!dest_format_supports_mipmaps(dest_ext) && img->is_compressed() && img->has_mipmaps()) {
		img->clear_mipmaps();
	}
	GDRE_ERR_DECOMPRESS_OR_FAIL(img);
	if (dest_ext == "jpg" || dest_ext == "jpeg") {
		err = img->save_jpg(dest_path, quality);
	} else if (dest_ext == "webp") {
		err = img->save_webp(dest_path, lossy, quality);
	} else if (dest_ext == "png") {
		err = img->save_png(dest_path);
	} else if (dest_ext == "tga") {
		err = ImageSaver::save_image_as_tga(dest_path, img);
	} else if (dest_ext == "svg") {
		err = ImageSaver::save_image_as_svg(dest_path, img);
	} else if (dest_ext == "dds") {
		err = img->save_dds(dest_path);
	} else if (dest_ext == "exr") {
		if (!is_supported_format_for_exr(img->get_format())) {
			if (img->get_format() == Image::FORMAT_RGBE9995) {
				img->convert(Image::FORMAT_RGBAF);
			} else {
				img->convert(Image::FORMAT_RGBA8);
			}
		}
		if (!is_supported_format_for_exr(img->get_format())) {
			return ERR_UNAVAILABLE;
		}
		err = img->save_exr(dest_path);
	} else if (dest_ext == "bmp") {
		err = ImageSaver::save_image_as_bmp(dest_path, img);
	} else if (dest_ext == "gif") {
		err = ImageSaver::save_image_as_gif(dest_path, img);
	} else if (dest_ext == "hdr") {
		err = ImageSaver::save_image_as_hdr(dest_path, img);
	} else {
		ERR_FAIL_V_MSG(ERR_FILE_BAD_PATH, "Invalid file name: " + dest_path);
	}
	return err;
}

Error ImageSaver::decompress_image(const Ref<Image> &img) {
	Error err;
	if (img->is_compressed()) {
		err = img->decompress();
		if (err == ERR_UNAVAILABLE) {
			return err;
		}
		ERR_FAIL_COND_V_MSG(err != OK || img.is_null(), err, "Failed to decompress image.");
	}
	return OK;
}

class GodotFileInterface : public tga::FileInterface {
	Ref<FileAccess> m_file;

public:
	GodotFileInterface(const String &p_path, FileAccess::ModeFlags p_mode) {
		m_file = FileAccess::open(p_path, p_mode);
	}

	// Returns true if we can read/write bytes from/into the file
	virtual bool ok() const override {
		return m_file.is_valid();
	}

	// Current position in the file
	virtual size_t tell() override {
		return m_file->get_position();
	}

	// Jump to the given position in the file
	virtual void seek(size_t absPos) override {
		m_file->seek(absPos);
	}

	// Returns the next byte in the file or 0 if ok() = false
	virtual uint8_t read8() override {
		return m_file->get_8();
	}

	// Writes one byte in the file (or do nothing if ok() = false)
	virtual void write8(uint8_t value) override {
		m_file->store_8(value);
	}
};

Error ImageSaver::save_image_as_bmp(const String &p_path, const Ref<Image> &p_img) {
	// Microsoft BMP format - BGR ordering
	Ref<Image> source_image = p_img->duplicate();
	GDRE_ERR_DECOMPRESS_OR_FAIL(source_image);

	int width = source_image->get_width();
	int height = source_image->get_height();

	// Convert to RGBA8 if needed
	bool has_alpha = source_image->detect_alpha();
	if (source_image->get_format() != Image::FORMAT_RGBA8 && source_image->get_format() != Image::FORMAT_RGB8) {
		if (has_alpha) {
			source_image->convert(Image::FORMAT_RGBA8);
		} else {
			source_image->convert(Image::FORMAT_RGB8);
		}
	} else if (!has_alpha && source_image->get_format() == Image::FORMAT_RGBA8) {
		source_image->convert(Image::FORMAT_RGB8);
	}
	int pixel_stride = has_alpha ? 4 : 3;

	// Determine BMP format based on alpha presence
	bool use_32bit = has_alpha;
	int bytes_per_pixel = use_32bit ? 4 : 3;
	int row_size = width * bytes_per_pixel;
	int padding_size = use_32bit ? 0 : (4 - (row_size % 4)) % 4; // 32-bit has no padding
	int padded_row_size = row_size + padding_size;

	// BMP file header (14 bytes)
	uint32_t info_header_size = use_32bit ? 108 : 40; // V4 header (108 bytes) for 32-bit, V3 header (40 bytes) for 24-bit
	uint32_t file_size = 14 + info_header_size + (padded_row_size * height);
	uint32_t pixel_data_offset = 14 + info_header_size;

	// BMP info header
	uint16_t planes = 1;
	uint16_t bits_per_pixel = use_32bit ? 32 : 24;
	uint32_t compression = use_32bit ? 3 : 0; // BI_BITFIELDS for 32-bit, BI_RGB for 24-bit
	uint32_t image_size = padded_row_size * height;
	uint32_t x_pixels_per_meter = 2835; // 72 DPI
	uint32_t y_pixels_per_meter = 2835; // 72 DPI
	uint32_t colors_used = 0;
	uint32_t important_colors = 0;

	Ref<FileAccess> fa = FileAccess::open(p_path, FileAccess::WRITE);
	if (fa.is_null()) {
		return ERR_FILE_CANT_WRITE;
	}

	// Write file header
	fa->store_16(0x4D42); // "BM" signature
	fa->store_32(file_size);
	fa->store_16(0); // Reserved
	fa->store_16(0); // Reserved
	fa->store_32(pixel_data_offset);

	// Write info header
	fa->store_32(info_header_size);
	fa->store_32(width);
	fa->store_32(height);
	fa->store_16(planes);
	fa->store_16(bits_per_pixel);
	fa->store_32(compression);
	fa->store_32(image_size);
	fa->store_32(x_pixels_per_meter);
	fa->store_32(y_pixels_per_meter);
	fa->store_32(colors_used);
	fa->store_32(important_colors);

	// Write V4 header fields for 32-bit format
	if (use_32bit) {
		// Color masks (same as V3 but in V4 header)
		fa->store_32(0x00FF0000); // Blue mask
		fa->store_32(0x0000FF00); // Green mask
		fa->store_32(0x000000FF); // Red mask
		fa->store_32(0xFF000000); // Alpha mask

		// Color space type (linear RGB = 0x4C494E45)
		fa->store_32(0x4C494E45); // "LINE"

		// CIEXYZTRIPLE for endpoints (unused for linear RGB)
		for (int i = 0; i < 9; i++) {
			fa->store_32(0); // 3 endpoints × 3 coordinates × 4 bytes each
		}

		// Gamma values (unused for linear RGB)
		fa->store_32(0); // Red gamma
		fa->store_32(0); // Green gamma
		fa->store_32(0); // Blue gamma
	}

	// Write pixel data (BMP stores rows bottom-up)
	Vector<uint8_t> image_data = source_image->get_data();
	constexpr uint8_t padding[4] = { 0, 0, 0, 0 }; // Padding bytes

	for (int y = height - 1; y >= 0; y--) { // Bottom-up order
		for (int x = 0; x < width; x++) {
			int src_index = (y * width + x) * pixel_stride;
			uint8_t r = image_data[src_index + 0];
			uint8_t g = image_data[src_index + 1];
			uint8_t b = image_data[src_index + 2];

			if (use_32bit) {
				uint8_t a = image_data[src_index + 3];
				fa->store_8(b);
				fa->store_8(g);
				fa->store_8(r);
				fa->store_8(a);
			} else {
				fa->store_8(b);
				fa->store_8(g);
				fa->store_8(r);
			}
		}

		// Write row padding (only for 24-bit format)
		if (!use_32bit && padding_size > 0) {
			fa->store_buffer(padding, padding_size);
		}
	}

	fa->close();
	return OK;
}

Error ImageSaver::save_image_as_tga(const String &p_path, const Ref<Image> &p_img) {
	Vector<uint8_t> buffer;
	Ref<Image> source_image = p_img->duplicate();
	GDRE_ERR_DECOMPRESS_OR_FAIL(source_image);
	int width = source_image->get_width();
	int height = source_image->get_height();
	bool isRGB = true;
	bool isAlpha = source_image->detect_alpha();
	switch (source_image->get_format()) {
		case Image::FORMAT_L8:
			isRGB = false;
			break;
		case Image::FORMAT_LA8:
			isRGB = true;
			source_image->convert(Image::FORMAT_RGBA8);
			break;
		case Image::FORMAT_RGB8:
			// we still need to convert it to RGBA8 even if it doesn't have alpha due to the encoder requiring 4 bytes per pixel
			source_image->convert(Image::FORMAT_RGBA8);
			break;
		case Image::FORMAT_RGBA8:
			break;
		default:
			source_image->convert(Image::FORMAT_RGBA8);
			isRGB = true;
			break;
	}

	tga::Header header;
	header.idLength = 0;
	header.colormapType = 0;
	header.imageType = isRGB ? tga::ImageType::RleRgb : tga::ImageType::RleGray;
	header.colormapOrigin = 0;
	header.colormapLength = 0;
	header.colormapDepth = 0;

	header.xOrigin = 0;
	header.yOrigin = 0;
	header.width = width;
	header.height = height;
	header.bitsPerPixel = isRGB ? (isAlpha ? 32 : 24) : 8;
	header.imageDescriptor = isAlpha ? 0xf : 0; // top-left origin always
	tga::Image tga_image{};
	Vector<uint8_t> tga_data = source_image->get_data(); //isRGB ? rgba_to_bgra(source_image->get_data()) : source_image->get_data();
	tga_image.pixels = tga_data.ptrw();
	tga_image.bytesPerPixel = isRGB ? 4 : 1;
	tga_image.rowstride = width * tga_image.bytesPerPixel;
	GodotFileInterface file_interface(p_path, FileAccess::WRITE);
	if (!file_interface.ok()) {
		return ERR_FILE_CANT_WRITE;
	}
	tga::Encoder encoder(&file_interface);
	encoder.writeHeader(header);
	encoder.writeImage(header, tga_image);
	encoder.writeFooter();
	return OK;
}

Error ImageSaver::save_image_as_svg(const String &p_path, const Ref<Image> &p_img) {
	VTracerConfig config;
	vtracer_set_default_config(&config);
	// this config converts the raster image to a vector image with a box for each pixel
	// this will ensure that the image will match the original image when re-imported into Godot
	config.color_mode = V_TRACER_COLOR_MODE_COLOR;
	config.hierarchical = V_TRACER_HIERARCHICAL_STACKED;
	config.mode = V_TRACER_PATH_SIMPLIFY_MODE_NONE;
	config.filter_speckle = 0;
	config.color_precision = 8;
	config.layer_difference = 0;
	config.corner_threshold = 180;
	config.length_threshold = 4.0;
	config.splice_threshold = 45;
	config.max_iterations = 10;
	config.path_precision = 2;
	config.keying_threshold = 0.0;

	Ref<Image> img = p_img->duplicate();
	GDRE_ERR_DECOMPRESS_OR_FAIL(img);
	// check if the image is RGBA; if not, convert it to RGBA
	if (img->get_format() != Image::FORMAT_RGBA8) {
		img->convert(Image::FORMAT_RGBA8);
	}
	auto data = img->get_data();
	VTracerColorImage svg_data{
		data.ptrw(),
		(size_t)img->get_width(),
		(size_t)img->get_height(),
	};
	const char *err_str = vtracer_convert_image_memory_to_svg(&svg_data, p_path.utf8().get_data(), &config);
	String err_msg;
	if (err_str) {
		err_msg = vformat("Failed to convert image to SVG: %s", err_str);
		vtracer_free_string(err_str);
	}
	ERR_FAIL_COND_V_MSG(!err_msg.is_empty(), ERR_CANT_CREATE, err_msg);
	return OK;
}

Error ImageSaver::save_images_as_animated_gif(const String &p_path, const Vector<Ref<Image>> &p_images, const Vector<float> &frame_durations_s, int quality) {
	ERR_FAIL_COND_V_MSG(p_images.is_empty(), ERR_FILE_EOF, "No images provided for animated GIF");

	// Ensure directory exists
	Error err = gdre::ensure_dir(p_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to create dirs for " + p_path);

	// Get dimensions from first image
	Ref<Image> first_image = p_images[0];

	int width = first_image->get_width();
	int height = first_image->get_height();

	// Validate all images have same dimensions
	for (int i = 1; i < p_images.size(); i++) {
		Ref<Image> img = p_images[i];
		ERR_FAIL_COND_V_MSG(img->get_width() != width || img->get_height() != height,
				ERR_INVALID_DATA, vformat("Image %d has different dimensions (%dx%d) than first image (%dx%d)", i, img->get_width(), img->get_height(), width, height));
	}

	GifskiSettings settings;
	settings.width = width;
	settings.height = height;
	settings.quality = quality;
	settings.fast = false;
	settings.repeat = 0; // 0 means loop forever

	// Begin GIF creation
	gifski *g = gifski_new(&settings);
	if (!g) {
		return ERR_CANT_CREATE;
	}
	auto sofeff = gifski_set_file_output(g, p_path.utf8().get_data());
	if (sofeff != GIFSKI_OK) {
		gifski_finish(g);
		return ERR_CANT_CREATE;
	}

	// Add each frame

	double seconds_per_frame = 0;
	if (frame_durations_s.is_empty()) {
		seconds_per_frame = 0.1; // default to 100ms per frame
	}
	double start_time = 0;

	for (int i = 0; i < p_images.size(); i++) {
		Ref<Image> frame_image = p_images[i];
		GDRE_ERR_DECOMPRESS_OR_FAIL(frame_image);
		// Convert to RGBA8 if needed
		if (frame_image->get_format() != Image::FORMAT_RGBA8) {
			frame_image->convert(Image::FORMAT_RGBA8);
		}

		Vector<uint8_t> image_data = frame_image->get_data();
		image_data.ptrw();
		if (frame_durations_s.size() > i) {
			seconds_per_frame = frame_durations_s[i];
		} // otherwise just use the last one

		// Add frame to GIF
		auto err = gifski_add_frame_rgba(g, i, width, height, image_data.ptrw(), start_time);
		start_time += seconds_per_frame;
		if (err != GIFSKI_OK) {
			gifski_finish(g);
			return ERR_CANT_CREATE;
		}
	}

	// Finalize GIF
	auto result = gifski_finish(g);
	if (result != GIFSKI_OK) {
		return ERR_CANT_CREATE;
	}

	return OK;
}

Error ImageSaver::_save_images_as_animated_gif(const String &p_path, const TypedArray<Image> &p_images, const Vector<float> &frame_durations_s, int quality) {
	Vector<Ref<Image>> images;
	for (int i = 0; i < p_images.size(); i++) {
		images.push_back(p_images[i]);
	}
	return save_images_as_animated_gif(p_path, images, frame_durations_s, quality);
}

Error ImageSaver::save_image_as_gif(const String &p_path, const Ref<Image> &p_img) {
	// Create a vector with just one image and use the animated GIF function
	Vector<Ref<Image>> images;
	images.push_back(p_img);
	return save_images_as_animated_gif(p_path, images, { 0.1 }, 100);
}
namespace {

constexpr uint8_t get_r(uint32_t rgbe) {
	return (uint8_t)((rgbe >> 1) & 0xFF);
}
constexpr uint8_t get_g(uint32_t rgbe) {
	return (uint8_t)((rgbe >> 10) & 0xFF);
}
constexpr uint8_t get_b(uint32_t rgbe) {
	return (uint8_t)((rgbe >> 19) & 0xFF);
}
constexpr uint8_t get_e(uint32_t rgbe) {
	return (uint8_t)(((rgbe >> 27) - 15 + 128) & 0xFF);
}

Vector<uint8_t> save_hdr_buffer(const Ref<Image> &p_img) {
	ERR_FAIL_COND_V_MSG(p_img.is_null(), Vector<uint8_t>(), "Can't save invalid image as HDR.");

	Ref<Image> img = p_img->duplicate();
	if (img->is_compressed() && img->decompress() != OK) {
		ERR_FAIL_V_MSG(Vector<uint8_t>(), "Failed to decompress image.");
	}

	img->clear_mipmaps();
	if (img->get_format() != Image::FORMAT_RGBE9995) {
		img->convert(Image::FORMAT_RGBE9995);
	}

	// Create a temporary file to write the HDR data
	Vector<uint8_t> buffer;

	// Write Radiance HDR header
	buffer.append_array(String("#?RADIANCE\n").to_utf8_buffer());
	buffer.append_array(String("FORMAT=32-bit_rle_rgbe\n").to_utf8_buffer());
	buffer.append_array(String("\n").to_utf8_buffer()); // Empty line to end header

	// Write resolution string (standard orientation: -Y height +X width)
	String resolution = vformat("-Y %d +X %d\n", img->get_height(), img->get_width());
	buffer.append_array(resolution.to_utf8_buffer());

	// Get image data
	const Vector<uint8_t> &data = img->get_data();
	const uint32_t *rgbe_data = reinterpret_cast<const uint32_t *>(data.ptr());
	int width = img->get_width();
	int height = img->get_height();
	// Write scanlines
	if (width < 8 || width >= 32768) {
		// Write flat data
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				uint32_t rgbe = rgbe_data[y * width + x];
				buffer.push_back(get_r(rgbe));
				buffer.push_back(get_g(rgbe));
				buffer.push_back(get_b(rgbe));
				buffer.push_back(get_e(rgbe));
			}
		}
	} else {
		// Write RLE-encoded data
		for (int y = 0; y < height; y++) {
			// For each scanline, we need to separate the RGBE components
			Vector<uint8_t> scanline_r, scanline_g, scanline_b, scanline_e;
			scanline_r.resize(width);
			scanline_g.resize(width);
			scanline_b.resize(width);
			scanline_e.resize(width);

			// Extract RGBE components from RGBE9995 format and convert to Radiance HDR format
			for (int x = 0; x < width; x++) {
				uint32_t rgbe = rgbe_data[y * width + x];
				scanline_r.write[x] = get_r(rgbe);
				scanline_g.write[x] = get_g(rgbe);
				scanline_b.write[x] = get_b(rgbe);
				scanline_e.write[x] = get_e(rgbe);
			}

			// Write RLE header for this scanline
			buffer.push_back(2); // Magic number for new RLE format
			buffer.push_back(2);
			buffer.push_back((width >> 8) & 0xFF); // High byte of width
			buffer.push_back(width & 0xFF); // Low byte of width

			// Write each component with RLE encoding
			const uint8_t *components[4] = { scanline_r.ptr(), scanline_g.ptr(), scanline_b.ptr(), scanline_e.ptr() };

			for (int c = 0; c < 4; c++) {
				const uint8_t *comp_data = components[c];
				int i = 0;

				while (i < width) {
					// Find run length
					// int run_start = i;
					uint8_t run_value = comp_data[i];
					int run_length = 1;

					while (i + run_length < width && run_length < 127 && comp_data[i + run_length] == run_value) {
						run_length++;
					}

					if (run_length >= 4) {
						// Write run
						buffer.push_back(128 + run_length);
						buffer.push_back(run_value);
						i += run_length;
					} else {
						// Write literal values
						int literal_start = i;
						int literal_length = 0;

						while (i < width && literal_length < 127) {
							if (i + 3 < width &&
									comp_data[i] == comp_data[i + 1] &&
									comp_data[i] == comp_data[i + 2] &&
									comp_data[i] == comp_data[i + 3]) {
								// Found a potential run, stop literal
								break;
							}
							literal_length++;
							i++;
						}

						buffer.push_back(literal_length);
						for (int j = 0; j < literal_length; j++) {
							buffer.push_back(comp_data[literal_start + j]);
						}
					}
				}
			}
		}
	}

	return buffer;
}
} //namespace

Error ImageSaver::save_image_as_hdr(const String &p_path, const Ref<Image> &p_img) {
	Vector<uint8_t> buffer = save_hdr_buffer(p_img);

	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_null()) {
		return ERR_CANT_CREATE;
	}

	file->store_buffer(buffer.ptr(), buffer.size());

	return OK;
}

void ImageSaver::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("decompress_image", "image"), &ImageSaver::decompress_image);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_image", "dest_path", "image", "lossy", "quality"), &ImageSaver::save_image, DEFVAL(false), DEFVAL(1.0));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_image_as_tga", "dest_path", "image"), &ImageSaver::save_image_as_tga);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_image_as_svg", "dest_path", "image"), &ImageSaver::save_image_as_svg);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_image_as_bmp", "dest_path", "image"), &ImageSaver::save_image_as_bmp);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_image_as_gif", "dest_path", "image"), &ImageSaver::save_image_as_gif);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_images_as_animated_gif", "dest_path", "images", "frame_durations_s", "quality"), &ImageSaver::_save_images_as_animated_gif, DEFVAL(100));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_image_as_hdr", "dest_path", "image"), &ImageSaver::save_image_as_hdr);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_supported_extensions"), &ImageSaver::get_supported_extensions);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("is_supported_extension", "ext"), &ImageSaver::is_supported_extension);
}
