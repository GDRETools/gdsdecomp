#ifndef PCK_CREATOR_H
#define PCK_CREATOR_H

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"

#include "utility/gdre_progress.h"

#include "gd_parallel_hashmap.h"
#include "packed_file_info.h"

#include <core/variant/typed_dictionary.h>

class PckCreator : public RefCounted {
	GDCLASS(PckCreator, RefCounted)

	bool opt_multi_thread = true;
	String pck_path;
	int version = 2;
	int ver_major = 4;
	int ver_minor = 4;
	int ver_rev = 0;
	bool encrypt = false;
	bool embed = false;
	String exe_to_embed;
	String watermark;
	struct File {
		String path;
		String src_path;
		uint64_t ofs = 0;
		uint64_t size = 0;
		bool encrypted = false;
		bool removal = false;
		Vector<uint8_t> md5;
		Error err = OK;
	};

	Vector<File> files_to_pck;
	uint64_t offset;
	std::atomic<bool> cancelled = false;
	String error_string;
	std::atomic<int64_t> last_completed = 0;
	std::atomic<int64_t> skipped_cnt = 0;
	std::atomic<int64_t> broken_cnt = 0;
	std::atomic<int64_t> data_read = 0;
	Vector<String> tmp_files;
	static constexpr size_t piecemeal_read_size = 65536; //1 * 1024 * 1024;
	static constexpr size_t _file_is_large = 100 * 1024 * 1024;
	static constexpr bool is_file_large(size_t size) { return size > _file_is_large; }

	bool _pck_file_check_md5(Ref<PackedFileInfo> &file);
	void _do_md5_check(uint32_t i, Ref<PackedFileInfo> *tokens);
	void reset();
	struct ProcessFolderToken {
		String path;
		size_t file_size = 0;
		uint8_t md5[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		Error err = OK;
	};
	void _do_process_folder(uint32_t i, File *tokens);

	inline Error read_and_write_file(size_t i, Ref<FileAccess> write_handle);
	Error wait_for_task(WorkerThreadPool::GroupID group_task, const Vector<String> &paths_to_check, EditorProgressGDDC *pr);
	Error headless_pck_create(const String &pck_path, const String &dir, const Vector<String> &include_filters, const Vector<String> &exclude_filters);
	Error non_headless_pck_create(const String &pck_path, const String &dir, const Vector<String> &include_filters, const Vector<String> &exclude_filters);

protected:
	static void _bind_methods();
	String get_file_description(int64_t i, File *userdata);

public:
	static Vector<String> get_files_to_pack(const String &p_dir, const Vector<String> &include_filters, const Vector<String> &exclude_filters);
	Error _process_folder(const String &p_pck_path, const String &p_dir, const Vector<String> &file_paths_to_pack, EditorProgressGDDC *pr, String &error_string);
	void start_pck(const String &p_pck_path, int pck_version, int ver_major, int ver_minor, int ver_rev, bool encrypt = false, bool embed = false, const String &exe_to_embed = "", const String &watermark = "");
	Error add_files(Dictionary file_paths_to_pack);
	Error _add_files(const HashMap<String, String> &file_paths_to_pack, EditorProgressGDDC *pr);
	Error finish_pck();
	Error pck_create(const String &p_pck_path, const String &p_dir, const Vector<String> &include_filters, const Vector<String> &exclude_filters);
	Error _create_after_process(EditorProgressGDDC *pr, String &error_string);
	void set_multi_thread(bool multi_thread) { opt_multi_thread = multi_thread; }
	bool get_multi_thread() const { return opt_multi_thread; }
	void set_pack_version(int ver) { version = ver; }
	int get_pack_version() const { return version; }
	void set_ver_major(int ver) { ver_major = ver; }
	int get_ver_major() const { return ver_major; }
	void set_ver_minor(int ver) { ver_minor = ver; }
	int get_ver_minor() const { return ver_minor; }
	void set_ver_rev(int ver) { ver_rev = ver; }
	int get_ver_rev() const { return ver_rev; }
	void set_encrypt(bool enc) { encrypt = enc; }
	bool get_encrypt() const { return encrypt; }
	void set_embed(bool emb) { embed = emb; }
	bool get_embed() const { return embed; }
	void set_exe_to_embed(const String &exe) { exe_to_embed = exe; }
	String get_exe_to_embed() const { return exe_to_embed; }
	void set_watermark(const String &wm) { watermark = wm; }
	String get_watermark() const { return watermark; }
	String get_error_message() const { return error_string; }
};

#endif // PCK_CREATOR_H
