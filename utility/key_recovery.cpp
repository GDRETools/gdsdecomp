#include "key_recovery.h"

#include "core/crypto/crypto_core.h"
#include "core/io/file_access_pack.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"
#include "core/templates/safe_refcount.h"
#include "utility/gdre_packed_source.h"
#include "utility/gdre_settings.h"

#include <cstring>

namespace {

constexpr int KEY_SIZE = 32;
constexpr int IV_SIZE = 16;
constexpr uint64_t CHUNK_SIZE = 4 * 1024 * 1024;
constexpr uint64_t MAX_INDEX_SIZE = 64 * 1024 * 1024;

// The decrypted index starts with the length of the first path, followed by the path itself.
// Exported packs normally use "res://" paths, but printable relative paths are accepted too.
// This is only a cheap filter: candidates that pass it are verified against the index MD5.
bool looks_like_index_start(const uint8_t *p_plain, int p_len) {
	if (p_len < 10) {
		return false;
	}
	uint32_t path_length = (uint32_t)p_plain[0] | ((uint32_t)p_plain[1] << 8) | ((uint32_t)p_plain[2] << 16) | ((uint32_t)p_plain[3] << 24);
	if (path_length < 6 || path_length > 4096) {
		return false;
	}
	int visible = MIN((int)path_length, p_len - 4);
	const uint8_t *path = p_plain + 4;
	bool res_path = visible >= 6 && memcmp(path, "res://", 6) == 0;
	for (int i = 0; i < visible; i++) {
		if (res_path) {
			if (i >= 6 && path[i] < 0x20) {
				return false;
			}
		} else if (path[i] < 0x20 || path[i] > 0x7E) {
			return false;
		}
	}
	return true;
}

struct KeyScanJob {
	String path;
	Vector<uint8_t> prefix;
	Vector<uint8_t> cipher;
	uint8_t iv[IV_SIZE] = {};
	uint8_t md5[16] = {};
	uint64_t table_size = 0;
	uint64_t file_size = 0;
	uint64_t chunk_size = CHUNK_SIZE;
	int chunk_count = 1;

	SafeFlag found;
	Mutex result_mutex;
	Vector<uint8_t> result;

	void scan_chunk(int p_chunk, KeyScanJob *p_self);
};

void KeyScanJob::scan_chunk(int p_chunk, KeyScanJob *p_self) {
	(void)p_self;
	if (found.is_set()) {
		return;
	}
	uint64_t start = (uint64_t)p_chunk * chunk_size;
	uint64_t end = MIN(start + chunk_size, file_size);
	if (start >= end) {
		return;
	}

	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	ERR_FAIL_COND(f.is_null());
	f->seek(start);

	uint64_t to_read = MIN(end + KEY_SIZE - 1, file_size) - start;
	Vector<uint8_t> buffer;
	buffer.resize((int)to_read);
	uint64_t got = f->get_buffer(buffer.ptrw(), to_read);
	if (got < KEY_SIZE) {
		return;
	}

	uint8_t key[KEY_SIZE];
	uint8_t plain[IV_SIZE];
	uint8_t iv[IV_SIZE];
	Vector<uint8_t> table;
	CryptoCore::AESContext ctx;

	for (uint64_t i = 0; i + KEY_SIZE <= got; i++) {
		if ((i & 0x3FFF) == 0 && found.is_set()) {
			return;
		}
		memcpy(key, buffer.ptr() + i, KEY_SIZE);
		memcpy(iv, this->iv, IV_SIZE);
		ctx.set_encode_key(key, 256);
		ctx.decrypt_cfb(IV_SIZE, iv, prefix.ptr(), plain);
		if (!looks_like_index_start(plain, IV_SIZE)) {
			continue;
		}

		table.resize(cipher.size());
		memcpy(iv, this->iv, IV_SIZE);
		ctx.decrypt_cfb(cipher.size(), iv, cipher.ptr(), table.ptrw());
		uint8_t hash[16];
		if (CryptoCore::md5(table.ptr(), table_size, hash) != OK) {
			continue;
		}
		if (memcmp(hash, md5, 16) != 0) {
			continue;
		}

		{
			MutexLock lock(result_mutex);
			if (result.is_empty()) {
				result.resize(KEY_SIZE);
				memcpy(result.ptrw(), key, KEY_SIZE);
			}
		}
		found.set();
		return;
	}
}

} // namespace

Vector<uint8_t> GDREKeyRecovery::recover_key(const String &p_path) {
	if (GDRESettings::get_singleton() && GDRESettings::get_singleton()->get_custom_decryptor().is_valid()) {
		return Vector<uint8_t>();
	}

	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), Vector<uint8_t>(), "Cannot open " + p_path);

	uint64_t file_size = f->get_length();
	uint64_t pck_size = file_size;
	uint32_t magic = f->get_32();
	if (magic != PACK_HEADER_MAGIC) {
		if (!GDREPackedSource::seek_offset_from_exe(f, p_path, pck_size)) {
			return Vector<uint8_t>();
		}
	}
	int64_t pck_start = (int64_t)f->get_position() - 4;
	uint64_t pck_end = (uint64_t)pck_start + pck_size;

	uint32_t version = f->get_32();
	f->get_32(); // Engine major.
	f->get_32(); // Engine minor.
	f->get_32(); // Engine patch.
	if (version > GDREPackedSource::CURRENT_PACK_FORMAT_VERSION) {
		return Vector<uint8_t>();
	}

	uint32_t pack_flags = 0;
	if (version >= PACK_FORMAT_VERSION_V2) {
		pack_flags = f->get_32();
		f->get_64(); // File base.
	}
	if (!(pack_flags & PACK_DIR_ENCRYPTED)) {
		return Vector<uint8_t>();
	}
	if (pack_flags & PACK_SPARSE_BUNDLE) {
		// Sparse bundles use a different, salt based encryption scheme.
		return Vector<uint8_t>();
	}

	if (version == PACK_FORMAT_VERSION_V3 || version == PACK_FORMAT_VERSION_V4) {
		uint64_t dir_offset = f->get_64() + pck_start;
		if (dir_offset == 0 || dir_offset >= pck_end) {
			return Vector<uint8_t>();
		}
		f->seek(dir_offset);
	} else {
		for (int i = 0; i < 16; i++) {
			f->get_32(); // Reserved.
		}
	}

	f->get_32(); // File count.

	uint8_t md5[16];
	f->get_buffer(md5, 16);
	uint64_t table_size = f->get_64();
	uint8_t iv[IV_SIZE];
	f->get_buffer(iv, IV_SIZE);
	uint64_t cipher_pos = f->get_position();

	if (table_size == 0 || table_size > MAX_INDEX_SIZE || file_size < KEY_SIZE) {
		return Vector<uint8_t>();
	}
	uint64_t cipher_size = (table_size + 15) & ~(uint64_t)15;
	if (cipher_pos + cipher_size > pck_end) {
		return Vector<uint8_t>();
	}

	Vector<uint8_t> cipher;
	cipher.resize((int)cipher_size);
	f->seek(cipher_pos);
	if (f->get_buffer(cipher.ptrw(), cipher_size) != cipher_size) {
		return Vector<uint8_t>();
	}

	KeyScanJob job;
	job.path = p_path;
	job.prefix.resize(MIN(KEY_SIZE, (int)cipher.size()));
	memcpy(job.prefix.ptrw(), cipher.ptr(), job.prefix.size());
	job.cipher = cipher;
	memcpy(job.iv, iv, IV_SIZE);
	memcpy(job.md5, md5, 16);
	job.table_size = table_size;
	job.file_size = file_size;

	int threads = MAX(1, WorkerThreadPool::get_singleton()->get_thread_count());
	int64_t chunk_count = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
	chunk_count = CLAMP(chunk_count, (int64_t)1, (int64_t)threads * 4);
	job.chunk_count = (int)chunk_count;
	job.chunk_size = (file_size + job.chunk_count - 1) / job.chunk_count;

	auto group_id = WorkerThreadPool::get_singleton()->add_template_group_task(&job, &KeyScanJob::scan_chunk, &job, job.chunk_count, MIN(job.chunk_count, threads), false);
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_id);

	return job.result;
}
