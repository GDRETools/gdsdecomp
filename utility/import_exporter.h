#ifndef IMPORT_EXPORTER_H
#define IMPORT_EXPORTER_H

#include "compat/resource_import_metadatav2.h"
#include "exporters/export_report.h"
#include "import_info.h"
#include "utility/godotver.h"

#include "core/object/object.h"
#include "core/object/ref_counted.h"

class ImportExporter;

class ImportExporterReport : public RefCounted {
	GDCLASS(ImportExporterReport, RefCounted)
	friend class ImportExporter;
	bool had_encryption_error = false;
	bool mono_detected = false;
	bool godotsteam_detected = false;
	bool exported_scenes = false;
	bool show_headless_warning = false;
	int session_files_total = 0;
	String gdre_version;
	String log_file_location;
	Vector<String> decompiled_scripts;
	Vector<String> failed_scripts;
	String translation_export_message;
	Vector<Ref<ExportReport>> failed;
	Vector<Ref<ExportReport>> success;
	Vector<Ref<ExportReport>> not_converted;
	Vector<String> failed_plugin_cfg_create;
	Vector<String> failed_gdnative_copy;
	Vector<String> unsupported_types;
	Vector<Dictionary> downloaded_plugins;
	Ref<GodotVer> ver;
	// TODO: add the rest of the options
	bool opt_lossy = true;

public:
	constexpr static const int REPORT_VERSION = 1;
	void set_ver(String p_ver);
	String get_ver();
	void set_lossy_opt(bool lossy) {
		opt_lossy = lossy;
	}

	Dictionary get_totals();
	Dictionary get_unsupported_types();
	Dictionary get_section_labels();

	Dictionary get_session_notes();
	String get_totals_string();
	Dictionary get_report_sections();
	String get_report_string();
	String get_editor_message_string();
	String get_detected_unsupported_resource_string();

	String get_session_notes_string();

	String get_log_file_location();
	Vector<String> get_decompiled_scripts();
	Vector<String> get_failed_scripts();
	String get_translation_export_message();
	Vector<Ref<ExportReport>> _get_lossy_imports() const;
	Vector<Ref<ExportReport>> _get_rewrote_metadata() const;
	Vector<Ref<ExportReport>> _get_failed_rewrite_md() const;
	Vector<Ref<ExportReport>> _get_failed_rewrite_md5() const;
	TypedArray<ExportReport> get_lossy_imports() const;
	TypedArray<ExportReport> get_rewrote_metadata() const;
	TypedArray<ExportReport> get_failed_rewrite_md() const;
	TypedArray<ExportReport> get_failed_rewrite_md5() const;
	TypedArray<ExportReport> get_failed() const;
	TypedArray<ExportReport> get_successes() const;
	TypedArray<ExportReport> get_not_converted() const;
	TypedArray<Dictionary> get_downloaded_plugins() const;
	Vector<String> get_failed_plugin_cfg_create() const;
	Vector<String> get_failed_gdnative_copy() const;

	bool is_steam_detected() const;
	bool is_mono_detected() const;

	void print_report();
	String get_gdre_version() const;
	ImportExporterReport();
	ImportExporterReport(String p_ver);

	Dictionary to_json() const;
	static Ref<ImportExporterReport> from_json(const Dictionary &p_json);

	bool is_equal_to(const Ref<ImportExporterReport> &p_import_exporter_report) const;

protected:
	static void _bind_methods();
};

struct FileInfoComparator;
class ImportExporter : public RefCounted {
	GDCLASS(ImportExporter, RefCounted)
	String output_dir;

	friend FileInfoComparator;

	struct ExportToken {
		Ref<ImportInfo> iinfo;
		Ref<ExportReport> report;
		bool supports_multithread;
	};

	Ref<ImportExporterReport> report;
	HashMap<String, Ref<ExportReport>> src_to_report;
	HashSet<String> textfile_extensions;
	HashSet<String> other_file_extensions;
	HashSet<String> valid_extensions;

	// for the cache file
	struct FileInfo {
		String file;
		String type;
		String resource_script_class; // If any resource has script with a global class name, its found here.
		ResourceUID::ID uid = ResourceUID::INVALID_ID;
		uint64_t modified_time = 0;
		uint64_t import_modified_time = 0;
		String import_md5;
		Vector<String> import_dest_paths;
		bool import_valid = false;
		String import_group_file;
		Vector<String> deps;
		bool verified = false; //used for checking changes
		// This is for script resources only.
		struct ScriptClassInfo {
			String name;
			String extends;
			String icon_path;
			bool is_abstract = false;
			bool is_tool = false;
		};
		ScriptClassInfo class_info;
	};

	void update_exts();

	void save_filesystem_cache(const Vector<std::shared_ptr<FileInfo>> &reports, String output_dir);
	Vector<std::shared_ptr<FileInfo>> read_filesystem_cache(const String &p_path);

	void _do_file_info(uint32_t i, std::shared_ptr<FileInfo> *file_info);
	String get_file_info_description(uint32_t i, std::shared_ptr<FileInfo> *file_info);
	void _do_export(uint32_t i, ExportToken *tokens);
	String get_export_token_description(uint32_t i, ExportToken *tokens);
	Error handle_auto_converted_file(const String &autoconverted_file);
	Error rewrite_import_source(const String &rel_dest_path, const Ref<ImportInfo> &iinfo);
	void report_unsupported_resource(const String &type, const String &format_name, const String &importer, const String &import_path);
	Error remove_remap_and_autoconverted(const String &src, const String &dst);
	void rewrite_metadata(ExportToken &token);
	Error unzip_and_copy_addon(const Ref<ImportInfoGDExt> &iinfo, const String &zip_path);
	Error _reexport_translations(Vector<ExportToken> &non_multithreaded_tokens, size_t token_size, Ref<EditorProgressGDDC> pr);
	void recreate_uid_file(const String &src_path, bool is_import, const HashSet<String> &files_to_export_set);
	Error recreate_plugin_config(const String &plugin_cfg_path);
	Error recreate_plugin_configs();

protected:
	static void _bind_methods();

public:
	Error export_imports(const String &output_dir, const Vector<String> &files_to_export = {});
	Ref<ImportExporterReport> get_report();
	void reset_log();
	void reset();
	ImportExporter();
	~ImportExporter();
};

#endif
