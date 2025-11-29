#pragma once

#include "compat/oggstr_loader_compat.h"
#include "core/io/json.h"
#include "exporters/fontfile_exporter.h"
#include "exporters/gdscript_exporter.h"
#include "exporters/mp3str_exporter.h"
#include "exporters/obj_exporter.h"
#include "exporters/oggstr_exporter.h"
#include "exporters/sample_exporter.h"
#include "exporters/texture_exporter.h"
#include "exporters/translation_exporter.h"
#include "test_common.h"
#include "tests/test_macros.h"
#include "utility/common.h"
#include "utility/pck_dumper.h"
#include <compat/resource_compat_text.h>
#include <compat/resource_loader_compat.h>
#include <modules/gdsdecomp/exporters/resource_exporter.h>

#include <modules/minimp3/audio_stream_mp3.h>
#include <scene/resources/audio_stream_wav.h>
namespace TestProjectExport {

String get_original_import_path(const Ref<ExportReport> &export_report, const String &original_extract_dir) {
	auto source_file = export_report->get_import_info()->get_source_file();
	return original_extract_dir.path_join(source_file.trim_prefix("res://"));
}

void test_exported_texture_2d(const Ref<ExportReport> &export_report, const String &original_extract_dir, const String &version) {
	// SUBCASE(vformat("%s: Test exported texture 2d %s -> %s", version, original_resource.get_file(), exported_resource.get_file()).utf8().get_data())
	{
		auto dests = export_report->get_resources_used();
		REQUIRE(dests.size() >= 1);
		String original_import_path = get_original_import_path(export_report, original_extract_dir);
		String pck_resource = dests[0];
		String exported_resource = export_report->get_saved_path();

		Ref<Texture2D> original_texture = ResourceCompatLoader::non_global_load(pck_resource);
		CHECK(original_texture.is_valid());

		Ref<Image> original_image = original_texture->get_image();
		CHECK(original_image.is_valid());

		Ref<Image> exported_image;
		exported_image.instantiate();
		exported_image->load(exported_resource);
		CHECK(original_image->get_width() == exported_image->get_width());
		CHECK(original_image->get_height() == exported_image->get_height());
		if (export_report->get_loss_type() != ImportInfo::LossType::LOSSLESS) {
			return; // TODO: Some sort of test for lossy textures
		}
		if (original_image->is_compressed()) {
			original_image->decompress();
		}
		for (int64_t x = 0; x < original_image->get_width(); x++) {
			for (int64_t y = 0; y < original_image->get_height(); y++) {
				Color c = original_image->get_pixel(x, y);
				Color c2 = exported_image->get_pixel(x, y);
				if (c != c2) {
					CHECK(c.a == 0.0);
					CHECK(c.a == c2.a);
				} else {
					CHECK(c == c2);
				}
			}
		}
	}
}

void test_exported_wav(const Ref<ExportReport> &export_report, const String &original_extract_dir, const String &version) {
	// SUBCASE(vformat("%s: Test exported texture 2d %s -> %s", version, original_resource.get_file(), exported_resource.get_file()).utf8().get_data())
	{
		auto dests = export_report->get_resources_used();
		REQUIRE(dests.size() >= 1);
		String original_resource = dests[0];
		String exported_resource = export_report->get_saved_path();
		Ref<AudioStreamWAV> original_audio = ResourceCompatLoader::non_global_load(original_resource);
		CHECK(original_audio.is_valid());
		// import_infos->set_param("force/8_bit", false);
		// import_infos->set_param("force/mono", false);
		// import_infos->set_param("force/max_rate", false);
		// import_infos->set_param("force/max_rate_hz", sample->get_mix_rate());
		// import_infos->set_param("edit/trim", false);
		// import_infos->set_param("edit/normalize", false);
		// import_infos->set_param("edit/loop_mode", sample->get_loop_mode());
		// import_infos->set_param("edit/loop_begin", sample->get_loop_begin());
		// import_infos->set_param("edit/loop_end", sample->get_loop_end());
		// // quote ok was added in 4.3
		// import_infos->set_param("compress/mode", 0); // force uncompressed to prevent generational loss

		//	r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "compress/mode", PROPERTY_HINT_ENUM, "PCM (Uncompressed),IMA ADPCM,Quite OK Audio"), 2));

		int compressed_mode = 0;
		switch (original_audio->get_format()) {
			case AudioStreamWAV::FORMAT_8_BITS:
			case AudioStreamWAV::FORMAT_16_BITS:
				compressed_mode = 0;
				break;
			case AudioStreamWAV::FORMAT_IMA_ADPCM:
				compressed_mode = 1;
				break;
			case AudioStreamWAV::FORMAT_QOA:
				compressed_mode = 2;
				break;
			default:
				break;
		}
		Dictionary options{
			{
					"force/8_bit",
					false,
			},
			{
					"force/mono",
					false,
			},
			{
					"force/max_rate",
					false,
			},
			{
					"force/max_rate_hz",
					original_audio->get_mix_rate(),
			},
			{
					"edit/trim",
					false,
			},
			{
					"edit/normalize",
					false,
			},
			{
					"edit/loop_mode",
					original_audio->get_loop_mode(),
			},
			{
					"edit/loop_begin",
					original_audio->get_loop_begin(),
			},
			{
					"edit/loop_end",
					original_audio->get_loop_end(),
			},
			{ "compress/mode", compressed_mode },
		};

		Ref<AudioStreamWAV> exported_audio = AudioStreamWAV::load_from_file(exported_resource, options);
		CHECK(exported_audio.is_valid());
		auto original_data = original_audio->get_data();
		auto exported_data = exported_audio->get_data();
		if (compressed_mode != 0) {
			// both compression types are lossy, so we can't compare the data directly
			// just check the size and return.
			CHECK(original_data.size() == exported_data.size());
			return;
		}
#ifdef DEBUG_ENABLED
		if (original_data != exported_data) {
			int first_mismatch = -1;
			int num_mismatches = 0;
			CHECK(original_data.size() == exported_data.size());
			for (int i = 0; i < original_data.size(); i++) {
				if (original_data[i] != exported_data[i]) {
					if (first_mismatch == -1) {
						first_mismatch = i;
					}
					num_mismatches++;
				}
			}
			String message = vformat("%s (%s): First mismatch at index %d/%d, num mismatches: %d", original_resource.get_file(), version, first_mismatch, original_data.size(), num_mismatches);
			print_line(message);
			CHECK_MESSAGE(false, message.utf8().get_data()); // data mismatch
			return;
		}
#endif
		CHECK(original_data == exported_data);
	}
}

void test_exported_audio_stream_ogg_vorbis(const Ref<ExportReport> &export_report, const String &original_extract_dir, const String &version) {
	// SUBCASE(vformat("%s: Test exported texture 2d %s -> %s", version, original_resource.get_file(), exported_resource.get_file()).utf8().get_data())
	{
		auto dests = export_report->get_resources_used();
		REQUIRE(dests.size() >= 1);
		String pck_resource = dests[0];
		String exported_resource = export_report->get_saved_path();
		String original_import_path = get_original_import_path(export_report, original_extract_dir);
		Ref<AudioStreamOggVorbis> original_audio = AudioStreamOggVorbis::load_from_file(original_import_path);
		CHECK(original_audio.is_valid());
		Ref<AudioStreamOggVorbis> pck_audio = ResourceCompatLoader::non_global_load(pck_resource);
		CHECK(pck_audio.is_valid());
		Ref<AudioStreamOggVorbis> exported_audio = AudioStreamOggVorbis::load_from_file(exported_resource);
		CHECK(exported_audio.is_valid());
		CHECK(original_audio->get_packet_sequence()->get_packet_data() == pck_audio->get_packet_sequence()->get_packet_data());
		CHECK(original_audio->get_packet_sequence()->get_packet_data() == exported_audio->get_packet_sequence()->get_packet_data());
	}
}

void test_exported_mp3_audio_stream(const Ref<ExportReport> &export_report, const String &original_extract_dir, const String &version) {
	{
		auto dests = export_report->get_resources_used();
		REQUIRE(dests.size() >= 1);
		String pck_resource = dests[0];
		String exported_resource = export_report->get_saved_path();
		String original_import_path = get_original_import_path(export_report, original_extract_dir);
		Ref<AudioStreamMP3> original_audio = AudioStreamMP3::load_from_file(original_import_path);
		CHECK(original_audio.is_valid());
		Ref<AudioStreamMP3> pck_audio = ResourceCompatLoader::non_global_load(pck_resource);
		CHECK(pck_audio.is_valid());
		Ref<AudioStreamMP3> exported_audio = AudioStreamMP3::load_from_file(exported_resource);
		CHECK(exported_audio.is_valid());
		CHECK(original_audio->get_length() == pck_audio->get_length());
		CHECK(original_audio->get_length() == exported_audio->get_length());
		CHECK(original_audio->get_data() == pck_audio->get_data());
		CHECK(original_audio->get_data() == exported_audio->get_data());
	}
}

void test_exported_gdscript(const Ref<ExportReport> &export_report, const String &original_extract_dir, const String &version) {
	// SUBCASE(vformat("%s: Test exported texture 2d %s -> %s", version, original_resource.get_file(), exported_resource.get_file()).utf8().get_data())
	{
		String exported_resource = export_report->get_saved_path();
		String original_compiled_resource = export_report->get_resources_used()[0];
		String original_script_path = get_original_import_path(export_report, original_extract_dir);

		String original_script_text = FileAccess::get_file_as_string(original_script_path);
		String exported_script_text = FileAccess::get_file_as_string(exported_resource);
		auto original_bytecode = FileAccess::get_file_as_bytes(original_compiled_resource);
		if (original_script_text.is_empty() && exported_script_text.is_empty()) {
			return;
		}
		CHECK(!original_script_text.is_empty());
		CHECK(!exported_script_text.is_empty());
		CHECK(original_bytecode.size() > 0);
		auto decomp = GDScriptDecomp::create_decomp_for_version(version, true);
		CHECK(decomp.is_valid());

		auto compiled_original_bytecode = decomp->compile_code_string(original_script_text);
		// Bytecode may not be exactly the same due to earlier Godot variant encoder failing to zero out the padding bytes,
		// so we need to use the tester function to compare the bytecode.
		Error err = decomp->test_bytecode_match(original_bytecode, compiled_original_bytecode, false, true);
		CHECK(decomp->get_error_message() == "");
		CHECK(err == OK);

		auto compiled_exported_bytecode = decomp->compile_code_string(exported_script_text);
		err = decomp->test_bytecode_match(original_bytecode, compiled_exported_bytecode, false, true);
		CHECK(decomp->get_error_message() == "");
		CHECK(err == OK);
	}
}

void test_json_import_info(const Ref<ImportInfo> &import_info) {
	Dictionary json = import_info->to_json();
	Ref<ImportInfo> import_info2 = ImportInfo::from_json(json);
	CHECK(import_info->is_equal_to(import_info2));
}

void test_json_export_report(const Ref<ExportReport> &export_report) {
	Dictionary json = export_report->to_json();
	Ref<ExportReport> export_report2 = ExportReport::from_json(json);
	CHECK(export_report->is_equal_to(export_report2));
}

void test_recovered_resource(const Ref<ExportReport> &export_report, const String &original_extract_dir, const String &version) {
	REQUIRE(export_report.is_valid());
	CHECK(export_report->get_error() == OK);
	if (export_report->get_exporter() != TranslationExporter::EXPORTER_NAME) {
		CHECK(export_report->get_message() == "");
	}
	CHECK(export_report->get_import_info().is_valid());
	CHECK(!export_report->get_import_info()->get_type().is_empty());
	CHECK(!export_report->get_import_info()->get_importer().is_empty());
	Ref<ImportInfo> import_info = export_report->get_import_info();

	test_json_import_info(import_info);
	test_json_export_report(export_report);
	String importer = import_info->get_importer();
	String exporter = export_report->get_exporter();
	auto dests = export_report->get_resources_used();
	REQUIRE(dests.size() >= 1);
	String original_resource = dests[0];
	String saved_path = export_report->get_saved_path();
	if (exporter == TextureExporter::EXPORTER_NAME && (importer == "texture" || importer == "texture_2d")) {
		test_exported_texture_2d(export_report, original_extract_dir, version);
	} else if (exporter == OggStrExporter::EXPORTER_NAME) {
		test_exported_audio_stream_ogg_vorbis(export_report, original_extract_dir, version);
	} else if (exporter == SampleExporter::EXPORTER_NAME) {
		test_exported_wav(export_report, original_extract_dir, version);
	} else if (exporter == Mp3StrExporter::EXPORTER_NAME) {
		test_exported_mp3_audio_stream(export_report, original_extract_dir, version);
	} else if (exporter == ObjExporter::EXPORTER_NAME) {
		// test_exported_obj(original_resource, saved_path, version);
	} else if (exporter == GDScriptExporter::EXPORTER_NAME) {
		test_exported_gdscript(export_report, original_extract_dir, version);
	}
}

String get_test_projects_path() {
	return get_gdsdecomp_path().path_join("tests/test_projects");
}

// TODO: might need to add '[Audio]'
// '[SceneTree]' is in the name so that the test runner instantiates the rendering server and various other things.
TEST_CASE("[GDSDecomp][ProjectRecovery] ([SceneTree]) Recover projects ") {
	// get a list of all version numbers in the test_projects/exported' directory
	String test_projects_path = get_test_projects_path();
	String original_path = test_projects_path.path_join("original");
	String exported_path = test_projects_path.path_join("exported");
	Vector<String> versions = gdre::get_dirs_at(exported_path, {}, false);
	for (const String &version : versions) {
		Vector<String> sub_projects = gdre::get_recursive_dir_list(exported_path.path_join(version), { "*.pck" }, false);
		for (const String &sub_project : sub_projects) {
			SUBCASE(vformat("%s: Test recover project %s", version, sub_project).utf8().get_data()) {
				String exported_pck_path = exported_path.path_join(version).path_join(sub_project);

				String subdir = version.path_join(sub_project.get_base_dir());
				String original_project_zip = original_path.path_join(subdir).path_join(sub_project.get_file().get_basename() + ".zip");
				// where we will extract the original project
				String original_extract_dir = get_tmp_path().path_join("original").path_join(subdir);
				// where we will output the exported project during recovery
				String exported_recovery_dir = get_tmp_path().path_join("exported").path_join(subdir);

				gdre::rimraf(original_extract_dir);
				Error err = gdre::unzip_file_to_dir(original_project_zip, original_extract_dir);
				CHECK(err == OK);

				gdre::rimraf(exported_recovery_dir);
				// load the project
				err = GDRESettings::get_singleton()->load_project({ exported_pck_path }, false);
				CHECK(err == OK);

				PckDumper dumper;
				err = dumper.check_md5_all_files();
				CHECK(err == OK);
				err = dumper.pck_dump_to_dir(exported_recovery_dir, {});
				CHECK(err == OK);
				ImportExporter import_exporter;
				err = import_exporter.export_imports(exported_recovery_dir, {});
				CHECK(err == OK);
				auto import_report = import_exporter.get_report();
				REQUIRE(import_report.is_valid());
				Dictionary json_report = import_report->to_json();
				{
					Ref<ImportExporterReport> import_exporter_report2 = ImportExporterReport::from_json(json_report);
					CHECK(import_report->is_equal_to(import_exporter_report2));
				}
#ifdef DEBUG_ENABLED
				{
					String json_report_path = exported_recovery_dir.path_join("json_report.json");
					auto fa = FileAccess::open(json_report_path, FileAccess::WRITE);
					REQUIRE(fa.is_valid());
					fa->store_string(JSON::stringify(json_report, "\t", false, true));
					fa->flush();
				}
#endif
				CHECK(import_report->get_failed().size() == 0);
				auto successes = import_report->get_successes();
				for (const auto &success : successes) {
					test_recovered_resource(success, original_extract_dir, version);
				}

				GDRESettings::get_singleton()->close_log_file();
				GDRESettings::get_singleton()->unload_project();
			}
		}
	}
}

} //namespace TestProjectExport
