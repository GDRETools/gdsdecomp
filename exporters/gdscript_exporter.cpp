#include "gdscript_exporter.h"
#include "compat/fake_script.h"
#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "utility/common.h" // For gdre namespace
#include "utility/gdre_settings.h"
void GDScriptExporter::_bind_methods() {
}

Error GDScriptExporter::export_file(const String &out_path, const String &res_path) {
	Error err = OK;
	Ref<FakeGDScript> script;
	script.instantiate();
	err = script->load_source_code(res_path);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to load script: " + res_path);
	return _export_file(out_path, script);
}

Error GDScriptExporter::_export_file(const String &out_path, Ref<FakeGDScript> script) {
	String source = script->get_source_code();
	ERR_FAIL_COND_V_MSG(source.is_empty(), ERR_FILE_CORRUPT, "Script source is empty: " + script->get_script_path());

	Error err = gdre::ensure_dir(out_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to ensure output directory exists: " + out_path.get_base_dir());

	Ref<FileAccess> f = FileAccess::open(out_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_FILE_CANT_WRITE, "Cannot write to file: " + out_path);

	f->store_string(source);
	return OK;
}

Ref<ExportReport> GDScriptExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	Ref<ExportReport> report;
	report.instantiate();
	report->set_import_info(import_infos);
	report->set_message("GDScript");

	String import_path = import_infos->get_path();
	String export_path = output_dir.path_join(import_infos->get_export_dest().replace("res://", ""));

	// Handle encrypted scripts
	bool is_encrypted = import_path.get_extension().to_lower() == "gde";
	if (is_encrypted) {
		auto key = GDRESettings::get_singleton()->get_encryption_key();
		if (key.size() == 0) {
			report->set_error(ERR_UNAUTHORIZED);
			report->set_message("No encryption key provided for encrypted script");
			return report;
		}
	}
	Ref<FakeGDScript> script;
	script.instantiate();
	Error err = script->load_source_code(import_path);
	if (err != OK) {
		report->set_error(err);
		if (is_encrypted && err == ERR_UNAUTHORIZED) {
			report->set_message("Encryption key is incorrect for encrypted script");
		} else {
			report->set_message(script->get_error_message());
		}
		return report;
	}
	if (script->get_source_code().is_empty()) {
		report->set_error(ERR_FILE_CORRUPT);
		report->set_message("Script source is empty");
		return report;
	}
	// Export the script
	err = _export_file(export_path, script);
	if (err != OK) {
		report->set_error(err);
		return report;
	}
	report->set_saved_path(export_path);
	return report;
}

void GDScriptExporter::get_handled_types(List<String> *out) const {
	out->push_back("GDScript");
}

void GDScriptExporter::get_handled_importers(List<String> *out) const {
	out->push_back("script_bytecode");
}

bool GDScriptExporter::supports_multithread() const {
	return true;
}

String GDScriptExporter::get_name() const {
	return "GDScript";
}

String GDScriptExporter::get_default_export_extension(const String &res_path) const {
	return "gd";
}
