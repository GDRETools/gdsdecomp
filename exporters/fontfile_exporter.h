#pragma once
#include "exporters/resource_exporter.h"

class FontFileExporter : public ResourceExporter {
	GDCLASS(FontFileExporter, ResourceExporter);

public:
	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual bool handles_import(const String &importer, const String &resource_type = String()) const override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
	virtual String get_name() const override;
};