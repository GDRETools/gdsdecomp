#include "image_enum_compat.h"

#include "core/error/error_macros.h"

const char *v2_format_names[V2Image::IMAGE_FORMAT_V2_MAX] = {
	"Grayscale",
	"Intensity",
	"GrayscaleAlpha",
	"RGB",
	"RGBA",
	"Indexed",
	"IndexedAlpha",
	"YUV422",
	"YUV444",
	"BC1",
	"BC2",
	"BC3",
	"BC4",
	"BC5",
	"PVRTC2",
	"PVRTC2Alpha",
	"PVRTC4",
	"PVRTC4Alpha",
	"ETC",
	"ATC",
	"ATCAlphaExp",
	"ATCAlphaInterp",
};

const char *v2_format_identifiers[V2Image::IMAGE_FORMAT_V2_MAX] = {
	"GRAYSCALE",
	"INTENSITY",
	"GRAYSCALE_ALPHA",
	"RGB",
	"RGBA",
	"INDEXED",
	"INDEXED_ALPHA",
	"YUV422",
	"YUV444",
	"BC1",
	"BC2",
	"BC3",
	"BC4",
	"BC5",
	"PVRTC2",
	"PVRTC2_ALPHA",
	"PVRTC4",
	"PVRTC4_ALPHA",
	"ETC",
	"ATC",
	"ATC_ALPHA_EXPLICIT",
	"ATC_ALPHA_INTERPOLATED"
};

const char *v3_format_identifiers[V3Image::FORMAT_MAX] = {

	"L8", //luminance
	"LA8", //luminance-alpha
	"R8",
	"RG8",
	"RGB8",
	"RGBA8",
	"RGBA4444",
	"RGBA5551",
	"RF", //float
	"RGF",
	"RGBF",
	"RGBAF",
	"RH", //half float
	"RGH",
	"RGBH",
	"RGBAH",
	"RGBE9995",
	"DXT1", //s3tc bc1
	"DXT3", //bc2
	"DXT5", //bc3
	"RGTC_R",
	"RGTC_RG",
	"BPTC_RGBA", //btpc bc7
	"BPTC_RGBF", //float bc6h
	"BPTC_RGBFU", //unsigned float bc6hu
	"PVRTC2", //pvrtc
	"PVRTC2A",
	"PVRTC4",
	"PVRTC4A",
	"ETC", //etc1
	"ETC2_R11", //etc2
	"ETC2_R11S", //signed, NOT srgb.
	"ETC2_RG11",
	"ETC2_RG11S",
	"ETC2_RGB8",
	"ETC2_RGBA8",
	"ETC2_RGB8A1"
};

const char *v3_format_names[V3Image::FORMAT_MAX] = {
	"Lum8", //luminance
	"LumAlpha8", //luminance-alpha
	"Red8",
	"RedGreen",
	"RGB8",
	"RGBA8",
	"RGBA4444",
	"RGBA5551",
	"RFloat", //float
	"RGFloat",
	"RGBFloat",
	"RGBAFloat",
	"RHalf", //half float
	"RGHalf",
	"RGBHalf",
	"RGBAHalf",
	"RGBE9995",
	"DXT1 RGB8", //s3tc
	"DXT3 RGBA8",
	"DXT5 RGBA8",
	"RGTC Red8",
	"RGTC RedGreen8",
	"BPTC_RGBA",
	"BPTC_RGBF",
	"BPTC_RGBFU",
	"PVRTC2", //pvrtc
	"PVRTC2A",
	"PVRTC4",
	"PVRTC4A",
	"ETC", //etc1
	"ETC2_R11", //etc2
	"ETC2_R11S", //signed", NOT srgb.
	"ETC2_RG11",
	"ETC2_RG11S",
	"ETC2_RGB8",
	"ETC2_RGBA8",
	"ETC2_RGB8A1",
};

const char *v4_format_identifiers[Image::FORMAT_MAX] = {
	"L8", //luminance
	"LA8", //luminance-alpha
	"R8",
	"RG8",
	"RGB8",
	"RGBA8",
	"RGBA4444",
	"RGB565",
	"RF", //float
	"RGF",
	"RGBF",
	"RGBAF",
	"RH", //half float
	"RGH",
	"RGBH",
	"RGBAH",
	"RGBE9995",
	"DXT1", //s3tc bc1
	"DXT3", //bc2
	"DXT5", //bc3
	"RGTC_R",
	"RGTC_RG",
	"BPTC_RGBA", //btpc bc7
	"BPTC_RGBF", //float bc6h
	"BPTC_RGBFU", //unsigned float bc6hu
	"ETC", //etc1
	"ETC2_R11", //etc2
	"ETC2_R11S", //signed, NOT srgb.
	"ETC2_RG11",
	"ETC2_RG11S",
	"ETC2_RGB8",
	"ETC2_RGBA8",
	"ETC2_RGB8A1",
	"ETC2_RA_AS_RG", //used to make basis universal happy
	"DXT5_RA_AS_RG" //used to make basis universal happy
};

V2Image::Format ImageEnumCompat::get_v2_format_from_string(const String &fmt_id) {
	String id = fmt_id.to_upper();
	if (id == "CUSTOM") {
		return V2Image::IMAGE_FORMAT_CUSTOM;
	}
	for (int i = 0; i < V2Image::IMAGE_FORMAT_V2_MAX; i++) {
		if (v2_format_identifiers[i] == id) {
			return (V2Image::Format)i;
		}
	}
	return V2Image::IMAGE_FORMAT_V2_MAX;
}

String ImageEnumCompat::get_v2_format_name(V2Image::Format p_format) {
	if (p_format == V2Image::IMAGE_FORMAT_CUSTOM) {
		return "Custom";
	}
	ERR_FAIL_INDEX_V(p_format, V2Image::IMAGE_FORMAT_V2_MAX, String());
	return v2_format_names[p_format];
}

String ImageEnumCompat::get_v2_format_identifier(V2Image::Format p_format) {
	if (p_format == V2Image::IMAGE_FORMAT_CUSTOM) {
		return "CUSTOM";
	}
	ERR_FAIL_INDEX_V(p_format, V2Image::IMAGE_FORMAT_V2_MAX, String());
	return v2_format_identifiers[p_format];
}

String ImageEnumCompat::get_v3_format_name(V3Image::Format p_format) {
	ERR_FAIL_INDEX_V(p_format, V3Image::FORMAT_MAX, String());
	return v3_format_names[p_format];
}

V3Image::Format ImageEnumCompat::get_v3_format_enum_from_name(String p_format) {
	for (int i = 0; i < V3Image::FORMAT_MAX; i++) {
		if (v3_format_names[i] == p_format) {
			return V3Image::Format(i);
		}
	}
	return V3Image::FORMAT_MAX;
}

String ImageEnumCompat::get_v3_format_identifier(V3Image::Format p_format) {
	ERR_FAIL_INDEX_V(p_format, V3Image::FORMAT_MAX, String());
	return v3_format_identifiers[p_format];
}

String ImageEnumCompat::get_v4_format_identifier(Image::Format p_format) {
	ERR_FAIL_INDEX_V(p_format, Image::FORMAT_MAX, String());
	return v4_format_identifiers[p_format];
}

String ImageEnumCompat::get_v2_format_identifier_pcfg(V2Image::Format p_format, int p_img_size) {
	switch (p_format) {
		case V2Image::IMAGE_FORMAT_GRAYSCALE:
			return "grayscale";
			break;
		case V2Image::IMAGE_FORMAT_INTENSITY:
			return "intensity";
			break;
		case V2Image::IMAGE_FORMAT_GRAYSCALE_ALPHA:
			return "grayscale_alpha";
			break;
		case V2Image::IMAGE_FORMAT_RGB:
			return "rgb";
			break;
		case V2Image::IMAGE_FORMAT_RGBA:
			return "rgba";
			break;
		case V2Image::IMAGE_FORMAT_INDEXED:
			return "indexed";
			break;
		case V2Image::IMAGE_FORMAT_INDEXED_ALPHA:
			return "indexed_alpha";
			break;
		case V2Image::IMAGE_FORMAT_BC1:
			return "bc1";
			break;
		case V2Image::IMAGE_FORMAT_BC2:
			return "bc2";
			break;
		case V2Image::IMAGE_FORMAT_BC3:
			return "bc3";
			break;
		case V2Image::IMAGE_FORMAT_BC4:
			return "bc4";
			break;
		case V2Image::IMAGE_FORMAT_BC5:
			return "bc5";
			break;
		case V2Image::IMAGE_FORMAT_ETC:
			return "etc";
			break;
		case V2Image::IMAGE_FORMAT_CUSTOM:
			return "custom custom_size=" + itos(p_img_size) + "";
			break;
		default:
			return "UNKNOWN_IMAGE_FORMAT";
	}
}

// V4 removed PVRTC, need to convert enums
V2Image::Format ImageEnumCompat::convert_image_format_enum_v4_to_v2(Image::Format p_format) {
	switch (p_format) {
		// convert old image format types to new ones
		case Image::FORMAT_L8:
			return V2Image::IMAGE_FORMAT_GRAYSCALE;
		case Image::FORMAT_LA8:
			return V2Image::IMAGE_FORMAT_GRAYSCALE_ALPHA;
		case Image::FORMAT_RGB8:
			return V2Image::IMAGE_FORMAT_RGB;
		case Image::FORMAT_RGBA8:
			return V2Image::IMAGE_FORMAT_RGBA;
		case Image::FORMAT_DXT1:
			return V2Image::IMAGE_FORMAT_BC1;
		case Image::FORMAT_DXT3:
			return V2Image::IMAGE_FORMAT_BC2;
		case Image::FORMAT_DXT5:
			return V2Image::IMAGE_FORMAT_BC3;
		case Image::FORMAT_RGTC_R:
			return V2Image::IMAGE_FORMAT_BC4;
		case Image::FORMAT_RGTC_RG:
			return V2Image::IMAGE_FORMAT_BC5;
		case Image::FORMAT_ETC:
			return V2Image::IMAGE_FORMAT_ETC;
		default: {
		}
	}
	return V2Image::IMAGE_FORMAT_V2_MAX;
}

Image::Format ImageEnumCompat::convert_image_format_enum_v2_to_v4(V2Image::Format p_format) {
	switch (p_format) {
		// convert old image format types to new ones
		case V2Image::IMAGE_FORMAT_GRAYSCALE:
			return Image::FORMAT_L8;
		case V2Image::IMAGE_FORMAT_GRAYSCALE_ALPHA:
			return Image::FORMAT_LA8;
		case V2Image::IMAGE_FORMAT_RGB:
			return Image::FORMAT_RGB8;
		case V2Image::IMAGE_FORMAT_RGBA:
			return Image::FORMAT_RGBA8;
		case V2Image::IMAGE_FORMAT_BC1:
			return Image::FORMAT_DXT1;
		case V2Image::IMAGE_FORMAT_BC2:
			return Image::FORMAT_DXT3;
		case V2Image::IMAGE_FORMAT_BC3:
			return Image::FORMAT_DXT5;
		case V2Image::IMAGE_FORMAT_BC4:
			return Image::FORMAT_RGTC_R;
		case V2Image::IMAGE_FORMAT_BC5:
			return Image::FORMAT_RGTC_RG;
		case V2Image::IMAGE_FORMAT_ETC:
			return Image::FORMAT_ETC;
		// If this is a deprecated or unsupported format...
		default: {
		}
	}
	return Image::FORMAT_MAX;
}
Image::Format ImageEnumCompat::convert_image_format_enum_v3_to_v4(V3Image::Format fmt) {
	// V4 removed
	if (Image::Format(fmt) <= Image::Format::FORMAT_BPTC_RGBFU) {
		return Image::Format(fmt);
	}

	// They removed four PVRTC enum values after BPTC_RGBFU, so just subtract 4
	if (fmt >= V3Image::Format::FORMAT_ETC && fmt < V3Image::Format::FORMAT_MAX) {
		return Image::Format(fmt - 4);
	}
	// If this is an invalid value, return FORMAT_MAX

	return Image::FORMAT_MAX;
}
