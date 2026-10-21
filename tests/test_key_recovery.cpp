#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/os/os.h"
#include "tests/test_macros.h"
#include "utility/key_recovery.h"

#include <cstring>

TEST_FORCE_LINK(test_key_recovery)

namespace {

constexpr int TEST_KEY_SIZE = 32;
constexpr int TEST_IV_SIZE = 16;

void append_u32(Vector<uint8_t> &r_buf, uint32_t p_value) {
	for (int i = 0; i < 4; i++) {
		r_buf.push_back((uint8_t)((p_value >> (i * 8)) & 0xFF));
	}
}

void append_u64(Vector<uint8_t> &r_buf, uint64_t p_value) {
	for (int i = 0; i < 8; i++) {
		r_buf.push_back((uint8_t)((p_value >> (i * 8)) & 0xFF));
	}
}

Vector<uint8_t> make_test_key() {
	Vector<uint8_t> key;
	key.resize(TEST_KEY_SIZE);
	for (int i = 0; i < TEST_KEY_SIZE; i++) {
		key.write[i] = (uint8_t)(i * 7 + 3);
	}
	return key;
}

Vector<uint8_t> make_test_iv() {
	Vector<uint8_t> iv;
	iv.resize(TEST_IV_SIZE);
	for (int i = 0; i < TEST_IV_SIZE; i++) {
		iv.write[i] = (uint8_t)(i * 11 + 5);
	}
	return iv;
}

// A minimal PCK directory table with a single file entry.
Vector<uint8_t> make_index_data() {
	Vector<uint8_t> data;
	String path = "res://test.txt";
	CharString path_utf8 = path.utf8();
	int padded_length = (path_utf8.length() + 3) & ~3;
	append_u32(data, padded_length);
	for (int i = 0; i < padded_length; i++) {
		data.push_back(i < path_utf8.length() ? (uint8_t)path_utf8[i] : 0);
	}
	append_u64(data, 0); // File offset.
	append_u64(data, 4); // File size.
	for (int i = 0; i < 16; i++) {
		data.push_back((uint8_t)i); // MD5.
	}
	append_u32(data, 0); // File flags.
	return data;
}

String make_test_dir() {
	String dir = OS::get_singleton()->get_temp_path().path_join("gdre_key_recovery");
	DirAccess::make_dir_recursive_absolute(dir);
	return dir;
}

bool write_test_pack(const String &p_path, uint32_t p_version, bool p_include_key) {
	Vector<uint8_t> plain = make_index_data();
	int index_size = plain.size();
	int padded_size = (index_size + 15) & ~15;
	plain.resize(padded_size);

	Vector<uint8_t> key = make_test_key();
	Vector<uint8_t> iv = make_test_iv();

	uint8_t md5[16];
	if (CryptoCore::md5(plain.ptr(), index_size, md5) != OK) {
		CHECK_MESSAGE(false, "Could not hash the test index");
		return false;
	}

	Vector<uint8_t> cipher;
	cipher.resize(padded_size);
	uint8_t cfb_iv[TEST_IV_SIZE];
	memcpy(cfb_iv, iv.ptr(), TEST_IV_SIZE);
	CryptoCore::AESContext ctx;
	if (ctx.set_encode_key(key.ptr(), 256) != OK || ctx.encrypt_cfb(padded_size, cfb_iv, plain.ptr(), cipher.ptrw()) != OK) {
		CHECK_MESSAGE(false, "Could not encrypt the test index");
		return false;
	}

	Vector<uint8_t> pack;
	append_u32(pack, PACK_HEADER_MAGIC);
	append_u32(pack, p_version);
	append_u32(pack, 4);
	append_u32(pack, 3);
	append_u32(pack, 0);
	append_u32(pack, PACK_DIR_ENCRYPTED | PACK_REL_FILEBASE);
	append_u64(pack, 0); // File base.
	if (p_version >= PACK_FORMAT_VERSION_V3) {
		append_u64(pack, 104); // Directory offset.
	}
	for (int i = 0; i < 16; i++) {
		append_u32(pack, 0); // Reserved.
	}
	int expected_header_size = p_version >= PACK_FORMAT_VERSION_V3 ? 104 : 96;
	CHECK(pack.size() == expected_header_size);
	if (pack.size() != expected_header_size) {
		return false;
	}

	append_u32(pack, 1); // File count.
	for (int i = 0; i < 16; i++) {
		pack.push_back(md5[i]);
	}
	append_u64(pack, index_size);
	for (int i = 0; i < TEST_IV_SIZE; i++) {
		pack.push_back(iv[i]);
	}
	pack.append_array(cipher);

	// File data area. The key is embedded here when requested, which is what a
	// self-contained executable containing the pack looks like to the scanner.
	for (int i = 0; i < 64; i++) {
		pack.push_back(0);
	}
	if (p_include_key) {
		pack.append_array(key);
	}

	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	if (f.is_null()) {
		CHECK_MESSAGE(false, "Could not create the test pack");
		return false;
	}
	f->store_buffer(pack.ptr(), pack.size());
	f->close();
	return true;
}

} // namespace

TEST_CASE("[GDSDecomp] Key recovery finds the key in a v2 pack") {
	String path = make_test_dir().path_join("encrypted_v2.pck");
	REQUIRE(write_test_pack(path, PACK_FORMAT_VERSION_V2, true));
	Vector<uint8_t> recovered = GDREKeyRecovery::recover_key(path);
	CHECK(recovered == make_test_key());
	DirAccess::remove_absolute(path);
}

TEST_CASE("[GDSDecomp] Key recovery finds the key in a v4 pack") {
	String path = make_test_dir().path_join("encrypted_v4.pck");
	REQUIRE(write_test_pack(path, PACK_FORMAT_VERSION_V4, true));
	Vector<uint8_t> recovered = GDREKeyRecovery::recover_key(path);
	CHECK(recovered == make_test_key());
	DirAccess::remove_absolute(path);
}

TEST_CASE("[GDSDecomp] Key recovery does not invent a key") {
	String path = make_test_dir().path_join("encrypted_v4_no_key.pck");
	REQUIRE(write_test_pack(path, PACK_FORMAT_VERSION_V4, false));
	Vector<uint8_t> recovered = GDREKeyRecovery::recover_key(path);
	CHECK(recovered.is_empty());
	DirAccess::remove_absolute(path);
}
