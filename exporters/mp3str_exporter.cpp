#include "mp3str_exporter.h"
#include "compat/resource_loader_compat.h"
#include "exporters/export_report.h"
#include "modules/minimp3/audio_stream_mp3.h"
#include "utility/common.h"

Error Mp3StrExporter::export_file(const String &p_dest_path, const String &p_src_path) {
	Error err;

	Ref<AudioStreamMP3> sample = ResourceCompatLoader::non_global_load(p_src_path, "", &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not load mp3str file " + p_src_path);

	err = _export_resource(sample, p_dest_path, p_src_path);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to export mp3str file " + p_src_path);

	print_verbose("Converted " + p_src_path + " to " + p_dest_path);
	return OK;
}

Error Mp3StrExporter::_export_resource(Ref<AudioStreamMP3> sample, const String &p_dest_path, const String &p_src_path) {
	Error err = gdre::ensure_dir(p_dest_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to create dirs for " + p_dest_path);

	Ref<FileAccess> f = FileAccess::open(p_dest_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Could not open " + p_dest_path + " for saving");

	PackedByteArray data = sample->get_data();
	f->store_buffer(data.ptr(), data.size());

	return OK;
}

Ref<ExportReport> Mp3StrExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	// Check if the exporter can handle the given importer and resource type

	String src_path = import_infos->get_path();
	String dst_path = output_dir.path_join(import_infos->get_export_dest().replace("res://", ""));
	Ref<ExportReport> report = memnew(ExportReport(import_infos));
	Error err = OK;
	Ref<AudioStreamMP3> sample = ResourceCompatLoader::non_global_load(src_path, "", &err);
	ERR_FAIL_COND_V_MSG(err != OK, report, "Could not load mp3str file " + src_path);
	err = _export_resource(sample, dst_path, src_path);
	report->set_error(err);
	if (err == OK) {
		report->set_saved_path(dst_path);
		if (import_infos->get_ver_major() == 4) {
			import_infos->set_param("loop", sample->has_loop());
			import_infos->set_param("loop_offset", sample->get_loop_offset());
			import_infos->set_param("bpm", sample->get_bpm());
			import_infos->set_param("beat_count", sample->get_beat_count());
			import_infos->set_param("bar_beats", sample->get_bar_beats());
		}
	}

	return report;
}

bool Mp3StrExporter::handles_import(const String &importer, const String &resource_type) const {
	return importer == "mp3" || resource_type == "AudioStreamMP3";
}

void Mp3StrExporter::get_handled_types(List<String> *out) const {
	out->push_back("AudioStreamMP3");
}

void Mp3StrExporter::get_handled_importers(List<String> *out) const {
	out->push_back("mp3");
}

String Mp3StrExporter::get_name() const {
	return "MP3";
}