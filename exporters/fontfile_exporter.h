#pragma once
#include "exporters/resource_exporter.h"

class FontFileExporter : public ResourceExporter {
	GDCLASS(FontFileExporter, ResourceExporter);
	Error _export_font_data_dynamic(const String &p_dest_path, const String &p_src_path);
	Error _export_image(const String &p_dest_path, const String &p_src_path, Ref<Image> &r_image);
	void _set_image_import_info(Ref<ImportInfo> import_infos, Ref<Image> image);

public:
	static constexpr const char *const EXPORTER_NAME = "FontFile";

	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
	virtual String get_name() const override;
	virtual String get_default_export_extension(const String &res_path) const override;
};
