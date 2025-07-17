#pragma once

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "utility/packed_file_info.h"

class DirSource : public PackSource {
	Vector<String> packs;
	static DirSource *singleton;

public:
	static DirSource *get_singleton();
	virtual bool try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset) override;
	virtual Ref<FileAccess> get_file(const String &p_path, PackedData::PackedFile *p_file) override;
	bool file_exists(const String &p_path) const;
	String get_pack_path(const String &p_path) const;
	void reset();
	DirSource();
	~DirSource();
};

class GDREPackedData {
	friend class FileAccessPack;
	friend class DirAccessGDRE;
	friend class PackSource;

public:
	struct PackedDir {
		PackedDir *parent = nullptr;
		String name;
		HashMap<String, PackedDir *> subdirs;
		HashSet<String> files;
	};

	struct PathMD5 {
		uint64_t a = 0;
		uint64_t b = 0;

		bool operator==(const PathMD5 &p_val) const {
			return (a == p_val.a) && (b == p_val.b);
		}
		static uint32_t hash(const PathMD5 &p_val) {
			uint32_t h = hash_murmur3_one_32(p_val.a);
			return hash_fmix32(hash_murmur3_one_32(p_val.b, h));
		}

		PathMD5() {}

		explicit PathMD5(const Vector<uint8_t> &p_buf) {
			a = *((uint64_t *)&p_buf[0]);
			b = *((uint64_t *)&p_buf[8]);
		}
	};

private:
	HashMap<PathMD5, PackedData::PackedFile, PathMD5> files;
	HashMap<String, Ref<PackedFileInfo>> file_map;

	Vector<PackSource *> sources;

	PackedDir *root = nullptr;

	DirSource dir_source;

	static GDREPackedData *singleton;
	bool disabled = false;
	bool packed_data_was_enabled = false;
	String old_dir_access_class;
	bool set_file_access_defaults = false;

	void _free_packed_dirs(PackedDir *p_dir);
	void _get_file_paths(PackedDir *p_dir, const String &p_parent_dir, HashSet<String> &r_paths) const;

	void _clear();

public:
	void set_default_file_access();
	void reset_default_file_access();
	void add_pack_source(PackSource *p_source);
	void add_path(const String &p_pkg_path, const String &p_path, uint64_t p_ofs, uint64_t p_size, const uint8_t *p_md5, PackSource *p_src, bool p_replace_files, bool p_encrypted = false, bool p_bundle = false); // for PackSource
	void remove_path(const String &p_path);
	uint8_t *get_file_hash(const String &p_path);
	HashSet<String> get_file_paths() const;

	void set_disabled(bool p_disabled);
	_FORCE_INLINE_ bool is_disabled() const;

	static GDREPackedData *get_singleton();
	Error add_pack(const String &p_path, bool p_replace_files, uint64_t p_offset);
	Error add_dir(const String &p_path, bool p_replace_files = false);

	void clear();

	_FORCE_INLINE_ Ref<FileAccess> try_open_path(const String &p_path);
	bool has_path(const String &p_path);

	_FORCE_INLINE_ Ref<DirAccess> try_open_directory(const String &p_path);
	_FORCE_INLINE_ bool has_directory(const String &p_path);

	Vector<Ref<PackedFileInfo>> get_file_info_list(const Vector<String> &filters = Vector<String>());
	static bool real_packed_data_has_pack_loaded();
	bool has_loaded_packs();
	String fix_res_path(const String &p_path);
	int64_t get_file_size(const String &p_path);
	static String get_current_file_access_class(FileAccess::AccessType p_access_type);
	static String get_current_dir_access_class(DirAccess::AccessType p_access_type);
	static String get_os_file_access_class_name();
	static String get_os_dir_access_class_name();
	GDREPackedData();
	~GDREPackedData();
};

class FileAccessGDRE : public FileAccess {
	GDCLASS(FileAccessGDRE, FileAccess);
	friend class GDREPackedData;
	Ref<FileAccess> proxy;
	AccessType access_type;

	typedef Ref<FileAccess> (*CreateFunc)();

	virtual uint64_t _get_modified_time(const String &p_file) override;
	virtual BitField<FileAccess::UnixPermissionFlags> _get_unix_permissions(const String &p_file) override;
	virtual Error _set_unix_permissions(const String &p_file, BitField<FileAccess::UnixPermissionFlags> p_permissions) override;
	virtual Error _set_hidden_attribute(const String &p_file, bool p_hidden) override;
	virtual bool _get_read_only_attribute(const String &p_file) override;
	virtual Error _set_read_only_attribute(const String &p_file, bool p_ro) override;
	virtual bool _get_hidden_attribute(const String &p_file) override;

	static Ref<FileAccess> _open_filesystem(const String &p_path, int p_mode_flags, Error *r_error);

protected:
	virtual void _set_access_type(AccessType p_access) override;

public:
	virtual Error open_internal(const String &p_path, int p_mode_flags) override; ///< open a file
	virtual bool is_open() const override; ///< true when file is open

	virtual void seek(uint64_t p_position) override; ///< seek to a given position
	virtual void seek_end(int64_t p_position = 0) override; ///< seek from the end of file
	virtual uint64_t get_position() const override; ///< get position in the file
	virtual uint64_t get_length() const override; ///< get size of the file

	virtual bool eof_reached() const override; ///< reading passed EOF

	virtual uint8_t get_8() const override; ///< get a byte
	virtual uint64_t get_buffer(uint8_t *p_dst, uint64_t p_length) const override;
	virtual uint64_t _get_access_time(const String &p_file) override;
	virtual int64_t _get_size(const String &p_file) override;

	virtual Error get_error() const override; ///< get last error
	virtual String fix_path(const String &p_path) const override; ///< fix a path, i.e. make it absolute and in the OS format
	virtual Error resize(int64_t p_length) override;
	virtual void flush() override;
	virtual bool store_8(uint8_t p_dest) override; ///< store a byte
	virtual bool store_buffer(const uint8_t *p_src, uint64_t p_length) override; ///< store an array of bytes

	virtual bool file_exists(const String &p_name) override; ///< return true if a file exists

	virtual void close() override;
};

class DirAccessGDRE : public DirAccess {
	GDSOFTCLASS(DirAccessGDRE, DirAccess);
	GDREPackedData::PackedDir *current;

	List<String> list_dirs;
	List<String> list_files;
	bool cdir = false;

	GDREPackedData::PackedDir *_find_dir(String p_dir);

	Ref<DirAccess> proxy;

protected:
	virtual bool is_hidden(const String &p_name) { return false; }
	Ref<DirAccess> _open_filesystem();

public:
	virtual String fix_path(const String &p_path) const override;
	virtual Error list_dir_begin() override; ///< This starts dir listing
	virtual String get_next() override;
	virtual bool current_is_dir() const override;
	virtual bool current_is_hidden() const override;

	virtual void list_dir_end() override; ///<

	virtual int get_drive_count() override;
	virtual String get_drive(int p_drive) override;
	virtual int get_current_drive() override;
	virtual bool drives_are_shortcuts() override;

	virtual Error change_dir(String p_dir) override; ///< can be relative or absolute, return false on success
	virtual String get_current_dir(bool p_include_drive = true) const override; ///< return current dir location
	virtual Error make_dir(String p_dir) override;

	virtual bool file_exists(String p_file) override;
	virtual bool dir_exists(String p_dir) override;
	virtual bool is_readable(String p_dir) override;
	virtual bool is_writable(String p_dir) override;

	virtual uint64_t get_modified_time(String p_file);

	virtual Error rename(String p_path, String p_new_path) override;
	virtual Error remove(String p_path) override;

	virtual bool is_link(String p_file) override;
	virtual String read_link(String p_file) override;
	virtual Error create_link(String p_source, String p_target) override;

	virtual uint64_t get_space_left() override;

	virtual String get_filesystem_type() const override;

	DirAccessGDRE();
	~DirAccessGDRE();
};

class PathFinder {
public:
	static bool real_packed_data_has_path(const String &p_path, bool check_disabled = false);
	static bool gdre_packed_data_valid_path(const String &p_path);
	static String _fix_path_file_access(const String &p_path, int p_mode_flags = 0);
};

template <class T>
class FileAccessProxy : public T {
	static_assert(std::is_base_of<FileAccess, T>::value, "T must derive from FileAccess");

	int mode_flags;

public:
	virtual String fix_path(const String &p_path) const override {
		return PathFinder::_fix_path_file_access(p_path.replace("\\", "/"), mode_flags);
	}
	virtual Error open_internal(const String &p_path, int p_mode_flags) override {
		mode_flags = p_mode_flags;
		return T::open_internal(p_path, p_mode_flags);
	}
};

template <class T>
class DirAccessProxy : public T {
	static_assert(std::is_base_of<DirAccess, T>::value, "T must derive from DirAccess");

public:
	virtual String fix_path(const String &p_path) const override {
		return PathFinder::_fix_path_file_access(p_path);
	}
};
