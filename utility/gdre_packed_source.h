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
	static bool _get_exe_embedded_pck_info(Ref<FileAccess> f, const String &p_path, GDREPackedSource::EXEPCKInfo &r_info);
	static bool seek_after_magic_unix(Ref<FileAccess> f);
	static bool get_pck_section_info_unix(Ref<FileAccess> f, GDREPackedSource::EXEPCKInfo &info);
	static bool seek_after_magic_windows(Ref<FileAccess> f);
	static bool get_pck_section_info_windows(Ref<FileAccess> f, GDREPackedSource::EXEPCKInfo &r_info);
	static bool is_executable(const String &p_path);
	static bool seek_offset_from_exe(Ref<FileAccess> f, const String &p_path);

public:
	static constexpr int CURRENT_PACK_FORMAT_VERSION = 3;
	static bool is_embeddable_executable(const String &p_path);
	static bool has_embedded_pck(const String &p_path);
	static bool get_exe_embedded_pck_info(const String &p_path, GDREPackedSource::EXEPCKInfo &r_info);
	virtual bool try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset);
	virtual Ref<FileAccess> get_file(const String &p_path, PackedData::PackedFile *p_file);
};
