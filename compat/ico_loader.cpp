/**************************************************************************/
/*  ico_loader.cpp                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "ico_loader.h"

#include "core/io/file_access_memory.h"
#include "core/io/image.h"

#include "modules/bmp/image_loader_bmp.h"
#include "utility/file_access_buffer.h"

namespace {
// PNG signature: 89 50 4E 47 0D 0A 1A 0A
static const uint8_t PNG_SIGNATURE[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

// BITMAPFILEHEADER structure (14 bytes)
// Note: Embedded BMP in ICO files excludes this header, so we need to construct it
static const uint16_t BITMAP_FILE_HEADER_SIZE = 14;
static const uint16_t BITMAP_SIGNATURE = 0x4D42; // "BM"
static const uint32_t BITMAP_INFO_HEADER_MIN_SIZE = 40;
struct bmp_header_s {
	struct bmp_file_header_s {
		uint16_t bmp_signature = 0;
		uint32_t bmp_file_size = 0;
		uint32_t bmp_file_padding = 0;
		uint32_t bmp_file_offset = 0;
	} bmp_file_header;

	struct bmp_info_header_s {
		uint32_t bmp_header_size = 0;
		int32_t bmp_width = 0;
		int32_t bmp_height = 0;
		uint16_t bmp_planes = 0;
		uint16_t bmp_bit_count = 0;
		uint32_t bmp_compression = 0;
		uint32_t bmp_size_image = 0;
		int32_t bmp_pixels_per_meter_x = 0;
		int32_t bmp_pixels_per_meter_y = 0;
		uint32_t bmp_colors_used = 0;
		uint32_t bmp_important_colors = 0;
	} bmp_info_header;

	struct bmp_bitfield_s {
		uint16_t alpha_mask = 0x8000;
		uint16_t red_mask = 0x7C00;
		uint16_t green_mask = 0x03E0;
		uint16_t blue_mask = 0x001F;
		uint16_t alpha_mask_width = 1u;
		uint16_t red_mask_width = 5u;
		uint16_t green_mask_width = 5u;
		uint16_t blue_mask_width = 5u;
		uint8_t alpha_offset = 15u; // Used for bit shifting.
		uint8_t red_offset = 10u; // Used for bit shifting.
		uint8_t green_offset = 5u; // Used for bit shifting.
		//uint8_t blue_offset = 0u; // Always LSB aligned no shifting needed.
		//uint8_t alpha_max = 1u; // Always boolean or on, so no scaling needed.
		uint8_t red_max = 32u; // Used for color space scaling.
		uint8_t green_max = 32u; // Used for color space scaling.
		uint8_t blue_max = 32u; // Used for color space scaling.
	} bmp_bitfield;

	Vector<uint8_t> bmp_color_table;
};

enum bmp_compression_s {
	BI_RGB = 0x00,
	BI_RLE8 = 0x01, // compressed
	BI_RLE4 = 0x02, // compressed
	BI_BITFIELDS = 0x03,
	BI_JPEG = 0x04,
	BI_PNG = 0x05,
	BI_ALPHABITFIELDS = 0x06,
	BI_CMYK = 0x0b,
	BI_CMYKRLE8 = 0x0c, // compressed
	BI_CMYKRLE4 = 0x0d // compressed
};

static uint8_t get_mask_width(uint16_t mask) {
	// Returns number of ones in the binary value of the parameter: mask.
	// Uses a Simple pop_count.
	uint8_t c = 0u;
	for (; mask != 0u; mask &= mask - 1u) {
		c++;
	}
	return c;
}

static Error parse_after_file_header(const Vector<uint8_t> &p_data, bmp_header_s &bmp_header) {
	Ref<FileAccessMemory> f = memnew(FileAccessMemory);
	f->open_custom(p_data.ptr(), p_data.size());
	bmp_header.bmp_info_header.bmp_header_size = f->get_32();
	ERR_FAIL_COND_V_MSG(bmp_header.bmp_info_header.bmp_header_size < BITMAP_INFO_HEADER_MIN_SIZE, ERR_FILE_CORRUPT,
			vformat("Couldn't parse the BMP info header. The file is likely corrupt: %s", f->get_path()));

	bmp_header.bmp_info_header.bmp_width = f->get_32();
	bmp_header.bmp_info_header.bmp_height = f->get_32();

	bmp_header.bmp_info_header.bmp_planes = f->get_16();
	ERR_FAIL_COND_V_MSG(bmp_header.bmp_info_header.bmp_planes != 1, ERR_FILE_CORRUPT,
			vformat("Couldn't parse the BMP planes. The file is likely corrupt: %s", f->get_path()));

	bmp_header.bmp_info_header.bmp_bit_count = f->get_16();
	bmp_header.bmp_info_header.bmp_compression = f->get_32();
	bmp_header.bmp_info_header.bmp_size_image = f->get_32();
	bmp_header.bmp_info_header.bmp_pixels_per_meter_x = f->get_32();
	bmp_header.bmp_info_header.bmp_pixels_per_meter_y = f->get_32();
	bmp_header.bmp_info_header.bmp_colors_used = f->get_32();
	bmp_header.bmp_info_header.bmp_important_colors = f->get_32();

	switch (bmp_header.bmp_info_header.bmp_compression) {
		case BI_BITFIELDS: {
			bmp_header.bmp_bitfield.red_mask = f->get_32();
			bmp_header.bmp_bitfield.green_mask = f->get_32();
			bmp_header.bmp_bitfield.blue_mask = f->get_32();
			bmp_header.bmp_bitfield.alpha_mask = f->get_32();

			bmp_header.bmp_bitfield.red_mask_width = get_mask_width(bmp_header.bmp_bitfield.red_mask);
			bmp_header.bmp_bitfield.green_mask_width = get_mask_width(bmp_header.bmp_bitfield.green_mask);
			bmp_header.bmp_bitfield.blue_mask_width = get_mask_width(bmp_header.bmp_bitfield.blue_mask);
			bmp_header.bmp_bitfield.alpha_mask_width = get_mask_width(bmp_header.bmp_bitfield.alpha_mask);

			bmp_header.bmp_bitfield.alpha_offset = bmp_header.bmp_bitfield.red_mask_width + bmp_header.bmp_bitfield.green_mask_width + bmp_header.bmp_bitfield.blue_mask_width;
			bmp_header.bmp_bitfield.red_offset = bmp_header.bmp_bitfield.green_mask_width + bmp_header.bmp_bitfield.blue_mask_width;
			bmp_header.bmp_bitfield.green_offset = bmp_header.bmp_bitfield.blue_mask_width;

			bmp_header.bmp_bitfield.red_max = (1 << bmp_header.bmp_bitfield.red_mask_width) - 1;
			bmp_header.bmp_bitfield.green_max = (1 << bmp_header.bmp_bitfield.green_mask_width) - 1;
			bmp_header.bmp_bitfield.blue_max = (1 << bmp_header.bmp_bitfield.blue_mask_width) - 1;
		} break;
		case BI_RLE8:
		case BI_RLE4:
		case BI_CMYKRLE8:
		case BI_CMYKRLE4: {
			// Stop parsing.
			ERR_FAIL_V_MSG(ERR_UNAVAILABLE,
					vformat("RLE compressed BMP files are not yet supported: %s", f->get_path()));
		} break;
	}
	// Don't rely on sizeof(bmp_file_header) as structure padding
	// adds 2 bytes offset leading to misaligned color table reading
	uint32_t ct_offset = BITMAP_FILE_HEADER_SIZE + bmp_header.bmp_info_header.bmp_header_size;
	f->seek(ct_offset);

	uint32_t color_table_size = 0;

	// bmp_colors_used may report 0 despite having a color table
	// for 4 and 1 bit images, so don't rely on this information
	if (bmp_header.bmp_info_header.bmp_bit_count <= 8) {
		// Support 256 colors max
		color_table_size = 1 << bmp_header.bmp_info_header.bmp_bit_count;
		ERR_FAIL_COND_V_MSG(color_table_size == 0, ERR_BUG,
				vformat("Couldn't parse the BMP color table: %s", f->get_path()));
	}

	// Color table is usually 4 bytes per color -> [B][G][R][0]
	bmp_header.bmp_color_table.resize(color_table_size * 4);
	uint8_t *bmp_color_table_w = bmp_header.bmp_color_table.ptrw();
	f->get_buffer(bmp_color_table_w, color_table_size * 4);

	return OK;
}
} //namespace

Error ImageLoaderICO::load_embedded_image(const IconEntry &p_icon_entry, Ref<Image> p_image, const Vector<uint8_t> &p_data, BitField<ImageFormatLoader::LoaderFlags> p_flags, float p_scale) {
	ERR_FAIL_COND_V_MSG(p_data.is_empty(), ERR_INVALID_DATA, "ImageLoaderICO: Empty image data.");

	// Check if it's PNG (modern ICO files often use PNG)
	if (p_data.size() >= 8) {
		bool is_png = true;
		for (int i = 0; i < 8; i++) {
			if (p_data[i] != PNG_SIGNATURE[i]) {
				is_png = false;
				break;
			}
		}

		if (is_png) {
			Error err = p_image->load_png_from_buffer(p_data);
			if (err == OK && !p_image->is_empty()) {
				if (p_scale != 1.0 && p_scale > 0.0) {
					int new_width = MAX(1, Math::round(p_image->get_width() * p_scale));
					int new_height = MAX(1, Math::round(p_image->get_height() * p_scale));
					p_image->resize(new_width, new_height, Image::INTERPOLATE_LANCZOS);
				}
				if (p_flags & FLAG_FORCE_LINEAR) {
					p_image->srgb_to_linear();
				}
				return OK;
			}
		}
	}

	// If it's not PNG, assume it's BMP (ICO spec only allows PNG or BMP)
	// Embedded BMP data excludes the BITMAPFILEHEADER, so we need to construct it
	if (p_data.size() < BITMAP_INFO_HEADER_MIN_SIZE) {
		ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, "ImageLoaderICO: Embedded image data is too small to be a valid BMP.");
	}

	// Read BITMAPINFOHEADER to get header size and calculate color table
	// The embedded BMP data starts with BITMAPINFOHEADER (no BITMAPFILEHEADER)

	bmp_header_s bmp_header;

	Error err = parse_after_file_header(p_data, bmp_header);
	ERR_FAIL_COND_V_MSG(err != OK, err, "ImageLoaderICO: Failed to parse the BMP info header.");

	uint32_t info_header_size = bmp_header.bmp_info_header.bmp_header_size;
	ERR_FAIL_COND_V_MSG(info_header_size < BITMAP_INFO_HEADER_MIN_SIZE, ERR_FILE_CORRUPT,
			"ImageLoaderICO: Invalid BITMAPINFOHEADER size in embedded BMP data.");

	uint16_t bits_per_pixel = bmp_header.bmp_info_header.bmp_bit_count;
	uint32_t color_table_size = 0;
	if (bits_per_pixel <= 8) {
		// Support 256 colors max, 4 bytes per color
		color_table_size = (1 << bits_per_pixel) * 4;
	}

	bmp_header.bmp_file_header.bmp_signature = BITMAP_SIGNATURE;
	bmp_header.bmp_file_header.bmp_file_size = BITMAP_FILE_HEADER_SIZE + p_data.size();
	bmp_header.bmp_file_header.bmp_file_padding = 0;
	bmp_header.bmp_file_header.bmp_file_offset = BITMAP_FILE_HEADER_SIZE + info_header_size + color_table_size;

	// 	The height of the BMP image must be twice the height declared in the image directory. This is because the actual image data will contain two parts: the actual image immediately followed by a 1 bit mask of the same size as the image used to determine which pixels will be drawn.
	// The mask has to align to a DWORD (32 bits) and should be packed with 0s. A 0 pixel means 'the corresponding pixel in the image will be drawn' and a 1 means 'ignore this pixel'. The pixel colour is either explicit for 24 and 32 bit versions (which do not have colour tables), indexed for the other depths (1,2,4,8,16) in table of a four byte (BGRA) colours that follows the BITMAPINFOHEADER.
	// For 1 bit, typically the two colours are #00000000 and #00FFFFFF and the A channel is ignored.
	// The pixel data for 1,2,4,8 and 16 bits is packed by byte and DWORD aligned.
	// 24 bit images are stored as B G R triples but are not DWORD aligned.
	// 32 bit images are stored as B G R A quads.
	// Originally, ICOs and CURs were intended to be used on monochrome displays and used the formula Output = (Existing AND Mask) XOR Image but on colour screens, the cursor is composed using A channel blending and the mask is used to determine which pixels are included or excluded.
	uint32_t actual_width = bmp_header.bmp_info_header.bmp_width;
	uint32_t actual_height = bmp_header.bmp_info_header.bmp_height / 2;

	int64_t data_offset = info_header_size + color_table_size;
	int64_t data_size = p_data.size() - data_offset;
	int64_t actual_image_size = bmp_header.bmp_info_header.bmp_size_image;
	if (data_size < actual_image_size) {
		// TODO: handle masks? Just ignore it for now.
	}

	// BMP height is actually twice the actual height, so we need to divide by 2

	Ref<FileAccessBuffer> file_access = FileAccessBuffer::create();
	file_access->reserve(bmp_header.bmp_file_header.bmp_file_size);

	// typedef struct tagBITMAPFILEHEADER {
	// 	WORD  bfType; // must be BITMAP_SIGNATURE
	// 	DWORD bfSize; // must be BITMAP_FILE_HEADER_SIZE + p_data.size()
	// 	WORD  bfReserved1; // must be 0
	// 	WORD  bfReserved2; // must be 0
	// 	DWORD bfOffBits;
	//   } BITMAPFILEHEADER, *LPBITMAPFILEHEADER, *PBITMAPFILEHEADER;

	file_access->store_16(bmp_header.bmp_file_header.bmp_signature);
	file_access->store_32(bmp_header.bmp_file_header.bmp_file_size);
	file_access->store_16(0);
	file_access->store_16(0);
	file_access->store_32(bmp_header.bmp_file_header.bmp_file_offset);

	file_access->store_32(bmp_header.bmp_info_header.bmp_header_size);
	file_access->store_32(actual_width);
	file_access->store_32(actual_height);
	// now store the rest of the data after the 4 + 4 + 4 = 12 bytes that we just wrote
	file_access->store_buffer(p_data.ptr() + 12, p_data.size() - 12);

	file_access->seek(0);
	err = ImageLoaderBMP().load_image(p_image, file_access, p_flags, p_scale);
	if (err == OK && !p_image->is_empty()) {
		if (p_scale != 1.0 && p_scale > 0.0) {
			int new_width = MAX(1, Math::round(p_image->get_width() * p_scale));
			int new_height = MAX(1, Math::round(p_image->get_height() * p_scale));
			p_image->resize(new_width, new_height, Image::INTERPOLATE_LANCZOS);
		}
		if (p_flags & FLAG_FORCE_LINEAR) {
			p_image->srgb_to_linear();
		}
		return OK;
	}

	ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, "ImageLoaderICO: Failed to load embedded BMP image data.");
}

Error ImageLoaderICO::load_image(Ref<Image> p_image, Ref<FileAccess> p_fileaccess, BitField<ImageFormatLoader::LoaderFlags> p_flags, float p_scale) {
	ERR_FAIL_COND_V_MSG(p_fileaccess.is_null(), ERR_INVALID_PARAMETER, "ImageLoaderICO: Invalid FileAccess.");

	// Read ICONDIR header
	uint16_t reserved = p_fileaccess->get_16();
	ERR_FAIL_COND_V_MSG(reserved != 0, ERR_FILE_UNRECOGNIZED, "ImageLoaderICO: Invalid ICO file (reserved field should be 0).");

	uint16_t icon_type = p_fileaccess->get_16();
	ERR_FAIL_COND_V_MSG(icon_type != 1, ERR_FILE_UNRECOGNIZED, "ImageLoaderICO: Invalid ICO file (type should be 1 for ICO).");

	uint16_t icon_count = p_fileaccess->get_16();
	ERR_FAIL_COND_V_MSG(icon_count == 0, ERR_FILE_CORRUPT, "ImageLoaderICO: ICO file contains no images.");

	// Read all icon entries
	Vector<IconEntry> entries;
	entries.resize(icon_count);

	int best_index = -1;
	int best_size = -1;

	for (uint16_t i = 0; i < icon_count; i++) {
		IconEntry &entry = entries.write[i];
		entry.width = p_fileaccess->get_8();
		entry.height = p_fileaccess->get_8();
		entry.colors = p_fileaccess->get_8();
		entry.reserved = p_fileaccess->get_8();
		entry.planes = p_fileaccess->get_16();
		entry.bits_per_pixel = p_fileaccess->get_16();
		entry.image_size = p_fileaccess->get_32();
		entry.image_offset = p_fileaccess->get_32();

		// Handle 0 width/height meaning 256
		uint16_t actual_width = (entry.width == 0) ? 256 : entry.width;
		uint16_t actual_height = (entry.height == 0) ? 256 : entry.height;

		// Select best image: prefer larger images (ICO files typically contain multiple sizes)
		int size = actual_width * actual_height;
		if (best_index == -1 || size > best_size) {
			best_index = i;
			best_size = size;
		}
	}

	ERR_FAIL_COND_V_MSG(best_index == -1, ERR_FILE_CORRUPT, "ImageLoaderICO: Could not select an image from ICO file.");

	const IconEntry &selected_entry = entries[best_index];

	// Read the selected image data
	ERR_FAIL_COND_V_MSG(selected_entry.image_offset + selected_entry.image_size > (uint32_t)p_fileaccess->get_length(),
			ERR_FILE_CORRUPT, "ImageLoaderICO: Image data offset exceeds file size.");

	Vector<uint8_t> image_data;
	image_data.resize(selected_entry.image_size);

	uint64_t saved_pos = p_fileaccess->get_position();
	p_fileaccess->seek(selected_entry.image_offset);
	p_fileaccess->get_buffer(image_data.ptrw(), selected_entry.image_size);
	p_fileaccess->seek(saved_pos);

	// Load the embedded image (PNG or BMP)
	Error err = load_embedded_image(selected_entry, p_image, image_data, p_flags, p_scale);
	if (err != OK) {
		return err;
	}

	if (p_image->is_empty()) {
		return ERR_INVALID_DATA;
	}

	return OK;
}

void ImageLoaderICO::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("ico");
}

ImageLoaderICO::ImageLoaderICO() {
}
