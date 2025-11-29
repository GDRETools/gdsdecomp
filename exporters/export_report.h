#pragma once
#include "core/object/ref_counted.h"
#include "utility/import_info.h"
#include "utility/task_manager.h"
#include <sys/types.h>

class ExportReport : public RefCounted {
	GDCLASS(ExportReport, RefCounted);

public:
	enum MetadataStatus {
		NOT_DIRTY,
		NOT_IMPORTABLE,
		DEPENDENCY_CHANGED,
		REWRITTEN,
		FAILED,
		MD5_FAILED
	};

private:
	Ref<ImportInfo> import_info;
	String exporter;
	String message;
	String saved_path;
	Vector<String> resources_used;
	String unsupported_format_type;
	Vector<String> error_messages;
	Vector<String> message_detail;
	Dictionary extra_info;
	TaskManager::DownloadTaskID download_task_id = -1;
	Error error = OK;
	ImportInfo::LossType loss_type = ImportInfo::LossType::LOSSLESS;
	MetadataStatus rewrote_metadata = NOT_DIRTY;

protected:
	static void _bind_methods();

public:
	String actual_type;
	String script_class;
	Vector<String> dependencies;
	int64_t modified_time = -1;
	int64_t import_modified_time = -1;
	String import_md5;

	void set_exporter(const String &p_exporter) { exporter = p_exporter; }
	String get_exporter() const { return exporter; }

	// setters and getters
	void set_message(const String &p_message) { message = p_message; }
	String get_message() const { return message; }

	void set_import_info(const Ref<ImportInfo> &p_import_info) { import_info = p_import_info; }
	Ref<ImportInfo> get_import_info() const { return import_info; }

	void set_resources_used(const Vector<String> &p_resources_used) {
		resources_used = p_resources_used;
	}
	Vector<String> get_resources_used() const { return resources_used; }

	String get_source_path() const { return import_info.is_valid() ? import_info->get_source_file() : ""; }

	String get_new_source_path() const { return import_info.is_valid() ? import_info->get_export_dest() : ""; }

	void set_saved_path(const String &p_saved_path) { saved_path = p_saved_path; }
	String get_saved_path() const { return saved_path; }

	void set_download_task_id(TaskManager::DownloadTaskID p_download_task_id) { download_task_id = p_download_task_id; }
	TaskManager::DownloadTaskID get_download_task_id() const { return download_task_id; }

	void set_error(Error p_error) { error = p_error; }
	Error get_error() const { return error; }

	void set_loss_type(ImportInfo::LossType p_loss_type) { loss_type = p_loss_type; }
	ImportInfo::LossType get_loss_type() const { return loss_type; }

	void set_unsupported_format_type(const String &p_type) { unsupported_format_type = p_type; }
	String get_unsupported_format_type() const { return unsupported_format_type; }

	void set_rewrote_metadata(MetadataStatus p_status) { rewrote_metadata = p_status; }
	MetadataStatus get_rewrote_metadata() const { return rewrote_metadata; }

	void append_error_messages(const Vector<String> &p_error_messages) { error_messages.append_array(p_error_messages); }
	void clear_error_messages() { error_messages.clear(); }
	Vector<String> get_error_messages() const { return error_messages; }

	void append_message_detail(const Vector<String> &p_message_detail) { message_detail.append_array(p_message_detail); }
	void clear_message_detail() { message_detail.clear(); }
	Vector<String> get_message_detail() const { return message_detail; }

	void set_extra_info(const Dictionary &p_extra_info) { extra_info = p_extra_info; }
	Dictionary get_extra_info() const { return extra_info; }

	String get_path() const { return import_info.is_valid() ? import_info->get_path() : ""; }

	Dictionary to_json() const;
	static Ref<ExportReport> from_json(const Dictionary &p_json);

	bool is_equal_to(const Ref<ExportReport> &p_export_report) const;

	ExportReport() {}
	ExportReport(Ref<ImportInfo> p_import_info, const String &p_exporter = "") :
			import_info(p_import_info), exporter(p_exporter) {}
};

VARIANT_ENUM_CAST(ExportReport::MetadataStatus);
