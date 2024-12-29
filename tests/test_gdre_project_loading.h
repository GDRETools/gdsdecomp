#pragma once
#include "../compat/fake_scene_state.h"
#include "../compat/resource_compat_binary.h"
#include "../compat/resource_compat_text.h"

#include "../../../../../../../../Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/v1/array"
#include "test_common.h"
#include "tests/test_macros.h"

#include <core/io/pck_packer.h>
#include <core/io/resource_format_binary.h>
#include <modules/gdscript/gdscript_tokenizer_buffer.h>
#include <scene/resources/resource_format_text.h>

#include <core/version_generated.gen.h>

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

TEST_CASE("[GDSDecomp][ProjectConfigLoader] loading example from current engine") {
	CHECK(ProjectSettings::get_singleton());
	auto text_project_path = get_tmp_path().path_join("project.godot");
	ProjectSettings::get_singleton()->save_custom(text_project_path);
	auto binary_project_path = get_tmp_path().path_join("project.binary");
	ProjectSettings::get_singleton()->save_custom(binary_project_path);

	SUBCASE("Text project loading") {
		ProjectConfigLoader loader;
		CHECK(loader.load_cfb(text_project_path, VERSION_MAJOR, VERSION_MINOR) == OK);
		CHECK(loader.get_config_version() == ProjectConfigLoader::CURRENT_CONFIG_VERSION);
	}
	SUBCASE("Binary project loading") {
		ProjectConfigLoader loader;
		CHECK(loader.load_cfb(binary_project_path, VERSION_MAJOR, VERSION_MINOR) == OK);
		// config version isn't saved in binary
		CHECK(loader.get_config_version() == 0);
	}
	SUBCASE("Text project saving") {
		ProjectConfigLoader loader;
		CHECK(loader.load_cfb(binary_project_path, VERSION_MAJOR, VERSION_MINOR) == OK);
		PackedStringArray engine_features = loader.get_setting("application/config/features", PackedStringArray());
		CHECK(engine_features.size() >= 1);
		String engine_version = engine_features[0];
		auto temp_project_dir = get_tmp_path().path_join("new_project");
		CHECK(gdre::ensure_dir(temp_project_dir) == OK);
		auto engine_feature = "engine/editor/feature/3d";
		CHECK(loader.save_cfb(temp_project_dir, VERSION_MAJOR, VERSION_MINOR) == OK);
		auto new_project_path = temp_project_dir.path_join("project.godot");
		auto old_project_text = FileAccess::get_file_as_string(text_project_path);
		auto new_project_text = FileAccess::get_file_as_string(new_project_path);
		CHECK(old_project_text == new_project_text);
		gdre::rimraf(temp_project_dir);
	}
	gdre::rimraf(text_project_path);
	gdre::rimraf(binary_project_path);
}
TEST_CASE("[GDSDecomp] GDRESettings project loading") {
	auto tmp_pck_path = get_tmp_path().path_join("test.pck");
	auto tmp_project_path = get_tmp_path().path_join("project.binary");

	auto target_project_path = "res://project.binary";
	auto test_script_path = get_gdsdecomp_path().path_join("helpers/has_char.gd");
	auto target_script_path = "res://helpers/has_char.gd";

	PCKPacker pck;
	pck.pck_start(tmp_pck_path, 32);
	CHECK(ProjectSettings::get_singleton());
	ProjectSettings::get_singleton()->save_custom(tmp_project_path);
	pck.add_file(target_project_path, tmp_project_path);
	pck.add_file(target_script_path, test_script_path);
	pck.flush(true);

	CHECK(FileAccess::exists(tmp_pck_path));

	GDRESettings *settings = GDRESettings::get_singleton();
	CHECK(settings->load_project({ tmp_pck_path }, false) == OK);

	CHECK(settings->get_pack_path() == tmp_pck_path);

	CHECK(settings->get_ver_major() == VERSION_MAJOR);
	CHECK(settings->get_ver_minor() == VERSION_MINOR);
	CHECK(settings->get_ver_rev() == VERSION_PATCH);

	SUBCASE("Detected correct bytecode revision") {
		CHECK(settings->get_bytecode_revision() != 0);
		auto decomp = GDScriptDecomp::create_decomp_for_version(vformat("%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH));
		CHECK(settings->get_bytecode_revision() == decomp->get_bytecode_rev());
	}
	SUBCASE("Check if project has files") {
		CHECK(settings->has_res_path(target_project_path));
		CHECK(settings->has_res_path(target_script_path));
		CHECK(FileAccess::exists(target_project_path));
		CHECK(FileAccess::exists(target_script_path));
	}
	CHECK(settings->unload_project() == OK);
	gdre::rimraf(tmp_project_path);
	gdre::rimraf(tmp_pck_path);
}
