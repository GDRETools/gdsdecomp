#ifndef GDRE_IMPORT_INFO_H
#define GDRE_IMPORT_INFO_H

#include "compat/resource_import_metadatav2.h"

#include "core/io/config_file.h"
#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/os/shared_object.h"
#include "utility/resource_info.h"
namespace V2ImportEnums {
enum TextureFormat {
	IMAGE_FORMAT_UNCOMPRESSED,
	IMAGE_FORMAT_COMPRESS_DISK_LOSSLESS,
	IMAGE_FORMAT_COMPRESS_DISK_LOSSY,
	IMAGE_FORMAT_COMPRESS_RAM,
};
enum FontMode {
	FONT_BITMAP,
	FONT_DISTANCE_FIELD
};
enum Storage {
	STORAGE_RAW,
	STORAGE_COMPRESS_LOSSY,
	STORAGE_COMPRESS_LOSSLESS
};

enum TextureMode {
	MODE_TEXTURE_2D,
	MODE_TEXTURE_3D,
	MODE_ATLAS,
	MODE_LARGE,
	MODE_MAX
};
enum SampleCompressMode {
	COMPRESS_MODE_DISABLED,
	COMPRESS_MODE_RAM,
	COMPRESS_MODE_DISK
};

enum SceneFlags {
	IMPORT_SCENE = 1,
	IMPORT_ANIMATION = 2,
	IMPORT_ANIMATION_DETECT_LOOP = 4,
	IMPORT_ANIMATION_OPTIMIZE = 8,
	IMPORT_ANIMATION_FORCE_ALL_TRACKS_IN_ALL_CLIPS = 16,
	IMPORT_ANIMATION_KEEP_VALUE_TRACKS = 32,
	IMPORT_GENERATE_TANGENT_ARRAYS = 256,
	IMPORT_FAIL_ON_MISSING_DEPENDENCIES = 512
};
} // namespace V2ImportEnums

namespace V3LTexCompressMode {
enum CompressMode {
	COMPRESS_LOSSLESS,
	COMPRESS_VIDEO_RAM,
	COMPRESS_UNCOMPRESSED
};
}
namespace V3TexCompressMode {
enum CompressMode {
	COMPRESS_LOSSLESS,
	COMPRESS_LOSSY,
	COMPRESS_VIDEO_RAM,
	COMPRESS_UNCOMPRESSED
};
} //namespace V3TexCompressMode

class ImportInfo : public RefCounted {
	GDCLASS(ImportInfo, RefCounted)

public:
	enum IInfoType {
		BASE,
		V2,
		MODERN,
		DUMMY,
		REMAP,
		GDEXT
	};

protected:
	String import_md_path; // path to the ".import" file
	int ver_major = 0; //2, 3, 4
	int ver_minor = 0;
	int format_ver = 0;
	bool not_an_import = false;
	bool auto_converted_export = false;
	bool dirty = false;
	String preferred_import_path;
	String export_dest;
	String export_lossless_copy;
	IInfoType iitype;
	void _init();
	virtual Error _load(const String &p_path) = 0;

public:
	enum LossType {
		UNKNOWN = -1,
		LOSSLESS = 0,
		STORED_LOSSY = 1,
		IMPORTED_LOSSY = 2,
		STORED_AND_IMPORTED_LOSSY = 3,
	};
	IInfoType get_iitype() const { return iitype; }
	int get_ver_major() const { return ver_major; }
	int get_ver_minor() const { return ver_minor; }
	bool is_auto_converted() const { return auto_converted_export; }
	bool is_import() const { return !not_an_import; }
	bool is_dirty() const { return dirty; }

	String get_import_md_path() const { return import_md_path; }
	void set_import_md_path(const String &p_path) { import_md_path = p_path; }

	String get_export_dest() const { return export_dest; }
	void set_export_dest(const String &p_path) { export_dest = p_path; }

	String get_export_lossless_copy() const { return export_lossless_copy; }
	void set_export_lossless_copy(const String &p_path) { export_lossless_copy = p_path; }

	// Gets the path of the preferred resource that we want to export from
	String get_path() const { return preferred_import_path; }
	// Sets the path of the preferred resource that we want to export from
	void set_preferred_resource_path(const String &p_path) { preferred_import_path = p_path; }
	// Gets the Godot resource type (e.g. "StreamTexture")
	virtual String get_type() const = 0;
	virtual void set_type(const String &p_type) = 0;

	virtual String get_importer() const = 0;

	virtual String get_compat_type() const = 0;
	// Gets the original file that the godot resource was imported from (e.g. res://icon.png)
	virtual String get_source_file() const = 0;
	virtual void set_source_file(const String &path) = 0;

	virtual String get_source_md5() const = 0;
	virtual void set_source_md5(const String &md5) = 0;

	virtual void set_source_and_md5(const String &path, const String &md5 = "") = 0;

	virtual String get_uid() const { return String(); }
	// v2 only, Atlas Textures had more than one source
	virtual Vector<String> get_additional_sources() const { return Vector<String>(); }
	virtual void set_additional_sources(const Vector<String> &p_add_sources) { return; }

	// Gets the godot resources that were created from from this import (e.g. res://icon.<md5>.stex)
	virtual Vector<String> get_dest_files() const = 0;
	virtual void set_dest_files(const Vector<String> p_dest_files) = 0;
	virtual bool has_dest_file(const String &p_dest_file) const { return get_dest_files().has(p_dest_file); }

	// v3-v4 only. Gets the metadata prop in the "remap" section of import file
	virtual Dictionary get_metadata_prop() const { return Dictionary(); }
	virtual void set_metadata_prop(Dictionary r_dict) { return; }

	virtual Variant get_param(const String &p_key) const = 0;
	virtual void set_param(const String &p_key, const Variant &p_val) = 0;
	virtual bool has_param(const String &p_key) const = 0;

	virtual Variant get_iinfo_val(const String &p_section, const String &p_prop) const = 0;
	virtual void set_iinfo_val(const String &p_section, const String &p_prop, const Variant &p_val) = 0;

	// gets the parameters used to import the resource.
	virtual Dictionary get_params() const = 0;
	virtual void set_params(Dictionary params) = 0;

	virtual Error reload() = 0;
	virtual String to_string() override;

	String as_text(bool full = true);

	virtual int get_import_loss_type() const;

	virtual Error save_to(const String &p_path) = 0;
	static Error get_resource_info(const String &p_path, ResourceInfo &i_info);
	static Ref<ImportInfo> copy(const Ref<ImportInfo> &p_iinfo);
	static Ref<ImportInfo> load_from_file(const String &p_path, int ver_major = 0, int ver_minor = 0);
	ImportInfo();

protected:
	static void _bind_methods();
};
class ImportInfoModern : public ImportInfo {
	GDCLASS(ImportInfoModern, ImportInfo)
private:
	friend class ImportInfo;

	String src_md5;
	Ref<ConfigFile> cf; // raw v3-v4 import data

	virtual Error _load(const String &p_path) override;

protected:
	static void _bind_methods();

public:
	// Gets the Godot resource type (e.g. "StreamTexture")
	virtual String get_type() const override;
	virtual void set_type(const String &p_type) override;
	virtual String get_compat_type() const override;

	virtual String get_importer() const override;

	// Gets the original file that the godot resource was imported from (e.g. res://icon.png)
	virtual String get_source_file() const override;
	virtual void set_source_file(const String &path) override;

	virtual String get_source_md5() const override;
	virtual void set_source_md5(const String &md5) override;

	virtual void set_source_and_md5(const String &path, const String &md5 = "") override;

	virtual String get_uid() const override;

	// Gets the godot resources that were created from from this import (e.g. res://icon.<md5>.stex)
	virtual Vector<String> get_dest_files() const override;
	virtual void set_dest_files(const Vector<String> p_dest_files) override;

	// v3-v4 only. Gets the metadata prop in the "remap" section of import file
	virtual Dictionary get_metadata_prop() const override;
	virtual void set_metadata_prop(Dictionary r_dict) override;

	virtual Variant get_param(const String &p_key) const override;
	virtual void set_param(const String &p_key, const Variant &p_val) override;
	virtual bool has_param(const String &p_key) const override;

	virtual Variant get_iinfo_val(const String &p_section, const String &p_prop) const override;
	virtual void set_iinfo_val(const String &p_section, const String &p_prop, const Variant &p_val) override;

	// gets the parameters used to import the resource.
	virtual Dictionary get_params() const override;
	virtual void set_params(Dictionary params) override;

	virtual Error save_to(const String &p_path) override;
	Error save_md5_file(const String &output_dir);

	virtual Error reload() override { return _load(import_md_path); }
	ImportInfoModern();
};

class ImportInfov2 : public ImportInfo {
	GDCLASS(ImportInfov2, ImportInfo)
private:
	friend class ImportInfo;

	String type;
	Vector<String> dest_files;
	Ref<ResourceImportMetadatav2> v2metadata;
	virtual Error _load(const String &p_path) override;

protected:
	static void _bind_methods();

public:
	// Gets the Godot resource type (e.g. "StreamTexture")
	virtual String get_type() const override;
	virtual void set_type(const String &p_type) override;
	virtual String get_compat_type() const override;

	virtual String get_importer() const override;

	// Gets the original file that the godot resource was imported from (e.g. res://icon.png)
	virtual String get_source_file() const override;
	virtual void set_source_file(const String &path) override;

	virtual String get_source_md5() const override;
	virtual void set_source_md5(const String &md5) override;

	virtual void set_source_and_md5(const String &path, const String &md5 = "") override;
	// v2 only, Atlas Textures had more than one source
	virtual Vector<String> get_additional_sources() const override;
	virtual void set_additional_sources(const Vector<String> &p_add_sources) override;

	// Gets the godot resources that were created from from this import (e.g. res://icon.<md5>.stex)
	virtual Vector<String> get_dest_files() const override;
	virtual void set_dest_files(const Vector<String> p_dest_files) override;

	virtual Variant get_param(const String &p_key) const override;
	virtual void set_param(const String &p_key, const Variant &p_val) override;
	virtual bool has_param(const String &p_key) const override;

	virtual Variant get_iinfo_val(const String &p_section, const String &p_prop) const override;
	virtual void set_iinfo_val(const String &p_section, const String &p_prop, const Variant &p_val) override;

	// gets the parameters used to import the resource.
	virtual Dictionary get_params() const override;
	virtual void set_params(Dictionary params) override;

	virtual Error save_to(const String &p_path) override;

	virtual Error reload() override { return _load(import_md_path); }
	ImportInfov2();
};

class ImportInfoDummy : public ImportInfo {
	GDCLASS(ImportInfoDummy, ImportInfo)
private:
	friend class ImportInfo;

	virtual Error _load(const String &p_path) override;

protected:
	String type;
	String source_file;
	String src_md5;
	Vector<String> dest_files;
	static void _bind_methods() {}

public:
	virtual String get_type() const override { return type; }
	virtual void set_type(const String &p_type) override { type = p_type; }
	virtual String get_compat_type() const override { return ClassDB::get_compatibility_remapped_class(get_type()); }

	virtual String get_importer() const override { return "<NONE>"; }

	virtual String get_source_file() const override { return source_file; }
	virtual void set_source_file(const String &path) override { source_file = path; }

	virtual String get_source_md5() const override { return src_md5; }
	virtual void set_source_md5(const String &md5) override { src_md5 = md5; }

	virtual void set_source_and_md5(const String &path, const String &md5 = "") override {
		source_file = path;
		src_md5 = md5;
	}
	virtual Vector<String> get_additional_sources() const override { return Vector<String>(); }
	virtual void set_additional_sources(const Vector<String> &p_add_sources) override { return; }

	virtual Vector<String> get_dest_files() const override { return dest_files; }
	virtual void set_dest_files(const Vector<String> p_dest_files) override { dest_files = p_dest_files; }

	virtual Variant get_param(const String &p_key) const override { return Variant(); }
	virtual void set_param(const String &p_key, const Variant &p_val) override { return; }
	virtual bool has_param(const String &p_key) const override { return false; }

	virtual Variant get_iinfo_val(const String &p_section, const String &p_prop) const override { return Variant(); }
	virtual void set_iinfo_val(const String &p_section, const String &p_prop, const Variant &p_val) override { return; }

	virtual Dictionary get_params() const override { return Dictionary(); }
	virtual void set_params(Dictionary params) override { return; }

	virtual Error save_to(const String &p_path) override { return ERR_UNAVAILABLE; }

	virtual Error reload() override { return _load(import_md_path); }
	ImportInfoDummy();
};

class ImportInfoRemap : public ImportInfoDummy {
	GDCLASS(ImportInfoRemap, ImportInfoDummy)
private:
	friend class ImportInfo;
	String importer = "<NONE>";

	virtual Error _load(const String &p_path) override;

public:
	virtual String get_importer() const override { return importer; }
	ImportInfoRemap();
};

class ImportInfoGDExt : public ImportInfoDummy {
	GDCLASS(ImportInfoGDExt, ImportInfoDummy)
private:
	friend class ImportInfo;
	String importer = "gdextension";
	Ref<ConfigFile> cf; // GDExtension/GDNative file
	Error _load_after_cf(const String &p_path);
	virtual Error _load(const String &p_path) override;
	String correct_path(const String &p_path) const;
	HashMap<String, String> get_libaries_section() const;

public:
	Error load_from_string(const String &p_fakepath, const String &p_string);
	virtual Variant get_iinfo_val(const String &p_section, const String &p_prop) const override;
	virtual void set_iinfo_val(const String &p_section, const String &p_prop, const Variant &p_val) override;

	virtual String get_importer() const override { return importer; }

	Vector<SharedObject> get_libaries(bool fix_rel_paths = true) const;
	Vector<SharedObject> get_dependencies(bool fix_rel_paths = true) const;

	String get_compatibility_minimum() const;
	String get_compatibility_maximum() const;

	ImportInfoGDExt();
};

VARIANT_ENUM_CAST(ImportInfo::LossType);
VARIANT_ENUM_CAST(ImportInfo::IInfoType);
#endif