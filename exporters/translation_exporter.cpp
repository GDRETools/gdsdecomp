#include "translation_exporter.h"

#include "compat/optimized_translation_extractor.h"
#include "compat/resource_loader_compat.h"
#include "exporters/export_report.h"
#include "utility/common.h"
#include "utility/gdre_settings.h"

#include "core/error/error_list.h"
#include "core/string/optimized_translation.h"
#include "core/string/translation.h"
#include "core/string/ustring.h"

Error TranslationExporter::export_file(const String &out_path, const String &res_path) {
	// Implementation for exporting translation files
	String iinfo_path = res_path.get_basename().get_basename() + ".csv.import";
	auto iinfo = ImportInfo::load_from_file(iinfo_path);
	ERR_FAIL_COND_V_MSG(iinfo.is_null(), ERR_CANT_OPEN, "Cannot find import info for translation.");
	Ref<ExportReport> report = export_resource(out_path.get_base_dir(), iinfo);
	ERR_FAIL_COND_V_MSG(report->get_error(), report->get_error(), "Failed to export translation resource.");
	return OK;
}

#define TEST_TR_KEY(key)                          \
	test = default_translation->get_message(key); \
	if (test == s) {                              \
		return key;                               \
	}                                             \
	key = key.to_upper();                         \
	test = default_translation->get_message(key); \
	if (test == s) {                              \
		return key;                               \
	}                                             \
	key = key.to_lower();                         \
	test = default_translation->get_message(key); \
	if (test == s) {                              \
		return key;                               \
	}

String guess_key_from_tr(String s, Ref<Translation> default_translation) {
	static const Vector<String> prefixes = { "$$", "##", "TR_", "KEY_TEXT_" };
	String key = s;
	String test;
	TEST_TR_KEY(key);
	String str = s;
	//remove punctuation
	str = str.replace("\n", "").replace(".", "").replace("…", "").replace("!", "").replace("?", "");
	str = str.strip_escapes().strip_edges().replace(" ", "_");
	key = str;
	TEST_TR_KEY(key);
	// Try adding prefixes
	for (String prefix : prefixes) {
		key = prefix + str;
		TEST_TR_KEY(key);
	}
	// failed
	return "";
}

String find_common_prefix(const HashMap<StringName, StringName> &key_to_msg) {
	// among all the keys in the vector, find the common prefix
	if (key_to_msg.size() == 0) {
		return "";
	}
	String prefix;
	auto add_to_prefix_func = [&](int i) {
		char32_t candidate = 0;
		for (const auto &E : key_to_msg) {
			auto &s = E.key;
			if (!s.is_empty()) {
				if (s.length() - 1 < i) {
					return false;
				}
				candidate = s[i];
				break;
			}
		}
		if (candidate == 0) {
			return false;
		}
		for (const auto &E : key_to_msg) {
			auto &s = E.key;
			if (!s.is_empty()) {
				if (s.length() - 1 < i || s[i] != candidate) {
					return false;
				}
			}
		}
		prefix += candidate;
		return true;
	};

	for (int i = 0; i < 100; i++) {
		if (!add_to_prefix_func(i)) {
			break;
		}
	}
	return prefix;
}

namespace {
StringName get_msg(Ref<Translation> default_translation, const String &key) {
	return default_translation->get_message(key);
};
} //namespace

Ref<ExportReport> TranslationExporter::export_resource(const String &output_dir, Ref<ImportInfo> iinfo) {
	// Implementation for exporting resources related to translations
	Error err = OK;
	// translation files are usually imported from one CSV and converted to multiple "<LOCALE>.translation" files
	String default_locale = GDRESettings::get_singleton()->pack_has_project_config() && GDRESettings::get_singleton()->has_project_setting("locale/fallback")
			? GDRESettings::get_singleton()->get_project_setting("locale/fallback")
			: "en";
	if (iinfo->get_dest_files().size() == 1) {
		default_locale = iinfo->get_dest_files()[0].get_basename().get_extension();
	}
	print_verbose("Exporting translation file " + iinfo->get_export_dest());
	Vector<Ref<Translation>> translations;
	Vector<Vector<StringName>> translation_messages;
	Ref<Translation> default_translation;
	Vector<StringName> default_messages;
	String header = "key";
	Vector<StringName> keys;
	Ref<ExportReport> report = memnew(ExportReport(iinfo));
	report->set_error(ERR_CANT_ACQUIRE_RESOURCE);
	for (String path : iinfo->get_dest_files()) {
		Ref<Translation> tr = ResourceCompatLoader::non_global_load(path, "", &err);
		ERR_FAIL_COND_V_MSG(err != OK, report, "Could not load translation file " + iinfo->get_path());
		ERR_FAIL_COND_V_MSG(!tr.is_valid(), report, "Translation file " + iinfo->get_path() + " was not valid");
		String locale = tr->get_locale();
		header += "," + locale;
		List<StringName> message_list;
		Vector<StringName> messages;
		if (tr->get_class_name() == "OptimizedTranslation") {
			Ref<OptimizedTranslation> otr = tr;
			Ref<OptimizedTranslationExtractor> ote;
			ote.instantiate();
			ote->set("locale", locale);
			ote->set("hash_table", otr->get("hash_table"));
			ote->set("bucket_table", otr->get("bucket_table"));
			ote->set("strings", otr->get("strings"));
			ote->get_message_value_list(&message_list);
			for (auto message : message_list) {
				messages.push_back(message);
			}
		} else {
			// We have a real translation class, get the keys
			if (locale.to_lower() == default_locale.to_lower()) {
				List<StringName> key_list;
				tr->get_message_list(&key_list);
				for (auto key : key_list) {
					keys.push_back(key);
				}
			}
			Dictionary msgdict = tr->get("messages");
			Array values = msgdict.values();
			for (int i = 0; i < values.size(); i++) {
				messages.push_back(values[i]);
			}
		}
		if (locale.to_lower() == default_locale.to_lower()) {
			default_messages = messages;
			default_translation = tr;
		}
		translation_messages.push_back(messages);
		translations.push_back(tr);
	}
	// We can't recover the keys from Optimized translations, we have to guess
	int missing_keys = 0;

	if (default_translation.is_null()) {
		report->set_error(ERR_FILE_MISSING_DEPENDENCIES);
		ERR_FAIL_V_MSG(report, "No default translation found for " + iinfo->get_path());
	}
	HashSet<String> resource_strings;
	HashMap<StringName, StringName> key_to_message;
	String prefix;
	bool keys_have_spaces = false;
	bool keys_have_periods = false;
	bool keys_have_dashes = false;
	bool keys_are_all_upper = true;
	bool keys_are_all_lower = true;
	bool keys_are_all_ascii = true;
	auto string_is_ascii = [](const String &s) {
		for (int i = 0; i < s.length(); i++) {
			if (s[i] > 127) {
				return false;
			}
		}
		return true;
	};
	auto set_key_stuff = [&](const String &key) {
		if (!keys_have_spaces && key.contains(" ")) {
			keys_have_spaces = true;
		}
		if (!keys_have_periods && key.contains(".")) {
			keys_have_periods = true;
		}
		if (!keys_have_dashes && key.contains("-")) {
			keys_have_dashes = true;
		}
		if (keys_are_all_upper && key.to_upper() != key) {
			keys_are_all_upper = false;
		}
		if (keys_are_all_lower && key.to_lower() != key) {
			keys_are_all_lower = false;
		}
		if (keys_are_all_ascii) {
			keys_are_all_ascii = string_is_ascii(key);
		}
	};
	auto try_key = [&](const String &key) {
		auto msg = default_translation->get_message(key);
		if (!msg.is_empty()) {
			set_key_stuff(key);
			if (key_to_message.has(key)) {
				if (msg != key_to_message[key]) {
					WARN_PRINT(vformat("Found matching key '%s' for message '%s' but key is used for message '%s'", key, msg, key_to_message[key]));
				} else {
					return;
				}
			}
			key_to_message[key] = msg;
		}
	};
	if (keys.size() == 0) {
		for (const StringName &msg : default_messages) {
			String key = guess_key_from_tr(msg, default_translation);
			if (key.is_empty()) {
				missing_keys++;
			} else {
				set_key_stuff(key);
				key_to_message[key] = msg;
			}
		}
		// We need to load all the resource strings in all resources to find the keys
		if (missing_keys) {
			if (!GDRESettings::get_singleton()->loaded_resource_strings()) {
				GDRESettings::get_singleton()->load_all_resource_strings();
			}
			GDRESettings::get_singleton()->get_resource_strings(resource_strings);
			for (const String &key : resource_strings) {
				try_key(key);
			}
			// We didn't find all the keys, look for keys in every PART of the resource strings
			if (key_to_message.size() != default_messages.size()) {
				prefix = find_common_prefix(key_to_message);
				bool has_common_prefix = !prefix.is_empty();
				// Only do this if no keys have spaces or they have a common prefix; otherwise this is practically useless to do
				if (!keys_have_spaces || has_common_prefix) {
					Ref<RegEx> re;
					re.instantiate();
					re->compile("\\b" + prefix + "[\\w\\d\\-\\_\\.]+\\b");
					for (const String &res_s : resource_strings) {
						if ((!has_common_prefix || res_s.contains(prefix))) {
							auto matches = re->search_all(res_s);
							for (const Ref<RegExMatch> match : matches) {
								for (const String &key : match->get_strings()) {
									try_key(key);
								}
							}
						}
					}
					// If we're still missing keys and no keys have spaces, we try combining every string with every other string
					// We first filter them according to common characteristics so that this doesn't take forever.
					if (key_to_message.size() != default_messages.size()) {
						HashSet<String> filtered_resource_strings;
						for (const String &res_s : resource_strings) {
							if (res_s.contains(" ")) {
								continue;
							}
							if (res_s.begins_with("res://")) {
								continue;
							}
							if (!prefix.is_empty() && !res_s.begins_with(prefix)) {
								continue;
							}
							if (keys_are_all_upper && res_s.to_upper() != res_s) {
								continue;
							}
							if (keys_are_all_lower && res_s.to_lower() != res_s) {
								continue;
							}
							if (keys_are_all_ascii && !string_is_ascii(res_s)) {
								continue;
							}
							filtered_resource_strings.insert(res_s);
						}
						for (const String &res_s : filtered_resource_strings) {
							for (const String &res_s2 : filtered_resource_strings) {
								try_key(res_s + res_s2);
							}
						}
					}
				}
			}
		}
		missing_keys = 0;
		keys.clear();
		for (int i = 0; i < default_messages.size(); i++) {
			auto &msg = default_messages[i];
			bool found = false;
			bool has_match = false;
			StringName matching_key;
			for (auto &E : key_to_message) {
				if (E.value == msg) {
					has_match = true;
					matching_key = E.key;
					if (!keys.has(E.key)) {
						keys.push_back(E.key);
						found = true;
						break;
					}
				}
			}
			if (!found) {
				if (has_match) {
					if (msg != key_to_message[matching_key]) {
						WARN_PRINT(vformat("Found matching key '%s' for message '%s' but key is used for message '%s'", matching_key, msg, key_to_message[matching_key]));
					} else {
						print_verbose(vformat("WARNING: Found duplicate key '%s' for message '%s'", matching_key, msg));
						keys.push_back(matching_key);
						continue;
					}
				} else {
					print_verbose(vformat("Could not find key for message '%s'", msg));
				}
				missing_keys++;
				keys.push_back("<MISSING KEY " + String(msg).split("\n")[0] + ">");
			}
		}
	}
	header += "\n";
	String export_dest = iinfo->get_export_dest();
	// If greater than 15% of the keys are missing, we save the file to the export directory.
	// The reason for this threshold is that the translations may contain keys that are not currently in use in the project.
	bool resave = missing_keys > (default_messages.size() * threshold);
	if (resave) {
		iinfo->set_export_dest("res://.assets/" + iinfo->get_export_dest().replace("res://", ""));
	}
	String output_path = output_dir.simplify_path().path_join(iinfo->get_export_dest().replace("res://", ""));
	err = gdre::ensure_dir(output_path.get_base_dir());
	ERR_FAIL_COND_V(err, report);
	Ref<FileAccess> f = FileAccess::open(output_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V(err, report);
	ERR_FAIL_COND_V(f.is_null(), report);
	// Set UTF-8 BOM (required for opening with Excel in UTF-8 format, works with all Godot versions)
	f->store_8(0xef);
	f->store_8(0xbb);
	f->store_8(0xbf);
	f->store_string(header);
	for (int i = 0; i < keys.size(); i++) {
		Vector<String> line_values;
		line_values.push_back(keys[i]);
		for (auto messages : translation_messages) {
			if (i >= messages.size()) {
				line_values.push_back("");
			} else {
				line_values.push_back(messages[i]);
			}
		}
		f->store_csv_line(line_values, ",");
	}
	f->flush();
	f = Ref<FileAccess>();
	report->set_error(OK);
	if (missing_keys) {
		String translation_export_message = "WARNING: Could not recover " + itos(missing_keys) + " keys for " + iinfo->get_source_file() + "\n";
		if (resave) {
			translation_export_message += "Saved " + iinfo->get_source_file().get_file() + " to " + iinfo->get_export_dest() + "\n";
		}
		report->set_message(translation_export_message);
	}
	report->set_new_source_path(iinfo->get_export_dest());
	report->set_saved_path(output_path);
	return report;
}

bool TranslationExporter::handles_import(const String &importer, const String &resource_type) const {
	// Check if the exporter can handle the given importer and resource type
	return importer == "translation_csv" || resource_type == "Translation";
}

void TranslationExporter::get_handled_types(List<String> *out) const {
	// Add the types of resources that this exporter can handle
	out->push_back("Translation");
}

void TranslationExporter::get_handled_importers(List<String> *out) const {
	// Add the importers that this exporter can handle
	out->push_back("translation_csv");
}