#include <core/object/object.h>
#include <core/object/script_instance.h>
#include <core/object/script_language.h>
#include <core/templates/rb_map.h>
#include <core/templates/rb_set.h>

#include "fake_script.h"

class FakeCSharpScript : public FakeScript {
	GDCLASS(FakeCSharpScript, FakeScript);
	bool tool = false;
	bool abstract = false;
	bool valid = false;
	bool loaded = false;
	bool autoload = true;
	bool reloading = false;
	bool is_binary = false;

	Ref<GDScriptNativeClass> native;
	Ref<Script> base;

	// Members are just indices to the instantiated script.
	// HashMap<StringName, MemberInfo> member_indices; // Includes member info of all base GDScript classes.
	HashMap<StringName, PropertyInfo> members; // Only members of the current class.
	HashMap<StringName, Variant> member_default_values;

	HashMap<StringName, MethodInfo> _signals;
	HashMap<StringName, MethodInfo> _methods;

	// Only static variables of the current class.
	// HashMap<StringName, MemberInfo> static_variables_indices;
	Vector<Variant> static_variables; // Static variable values.

	int override_bytecode_revision = 0;
	String script_path;
	bool path_valid = false; // False if using default path.
	Vector<String> base_classes;
	Vector<String> base_type_paths;
	StringName local_name; // Inner class identifier or `class_name`.
	StringName global_name; // `class_name`.
	Dictionary script_info;

	int ver_major = 0;
	HashMap<StringName, Pair<int, int>> subclasses;
	Vector<StringName> export_vars;

	String icon_path;
	bool globally_available = false;

protected:
	static void _bind_methods();

	Ref<Script> load_base_script() const;

public:
	Error _reload_from_file();
	virtual void reload_from_file() override;

	virtual bool can_instantiate() const override;

	Ref<Script> get_base_script() const override; //for script inheritance
	virtual StringName get_global_name() const override;
	virtual bool inherits_script(const Ref<Script> &p_script) const override;

	virtual StringName get_instance_base_type() const override;
	// this may not work in all scripts, will return empty if so
	virtual ScriptInstance *instance_create(Object *p_this) override;
	virtual PlaceHolderScriptInstance *placeholder_instance_create(Object *p_this) override;
	virtual bool instance_has(const Object *p_this) const override;

	// virtual bool has_source_code() const override;
	// virtual String get_source_code() const override;
	// virtual void set_source_code(const String &p_code) override;
	virtual Error reload(bool p_keep_state = false) override;

#ifdef TOOLS_ENABLED
	virtual StringName get_doc_class_name() const override;
	virtual Vector<DocData::ClassDoc> get_documentation() const override;
	virtual String get_class_icon_path() const override;
	virtual PropertyInfo get_class_category() const override;
#endif // TOOLS_ENABLED

	// TODO: In the next compat breakage rename to `*_script_*` to disambiguate from `Object::has_method()`.
	virtual bool has_method(const StringName &p_method) const override;
	virtual bool has_static_method(const StringName &p_method) const override;

	virtual int get_script_method_argument_count(const StringName &p_method, bool *r_is_valid = nullptr) const override;

	virtual Variant callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) override;

	virtual MethodInfo get_method_info(const StringName &p_method) const override;

	virtual bool is_tool() const override;
	virtual bool is_valid() const override;
	virtual bool is_abstract() const override;

	virtual ScriptLanguage *get_language() const override;

	virtual bool has_script_signal(const StringName &p_signal) const override;
	virtual void get_script_signal_list(List<MethodInfo> *r_signals) const override;

	virtual bool get_property_default_value(const StringName &p_property, Variant &r_value) const override;

	virtual void update_exports() override;
	//editor tool
	virtual void get_script_method_list(List<MethodInfo> *p_list) const override;
	virtual void get_script_property_list(List<PropertyInfo> *p_list) const override;

	virtual int get_member_line(const StringName &p_member) const override;

	virtual void get_constants(HashMap<StringName, Variant> *p_constants) override;
	virtual void get_members(HashSet<StringName> *p_members) override;

	virtual bool is_placeholder_fallback_enabled() const override;

	virtual const Variant get_rpc_config() const override;

	virtual String get_script_path() const override;
	virtual Error load_source_code(const String &p_path) override;
	virtual bool is_global_class() const override;
	virtual String get_icon_path() const override;
	virtual bool is_loaded() const override;

	void set_autoload(bool p_autoload);
	bool is_autoload() const;

	FakeCSharpScript();
};
