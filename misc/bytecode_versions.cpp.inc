//_PRELUDE_
// clang-format off

#include "bytecode/bytecode_versions.h"

#include "bytecode/bytecode_custom.h"
//_BYTECODE_HEADERS_

void register_decomp_versions() {
//_BYTECODE_CLASSDB_REGISTER_
	ClassDB::register_class<GDScriptDecomp_custom>();
}

GDScriptDecomp *GDScriptDecompVersion::create_decomp_for_commit(int p_commit_hash) {
	switch (p_commit_hash) {
//_BYTECODE_CASE_STATEMENTS_
		default:
			for (int i = 0; i < GDScriptDecompVersion::decomp_versions.size(); i++) {
				if (GDScriptDecompVersion::decomp_versions[i].commit == p_commit_hash) {
					if (GDScriptDecompVersion::decomp_versions[i].is_custom()){
						return GDScriptDecompVersion::decomp_versions[i].create_decomp();
					}
					ERR_FAIL_V_MSG(nullptr, "Bytecode version is not custom?!!?!?!?!?!?!?!?!?!?!?");
				}
			}
			return nullptr;
	}
}

Vector<GDScriptDecompVersion> GDScriptDecompVersion::decomp_versions = {
//_BYTECODE_DECOMP_VERSIONS_
};

int GDScriptDecompVersion::number_of_custom_versions = 0;

Vector<Ref<GDScriptDecomp>> GDScriptDecompVersion::get_decomps_for_bytecode_ver(int bytecode_version, bool include_dev) {
	Vector<Ref<GDScriptDecomp>> decomps;
	for (int i = 0; i < GDScriptDecompVersion::decomp_versions.size(); i++) {
		if (GDScriptDecompVersion::decomp_versions[i].bytecode_version == bytecode_version && (include_dev || !GDScriptDecompVersion::decomp_versions[i].is_dev)) {
			decomps.push_back(Ref<GDScriptDecomp>(create_decomp_for_commit(GDScriptDecompVersion::decomp_versions[i].commit)));
		}
	}
	return decomps;
}

Vector<GDScriptDecompVersion> GDScriptDecompVersion::get_decomp_versions(bool include_dev, int ver_major) {
	Vector<GDScriptDecompVersion> versions;
	String ver_major_str = itos(ver_major);
	for (int i = 0; i < GDScriptDecompVersion::decomp_versions.size(); i++) {
		if (GDScriptDecompVersion::decomp_versions[i].commit == 0xfffffff || GDScriptDecompVersion::decomp_versions[i].commit == 0x0000000) {
			continue;
		}
		if (!include_dev && GDScriptDecompVersion::decomp_versions[i].is_dev) {
			continue;
		}
		if (ver_major > 0 && GDScriptDecompVersion::decomp_versions[i].get_major_version() != ver_major) {
			continue;
		}
		versions.push_back(GDScriptDecompVersion(GDScriptDecompVersion::decomp_versions[i]));
	}
	return versions;
}

GDScriptDecomp *GDScriptDecompVersion::create_decomp() const {
	if (is_custom()) {
		return GDScriptDecomp_custom::_create_from_json(custom);
	}
	return create_decomp_for_commit(commit);
}

GDScriptDecompVersion GDScriptDecompVersion::create_version_from_custom_def(Dictionary p_custom_def){
	static constexpr int CUSTOM_PREFIX = 0xf0000000;
	int revision = GDScriptDecompVersion::number_of_custom_versions | CUSTOM_PREFIX;
	String rev_str = String::num_int64(static_cast<uint32_t>(revision), 16).to_lower();
	p_custom_def["bytecode_rev"] = rev_str;
	GDScriptDecompVersion decomp_version;
	String custom_prefix = "-custom." + itos(GDScriptDecompVersion::number_of_custom_versions);
	decomp_version.commit = revision;
	decomp_version.min_version = p_custom_def.get("engine_version", "").operator String().split("-")[0] + custom_prefix;
	decomp_version.bytecode_version = p_custom_def.get("bytecode_version", 0);
	String parent_str = p_custom_def.get("parent", "");
	decomp_version.parent = parent_str.hex_to_int();
	decomp_version.is_dev = true;
	decomp_version.max_version = p_custom_def.get("max_engine_version", "");
	if (!decomp_version.max_version.is_empty()) {
		decomp_version.max_version = decomp_version.max_version.split("-")[0] + custom_prefix;
	}
	int engine_ver_major = p_custom_def.get("engine_ver_major", 0);
	if (engine_ver_major <= 0) {
		engine_ver_major = decomp_version.min_version.get_slice(".", 0).to_int();
		p_custom_def.set("engine_ver_major", engine_ver_major);
	}
	int variant_ver_major = p_custom_def.get("variant_ver_major", engine_ver_major);

	if (decomp_version.min_version.is_empty()) {
		ERR_FAIL_V_MSG(GDScriptDecompVersion(), "engine_version is required");
	}
	if (decomp_version.bytecode_version == 0) {
		ERR_FAIL_V_MSG(GDScriptDecompVersion(), "bytecode_version is required");
	}
	if (engine_ver_major <= 0) {
		ERR_FAIL_V_MSG(GDScriptDecompVersion(), "engine_ver_major is required");
	}
	if (variant_ver_major <= 0) {
		ERR_FAIL_V_MSG(GDScriptDecompVersion(), "variant_ver_major is required");
	}
	static constexpr const char *nameforamt = "%s (%s / UNKNOWN / Bytecode version: %d) - User defined bytecode based on %s";
	decomp_version.name = vformat(nameforamt, decomp_version.min_version, rev_str, decomp_version.bytecode_version, parent_str);
	decomp_version.custom = p_custom_def;
	Ref<GDScriptDecomp> decomp = decomp_version.create_decomp();
	if (decomp.is_null()) {
		ERR_FAIL_V_MSG(GDScriptDecompVersion(), "Invalid custom definition");
	}
	return decomp_version;
}

GDScriptDecompVersion GDScriptDecompVersion::create_derived_version_from_custom_def(int revision, Dictionary p_custom_def){
	Ref<GDScriptDecomp> decomp = create_decomp_for_commit(revision);
	ERR_FAIL_COND_V(decomp.is_null(), GDScriptDecompVersion());
	Dictionary ref_def = decomp->to_json();
	ERR_FAIL_COND_V(ref_def.is_empty(), GDScriptDecompVersion());
	int parent = decomp->get_bytecode_rev();
	for (auto &E : p_custom_def) {
		ref_def.set(E.key, E.value);
	}
	ref_def["parent"] = String::num_int64(parent, 16).to_lower();
	return create_version_from_custom_def(ref_def);
}


int GDScriptDecompVersion::register_decomp_version_custom(Dictionary p_custom_def) {
	auto version = create_version_from_custom_def(p_custom_def);
	if (version.name.is_empty()) {
		return 0;
	}
	GDScriptDecompVersion::decomp_versions.push_back(version);
	GDScriptDecompVersion::number_of_custom_versions++;
	return version.commit;
}

int GDScriptDecompVersion::register_derived_decomp_version_custom(int revision, Dictionary p_custom_def) {
	auto version = create_derived_version_from_custom_def(revision, p_custom_def);
	if (version.name.is_empty()) {
		return 0;
	}
	GDScriptDecompVersion::decomp_versions.push_back(version);
	GDScriptDecompVersion::number_of_custom_versions++;
	return version.commit;
}
