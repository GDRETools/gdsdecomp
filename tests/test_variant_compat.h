
#ifndef TEST_VARIANT_COMPAT_H
#define TEST_VARIANT_COMPAT_H
#include "compat/image_enum_compat.h"
#include "compat/image_parser_v2.h"
#include "compat/input_event_parser_v2.h"
#include "compat/variant_decoder_compat.h"
#include "core/io/image.h"
#include "core/variant/variant.h"
#include "core/version_generated.gen.h"
#include "tests/test_macros.h"

#include "../compat/variant_writer_compat.h"
#include "utility/file_access_buffer.h"

namespace TestVariantCompat {

static inline Array build_array() {
	return Array();
}

template <typename... Targs>
static inline Array build_array(Variant item, Targs... Fargs) {
	Array a = build_array(Fargs...);
	a.push_front(item);
	return a;
}

static inline Dictionary build_dictionary() {
	return Dictionary();
}

template <typename... Targs>
static inline Dictionary build_dictionary(Variant key, Variant item, Targs... Fargs) {
	Dictionary d = build_dictionary(Fargs...);
	d[key] = item;
	return d;
}

template <class T>
void _ALWAYS_INLINE_ test_variant_write_v2(const String &name, const T &p_val, const String &expected_v2 = "") {
	// we need to use a macro here to get the name of the type, as we cannot use typeid(T).name() in a constexpr context
	SUBCASE(vformat("%s write_to_string v2", name).utf8().get_data()) {
		String compat_ret;
		Error error = VariantWriterCompat::write_to_string(p_val, compat_ret, 2);
		CHECK(error == OK);
		if (expected_v2.size() > 0) {
			CHECK(compat_ret.size() == expected_v2.size());
			CHECK(compat_ret == expected_v2);
		}
	}
}

template <class T>
void _ALWAYS_INLINE_ test_variant_write_v3(const String &name, const T &p_val, const String &expected_v3 = "") {
	// we need to use a macro here to get the name of the type, as we cannot use typeid(T).name() in a constexpr context
	SUBCASE(vformat("%s write_to_string v3", name).utf8().get_data()) {
		String compat_ret;
		Error error = VariantWriterCompat::write_to_string(p_val, compat_ret, 3);
		CHECK(error == OK);
		if (expected_v3.size() > 0) {
			CHECK(compat_ret.size() == expected_v3.size());
			CHECK(compat_ret == expected_v3);
		}
	}
}

template <class T>
void _ALWAYS_INLINE_ test_variant_write_v4(const String &name, const T &p_val) {
	SUBCASE(vformat("%s write_to_string v4 compat", name).utf8().get_data()) {
		String compat_ret;
		Error error = VariantWriterCompat::write_to_string(p_val, compat_ret, GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR, nullptr, nullptr, true);
		CHECK(error == OK);
		String gd_ret;
		error = VariantWriter::write_to_string(p_val, gd_ret, nullptr, nullptr, true);
		CHECK(error == OK);
		CHECK(compat_ret.size() == gd_ret.size());
		CHECK(compat_ret == gd_ret);
	}
	SUBCASE(vformat("%s write_to_string v4 no compat", name).utf8().get_data()) {
		String compat_ret;
		Error error = VariantWriterCompat::write_to_string(p_val, compat_ret, GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR, nullptr, nullptr, false);
		CHECK(error == OK);
		String gd_ret;
		error = VariantWriter::write_to_string(p_val, gd_ret, nullptr, nullptr, false);
		CHECK(error == OK);
		CHECK(compat_ret.size() == gd_ret.size());
		CHECK(compat_ret == gd_ret);
	}
}

template <class T>
void _ALWAYS_INLINE_ test_variant_write_all(const String &name, const T &p_val, const String &expected_v2, const String &expected_v3) {
	test_variant_write_v2(name, p_val, expected_v2);
	test_variant_write_v3(name, p_val, expected_v3);
	test_variant_write_v4(name, p_val);
}

static constexpr const char *byte_array_v2_name = "ByteArray";
static constexpr const char *byte_array_v3_name = "PoolByteArray";
static constexpr const char *int_array_v2_name = "IntArray";
static constexpr const char *int_array_v3_name = "PoolIntArray";
static constexpr const char *float_array_v2_name = "FloatArray";
static constexpr const char *float_array_v3_name = "PoolRealArray";
static constexpr const char *string_array_v2_name = "StringArray";
static constexpr const char *string_array_v3_name = "PoolStringArray";
static constexpr const char *vector2_array_v2_name = "Vector2Array";
static constexpr const char *vector2_array_v3_name = "PoolVector2Array";
static constexpr const char *vector3_array_v2_name = "Vector3Array";
static constexpr const char *vector3_array_v3_name = "PoolVector3Array";

String _ALWAYS_INLINE_ make_expected_vec_cfg(const String &arg_str) {
	return vformat("[ %s ]", arg_str);
}

String _ALWAYS_INLINE_ make_expected_vec(const String &arg_str, const String &type_name) {
	return vformat("%s( %s )", type_name, arg_str);
}

template <class T>
void _ALWAYS_INLINE_ test_vector_write_all(const String &test_name, const Vector<T> &p_val, const String &v2name, const String &v3name, const String &arg_str) {
	test_variant_write_v2(test_name, p_val, make_expected_vec(arg_str, v2name));
	test_variant_write_v3(test_name, p_val, make_expected_vec(arg_str, v3name));
	test_variant_write_v4(test_name, p_val);
}

TEST_CASE("[GDSDecomp][VariantCompat] Vector<uint8_t>") {
	Vector<uint8_t> arr = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 100, 110, 111, 255 };
	String arg_str = "0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 100, 110, 111, 255";
	test_vector_write_all("Simple Vector<uint8_t>", arr, byte_array_v2_name, byte_array_v3_name, arg_str);

	static constexpr uint64_t large_array_size = 10000;
	Vector<uint8_t> large_arr;
	large_arr.resize(large_array_size);
	String large_arg_str = itos(0);
	large_arr.write[0] = 0;
	for (uint64_t i = 1; i < large_array_size; i++) {
		large_arr.write[i] = i % 256;
		large_arg_str += ", " + itos(i % 256);
	}
	test_vector_write_all("Large Vector<uint8_t>", large_arr, byte_array_v2_name, byte_array_v3_name, large_arg_str);
}

// Disabling this for now, CI does not have enough RAM to run this test
#if 0
TEST_CASE("[GDSDecomp][VariantCompat] Vector<uint8_t> EXTRA_LARGE") {
	static constexpr uint64_t max_el_str_size = INT_MAX - sizeof("PackedByteArray(") - sizeof(")");
	static constexpr uint64_t el_size = 5; // "255" + ", "
	static constexpr uint64_t large_array_size = max_el_str_size / 5 - el_size;
	Vector<uint8_t> large_arr;
	large_arr.resize(large_array_size);
	memset(large_arr.ptrw(), 255, large_array_size);
	String thing;
	Error err = VariantWriterCompat::write_to_string(large_arr, thing, 4, nullptr, nullptr, true);
	CHECK(err == OK);
	CHECK(thing.begins_with("PackedByteArray(255"));
	CHECK(thing.ends_with("255)"));
}
#endif

TEST_CASE("[GDSDecomp][VariantCompat] int") {
	test_variant_write_all<int>("0", 0, "0", "0");
	// INT32_MAX
	test_variant_write_all<int>("INT_MAX", INT32_MAX, "2147483647", "2147483647");
	// INT32_MIN
	test_variant_write_all<int>("INT_MIN", INT32_MIN, "-2147483648", "-2147483648");
}

TEST_CASE("[GDSDecomp][VariantCompat] int64_t") {
	test_variant_write_all<int64_t>("0", 0, "0", "0");
	test_variant_write_all<int64_t>("INT64_MAX", INT64_MAX, "9223372036854775807", "9223372036854775807");
	test_variant_write_all<int64_t>("INT64_MIN", INT64_MIN, "-9223372036854775808", "-9223372036854775808");
}

TEST_CASE("[GDSDecomp][VariantCompat] float") {
	test_variant_write_all<float>("100000.0", 100000.0, "100000.0", "100000.0");
	test_variant_write_all<float>("0.0", 0.0, "0.0", "0.0");
	test_variant_write_all<float>("1.0", 1.0, "1.0", "1.0");
	test_variant_write_all<float>("INFINITY", INFINITY, "inf", "inf");
	test_variant_write_all<float>("-INFINITY", -INFINITY, "-inf", "inf_neg");
	test_variant_write_all<float>("NAN", NAN, "nan", "nan");
}

TEST_CASE("[GDSDecomp][VariantCompat] String") {
	{
		String str = "Hello";
		test_variant_write_all("Simple String", str, "\"Hello\"", "\"Hello\"");
	}

	{
		// Strings are written as multi-line outside of PackedStringArrays, so escapes other than `\\` and `\"` are not escaped
		String str = "Hello\nWorld";
		test_variant_write_all("Escaped String \\n", str, "\"Hello\nWorld\"", "\"Hello\nWorld\"");
	}

	{
		String str = "Hello\tWorld";
		test_variant_write_all("Escaped String \\t", str, "\"Hello\tWorld\"", "\"Hello\tWorld\"");
	}

	{
		String str = "Hello\"World";
		test_variant_write_all("Escaped String \\\"", str, "\"Hello\\\"World\"", "\"Hello\\\"World\"");
	}
	{
		String str = "Hello\\World";
		test_variant_write_all("Escaped String \\\\", str, "\"Hello\\\\World\"", "\"Hello\\\\World\"");
	}
}

void _ALWAYS_INLINE_ init_array(Array &array, const Vector<Variant> &list) {
	for (const Variant &v : list) {
		array.push_back(v);
	}
}

TEST_CASE("[GDSDecomp][VariantCompat] Array") {
	{
		Array array;
		init_array(array, { 0, 1.0, "Hello", Vector2(0, 0), Vector3(0, 0, 0), Color(0, 0, 0, 0), Array() });
		String arg_string = "[ 0, 1.0, \"Hello\", Vector2( 0, 0 ), Vector3( 0, 0, 0 ), Color( 0, 0, 0, 0 ), [  ] ]";
		test_variant_write_all("Simple Array", array, arg_string, arg_string);
	}
	{
		Array array;
		array.set_typed(Variant::Type::INT, {}, {});
		init_array(array, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 });
		String arg_string = "[ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 ]"; // no typed arrays in v2 and v3, so it will be written as a normal array
		test_variant_write_all("Typed Array", array, arg_string, arg_string);
	}
}

TEST_CASE("[GDSDecomp][VariantCompat] Vector<int32_t>") {
	Vector<int32_t> arr = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 100, 110, 111, 1000, 1001, 10000, 10001, 100000, 100001, 1000000, 1000001, 10000000, 10000001, 100000000, 100000001, 1000000000, 1000000001 };
	String arg_str = "0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 100, 110, 111, 1000, 1001, 10000, 10001, 100000, 100001, 1000000, 1000001, 10000000, 10000001, 100000000, 100000001, 1000000000, 1000000001";
	test_vector_write_all("Simple Vector<int32_t>", arr, int_array_v2_name, int_array_v3_name, arg_str);
}

TEST_CASE("[GDSDecomp][VariantCompat] Vector<int64_t>") {
	Vector<int64_t> arr = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 100, 110, 111, 1000, 1001, 10000, 10001, 100000, 100001, 1000000, 1000001, 10000000, 10000001, 100000000, 100000001, 1000000000, 1000000001 };
	test_variant_write_v4("Simple Vector<int64_t>", arr);
}

TEST_CASE("[GDSDecomp][VariantCompat] Vector<float>") {
	Vector<float> arr = { 0.0, -0.0, 1.0, NAN, INFINITY, -INFINITY, 0 };
	test_variant_write_all("Simple Vector<float>", arr,
			vformat("%s( %s )", float_array_v2_name, "0, 0, 1, nan, inf, -inf, 0"),
			vformat("%s( %s )", float_array_v3_name, "0, 0, 1, nan, inf, inf_neg, 0"));
}

TEST_CASE("[GDSDecomp][VariantCompat] Vector<double>") {
	Vector<double> arr = { 0.0, 1.0, NAN, INFINITY, -INFINITY, 0 };
	test_variant_write_v4("Simple Vector<double>", arr);
}

// TODO: disabling the rest of the float tests until the pr that fixes float precision lands.
TEST_CASE("[GDSDecomp][VariantCompat] Vector<float> (with >6 precision)") {
	Vector<float> arr = { 0.0, 1.0, 1.1, NAN, INFINITY, -INFINITY, 0 };
	test_variant_write_v4("Simple Vector<float>", arr);
}

TEST_CASE("[GDSDecomp][VariantCompat] Vector<String>") {
	{
		Vector<String> arr = { "Hello", "World", "This", "Is", "A", "Test" };
		String arg_str = "\"Hello\", \"World\", \"This\", \"Is\", \"A\", \"Test\"";
		test_vector_write_all("Simple Vector<String>", arr, string_array_v2_name, string_array_v3_name, arg_str);
	}

	{
		Vector<String> arr = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "100", "110", "111", "1000", "1001", "10000", "10001", "100000", "100001", "1000000", "1000001", "10000000", "10000001", "100000000", "100000001", "1000000000", "1000000001" };
		String arg_str = "\"0\", \"1\", \"2\", \"3\", \"4\", \"5\", \"6\", \"7\", \"8\", \"9\", \"10\", \"11\", \"100\", \"110\", \"111\", \"1000\", \"1001\", \"10000\", \"10001\", \"100000\", \"100001\", \"1000000\", \"1000001\", \"10000000\", \"10000001\", \"100000000\", \"100000001\", \"1000000000\", \"1000000001\"";
		test_vector_write_all("Numeral Vector<String>", arr, string_array_v2_name, string_array_v3_name, arg_str);
	}

	{
		// strings with escapes
		Vector<String> arr = { "Hello\nWorld", "This\tIs", "A\"Test" };
		String arg_str = "\"Hello\\nWorld\", \"This\\tIs\", \"A\\\"Test\"";
		test_vector_write_all("Escaped Vector<String>", arr, string_array_v2_name, string_array_v3_name, arg_str);
	}
}

TEST_CASE("[GDSDecomp][VariantCompat] Vector<Vector2>") {
	Vector<Vector2> arr = { Vector2(0, 0), Vector2(1, 1) };
	String arg_str = "0, 0, 1, 1";
	test_vector_write_all("Simple Vector<Vector2>", arr, vector2_array_v2_name, vector2_array_v3_name, arg_str);
}
TEST_CASE("[GDSDecomp][VariantCompat] Vector<Vector3> with precision") {
	Vector<Vector3> arr = { Vector3(0, 0, 0), Vector3(1, 1, 1.1) };
	String arg_str = "0, 0, 0, 1, 1, 1.1";
	test_vector_write_all("Simple Vector<Vector3>", arr, vector3_array_v2_name, vector3_array_v3_name, arg_str);
}

TEST_CASE("[GDSDecomp][VariantCompat] Vector<Vector3>") {
	Vector<Vector3> arr = { Vector3(0, 0, 0), Vector3(1, 1, 1) };
	String arg_str = "0, 0, 0, 1, 1, 1";
	test_vector_write_all("Simple Vector<Vector3>", arr, vector3_array_v2_name, vector3_array_v3_name, arg_str);
}

// vector4
TEST_CASE("[GDSDecomp][VariantCompat] Vector<Vector4>") {
	Vector<Vector4> arr = { Vector4(0, 0, 0, 0), Vector4(1, 1, 1, 1) };
	String arg_str = "0, 0, 0, 0, 1, 1, 1, 1";
	test_variant_write_v4("Simple Vector<Vector4>", arr);
}

// color
TEST_CASE("[GDSDecomp][VariantCompat] Vector<Color>") {
	Vector<Color> arr = { Color(0, 0, 0, 0), Color(1, 1, 1, 1) };
	String arg_str = "0, 0, 0, 0, 1, 1, 1, 1";
	test_vector_write_all("Simple Vector<Color>", arr, "ColorArray", "PoolColorArray", arg_str);
}

Variant parse_and_get_variant(const String &str, Variant::Type expected_type) {
	VariantParser::StreamString ss;
	ss.s = str;
	Variant variant;
	String errs;
	int line;
	VariantParserCompat::parse(&ss, variant, errs, line);
	CHECK(errs.is_empty());
	CHECK(variant.get_type() == expected_type);
	return variant;
}

Ref<InputEventKey> parse_and_get_ie_key(const String &str) {
	Ref<InputEventKey> iek = parse_and_get_variant(str, Variant::Type::OBJECT);
	REQUIRE(iek.is_valid());
	return iek;
}

void expect_ie_key(const Ref<InputEventKey> &iek, Key key, int device = 0, bool ctrl_pressed = false, bool shift_pressed = false, bool alt_pressed = false, bool meta_pressed = false) {
	REQUIRE(iek.is_valid());
	CHECK(iek->get_keycode() == key);
	CHECK(iek->get_device() == 0);
	CHECK(iek->is_ctrl_pressed() == ctrl_pressed);
	CHECK(iek->is_shift_pressed() == shift_pressed);
	CHECK(iek->is_alt_pressed() == alt_pressed);
	CHECK(iek->is_meta_pressed() == meta_pressed);
}

void expect_variant_write_match(const Variant &variant, const String &expected_str, int ver_major, bool is_pcfg) {
	String variant_str;
	if (is_pcfg) {
		VariantWriterCompat::write_to_string_pcfg(variant, variant_str, ver_major);
	} else {
		VariantWriterCompat::write_to_string(variant, variant_str, ver_major);
	}
	CHECK(variant_str == expected_str);
}

void expect_variant_decode_encode_match(const Ref<InputEvent> &variant, const String &expected_str, int ver_major, bool is_pcfg) {
	int len;
	bool p_full_objects = false;
	Error err = VariantDecoderCompat::encode_variant_compat(ver_major, variant, nullptr, len, p_full_objects);
	CHECK(err == OK);

	Vector<uint8_t> buff;
	buff.resize(len);

	uint8_t *w = buff.ptrw();
	err = VariantDecoderCompat::encode_variant_compat(ver_major, variant, &w[0], len, p_full_objects);
	CHECK(err == OK);

	Variant decoded;
	err = VariantDecoderCompat::decode_variant_compat(ver_major, decoded, buff.ptr(), len, nullptr, p_full_objects);
	CHECK(err == OK);
	Ref<InputEvent> decoded_ie = decoded;
	REQUIRE(decoded_ie.is_valid());
	CHECK(decoded_ie->as_text() == variant->as_text());

	expect_variant_write_match(decoded_ie, expected_str, ver_major, is_pcfg);
}

inline void expect_iek_decode_encode_write_match(const Ref<InputEvent> &variant, const String &expected_str, int ver_major, bool is_pcfg) {
	expect_variant_write_match(variant, expected_str, ver_major, is_pcfg);
	expect_variant_decode_encode_match(variant, expected_str, ver_major, is_pcfg);
}

void test_iek(const String &fmt, const String &int_fmt, const String &key_str, Key key, bool is_pcfg, int device = 0, bool ctrl_pressed = false, bool shift_pressed = false, bool alt_pressed = false, bool meta_pressed = false) {
	String iek_str = vformat(fmt, key_str);
	Ref<InputEventKey> iek = parse_and_get_ie_key(iek_str);
	expect_ie_key(iek, key, device, ctrl_pressed, shift_pressed, alt_pressed, meta_pressed);
	V2InputEvent::V2KeyList v2_key = InputEventParserV2::convert_v4_key_to_v2_key(key);
	String iek_str_int = vformat(int_fmt, (int)v2_key);
	iek = parse_and_get_ie_key(iek_str_int);
	expect_ie_key(iek, key, device, ctrl_pressed, shift_pressed, alt_pressed, meta_pressed);
	if (!is_pcfg) {
		expect_iek_decode_encode_write_match(iek, iek_str_int, 2, is_pcfg);
	} else {
		expect_iek_decode_encode_write_match(iek, iek_str, 2, is_pcfg);
	}
}

TEST_CASE("[GDSDecomp][VariantCompat] v2 InputEvent") {
	SUBCASE("KEY ALL") {
		for (auto &[key, value] : InputEventParserV2::get_key_code_to_v2_string_map()) {
			test_iek("InputEvent(KEY,%s)", "InputEvent(KEY,%d)", value, key, false);
		}
	}

	SUBCASE("KEY ALL (project config)") {
		for (auto &[key, value] : InputEventParserV2::get_key_code_to_v2_string_map()) {
			test_iek("key(%s)", "key(%d)", value, key, true);
		}
	}

	SUBCASE("KEY ALL MODIFIER FLAGS") {
		for (auto &[key, value] : InputEventParserV2::get_key_code_to_v2_string_map()) {
			test_iek("InputEvent(KEY,%s,CSAM)", "InputEvent(KEY,%d,CSAM)", value, key, false, 0, true, true, true, true);
		}
	}

	SUBCASE("KEY ALL (project config modifier flags)") {
		for (auto &[key, value] : InputEventParserV2::get_key_code_to_v2_string_map()) {
			test_iek("key(%s, CSAM)", "key(%d, CSAM)", value, key, true, 0, true, true, true, true);
		}
	}
	SUBCASE("MBUTTON") {
		static const String iem_str_fmt = "InputEvent(MBUTTON,%d)";
		for (int i = 1; i <= 7; i++) {
			String iem_str = vformat(iem_str_fmt, i);
			Variant iem_parsed = parse_and_get_variant(iem_str, Variant::Type::OBJECT);
			Ref<InputEventMouseButton> iem = iem_parsed;
			REQUIRE(iem.is_valid());
			CHECK(iem->get_button_index() == MouseButton(i));
			CHECK(iem->get_device() == 0);
			expect_iek_decode_encode_write_match(iem, iem_str, 2, false);
		}
	}
	SUBCASE("mbutton (project config)") {
		static const String iem_str_pcfg_fmt = "mbutton(1, %d)";
		for (int i = 1; i <= 7; i++) {
			String iem_str_pcfg = vformat(iem_str_pcfg_fmt, i);
			Variant iem_parsed = parse_and_get_variant(iem_str_pcfg, Variant::Type::OBJECT);
			Ref<InputEventMouseButton> iem = iem_parsed;
			REQUIRE(iem.is_valid());
			CHECK(iem->get_button_index() == MouseButton(i));
			CHECK(iem->get_device() == 1);
			expect_iek_decode_encode_write_match(iem, iem_str_pcfg, 2, true);
		}
	}
	SUBCASE("JBUTTON") {
		static const String iejb_str_fmt = "InputEvent(JBUTTON,%d)";
		for (int i = 0; i < V2InputEvent::JOY_BUTTON_MAX; i++) {
			String iejb_str = vformat(iejb_str_fmt, i);
			Variant iejb_parsed = parse_and_get_variant(iejb_str, Variant::Type::OBJECT);
			Ref<InputEventJoypadButton> iejb = iejb_parsed;
			REQUIRE(iejb.is_valid());
			CHECK(iejb->get_button_index() == JoyButton(i));
			CHECK(iejb->get_device() == 0);
			expect_iek_decode_encode_write_match(iejb, iejb_str, 2, false);
		}
	}
	SUBCASE("jbutton (project config)") {
		static const String iejb_str_pcfg_fmt = "jbutton(1, %d)";
		for (int i = 0; i < V2InputEvent::JOY_BUTTON_MAX; i++) {
			String iejb_str_pcfg = vformat(iejb_str_pcfg_fmt, i);
			Variant iejb_parsed = parse_and_get_variant(iejb_str_pcfg, Variant::Type::OBJECT);
			Ref<InputEventJoypadButton> iejb = iejb_parsed;
			REQUIRE(iejb.is_valid());
			CHECK(iejb->get_button_index() == JoyButton(i));
			CHECK(iejb->get_device() == 1);
			expect_iek_decode_encode_write_match(iejb, iejb_str_pcfg, 2, true);
		}
	}
	SUBCASE("JAXIS") {
		const int jaxis_max = V2InputEvent::JOY_AXIS_MAX;
		static const String iejaxis_str_fmt = "InputEvent(JAXIS,%d,%d)";
		Vector<int> axis_values = { -1, 1 };
		for (int i = 0; i < jaxis_max; i++) {
			for (int axis_value : axis_values) {
				String iejaxis_str = vformat(iejaxis_str_fmt, i, axis_value);
				Variant iejaxis_parsed = parse_and_get_variant(iejaxis_str, Variant::Type::OBJECT);
				Ref<InputEventJoypadMotion> iejaxis = iejaxis_parsed;
				REQUIRE(iejaxis.is_valid());
				CHECK(iejaxis->get_axis() == JoyAxis(i));
				CHECK(iejaxis->get_axis_value() == axis_value);
				CHECK(iejaxis->get_device() == 0);
				expect_iek_decode_encode_write_match(iejaxis, iejaxis_str, 2, false);
			}
		}
	}
	SUBCASE("jaxis (project config)") {
		String iejaxis_str_pcfg = "jaxis(1, 2)";
		Variant iejaxis_parsed = parse_and_get_variant(iejaxis_str_pcfg, Variant::Type::OBJECT);
		Ref<InputEventJoypadMotion> iejaxis = iejaxis_parsed;
		REQUIRE(iejaxis.is_valid());
		CHECK(iejaxis->get_device() == 1);
		CHECK(iejaxis->get_axis() == JoyAxis(1));
		CHECK(iejaxis->get_axis_value() == -1);
		expect_iek_decode_encode_write_match(iejaxis, iejaxis_str_pcfg, 2, true);
	}
}

// Helper functions for image testing
Ref<Image> create_test_image(int width, int height, Image::Format format, const Vector<uint8_t> &data = Vector<uint8_t>()) {
	if (data.is_empty()) {
		Ref<Image> img;
		img.instantiate();
		img->initialize_data(width, height, false, format);
		return img;
	} else {
		return Image::create_from_data(width, height, false, format, data);
	}
}

Ref<Image> create_simple_rgba_image() {
	// Create a simple 2x2 RGBA image with red, green, blue, white pixels
	Vector<uint8_t> data = {
		255, 0, 0, 255, // Red
		0, 255, 0, 255, // Green
		0, 0, 255, 255, // Blue
		255, 255, 255, 255 // White
	};
	return create_test_image(2, 2, Image::FORMAT_RGBA8, data);
}

Ref<Image> create_simple_rgb_image() {
	// Create a simple 2x2 RGB image
	Vector<uint8_t> data = {
		255, 0, 0, // Red
		0, 255, 0, // Green
		0, 0, 255, // Blue
		255, 255, 255 // White
	};
	return create_test_image(2, 2, Image::FORMAT_RGB8, data);
}

Ref<Image> create_simple_grayscale_image() {
	// Create a simple 2x2 grayscale image
	Vector<uint8_t> data = {
		0, // Black
		85, // Dark gray
		170, // Light gray
		255 // White
	};
	return create_test_image(2, 2, Image::FORMAT_L8, data);
}

void expect_image_write_match(const Ref<Image> &img, const String &expected_str, int ver_major, bool is_pcfg) {
	String img_str;
	if (is_pcfg) {
		VariantWriterCompat::write_to_string_pcfg(img, img_str, ver_major);
	} else {
		VariantWriterCompat::write_to_string(img, img_str, ver_major);
	}
	CHECK(img_str == expected_str);
}

Ref<Image> parse_and_get_image(const String &str) {
	VariantParser::StreamString ss;
	ss.s = str;
	Variant variant;
	String errs;
	int line;
	VariantParserCompat::parse(&ss, variant, errs, line);
	CHECK(errs.is_empty());
	CHECK(variant.get_type() == Variant::Type::OBJECT);
	Ref<Image> img = variant;
	REQUIRE(img.is_valid());
	return img;
}

void compare_images(const Ref<Image> &original_img, const Ref<Image> &decoded_image) {
	// Compare image properties
	CHECK(original_img->get_width() == decoded_image->get_width());
	CHECK(original_img->get_height() == decoded_image->get_height());
	CHECK(original_img->get_format() == decoded_image->get_format());
	CHECK(original_img->get_mipmap_count() == decoded_image->get_mipmap_count());

	// Compare image data
	Vector<uint8_t> original_data = original_img->get_data();
	Vector<uint8_t> decoded_data = decoded_image->get_data();
	CHECK(original_data.size() == decoded_data.size());
	for (int i = 0; i < original_data.size(); i++) {
		CHECK(original_data[i] == decoded_data[i]);
	}
}

void expect_image_decode_encode_match(const Ref<Image> &img, const String &expected_str, int ver_major, bool is_pcfg) {
	int len;
	bool p_full_objects = false;
	Error err = VariantDecoderCompat::encode_variant_compat(ver_major, img, nullptr, len, p_full_objects);
	CHECK(err == OK);

	Vector<uint8_t> buff;
	buff.resize(len);

	uint8_t *w = buff.ptrw();
	err = VariantDecoderCompat::encode_variant_compat(ver_major, img, &w[0], len, p_full_objects);
	CHECK(err == OK);

	Variant decoded;
	err = VariantDecoderCompat::decode_variant_compat(ver_major, decoded, buff.ptr(), len, nullptr, p_full_objects);
	CHECK(err == OK);
	Ref<Image> decoded_img = decoded;
	REQUIRE(decoded_img.is_valid());

	compare_images(img, decoded_img);

	expect_image_write_match(decoded_img, expected_str, ver_major, is_pcfg);
}

void test_image_variant(const String &test_name, const Ref<Image> &img, const String &expected_v2_str, const String &expected_v2_pcfg_str, bool compress_lossless = true) {
	SUBCASE(vformat("%s write v2", test_name).utf8().get_data()) {
		expect_image_write_match(img, expected_v2_str, 2, false);
	}

	SUBCASE(vformat("%s write v2 pcfg", test_name).utf8().get_data()) {
		expect_image_write_match(img, expected_v2_pcfg_str, 2, true);
	}

	SUBCASE(vformat("%s decode/encode v2", test_name).utf8().get_data()) {
		expect_image_decode_encode_match(img, expected_v2_str, 2, false);
	}

	SUBCASE(vformat("%s decode/encode v2 pcfg", test_name).utf8().get_data()) {
		expect_image_decode_encode_match(img, expected_v2_pcfg_str, 2, true);
	}

	SUBCASE(vformat("%s parse and compare", test_name).utf8().get_data()) {
		Ref<Image> parsed_img = parse_and_get_image(expected_v2_str);
		compare_images(img, parsed_img);
	}

	SUBCASE(vformat("%s parse and compare pcfg", test_name).utf8().get_data()) {
		Ref<Image> parsed_img = parse_and_get_image(expected_v2_pcfg_str);
		compare_images(img, parsed_img);
	}

	SUBCASE(vformat("%s decode and encode in resources", test_name).utf8().get_data()) {
		Ref<FileAccessBuffer> f = FileAccessBuffer::create();
		bool compress = compress_lossless;
		Error err = ImageParserV2::write_image_v2_to_bin(f, img, compress);
		CHECK(err == OK);
		f->seek(0);
		Variant variant;
		err = ImageParserV2::decode_image_v2(f, variant);
		CHECK(err == OK);
		Ref<Image> decoded_img = variant;
		REQUIRE(decoded_img.is_valid());
		compare_images(img, decoded_img);
		expect_image_write_match(decoded_img, expected_v2_str, 2, false);
	}
}

TEST_CASE("[GDSDecomp][VariantCompat] v2 images") {
	SUBCASE("Image with Mipmaps") {
		Ref<Image> img = create_simple_rgba_image();
		img->generate_mipmaps();
		String data_str;
		auto data = img->get_data();
		for (int i = 0; i < data.size(); i++) {
			if (i > 0) {
				data_str += ", ";
			}
			data_str += itos(data[i]);
		}
		String pcfg_data_str = String::hex_encode_buffer(data.ptr(), data.size()).to_upper();
		String expected_v2 = vformat("Image( 2, 2, %d, RGBA, %s )", img->get_mipmap_count(), data_str);
		String expected_v2_pcfg = vformat("img(rgba, %d, 2, 2, %s)", img->get_mipmap_count(), pcfg_data_str);
		test_image_variant("Image with Mipmaps", img, expected_v2, expected_v2_pcfg, false);
	}
	SUBCASE("Empty Image") {
		Ref<Image> empty_img;
		empty_img.instantiate();
		test_image_variant("Empty Image", empty_img, "Image()", "img()");
	}

	SUBCASE("Simple RGBA Image") {
		Ref<Image> img = create_simple_rgba_image();
		String expected_v2 = "Image( 2, 2, 0, RGBA, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 )";
		String expected_v2_pcfg = "img(rgba, 0, 2, 2, FF0000FF00FF00FF0000FFFFFFFFFFFF)";
		test_image_variant("Simple RGBA Image", img, expected_v2, expected_v2_pcfg);
	}

	SUBCASE("Simple RGB Image") {
		Ref<Image> img = create_simple_rgb_image();
		String expected_v2 = "Image( 2, 2, 0, RGB, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255 )";
		String expected_v2_pcfg = "img(rgb, 0, 2, 2, FF000000FF000000FFFFFFFF)";
		test_image_variant("Simple RGB Image", img, expected_v2, expected_v2_pcfg);
	}

	SUBCASE("Simple Grayscale Image") {
		Ref<Image> img = create_simple_grayscale_image();
		String expected_v2 = "Image( 2, 2, 0, GRAYSCALE, 0, 85, 170, 255 )";
		String expected_v2_pcfg = "img(grayscale, 0, 2, 2, 0055AAFF)";
		test_image_variant("Simple Grayscale Image", img, expected_v2, expected_v2_pcfg);
	}

	SUBCASE("Other formats") {
		for (int i = 0; i < V2Image::IMAGE_FORMAT_V2_MAX; i++) {
			auto fmt = ImageEnumCompat::convert_image_format_enum_v2_to_v4((V2Image::Format)i);
			if (fmt == Image::FORMAT_MAX) {
				continue;
			}
			Ref<Image> img = Image::create_empty(2, 2, false, fmt);

			String data_str;
			auto data = img->get_data();
			for (int i = 0; i < data.size(); i++) {
				if (i > 0) {
					data_str += ", ";
				}
				data_str += itos(data[i]);
			}
			String pcfg_data_str = String::hex_encode_buffer(data.ptr(), data.size()).to_upper();

			String format_identifier = ImageEnumCompat::get_v2_format_identifier((V2Image::Format)i);

			String expected_v2 = vformat("Image( 2, 2, %d, %s, %s )", img->get_mipmap_count(), format_identifier, data_str);
			String expected_v2_pcfg = vformat("img(%s, %d, 2, 2, %s)", format_identifier.to_lower(), img->get_mipmap_count(), pcfg_data_str);
			test_image_variant("Other formats: " + format_identifier, img, expected_v2, expected_v2_pcfg);
		}
	}

	SUBCASE("Parse Image from String") {
		String img_str = "Image( 2, 2, 0, RGBA, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 )";
		Ref<Image> parsed_img = parse_and_get_image(img_str);
		CHECK(parsed_img->get_width() == 2);
		CHECK(parsed_img->get_height() == 2);
		CHECK(parsed_img->get_format() == Image::FORMAT_RGBA8);
		CHECK(parsed_img->get_mipmap_count() == 0);

		Vector<uint8_t> data = parsed_img->get_data();
		CHECK(data.size() == 16); // 2x2x4 bytes
		CHECK(data[0] == 255); // Red pixel R
		CHECK(data[1] == 0); // Red pixel G
		CHECK(data[2] == 0); // Red pixel B
		CHECK(data[3] == 255); // Red pixel A
	}

	SUBCASE("Parse Image from String (pcfg format)") {
		String img_str = "img(rgba, 0, 2, 2, FF0000FF00FF00FF0000FFFFFFFFFFFF)";
		Ref<Image> parsed_img = parse_and_get_image(img_str);
		CHECK(parsed_img->get_width() == 2);
		CHECK(parsed_img->get_height() == 2);
		CHECK(parsed_img->get_format() == Image::FORMAT_RGBA8);
		CHECK(parsed_img->get_mipmap_count() == 0);

		Vector<uint8_t> data = parsed_img->get_data();
		CHECK(data.size() == 16); // 2x2x4 bytes
		CHECK(data[0] == 255); // Red pixel R
		CHECK(data[1] == 0); // Red pixel G
		CHECK(data[2] == 0); // Red pixel B
		CHECK(data[3] == 255); // Red pixel A
	}

	SUBCASE("Parse Empty Image") {
		String img_str = "Image()";
		Ref<Image> parsed_img = parse_and_get_image(img_str);
		CHECK(parsed_img->is_empty());
	}

	SUBCASE("Parse Empty Image (pcfg format)") {
		String img_str = "img()";
		Ref<Image> parsed_img = parse_and_get_image(img_str);
		CHECK(parsed_img->is_empty());
	}

	SUBCASE("Roundtrip Test - Write then Parse") {
		Ref<Image> original_img = create_simple_rgba_image();
		String written_str;
		VariantWriterCompat::write_to_string(original_img, written_str, 2);

		Ref<Image> parsed_img = parse_and_get_image(written_str);

		// Compare properties
		CHECK(parsed_img->get_width() == original_img->get_width());
		CHECK(parsed_img->get_height() == original_img->get_height());
		CHECK(parsed_img->get_format() == original_img->get_format());
		CHECK(parsed_img->get_mipmap_count() == original_img->get_mipmap_count());

		// Compare data
		Vector<uint8_t> original_data = original_img->get_data();
		Vector<uint8_t> parsed_data = parsed_img->get_data();
		CHECK(original_data.size() == parsed_data.size());
		for (int i = 0; i < original_data.size(); i++) {
			CHECK(original_data[i] == parsed_data[i]);
		}
	}
}

// The following tests are pilfered from test_variant.h, repurposed for VariantCompat.

TEST_CASE("[GDSDecomp][VariantCompat] Writer and parser integer") {
	int64_t a32 = 2147483648; // 2^31, so out of bounds for 32-bit signed int [-2^31, +2^31-1].
	String a32_str;
	VariantWriterCompat::write_to_string(a32, a32_str, 4);

	CHECK_MESSAGE(a32_str != "-2147483648", "Should not wrap around");

	int64_t b64 = 9223372036854775807; // 2^63-1, upper bound for signed 64-bit int.
	String b64_str;
	VariantWriterCompat::write_to_string(b64, b64_str, 4);

	CHECK_MESSAGE(b64_str == "9223372036854775807", "Should not wrap around.");

	VariantParser::StreamString ss;
	String errs;
	int line;
	Variant b64_parsed;
	int64_t b64_int_parsed;

	ss.s = b64_str;
	VariantParserCompat::parse(&ss, b64_parsed, errs, line);
	b64_int_parsed = b64_parsed;

	CHECK_MESSAGE(b64_int_parsed == 9223372036854775807, "Should parse back.");

	ss.s = "9223372036854775808"; // Overflowed by one.
	VariantParserCompat::parse(&ss, b64_parsed, errs, line);
	b64_int_parsed = b64_parsed;

	CHECK_MESSAGE(b64_int_parsed == 9223372036854775807, "The result should be clamped to max value.");

	ss.s = "1e100"; // Googol! Scientific notation.
	VariantParserCompat::parse(&ss, b64_parsed, errs, line);
	b64_int_parsed = b64_parsed;

	CHECK_MESSAGE(b64_int_parsed == 9223372036854775807, "The result should be clamped to max value.");
}

TEST_CASE("[GDSDecomp][VariantCompat] Writer and parser Variant::FLOAT") {
	// Variant::FLOAT is always 64-bit (C++ double).
	// This is the maximum non-infinity double-precision float.
	double a64 = 179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.0;
	String a64_str;
	VariantWriterCompat::write_to_string(a64, a64_str, 4);

	CHECK_MESSAGE(a64_str == "1.7976931348623157e+308", "Writes in scientific notation.");
	CHECK_MESSAGE(a64_str != "inf", "Should not overflow.");
	CHECK_MESSAGE(a64_str != "nan", "The result should be defined.");

	String errs;
	int line;
	Variant variant_parsed;
	double float_parsed;

	VariantParser::StreamString bss;
	bss.s = a64_str;
	VariantParserCompat::parse(&bss, variant_parsed, errs, line);
	float_parsed = variant_parsed;
	// Loses precision, but that's alright.
	CHECK_MESSAGE(float_parsed == 1.797693134862315708145274237317e+308, "Should parse back.");

	// Approximation of Googol with a double-precision float.
	VariantParser::StreamString css;
	css.s = "1.0e+100";
	VariantParserCompat::parse(&css, variant_parsed, errs, line);
	float_parsed = variant_parsed;
	CHECK_MESSAGE(float_parsed == 1.0e+100, "Should match the double literal.");
}

TEST_CASE("[GDSDecomp][VariantCompat] Writer and parser Vector2") {
	Variant vec2_parsed;
	String vec2_str;
	String errs;
	int line;
	// Variant::VECTOR2 and Vector2 can be either 32-bit or 64-bit depending on the precision level of real_t.
	{
		Vector2 vec2 = Vector2(1.2, 3.4);
		VariantWriterCompat::write_to_string(vec2, vec2_str, 4);
		// Reminder: "1.2" and "3.4" are not exactly those decimal numbers. They are the closest float to them.
		CHECK_MESSAGE(vec2_str == "Vector2(1.2, 3.4)", "Should write with enough digits to ensure parsing back is exact.");
		VariantParser::StreamString stream;
		stream.s = vec2_str;
		VariantParserCompat::parse(&stream, vec2_parsed, errs, line);
		CHECK_MESSAGE(Vector2(vec2_parsed) == vec2, "Should parse back to the same Vector2.");
	}
	// Check with big numbers and small numbers.
	{
		Vector2 vec2 = Vector2(1.234567898765432123456789e30, 1.234567898765432123456789e-10);
		VariantWriterCompat::write_to_string(vec2, vec2_str, 4);
#ifdef REAL_T_IS_DOUBLE
		CHECK_MESSAGE(vec2_str == "Vector2(1.2345678987654322e+30, 1.2345678987654322e-10)", "Should write with enough digits to ensure parsing back is exact.");
#else
		CHECK_MESSAGE(vec2_str == "Vector2(1.2345679e+30, 1.2345679e-10)", "Should write with enough digits to ensure parsing back is exact.");
#endif
		VariantParser::StreamString stream;
		stream.s = vec2_str;
		VariantParserCompat::parse(&stream, vec2_parsed, errs, line);
		CHECK_MESSAGE(Vector2(vec2_parsed) == vec2, "Should parse back to the same Vector2.");
	}
}

TEST_CASE("[GDSDecomp][VariantCompat] Writer and parser array") {
	Array a = build_array(1, String("hello"), build_array(Variant()));
	String a_str;
	VariantWriterCompat::write_to_string(a, a_str, 4);

	CHECK_EQ(a_str, "[1, \"hello\", [null]]");

	VariantParser::StreamString ss;
	String errs;
	int line;
	Variant a_parsed;

	ss.s = a_str;
	VariantParserCompat::parse(&ss, a_parsed, errs, line);

	CHECK_MESSAGE(a_parsed == Variant(a), "Should parse back.");
}

TEST_CASE("[GDSDecomp][VariantCompat] Writer recursive array") {
	// There is no way to accurately represent a recursive array,
	// the only thing we can do is make sure the writer doesn't blow up

	// Self recursive
	Array a;
	a.push_back(a);

	// Writer should it recursion limit while visiting the array
	ERR_PRINT_OFF;
	String a_str;
	VariantWriterCompat::write_to_string(a, a_str, 4);
	ERR_PRINT_ON;

	// Nested recursive
	Array a1;
	Array a2;
	a1.push_back(a2);
	a2.push_back(a1);

	// Writer should it recursion limit while visiting the array
	ERR_PRINT_OFF;
	String a1_str;
	VariantWriterCompat::write_to_string(a1, a1_str, 4);
	ERR_PRINT_ON;

	// Break the recursivity otherwise Dictionary tearndown will leak memory
	a.clear();
	a1.clear();
	a2.clear();
}

TEST_CASE("[GDSDecomp][VariantCompat] Writer and parser dictionary") {
	// d = {{1: 2}: 3, 4: "hello", 5: {null: []}}
	Dictionary d = build_dictionary(build_dictionary(1, 2), 3, 4, String("hello"), 5, build_dictionary(Variant(), build_array()));
	String d_str;
	VariantWriterCompat::write_to_string(d, d_str, 4);

	CHECK_EQ(d_str, "{\n4: \"hello\",\n5: {\nnull: []\n},\n{\n1: 2\n}: 3\n}");

	VariantParser::StreamString ss;
	String errs;
	int line;
	Variant d_parsed;

	ss.s = d_str;
	VariantParserCompat::parse(&ss, d_parsed, errs, line);

	CHECK_MESSAGE(d_parsed == Variant(d), "Should parse back.");
}

TEST_CASE("[GDSDecomp][VariantCompat] Writer key sorting") {
	Dictionary d = build_dictionary(StringName("C"), 3, "A", 1, StringName("B"), 2, "D", 4);
	String d_str;
	VariantWriterCompat::write_to_string(d, d_str, 4);

	CHECK_EQ(d_str, "{\n\"A\": 1,\n&\"B\": 2,\n&\"C\": 3,\n\"D\": 4\n}");
}

TEST_CASE("[GDSDecomp][VariantCompat] Writer recursive dictionary") {
	// There is no way to accurately represent a recursive dictionary,
	// the only thing we can do is make sure the writer doesn't blow up

	// Self recursive
	Dictionary d;
	d[1] = d;

	// Writer should it recursion limit while visiting the dictionary
	ERR_PRINT_OFF;
	String d_str;
	VariantWriterCompat::write_to_string(d, d_str, 4);
	ERR_PRINT_ON;

	// Nested recursive
	Dictionary d1;
	Dictionary d2;
	d1[2] = d2;
	d2[1] = d1;

	// Writer should it recursion limit while visiting the dictionary
	ERR_PRINT_OFF;
	String d1_str;
	VariantWriterCompat::write_to_string(d1, d1_str, 4);
	ERR_PRINT_ON;

	// Break the recursivity otherwise Dictionary tearndown will leak memory
	d.clear();
	d1.clear();
	d2.clear();
}

} //namespace TestVariantCompat
#endif //TEST_VARIANT_COMPAT_H
