//_PRELUDE_
// clang-format off
#pragma once

#include "bytecode_base.h"

class __CLASS_NAME__ : public GDScriptDecomp {
	GDCLASS(__CLASS_NAME__, GDScriptDecomp);
protected:
	static void _bind_methods(){};
	static constexpr int bytecode_version = /*<BYTECODE_VERSION>*/;
	static constexpr int bytecode_rev = /*<BYTECODE_REV>*/;
	static constexpr int engine_ver_major = /*<ENGINE_VER_MAJOR>*/;
	static constexpr int variant_ver_major = /*<VARIANT_VER_MAJOR>*/;
	static constexpr const char *bytecode_rev_str = /*<BYTECODE_REV_STR>*/;
	static constexpr const char *engine_version = /*<ENGINE_VERSION>*/;
	static constexpr const char *max_engine_version = /*<MAX_ENGINE_VERSION>*/;
	static constexpr const char *date = /*<DATE>*/;
	static constexpr int parent = /*<PARENT_COMMIT>*/;

/*<ADDED_TOKENS>*/
/*<REMOVED_TOKENS>*/
/*<ADDED_FUNCTIONS>*/
/*<REMOVED_FUNCTIONS>*/
/*<ARG_COUNT_CHANGED>*/
/*<TOKENS_RENAMED>*/
/*<RENAMED_FUNCTIONS>*/
public:
	virtual String get_function_name(int p_func) const override;
	virtual int get_function_count() const override;
	virtual Pair<int, int> get_function_arg_count(int p_func) const override;
	virtual int get_token_max() const override;
	virtual int get_function_index(const String &p_func) const override;
	virtual GDScriptDecomp::GlobalToken get_global_token(int p_token) const override;
	virtual int get_local_token_val(GDScriptDecomp::GlobalToken p_token) const override;
	virtual int get_bytecode_version() const override { return bytecode_version; }
	virtual int get_bytecode_rev() const override { return bytecode_rev; }
	virtual int get_engine_ver_major() const override { return engine_ver_major; }
	virtual int get_variant_ver_major() const override { return variant_ver_major; }
	virtual int get_parent() const override { return parent; }
	virtual String get_engine_version() const override { return engine_version; }
	virtual String get_max_engine_version() const override { return max_engine_version; }
	virtual String get_date() const override { return date; }
	__CLASS_NAME__() {}
};

