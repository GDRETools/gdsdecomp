#pragma once
#include "exporters/resource_exporter.h"

class GDExtensionExporter : public ResourceExporter {
	GDCLASS(GDExtensionExporter, ResourceExporter);

public:
	static String get_plugin_download(const String &output_dir, Ref<ImportInfoGDExt> import_infos);
	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
};