#ifndef ASSET_LIBRARY_SOURCE_H
#define ASSET_LIBRARY_SOURCE_H

#include "core/variant/dictionary.h"
#include "plugin_source.h"

struct EditListCache {
	double retrieved_time = 0;
	Vector<Dictionary> edit_list;
};

class AssetLibrarySource : public PluginSource {
	GDCLASS(AssetLibrarySource, PluginSource)

private:
	static HashMap<String, String> GODOT_VERSION_RELEASE_DATES;

	HashMap<uint64_t, EditListCache> edit_list_cache;

	// Helper methods
	Vector<Dictionary> get_list_of_edits(int asset_id);
	Dictionary get_edit(int edit_id);
	Vector<Dictionary> search_for_assets(const String &plugin_name, int ver_major = 0);
	Vector<int> search_for_asset_ids(const String &plugin_name, int ver_major = 0);
	Vector<Dictionary> get_assets_for_plugin(const String &plugin_name);
	Vector<String> get_version_strings_for_asset(int asset_id);

public:
	// Implementation of PluginSource interface
	Vector<String> get_plugin_version_numbers(const String &plugin_name) override;
	ReleaseInfo get_release_info(const String &plugin_name, const String &version_key) override;
	void load_cache_internal() override;
	void save_cache() override;
	bool handles_plugin(const String &plugin_name) override;
	bool is_default() override { return true; }
	String get_plugin_name() override;
};

#endif // ASSET_LIBRARY_SOURCE_H
