#pragma once

#include "core/object/object.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "plugin_manager/github_source.h"

class GitLabSource : public GitHubSource {
	GDCLASS(GitLabSource, GitHubSource)

private:
	static constexpr const char *_gitlab_release_api_url = "https://gitlab.com/api/v4/projects/{0}%2f{1}/releases";
	static const String gitlab_release_api_url;

	virtual const HashMap<String, String> &get_plugin_repo_map() override;
	virtual const HashMap<String, Vector<String>> &get_plugin_tag_masks() override;
	virtual const HashMap<String, Vector<String>> &get_plugin_release_file_masks() override;
	virtual const HashMap<String, Vector<String>> &get_plugin_release_file_exclude_masks() override;

	virtual const String &get_release_api_url() override;
	virtual int get_release_page_limit() override;

	virtual bool recache_release_list(const String &plugin_name) override;
	virtual String get_plugin_name() override;

public:
	GitLabSource();
	~GitLabSource();
};
