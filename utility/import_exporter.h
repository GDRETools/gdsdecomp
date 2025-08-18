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
	String log_file_location;
	Vector<String> decompiled_scripts;
	Vector<String> failed_scripts;
	String translation_export_message;
	Vector<Ref<ExportReport>> lossy_imports;
	Vector<Ref<ExportReport>> rewrote_metadata;
	Vector<Ref<ExportReport>> failed_rewrite_md;
	Vector<Ref<ExportReport>> failed_rewrite_md5;
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
	ImportExporterReport() {
		set_ver("0.0.0");
	}
	ImportExporterReport(String p_ver) {
		set_ver(p_ver);
	}

protected:
	static void _bind_methods();
};

class ImportExporter : public RefCounted {
	GDCLASS(ImportExporter, RefCounted)
	bool opt_bin2text = true;
	bool opt_export_textures = true;
	bool opt_export_samples = true;
	bool opt_export_ogg = true;
	bool opt_export_mp3 = true;
	bool opt_lossy = true;
	bool opt_export_jpg = true;
	bool opt_export_webp = true;
	bool opt_rewrite_imd_v2 = true;
	bool opt_rewrite_imd_v3 = true;
	bool opt_decompile = true;
	bool opt_only_decompile = false;
	bool opt_write_md5_files = true;
	std::atomic<int> last_completed = 0;
	std::atomic<bool> cancelled = false;
	String output_dir;

	struct ExportToken {
		Ref<ImportInfo> iinfo;
		Ref<ExportReport> report;
		bool supports_multithread;
	};

	Ref<ImportExporterReport> report;
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
	Error export_imports(const String &output_dir = "", const Vector<String> &files_to_export = {});
	Ref<ImportExporterReport> get_report();
	void reset_log();
	void reset();
	ImportExporter();
	~ImportExporter();
};

#endif
