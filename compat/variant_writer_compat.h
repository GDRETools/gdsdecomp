#pragma once

#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/variant/variant.h"
#include "core/variant/variant_parser.h"

#include "utility/resource_info.h"

class VariantParserCompat : VariantParser {
	static Error _parse_dictionary(Dictionary &object, Stream *p_stream, int &line, String &r_err_str, ResourceParser *p_res_parser = nullptr);
	static Error _parse_array(Array &array, Stream *p_stream, int &line, String &r_err_str, ResourceParser *p_res_parser = nullptr);

public:
	static Error _default_parse_resource(VariantParser::Stream *p_stream, Ref<Resource> &r_res, int &line, String &r_err_str, ResourceInfo::LoadType load_type, bool use_sub_threads = false, ResourceFormatLoader::CacheMode cache_mode = ResourceFormatLoader::CACHE_MODE_REUSE);
	static Error parse_value(VariantParser::Token &token, Variant &value, VariantParser::Stream *p_stream, int &line, String &r_err_str, VariantParser::ResourceParser *p_res_parser);
	static Error parse_tag(VariantParser::Stream *p_stream, int &line, String &r_err_str, Tag &r_tag, VariantParser::ResourceParser *p_res_parser = nullptr, bool p_simple_tag = false);
	static Error parse_tag_assign_eof(VariantParser::Stream *p_stream, int &line, String &r_err_str, Tag &r_tag, String &r_assign, Variant &r_value, VariantParser::ResourceParser *p_res_parser = nullptr, bool p_simple_tag = false);
	static Error parse(Stream *p_stream, Variant &r_ret, String &r_err_str, int &r_err_line, ResourceParser *p_res_parser = nullptr);
};

class VariantWriterCompat {
public:
	typedef Error (*StoreStringFunc)(void *ud, const String &p_string);
	typedef String (*EncodeResourceFunc)(void *ud, const Ref<Resource> &p_resource);

	static Error write_to_string(const Variant &p_variant, String &r_string, const uint32_t ver_major, const uint32_t ver_minor = 0, EncodeResourceFunc p_encode_res_func = nullptr, void *p_encode_res_ud = nullptr, bool p_compat_4x_force_v3 = true);
	static Error write_to_string_pcfg(const Variant &p_variant, String &r_string, const uint32_t ver_major, const uint32_t ver_minor = 0, EncodeResourceFunc p_encode_res_func = nullptr, void *p_encode_res_ud = nullptr, bool p_compat_4x_force_v3 = true);
	static Error write_to_string_script(const Variant &p_variant, String &r_string, const uint32_t ver_major, const uint32_t ver_minor = 0, EncodeResourceFunc p_encode_res_func = nullptr, void *p_encode_res_ud = nullptr, bool p_compat_4x_force_v3 = true);
};
