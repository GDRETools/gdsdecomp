#pragma once

#include "bytecode/bytecode_base.h"

void register_decomp_versions();
GDScriptDecomp *create_decomp_for_commit(int p_commit_hash);
Vector<Ref<GDScriptDecomp>> get_decomps_for_bytecode_ver(int bytecode_version, bool include_dev = false);
struct GDScriptDecompVersion {
	// automatically updated by `bytecode_generator.py`
	static constexpr int LATEST_GDSCRIPT_COMMIT = 0xebc36a7;

	static Vector<GDScriptDecompVersion> decomp_versions;
	static int number_of_custom_versions;
	int commit = 0;
	String name;
	int bytecode_version;
	bool is_dev;
	String min_version;
	String max_version;
	int parent;
	Dictionary custom;

	bool is_custom() const {
		return custom.size() > 0;
	}

	Ref<GodotVer> get_min_version() const {
		return GodotVer::parse(min_version);
	}
	Ref<GodotVer> get_max_version() const {
		return GodotVer::parse(max_version);
	}

	int get_major_version() const {
		if (min_version.is_empty()) {
			return 0;
		}
		switch (min_version[0]) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				return min_version[0] - '0';
			default:
				return 0;
		}
	}

	static GDScriptDecompVersion create_version_from_custom_def(Dictionary p_custom_def);
	static GDScriptDecompVersion create_derived_version_from_custom_def(int revision, Dictionary p_custom_def);
	static int register_decomp_version_custom(Dictionary p_custom_def);
	static int register_derived_decomp_version_custom(int revision, Dictionary p_custom_def);

	GDScriptDecomp *create_decomp() const;
};
Vector<GDScriptDecompVersion> get_decomp_versions(bool include_dev = true, int ver_major = 0);
