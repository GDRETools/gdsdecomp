#pragma once

#include "core/io/file_access_pack.h"

class GDREPackedSource : public PackSource {
public:
	struct EXEPCKInfo {
		enum EXEType {
			PE = 0,
			ELF = 1,
			MACHO = 2,
			UNKNOWN = 3
		};
		uint64_t pck_section_header_pos = 0;
		uint64_t pck_embed_off = 0;
		uint64_t pck_actual_off = 0;
		uint64_t pck_embed_size = 0;
		uint64_t pck_actual_size = 0;
		EXEType type = UNKNOWN;
		uint32_t section_bit_size = 32;
	};

private:
	static bool _get_exe_embedded_pck_info(Ref<FileAccess> f, const String &p_path, GDREPackedSource::EXEPCKInfo &r_info, const PackedByteArray &custom_magic = PackedByteArray());
	static bool seek_after_magic_unix(Ref<FileAccess> f);
	static bool get_pck_section_info_unix(Ref<FileAccess> f, GDREPackedSource::EXEPCKInfo &info);
	static bool seek_after_magic_windows(Ref<FileAccess> f);
	static bool get_pck_section_info_windows(Ref<FileAccess> f, GDREPackedSource::EXEPCKInfo &r_info);
	static bool is_magic_ascii(uint32_t magic);
	static String get_magic_ascii(uint32_t magic);

public:
	static constexpr int CURRENT_PACK_FORMAT_VERSION = 4;
	static bool is_executable(const String &p_path);
	static bool is_embeddable_executable(const String &p_path);
	static bool has_embedded_pck(const String &p_path);
	static bool get_exe_embedded_pck_info(const String &p_path, GDREPackedSource::EXEPCKInfo &r_info);
	static bool seek_offset_from_exe(Ref<FileAccess> f, const String &p_path, uint64_t &r_pck_size, const PackedByteArray &custom_magic = PackedByteArray());
	static Ref<FileAccess> get_bundled_file(const String &p_path, PackedData::PackedFile *p_file, const Vector<uint8_t> &p_decryption_key = Vector<uint8_t>());
	static Ref<FileAccess> open_encrypted_file(const Ref<FileAccess> &p_base, const Vector<uint8_t> &p_key, FileAccess::ModeFlags p_mode = FileAccess::READ, bool p_with_magic = true, const Vector<uint8_t> &p_iv = Vector<uint8_t>());

	virtual bool try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset = 0, const Vector<uint8_t> &p_decryption_key = Vector<uint8_t>()) override;
	virtual Ref<FileAccess> get_file(const String &p_path, PackedData::PackedFile *p_file, const Vector<uint8_t> &p_decryption_key = Vector<uint8_t>()) override;
};
