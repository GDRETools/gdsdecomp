#pragma once
#include "core/error/error_list.h"
#include "core/object/ref_counted.h"
#include "exporters/export_report.h"
#include "utility/import_exporter.h"
#include "utility/import_info.h"

class ResourceExporter : public RefCounted {
	GDCLASS(ResourceExporter, RefCounted);

protected:
	static void _bind_methods();
	static int get_ver_major(const String &res_path);
	static Error write_to_file(const String &path, const Vector<uint8_t> &data);

public:
	virtual String get_name() const;
	virtual Error export_file(const String &out_path, const String &res_path);
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos);
	virtual bool handles_import(const String &importer, const String &resource_type = String()) const;
	virtual void get_handled_types(List<String> *out) const;
	virtual void get_handled_importers(List<String> *out) const;
	virtual bool supports_multithread() const;
	virtual bool supports_nonpack_export() const;
	virtual String get_default_export_extension(const String &res_path) const;
};

class Exporter : public Object {
	GDCLASS(Exporter, Object);

	friend class ImportExporter;
	enum {
		MAX_EXPORTERS = 64,
	};
	static Ref<ResourceExporter> exporters[MAX_EXPORTERS];
	static int exporter_count;

protected:
	static void _bind_methods();

public:
	static void add_exporter(Ref<ResourceExporter> exporter, bool at_front = false);
	static void remove_exporter(Ref<ResourceExporter> exporter);
	static Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos);
	static Error export_file(const String &out_path, const String &res_path);
	static Ref<ResourceExporter> get_exporter(const String &importer, const String &type);
	static Ref<ResourceExporter> get_exporter_from_path(const String &res_path, bool p_nonpack_export = false);
	static bool is_exportable_resource(const String &res_path);
	static String get_default_export_extension(const String &res_path);
};
