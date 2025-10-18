#ifndef IMAGE_PARSER_V2_H
#define IMAGE_PARSER_V2_H

#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource.h"
#include "core/variant/variant.h"
#include "core/variant/variant_parser.h"
#include "image_enum_compat.h"
class ImageParserV2 {
public:
	static Ref<Image> convert_indexed_image(const Vector<uint8_t> &p_imgdata, int p_width, int p_height, int p_mipmaps, V2Image::Format p_format, Error *r_error);

	static String image_v2_to_string(const Variant &r_v, bool is_pcfg = false);
	static Error parse_image_construct_v2(VariantParser::Stream *f, Variant &r_v, bool convert_indexed, int &line, String &p_err_str, bool is_pcfg = false);

	static Error decode_image_v2(Ref<FileAccess> f, Variant &r_v, bool convert_indexed = true);
	static Error write_image_v2_to_bin(Ref<FileAccess> f, const Variant &r_v, bool compress_lossless = true);
};

#endif // IMAGE_PARSER_V2_H
