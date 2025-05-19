#pragma once
#include "exporters/resource_exporter.h"

class AudioStreamOggVorbis;
class OggStrExporter : public ResourceExporter {
	GDCLASS(OggStrExporter, ResourceExporter);
	static Error get_data_from_ogg_stream(const String &real_src, const Ref<AudioStreamOggVorbis> &sample, Vector<uint8_t> &r_data);

	Error _export_file(const String real_src, const String &out_path, const String &res_path, Ref<AudioStreamOggVorbis> &r_sample, int ver_major);
	static Vector<uint8_t> get_ogg_stream_data(const String &real_src, const Ref<AudioStreamOggVorbis> &sample);
	static Vector<uint8_t> load_ogg_stream_data(const String &real_src, const String &p_path, Ref<AudioStreamOggVorbis> &r_sample, int ver_major = 0, Error *r_err = nullptr);

public:
	virtual String get_name() const override;
	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
};