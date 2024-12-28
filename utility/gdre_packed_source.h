#pragma once

#include "core/io/file_access_pack.h"

class GDREPackedSource : public PackSource {
public:
	static constexpr int CURRENT_PACK_FORMAT_VERSION = 2;
	static bool is_embeddable_executable(const String &p_path);
	static bool has_embedded_pck(const String &p_path);
	virtual bool try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset);
	virtual Ref<FileAccess> get_file(const String &p_path, PackedData::PackedFile *p_file);
};
