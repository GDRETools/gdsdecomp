#pragma once
#include "exporters/resource_exporter.h"

class TranslationExporter : public ResourceExporter {
	GDCLASS(TranslationExporter, ResourceExporter);

	HashSet<String> all_keys_found;

public:
	static constexpr float threshold = 0.15; // TODO: put this in the project configuration

	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual bool handles_import(const String &importer, const String &resource_type = String()) const override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
	virtual bool supports_multithread() const override { return false; }
	virtual String get_name() const override;
	virtual bool supports_nonpack_export() const override { return false; }
};