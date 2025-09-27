#include "codeberg_source.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "utility/common.h"
#include "utility/plugin_manager.h"

const String CodebergSource::codeberg_release_api_url = _codeberg_release_api_url;
namespace {
static const HashMap<String, Vector<String>> tag_masks = {
	{ "godotsteam", { "*gdn*", "*gde*" } },
	{ "godotsteam_server", { "*gdn*", "*gde*" } },
};

static const HashMap<String, Vector<String>> release_file_masks = {};

static const HashMap<String, Vector<String>> release_file_exclude_masks = {};

static const HashMap<String, String> plugin_map = {
	{ "godotsteam", "https://codeberg.org/GodotSteam/GodotSteam" },
	{ "godotsteam_server", "https://codeberg.org/GodotSteam/GodotSteam-Server" },
};
} // namespace

CodebergSource::CodebergSource() {
	// Initialize any necessary resources
}

CodebergSource::~CodebergSource() {
	// Clean up any resources
}

const HashMap<String, String> &CodebergSource::get_plugin_repo_map() {
	return plugin_map;
}

const HashMap<String, Vector<String>> &CodebergSource::get_plugin_tag_masks() {
	return tag_masks;
}

const HashMap<String, Vector<String>> &CodebergSource::get_plugin_release_file_masks() {
	return release_file_masks;
}

const HashMap<String, Vector<String>> &CodebergSource::get_plugin_release_file_exclude_masks() {
	return release_file_exclude_masks;
}

String CodebergSource::get_plugin_name() {
	return "codeberg";
}

// Codeberg API behaves like the GitHub API, so all we have to do is return the correct URL and the page limit
const String &CodebergSource::get_release_api_url() {
	return codeberg_release_api_url;
}

int CodebergSource::get_release_page_limit() {
	return 30;
}
