#pragma once
#include "../compat/fake_scene_state.h"
#include "../compat/resource_compat_binary.h"
#include "../compat/resource_compat_text.h"

#include "test_common.h"
#include "tests/test_macros.h"

#include <core/io/pck_packer.h>
#include <core/io/resource_format_binary.h>
#include <modules/gdscript/gdscript_tokenizer_buffer.h>
#include <scene/resources/resource_format_text.h>

#include "core/version_generated.gen.h"
#include <utility/file_access_gdre.h>
#include <utility/import_exporter.h>
#include <utility/pck_dumper.h>

inline Error create_test_pck(const String &pck_path, const HashMap<String, String> &paths) {
	PCKPacker pck;
	Error err = pck.pck_start(pck_path, 32);
	ERR_FAIL_COND_V(err, err);
	for (const auto &path : paths) {
		err = pck.add_file(path.key, path.value);
		ERR_FAIL_COND_V(err, err);
	}
	err = pck.flush(false);
	ERR_FAIL_COND_V(err, err);
	if (!FileAccess::exists(pck_path)) {
		return ERR_FILE_NOT_FOUND;
	}
	return err;
}

inline Error store_file_as_string(const String &path, const String &content) {
	ERR_FAIL_COND_V(gdre::ensure_dir(path.get_base_dir()) != OK, ERR_FILE_CANT_WRITE);
	auto fa = FileAccess::open(path, FileAccess::WRITE);
	ERR_FAIL_COND_V(fa.is_null(), ERR_FILE_CANT_OPEN);
	ERR_FAIL_COND_V(!fa->store_string(content), ERR_FILE_CANT_WRITE);
	fa->flush();
	fa->close();
	return OK;
}

inline void test_pck_files(HashMap<String, String> files) {
	for (const auto &file : files) {
		CHECK(FileAccess::exists(file.key));
		auto fa = FileAccess::open(file.key, FileAccess::READ);
		CHECK(fa.is_valid());
		CHECK(Ref<FileAccessGDRE>(fa).is_valid());
		auto content = fa->get_buffer(fa->get_length());

		auto fa_old = FileAccess::open(file.value, FileAccess::READ);
		CHECK(fa_old.is_valid());
		CHECK(!Ref<FileAccessGDRE>(fa_old).is_valid());
		auto old_content = fa_old->get_buffer(fa_old->get_length());
		CHECK(content == old_content);
	}
}

TEST_CASE("[GDSDecomp] GDRESettings works") {
	REQUIRE(GDRESettings::get_singleton());
	GDRESettings *settings = GDRESettings::get_singleton();
	CHECK(settings->get_cwd() != "");
	auto cwd = settings->get_cwd();
	auto gdsdecomp_path = get_gdsdecomp_path();
	// check that modules/gdsdecomp exists
	auto da = DirAccess::open(gdsdecomp_path);
	CHECK(da.is_valid());
	CHECK(da->dir_exists(gdsdecomp_path));
}

TEST_CASE("[GDSDecomp][ProjectConfigLoader] loading example from current engine") {
	REQUIRE(ProjectSettings::get_singleton());
	CHECK(gdre::ensure_dir(get_tmp_path()) == OK);
	auto text_project_path = get_tmp_path().path_join("project.godot");
	ProjectSettings::get_singleton()->save_custom(text_project_path);
	auto binary_project_path = get_tmp_path().path_join("project.binary");
	ProjectSettings::get_singleton()->save_custom(binary_project_path);

	SUBCASE("Text project loading") {
		ProjectConfigLoader loader;
		CHECK(loader.load_cfb(text_project_path, GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR) == OK);
		CHECK(loader.get_config_version() == ProjectConfigLoader::CURRENT_CONFIG_VERSION);
	}
	SUBCASE("Binary project loading") {
		ProjectConfigLoader loader;
		CHECK(loader.load_cfb(binary_project_path, GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR) == OK);
		// config version isn't saved in binary
		CHECK(loader.get_config_version() == 0);
	}
	SUBCASE("Text project saving") {
		ProjectConfigLoader loader;
		CHECK(loader.load_cfb(binary_project_path, GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR) == OK);
		PackedStringArray engine_features = loader.get_setting("application/config/features", PackedStringArray());
		CHECK(engine_features.size() >= 1);
		String engine_version = engine_features[0];
		auto temp_project_dir = get_tmp_path().path_join("new_project");
		CHECK(gdre::ensure_dir(temp_project_dir) == OK);
		CHECK(loader.save_cfb(temp_project_dir, GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR) == OK);
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
	REQUIRE(gdre::ensure_dir(get_tmp_path()) == OK);
	auto tmp_pck_path = get_tmp_path().path_join("test.pck");
	auto tmp_project_path = get_tmp_path().path_join("project.binary");
	REQUIRE(ProjectSettings::get_singleton());
	ProjectSettings::get_singleton()->save_custom(tmp_project_path);

	HashMap<String, String> files = {
		{ "res://project.binary", tmp_project_path }
	};
	CHECK(create_test_pck(tmp_pck_path, files) == OK);

	GDRESettings *settings = GDRESettings::get_singleton();
	REQUIRE(settings);
	CHECK(settings->load_project({ tmp_pck_path }, false) == OK);
	CHECK(settings->is_pack_loaded());
	CHECK(settings->pack_has_project_config());
	CHECK(settings->is_project_config_loaded());
	CHECK(settings->get_pack_path() == tmp_pck_path);

	CHECK(settings->get_ver_major() == GODOT_VERSION_MAJOR);
	CHECK(settings->get_ver_minor() == GODOT_VERSION_MINOR);
	auto decomp = GDScriptDecomp::create_decomp_for_version(vformat("%d.%d.%d", GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR, GODOT_VERSION_PATCH));
	REQUIRE(decomp.is_valid());
	CHECK(settings->get_bytecode_revision() != 0);
	CHECK(settings->get_bytecode_revision() == decomp->get_bytecode_rev());
	for (const auto &file : files) {
		CHECK(settings->has_path_loaded(file.key));
	}
	test_pck_files(files);
	CHECK(settings->unload_project() == OK);
	gdre::rimraf(tmp_project_path);
	gdre::rimraf(tmp_pck_path);
}

TEST_CASE("[GDSDecomp] FileAccessGDRE tests") {
	CHECK(gdre::ensure_dir(get_tmp_path()) == OK);
	auto tmp_pck_path = get_tmp_path().path_join("FileAccessGDRETest.pck");
	auto tmp_test_file = get_tmp_path().path_join("test.txt");

	CHECK(store_file_as_string(tmp_test_file, "dummy") == OK);
	HashMap<String, String> files = {
		{ "res://test.txt", tmp_test_file }
	};

	CHECK(create_test_pck(tmp_pck_path, files) == OK);

	CHECK(!FileAccess::exists("res://test.txt"));
	CHECK(GDREPackedData::get_current_dir_access_class(DirAccess::ACCESS_RESOURCES) == GDREPackedData::get_os_dir_access_class_name());
	CHECK(GDREPackedData::get_current_file_access_class(FileAccess::ACCESS_RESOURCES) == GDREPackedData::get_os_file_access_class_name());

	auto settings = GDRESettings::get_singleton();
	REQUIRE(settings);
	CHECK(settings->load_project({ tmp_pck_path }, false) == OK);
	CHECK(GDREPackedData::get_current_dir_access_class(DirAccess::ACCESS_RESOURCES) == "DirAccessGDRE");
	CHECK(GDREPackedData::get_current_file_access_class(FileAccess::ACCESS_RESOURCES) == "FileAccessGDRE");

	test_pck_files(files);

	CHECK(settings->unload_project() == OK);
	CHECK(GDREPackedData::get_current_dir_access_class(DirAccess::ACCESS_RESOURCES) == GDREPackedData::get_os_dir_access_class_name());
	CHECK(GDREPackedData::get_current_file_access_class(FileAccess::ACCESS_RESOURCES) == GDREPackedData::get_os_file_access_class_name());

	gdre::rimraf(tmp_test_file);
	gdre::rimraf(tmp_pck_path);
}

// Disabling this for now; fragile and kind of redundant.
#if 0
static constexpr const char *const export_presets =
		R"([preset.0]

name="Dumb"
platform="Windows Desktop"
runnable=true
advanced_options=false
dedicated_server=false
custom_features=""
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path="test.exe"
encryption_include_filters=""
encryption_exclude_filters=""
encrypt_pck=false
encrypt_directory=false
script_export_mode=1

[preset.0.options]
texture_format/bptc=false
texture_format/s3tc=true
texture_format/etc=false
texture_format/etc2=false
texture_format/no_bptc_fallbacks=true
)";

TEST_CASE("[GDSDecomp] uh oh") {
	REQUIRE(GDRESettings::get_singleton());
	CHECK(gdre::ensure_dir(get_tmp_path()) == OK);
	// get the path to the currently executing binary
	String gdscript_tests_path = "modules/gdscript/tests/scripts";

	auto scripts_path = GDRESettings::get_singleton()->get_cwd().path_join(gdscript_tests_path);
	auto da = DirAccess::open(scripts_path);
	CHECK(da.is_valid());
	CHECK(da->dir_exists(scripts_path));
	// copy the scripts dir to a temporary directory
	auto temp_scripts_path = get_tmp_path().path_join("gdscripts_project");
	CHECK(da->copy_dir(scripts_path, temp_scripts_path) == OK);
	CHECK(store_file_as_string(temp_scripts_path.path_join("export_presets.cfg"), export_presets) == OK);

	auto exec_path = OS::get_singleton()->get_executable_path();
	auto pck_path = get_tmp_path().path_join("gdscripts.pck");
	List<String> args = { "--headless", "-e", "--path", temp_scripts_path, "--export-pack", "Dumb", pck_path };
	String pipe;
	Error export_cmd_error = OS::get_singleton()->execute(exec_path, args, &pipe);
	if (export_cmd_error) {
		print_line(pipe);
	}
	CHECK(export_cmd_error == OK);
	// split the output into lines
	auto lines = pipe.split("\n");
	HashSet<String> packed_files;
	for (const auto &line : lines) {
		// check if the line is a file path
		String ln = line.strip_edges();
		if (ln.begins_with("savepack:") && ln.contains("Storing File: ")) {
			// get the file path
			auto file = line.get_slice("Storing File: ", 1).strip_edges();
			packed_files.insert(file);
		}
	}
	auto settings = GDRESettings::get_singleton();
	CHECK(settings->load_project({ pck_path }, false) == OK);
	CHECK(settings->is_pack_loaded());
	CHECK(settings->pack_has_project_config());
	CHECK(settings->is_project_config_loaded());
	CHECK(settings->get_pack_path() == pck_path);
	CHECK(settings->get_ver_major() == GODOT_VERSION_MAJOR);
	CHECK(settings->get_ver_minor() == GODOT_VERSION_MINOR);
	auto decomp = GDScriptDecomp::create_decomp_for_version(vformat("%d.%d.%d", GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR, GODOT_VERSION_PATCH));
	CHECK(decomp.is_valid());
	CHECK(settings->get_bytecode_revision() != 0);
	CHECK(settings->get_bytecode_revision() == decomp->get_bytecode_rev());
	for (const auto &file : packed_files) {
		CHECK(settings->has_path_loaded(file));
	}
	String output_dir = get_tmp_path().path_join("gdscripts_project_decompiled");
	PckDumper dumper;
	Vector<String> broken_files;
	int checked_files;
	CHECK(dumper._check_md5_all_files(broken_files, checked_files, nullptr) == OK);
	CHECK(broken_files.size() == 0);
	CHECK(dumper.pck_dump_to_dir(output_dir, {}) == OK);
	HashMap<String, String> pck_files;
	for (const auto &file : packed_files) {
		String output_file_path = output_dir.path_join(file.trim_prefix("res://"));
		pck_files[file] = output_file_path;
	}
	test_pck_files(pck_files);
	ImportExporter import_exporter;
	CHECK(import_exporter.export_imports(output_dir, {}) == OK);
	auto export_report = import_exporter.get_report();
	CHECK(export_report->get_failed().size() == 0);
	CHECK(settings->unload_project() == OK);
	gdre::rimraf(output_dir);
	gdre::rimraf(pck_path);
	gdre::rimraf(temp_scripts_path);
}
#endif
