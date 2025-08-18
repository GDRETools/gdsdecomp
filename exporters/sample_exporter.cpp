#include "sample_exporter.h"

#include "compat/resource_loader_compat.h"
#include "utility/common.h"
#include "utility/import_info.h"

#include "scene/resources/audio_stream_wav.h"

struct IMA_ADPCM_State {
	int16_t step_index = 0;
	int32_t predictor = 0;
	int32_t last_nibble = -1;
};
struct QOA_State {
	qoa_desc desc = {};
	uint32_t data_ofs = 0;
	uint32_t frame_len = 0;
	LocalVector<int16_t> dec;
	uint32_t dec_len = 0;
	int64_t cache_pos = -1;
	int16_t cache[2] = { 0, 0 };
	int16_t cache_r[2] = { 0, 0 };
};

#define DATA_PAD 16

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
	bool is_stereo = p_sample->is_stereo();
	Vector<uint8_t> dest_data;
	int64_t p_offset = 0;
	int64_t p_increment = 1;
	int32_t final = 0, final_r = 0, next = 0, next_r = 0;
	QOA_State p_qoa;
	int sign = 1;

	qoa_decode_header(data.ptr(), data.size(), &p_qoa.desc);
	int64_t p_amount = p_qoa.desc.samples; // number of samples for EACH channel, not total
	dest_data.resize(p_amount * sizeof(int16_t) * (is_stereo ? 2 : 1)); // number of 16-bit samples * number of channels
	p_qoa.frame_len = qoa_max_frame_size(&p_qoa.desc);
	int samples_len = (p_qoa.desc.samples > QOA_FRAME_LEN ? QOA_FRAME_LEN : p_qoa.desc.samples);
	int dec_len = p_qoa.desc.channels * samples_len;
	p_qoa.dec.resize(dec_len);
	int16_t *dest = (int16_t *)dest_data.ptrw();
	while (p_amount) {
		p_amount--;
		int64_t pos = p_offset;
		if (pos != p_qoa.cache_pos) { // Prevents triple decoding on lower mix rates.
			for (int i = 0; i < 2; i++) {
				// Sign operations prevent triple decoding on backward loops, maxing prevents pop.
				uint32_t interp_pos = MIN(pos + (i * sign) + (sign < 0), p_qoa.desc.samples - 1);
				uint32_t new_data_ofs = 8 + interp_pos / QOA_FRAME_LEN * p_qoa.frame_len;
				const uint8_t *src_ptr = (const uint8_t *)data.ptr();

				if (p_qoa.data_ofs != new_data_ofs) {
					p_qoa.data_ofs = new_data_ofs;
					const uint8_t *ofs_src = (uint8_t *)src_ptr + p_qoa.data_ofs;
					qoa_decode_frame(ofs_src, p_qoa.frame_len, &p_qoa.desc, p_qoa.dec.ptr(), &p_qoa.dec_len);
				}

				uint32_t dec_idx = (interp_pos % QOA_FRAME_LEN) * p_qoa.desc.channels;

				if ((sign > 0 && i == 0) || (sign < 0 && i == 1)) {
					final = p_qoa.dec[dec_idx];
					p_qoa.cache[0] = final;
					if (is_stereo) {
						final_r = p_qoa.dec[dec_idx + 1];
						p_qoa.cache_r[0] = final_r;
					}
				} else {
					next = p_qoa.dec[dec_idx];
					p_qoa.cache[1] = next;
					if (is_stereo) {
						next_r = p_qoa.dec[dec_idx + 1];
						p_qoa.cache_r[1] = next_r;
					}
				}
			}
			p_qoa.cache_pos = pos;
		} else {
			final = p_qoa.cache[0];
			if (is_stereo) {
				final_r = p_qoa.cache_r[0];
			}

			next = p_qoa.cache[1];
			if (is_stereo) {
				next_r = p_qoa.cache_r[1];
			}
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
