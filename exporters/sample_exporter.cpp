#include "sample_exporter.h"

#include "compat/resource_loader_compat.h"
#include "gdre_test_macros.h"
#include "utility/common.h"
#include "utility/import_info.h"

#include "core/string/ustring.h"
#include "exporters/export_report.h"
#include "scene/resources/audio_stream_wav.h"

#define DATA_PAD 16
namespace {

struct IMA_ADPCM_State {
	int16_t step_index = 0;
	int32_t predictor = 0;
	int32_t last_nibble = -1;
};

int64_t qoa_decode(const unsigned char *bytes, int size, qoa_desc *qoa, Vector<uint8_t> &r_dest_data) {
	unsigned int p = qoa_decode_header(bytes, size, qoa);
	if (!p) {
		return 0;
	}

	/* Calculate the required size of the sample buffer and allocate */
	int total_samples = qoa->samples * qoa->channels;
	r_dest_data.resize(total_samples * sizeof(short));
	int16_t *sample_data = (int16_t *)r_dest_data.ptrw();

	unsigned int sample_index = 0;
	unsigned int frame_len;
	unsigned int frame_size;

	/* Decode all frames */
	do {
		short *sample_ptr = sample_data + sample_index * qoa->channels;
		frame_size = qoa_decode_frame(bytes + p, size - p, qoa, sample_ptr, &frame_len);

		p += frame_size;
		sample_index += frame_len;
	} while (frame_size && sample_index < qoa->samples);

	// qoa->samples = sample_index;
	return sample_index;
}
} //namespace

Ref<AudioStreamWAV> SampleExporter::convert_qoa_to_16bit(const Ref<AudioStreamWAV> &p_sample) {
	ERR_FAIL_COND_V_MSG(p_sample->get_format() != AudioStreamWAV::FORMAT_QOA, Ref<AudioStreamWAV>(), "Sample is not QOA.");
	Ref<AudioStreamWAV> new_sample = memnew(AudioStreamWAV);

	new_sample->set_format(AudioStreamWAV::FORMAT_16_BITS);
	new_sample->set_loop_mode(p_sample->get_loop_mode());
	new_sample->set_loop_begin(p_sample->get_loop_begin());
	new_sample->set_loop_end(p_sample->get_loop_end());
	new_sample->set_mix_rate(p_sample->get_mix_rate());
	new_sample->set_stereo(p_sample->is_stereo());

	auto data = p_sample->get_data();
	Vector<uint8_t> dest_data;

	qoa_desc p_qoa;
	int64_t p_amount = qoa_decode(data.ptr(), data.size(), &p_qoa, dest_data);

	ERR_FAIL_COND_V_MSG(p_amount == 0, Ref<AudioStreamWAV>(), "Failed to decode QOA sample");
	if (p_qoa.samples != p_amount) {
		WARN_PRINT(vformat("%s: Data size mismatch: %d vs %d", p_sample->get_path(), p_qoa.samples, p_amount));
	}

	new_sample->set_data(dest_data);
	return new_sample;
}

Ref<AudioStreamWAV> SampleExporter::convert_adpcm_to_16bit(const Ref<AudioStreamWAV> &p_sample) {
	ERR_FAIL_COND_V_MSG(p_sample->get_format() != AudioStreamWAV::FORMAT_IMA_ADPCM, Ref<AudioStreamWAV>(), "Sample is not IMA ADPCM.");
	Ref<AudioStreamWAV> new_sample = memnew(AudioStreamWAV);

	new_sample->set_format(AudioStreamWAV::FORMAT_16_BITS);
	new_sample->set_loop_mode(p_sample->get_loop_mode());
	new_sample->set_loop_begin(p_sample->get_loop_begin());
	new_sample->set_loop_end(p_sample->get_loop_end());
	new_sample->set_mix_rate(p_sample->get_mix_rate());
	new_sample->set_stereo(p_sample->is_stereo());

	IMA_ADPCM_State p_ima_adpcm[2];
	int32_t final, final_r;
	int64_t p_offset = 0;
	int64_t p_increment = 1;
	auto data = p_sample->get_data(); // This gets the data past the DATA_PAD, so no need to add it to the offsets.
	bool is_stereo = p_sample->is_stereo();
	int64_t p_amount = data.size() * (is_stereo ? 1 : 2); // number of samples for EACH channel, not total
	Vector<uint8_t> dest_data;
	dest_data.resize(p_amount * sizeof(int16_t) * (is_stereo ? 2 : 1)); // number of 16-bit samples * number of channels
	int16_t *dest = (int16_t *)dest_data.ptrw();
	while (p_amount) {
		p_amount--;
		int64_t pos = p_offset;

		while (pos > p_ima_adpcm[0].last_nibble) {
			static const int16_t _ima_adpcm_step_table[89] = {
				7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
				19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
				50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
				130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
				337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
				876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
				2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
				5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
				15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
			};

			static const int8_t _ima_adpcm_index_table[16] = {
				-1, -1, -1, -1, 2, 4, 6, 8,
				-1, -1, -1, -1, 2, 4, 6, 8
			};

			for (int i = 0; i < (is_stereo ? 2 : 1); i++) {
				int16_t nibble, diff, step;

				p_ima_adpcm[i].last_nibble++;
				const uint8_t *src_ptr = (const uint8_t *)data.ptr();
				// src_ptr += AudioStreamWAV::DATA_PAD;
				int source_index = (p_ima_adpcm[i].last_nibble >> 1) * (is_stereo ? 2 : 1) + i;
				uint8_t nbb = src_ptr[source_index];
				nibble = (p_ima_adpcm[i].last_nibble & 1) ? (nbb >> 4) : (nbb & 0xF);
				step = _ima_adpcm_step_table[p_ima_adpcm[i].step_index];

				p_ima_adpcm[i].step_index += _ima_adpcm_index_table[nibble];
				if (p_ima_adpcm[i].step_index < 0) {
					p_ima_adpcm[i].step_index = 0;
				}
				if (p_ima_adpcm[i].step_index > 88) {
					p_ima_adpcm[i].step_index = 88;
				}

				diff = step >> 3;
				if (nibble & 1) {
					diff += step >> 2;
				}
				if (nibble & 2) {
					diff += step >> 1;
				}
				if (nibble & 4) {
					diff += step;
				}
				if (nibble & 8) {
					diff = -diff;
				}

				p_ima_adpcm[i].predictor += diff;
				if (p_ima_adpcm[i].predictor < -0x8000) {
					p_ima_adpcm[i].predictor = -0x8000;
				} else if (p_ima_adpcm[i].predictor > 0x7FFF) {
					p_ima_adpcm[i].predictor = 0x7FFF;
				}

				//printf("%i - %i - pred %i\n",int(p_ima_adpcm[i].last_nibble),int(nibble),int(p_ima_adpcm[i].predictor));
			}
		}

		final = p_ima_adpcm[0].predictor;
		if (is_stereo) {
			final_r = p_ima_adpcm[1].predictor;
		}

		*dest = final;
		dest++;
		if (is_stereo) {
			*dest = final_r;
			dest++;
		}
		p_offset += p_increment;
	}
	new_sample->set_data(dest_data);
	return new_sample;
}

Ref<AudioStreamWAV> SampleExporter::decompress(const Ref<AudioStreamWAV> &p_sample) {
	switch (p_sample->get_format()) {
		case AudioStreamWAV::FORMAT_QOA:
			return convert_qoa_to_16bit(p_sample);
		case AudioStreamWAV::FORMAT_IMA_ADPCM:
			return convert_adpcm_to_16bit(p_sample);
		default:
			return p_sample;
	}
}

Error SampleExporter::_export_file(const String &out_path, const String &res_path, Ref<AudioStreamWAV> &r_sample, int ver_major) {
	// Implement the export logic here
	Error err;
	r_sample = ResourceCompatLoader::non_global_load(res_path, "", &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not load sample file " + res_path);
	ERR_FAIL_COND_V_MSG(r_sample.is_null(), ERR_FILE_UNRECOGNIZED, "Sample not loaded: " + res_path);
	bool converted = false;
	if (r_sample->get_format() == AudioStreamWAV::FORMAT_IMA_ADPCM) {
		// convert to 16-bit
		r_sample = convert_adpcm_to_16bit(r_sample);
		converted = true;
	} else if (r_sample->get_format() == AudioStreamWAV::FORMAT_QOA) {
		r_sample = convert_qoa_to_16bit(r_sample);
		converted = true;
	}
	ERR_FAIL_COND_V_MSG(r_sample.is_null(), ERR_FILE_UNRECOGNIZED, "Failed to decompress sample: " + res_path);
	err = gdre::ensure_dir(out_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to create dirs for " + out_path);
	err = r_sample->save_to_wav(out_path);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not save " + out_path);

	return converted ? ERR_PRINTER_ON_FIRE : OK;
}

Error SampleExporter::export_file(const String &out_path, const String &res_path) {
	Ref<AudioStreamWAV> sample;
	Error err = _export_file(out_path, res_path, sample, 0);
	if (err == ERR_PRINTER_ON_FIRE) {
		return OK;
	}
	return err;
}

Ref<ExportReport> SampleExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	// Implement the resource export logic here
	Ref<ExportReport> report = memnew(ExportReport(import_infos, get_name()));
	String src_path = import_infos->get_path();
	String dst_path = output_dir.path_join(import_infos->get_export_dest().replace("res://", ""));
	Ref<AudioStreamWAV> sample;
	Error err = _export_file(dst_path, src_path, sample, import_infos->get_ver_major());

	report->set_resources_used({ import_infos->get_path() });

	if (err == ERR_PRINTER_ON_FIRE) {
		report->set_loss_type(ImportInfo::STORED_LOSSY);
	} else if (err != OK) {
		report->set_error(err);
		report->set_message("Failed to export sample: " + src_path);
	}

	if (err == OK || err == ERR_PRINTER_ON_FIRE) {
		report->set_saved_path(dst_path);
		if (import_infos->get_ver_major() >= 4) {
			// 			r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "force/8_bit"), false));
			// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "force/mono"), false));
			// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "force/max_rate", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), false));
			// r_options->push_back(ImportOption(PropertyInfo(Variant::FLOAT, "force/max_rate_hz", PROPERTY_HINT_RANGE, "11025,192000,1,exp"), 44100));
			// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "edit/trim"), false));
			// r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "edit/normalize"), false));
			// // Keep the `edit/loop_mode` enum in sync with AudioStreamWAV::LoopMode (note: +1 offset due to "Detect From WAV").
			// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "edit/loop_mode", PROPERTY_HINT_ENUM, "Detect From WAV,Disabled,Forward,Ping-Pong,Backward", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), 0));
			// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "edit/loop_begin"), 0));
			// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "edit/loop_end"), -1));
			// // Quite OK Audio is lightweight enough and supports virtually every significant AudioStreamWAV feature.
			// r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "compress/mode", PROPERTY_HINT_ENUM, "PCM (Uncompressed),IMA ADPCM,Quite OK Audio"), 2));
			import_infos->set_param("force/8_bit", false);
			import_infos->set_param("force/mono", false);
			import_infos->set_param("force/max_rate", false);
			import_infos->set_param("force/max_rate_hz", sample->get_mix_rate());
			import_infos->set_param("edit/trim", false);
			import_infos->set_param("edit/normalize", false);
			import_infos->set_param("edit/loop_mode", sample->get_loop_mode());
			import_infos->set_param("edit/loop_begin", sample->get_loop_begin());
			import_infos->set_param("edit/loop_end", sample->get_loop_end());
			// quote ok was added in 4.3
			import_infos->set_param("compress/mode", 0); // force uncompressed to prevent generational loss
		}
	}
	return report;
}

void SampleExporter::get_handled_types(List<String> *out) const {
	out->push_back("AudioStreamWAV");
	out->push_back("AudioStreamSample");
}

void SampleExporter::get_handled_importers(List<String> *out) const {
	out->push_back("sample");
	out->push_back("wav");
}

String SampleExporter::get_name() const {
	return EXPORTER_NAME;
}

String SampleExporter::get_default_export_extension(const String &res_path) const {
	return "wav";
}

Vector<String> SampleExporter::get_export_extensions(const String &res_path) const {
	return { "wav" };
}

Error SampleExporter::test_export(const Ref<ExportReport> &export_report, const String &original_project_dir) const {
	Error _ret_err = OK;
	{
		auto dests = export_report->get_resources_used();
		GDRE_REQUIRE_GE(dests.size(), 1);
		String original_resource = dests[0];
		String exported_resource = export_report->get_saved_path();
		Ref<AudioStreamWAV> original_audio = ResourceCompatLoader::non_global_load(original_resource);
		GDRE_CHECK(original_audio.is_valid());

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
		GDRE_CHECK(exported_audio.is_valid());
		auto original_data = original_audio->get_data();
		auto exported_data = exported_audio->get_data();
		if (compressed_mode != 0) {
			// both compression types are lossy, so we can't compare the data directly
			// just check the size and return.
			if (export_report->get_import_info()->is_import()) {
				GDRE_CHECK_EQ(original_data.size(), exported_data.size());
			}
		} else {
			String data_mismatch_error_message = gdre_test::get_error_message_for_vector_mismatch(original_data, exported_data);
			if (!data_mismatch_error_message.is_empty()) {
				data_mismatch_error_message = vformat("%s (ver_major: %d): %s", original_resource.get_file(), export_report->get_import_info()->get_ver_major(), data_mismatch_error_message);
			}
			GDRE_CHECK_EQ(data_mismatch_error_message, "");
		}
	}
	return _ret_err;
}
