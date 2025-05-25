#pragma once
#include "exporters/resource_exporter.h"
class AudioStreamWAV;
class SampleExporter : public ResourceExporter {
	GDCLASS(SampleExporter, ResourceExporter);
	Error _export_file(const String &out_path, const String &res_path, Ref<AudioStreamWAV> &r_sample, int ver_major = 0);

public:
	static Ref<AudioStreamWAV> convert_adpcm_to_16bit(const Ref<AudioStreamWAV> &p_sample);
	static Ref<AudioStreamWAV> convert_qoa_to_16bit(const Ref<AudioStreamWAV> &p_sample);

	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
	virtual String get_name() const override;
};