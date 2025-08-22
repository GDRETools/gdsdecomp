#pragma once
#include "test_common.h"

#include "core/io/image.h"
#include "tests/test_macros.h"
#include "utility/image_saver.h"

TEST_CASE("[GDSDecomp][ImageSaver] Test saving images") {
	Ref<Image> img = Image::create_empty(100, 100, false, Image::FORMAT_RGB8);
	img->fill(Color(1, 0, 0));
	String temp_dir = get_tmp_path().path_join("image_saver");
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
