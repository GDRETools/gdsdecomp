#ifndef GITLAB_SOURCE_H
#define GITLAB_SOURCE_H

#include "core/object/object.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "github_source.h"

class CodebergSource : public GitHubSource {
	GDCLASS(CodebergSource, GitHubSource)

private:
	static constexpr const char *_codeberg_release_api_url = "https://codeberg.org/api/v1/repos/{0}/{1}/releases?per_page=30&page={2}";
	static const String codeberg_release_api_url;

	virtual const HashMap<String, String> &get_plugin_repo_map() override;
	virtual const HashMap<String, Vector<String>> &get_plugin_tag_masks() override;
	virtual const HashMap<String, Vector<String>> &get_plugin_release_file_masks() override;
	virtual const HashMap<String, Vector<String>> &get_plugin_release_file_exclude_masks() override;
	virtual const String &get_release_api_url() override;
	virtual int get_release_page_limit() override;

	virtual String get_plugin_name() override;

public:
	CodebergSource();
	~CodebergSource();
};

#endif // GITLAB_SOURCE_H
