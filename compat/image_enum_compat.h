#pragma once
#include "core/io/image.h"

namespace V2Image {
enum Format {
	IMAGE_ENCODING_EMPTY = 0,
	IMAGE_ENCODING_RAW = 1,
	IMAGE_ENCODING_LOSSLESS = 2,
	IMAGE_ENCODING_LOSSY = 3,

	IMAGE_FORMAT_GRAYSCALE = 0,
	IMAGE_FORMAT_INTENSITY = 1,
	IMAGE_FORMAT_GRAYSCALE_ALPHA = 2,
	IMAGE_FORMAT_RGB = 3,
	IMAGE_FORMAT_RGBA = 4,
	IMAGE_FORMAT_INDEXED = 5,
	IMAGE_FORMAT_INDEXED_ALPHA = 6,
	FORMAT_YUV_422 = 7,
	FORMAT_YUV_444 = 8,
	IMAGE_FORMAT_BC1 = 9,
	IMAGE_FORMAT_BC2 = 10,
	IMAGE_FORMAT_BC3 = 11,
	IMAGE_FORMAT_BC4 = 12,
	IMAGE_FORMAT_BC5 = 13,
	IMAGE_FORMAT_PVRTC2 = 14,
	IMAGE_FORMAT_PVRTC2_ALPHA = 15,
	IMAGE_FORMAT_PVRTC4 = 16,
	IMAGE_FORMAT_PVRTC4_ALPHA = 17,
	IMAGE_FORMAT_ETC = 18,
	IMAGE_FORMAT_ATC = 19,
	IMAGE_FORMAT_ATC_ALPHA_EXPLICIT = 20,
	IMAGE_FORMAT_ATC_ALPHA_INTERPOLATED = 21,
	IMAGE_FORMAT_V2_MAX = 22,
	IMAGE_FORMAT_CUSTOM = 30
};
} // namespace V2Image
namespace V3Image {
enum Format {
	FORMAT_L8, //luminance
	FORMAT_LA8, //luminance-alpha
	FORMAT_R8,
	FORMAT_RG8,
	FORMAT_RGB8,
	FORMAT_RGBA8,
	FORMAT_RGBA4444,
	FORMAT_RGBA5551,
	FORMAT_RF, //float
	FORMAT_RGF,
	FORMAT_RGBF,
	FORMAT_RGBAF,
	FORMAT_RH, //half float
	FORMAT_RGH,
	FORMAT_RGBH,
	FORMAT_RGBAH,
	FORMAT_RGBE9995,
	FORMAT_DXT1, //s3tc bc1
	FORMAT_DXT3, //bc2
	FORMAT_DXT5, //bc3
	FORMAT_RGTC_R,
	FORMAT_RGTC_RG,
	FORMAT_BPTC_RGBA, //btpc bc7
	FORMAT_BPTC_RGBF, //float bc6h
	FORMAT_BPTC_RGBFU, //unsigned float bc6hu
	FORMAT_PVRTC2, //pvrtc
	FORMAT_PVRTC2A,
	FORMAT_PVRTC4,
	FORMAT_PVRTC4A,
	FORMAT_ETC, //etc1
	FORMAT_ETC2_R11, //etc2
	FORMAT_ETC2_R11S, //signed, NOT srgb.
	FORMAT_ETC2_RG11,
	FORMAT_ETC2_RG11S,
	FORMAT_ETC2_RGB8,
	FORMAT_ETC2_RGBA8,
	FORMAT_ETC2_RGB8A1,
	FORMAT_MAX
};
}

class ImageEnumCompat {
public:
	static V2Image::Format get_v2_format_from_string(const String &fmt_id);
	static String get_v2_format_name(V2Image::Format p_format);
	static String get_v2_format_identifier(V2Image::Format p_format);
	static String get_v2_format_identifier_pcfg(V2Image::Format p_format, int p_img_size);

	static String get_v3_format_name(V3Image::Format p_format);
	static String get_v3_format_identifier(V3Image::Format p_format);
	static V3Image::Format get_v3_format_enum_from_name(String p_format);

	static String get_v4_format_identifier(Image::Format p_format);

	static Image::Format convert_image_format_enum_v3_to_v4(V3Image::Format fmt);
	static V2Image::Format convert_image_format_enum_v4_to_v2(Image::Format p_format);
	static Image::Format convert_image_format_enum_v2_to_v4(V2Image::Format p_format);
};
