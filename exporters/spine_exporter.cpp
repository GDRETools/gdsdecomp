#include "spine_exporter.h"
#include "core/io/dir_access.h"
#include "utility/common.h"
#include "core/io/file_access.h"
#include "core/io/json.h"

Error _load_dict(const String &res_path, Dictionary &dict) {
	String content = FileAccess::get_file_as_string(res_path);
	ERR_FAIL_COND_V_MSG(content.is_empty(), ERR_FILE_CANT_OPEN, "Failed to load file: " + res_path);
	dict = JSON::parse_string(content);
	ERR_FAIL_COND_V_MSG(dict.is_empty(), ERR_FILE_CANT_OPEN, "Failed to parse file: " + res_path);
	return OK;
}


Error _export_file(const String &out_path, const String &res_path, Dictionary &dict) {
	// We load it as json
	String content = FileAccess::get_file_as_string(res_path);
	ERR_FAIL_COND_V_MSG(content.is_empty(), ERR_FILE_CANT_OPEN, "Failed to load file: " + res_path);
	dict = JSON::parse_string(content);
	ERR_FAIL_COND_V_MSG(dict.is_empty(), ERR_FILE_CANT_OPEN, "Failed to parse file: " + res_path);
	String source_path = dict["source_path"];
	String atlas_data = dict["atlas_data"];
	String normal_map_prefix = dict["normal_texture_prefix"];
	String specular_map_prefix = dict["specular_texture_prefix"];

	gdre::ensure_dir(out_path.get_base_dir());
	auto f = FileAccess::open(out_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_FILE_CANT_OPEN, "Failed to open file: " + out_path);
	f->store_string(atlas_data);
	return OK;
}


Error SpineAtlasExporter::export_file(const String &out_path, const String &res_path) {
	// We load it as json
	Dictionary dict;
	return _export_file(out_path, res_path, dict);
}

Ref<ExportReport> SpineAtlasExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	Ref<ExportReport> report = Ref<ExportReport>(memnew(ExportReport(import_infos)));
	auto dest = output_dir.path_join(import_infos->get_export_dest().trim_prefix("res://"));
	Dictionary dict;
	report->set_error(_export_file(dest, import_infos->get_path(), dict));
	if (report->get_error() == OK) {
		report->set_saved_path(dest);
		if (import_infos->get_ver_major() >= 4){
			Dictionary params;
			params["normal_map_prefix"] = dict["normal_texture_prefix"];
			params["specular_map_prefix"] = dict["specular_texture_prefix"];
			import_infos->set_params(params);
		}
	}
	return report;
}

void SpineAtlasExporter::get_handled_types(List<String> *out) const {
	out->push_back("SpineAtlasResource");
}

void SpineAtlasExporter::get_handled_importers(List<String> *out) const {
	out->push_back("spine.atlas");
}

String SpineAtlasExporter::get_name() const {
	return EXPORTER_NAME;
}

String SpineAtlasExporter::get_default_export_extension(const String &res_path) const {
	return "atlas";
}

// Literally all we have to do is copy the file
Error SpineSkeletonExporter::export_file(const String &out_path, const String &res_path) {
	gdre::ensure_dir(out_path.get_base_dir());
	Ref<DirAccess> da = DirAccess::create_for_path(out_path.get_base_dir());
	return da->copy(res_path, out_path);
}

Ref<ExportReport> SpineSkeletonExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	Ref<ExportReport> report = Ref<ExportReport>(memnew(ExportReport(import_infos)));
	auto dest = output_dir.path_join(import_infos->get_export_dest().trim_prefix("res://"));
	report->set_error(export_file(dest, import_infos->get_path()));
	if (report->get_error() == OK) {
		report->set_saved_path(dest);
	}
	return report;
}

void SpineSkeletonExporter::get_handled_types(List<String> *out) const {
	out->push_back("SpineSkeletonFileResource");
}

void SpineSkeletonExporter::get_handled_importers(List<String> *out) const {
	out->push_back("spine.skel");
	out->push_back("spine.json");
}

String SpineSkeletonExporter::get_name() const {
	return EXPORTER_NAME;
}

String SpineSkeletonExporter::get_default_export_extension(const String &res_path) const {
	return "spine-json";
}
