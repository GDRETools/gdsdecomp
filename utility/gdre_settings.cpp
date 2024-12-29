#include "gdre_settings.h"

#include "bytecode/bytecode_base.h"
#include "bytecode/bytecode_tester.h"
#include "compat/resource_compat_binary.h"
#include "compat/resource_loader_compat.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/string/print_string.h"
#include "editor/gdre_version.gen.h"
#include "modules/zip/zip_reader.h"
#include "utility/common.h"
#include "utility/extension_info_getter.h"
#include "utility/file_access_gdre.h"
#include "utility/gdre_logger.h"
#include "utility/gdre_packed_source.h"
#include "utility/import_info.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/object/script_language.h"
#include "modules/regex/regex.h"
#include "servers/rendering_server.h"

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
	buffer.resize((int)expected_size);
	if (::GetCurrentDirectoryW(expected_size, (wchar_t *)buffer.ptrw()) == 0)
		return ".";

	String result;
	if (result.parse_utf16(buffer.ptr())) {
		return ".";
	}
	return result.simplify_path();
#elif defined(UNIX_ENABLED)
	char buffer[PATH_MAX];
	if (::getcwd(buffer, sizeof(buffer)) == nullptr) {
		return ".";
	}

	String result;
	if (result.parse_utf8(buffer)) {
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
	if (FileAccess::exists("res://engine.cfb") || FileAccess::exists("res://engine.cfg")) {
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
	AssetLibInfoGetter::load_cache();
}

GDRESettings::~GDRESettings() {
	AssetLibInfoGetter::save_cache();
	remove_current_pack();
	memdelete(gdre_packeddata_singleton);
	singleton = nullptr;
	logger->_disable();
	// logger doesn't get memdeleted because the OS singleton will do so
}
String GDRESettings::get_cwd() {
	return GDRESettings::_get_cwd();
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

String GDRESettings::get_gdre_user_path() const {
	return gdre_user_path;
}

Vector<uint8_t> GDRESettings::get_encryption_key() {
	return enc_key;
}
String GDRESettings::get_encryption_key_string() {
	return enc_key_str;
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
String GDRESettings::get_project_path() {
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
	reset_encryption_key();
}

void GDRESettings::reset_encryption_key() {
	if (set_key) {
		memcpy(script_encryption_key, old_key, 32);
		set_key = false;
		enc_key_str = "";
		enc_key.clear();
	}
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

	da = da->open("res://");
	project_path = p_path;
	PackedStringArray pa = da->get_files_at("res://");
	if (is_print_verbose_enabled()) {
		for (auto s : pa) {
			print_verbose(s);
		}
	}

	Ref<PackInfo> pckinfo;
	pckinfo.instantiate();
	pckinfo->init(
			p_path, Ref<GodotVer>(memnew(GodotVer)), 1, 0, 0, pa.size(), PackInfo::DIR);
	add_pack_info(pckinfo);
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
		}
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
			}
		}
	}
	ERR_FAIL_COND_V_MSG(!is_pack_loaded(), ERR_FILE_CANT_READ, "FATAL ERROR: loaded project pack, but didn't load files from it!");
	if (_cmd_line_extract) {
		// we don't want to load the imports and project config if we're just extracting.
		return OK;
	}

	if (!has_valid_version()) {
		// We need to get the version from the binary resources.
		err = get_version_from_bin_resources();
		// this is a catastrophic failure, unload the pack
		if (err) {
			unload_project();
			ERR_FAIL_V_MSG(err, "FATAL ERROR: Can't determine engine version of project pack!");
		}
	}

	err = detect_bytecode_revision();
	if (err) {
		if (err == ERR_PRINTER_ON_FIRE) {
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

Error GDRESettings::detect_bytecode_revision() {
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
		ERR_FAIL_COND_V_MSG(err, ERR_PRINTER_ON_FIRE, "Cannot determine bytecode revision: Encryption error (Did you set the correct key?)");
		bytecode_files.append_array(encrypted_files);
	}
	if (bytecode_files.is_empty()) {
		return guess_from_version(ERR_PARSE_ERROR);
	}
	auto revision = BytecodeTester::test_files(bytecode_files, ver_major, ver_minor);
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
		} else {
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
		err = current_project->pcfg->load_cfb("res://engine.cfb", get_ver_major(), get_ver_minor());
		ERR_FAIL_COND_V_MSG(err, err, "Failed to load project config!");
	} else if (get_ver_major() == 3 || get_ver_major() == 4) {
		err = current_project->pcfg->load_cfb("res://project.binary", get_ver_major(), get_ver_minor());
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
		current_project->version = packinfo->version;
		current_project->pack_file = packinfo->pack_file;
		current_project->type = packinfo->type;
	} else {
		if (!current_project->version->eq(packinfo->version)) {
			WARN_PRINT("Warning: Pack version mismatch!");
		}
	}
}
// PackedSource doesn't pass back useful error information when loading packs,
// this is a hack so that we can tell if it was an encryption error.
void GDRESettings::_set_error_encryption(bool is_encryption_error) {
	error_encryption = is_encryption_error;
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

Error GDRESettings::set_encryption_key(Vector<uint8_t> key) {
	ERR_FAIL_COND_V_MSG(key.size() < 32, ERR_INVALID_PARAMETER, "Key must be 32 bytes!");
	if (!set_key) {
		memcpy(old_key, script_encryption_key, 32);
	}
	memcpy(script_encryption_key, key.ptr(), 32);
	set_key = true;
	enc_key = key;
	enc_key_str = String::hex_encode_buffer(key.ptr(), 32);
	return OK;
}

Vector<String> GDRESettings::get_file_list(const Vector<String> &filters) {
	Vector<String> ret;
	if (get_pack_type() == PackInfo::DIR) {
		return gdre::get_recursive_dir_list("res://", filters, true);
	}
	Vector<Ref<PackedFileInfo>> flist = get_file_info_list(filters);
	for (int i = 0; i < flist.size(); i++) {
		ret.push_back(flist[i]->path);
	}
	return ret;
}

Array GDRESettings::get_file_info_array(const Vector<String> &filters) {
	Array ret;
	for (auto file_info : GDREPackedData::get_singleton()->get_file_info_list(filters)) {
		ret.push_back(file_info);
	}
	return ret;
}

Vector<Ref<PackedFileInfo>> GDRESettings::get_file_info_list(const Vector<String> &filters) {
	return GDREPackedData::get_singleton()->get_file_info_list(filters);
}
// TODO: Overhaul all these pathing functions
String GDRESettings::localize_path(const String &p_path, const String &resource_dir) const {
	String res_path = resource_dir != "" ? resource_dir : project_path;

	if (res_path == "") {
		//not initialized yet
		if (!p_path.is_absolute_path()) {
			//just tack on a "res://" here
			return "res://" + p_path;
		}
		return p_path;
	}

	if (p_path.begins_with("res://") || p_path.begins_with("user://") ||
			(p_path.is_absolute_path() && !p_path.begins_with(res_path))) {
		return p_path.simplify_path();
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
	if (OS::get_singleton()->get_name().begins_with("Win")) {
		auto reg = RegEx("^[A-Za-z]:\\/");
		if (reg.search(p_path).is_valid()) {
			return true;
		}
		return false;
	}
	// unix
	if (p_path.begins_with("/")) {
		return true;
	}
	return false;
}

// This gets the path necessary to open the file by checking for its existence
// If a pack is loaded, it will try to find it in the pack and fail if it can't
// If not, it will look for it in the file system and fail if it can't
// If it fails to find it, it returns an empty string
String GDRESettings::_get_res_path(const String &p_path, const String &resource_dir, const bool suppress_errors) {
	String res_dir = resource_dir != "" ? resource_dir : project_path;
	String res_path;
	// Try and find it in the packed data
	if (is_pack_loaded() && get_pack_type() != PackInfo::DIR) {
		if (GDREPackedData::get_singleton()->has_path(p_path)) {
			return p_path;
		}
		res_path = localize_path(p_path, res_dir);
		if (res_path != p_path && GDREPackedData::get_singleton()->has_path(res_path)) {
			return res_path;
		}
		// localize_path did nothing
		if (!res_path.is_absolute_path()) {
			res_path = "res://" + res_path;
			if (GDREPackedData::get_singleton()->has_path(res_path)) {
				return res_path;
			}
		}
		// Can't find it
		ERR_FAIL_COND_V_MSG(!suppress_errors, "", "Can't find " + res_path + " in PackedData");
		return "";
	}
	//try and find it on the file system
	res_path = p_path;
	if (res_path.is_absolute_path() && is_fs_path(res_path)) {
		if (!FileAccess::exists(res_path)) {
			ERR_FAIL_COND_V_MSG(!suppress_errors, "", "Resource " + res_path + " does not exist");
			return "";
		}
		return res_path;
	}

	if (res_dir == "") {
		ERR_FAIL_COND_V_MSG(!suppress_errors, "", "Can't find resource without project dir set");
		return "";
	}

	res_path = globalize_path(res_path, res_dir);
	if (!FileAccess::exists(res_path)) {
		ERR_FAIL_COND_V_MSG(!suppress_errors, "", "Resource " + res_path + " does not exist");
		return "";
	}
	return res_path;
}

bool GDRESettings::has_res_path(const String &p_path, const String &resource_dir) {
	return _get_res_path(p_path, resource_dir, true) != "";
}

String GDRESettings::get_res_path(const String &p_path, const String &resource_dir) {
	return _get_res_path(p_path, resource_dir, false);
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
	if (idx != -1) {
		if (dst.is_empty()) {
			return true;
		}
		return idx + 1 == remaps.size() ? false : remaps[idx + 1] == dst;
	}
	return false;
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
		if (!FileAccess::exists(local_src + ".import")) {
			return src;
		}
		for (int i = 0; i < import_files.size(); i++) {
			Ref<ImportInfo> iinfo = import_files[i];
			if (iinfo->get_source_file() == local_src) {
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
			if (idx != -1 && idx + 1 < remaps.size()) {
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

bool GDRESettings::has_file(const String &p_path) {
	return GDREPackedData::get_singleton()->has_path(p_path);
}

Error GDRESettings::load_pack_uid_cache(bool p_reset) {
	if (!is_pack_loaded()) {
		return ERR_UNAVAILABLE;
	}
	String cache_file = "res://.godot/uid_cache.bin";
	//"application/config/use_hidden_project_data_directory"
	if (is_project_config_loaded()) {
		// if this is set, we want to load the cache from the hidden directory
		cache_file = current_project->pcfg->get_setting("application/config/use_hidden_project_data_directory", true) ? cache_file : "res://godot/uid_cache.bin";
	} else {
		// check if res://.godot/uid_cache.bin exists
		if (!FileAccess::exists(cache_file)) {
			cache_file = "res://godot/uid_cache.bin";
		}
	}
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
	}

	uint32_t entry_count = f->get_32();
	for (uint32_t i = 0; i < entry_count; i++) {
		int64_t id = f->get_64();
		int32_t len = f->get_32();
		UID_Cache c;
		c.cs.resize(len + 1);
		ERR_FAIL_COND_V(c.cs.size() != len + 1, ERR_FILE_CORRUPT); // out of memory
		c.cs[len] = 0;
		int32_t rl = f->get_buffer((uint8_t *)c.cs.ptrw(), len);
		ERR_FAIL_COND_V(rl != len, ERR_FILE_CORRUPT);

		c.saved_to_cache = true;
		unique_ids[id] = c;
	}
	for (auto E : unique_ids) {
		if (ResourceUID::get_singleton()->has_id(E.key)) {
			String old_path = ResourceUID::get_singleton()->get_id_path(E.key);
			String new_path = String(E.value.cs);
			if (old_path != new_path) {
				WARN_PRINT("Duplicate ID found in cache: " + itos(E.key) + " -> " + old_path + "\nReplacing with: " + new_path);
			}
			ResourceUID::get_singleton()->set_id(E.key, new_path);
		} else {
			ResourceUID::get_singleton()->add_id(E.key, String(E.value.cs));
		}
	}
	return OK;
}

Error GDRESettings::reset_uid_cache() {
	unique_ids.clear();
	ResourceUID::get_singleton()->clear();
	return ResourceUID::get_singleton()->load_from_cache(true);
}

void GDRESettings::_do_import_load(uint32_t i, IInfoToken *tokens) {
	tokens[i].info = ImportInfo::load_from_file(tokens[i].path, tokens[i].ver_major, tokens[i].ver_minor);
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
	if (_ver_major == 2) {
		List<String> extensions;
		ResourceCompatLoader::get_base_extensions(&extensions, 2);
		Vector<String> v2wildcards;
		for (auto &ext : extensions) {
			v2wildcards.push_back("*." + ext);
		}
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

	auto group_id = WorkerThreadPool::get_singleton()->add_template_group_task(
			this,
			&GDRESettings::_do_import_load,
			tokens.ptrw(),
			tokens.size(), -1, true, SNAME("GDRESettings::load_import_files"));

	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_id);
	for (int i = 0; i < tokens.size(); i++) {
		if (tokens[i].info.is_null()) {
			WARN_PRINT("Can't load import file: " + resource_files[i]);
			continue;
		}
		if (tokens[i].info->get_iitype() == ImportInfo::REMAP) {
			remap_iinfo.insert(tokens[i].path, tokens[i].info);
		}
		import_files.push_back(tokens[i].info);
	}
	return OK;
}

Error GDRESettings::_load_import_file(const String &p_path, bool should_load_md5) {
	Ref<ImportInfo> i_info = ImportInfo::load_from_file(p_path, get_ver_major(), get_ver_minor());
	ERR_FAIL_COND_V_MSG(i_info.is_null(), ERR_FILE_CANT_OPEN, "Failed to load import file " + p_path);

	import_files.push_back(i_info);
	// get source md5 from md5 file
	if (should_load_md5) {
		String src = i_info->get_dest_files()[0];
		// only files under the ".import" or ".godot" paths will have md5 files
		if (src.begins_with("res://.godot") || src.begins_with("res://.import")) {
			// sound.wav-<pathmd5>.smp -> sound.wav-<pathmd5>.md5
			String md5 = src.get_basename() + ".md5";
			while (true) {
				if (GDREPackedData::get_singleton()->has_path(md5)) {
					break;
				}

				// image.png-<pathmd5>.s3tc.stex -> image.png-<pathmd5>.md5
				if (md5 != md5.get_basename()) {
					md5 = md5.get_basename();
					continue;
				}
				// we didn't find it
				md5 = "";
				break;
			}
			if (!md5.is_empty()) {
				Ref<FileAccess> file = FileAccess::open(md5, FileAccess::READ);
				ERR_FAIL_COND_V_MSG(file.is_null(), ERR_PRINTER_ON_FIRE, "Failed to load md5 file associated with import");
				String text = file->get_line();
				while (!text.begins_with("source") && !file->eof_reached()) {
					text = file->get_line();
				}
				if (!text.begins_with("source") || text.split("=").size() < 2) {
					WARN_PRINT("md5 file does not have source md5 info!");
					return ERR_PRINTER_ON_FIRE;
				}
				text = text.split("=")[1].strip_edges().replace("\"", "");
				if (!text.is_valid_hex_number(false)) {
					WARN_PRINT("source md5 hash is not valid!");
					return ERR_PRINTER_ON_FIRE;
				}
				i_info->set_source_md5(text);
			}
		}
	}
	return OK;
}
Error GDRESettings::load_import_file(const String &p_path) {
	return _load_import_file(p_path, false);
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

Ref<ImportInfo> GDRESettings::get_import_info_by_dest(const String &p_path) {
	Ref<ImportInfo> iinfo;
	for (int i = 0; i < import_files.size(); i++) {
		iinfo = import_files[i];
		if (iinfo->get_path() == p_path) {
			return iinfo;
		}
	}
	// not found
	return Ref<ImportInfo>();
}

Vector<String> GDRESettings::get_code_files() {
	return get_file_list({ "*.gdc", "*.gde" });
}

bool GDRESettings::pack_has_project_config() {
	if (!is_pack_loaded()) {
		return false;
	}
	if (get_ver_major() == 2) {
		if (has_res_path("res://engine.cfb")) {
			return true;
		}
	} else if (get_ver_major() == 3 || get_ver_major() == 4) {
		if (has_res_path("res://project.binary")) {
			return true;
		}
	} else {
		if (has_res_path("res://engine.cfb") || has_res_path("res://project.binary")) {
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
			"This software in an alpha stage. Please report any bugs to the GitHub repository\n");
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
	} else if (src_ext == "csv") {
		Ref<FileAccess> f = FileAccess::open(tokens[i].path, FileAccess::READ, &tokens[i].err);
		ERR_FAIL_COND_MSG(f.is_null(), "Failed to open file " + tokens[i].path);
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
		return;
	} else if (src_ext == "json") {
		String jstring = FileAccess::get_file_as_string(tokens[i].path, &tokens[i].err);
		ERR_FAIL_COND_MSG(tokens[i].err, "Failed to open file " + tokens[i].path);
		if (jstring.strip_edges().is_empty()) {
			return;
		}
		Variant var = JSON::parse_string(jstring);
		gdre::get_strings_from_variant(var, tokens[i].strings, tokens[i].engine_version);
		return;
	}
	auto res = ResourceCompatLoader::fake_load(tokens[i].path, "", &tokens[i].err);
	ERR_FAIL_COND_MSG(res.is_null(), "Failed to load resource " + tokens[i].path);
	gdre::get_strings_from_variant(res, tokens[i].strings, tokens[i].engine_version);
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
	for (int i = 0; i < r_files.size(); i++) {
		tokens.write[i].path = r_files[i];
		tokens.write[i].engine_version = get_version_string();
	}
	print_line("Loading resource strings, this may take a while!!");
	auto group_task = WorkerThreadPool::get_singleton()->add_template_group_task(
			this,
			&GDRESettings::_do_string_load,
			tokens.ptrw(),
			tokens.size(), -1, true, SNAME("GDRESettings::load_all_resource_strings"));

	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);
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
	AssetLibInfoGetter::prepop_plugin_cache(plugins, true);
}

bool GDRESettings::get_setting_download_plugins() const {
	return download_plugins;
}

void GDRESettings::set_setting_download_plugins(bool p_val) {
	download_plugins = p_val;
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
	ClassDB::bind_method(D_METHOD("get_version_string"), &GDRESettings::get_version_string);
	ClassDB::bind_method(D_METHOD("get_ver_major"), &GDRESettings::get_ver_major);
	ClassDB::bind_method(D_METHOD("get_ver_minor"), &GDRESettings::get_ver_minor);
	ClassDB::bind_method(D_METHOD("get_ver_rev"), &GDRESettings::get_ver_rev);
	ClassDB::bind_method(D_METHOD("get_file_count"), &GDRESettings::get_file_count);
	ClassDB::bind_method(D_METHOD("globalize_path", "p_path", "resource_path"), &GDRESettings::globalize_path);
	ClassDB::bind_method(D_METHOD("localize_path", "p_path", "resource_path"), &GDRESettings::localize_path);
	ClassDB::bind_method(D_METHOD("set_project_path", "p_path"), &GDRESettings::set_project_path);
	ClassDB::bind_method(D_METHOD("get_project_path"), &GDRESettings::get_project_path);
	ClassDB::bind_method(D_METHOD("get_res_path", "p_path", "resource_dir"), &GDRESettings::get_res_path);
	ClassDB::bind_method(D_METHOD("has_res_path", "p_path", "resource_dir"), &GDRESettings::has_res_path);
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
	ClassDB::bind_method(D_METHOD("has_file", "p_path"), &GDRESettings::has_file);
	ClassDB::bind_method(D_METHOD("load_import_files"), &GDRESettings::load_import_files);
	ClassDB::bind_method(D_METHOD("load_import_file", "p_path"), &GDRESettings::load_import_file);
	ClassDB::bind_method(D_METHOD("get_import_info_by_source", "p_path"), &GDRESettings::get_import_info_by_source);
	ClassDB::bind_method(D_METHOD("get_import_info_by_dest", "p_path"), &GDRESettings::get_import_info_by_dest);
	ClassDB::bind_method(D_METHOD("get_code_files"), &GDRESettings::get_code_files);
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
	ClassDB::bind_method(D_METHOD("get_setting_download_plugins"), &GDRESettings::get_setting_download_plugins);
	ClassDB::bind_method(D_METHOD("set_setting_download_plugins", "p_val"), &GDRESettings::set_setting_download_plugins);
	// ClassDB::bind_method(D_METHOD("get_auto_display_scale"), &GDRESettings::get_auto_display_scale);
	// TODO: route this through GDRE Settings rather than GDRE Editor
	//ADD_SIGNAL(MethodInfo("write_log_message", PropertyInfo(Variant::STRING, "message")));
}

// This is at the bottom to account for the platform header files pulling in their respective OS headers and creating all sorts of issues

#ifdef WINDOWS_ENABLED
#include "platform/windows/os_windows.h"
#endif
#ifdef LINUXBSD_ENABLED
#include "platform/linuxbsd/os_linuxbsd.h"
#endif
#ifdef MACOS_ENABLED
#include "drivers/unix/os_unix.h"
#endif
#ifdef UWP_ENABLED
#include "platform/uwp/os_uwp.h"
#endif
#ifdef WEB_ENABLED
#include "platform/web/os_web.h"
#endif
#if defined(__ANDROID__)
#include "platform/android/os_android.h"
#endif
#ifdef IPHONE_ENABLED
#include "platform/iphone/os_iphone.h"
#endif
// A hack to add another logger to the OS singleton
template <class T>
class GDREOS : public T {
	static_assert(std::is_base_of<OS, T>::value, "T must derive from OS");

public:
	static void do_add_logger(GDREOS<T> *ptr, Logger *p_logger) {
		ptr->add_logger(p_logger);
	}
};

// This adds another logger to the global composite logger so that we can write
// export logs to project directories.
// main.cpp is apparently the only class that can add these, but "add_logger" is
// only protected, so we can cast the singleton to a child class pointer and then call it.
void GDRESettings::add_logger() {
	OS *os_singleton = OS::get_singleton();
	String os_name = os_singleton->get_name();

	if (os_name == "Windows") {
#ifdef WINDOWS_ENABLED
		GDREOS<OS_Windows> *_gdre_os = reinterpret_cast<GDREOS<OS_Windows> *>(os_singleton);
		GDREOS<OS_Windows>::do_add_logger(_gdre_os, logger);
#endif
	}
#ifdef LINUXBSD_ENABLED
	else if (os_name == "Linux" || os_name.find("BSD") == -1) {
		GDREOS<OS_LinuxBSD> *_gdre_os = reinterpret_cast<GDREOS<OS_LinuxBSD> *>(os_singleton);
		GDREOS<OS_LinuxBSD>::do_add_logger(_gdre_os, logger);
	}
#endif
#ifdef MACOS_ENABLED
	else if (os_name == "macOS") {
		GDREOS<OS_Unix> *_gdre_os = reinterpret_cast<GDREOS<OS_Unix> *>(os_singleton);
		GDREOS<OS_Unix>::do_add_logger(_gdre_os, logger);
	}
#endif
#ifdef UWP_ENABLED
	else if (os_name == "UWP") {
		GDREOS<OS_UWP> *_gdre_os = reinterpret_cast<GDREOS<OS_UWP> *>(os_singleton);
		GDREOS<OS_UWP>::do_add_logger(_gdre_os, logger);
	}
#endif
#ifdef WEB_ENABLED
	else if (os_name == "Web") {
		GDREOS<OS_Web> *_gdre_os = reinterpret_cast<GDREOS<OS_Web> *>(os_singleton);
		GDREOS<OS_Web>::do_add_logger(_gdre_os, logger);
	}
#endif
#if defined(__ANDROID__) // the rest of these are probably unnecessary
	else if (os_name == "Android") {
		GDREOS<OS_Android> *_gdre_os = reinterpret_cast<GDREOS<OS_Android> *>(os_singleton);
		GDREOS<OS_Android>::do_add_logger(_gdre_os, logger);
	}
#endif
#ifdef IPHONE_ENABLED
	else if (os_name == "iOS") {
		GDREOS<OSIPhone> *_gdre_os = reinterpret_cast<GDREOS<OSIPhone> *>(os_singleton);
		GDREOS<OSIPhone>::do_add_logger(_gdre_os, logger);
	}
#endif
	else {
		WARN_PRINT("No logger being set, there will be no logs!");
	}
}