#include "csharp_exporter.h"
#include "compat/fake_csharp_script.h"
#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "utility/common.h"
#include "utility/gdre_settings.h"
#include "utility/godot_mono_decomp_wrapper.h"

void CSharpExporter::_bind_methods() {
}

Error CSharpExporter::export_file(const String &out_path, const String &res_path) {
	ERR_FAIL_COND_V_MSG(out_path.is_empty(), ERR_INVALID_PARAMETER, "Output path is empty");
	ERR_FAIL_COND_V_MSG(res_path.is_empty(), ERR_INVALID_PARAMETER, "Resource path is empty");

	Ref<GodotMonoDecompWrapper> decompiler = GDRESettings::get_singleton()->get_dotnet_decompiler();
	ERR_FAIL_COND_V_MSG(decompiler.is_null(), ERR_CANT_RESOLVE, "No dotnet decompiler loaded");

	auto source = decompiler->decompile_individual_file(res_path);
	ERR_FAIL_COND_V_MSG(source.is_empty(), ERR_CANT_RESOLVE, "Failed to decompile C# script: " + res_path);

	Error err = gdre::ensure_dir(out_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to ensure output directory exists: " + out_path.get_base_dir());

	Ref<FileAccess> f = FileAccess::open(out_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_FILE_CANT_WRITE, "Cannot write to file: " + out_path);

	f->store_string(source);
	return OK;
}

Ref<ExportReport> CSharpExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	Ref<ExportReport> report;
	report.instantiate();
	report->set_error(ERR_UNAVAILABLE);
	report->set_message("CSharpExporter::export_resource is not implemented");
	return report;
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

Vector<String> CSharpExporter::get_export_extensions(const String &res_path) const {
	Vector<String> extensions;
	extensions.push_back(get_default_export_extension(res_path));
	return extensions;
}
