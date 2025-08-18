#include "csharp_exporter.h"

#include "compat/fake_csharp_script.h"
#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "utility/common.h" // For gdre namespace
#include "utility/gdre_settings.h"

void CSharpExporter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("export_file"), &CSharpExporter::export_file);
	ClassDB::bind_method(D_METHOD("export_resource"), &CSharpExporter::export_resource);
	ClassDB::bind_method(D_METHOD("supports_multithread"), &CSharpExporter::supports_multithread);
	ClassDB::bind_method(D_METHOD("get_name"), &CSharpExporter::get_name);
	ClassDB::bind_method(D_METHOD("supports_nonpack_export"), &CSharpExporter::supports_nonpack_export);
	ClassDB::bind_method(D_METHOD("get_default_export_extension"), &CSharpExporter::get_default_export_extension);
}

Error CSharpExporter::export_file(const String &out_path, const String &res_path) {
	Error err = OK;
	Ref<FakeCSharpScript> csharp_script;
	csharp_script.instantiate();
	err = csharp_script->load_source_code(res_path);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to load C# script: " + res_path + " (" + csharp_script->get_error_message() + ")");
	return _export_file(out_path, csharp_script);
}

Error CSharpExporter::_export_file(const String &out_path, Ref<FakeCSharpScript> csharp_script) {
	String source = csharp_script->get_source_code();
	ERR_FAIL_COND_V_MSG(source.is_empty(), ERR_FILE_CORRUPT, "C# script source is empty: " + csharp_script->get_script_path());

	Error err = gdre::ensure_dir(out_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to ensure output directory exists: " + out_path.get_base_dir());

	Ref<FileAccess> f = FileAccess::open(out_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_FILE_CANT_WRITE, "Cannot write to file: " + out_path);

	f->store_string(source);
	return OK;
}

Ref<ExportReport> CSharpExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	// This should not be used by the import exporter during full project recovery
	ERR_FAIL_V_MSG(Ref<ExportReport>(), "Not implemented");
}

void CSharpExporter::get_handled_types(List<String> *out) const {
	out->push_back("CSharpScript");
}

void CSharpExporter::get_handled_importers(List<String> *out) const {
}

bool CSharpExporter::supports_multithread() const {
	return true;
}

String CSharpExporter::get_name() const {
	return EXPORTER_NAME;
}

String CSharpExporter::get_default_export_extension(const String &res_path) const {
	return "cs";
}
