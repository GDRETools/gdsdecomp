#pragma once
#include "exporters/resource_exporter.h"

class SpineAtlasExporter : public ResourceExporter {
	GDCLASS(SpineAtlasExporter, ResourceExporter);

public:
	static constexpr const char *const EXPORTER_NAME = "SpineAtlas";

	virtual String get_name() const override;
	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
	virtual String get_default_export_extension(const String &res_path) const override;
};

class SpineSkeletonExporter : public ResourceExporter {
	GDCLASS(SpineSkeletonExporter, ResourceExporter);
	static constexpr const char *const EXPORTER_NAME = "SpineSkeleton";

	virtual String get_name() const override;
	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
	virtual String get_default_export_extension(const String &res_path) const override;
};
