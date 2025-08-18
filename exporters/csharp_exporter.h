#pragma once

#include "core/error/error_list.h"
#include "core/object/ref_counted.h"
#include "exporters/resource_exporter.h"
#include "utility/import_info.h"

class FakeCSharpScript;
class CSharpExporter : public ResourceExporter {
	GDCLASS(CSharpExporter, ResourceExporter);

	virtual Error _export_file(const String &out_path, Ref<FakeCSharpScript> res);

protected:
	static void _bind_methods();

public:
	static constexpr const char *const EXPORTER_NAME = "CSharpScript";
	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
	virtual bool supports_multithread() const override;
	virtual String get_name() const override;
	virtual bool supports_nonpack_export() const override { return false; }
	virtual String get_default_export_extension(const String &res_path) const override;
	CSharpExporter() = default;
	~CSharpExporter() = default;
};
