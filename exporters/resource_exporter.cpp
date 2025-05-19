#include "resource_exporter.h"
#include "compat/resource_loader_compat.h"
#include "utility/common.h"

#include "core/error/error_list.h"
#include "core/error/error_macros.h"

Ref<ResourceExporter> Exporter::exporters[MAX_EXPORTERS];
int Exporter::exporter_count = 0;

Error ResourceExporter::write_to_file(const String &path, const Vector<uint8_t> &data) {
	Error err = gdre::ensure_dir(path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err, err, "Failed to create directory for " + path);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(file.is_null(), !err ? ERR_FILE_CANT_WRITE : err, "Cannot open file '" + path + "' for writing.");
	file->store_buffer(data.ptr(), data.size());
	file->flush();
	return OK;
}

int ResourceExporter::get_ver_major(const String &res_path) {
	Error err;
	auto info = ResourceCompatLoader::get_resource_info(res_path, "", &err);
	ERR_FAIL_COND_V_MSG(err != OK, 0, "Failed to get resource info for " + res_path);
	return info->ver_major; // Placeholder return value
}

bool ResourceExporter::supports_multithread() const {
	return true;
}

bool ResourceExporter::handles_import(const String &importer, const String &resource_type) const {
	if (!importer.is_empty()) {
		List<String> handled_importers;
		get_handled_importers(&handled_importers);
		for (const String &h : handled_importers) {
			if (h == importer) {
				return true;
			}
		}
		return false;
	}
	if (!resource_type.is_empty()) {
		List<String> handled_types;
		get_handled_types(&handled_types);
		for (const String &h : handled_types) {
			if (h == resource_type) {
				return true;
			}
		}
	}
	return false;
}

void ResourceExporter::get_handled_importers(List<String> *out) const {
}

void ResourceExporter::get_handled_types(List<String> *out) const {
}

Error ResourceExporter::export_file(const String &out_path, const String &res_path) {
	return ERR_UNAVAILABLE;
}

Ref<ExportReport> ResourceExporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	auto thing = Ref<ExportReport>(import_infos);
	thing->set_error(ERR_UNAVAILABLE);
	return thing;
}

String ResourceExporter::get_name() const {
	return get_class().trim_suffix("Exporter");
}

bool ResourceExporter::supports_nonpack_export() const {
	return true;
}

void ResourceExporter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_name"), &ResourceExporter::get_name);
	ClassDB::bind_method(D_METHOD("supports_nonpack_export"), &ResourceExporter::supports_nonpack_export);
	ClassDB::bind_method(D_METHOD("export_file", "out_path", "src_path"), &ResourceExporter::export_file);
	ClassDB::bind_method(D_METHOD("export_resource", "output_dir", "import_infos"), &ResourceExporter::export_resource);
	ClassDB::bind_method(D_METHOD("handles_import", "importer", "resource_type"), &ResourceExporter::handles_import);
}

void Exporter::add_exporter(Ref<ResourceExporter> exporter, bool at_front) {
	if (exporter_count < MAX_EXPORTERS) {
		if (at_front) {
			for (int i = exporter_count; i > 0; --i) {
				exporters[i] = exporters[i - 1];
			}
			exporters[0] = exporter;
		} else {
			exporters[exporter_count] = exporter;
		}
		exporter_count++;
	}
}

void Exporter::remove_exporter(Ref<ResourceExporter> exporter) {
	// Find exporter
	int i = 0;
	for (; i < exporter_count; ++i) {
		if (exporters[i] == exporter) {
			break;
		}
	}

	ERR_FAIL_COND(i >= exporter_count); // Not found

	// Shift next exporters up
	for (; i < exporter_count - 1; ++i) {
		exporters[i] = exporters[i + 1];
	}
	exporters[exporter_count - 1].unref();
	--exporter_count;
}

Ref<ResourceExporter> Exporter::get_exporter(const String &importer, const String &type) {
	for (int i = 0; i < exporter_count; ++i) {
		if (exporters[i]->handles_import(importer, type)) {
			return exporters[i];
		}
	}
	return Ref<ResourceExporter>();
}

Ref<ExportReport> Exporter::export_resource(const String &output_dir, Ref<ImportInfo> import_infos) {
	String import_type = import_infos->get_type();
	String importer = import_infos->get_importer();
	auto exporter = get_exporter(importer, import_type);
	if (exporter.is_null()) {
		Ref<ExportReport> report{ memnew(ExportReport(import_infos)) };
		report->set_message("No exporter found for importer " + importer + " and type: " + import_type);
		report->set_error(ERR_UNAVAILABLE);
		return report;
	}
	return exporter->export_resource(output_dir, import_infos);
}

Error Exporter::export_file(const String &out_path, const String &res_path) {
	String importer = "";
	Ref<ResourceInfo> info = ResourceCompatLoader::get_resource_info(res_path, importer);
	String type = info.is_valid() ? info->type : "";
	auto exporter = get_exporter(importer, type);
	if (exporter.is_null()) {
		return ERR_UNAVAILABLE;
	}
	return exporter->export_file(out_path, res_path);
}

Ref<ResourceExporter> Exporter::get_nonpack_exporter_from_path(const String &res_path) {
	String importer = "";
	Ref<ResourceInfo> info = ResourceCompatLoader::get_resource_info(res_path, importer);
	String type = info.is_valid() ? info->type : "";
	for (int i = 0; i < exporter_count; ++i) {
		if (exporters[i]->handles_import(importer, type) && exporters[i]->supports_nonpack_export()) {
			return exporters[i];
		}
	}
	return Ref<ResourceExporter>();
}

void Exporter::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("add_exporter", "exporter", "at_front"), &Exporter::add_exporter);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("remove_exporter", "exporter"), &Exporter::remove_exporter);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("export_resource", "output_dir", "import_infos"), &Exporter::export_resource);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("export_file", "out_path", "res_path"), &Exporter::export_file);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_nonpack_exporter_from_path", "res_path"), &Exporter::get_nonpack_exporter_from_path);
}
