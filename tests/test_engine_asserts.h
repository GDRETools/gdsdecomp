#pragma once
#include "../compat/fake_scene_state.h"
#include "../compat/resource_compat_binary.h"
#include "../compat/resource_compat_text.h"

#include "test_common.h"
#include "tests/test_macros.h"

#include <core/io/resource_format_binary.h>
#include <scene/resources/resource_format_text.h>

namespace TestEngineAsserts {

TEST_CASE("[GDSDecomp] GDRESettings works") {
	GDRESettings *settings = GDRESettings::get_singleton();
	CHECK(settings != nullptr);
	CHECK(settings->get_cwd() != "");
	auto cwd = settings->get_cwd();
	auto gdsdecomp_path = get_gdsdecomp_path();
	// check that modules/gdsdecomp exists
	auto da = DirAccess::open(gdsdecomp_path);
	CHECK(da.is_valid());
	CHECK(da->dir_exists(gdsdecomp_path));
}

TEST_CASE("[GDSDecomp][SceneState] SceneState packed version format hasn't changed") {
	Ref<PackedScene> packed_scene;
	packed_scene.instantiate();
	auto state = packed_scene->get_state();
	auto d = state->get_bundled_scene();
	CHECK(d.has("version"));
	int version = d["version"];
	CHECK(version == SceneStateInstanceGetter::CURRENT_PACKED_SCENE_VERSION);
}

TEST_CASE("[GDSDecomp][ResourceFormatLoaderCompatBinary] ResourceFormatLoaderCompatBinary can load a resource") {
	ResourceFormatSaverBinaryInstance saver;
	Ref<Resource> resource;
	resource.instantiate();
	auto temp_path = get_tmp_path().path_join("test.res");
	Error error = saver.save(temp_path, resource, 0);
	CHECK(error == OK);

	ResourceFormatLoaderCompatBinary loader;
	SUBCASE("Binary format version hasn't changed") {
		auto res_info = loader.get_resource_info(temp_path, &error);
		CHECK(error == OK);
		CHECK(res_info.ver_format == ResourceLoaderCompatBinary::get_current_format_version());
	}
	SUBCASE("ResourceInfo has other correct info") {
		auto res_info = loader.get_resource_info(temp_path, &error);
		CHECK(error == OK);
		CHECK(res_info.resource_format == "binary");
		CHECK(res_info.type == "Resource");
	}
	// gdre::rimraf(temp_path);
}
TEST_CASE("[GDSDecomp][ResourceFormatLoaderCompatText] ResourceFormatLoaderCompatBinary can load a resource") {
	SUBCASE("Text format version hasn't changed") {
		CHECK(ResourceLoaderText::FORMAT_VERSION == ResourceLoaderCompatText::FORMAT_VERSION);
	}
	ResourceFormatSaverTextInstance saver;
	Ref<Resource> resource;
	resource.instantiate();
	// force the saver to save as newest version
	resource->set_meta("_test", PackedVector4Array{ Vector4{} });
	auto temp_path = get_tmp_path().path_join("test.tres");
	Error error = saver.save(temp_path, resource, 0);
	CHECK(error == OK);
	ResourceFormatLoaderCompatText loader;
	SUBCASE("ResourceInfo has correct info") {
		auto res_info = loader.get_resource_info(temp_path, &error);
		CHECK(error == OK);
		CHECK(res_info.ver_format == ResourceLoaderCompatText::FORMAT_VERSION);
		CHECK(res_info.resource_format == "text");
		CHECK(res_info.type == "Resource");
	}
	// gdre::rimraf(temp_path);
}

} //namespace TestEngineAsserts
