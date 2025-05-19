#include "fontfile_exporter.h"
#include "compat/resource_loader_compat.h"
#include "exporters/export_report.h"

Error FontFileExporter::export_file(const String &p_dest_path, const String &p_src_path) {
	Error err;
	Ref<Resource> fontfile = ResourceCompatLoader::fake_load(p_src_path, "", &err);
	ERR_FAIL_COND_V_MSG(err, err, "Failed to load font file " + p_src_path);
	PackedByteArray data = fontfile->get("data");
	ERR_FAIL_COND_V_MSG(data.size() == 0, ERR_FILE_CORRUPT, "Font file " + p_src_path + " is empty");
	write_to_file(p_dest_path, data);
	return OK;
}

Ref<ExportReport> FontFileExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	// Check if the exporter can handle the given importer and resource type
	String src_path = import_infos->get_path();
	String dst_path = output_dir.path_join(import_infos->get_export_dest().replace("res://", ""));
	Ref<ExportReport> report = memnew(ExportReport(import_infos));
	Error err = export_file(dst_path, src_path);
	report->set_error(err);
	report->set_saved_path(dst_path);
	return report;
}

bool FontFileExporter::handles_import(const String &importer, const String &resource_type) const {
	if (!importer.is_empty()) {
		if (importer == "font_data_dynamic") {
			return true;
		}
		return false;
	}
	return resource_type == "FontFile";
}

void FontFileExporter::get_handled_types(List<String> *out) const {
	out->push_back("FontFile");
}

void FontFileExporter::get_handled_importers(List<String> *out) const {
	out->push_back("font_data_dynamic");
}

String FontFileExporter::get_name() const {
	return "FontFile";
}