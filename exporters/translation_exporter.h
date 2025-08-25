#pragma once
#include "exporters/resource_exporter.h"

class Translation;
class TranslationExporter : public ResourceExporter {
	GDCLASS(TranslationExporter, ResourceExporter);

	HashSet<String> all_keys_found;

	static Error parse_csv(const String &csv_path, HashMap<String, Vector<String>> &new_messages, int64_t &missing_keys, bool &has_non_empty_lines_without_key, int64_t &non_empty_line_count);
	static int64_t _count_non_empty_messages(const Vector<Vector<String>> &translation_messages);
	static Error get_translations(Ref<ImportInfo> iinfo, String &default_locale, Ref<Translation> &default_translation, Vector<Ref<Translation>> &translations, Vector<String> &keys);

protected:
	static void _bind_methods();

public:
	static constexpr float threshold = 0.15; // TODO: put this in the project configuration

	static constexpr const char *const EXPORTER_NAME = "Translation";
	static constexpr const char *const MISSING_KEY_PREFIX = "<!MissingKey:";
	static constexpr const char *const MISSING_KEY_FORMAT = "<!MissingKey:%d:%s>";

	virtual Error export_file(const String &out_path, const String &res_path) override;
	virtual Ref<ExportReport> export_resource(const String &output_dir, Ref<ImportInfo> import_infos) override;
	virtual void get_handled_types(List<String> *out) const override;
	virtual void get_handled_importers(List<String> *out) const override;
	virtual bool supports_multithread() const override { return false; }
	virtual String get_name() const override;
	virtual bool supports_nonpack_export() const override { return false; }
	virtual String get_default_export_extension(const String &res_path) const override;

	static TypedDictionary<String, Vector<String>> get_messages_from_translation(Ref<ImportInfo> translation_info);
	static TypedDictionary<String, Vector<String>> get_csv_messages(const String &csv_path, Dictionary ret_info);
	static int64_t count_non_empty_messages_from_info(Ref<ImportInfo> translation_info);
	static int64_t count_non_empty_messages(const TypedDictionary<String, Vector<String>> &translation_messages);

	static Error patch_translations(const String &output_dir, const String &csv_path, Ref<ImportInfo> translation_info, const Vector<String> &locales_to_patch, Dictionary r_file_map);
	static Error patch_project_config(const String &output_dir, Dictionary file_map);
};
