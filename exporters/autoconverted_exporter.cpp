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
	Ref<ExportReport> report = memnew(ExportReport(import_infos));
	if (import_infos->get_export_dest().get_extension().to_lower() == "xml") {
		report->set_error(ERR_UNAVAILABLE);
		report->set_unsupported_format_type("2.0 XML format");
		return report;
	}
	Error err = ResourceCompatLoader::to_text(src_path, dst_path, 0, import_infos->get_source_file());
	report->set_error(err);
	report->set_saved_path(dst_path);
	return report;
}

bool AutoConvertedExporter::handles_import(const String &importer, const String &resource_type) const {
	return importer == "autoconverted";
}

void AutoConvertedExporter::get_handled_types(List<String> *out) const {
}

void AutoConvertedExporter::get_handled_importers(List<String> *out) const {
	out->push_back("autoconverted");
}

String AutoConvertedExporter::get_name() const {
	return "AutoConverted";
}