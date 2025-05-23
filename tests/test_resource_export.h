#pragma once

#include "compat/oggstr_loader_compat.h"
#include "test_common.h"
#include "tests/test_macros.h"
#include <compat/resource_compat_text.h>
#include <compat/resource_loader_compat.h>
#include <modules/gdsdecomp/exporters/resource_exporter.h>
#include <scene/resources/audio_stream_wav.h>
namespace TestResourceExport {
// oggvorbisstr
// texture
// font
// sample

TEST_CASE("[GDSDecomp][ResourceExport] Export sample") {
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);
	int dummy_idx = AudioDriverManager::get_driver_count() - 1;
	AudioDriverManager::initialize(dummy_idx);
	AudioServer *audio_server = memnew(AudioServer);
	audio_server->init();

	for (const String &version : versions) {
		String test_dir = get_test_resources_path().path_join(version).path_join("sample");
		Vector<String> files = gdre::get_recursive_dir_list(test_dir, { "*.sample" });
		String output_dir = get_tmp_path().path_join(version).path_join("sample");
		for (const String &file : files) {
			// original file
			String original_file = file.rsplit("-", true, 1)[0];
			String output_file = output_dir.path_join(original_file.get_file());
			Error err = Exporter::export_file(output_file, file);
			CHECK(err == OK);
			// load the sample file, not the wav
			Ref<AudioStreamWAV> original_sample = ResourceCompatLoader::non_global_load(file);
			CHECK(original_sample.is_valid());
			Ref<AudioStreamWAV> exported_sample = AudioStreamWAV::load_from_file(output_file, {});
			CHECK(exported_sample.is_valid());
			CHECK(original_sample->get_data() == exported_sample->get_data());
		}
	}
}

TEST_CASE("[GDSDecomp][ResourceExport] Export oggvorbisstr") {
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);

	for (const String &version : versions) {
		String test_dir = get_test_resources_path().path_join(version).path_join("ogg");
		Vector<String> files = gdre::get_recursive_dir_list(test_dir, { "*.oggvorbisstr" });
		String output_dir = get_tmp_path().path_join(version).path_join("ogg");
		for (const String &file : files) {
			// original file
			String original_file = file.rsplit("-", true, 1)[0];
			String output_file = output_dir.path_join(original_file.get_file());
			Error err = Exporter::export_file(output_file, file);
			CHECK(err == OK);
			Ref<AudioStreamOggVorbis> original_audio = ResourceCompatLoader::non_global_load(file);
			CHECK(original_audio.is_valid());
			Ref<AudioStreamOggVorbis> exported_audio = AudioStreamOggVorbis::load_from_file(output_file);
			CHECK(exported_audio.is_valid());
			auto original_packet_sequence = original_audio->get_packet_sequence();
			auto exported_packet_sequence = exported_audio->get_packet_sequence();
			Array original_packet_data = original_packet_sequence->get_packet_data();
			Array exported_packet_data = exported_packet_sequence->get_packet_data();
			CHECK(original_packet_data.size() == exported_packet_data.size());
			for (int i = 0; i < original_packet_data.size(); i++) {
				CHECK(original_packet_data[i] == exported_packet_data[i]);
			}
		}
	}
}

TEST_CASE("[GDSDecomp][ResourceExport] Export texture") {
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);

	for (const String &version : versions) {
		String test_dir = get_test_resources_path().path_join(version).path_join("texture");
		Vector<String> files = gdre::get_recursive_dir_list(test_dir, { "*.ctex" });
		String output_dir = get_tmp_path().path_join(version).path_join("texture");
		for (const String &file : files) {
			// original file
			String original_file = file.rsplit("-", true, 1)[0];
			String output_file = output_dir.path_join(original_file.get_file());
			gdre::ensure_dir(output_file.get_base_dir());

			Error err = Exporter::export_file(output_file, file);
			CHECK(err == OK);

			Ref<Texture2D> original_texture = ResourceCompatLoader::non_global_load(file);
			CHECK(original_texture.is_valid());

			Ref<Image> original_image = original_texture->get_image();
			CHECK(original_image.is_valid());

			Ref<Image> exported_image;
			exported_image.instantiate();
			exported_image->load(output_file);
			CHECK(original_image->get_width() == exported_image->get_width());
			CHECK(original_image->get_height() == exported_image->get_height());
			for (size_t x = 0; x < original_image->get_width(); x++) {
				for (size_t y = 0; y < original_image->get_height(); y++) {
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
}

} // namespace TestResourceExport
