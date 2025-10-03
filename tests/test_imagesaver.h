#pragma once
#include "test_common.h"

#include "core/io/image.h"
#include "tests/test_macros.h"
#include "utility/common.h"
#include "utility/image_saver.h"
namespace TestImageSaver {
void test_svg_saving(const String &file, const String &test_files_dir, const String &output_dir) {
	Ref<Image> img = Image::load_from_file(test_files_dir.path_join(file));
	REQUIRE(img.is_valid());
	CHECK(ImageSaver::save_image(output_dir.path_join(file), img, false) == OK);

	Ref<Image> resaved_image = Image::load_from_file(output_dir.path_join(file));
	REQUIRE(resaved_image.is_valid());
#ifdef DEBUG_ENABLED
	if (resaved_image->get_data() != img->get_data()) {
		// save them both as pngs to the output path
		auto a_path = output_dir.path_join(file.get_basename() + ".png");
		auto b_path = output_dir.path_join(file.get_basename() + "_resaved.png");
		img->save_png(a_path);
		resaved_image->save_png(b_path);
		for (int i = 0; i < img->get_width(); i++) {
			for (int j = 0; j < img->get_height(); j++) {
				CHECK(img->get_pixel(i, j) == resaved_image->get_pixel(i, j));
			}
		}
	}
#endif
	// the svg that we save should be pixel-for-pixel accurate to the original when loaded as raster images
	CHECK(resaved_image->get_data() == img->get_data());
}
} //namespace TestImageSaver

TEST_CASE("[GDSDecomp][ImageSaver] Test SVG saving") {
	auto editor_icons_path = GDRESettings::get_singleton()->get_cwd().path_join("editor/icons");
	auto output_path = get_tmp_path().path_join("image_saver");
	gdre::rimraf(output_path);
	gdre::ensure_dir(output_path);
	auto files = gdre::get_files_at(editor_icons_path, { "*.svg" }, false);
	for (const String &file : files) {
		SUBCASE(file.utf8().get_data()) {
			TestImageSaver::test_svg_saving(file, editor_icons_path, output_path);
		}
	}
}

TEST_CASE("[GDSDecomp][ImageSaver] Test saving images") {
	Ref<Image> img = Image::create_empty(100, 100, false, Image::FORMAT_RGB8);
	img->fill(Color(1, 0, 0));
	String temp_dir = get_tmp_path().path_join("image_saver");
	gdre::rimraf(temp_dir);
	gdre::ensure_dir(temp_dir);
	auto exts = ImageSaver::get_supported_extensions();
	for (const String &ext : exts) {
		SUBCASE(vformat("Image saver can save in format %s", ext).utf8().get_data()) {
			String temp_path = temp_dir.path_join(vformat("test.%s", ext));
			CHECK(ImageSaver::save_image(temp_path, img, false) == OK);
			CHECK(FileAccess::get_file_as_bytes(temp_path).size() > 0);
		}
	}
}
