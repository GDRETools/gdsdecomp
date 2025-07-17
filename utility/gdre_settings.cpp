#include "gdre_settings.h"

#include "bytecode/bytecode_base.h"
#include "bytecode/bytecode_tester.h"
#include "compat/resource_compat_binary.h"
#include "compat/resource_loader_compat.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"
#include "modules/zip/zip_reader.h"
#include "utility/common.h"
#include "utility/file_access_gdre.h"
#include "utility/gdre_logger.h"
#include "utility/gdre_packed_source.h"
#include "utility/gdre_version.gen.h"
#include "utility/import_info.h"
#include "utility/plugin_manager.h"
#include "utility/task_manager.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/object/script_language.h"
#include "modules/regex/regex.h"
#include "servers/rendering_server.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#endif

#include <sys/types.h>

#if defined(WINDOWS_ENABLED)
#include <windows.h>
#elif defined(UNIX_ENABLED)
#include <limits.h>
#include <unistd.h>
#endif
#include <stdlib.h>

String GDRESettings::_get_cwd() {
#if defined(WINDOWS_ENABLED)
	const DWORD expected_size = ::GetCurrentDirectoryW(0, nullptr);

	Char16String buffer;
	buffer.resize_uninitialized((int)expected_size);
	if (::GetCurrentDirectoryW(expected_size, (wchar_t *)buffer.ptrw()) == 0)
		return ".";

	String result;
	if (result.append_utf16(buffer.ptr())) {
		return ".";
	}
	return result.simplify_path();
#elif defined(UNIX_ENABLED)
	char buffer[PATH_MAX];
	if (::getcwd(buffer, sizeof(buffer)) == nullptr) {
		return ".";
	}

	String result;
	if (result.append_utf8(buffer)) {
		return ".";
	}

	return result.simplify_path();
#else
	return ".";
#endif
}

GDRESettings *GDRESettings::singleton = nullptr;
String get_java_path() {
	if (!OS::get_singleton()->has_environment("JAVA_HOME")) {
		return "";
	}
	String exe_ext = "";
	if (OS::get_singleton()->get_name() == "Windows") {
		exe_ext = ".exe";
	}
	return OS::get_singleton()->get_environment("JAVA_HOME").simplify_path().path_join("bin").path_join("java") + exe_ext;
}

int get_java_version() {
	List<String> args;
	// when using "-version", java will ALWAYS output on stderr in the format:
	// <java/openjdk/etc> version "x.x.x" <optional_builddate>
	args.push_back("-version");
	String output;
	int retval = 0;
	String java_path = get_java_path();
	if (java_path.is_empty()) {
		return -1;
	}
	Error err = OS::get_singleton()->execute(java_path, args, &output, &retval, true);
	if (err || retval) {
		return -1;
	}
	Vector<String> components = output.split("\n")[0].split(" ");
	if (components.size() < 3) {
		return 0;
	}
	String version_string = components[2].replace("\"", "");
	components = version_string.split(".", false);
	if (components.size() < 3) {
		return 0;
	}
	int version_major = components[0].to_int();
	int version_minor = components[1].to_int();
	// "1.8", and the like
	if (version_major == 1) {
		return version_minor;
	}
	return version_major;
}
bool GDRESettings::check_if_dir_is_v4() {
	// these are files that will only show up in version 4
	static const Vector<String> wildcards = { "*.ctex" };
	if (get_file_list(wildcards).size() > 0) {
		return true;
	} else {
		return false;
	}
}

bool GDRESettings::check_if_dir_is_v3() {
	// these are files that will only show up in version 3
	static const Vector<String> wildcards = { "*.stex" };
	if (get_file_list(wildcards).size() > 0) {
		return true;
	} else {
		return false;
	}
}

bool GDRESettings::check_if_dir_is_v2() {
	// these are files that will NOT show up in version 2
	static const Vector<String> wildcards = { "*.import", "*.remap" };
	if (get_file_list(wildcards).size() == 0) {
		return true;
	} else {
		return false;
	}
}

int GDRESettings::get_ver_major_from_dir() {
	if (check_if_dir_is_v2() && (FileAccess::exists("res://engine.cfb") || FileAccess::exists("res://engine.cfg"))) {
		return 2;
	}
	if (check_if_dir_is_v4())
		return 4;
	if (check_if_dir_is_v3())
		return 3;
	bool not_v2 = !check_if_dir_is_v2() || FileAccess::exists("res://project.binary") || FileAccess::exists("res://project.godot");

	// deeper checking; we know it's not v2, so we don't need to check that.
	HashSet<String> v2exts;
	HashSet<String> v3exts;
	HashSet<String> v4exts;
	HashSet<String> v2onlyexts;
	HashSet<String> v3onlyexts;
	HashSet<String> v4onlyexts;

	auto get_exts_func([&](HashSet<String> &ext, HashSet<String> &ext2, int ver_major) {
		List<String> exts;
		ResourceCompatLoader::get_base_extensions(&exts, ver_major);
		for (const String &extf : exts) {
			ext.insert(extf);
			ext2.insert(extf);
		}
	});
	get_exts_func(v2onlyexts, v2exts, 2);
	get_exts_func(v3onlyexts, v3exts, 3);
	get_exts_func(v4onlyexts, v4exts, 4);
	auto check_func([&](HashSet<String> &exts) {
		Vector<String> wildcards;
		for (auto &ext : exts) {
			wildcards.push_back("*." + ext);
		}
		auto list = get_file_list(wildcards);
		if (list.size() > 0) {
			return true;
		}
		return false;
	});

	for (const String &ext : v2exts) {
		if (v4exts.has(ext) || v3exts.has(ext)) {
			v4onlyexts.erase(ext);
			v3onlyexts.erase(ext);
			v2onlyexts.erase(ext);
		}
	}

	for (const String &ext : v3exts) {
		if (v4exts.has(ext)) {
			v4onlyexts.erase(ext);
			v3onlyexts.erase(ext);
		}
	}
	if (check_func(v4onlyexts)) {
		return 4;
	}
	if (check_func(v3onlyexts)) {
		return 3;
	}
	if (!not_v2) {
		if (check_func(v2onlyexts)) {
			return 2;
		}
	}
	return 0;
}

// We have to set this in the singleton here, since after Godot is done initializing,
// it will change the CWD to the executable dir
String GDRESettings::exec_dir = GDRESettings::_get_cwd();
GDRESettings *GDRESettings::get_singleton() {
	// TODO: get rid of this hack (again), the in-editor menu requires this.
	// if (!singleton) {
	// 	memnew(GDRESettings);
	// }
	return singleton;
}
// This adds compatibility classes for old objects that we know can be loaded on v4 just by changing the name
void addCompatibilityClasses() {
	ClassDB::add_compatibility_class("PHashTranslation", "OptimizedTranslation");
}

GDRESettings::GDRESettings() {
#ifdef TOOLS_ENABLED
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->set_warn_on_surface_upgrade(false);
	}
#endif
	singleton = this;
	gdre_packeddata_singleton = memnew(GDREPackedData);
	addCompatibilityClasses();
	gdre_user_path = ProjectSettings::get_singleton()->globalize_path("user://");
	gdre_resource_path = ProjectSettings::get_singleton()->get_resource_path();
	logger = memnew(GDRELogger);
	headless = !RenderingServer::get_singleton() || RenderingServer::get_singleton()->get_video_adapter_name().is_empty();
	add_logger();
	PluginManager::load_cache();
}

GDRESettings::~GDRESettings() {
	PluginManager::save_cache();
	remove_current_pack();
	memdelete(gdre_packeddata_singleton);
	singleton = nullptr;
	logger->_disable();
	// logger doesn't get memdeleted because the OS singleton will do so
}
String GDRESettings::get_cwd() {
	return GDRESettings::_get_cwd();
}

void GDRESettings::set_exec_dir(const String &p_cwd) {
	GDRESettings::exec_dir = p_cwd;
}

String GDRESettings::get_exec_dir() {
	return GDRESettings::exec_dir;
}

bool GDRESettings::are_imports_loaded() const {
	return import_files.size() > 0;
}

String GDRESettings::get_gdre_resource_path() const {
	return gdre_resource_path;
}

String GDRESettings::get_gdre_tmp_path() const {
	return gdre_user_path.path_join(".tmp");
}

String GDRESettings::get_gdre_user_path() const {
	return gdre_user_path;
}

bool GDRESettings::is_pack_loaded() const {
	return current_project.is_valid();
}

bool GDRESettings::has_valid_version() const {
	return is_pack_loaded() && current_project->version.is_valid() && current_project->version->is_valid_semver();
}

GDRESettings::PackInfo::PackType GDRESettings::get_pack_type() const {
	return is_pack_loaded() ? current_project->type : PackInfo::UNKNOWN;
}
String GDRESettings::get_pack_path() const {
	return is_pack_loaded() ? current_project->pack_file : "";
}
String GDRESettings::get_version_string() const {
	return has_valid_version() ? current_project->version->as_text() : String();
}
uint32_t GDRESettings::get_ver_major() const {
	return has_valid_version() ? current_project->version->get_major() : 0;
}
uint32_t GDRESettings::get_ver_minor() const {
	return has_valid_version() ? current_project->version->get_minor() : 0;
}
uint32_t GDRESettings::get_ver_rev() const {
	return has_valid_version() ? current_project->version->get_patch() : 0;
}
uint32_t GDRESettings::get_file_count() const {
	if (!is_pack_loaded()) {
		return 0;
	}
	int count = 0;
	for (const auto &pack : packs) {
		count += pack->file_count;
	}
	return count;
}

void GDRESettings::set_ver_rev(uint32_t p_rev) {
	if (is_pack_loaded()) {
		if (!has_valid_version()) {
			current_project->version = Ref<GodotVer>(memnew(GodotVer(0, 0, p_rev)));
		} else {
			current_project->version->set_patch(p_rev);
			if (current_project->version->get_build_metadata() == "x") {
				current_project->version->set_build_metadata("");
			}
		}
	}
}
void GDRESettings::set_project_path(const String &p_path) {
	project_path = p_path;
}
String GDRESettings::get_project_path() const {
	return project_path;
}
bool GDRESettings::is_project_config_loaded() const {
	if (!is_pack_loaded()) {
		return false;
	}
	bool is_loaded = current_project->pcfg->is_loaded();
	return is_loaded;
}

void GDRESettings::remove_current_pack() {
	current_project = Ref<PackInfo>();
	packs.clear();
	import_files.clear();
	remap_iinfo.clear();
	unload_encryption_key();
}

String get_standalone_pck_path() {
	String exec_path = OS::get_singleton()->get_executable_path();
	String exec_dir = exec_path.get_base_dir();
	String exec_filename = exec_path.get_file();
	String exec_basename = exec_filename.get_basename();

	return exec_dir.path_join(exec_basename + ".pck");
}

// This loads project directories by setting the global resource path to the project directory
// We have to be very careful about this, this means that any GDRE resources we have loaded
// could fail to reload if they somehow became unloaded while we were messing with the project.
Error GDRESettings::load_dir(const String &p_path) {
	if (is_pack_loaded()) {
		return ERR_ALREADY_IN_USE;
	}
	Ref<DirAccess> da = DirAccess::open(p_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(da.is_null(), ERR_FILE_CANT_OPEN, "FATAL ERROR: Can't find folder!");
	ERR_FAIL_COND_V_MSG(!da->dir_exists(p_path), ERR_FILE_CANT_OPEN, "FATAL ERROR: Can't find folder!");

	// This is a hack to get the resource path set to the project folder
	ProjectSettings *settings_singleton = ProjectSettings::get_singleton();
	GDREPackSettings *new_singleton = reinterpret_cast<GDREPackSettings *>(settings_singleton);
	GDREPackSettings::do_set_resource_path(new_singleton, p_path);
	project_path = p_path;
	Error err = GDREPackedData::get_singleton()->add_dir(p_path, false);
	if (err != OK) {
		unload_dir();
		ERR_FAIL_V_MSG(err, "FATAL ERROR: Can't open directory!");
	}
	return OK;
}

Error GDRESettings::unload_dir() {
	ProjectSettings *settings_singleton = ProjectSettings::get_singleton();
	GDREPackSettings *new_singleton = static_cast<GDREPackSettings *>(settings_singleton);
	GDREPackSettings::do_set_resource_path(new_singleton, gdre_resource_path);
	project_path = "";
	return OK;
}

bool is_macho(const String &p_path) {
	Ref<FileAccess> fa = FileAccess::open(p_path, FileAccess::READ);
	if (fa.is_null()) {
		return false;
	}
	uint8_t header[4];
	fa->get_buffer(header, 4);
	fa->close();
	if ((header[0] == 0xcf || header[0] == 0xce) && header[1] == 0xfa && header[2] == 0xed && header[3] == 0xfe) {
		return true;
	}

	// handle fat binaries
	// always stored in big-endian format
	if (header[0] == 0xca && header[1] == 0xfe && header[2] == 0xba && header[3] == 0xbe) {
		return true;
	}
	// handle big-endian mach-o binaries
	if (header[0] == 0xfe && header[1] == 0xed && header[2] == 0xfa && (header[3] == 0xce || header[3] == 0xcf)) {
		return true;
	}

	return false;
}

Error check_embedded(String &p_path) {
	String extension = p_path.get_extension().to_lower();
	if (extension != "pck" && extension != "apk" && extension != "zip") {
		// check if it's a mach-o executable

		if (GDREPackedSource::is_embeddable_executable(p_path)) {
			if (!GDREPackedSource::has_embedded_pck(p_path)) {
				return ERR_FILE_UNRECOGNIZED;
			}
		} else if (is_macho(p_path)) {
			return ERR_FILE_UNRECOGNIZED;
		}
	}
	return OK;
}

Error GDRESettings::load_pck(const String &p_path) {
	// Check if the path is already loaded
	for (const auto &pack : packs) {
		if (pack->pack_file == p_path) {
			return ERR_ALREADY_IN_USE;
		}
	}
	Error err = GDREPackedData::get_singleton()->add_pack(p_path, true, 0);
	if (err) {
		ERR_FAIL_COND_V_MSG(error_encryption, ERR_PRINTER_ON_FIRE, "FATAL ERROR: Cannot open encrypted pck! (wrong key?)");
	}
	ERR_FAIL_COND_V_MSG(err, err, "FATAL ERROR: Can't open pack!");
	ERR_FAIL_COND_V_MSG(!is_pack_loaded(), ERR_FILE_CANT_READ, "FATAL ERROR: loaded project pack, but didn't load files from it!");
	return OK;
}

bool is_zip_file_pack(const String &p_path) {
	Ref<ZIPReader> zip = memnew(ZIPReader);
	Error err = zip->open(p_path);
	if (err) {
		return false;
	}
	auto files = zip->get_files();
	for (int i = 0; i < files.size(); i++) {
		if (files[i] == "engine.cfg" || files[i] == "engine.cfb" || files[i] == "project.godot" || files[i] == "project.binary") {
			return true;
		}
		if (files[i].begins_with(".godot/")) {
			return true;
		}
	}
	return false;
}
String GDRESettings::get_home_dir() {
#ifdef WINDOWS_ENABLED
	return OS::get_singleton()->get_environment("USERPROFILE");
#else
	return OS::get_singleton()->get_environment("HOME");
#endif
}
// For printing out paths, we want to replace the home directory with ~ to keep PII out of logs
String GDRESettings::sanitize_home_in_path(const String &p_path) {
#ifdef WINDOWS_ENABLED
	String home_dir = OS::get_singleton()->get_environment("USERPROFILE");
#else
	String home_dir = OS::get_singleton()->get_environment("HOME");
#endif
	if (p_path.begins_with(home_dir)) {
		return String("~").path_join(p_path.replace_first(home_dir, ""));
	}
	return p_path;
}

Error GDRESettings::load_project(const Vector<String> &p_paths, bool _cmd_line_extract) {
	GDRELogger::clear_error_queues();
	if (is_pack_loaded()) {
		return ERR_ALREADY_IN_USE;
	}

	if (p_paths.is_empty()) {
		ERR_FAIL_V_MSG(ERR_FILE_NOT_FOUND, "No valid paths provided!");
	}

	if (logger->get_path().is_empty()) {
		logger->start_prebuffering();
		log_sysinfo();
	}
	load_encryption_key();

	Error err = ERR_CANT_OPEN;
	Vector<String> pck_files = p_paths;
	// This may be a ".app" bundle, so we need to check if it's a valid Godot app
	// and if so, load the pck from inside the bundle
	if (pck_files[0].get_extension().to_lower() == "app" && DirAccess::exists(pck_files[0])) {
		if (pck_files.size() > 1) {
			ERR_FAIL_V_MSG(ERR_FILE_NOT_FOUND, "Cannot specify multiple directories!");
		}
		String resources_path = pck_files[0].path_join("Contents").path_join("Resources");
		if (DirAccess::exists(resources_path)) {
			auto list = gdre::get_recursive_dir_list(resources_path, { "*.pck" }, true);
			if (!list.is_empty()) {
				pck_files = list;
			} else {
				ERR_FAIL_V_MSG(ERR_FILE_NOT_FOUND, "Can't find pck file in .app bundle!");
			}
		}
	}

	if (DirAccess::exists(pck_files[0])) {
		if (pck_files.size() > 1) {
			ERR_FAIL_V_MSG(ERR_FILE_NOT_FOUND, "Cannot specify multiple directories!");
		}
		print_line("Opening file: " + sanitize_home_in_path(pck_files[0]));
		err = load_dir(pck_files[0]);
		ERR_FAIL_COND_V_MSG(err, err, "FATAL ERROR: Can't load project directory!");
		load_pack_uid_cache();
		load_pack_gdscript_cache();
	} else {
		for (auto path : pck_files) {
			auto san_path = sanitize_home_in_path(path);
			print_line("Opening file: " + san_path);
			if (check_embedded(path) != OK) {
				String new_path = path;
				String parent_path = path.get_base_dir();
				if (parent_path.is_empty()) {
					parent_path = GDRESettings::get_exec_dir();
				}
				if (parent_path.get_file().to_lower() == "macos") {
					// we want to get ../Resources
					parent_path = parent_path.get_base_dir().path_join("Resources");
					String pck_path = parent_path.path_join(path.get_file().get_basename() + ".pck");
					if (FileAccess::exists(pck_path)) {
						new_path = pck_path;
						err = OK;
					}
					if (pck_files.has(new_path)) {
						// we already tried this path
						WARN_PRINT("EXE does not have an embedded pck, not loading " + san_path);
						continue;
					}
				}
				if (err != OK) {
					String pck_path = path.get_basename() + ".pck";
					bool only_1_path = pck_files.size() == 1;
					bool already_has_path = pck_files.has(pck_path);
					bool exists = FileAccess::exists(pck_path);
					if (!only_1_path && (already_has_path || !exists)) {
						// we already tried this path
						WARN_PRINT("EXE does not have an embedded pck, not loading " + san_path);
						continue;
					}
					ERR_FAIL_COND_V_MSG(!exists, err, "Can't find embedded pck file in executable and cannot find pck file in same directory!");
					new_path = pck_path;
				}
				path = new_path;
				WARN_PRINT("Could not find embedded pck in EXE, found pck file, loading from: " + san_path);
			}
			err = load_pck(path);
			if (err) {
				unload_project();
				ERR_FAIL_COND_V_MSG(err, err, "Can't load project!");
			}
			load_pack_uid_cache();
			load_pack_gdscript_cache();
		}
	}

	ERR_FAIL_COND_V_MSG(!is_pack_loaded(), ERR_FILE_CANT_READ, "FATAL ERROR: loaded project pack, but didn't load files from it!");
	if (_cmd_line_extract) {
		// we don't want to load the imports and project config if we're just extracting.
		return OK;
	}

	// Load embedded zips within the pck
	auto zip_files = get_file_list({ "*.zip" });
	if (zip_files.size() > 0) {
		Vector<String> pck_zip_files;
		for (auto path : pck_files) {
			if (path.get_extension().to_lower() == "zip") {
				pck_zip_files.push_back(path.get_file().to_lower());
			}
		}
		for (auto zip_file : zip_files) {
			if (is_zip_file_pack(zip_file) && !pck_zip_files.has(zip_file.get_file().to_lower())) {
				err = load_pck(zip_file);
				if (err) {
					unload_project();
					ERR_FAIL_COND_V_MSG(err, err, "Can't load project!");
				}
				load_pack_uid_cache();
				load_pack_gdscript_cache();
			}
		}
	}

	bool invalid_ver = !has_valid_version() || current_project->suspect_version;

	if (invalid_ver) {
		// We need to get the version from the binary resources.
		err = get_version_from_bin_resources();
		// this is a catastrophic failure, unload the pack
		if (err) {
			unload_project();
			ERR_FAIL_V_MSG(err, "FATAL ERROR: Can't determine engine version of project pack!");
		}
	}

	err = detect_bytecode_revision(invalid_ver);
	if (err) {
		if (err == ERR_UNAUTHORIZED) {
			_set_error_encryption(true);
		}
		WARN_PRINT("Could not determine bytecode revision, not able to decompile scripts...");
	}

	if (!pack_has_project_config()) {
		WARN_PRINT("Could not find project configuration in directory, may be a seperate resource pack...");
	} else {
		err = load_project_config();
		ERR_FAIL_COND_V_MSG(err, err, "FATAL ERROR: Can't open project config!");
	}

	err = load_import_files();
	ERR_FAIL_COND_V_MSG(err, ERR_FILE_CANT_READ, "FATAL ERROR: Could not load imported binary files!");

	return OK;
}

constexpr bool GDRESettings::need_correct_patch(int ver_major, int ver_minor) {
	return ((ver_major == 2 || ver_major == 3) && ver_minor == 1);
}

Error GDRESettings::detect_bytecode_revision(bool p_no_valid_version) {
	if (!is_pack_loaded()) {
		return ERR_FILE_CANT_OPEN;
	}
	if (current_project->bytecode_revision != 0) {
		return OK;
	}
	int ver_major = -1;
	int ver_minor = -1;
	if (has_valid_version()) {
		ver_major = get_ver_major();
		ver_minor = get_ver_minor();
	}
	Vector<String> bytecode_files = get_file_list({ "*.gdc" });
	Vector<String> encrypted_files = get_file_list({ "*.gde" });

	auto guess_from_version = [&](Error fail_error = ERR_FILE_CANT_OPEN) {
		if (ver_major > 0 && ver_minor >= 0) {
			auto decomp = GDScriptDecomp::create_decomp_for_version(current_project->version->as_text(), true);
			ERR_FAIL_COND_V_MSG(decomp.is_null(), fail_error, "Cannot determine bytecode revision");
			print_line("Guessing bytecode revision from engine version: " + get_version_string() + " (rev 0x" + String::num_int64(decomp->get_bytecode_rev(), 16) + ")");
			current_project->bytecode_revision = decomp->get_bytecode_rev();
			return OK;
		}
		current_project->bytecode_revision = 0;
		ERR_FAIL_V_MSG(fail_error, "Cannot determine bytecode revision!");
	};
	if (!encrypted_files.is_empty()) {
		auto file = encrypted_files[0];
		// test this file to see if it decrypts properly
		Vector<uint8_t> buffer;
		Error err = GDScriptDecomp::get_buffer_encrypted(file, ver_major > 0 ? ver_major : 3, enc_key, buffer);
		ERR_FAIL_COND_V_MSG(err, ERR_UNAUTHORIZED, "Cannot determine bytecode revision: Encryption error (Did you set the correct key?)");
		bytecode_files.append_array(encrypted_files);
	}
	if (bytecode_files.is_empty()) {
		return guess_from_version(ERR_PARSE_ERROR);
	}
	auto revision = BytecodeTester::test_files(bytecode_files, ver_major, ver_minor, true);
	if (revision == 0) {
		ERR_FAIL_COND_V_MSG(need_correct_patch(ver_major, ver_minor), ERR_FILE_CANT_OPEN, "Cannot determine bytecode revision: Need the correct patch version for engine version " + itos(ver_major) + "." + itos(ver_minor) + ".x!");
		return guess_from_version(ERR_FILE_CANT_OPEN);
	}
	current_project->bytecode_revision = revision;
	auto decomp = GDScriptDecomp::create_decomp_for_commit(revision);
	ERR_FAIL_COND_V_MSG(decomp.is_null(), ERR_FILE_CANT_OPEN, "Cannot determine bytecode revision!");
	auto check_if_same_minor_major = [&](Ref<GodotVer> version, Ref<GodotVer> max_ver) {
		if (!(max_ver->get_major() == version->get_major() && max_ver->get_minor() == version->get_minor())) {
			return false;
		}
		return true;
	};
	if (!has_valid_version()) {
		current_project->version = decomp->get_godot_ver();
		current_project->version->set_build_metadata("");
	} else {
		auto version = decomp->get_godot_ver();
		if (version->is_prerelease()) {
			current_project->version = decomp->get_max_engine_version().is_empty() ? version : decomp->get_max_godot_ver();
		} else if (ver_major < 3 || (ver_major == 3 && ver_minor <= 1) || p_no_valid_version) { // didn't write correct patch version or did not have a correct pck version
			auto max_version = decomp->get_max_godot_ver();
			if (max_version.is_valid() && (check_if_same_minor_major(current_project->version, max_version))) {
				if (max_version->get_patch() > current_project->version->get_patch()) {
					current_project->version->set_patch(max_version->get_patch());
				}
			} else if (check_if_same_minor_major(current_project->version, version)) {
				if (version->get_patch() > current_project->version->get_patch()) {
					current_project->version->set_patch(version->get_patch());
				}
			}
		}
	}
	return OK;
}

int GDRESettings::get_bytecode_revision() const {
	return is_pack_loaded() ? current_project->bytecode_revision : 0;
}

Error GDRESettings::get_version_from_bin_resources() {
	int consistent_versions = 0;
	int inconsistent_versions = 0;
	int ver_major = 0;
	int ver_minor = 0;
	int min_major = INT_MAX;
	int max_major = 0;
	int min_minor = INT_MAX;
	int max_minor = 0;

	int i;
	int version_from_dir = get_ver_major_from_dir();

	// only test the bytecode on non-encrypted 3.x files
	Vector<String> bytecode_files = get_file_list({ "*.gdc" });
	Vector<Ref<GDScriptDecomp>> decomps;

	auto check_if_same_minor_major = [&](Ref<GodotVer> version, Ref<GodotVer> max_ver) {
		if (!(max_ver->get_major() == version->get_major() && max_ver->get_minor() == version->get_minor())) {
			return false;
		}
		return true;
	};
	auto set_min_max = [&](Ref<GodotVer> version) {
		min_minor = MIN(min_minor, version->get_minor());
		max_minor = MAX(max_minor, version->get_minor());
		min_major = MIN(min_major, version->get_major());
		max_major = MAX(max_major, version->get_major());
	};

	auto do_thing = [&]() {
		if (decomps.size() == 1) {
			auto version = decomps[0]->get_godot_ver();
			auto max_version = decomps[0]->get_max_godot_ver();
			if (version->get_major() != 4 && (max_version.is_null() || check_if_same_minor_major(version, max_version))) {
				current_project->version = max_version.is_valid() ? max_version : version;
				current_project->version->set_build_metadata("");
				return true;
			}
		};
		for (auto decomp : decomps) {
			auto version = decomp->get_godot_ver();
			auto max_version = decomp->get_max_godot_ver();
			set_min_max(version);
			if (max_version.is_valid()) {
				set_min_max(max_version);
			}
		}
		return false;
	};

	if (!bytecode_files.is_empty()) {
		decomps = BytecodeTester::get_possible_decomps(bytecode_files);
		if (decomps.is_empty()) {
			decomps = BytecodeTester::get_possible_decomps(bytecode_files, true);
		}
		ERR_FAIL_COND_V_MSG(decomps.is_empty(), ERR_FILE_NOT_FOUND, "Cannot determine version from bin resources: decomp testing failed!");
		if (do_thing()) {
			return OK;
		}
		if (min_major == max_major && min_minor == max_minor) {
			current_project->version = GodotVer::create(min_major, min_minor, 0);
			return OK;
		}
	} else {
		min_minor = 0;
		max_minor = INT_MAX;
		min_major = 0;
		max_major = INT_MAX;
	}

	List<String> exts;
	ResourceCompatLoader::get_base_extensions(&exts, version_from_dir);
	Vector<String> wildcards;
	for (const String &ext : exts) {
		wildcards.push_back("*." + ext);
	}
	Vector<String> files = get_file_list(wildcards);
	uint64_t max = files.size();
	bool sus_warning = false;
	for (i = 0; i < max; i++) {
		bool suspicious = false;
		uint32_t res_major = 0;
		uint32_t res_minor = 0;
		Error err = ResourceFormatLoaderCompatBinary::get_ver_major_minor(files[i], res_major, res_minor, suspicious);
		if (err) {
			continue;
		}
		if (!sus_warning && suspicious) {
			if (res_major == 3 && res_minor == 1) {
				WARN_PRINT("Warning: Found suspicious major/minor version, probably Sonic Colors Unlimited...");
				max = 1000;
			} else {
				WARN_PRINT("Warning: Found suspicious major/minor version...");
			}
			sus_warning = true;
		}
		if (consistent_versions == 0) {
			ver_major = res_major;
			ver_minor = res_minor;
		}
		if (ver_major == res_major && res_minor == ver_minor) {
			consistent_versions++;
		} else {
			if (ver_major != res_major) {
				WARN_PRINT_ONCE("WARNING!!!!! Inconsistent major versions in binary resources!");
				if (ver_major < res_major) {
					ver_major = res_major;
					ver_minor = res_minor;
				}
			} else if (ver_minor < res_minor) {
				ver_minor = res_minor;
			}
			inconsistent_versions++;
		}
	}
	if (inconsistent_versions > 0) {
		WARN_PRINT(itos(inconsistent_versions) + " binary resources had inconsistent versions!");
	}
	// we somehow didn't get a version major??
	if (ver_major == 0) {
		WARN_PRINT("Couldn't determine ver major from binary resources?!");
		ver_major = version_from_dir;
		ERR_FAIL_COND_V_MSG(ver_major == 0, ERR_CANT_ACQUIRE_RESOURCE, "Can't find version from directory!");
	}

	current_project->version = GodotVer::create(ver_major, ver_minor, 0);
	return OK;
}

Error GDRESettings::load_project_config() {
	Error err;
	ERR_FAIL_COND_V_MSG(!is_pack_loaded(), ERR_FILE_CANT_OPEN, "Pack not loaded!");
	ERR_FAIL_COND_V_MSG(is_project_config_loaded(), ERR_ALREADY_IN_USE, "Project config is already loaded!");
	ERR_FAIL_COND_V_MSG(!pack_has_project_config(), ERR_FILE_NOT_FOUND, "Could not find project config!");
	if (get_ver_major() == 2) {
		err = current_project->pcfg->load_cfb(has_path_loaded("res://engine.cfb") ? "res://engine.cfb" : "res://engine.cfg", get_ver_major(), get_ver_minor());
		ERR_FAIL_COND_V_MSG(err, err, "Failed to load project config!");
	} else if (get_ver_major() >= 3) {
		err = current_project->pcfg->load_cfb(has_path_loaded("res://project.binary") ? "res://project.binary" : "res://project.godot", get_ver_major(), get_ver_minor());
		ERR_FAIL_COND_V_MSG(err, err, "Failed to load project config!");
	} else {
		ERR_FAIL_V_MSG(ERR_FILE_UNRECOGNIZED,
				"Godot version not set or project uses unsupported Godot version");
	}
	return OK;
}

Error GDRESettings::save_project_config(const String &p_out_dir = "") {
	String output_dir = p_out_dir;
	if (output_dir.is_empty()) {
		output_dir = project_path;
	}
	return current_project->pcfg->save_cfb(output_dir, get_ver_major(), get_ver_minor());
}

Error GDRESettings::unload_project() {
	if (!is_pack_loaded()) {
		return ERR_DOES_NOT_EXIST;
	}
	logger->stop_prebuffering();
	error_encryption = false;
	reset_uid_cache();
	reset_gdscript_cache();
	if (get_pack_type() == PackInfo::DIR) {
		unload_dir();
	}

	remove_current_pack();
	GDREPackedData::get_singleton()->clear();
	return OK;
}

void GDRESettings::add_pack_info(Ref<PackInfo> packinfo) {
	ERR_FAIL_COND_MSG(!packinfo.is_valid(), "Invalid pack info!");
	packs.push_back(packinfo);
	if (!current_project.is_valid()) { // only set if we don't have a current pack
		current_project = Ref<ProjectInfo>(memnew(ProjectInfo));
		current_project->version = GodotVer::copy_from(packinfo->version);
		current_project->pack_file = packinfo->pack_file;
		current_project->type = packinfo->type;
		current_project->suspect_version = packinfo->suspect_version;
	} else {
		if (!current_project->version->eq(packinfo->version)) {
			if ((!current_project->version->is_valid_semver() || current_project->version->get_major() == 0) &&
					packinfo->version->is_valid_semver() && packinfo->version->get_major() != 0) {
				current_project->version = GodotVer::copy_from(packinfo->version);
			} else {
				WARN_PRINT("Warning: Pack version mismatch!");
			}
		}
	}
}

StringName GDRESettings::get_cached_script_class(const String &p_path) {
	if (!is_pack_loaded()) {
		return "";
	}
	String path = p_path;
	if (!script_cache.has(path)) {
		path = get_mapped_path(p_path);
	}
	if (script_cache.has(path)) {
		auto &dict = script_cache.get(path);
		if (dict.has("class")) {
			return dict["class"];
		}
	}
	return "";
}

StringName GDRESettings::get_cached_script_base(const String &p_path) {
	if (!is_pack_loaded()) {
		return "";
	}
	String path = p_path;
	if (!script_cache.has(path)) {
		path = get_mapped_path(p_path);
	}
	if (script_cache.has(p_path)) {
		auto &dict = script_cache[p_path];
		if (dict.has("base")) {
			return dict["base"];
		}
	}
	return "";
}
bool GDRESettings::had_encryption_error() const {
	return error_encryption;
}
// PackedSource doesn't pass back useful error information when loading packs,
// this is a hack so that we can tell if it was an encryption error.
void GDRESettings::_set_error_encryption(bool is_encryption_error) {
	error_encryption = is_encryption_error;
}

void GDRESettings::load_encryption_key() {
	if (enc_key.size() == 32) {
		memcpy(script_encryption_key, enc_key.ptr(), 32);
	} else {
		memset(script_encryption_key, 0, 32);
	}
}

void GDRESettings::unload_encryption_key() {
	memset(script_encryption_key, 0, 32);
}

Vector<uint8_t> GDRESettings::get_encryption_key() {
	return enc_key;
}

String GDRESettings::get_encryption_key_string() {
	if (enc_key.is_empty()) {
		return "";
	}
	return String::hex_encode_buffer(enc_key.ptr(), enc_key.size());
}

Error GDRESettings::set_encryption_key(Vector<uint8_t> key) {
	if (key.size() != 32) {
		return ERR_INVALID_PARAMETER;
	}
	enc_key = key;
	if (is_pack_loaded()) {
		load_encryption_key();
	}
	return OK;
}

Error GDRESettings::set_encryption_key_string(const String &key_str) {
	String skey = key_str.replace_first("0x", "");
	ERR_FAIL_COND_V_MSG(!skey.is_valid_hex_number(false) || skey.size() < 64, ERR_INVALID_PARAMETER, "not a valid key");

	Vector<uint8_t> key;
	key.resize(32);
	for (int i = 0; i < 32; i++) {
		int v = 0;
		if (i * 2 < skey.length()) {
			char32_t ct = skey.to_lower()[i * 2];
			if (ct >= '0' && ct <= '9')
				ct = ct - '0';
			else if (ct >= 'a' && ct <= 'f')
				ct = 10 + ct - 'a';
			v |= ct << 4;
		}

		if (i * 2 + 1 < skey.length()) {
			char32_t ct = skey.to_lower()[i * 2 + 1];
			if (ct >= '0' && ct <= '9')
				ct = ct - '0';
			else if (ct >= 'a' && ct <= 'f')
				ct = 10 + ct - 'a';
			v |= ct;
		}
		key.write[i] = v;
	}
	set_encryption_key(key);
	return OK;
}

void GDRESettings::reset_encryption_key() {
	enc_key.clear();
	if (is_pack_loaded()) {
		load_encryption_key();
	}
}

Vector<String> GDRESettings::get_file_list(const Vector<String> &filters) {
	if (!is_pack_loaded()) {
		return gdre::get_recursive_dir_list("res://", filters);
	}
	Vector<String> ret;
	Vector<Ref<PackedFileInfo>> flist = get_file_info_list(filters);
	for (int i = 0; i < flist.size(); i++) {
		ret.push_back(flist[i]->path);
	}
	return ret;
}

Array GDRESettings::get_file_info_array(const Vector<String> &filters) {
	Array ret;
	for (auto file_info : get_file_info_list(filters)) {
		ret.push_back(file_info);
	}
	return ret;
}

Vector<Ref<PackedFileInfo>> GDRESettings::get_file_info_list(const Vector<String> &filters) {
	return GDREPackedData::get_singleton()->get_file_info_list(filters);
}

TypedArray<GDRESettings::PackInfo> GDRESettings::get_pack_info_list() const {
	TypedArray<PackInfo> ret;
	for (const auto &pack : packs) {
		ret.push_back(pack);
	}
	return ret;
}

String GDRESettings::localize_path(const String &p_path, const String &resource_dir) const {
	String res_path = resource_dir != "" ? resource_dir : project_path;

	if (p_path.begins_with("res://") || p_path.begins_with("user://")) {
		return p_path.simplify_path();
	}
	if ((p_path.is_absolute_path()) && (res_path == "" || !p_path.begins_with(res_path))) {
		if (is_pack_loaded()) {
			// it's a file system path... we need to start popping off the left-hand sides of the path until we find a directory that exists in the pack
			String dir_path = p_path.get_base_dir().simplify_path();
			// LEFT hand side, not right
			while (!dir_path.is_empty() && !DirAccess::dir_exists_absolute("res://" + dir_path)) {
				auto parts = dir_path.split("/", false, 1);
				if (parts.size() < 2) {
					dir_path = "";
					break;
				}
				dir_path = parts[1];
			}
			if (!dir_path.is_empty()) {
				return "res://" + dir_path.path_join(p_path.get_file());
			}
		}
		return p_path.simplify_path();
	}

	if (res_path == "") {
		//not initialized yet
		if (!p_path.is_absolute_path()) {
			//just tack on a "res://" here
			return "res://" + p_path;
		}
		return p_path;
	}

	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);

	String path = p_path.replace("\\", "/").simplify_path();

	if (dir->change_dir(path) == OK) {
		String cwd = dir->get_current_dir();
		cwd = cwd.replace("\\", "/");

		res_path = res_path.path_join("");

		// DirAccess::get_current_dir() is not guaranteed to return a path that with a trailing '/',
		// so we must make sure we have it as well in order to compare with 'res_path'.
		cwd = cwd.path_join("");

		if (!cwd.begins_with(res_path)) {
			return p_path;
		}

		return cwd.replace_first(res_path, "res://");
	} else {
		int sep = path.rfind("/");
		if (sep == -1) {
			return "res://" + path;
		}

		String parent = path.substr(0, sep);

		String plocal = localize_path(parent, res_path);
		if (plocal == "") {
			return "";
		}
		// Only strip the starting '/' from 'path' if its parent ('plocal') ends with '/'
		if (plocal[plocal.length() - 1] == '/') {
			sep += 1;
		}
		return plocal + path.substr(sep, path.size() - sep);
	}
}

String GDRESettings::globalize_path(const String &p_path, const String &resource_dir) const {
	String res_path = resource_dir != "" ? resource_dir : project_path;

	if (p_path.begins_with("res://")) {
		if (res_path != "") {
			return p_path.replace("res:/", res_path);
		}
		return p_path.replace("res://", "");
	} else if (p_path.begins_with("user://")) {
		String data_dir = OS::get_singleton()->get_user_data_dir();
		if (data_dir != "") {
			return p_path.replace("user:/", data_dir);
		}
		return p_path.replace("user://", "");
	} else if (!p_path.is_absolute_path()) {
		return res_path.path_join(p_path);
	}

	return p_path;
}

bool GDRESettings::is_fs_path(const String &p_path) const {
	if (!p_path.is_absolute_path()) {
		return true;
	}
	if (p_path.find("://") == -1 || p_path.begins_with("file://")) {
		return true;
	}
	//windows
	auto reg = RegEx("^[A-Za-z]:\\/");
	if (reg.search(p_path).is_valid()) {
		return true;
	}
	// unix
	if (p_path.begins_with("/")) {
		return true;
	}
	return false;
}

bool GDRESettings::has_any_remaps() const {
	if (is_pack_loaded()) {
		// version 3-4
		if (get_ver_major() >= 3) {
			if (remap_iinfo.size() > 0) {
				return true;
			}
			if (current_project->pcfg->is_loaded() && current_project->pcfg->has_setting("path_remap/remapped_paths")) {
				return true;
			}
		} else { // version 1-2
			if (current_project->pcfg->is_loaded() && current_project->pcfg->has_setting("remap/all")) {
				return true;
			}
		}
	}
	return false;
}

Dictionary GDRESettings::get_remaps(bool include_imports) const {
	Dictionary ret;
	if (is_pack_loaded()) {
		if (get_ver_major() >= 3) {
			for (auto E : remap_iinfo) {
				ret[E.key] = E.value->get_path();
			}
			if (current_project->pcfg->is_loaded() && current_project->pcfg->has_setting("path_remap/remapped_paths")) {
				PackedStringArray v3remaps = current_project->pcfg->get_setting("path_remap/remapped_paths", PackedStringArray());
				for (int i = 0; i < v3remaps.size(); i += 2) {
					ret[v3remaps[i]] = v3remaps[i + 1];
				}
			}
		} else {
			if (current_project->pcfg->is_loaded() && current_project->pcfg->has_setting("remap/all")) {
				PackedStringArray v2remaps = current_project->pcfg->get_setting("remap/all", PackedStringArray());
				for (int i = 0; i < v2remaps.size(); i += 2) {
					ret[v2remaps[i]] = v2remaps[i + 1];
				}
			}
		}
		if (include_imports) {
			for (int i = 0; i < import_files.size(); i++) {
				Ref<ImportInfo> iinfo = import_files[i];
				ret[iinfo->get_source_file()] = iinfo->get_path();
			}
		}
	}
	return ret;
}

bool has_old_remap(const Vector<String> &remaps, const String &src, const String &dst) {
	int idx = remaps.find(src);
	if (idx != -1 && idx % 2 == 0) {
		if (dst.is_empty()) {
			return true;
		}
		return idx + 1 >= remaps.size() ? false : remaps[idx + 1] == dst;
	}
	return false;
}

String GDRESettings::get_remapped_source_path(const String &p_dst) const {
	if (is_pack_loaded()) {
		if (get_ver_major() >= 3) {
			for (auto E : remap_iinfo) {
				if (E.value->get_path() == p_dst) {
					return E.key;
				}
			}
		}
		String setting = get_ver_major() < 3 ? "remap/all" : "path_remap/remapped_paths";
		if (is_project_config_loaded() && current_project->pcfg->has_setting(setting)) {
			PackedStringArray remaps = current_project->pcfg->get_setting(setting, PackedStringArray());
			int idx = remaps.find(p_dst);
			if (idx != -1 && idx % 2 == 1 && idx - 1 >= 0) {
				return remaps[idx - 1];
			}
		}
	}
	return "";
}

String GDRESettings::get_mapped_path(const String &p_src) const {
	String src = p_src;
	if (src.begins_with("uid://")) {
		auto id = ResourceUID::get_singleton()->text_to_id(src);
		if (ResourceUID::get_singleton()->has_id(id)) {
			src = ResourceUID::get_singleton()->get_id_path(id);
		} else {
			return "";
		}
	}
	if (is_pack_loaded()) {
		String remapped_path = get_remap(src);
		if (!remapped_path.is_empty()) {
			return remapped_path;
		}
		String local_src = localize_path(src);

		for (int i = 0; i < import_files.size(); i++) {
			Ref<ImportInfo> iinfo = import_files[i];
			if (iinfo->get_source_file().nocasecmp_to(local_src) == 0) {
				return iinfo->get_path();
			}
		}
	} else {
		Ref<ImportInfo> iinfo;
		String iinfo_path = src + ".import";
		String dep_path;
		if (FileAccess::exists(iinfo_path)) {
			iinfo = ImportInfo::load_from_file(iinfo_path, 0, 0);
			if (FileAccess::exists(iinfo->get_path())) {
				return iinfo->get_path();
			}
			auto dests = iinfo->get_dest_files();
			for (int i = 0; i < dests.size(); i++) {
				if (FileAccess::exists(dests[i])) {
					return dests[i];
				}
			}
		}
		iinfo_path = src + ".remap";
		if (FileAccess::exists(iinfo_path)) {
			iinfo = ImportInfo::load_from_file(iinfo_path, 0, 0);
			if (FileAccess::exists(iinfo->get_path())) {
				return iinfo->get_path();
			}
		}
	}
	return src;
}

String GDRESettings::get_remap(const String &src) const {
	if (is_pack_loaded()) {
		String local_src = localize_path(src);
		if (get_ver_major() >= 3) {
			String remap_file = local_src + ".remap";
			if (remap_iinfo.has(remap_file)) {
				return remap_iinfo[remap_file]->get_path();
			}
		}
		String setting = get_ver_major() < 3 ? "remap/all" : "path_remap/remapped_paths";
		if (is_project_config_loaded() && current_project->pcfg->has_setting(setting)) {
			PackedStringArray remaps = current_project->pcfg->get_setting(setting, PackedStringArray());
			int idx = remaps.find(local_src);
			if (idx != -1 && idx + 1 < remaps.size() && idx % 2 == 0) {
				return remaps[idx + 1];
			}
		}
	}
	return "";
}

bool GDRESettings::has_remap(const String &src, const String &dst) const {
	if (is_pack_loaded()) {
		String local_src = localize_path(src);
		String local_dst = !dst.is_empty() ? localize_path(dst) : "";
		if (get_ver_major() >= 3) {
			String remap_file = local_src + ".remap";
			if (remap_iinfo.has(remap_file)) {
				if (dst.is_empty()) {
					return true;
				}
				String dest_file = remap_iinfo[remap_file]->get_path();
				return dest_file == local_dst;
			}
		}
		String setting = get_ver_major() < 3 ? "remap/all" : "path_remap/remapped_paths";
		if (is_project_config_loaded() && current_project->pcfg->has_setting(setting)) {
			return has_old_remap(current_project->pcfg->get_setting(setting, PackedStringArray()), local_src, local_dst);
		}
	}
	return false;
}

//only works on 2.x right now
Error GDRESettings::add_remap(const String &src, const String &dst) {
	ERR_FAIL_COND_V_MSG(!is_pack_loaded(), ERR_DATABASE_CANT_READ, "Pack not loaded!");
	if (get_ver_major() >= 3) {
		ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "Adding Remaps is not supported in 3.x-4.x packs yet!");
	}
	ERR_FAIL_COND_V_MSG(!is_project_config_loaded(), ERR_DATABASE_CANT_READ, "project config not loaded!");
	String setting = get_ver_major() < 3 ? "remap/all" : "path_remap/remapped_paths";
	PackedStringArray v2remaps = current_project->pcfg->get_setting(setting, PackedStringArray());
	String local_src = localize_path(src);
	String local_dst = localize_path(dst);
	int idx = v2remaps.find(local_src);
	if (idx != -1) {
		v2remaps.write[idx + 1] = local_dst;
	} else {
		v2remaps.push_back(local_src);
		v2remaps.push_back(local_dst);
	}
	current_project->pcfg->set_setting(setting, v2remaps);
	return OK;
}

Error GDRESettings::remove_remap(const String &src, const String &dst, const String &output_dir) {
	ERR_FAIL_COND_V_MSG(!is_pack_loaded(), ERR_DATABASE_CANT_READ, "Pack not loaded!");
	Error err;
	if (get_ver_major() >= 3) {
		ERR_FAIL_COND_V_MSG(output_dir.is_empty(), ERR_INVALID_PARAMETER, "Output directory must be specified for 3.x-4.x packs!");
		String remap_file = localize_path(src) + ".remap";
		if (remap_iinfo.has(remap_file)) {
			if (!dst.is_empty()) {
				String dest_file = remap_iinfo[remap_file]->get_path();
				if (dest_file != localize_path(dst)) {
					ERR_FAIL_V_MSG(ERR_DOES_NOT_EXIST, "Remap between" + src + " and " + dst + " does not exist!");
				}
			}
			remap_iinfo.erase(remap_file);
			Ref<DirAccess> da = DirAccess::open(output_dir, &err);
			ERR_FAIL_COND_V_MSG(err, err, "Can't open directory " + output_dir);
			String dest_path = output_dir.path_join(remap_file.replace("res://", ""));
			if (!FileAccess::exists(dest_path)) {
				return ERR_FILE_NOT_FOUND;
			}
			return da->remove(dest_path);
		}
	}
	if (!is_project_config_loaded()) {
		ERR_FAIL_COND_V_MSG(get_ver_major() < 3, ERR_DATABASE_CANT_READ, "project config not loaded!");
		ERR_FAIL_V_MSG(ERR_DOES_NOT_EXIST, "Remap between" + src + " and " + dst + " does not exist!");
	}
	String setting = get_ver_major() < 3 ? "remap/all" : "path_remap/remapped_paths";
	ERR_FAIL_COND_V_MSG(!current_project->pcfg->has_setting(setting), ERR_DOES_NOT_EXIST, "Remap between" + src + " and " + dst + " does not exist!");
	PackedStringArray v2remaps = current_project->pcfg->get_setting(setting, PackedStringArray());
	String local_src = localize_path(src);
	String local_dst = localize_path(dst);
	if (has_old_remap(v2remaps, local_src, local_dst)) {
		v2remaps.erase(local_src);
		v2remaps.erase(local_dst);
		if (v2remaps.size()) {
			err = current_project->pcfg->set_setting("remap/all", v2remaps);
		} else {
			err = current_project->pcfg->remove_setting("remap/all");
		}
		return err;
	}
	ERR_FAIL_V_MSG(ERR_DOES_NOT_EXIST, "Remap between" + src + " and " + dst + " does not exist!");
}

bool GDRESettings::has_project_setting(const String &p_setting) {
	ERR_FAIL_COND_V_MSG(!is_pack_loaded(), false, "Pack not loaded!");
	if (!is_project_config_loaded()) {
		WARN_PRINT("Attempted to check project setting " + p_setting + ", but no project config loaded");
		return false;
	}
	return current_project->pcfg->has_setting(p_setting);
}

Variant GDRESettings::get_project_setting(const String &p_setting) {
	ERR_FAIL_COND_V_MSG(!is_pack_loaded(), Variant(), "Pack not loaded!");
	ERR_FAIL_COND_V_MSG(!is_project_config_loaded(), Variant(), "project config not loaded!");
	return current_project->pcfg->get_setting(p_setting, Variant());
}

String GDRESettings::get_project_config_path() {
	ERR_FAIL_COND_V_MSG(!is_project_config_loaded(), String(), "project config not loaded!");
	return current_project->pcfg->get_cfg_path();
}

String GDRESettings::get_log_file_path() {
	if (!logger) {
		return "";
	}
	return logger->get_path();
}

bool GDRESettings::is_headless() const {
	return headless;
}

float GDRESettings::get_auto_display_scale() const {
#ifdef LINUXBSD_ENABLED
	if (DisplayServer::get_singleton()->get_name() == "Wayland") {
		float main_window_scale = DisplayServer::get_singleton()->screen_get_scale(DisplayServer::SCREEN_OF_MAIN_WINDOW);

		if (DisplayServer::get_singleton()->get_screen_count() == 1 || Math::fract(main_window_scale) != 0) {
			// If we have a single screen or the screen of the window is fractional, all
			// bets are off. At this point, let's just return the current's window scale,
			// which is special-cased to the scale of `SCREEN_OF_MAIN_WINDOW`.
			return main_window_scale;
		}

		// If the above branch didn't fire, fractional scaling isn't going to work
		// properly anyways (we're need the ability to change the UI scale at runtime).
		// At this point it's more convenient to "supersample" like we do with other
		// platforms, hoping that the user is only using integer-scaled screens.
		return DisplayServer::get_singleton()->screen_get_max_scale();
	}
#endif

#if defined(MACOS_ENABLED) || defined(ANDROID_ENABLED)
	return DisplayServer::get_singleton()->screen_get_max_scale();
#else
	const int screen = DisplayServer::get_singleton()->window_get_current_screen();

	if (DisplayServer::get_singleton()->screen_get_size(screen) == Vector2i()) {
		// Invalid screen size, skip.
		return 1.0;
	}

	// Use the smallest dimension to use a correct display scale on portrait displays.
	const int smallest_dimension = MIN(DisplayServer::get_singleton()->screen_get_size(screen).x, DisplayServer::get_singleton()->screen_get_size(screen).y);
	if (DisplayServer::get_singleton()->screen_get_dpi(screen) >= 192 && smallest_dimension >= 1400) {
		// hiDPI display.
		return 2.0;
	} else if (smallest_dimension >= 1700) {
		// Likely a hiDPI display, but we aren't certain due to the returned DPI.
		// Use an intermediate scale to handle this situation.
		return 1.5;
	} else if (smallest_dimension <= 800) {
		// Small loDPI display. Use a smaller display scale so that editor elements fit more easily.
		// Icons won't look great, but this is better than having editor elements overflow from its window.
		return 0.75;
	}
	return 1.0;
#endif
}

String GDRESettings::get_sys_info_string() const {
	String OS_Name = OS::get_singleton()->get_distribution_name();
	String OS_Version = OS::get_singleton()->get_version();
	String adapter_name = RenderingServer::get_singleton() ? RenderingServer::get_singleton()->get_video_adapter_name() : "";
	String render_driver = OS::get_singleton()->get_current_rendering_driver_name();
	if (adapter_name.is_empty()) {
		adapter_name = "headless";
	} else {
		adapter_name += ", " + render_driver;
	}

	return OS_Name + " " + OS_Version + ", " + adapter_name;
}

void GDRESettings::log_sysinfo() {
	print_line("GDRE Tools " + String(GDRE_VERSION));
	print_line(get_sys_info_string());
}

Error GDRESettings::open_log_file(const String &output_dir) {
	String logfile = output_dir.path_join("gdre_export.log");
	bool was_buffering = logger->is_prebuffering_enabled();
	Error err = logger->open_file(logfile);
	if (!was_buffering) {
		log_sysinfo();
	}
	ERR_FAIL_COND_V_MSG(err == ERR_ALREADY_IN_USE, err, "Already logging to another file");
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not open log file " + logfile);
	return OK;
}

Error GDRESettings::close_log_file() {
	logger->close_file();
	return OK;
}

Array GDRESettings::get_import_files(bool copy) {
	if (!copy) {
		return import_files;
	}
	Array ifiles;
	for (int i = 0; i < import_files.size(); i++) {
		ifiles.push_back(ImportInfo::copy(import_files[i]));
	}
	return ifiles;
}

bool GDRESettings::has_path_loaded(const String &p_path) {
	if (is_pack_loaded()) {
		return GDREPackedData::get_singleton()->has_path(p_path);
	}
	return false;
}

String GDRESettings::get_loaded_pack_data_dir() {
	String data_dir = "res://.godot";
	if (is_project_config_loaded()) {
		// if this is set, we want to load the cache from the hidden directory
		return current_project->pcfg->get_setting(
					   "application/config/use_hidden_project_data_directory",
					   true)
				? data_dir
				: "res://godot";
	}
	// else...
	if (!DirAccess::exists(data_dir) && DirAccess::exists("res://godot")) {
		return "res://godot";
	}

	return data_dir;
}

Error GDRESettings::load_pack_uid_cache(bool p_reset) {
	if (!is_pack_loaded()) {
		return ERR_UNAVAILABLE;
	}
	String cache_file = get_loaded_pack_data_dir().path_join("uid_cache.bin");
	if (!FileAccess::exists(cache_file)) {
		return ERR_FILE_NOT_FOUND;
	}
	Ref<FileAccess> f = FileAccess::open(cache_file, FileAccess::READ);

	if (f.is_null()) {
		return ERR_CANT_OPEN;
	}

	if (p_reset) {
		ResourceUID::get_singleton()->clear();
		unique_ids.clear();
		path_to_uid.clear();
	}

	uint32_t entry_count = f->get_32();
	for (uint32_t i = 0; i < entry_count; i++) {
		int64_t id = f->get_64();
		int32_t len = f->get_32();
		UID_Cache c;
		c.cs.resize_uninitialized(len + 1);
		ERR_FAIL_COND_V(c.cs.size() != len + 1, ERR_FILE_CORRUPT); // out of memory
		c.cs[len] = 0;
		int32_t rl = f->get_buffer((uint8_t *)c.cs.ptrw(), len);
		ERR_FAIL_COND_V(rl != len, ERR_FILE_CORRUPT);

		c.saved_to_cache = true;
		unique_ids[id] = c;
		path_to_uid[String::utf8(c.cs)] = id;
	}
	for (auto E : path_to_uid) {
		if (ResourceUID::get_singleton()->has_id(E.second)) {
			String old_path = ResourceUID::get_singleton()->get_id_path(E.second);
			String new_path = E.first;
			if (old_path != new_path) {
				WARN_PRINT("Duplicate ID found in cache: " + itos(E.second) + " -> " + old_path + "\nReplacing with: " + new_path);
			}
			ResourceUID::get_singleton()->set_id(E.second, new_path);
		} else {
			ResourceUID::get_singleton()->add_id(E.second, E.first);
		}
	}
#ifdef TOOLS_ENABLED
	if (!EditorNode::get_singleton()) {
		ResourceSaver::set_get_resource_id_for_path(&GDRESettings::_get_uid_for_path);
	}
#else
	ResourceSaver::set_get_resource_id_for_path(&GDRESettings::_get_uid_for_path);
#endif
	return OK;
}

Error GDRESettings::reset_uid_cache() {
	unique_ids.clear();
	path_to_uid.clear();
	ResourceUID::get_singleton()->clear();
	return ResourceUID::get_singleton()->load_from_cache(true);
}

ResourceUID::ID GDRESettings::_get_uid_for_path(const String &p_path, bool _generate) {
	return get_singleton()->get_uid_for_path(p_path);
}

ResourceUID::ID GDRESettings::get_uid_for_path(const String &p_path) const {
	ResourceUID::ID id = ResourceUID::INVALID_ID;
	path_to_uid.if_contains(p_path, [&](const ParallelFlatHashMap<String, ResourceUID::ID>::value_type &e) {
		id = e.second;
	});
	//
	// if (id == ResourceUID::INVALID_ID) {
	// 	auto src_iinfo = get_import_info_by_dest(p_path);
	// 	path_to_uid.if_contains(src_iinfo->get_source_file(), [&](const ParallelFlatHashMap<String, ResourceUID::ID>::value_type &e) {
	// 		id = e.second;
	// 	});}
	return id;
}

String GDRESettings::get_game_name() const {
	String game_name;
	if (is_project_config_loaded()) {
		game_name = current_project->pcfg->get_setting(get_ver_major() <= 2 ? "application/name" : "application/config/name", "");
	}
	if (game_name.is_empty() && is_pack_loaded()) {
		game_name = get_project_path().get_file().get_basename();
	}
	return game_name;
}

Error GDRESettings::load_pack_gdscript_cache(bool p_reset) {
	if (!is_pack_loaded()) {
		return ERR_UNAVAILABLE;
	}

	auto cache_file = get_loaded_pack_data_dir().path_join("global_script_class_cache.cfg");
	if (!FileAccess::exists(cache_file)) {
		return ERR_FILE_NOT_FOUND;
	}
	Array global_class_list;
	Ref<ConfigFile> cf;
	cf.instantiate();
	if (cf->load(cache_file) == OK) {
		global_class_list = cf->get_value("", "list", Array());
	} else {
		return ERR_FILE_CANT_READ;
	}

	if (p_reset) {
		reset_gdscript_cache();
	}
	for (int i = 0; i < global_class_list.size(); i++) {
		Dictionary d = global_class_list[i];
		String path = d["path"];
		// path = path.simplify_path();
		script_cache[path] = d;
	}
	return OK;
}

Error GDRESettings::reset_gdscript_cache() {
	script_cache.clear();
	return OK;
}

void GDRESettings::_do_import_load(uint32_t i, IInfoToken *tokens) {
	tokens[i].info = ImportInfo::load_from_file(tokens[i].path, tokens[i].ver_major, tokens[i].ver_minor);
	if (tokens[i].info.is_null()) {
		tokens[i].err = ERR_FILE_CANT_OPEN;
	} else {
		if (tokens[i].info->get_iitype() == ImportInfo::REMAP) {
			if (!FileAccess::exists(tokens[i].info->get_path())) {
				tokens[i].err = ERR_FILE_MISSING_DEPENDENCIES;
			}
		}
	}
}

String GDRESettings::get_IInfoToken_description(uint32_t i, IInfoToken *p_userdata) {
	return p_userdata[i].path;
}

Error GDRESettings::load_import_files() {
	Vector<String> resource_files;
	ERR_FAIL_COND_V_MSG(!is_pack_loaded(), ERR_DOES_NOT_EXIST, "pack/dir not loaded!");
	static const Vector<String> v3wildcards = {
		"*.import",
		"*.remap",
		"*.gdnlib",
		"*.gdextension",
	};
	int _ver_major = get_ver_major();
	// version isn't set, we have to guess from contents of dir.
	// While the load_import_file() below will still have 0.0 set as the version,
	// load_from_file() will automatically load the binary resources to determine the version.
	if (_ver_major == 0) {
		_ver_major = get_ver_major_from_dir();
	}
	if (_ver_major <= 2) {
		List<String> extensions;
		ResourceCompatLoader::get_base_extensions(&extensions, 2);
		Vector<String> v2wildcards;
		for (auto &ext : extensions) {
			v2wildcards.push_back("*." + ext);
		}
		v2wildcards.push_back("*.gde");
		v2wildcards.push_back("*.gdc");
		resource_files = get_file_list(v2wildcards);
	} else if (_ver_major == 3 || _ver_major == 4) {
		resource_files = get_file_list(v3wildcards);
	} else {
		ERR_FAIL_V_MSG(ERR_BUG, "Can't determine major version!");
	}
	Vector<IInfoToken> tokens;
	for (int i = 0; i < resource_files.size(); i++) {
		tokens.push_back({ resource_files[i], nullptr, (int)get_ver_major(), (int)get_ver_minor() });
	}

	if (tokens.size() == 0) {
		print_line("No import files found!");
		return OK;
	}

	Error err = TaskManager::get_singleton()->run_multithreaded_group_task(
			this,
			&GDRESettings::_do_import_load,
			tokens.ptrw(),
			tokens.size(),
			&GDRESettings::get_IInfoToken_description,
			"GDRESettings::load_import_files",
			RTR("Loading import files..."),
			false);

	if (err != OK) {
		WARN_PRINT("Failed to load import files!");
	}
	for (int i = 0; i < tokens.size(); i++) {
		if (tokens[i].info.is_null()) {
			WARN_PRINT("Can't load import file: " + resource_files[i]);
			continue;
		}
		if (tokens[i].info->get_iitype() == ImportInfo::REMAP) {
			if (tokens[i].err == ERR_FILE_MISSING_DEPENDENCIES) {
				WARN_PRINT(vformat("Remapped path does not exist: %s -> %s", tokens[i].info->get_source_file(), tokens[i].info->get_path()));
			} else if (tokens[i].err) {
				WARN_PRINT("Can't load remap file: " + resource_files[i] + " (" + itos(tokens[i].err) + ")");
				continue;
			} else {
				remap_iinfo.insert(tokens[i].path, tokens[i].info);
			}
		}
		import_files.push_back(tokens[i].info);
	}
	return OK;
}

Error GDRESettings::load_import_file(const String &p_path) {
	Ref<ImportInfo> i_info = ImportInfo::load_from_file(p_path, get_ver_major(), get_ver_minor());
	ERR_FAIL_COND_V_MSG(i_info.is_null(), ERR_FILE_CANT_OPEN, "Failed to load import file " + p_path);

	import_files.push_back(i_info);
	if (i_info->get_iitype() == ImportInfo::REMAP) {
		if (!FileAccess::exists(i_info->get_path())) {
			print_line(vformat("Remapped path does not exist: %s -> %s", i_info->get_source_file(), i_info->get_path()));
			return ERR_FILE_MISSING_DEPENDENCIES;
		}
		remap_iinfo.insert(p_path, i_info);
	}
	return OK;
}

Ref<ImportInfo> GDRESettings::get_import_info_by_source(const String &p_path) {
	Ref<ImportInfo> iinfo;
	for (int i = 0; i < import_files.size(); i++) {
		iinfo = import_files[i];
		if (iinfo->get_source_file() == p_path) {
			return iinfo;
		}
	}
	// not found
	return Ref<ImportInfo>();
}

Ref<ImportInfo> GDRESettings::get_import_info_by_dest(const String &p_path) const {
	Ref<ImportInfo> iinfo;
	for (int i = 0; i < import_files.size(); i++) {
		iinfo = import_files[i];
		// for (auto &dest : iinfo->get_dest_files()) {
		// 	if (dest.to_lower() == p_path.to_lower()) {
		// 		return iinfo;
		// 	}
		// }
		if (iinfo->get_dest_files().has(p_path)) {
			return iinfo;
		}
	}
	return Ref<ImportInfo>();
}
bool GDRESettings::pack_has_project_config() {
	if (!is_pack_loaded()) {
		return false;
	}
	if (get_ver_major() == 2) {
		if (has_path_loaded("res://engine.cfb") || has_path_loaded("res://engine.cfg")) {
			return true;
		}
	} else if (get_ver_major() == 3 || get_ver_major() == 4) {
		if (has_path_loaded("res://project.binary") || has_path_loaded("res://project.godot")) {
			return true;
		}
	} else {
		if (has_path_loaded("res://engine.cfb") || has_path_loaded("res://engine.cfg") ||
				has_path_loaded("res://project.binary") || has_path_loaded("res://project.godot")) {
			return true;
		}
	}
	return false;
}

String GDRESettings::get_gdre_version() const {
	return GDRE_VERSION;
}

String GDRESettings::get_disclaimer_text() const {
	return String("Godot RE Tools, ") + String(GDRE_VERSION) + String(" \n\n") +
			get_disclaimer_body();
}

String GDRESettings::get_disclaimer_body() {
	return RTR(String("Resources, binary code and source code might be protected by copyright and trademark ") +
			"laws. Before using this software make sure that decompilation is not prohibited by the " +
			"applicable license agreement, permitted under applicable law or you obtained explicit " +
			"permission from the copyright owner.\n\n" +
			"The authors and copyright holders of this software do neither encourage nor condone " +
			"the use of this software, and disclaim any liability for use of the software in violation of " +
			"applicable laws.\n\n" +
			"Please report any bugs to the GitHub repository\n");
}

// bool has_resource_strings() const;
// void load_all_resource_strings();
// void get_resource_strings(HashSet<String> &r_strings) const;

bool GDRESettings::loaded_resource_strings() const {
	return is_pack_loaded() && current_project->resource_strings.size() > 0;
}

//	void _do_string_load(uint32_t i, StringLoadToken *tokens);
void GDRESettings::_do_string_load(uint32_t i, StringLoadToken *tokens) {
	String src_ext = tokens[i].path.get_extension().to_lower();
	// check if script
	if (src_ext == "gd" || src_ext == "gdc" || src_ext == "gde") {
		tokens[i].err = GDScriptDecomp::get_script_strings(tokens[i].path, tokens[i].engine_version, tokens[i].strings);
		return;
	} else if (src_ext == "csv" || src_ext == "json") {
		Ref<FileAccess> f = FileAccess::open(tokens[i].path, FileAccess::READ, &tokens[i].err);
		ERR_FAIL_COND_MSG(f.is_null(), "Failed to open file " + tokens[i].path);
		uint64_t file_len = f->get_length();
		if (file_len == 0) {
			return;
		}
		Vector<uint8_t> file_buf;
		file_buf.resize(file_len);
		f->get_buffer(file_buf.ptrw(), file_len);
		// check first 8000 bytes for null bytes
		for (int j = 0; j < MIN(file_len, 8000); j++) {
			if (file_buf[j] == 0) {
				return;
			}
		}
		if (!gdre::detect_utf8(file_buf)) {
			return;
		}

		if (src_ext == "csv") {
			f->seek(0);
			// get the first line
			String header = f->get_line();
			String delimiter = ",";
			if (!header.contains(",")) {
				if (header.contains(";")) {
					delimiter = ";";
				} else if (header.contains("|")) {
					delimiter = "|";
				} else if (header.contains("\t")) {
					delimiter = "\t";
				}
			}
			f->seek(0);
			while (!f->eof_reached()) {
				Vector<String> line = f->get_csv_line(delimiter);
				for (int j = 0; j < line.size(); j++) {
					if (!line[j].is_numeric()) {
						tokens[i].strings.append(line[j]);
					}
				}
			}
		} else if (src_ext == "json") {
			String jstring;
			tokens[i].err = jstring.append_utf8((const char *)file_buf.ptr(), file_len);
			ERR_FAIL_COND_MSG(tokens[i].err, "Failed to open file " + tokens[i].path);
			if (jstring.strip_edges().is_empty()) {
				return;
			}
			Variant var = JSON::parse_string(jstring);
			gdre::get_strings_from_variant(var, tokens[i].strings, tokens[i].engine_version);
		}
		return;
	}
	auto res = ResourceCompatLoader::fake_load(tokens[i].path, "", &tokens[i].err);
	if (res.is_null()) {
		WARN_PRINT("Failed to load resource " + tokens[i].path);
	}
	gdre::get_strings_from_variant(res, tokens[i].strings, tokens[i].engine_version);
}

String GDRESettings::get_string_load_token_description(uint32_t i, StringLoadToken *p_userdata) {
	return p_userdata[i].path;
}

void GDRESettings::load_all_resource_strings() {
	if (!is_pack_loaded()) {
		return;
	}
	current_project->resource_strings.clear();
	List<String> extensions;
	ResourceCompatLoader::get_base_extensions(&extensions, get_ver_major());
	Vector<String> wildcards;
	for (auto &ext : extensions) {
		wildcards.push_back("*." + ext);
	}
	wildcards.push_back("*.tres");
	wildcards.push_back("*.tscn");
	wildcards.push_back("*.gd");
	wildcards.push_back("*.gdc");
	if (!error_encryption) {
		wildcards.push_back("*.gde");
	}
	wildcards.push_back("*.csv");
	wildcards.push_back("*.json");

	Vector<String> r_files = get_file_list(wildcards);
	Vector<StringLoadToken> tokens;
	tokens.resize(r_files.size());
	String engine_ver = get_version_string();
	for (int i = 0; i < r_files.size(); i++) {
		tokens.write[i].path = r_files[i];
		tokens.write[i].engine_version = engine_ver;
	}
	print_line("Loading resource strings, this may take a while!!");
	Error err = TaskManager::get_singleton()->run_multithreaded_group_task(
			this,
			&GDRESettings::_do_string_load,
			tokens.ptrw(),
			tokens.size(),
			&GDRESettings::get_string_load_token_description,
			"GDRESettings::load_all_resource_strings", RTR("Loading resource strings..."));
	if (err != OK) {
		WARN_PRINT("Failed to load resource strings!");
	}
	print_line("Resource strings loaded!");
	for (int i = 0; i < tokens.size(); i++) {
		if (tokens[i].err != OK) {
			print_verbose("Failed to load resource strings for " + tokens[i].path);
			continue;
		}
		for (auto &str : tokens[i].strings) {
			current_project->resource_strings.insert(str);
		}
	}
}

void GDRESettings::get_resource_strings(HashSet<String> &r_strings) const {
	r_strings = current_project->resource_strings;
}

void GDRESettings::prepop_plugin_cache(const Vector<String> &plugins) {
	PluginManager::prepop_cache(plugins, !GDREConfig::get_singleton()->get_setting("force_single_threaded", false));
}

Vector<String> GDRESettings::get_errors() {
	return GDRELogger::get_errors();
}

void GDRESettings::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_project", "p_paths", "cmd_line_extract"), &GDRESettings::load_project, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("unload_project"), &GDRESettings::unload_project);
	ClassDB::bind_method(D_METHOD("get_gdre_resource_path"), &GDRESettings::get_gdre_resource_path);
	ClassDB::bind_method(D_METHOD("get_gdre_user_path"), &GDRESettings::get_gdre_user_path);
	ClassDB::bind_method(D_METHOD("get_encryption_key"), &GDRESettings::get_encryption_key);
	ClassDB::bind_method(D_METHOD("get_encryption_key_string"), &GDRESettings::get_encryption_key_string);
	ClassDB::bind_method(D_METHOD("is_pack_loaded"), &GDRESettings::is_pack_loaded);
	ClassDB::bind_method(D_METHOD("_set_error_encryption", "is_encryption_error"), &GDRESettings::_set_error_encryption);
	ClassDB::bind_method(D_METHOD("set_encryption_key_string", "key"), &GDRESettings::set_encryption_key_string);
	ClassDB::bind_method(D_METHOD("set_encryption_key", "key"), &GDRESettings::set_encryption_key);
	ClassDB::bind_method(D_METHOD("reset_encryption_key"), &GDRESettings::reset_encryption_key);
	ClassDB::bind_method(D_METHOD("get_file_list", "filters"), &GDRESettings::get_file_list, DEFVAL(Vector<String>()));
	ClassDB::bind_method(D_METHOD("get_file_info_array", "filters"), &GDRESettings::get_file_info_array, DEFVAL(Vector<String>()));
	ClassDB::bind_method(D_METHOD("get_pack_type"), &GDRESettings::get_pack_type);
	ClassDB::bind_method(D_METHOD("get_pack_path"), &GDRESettings::get_pack_path);
	ClassDB::bind_method(D_METHOD("get_pack_info_list"), &GDRESettings::get_pack_info_list);
	ClassDB::bind_method(D_METHOD("get_version_string"), &GDRESettings::get_version_string);
	ClassDB::bind_method(D_METHOD("get_ver_major"), &GDRESettings::get_ver_major);
	ClassDB::bind_method(D_METHOD("get_ver_minor"), &GDRESettings::get_ver_minor);
	ClassDB::bind_method(D_METHOD("get_ver_rev"), &GDRESettings::get_ver_rev);
	ClassDB::bind_method(D_METHOD("get_file_count"), &GDRESettings::get_file_count);
	ClassDB::bind_method(D_METHOD("globalize_path", "p_path", "resource_path"), &GDRESettings::globalize_path);
	ClassDB::bind_method(D_METHOD("localize_path", "p_path", "resource_path"), &GDRESettings::localize_path);
	ClassDB::bind_method(D_METHOD("set_project_path", "p_path"), &GDRESettings::set_project_path);
	ClassDB::bind_method(D_METHOD("get_project_path"), &GDRESettings::get_project_path);
	ClassDB::bind_method(D_METHOD("open_log_file", "output_dir"), &GDRESettings::open_log_file);
	ClassDB::bind_method(D_METHOD("get_log_file_path"), &GDRESettings::get_log_file_path);
	ClassDB::bind_method(D_METHOD("is_fs_path", "p_path"), &GDRESettings::is_fs_path);
	ClassDB::bind_method(D_METHOD("close_log_file"), &GDRESettings::close_log_file);
	ClassDB::bind_method(D_METHOD("get_remaps", "include_imports"), &GDRESettings::get_remaps, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("has_any_remaps"), &GDRESettings::has_any_remaps);
	ClassDB::bind_method(D_METHOD("has_remap", "src", "dst"), &GDRESettings::has_remap);
	ClassDB::bind_method(D_METHOD("add_remap", "src", "dst"), &GDRESettings::add_remap);
	ClassDB::bind_method(D_METHOD("remove_remap", "src", "dst", "output_dir"), &GDRESettings::remove_remap);
	ClassDB::bind_method(D_METHOD("get_project_setting", "p_setting"), &GDRESettings::get_project_setting);
	ClassDB::bind_method(D_METHOD("has_project_setting", "p_setting"), &GDRESettings::has_project_setting);
	ClassDB::bind_method(D_METHOD("get_project_config_path"), &GDRESettings::get_project_config_path);
	ClassDB::bind_method(D_METHOD("get_cwd"), &GDRESettings::get_cwd);
	ClassDB::bind_method(D_METHOD("get_import_files", "copy"), &GDRESettings::get_import_files);
	ClassDB::bind_method(D_METHOD("has_path_loaded", "p_path"), &GDRESettings::has_path_loaded);
	ClassDB::bind_method(D_METHOD("load_import_files"), &GDRESettings::load_import_files);
	ClassDB::bind_method(D_METHOD("load_import_file", "p_path"), &GDRESettings::load_import_file);
	ClassDB::bind_method(D_METHOD("get_import_info_by_source", "p_path"), &GDRESettings::get_import_info_by_source);
	ClassDB::bind_method(D_METHOD("get_import_info_by_dest", "p_path"), &GDRESettings::get_import_info_by_dest);
	ClassDB::bind_method(D_METHOD("get_exec_dir"), &GDRESettings::get_exec_dir);
	ClassDB::bind_method(D_METHOD("are_imports_loaded"), &GDRESettings::are_imports_loaded);
	ClassDB::bind_method(D_METHOD("is_project_config_loaded"), &GDRESettings::is_project_config_loaded);
	ClassDB::bind_method(D_METHOD("is_headless"), &GDRESettings::is_headless);
	ClassDB::bind_method(D_METHOD("get_sys_info_string"), &GDRESettings::get_sys_info_string);
	ClassDB::bind_method(D_METHOD("load_project_config"), &GDRESettings::load_project_config);
	ClassDB::bind_method(D_METHOD("save_project_config", "p_out_dir"), &GDRESettings::save_project_config);
	ClassDB::bind_method(D_METHOD("pack_has_project_config"), &GDRESettings::pack_has_project_config);
	ClassDB::bind_method(D_METHOD("get_gdre_version"), &GDRESettings::get_gdre_version);
	ClassDB::bind_method(D_METHOD("get_disclaimer_text"), &GDRESettings::get_disclaimer_text);
	ClassDB::bind_method(D_METHOD("prepop_plugin_cache", "plugins"), &GDRESettings::prepop_plugin_cache);
	ClassDB::bind_method(D_METHOD("get_home_dir"), &GDRESettings::get_home_dir);
	ClassDB::bind_method(D_METHOD("get_errors"), &GDRESettings::get_errors);
	ClassDB::bind_method(D_METHOD("get_auto_display_scale"), &GDRESettings::get_auto_display_scale);
}

// This is at the bottom to account for the platform header files pulling in their respective OS headers and creating all sorts of issues

#ifdef WINDOWS_ENABLED
#include "platform/windows/os_windows.h"
#include "platform/windows/windows_terminal_logger.h"
#define PLATFORM_OS OS_Windows
#define STDOUT_LOGGER WindowsTerminalLogger
#endif
#ifdef LINUXBSD_ENABLED
#include "drivers/unix/os_unix.h"
#include "platform/linuxbsd/os_linuxbsd.h"
#define PLATFORM_OS OS_LinuxBSD
#define STDOUT_LOGGER UnixTerminalLogger
#endif
#ifdef MACOS_ENABLED
#include "drivers/unix/os_unix.h"
#define PLATFORM_OS OS_Unix
#define STDOUT_LOGGER UnixTerminalLogger
#endif
#ifdef WEB_ENABLED
#include "core/io/logger.h"
#include "platform/web/os_web.h"
#define PLATFORM_OS OS_Web
#define STDOUT_LOGGER StdLogger
#endif
#ifdef ANDROID_ENABLED
#include "platform/android/os_android.h"
#include <android/log.h>
#define PLATFORM_OS OS_Android
// TODO: This is a hack to get around the fact that AndroidLogger is not exposed in the header
class GDREAndroidLogger : public Logger {
public:
	virtual void logv(const char *p_format, va_list p_list, bool p_err) {
		__android_log_vprint(p_err ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "godot", p_format, p_list);
	}

	virtual ~AndroidLogger() {}
};
#define STDOUT_LOGGER GDREAndroidLogger
#endif
#ifdef IPHONE_ENABLED
#include "platform/ios/ios_terminal_logger.h"
#include "platform/ios/os_ios.h"
#define PLATFORM_OS OS_IOS
#define STDOUT_LOGGER IOSTerminalLogger
#endif
// A hack to add another logger to the OS singleton
template <class T>
class GDREOS : public T {
	static_assert(std::is_base_of<OS, T>::value, "T must derive from OS");

public:
	static void do_add_logger(GDREOS<T> *ptr, Logger *p_logger) {
		ptr->add_logger(p_logger);
	}
	static void do_set_logger(GDREOS<T> *ptr, CompositeLogger *p_logger) {
		ptr->_set_logger(p_logger);
	}
};

// This adds another logger to the global composite logger so that we can write
// export logs to project directories.
// main.cpp is apparently the only class that can add these, but "add_logger" is
// only protected, so we can cast the singleton to a child class pointer and then call it.
void GDRESettings::add_logger() {
	OS *os_singleton = OS::get_singleton();
	String os_name = os_singleton->get_name();
	GDREOS<PLATFORM_OS> *_gdre_os = reinterpret_cast<GDREOS<PLATFORM_OS> *>(os_singleton);
	STDOUT_LOGGER *stdout_logger = memnew(STDOUT_LOGGER);
	GDRELogger::set_stdout_logger(stdout_logger);

	// TODO: add a logger for global debug logging if it's enabled
	Vector<Logger *> loggers;
	loggers.push_back(logger);
	GDREOS<PLATFORM_OS>::do_set_logger(_gdre_os, memnew(CompositeLogger(loggers)));
	// GDREOS<PLATFORM_OS>::do_add_logger(_gdre_os, logger);
}
