#include "autoconverted_exporter.h"
#include "compat/resource_loader_compat.h"
#include "core/error/error_list.h"
#include "exporters/export_report.h"

Error AutoConvertedExporter::export_file(const String &p_dest_path, const String &p_src_path) {
	return ResourceCompatLoader::to_text(p_src_path, p_dest_path);
}

Ref<ExportReport> AutoConvertedExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	// Check if the exporter can handle the given importer and resource type
	String dst_ext = import_infos->get_export_dest().get_extension().to_lower();
	String src_path = import_infos->get_path();
	String dst_path = output_dir.path_join(import_infos->get_export_dest().replace("res://", ""));
	Ref<ExportReport> report = memnew(ExportReport(import_infos, get_name()));
	Error err = ResourceCompatLoader::to_text(src_path, dst_path, 0, import_infos->get_source_file());
	report->set_error(err);
	report->set_saved_path(dst_path);
	return report;
}

void AutoConvertedExporter::get_handled_types(List<String> *out) const {
}

void AutoConvertedExporter::get_handled_importers(List<String> *out) const {
	out->push_back("autoconverted");
}

String AutoConvertedExporter::get_name() const {
	return EXPORTER_NAME;
}

String AutoConvertedExporter::get_default_export_extension(const String &res_path) const {
	if (res_path.get_extension().to_lower() == "scn") {
		return "tscn";
	}
	return "tres";
}
