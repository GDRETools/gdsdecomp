#include "godot_mono_decomp_wrapper.h"

#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/templates/vector.h"

#include "utility/gd_parallel_queue.h"
#include "utility/gdre_settings.h"
#include "utility/task_manager.h"

#if !GODOT_MONO_DECOMP_DISABLED
#include "godot_mono_decomp.h"

Ref<GodotMonoDecompWrapper> GodotMonoDecompWrapper::create(const String &assembly_path, const Vector<String> &originalProjectFiles, const GodotMonoDecompSettings &settings) {
	Ref<GodotMonoDecompWrapper> wrapper = memnew(GodotMonoDecompWrapper);
	Error err = wrapper->_load(assembly_path, originalProjectFiles, settings);
	ERR_FAIL_COND_V_MSG(err != OK, Ref<GodotMonoDecompWrapper>(), "Failed to load assembly " + assembly_path + " (Not a valid .NET assembly?)");
	return wrapper;
}

Error GodotMonoDecompWrapper::_load(const String &p_assembly_path, const Vector<String> &p_original_project_files, const GodotMonoDecompSettings &p_settings) {
	CharString assembly_path_chrstr = p_assembly_path.utf8();
	const char *assembly_path_c = assembly_path_chrstr.get_data();
	Vector<String> assembly_search_paths = p_settings.AdditionalAssemblySearchPaths;
	const char **assembly_search_paths_c_array = new const char *[assembly_search_paths.size()];
	Vector<CharString> assembly_search_paths_chrstrs;
	assembly_search_paths_chrstrs.resize(assembly_search_paths.size());
	for (int i = 0; i < assembly_search_paths.size(); i++) {
		// to keep them from being freed
		assembly_search_paths_chrstrs.write[i] = assembly_search_paths[i].utf8();
		assembly_search_paths_c_array[i] = assembly_search_paths_chrstrs[i].get_data();
	}

	CharString godotVersionOverride_chrstr = p_settings.GodotVersionOverride.is_empty() ? "" : p_settings.GodotVersionOverride.utf8();
	const char *godotVersionOverride_c = p_settings.GodotVersionOverride.is_empty() ? nullptr : godotVersionOverride_chrstr.get_data();

	const char **originalProjectFiles_c_array = new const char *[p_original_project_files.size()];
	Vector<CharString> originalProjectFiles_chrstrs;
	originalProjectFiles_chrstrs.resize(p_original_project_files.size());
	for (int i = 0; i < p_original_project_files.size(); i++) {
		// to keep them from being freed
		originalProjectFiles_chrstrs.write[i] = p_original_project_files[i].utf8();
		originalProjectFiles_c_array[i] = originalProjectFiles_chrstrs[i].get_data();
	}

	auto new_decompiler_handle = GodotMonoDecomp_CreateGodotModuleDecompiler(
			assembly_path_c,
			originalProjectFiles_c_array,
			p_original_project_files.size(),
			assembly_search_paths_c_array,
			assembly_search_paths.size(),
			godotVersionOverride_c,
			p_settings.WriteNuGetPackageReferences,
			p_settings.VerifyNuGetPackageIsFromNugetOrg,
			p_settings.CopyOutOfTreeReferences,
			p_settings.CreateAdditionalProjectsForProjectReferences,
			p_settings.RemoveGeneratedJsonContextBody,
			p_settings.EnableCollectionInitializerLifting,
			p_settings.EmitILAnnotationComments,
			p_settings.AutomaticallySearchWorkshopDependencies,
			(LanguageVersion)p_settings.OverrideLanguageVersion);
	delete[] originalProjectFiles_c_array;
	delete[] assembly_search_paths_c_array;
	if (new_decompiler_handle == nullptr) {
		return ERR_CANT_CREATE;
	}
	this->decompilerHandle = new_decompiler_handle;
	this->assembly_path = p_assembly_path;
	this->originalProjectFiles = p_original_project_files;
	this->settings = p_settings;
	return OK;
}

struct DecompileModuleTaskData : public TaskRunnerStruct {
	String outputCSProjectPath;
	Vector<String> excludeFiles;
	Error err = OK;
	std::atomic<int> current_step = 0;
	int total_steps = 0;
	ParalellQueue<String> queue;
	String current_step_description = "Decompiling module...";
	bool cancelled = false;

	DecompileModuleTaskData(const String &p_outputCSProjectPath, const Vector<String> &p_excludeFiles, int p_total_steps) :
			outputCSProjectPath(p_outputCSProjectPath), excludeFiles(p_excludeFiles), total_steps(p_total_steps), queue(p_total_steps) {}

	int get_current_task_step_value() override {
		return current_step;
	}

	String get_current_task_step_description() override {
		// pop them all off until we get the current one
		while (queue.try_pop(current_step_description)) {
		}
		return current_step_description;
	}

	void cancel() override {
		cancelled = true;
	}

	int progress_callback(int p_current, int p_total, const char *p_description) {
		if (cancelled) {
			return 1;
		}
		current_step++;
		// compare and exchange if p_current is greater than current_step
		// current_step = p_current;
		// total_steps = p_total;
		String description = String::utf8(p_description);
		queue.push(description);
		return 0;
	}

	static int _progress_callback(void *p_userdata, int p_current, int p_total, const char *p_description) {
		DecompileModuleTaskData *taskData = (DecompileModuleTaskData *)p_userdata;
		return taskData->progress_callback(p_current, p_total, p_description);
	}

	void run(void *p_userdata) override {
		GodotMonoDecompWrapper *wrapper = (GodotMonoDecompWrapper *)p_userdata;
		if (wrapper->decompilerHandle == nullptr) {
			ERR_PRINT("Decompiler handle is null");
			err = ERR_CANT_CREATE;
			return;
		}
		CharString outputCSProjectPath_chrstr = outputCSProjectPath.utf8();
		const char *outputCSProjectPath_c = outputCSProjectPath_chrstr.get_data();
		Vector<CharString> excludeFiles_chrstrrs;
		excludeFiles_chrstrrs.resize(excludeFiles.size());
		const char **excludeFiles_c_array = new const char *[excludeFiles.size()];
		for (int i = 0; i < excludeFiles.size(); i++) {
			excludeFiles_chrstrrs.write[i] = excludeFiles[i].utf8();
			excludeFiles_c_array[i] = excludeFiles_chrstrrs[i].get_data();
		}

		int result = GodotMonoDecomp_DecompileModuleWithProgress(wrapper->decompilerHandle, outputCSProjectPath_c, excludeFiles_c_array, excludeFiles.size(), &DecompileModuleTaskData::_progress_callback, this);
		delete[] excludeFiles_c_array;
		if (result != 0 && !cancelled) {
			err = ERR_CANT_CREATE;
		}
	}

	virtual ~DecompileModuleTaskData() = default;
};

Error GodotMonoDecompWrapper::decompile_module(const String &outputCSProjectPath, const Vector<String> &excludeFiles) {
	int total_steps = GodotMonoDecomp_GetNumberOfFilesInFileMap(decompilerHandle);
	auto taskData = std::make_shared<DecompileModuleTaskData>(outputCSProjectPath, excludeFiles, total_steps);
	TaskManager::get_singleton()->run_task(taskData, this, "Decompiling C# scripts...", total_steps, true, true);
	return taskData->err;
}

String GodotMonoDecompWrapper::decompile_individual_file(const String &file) {
	ERR_FAIL_COND_V_MSG(decompilerHandle == nullptr, "", "Decompiler handle is null");
	CharString file_chrstr = file.utf8();
	const char *file_c = file_chrstr.get_data();
	const char *result = GodotMonoDecomp_DecompileIndividualFile(decompilerHandle, file_c);
	ERR_FAIL_COND_V_MSG(result == nullptr, "", "Failed to decompile individual file");
	String result_str = String::utf8(result);
	GodotMonoDecomp_FreeString((void *)result);
	return result_str;
}

Dictionary GodotMonoDecompWrapper::get_script_info(const String &file) {
	ERR_FAIL_COND_V_MSG(decompilerHandle == nullptr, Dictionary(), "Decompiler handle is null");
	CharString file_chrstr = file.utf8();
	const char *file_c = file_chrstr.get_data();
	const char *result = GodotMonoDecomp_GetScriptInfo(decompilerHandle, file_c);
	if (result == nullptr) {
		return Dictionary();
	}
	String result_str = String::utf8(result);
	GodotMonoDecomp_FreeString((void *)result);
	Dictionary dict = JSON::parse_string(result_str);
	return dict;
}

Vector<String> GodotMonoDecompWrapper::get_files_not_present_in_file_map() {
	ERR_FAIL_COND_V_MSG(decompilerHandle == nullptr, Vector<String>(), "Decompiler handle is null");
	int num = GodotMonoDecomp_GetNumberOfFilesNotPresentInFileMap(decompilerHandle);
	if (num == 0) {
		return Vector<String>();
	}
	ERR_FAIL_COND_V_MSG(num < 0, Vector<String>(), "Failed to get number of files not present in file map");
	const char **files_not_present_in_file_map_c_array = GodotMonoDecomp_GetFilesNotPresentInFileMap(decompilerHandle);
	Vector<String> files_not_present_in_file_map_strs;
	for (int i = 0; i < num; i++) {
		files_not_present_in_file_map_strs.push_back("res://" + String::utf8(files_not_present_in_file_map_c_array[i]).trim_prefix("res://"));
	}
	GodotMonoDecomp_FreeArray((void *)files_not_present_in_file_map_c_array, num);
	return files_not_present_in_file_map_strs;
}

Vector<String> GodotMonoDecompWrapper::get_files_in_file_map() {
	ERR_FAIL_COND_V_MSG(decompilerHandle == nullptr, Vector<String>(), "Decompiler handle is null");
	int num = GodotMonoDecomp_GetNumberOfFilesInFileMap(decompilerHandle);
	if (num == 0) {
		return Vector<String>();
	}
	ERR_FAIL_COND_V_MSG(num < 0, Vector<String>(), "Failed to get number of files in file map");
	const char **files_in_file_map_c_array = GodotMonoDecomp_GetFilesInFileMap(decompilerHandle);
	Vector<String> files_in_file_map_strs;
	for (int i = 0; i < num; i++) {
		files_in_file_map_strs.push_back("res://" + String::utf8(files_in_file_map_c_array[i]).trim_prefix("res://"));
	}
	GodotMonoDecomp_FreeArray((void *)files_in_file_map_c_array, num);
	return files_in_file_map_strs;
}

bool GodotMonoDecompWrapper::is_custom_version_detected() const {
	ERR_FAIL_COND_V_MSG(decompilerHandle == nullptr, false, "Decompiler handle is null");
	return GodotMonoDecomp_IsCustomVersionDetected(decompilerHandle) != 0;
}

Vector<String> GodotMonoDecompWrapper::get_all_strings_in_module() {
	ERR_FAIL_COND_V_MSG(decompilerHandle == nullptr, Vector<String>(), "Decompiler handle is null");
	int num_strings = 0;
	char32_t **strings = GodotMonoDecomp_GetAllUtf32StringsInModule(decompilerHandle, &num_strings);
	Vector<String> strings_strs;
	for (int i = 0; i < num_strings; i++) {
		strings_strs.push_back(String(strings[i]));
	}
	GodotMonoDecomp_FreeArray((void *)strings, num_strings);
	return strings_strs;
}

Dictionary GodotMonoDecompWrapper::get_language_versions() {
	int num_versions = 0;
	int *versions = GodotMonoDecomp_GetLanguageVersions(&num_versions);
	Dictionary ret = { { 0, "Auto-detect" } };
	for (int i = 0; i < num_versions; i++) {
		if (ret.has(versions[i])) {
			continue;
		}
		int ver = versions[i];
		if (ver < 100) {
			ret[ver] = "C# " + String::num_int64(ver) + ".0";
		} else if (ver == INT_MAX) {
			ret[ver] = "Latest";
		} else {
			int ver_major = ver / 100;
			int ver_minor = ver % 100;
			ret[ver] = "C# " + String::num_int64(ver_major) + "." + String::num_int64(ver_minor);
		}
	}
	return ret;
}

Error GodotMonoDecompWrapper::set_settings(const GodotMonoDecompSettings &p_settings) {
	if (p_settings != settings) {
		Error err = _load(assembly_path, originalProjectFiles, p_settings);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to reload assembly " + assembly_path + " (Not a valid .NET assembly?)");
	}
	settings = p_settings;
	return OK;
}

bool GodotMonoDecompWrapper::is_file_assembly(const String &file) {
	CharString file_chrstr = file.utf8();
	const char *file_c = file_chrstr.get_data();
	return GodotMonoDecomp_CheckFileIsAssembly(file_c) != 0;
}

Vector<String> GodotMonoDecompWrapper::get_global_class_files() {
	ERR_FAIL_COND_V_MSG(decompilerHandle == nullptr, Vector<String>(), "Decompiler handle is null");
	int num_files = 0;
	const char **files = GodotMonoDecomp_GetGlobalClassFiles(decompilerHandle, &num_files);
	Vector<String> files_strs;
	for (int i = 0; i < num_files; i++) {
		files_strs.push_back("res://" + String::utf8(files[i]).trim_prefix("res://"));
	}
	GodotMonoDecomp_FreeArray((void *)files, num_files);
	return files_strs;
}

GodotMonoDecompWrapper::~GodotMonoDecompWrapper() {
	if (decompilerHandle != nullptr) {
		GodotMonoDecomp_FreeObjectHandle(decompilerHandle);
	}
}
#else
constexpr const char *GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE = "GodotMonoDecompWrapper is not enabled in this build of GDRE Tools";
Ref<GodotMonoDecompWrapper> GodotMonoDecompWrapper::create(const String &assembly_path, const Vector<String> &originalProjectFiles, const GodotMonoDecompSettings &settings) {
	ERR_FAIL_V_MSG(Ref<GodotMonoDecompWrapper>(), GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
Error GodotMonoDecompWrapper::_load(const String &p_assembly_path, const Vector<String> &p_original_project_files, const GodotMonoDecompSettings &p_settings) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
Error GodotMonoDecompWrapper::decompile_module(const String &outputCSProjectPath, const Vector<String> &excludeFiles) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
String GodotMonoDecompWrapper::decompile_individual_file(const String &file) {
	ERR_FAIL_V_MSG({}, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
Dictionary GodotMonoDecompWrapper::get_script_info(const String &file) {
	ERR_FAIL_V_MSG({}, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
Vector<String> GodotMonoDecompWrapper::get_files_not_present_in_file_map() {
	ERR_FAIL_V_MSG({}, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
Vector<String> GodotMonoDecompWrapper::get_files_in_file_map() {
	ERR_FAIL_V_MSG({}, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
bool GodotMonoDecompWrapper::is_custom_version_detected() const {
	ERR_FAIL_V_MSG(false, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
Vector<String> GodotMonoDecompWrapper::get_all_strings_in_module() {
	ERR_FAIL_V_MSG({}, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
Dictionary GodotMonoDecompWrapper::get_language_versions() {
	ERR_FAIL_V_MSG({}, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
Error GodotMonoDecompWrapper::set_settings(const GodotMonoDecompSettings &p_settings) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
bool GodotMonoDecompWrapper::is_file_assembly(const String &file) const {
	ERR_FAIL_V_MSG(false, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
Vector<String> GodotMonoDecompWrapper::get_global_class_files() {
	ERR_FAIL_V_MSG({}, GODOT_MONO_DECOMP_DISABLED_ERROR_MESSAGE);
}
GodotMonoDecompWrapper::~GodotMonoDecompWrapper() {}
#endif

GodotMonoDecompWrapper::GodotMonoDecompSettings GodotMonoDecompWrapper::get_settings() const {
	return settings;
}

GodotMonoDecompWrapper::GodotMonoDecompSettings GodotMonoDecompWrapper::GodotMonoDecompSettings::get_default_settings() {
	auto settings = GodotMonoDecompSettings();
	settings.GodotVersionOverride = GDRESettings::get_singleton() ? GDRESettings::get_singleton()->get_version_string() : "";
	if (!GDREConfig::get_singleton()) {
		return settings;
	}
	settings.WriteNuGetPackageReferences = GDREConfig::get_singleton()->get_setting("CSharp/write_nuget_package_references", true);
	settings.VerifyNuGetPackageIsFromNugetOrg = GDREConfig::get_singleton()->get_setting("CSharp/verify_nuget_package_is_from_nuget_org", false);
	settings.CopyOutOfTreeReferences = GDREConfig::get_singleton()->get_setting("CSharp/copy_out_of_tree_references", true);
	settings.CreateAdditionalProjectsForProjectReferences = GDREConfig::get_singleton()->get_setting("CSharp/create_additional_projects_for_project_references", true);
	settings.RemoveGeneratedJsonContextBody = GDREConfig::get_singleton()->get_setting("CSharp/remove_generated_json_context_body", false);
	settings.EnableCollectionInitializerLifting = GDREConfig::get_singleton()->get_setting("CSharp/enable_collection_initializer_lifting", true);
	settings.EmitILAnnotationComments = GDREConfig::get_singleton()->get_setting("CSharp/emit_il_annotation_comments", false);
	settings.OverrideLanguageVersion = GDREConfig::get_singleton()->get_setting("CSharp/force_language_version", 0);
	settings.AutomaticallySearchWorkshopDependencies = GDREConfig::get_singleton()->get_setting("CSharp/automatically_search_workshop_dependencies", true);
	settings.AdditionalAssemblySearchPaths = GDREConfig::get_singleton()->get_setting("CSharp/additional_assembly_search_paths", Vector<String>());
	return settings;
}

bool GodotMonoDecompWrapper::GodotMonoDecompSettings::operator==(const GodotMonoDecompSettings &p_other) const {
	return WriteNuGetPackageReferences == p_other.WriteNuGetPackageReferences &&
			VerifyNuGetPackageIsFromNugetOrg == p_other.VerifyNuGetPackageIsFromNugetOrg &&
			CopyOutOfTreeReferences == p_other.CopyOutOfTreeReferences &&
			CreateAdditionalProjectsForProjectReferences == p_other.CreateAdditionalProjectsForProjectReferences &&
			RemoveGeneratedJsonContextBody == p_other.RemoveGeneratedJsonContextBody &&
			EnableCollectionInitializerLifting == p_other.EnableCollectionInitializerLifting &&
			EmitILAnnotationComments == p_other.EmitILAnnotationComments &&
			AutomaticallySearchWorkshopDependencies == p_other.AutomaticallySearchWorkshopDependencies &&
			OverrideLanguageVersion == p_other.OverrideLanguageVersion &&
			GodotVersionOverride == p_other.GodotVersionOverride &&
			AdditionalAssemblySearchPaths == p_other.AdditionalAssemblySearchPaths;
}

bool GodotMonoDecompWrapper::GodotMonoDecompSettings::operator!=(const GodotMonoDecompSettings &p_other) const {
	return !(*this == p_other);
}

GodotMonoDecompWrapper::GodotMonoDecompWrapper() {}

void GodotMonoDecompWrapper::_bind_methods() {
	ClassDB::bind_static_method("GodotMonoDecompWrapper", D_METHOD("is_godot_mono_decomp_enabled"), &GodotMonoDecompWrapper::is_godot_mono_decomp_enabled);
	ClassDB::bind_method(D_METHOD("decompile_module", "outputCSProjectPath", "excludeFiles"), &GodotMonoDecompWrapper::decompile_module, DEFVAL(Vector<String>()));
	ClassDB::bind_method(D_METHOD("decompile_individual_file", "file"), &GodotMonoDecompWrapper::decompile_individual_file);
	ClassDB::bind_method(D_METHOD("get_script_info", "file"), &GodotMonoDecompWrapper::get_script_info);
	ClassDB::bind_method(D_METHOD("get_files_not_present_in_file_map"), &GodotMonoDecompWrapper::get_files_not_present_in_file_map);
	ClassDB::bind_method(D_METHOD("get_files_in_file_map"), &GodotMonoDecompWrapper::get_files_in_file_map);
	ClassDB::bind_method(D_METHOD("is_custom_version_detected"), &GodotMonoDecompWrapper::is_custom_version_detected);
	ClassDB::bind_method(D_METHOD("get_global_class_files"), &GodotMonoDecompWrapper::get_global_class_files);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("is_file_assembly", "file"), &GodotMonoDecompWrapper::is_file_assembly);
}
