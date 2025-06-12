#include "gdre_packed_source.h"
#include "core/io/file_access_encrypted.h"
#include "core/object/script_language.h"
#include "file_access_gdre.h"
#include "gdre_settings.h"

// TODO: FIX THIS!
// static_assert(PACK_FORMAT_VERSION == GDREPackedSource::CURRENT_PACK_FORMAT_VERSION, "Pack format version changed.");

bool GDREPackedSource::seek_after_magic_unix(Ref<FileAccess> f) {
	f->seek(0);
	uint32_t magic = f->get_32();
	if (magic != 0x464c457f) { // 0x7F + "ELF"
		return false;
	}
	return true;
}

bool GDREPackedSource::get_pck_section_info_unix(Ref<FileAccess> f, GDREPackedSource::EXEPCKInfo &info) {
	if (f.is_null()) {
		return false;
	}
	// Read and check ELF magic number.
	if (!seek_after_magic_unix(f)) {
		return false;
	}
	info.type = EXEPCKInfo::ELF;
	// Read program architecture bits from class field.
	info.section_bit_size = f->get_8() * 32;

	// Get info about the section header table.
	int64_t section_table_pos;
	int64_t section_header_size;
	if (info.section_bit_size == 32) {
		section_header_size = 40;
		f->seek(0x20);
		section_table_pos = f->get_32();
		f->seek(0x30);
	} else { // 64
		section_header_size = 64;
		f->seek(0x28);
		section_table_pos = f->get_64();
		f->seek(0x3c);
	}
	int num_sections = f->get_16();
	int string_section_idx = f->get_16();

	// Load the strings table.
	uint8_t *strings;
	{
		// Jump to the strings section header.
		f->seek(section_table_pos + string_section_idx * section_header_size);

		// Read strings data size and offset.
		int64_t string_data_pos;
		int64_t string_data_size;
		if (info.section_bit_size == 32) {
			f->seek(f->get_position() + 0x10);
			string_data_pos = f->get_32();
			string_data_size = f->get_32();
		} else { // 64
			f->seek(f->get_position() + 0x18);
			string_data_pos = f->get_64();
			string_data_size = f->get_64();
		}

		// Read strings data.
		f->seek(string_data_pos);
		strings = (uint8_t *)memalloc(string_data_size);
		if (!strings) {
			return false;
		}
		f->get_buffer(strings, string_data_size);
	}

	// Search for the "pck" section.
	bool found = false;
	for (int i = 0; i < num_sections; ++i) {
		int64_t section_header_pos = section_table_pos + i * section_header_size;
		f->seek(section_header_pos);

		uint32_t name_offset = f->get_32();
		if (strcmp((char *)strings + name_offset, "pck") == 0) {
			info.pck_section_header_pos = section_header_pos;
			if (info.section_bit_size == 32) {
				f->seek(section_header_pos + 0x10);
				info.pck_embed_off = f->get_32();
				info.pck_embed_size = f->get_32();
			} else { // 64
				f->seek(section_header_pos + 0x18);
				info.pck_embed_off = f->get_64();
				info.pck_embed_size = f->get_64();
			}
			found = true;
			break;
		}
	}
	memfree(strings);
	return found;
}

bool GDREPackedSource::seek_after_magic_windows(Ref<FileAccess> f) {
	f->seek(0x3c);
	uint32_t pe_pos = f->get_32();
	if (pe_pos > f->get_length()) {
		return false;
	}
	f->seek(pe_pos);
	uint32_t magic = f->get_32();
	if (magic != 0x00004550) {
		return false;
	}
	return true;
}

bool GDREPackedSource::get_pck_section_info_windows(Ref<FileAccess> f, GDREPackedSource::EXEPCKInfo &r_info) {
	if (f.is_null()) {
		return false;
	}
	// Process header.
	if (!seek_after_magic_windows(f)) {
		return false;
	}
	r_info.type = EXEPCKInfo::PE;
	int num_sections;
	{
		int64_t header_pos = f->get_position();

		f->seek(header_pos + 2);
		num_sections = f->get_16();
		f->seek(header_pos + 16);
		uint16_t opt_header_size = f->get_16();

		// Skip rest of header + optional header to go to the section headers.
		f->seek(f->get_position() + 2 + opt_header_size);
	}
	int64_t section_table_pos = f->get_position();

	// Search for the "pck" section.
	bool found = false;
	for (int i = 0; i < num_sections; ++i) {
		int64_t section_header_pos = section_table_pos + i * 40;
		f->seek(section_header_pos);

		uint8_t section_name[9];
		f->get_buffer(section_name, 8);
		section_name[8] = '\0';

		if (strcmp((char *)section_name, "pck") == 0) {
			found = true;
			r_info.pck_section_header_pos = section_header_pos;
			f->seek(section_header_pos + 16);
			r_info.pck_embed_size = f->get_32();
			f->seek(section_header_pos + 20);
			r_info.pck_embed_off = f->get_32();

			break;
		}
	}
	return found;
}

bool GDREPackedSource::is_executable(const String &p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_V(f.is_null(), false);
	String extension = p_path.get_extension().to_lower();
	if (extension.ends_with("exe") || extension.ends_with("dll")) {
		return seek_after_magic_windows(f);
	}
	return seek_after_magic_unix(f);
}

bool GDREPackedSource::_get_exe_embedded_pck_info(Ref<FileAccess> f, const String &p_path, EXEPCKInfo &r_info) {
	bool pck_header_found = false;
	uint32_t magic = 0;
	if (f.is_null()) {
		return false;
	}

	pck_header_found = p_path.get_extension().to_lower() == "exe" ? get_pck_section_info_windows(f, r_info) : get_pck_section_info_unix(f, r_info);
	if (pck_header_found && r_info.pck_embed_off != 0) {
		r_info.pck_actual_off = r_info.pck_embed_off;
		// Search for the header, in case PCK start and section have different alignment.
		for (int i = 0; i < 8; i++) {
			f->seek(r_info.pck_actual_off);

			magic = f->get_32();
			if (magic == PACK_HEADER_MAGIC) {
				uint64_t ret_pos = f->get_position();
				uint64_t magic_pos = ret_pos - 4;
				f->seek(r_info.pck_embed_off + r_info.pck_embed_size - 4);
				if (f->get_32() == PACK_HEADER_MAGIC) {
					f->seek(r_info.pck_embed_off + r_info.pck_embed_size - 12);
					r_info.pck_actual_size = f->get_64();
				} else {
					WARN_PRINT("PCK header not found at the end of the embed section.");
					r_info.pck_actual_size = r_info.pck_embed_size - i;
				}
				f->seek(ret_pos);
				return true;
			}
			r_info.pck_actual_off++;
		}
	}

	// Search for the header at the end of file - self contained executable.
	{
		f->seek_end();
		f->seek(f->get_position() - 4);
		magic = f->get_32();

		if (magic == PACK_HEADER_MAGIC) {
			f->seek(f->get_position() - 12);
			r_info.pck_actual_size = f->get_64();
			r_info.pck_embed_size = r_info.pck_actual_size + 12; // pck_size + magic at the end
			f->seek(f->get_position() - r_info.pck_actual_size - 8);
			r_info.pck_embed_off = f->get_position();
			r_info.pck_actual_off = r_info.pck_embed_off;
			magic = f->get_32();
			if (magic == PACK_HEADER_MAGIC) {
				return true;
			}
		}
	}
	r_info.pck_actual_off = 0;
	r_info.pck_actual_size = 0;
	return false;
}

bool GDREPackedSource::seek_offset_from_exe(Ref<FileAccess> f, const String &p_path) {
	EXEPCKInfo info;
	auto ret = _get_exe_embedded_pck_info(f, p_path, info);
#ifdef DEBUG_ENABLED
	if (ret) {
		if (info.pck_section_header_pos == 0) {
			print_verbose("PCK header found at the end of executable, loading from offset 0x" + String::num_int64(info.pck_actual_off, 16));
		} else {
			print_verbose("PCK header found from pck section, loading from offset 0x" + String::num_int64(info.pck_actual_off, 16));
		}
		print_verbose("PCK embed offset: " + String::num_int64(info.pck_embed_off, 16));
		print_verbose("PCK embed size: " + itos(info.pck_embed_size));
		print_verbose("PCK actual offset: " + String::num_int64(info.pck_actual_off, 16));
		print_verbose("PCK actual size: " + itos(info.pck_actual_size));
	} else {
		print_verbose("Embedded PCK not found in executable.");
	}
#endif
	return ret;
}

bool GDREPackedSource::get_exe_embedded_pck_info(const String &p_path, GDREPackedSource::EXEPCKInfo &r_info) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	auto ret = _get_exe_embedded_pck_info(f, p_path, r_info);
#ifdef DEBUG_ENABLED
	if (ret) {
		print_verbose("PCK embed offset: " + String::num_int64(r_info.pck_embed_off, 16));
		print_verbose("PCK embed size: " + itos(r_info.pck_embed_size));
		print_verbose("PCK actual offset: " + String::num_int64(r_info.pck_actual_off, 16));
		print_verbose("PCK actual size: " + itos(r_info.pck_actual_size));
	} else {
		print_verbose("Embedded PCK not found in executable.");
	}
#endif
	return ret;
}

bool GDREPackedSource::is_embeddable_executable(const String &p_path) {
	return is_executable(p_path);
}

bool GDREPackedSource::has_embedded_pck(const String &p_path) {
	if (!is_executable(p_path)) {
		return false;
	}
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		return false;
	}
	return seek_offset_from_exe(f, p_path);
}

bool GDREPackedSource::try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset) {
	if (p_path.get_extension().to_lower() == "apk" || p_path.get_extension().to_lower() == "zip") {
		return false;
	}
	String pck_path = p_path.replace("_GDRE_a_really_dumb_hack", "");
	Ref<FileAccess> f = FileAccess::open(pck_path, FileAccess::READ);
	if (f.is_null()) {
		return false;
	}

	f->seek(p_offset);

	bool is_exe = false;
	uint32_t magic = f->get_32();

	if (magic != PACK_HEADER_MAGIC) {
		// Loading with offset feature not supported for self contained exe files.
		if (p_offset != 0) {
			ERR_FAIL_V_MSG(false, "Loading self-contained executable with offset not supported.");
		}

		if (!seek_offset_from_exe(f, pck_path)) {
			return false;
		}
		is_exe = true;
	}

	int64_t pck_start_pos = f->get_position() - 4;

	uint32_t version = f->get_32();
	uint32_t ver_major = f->get_32();
	uint32_t ver_minor = f->get_32();
	uint32_t ver_rev = f->get_32(); // patch number, did not start getting set to anything other than 0 until 3.2

	if (version > CURRENT_PACK_FORMAT_VERSION) {
		ERR_FAIL_V_MSG(false, "Pack version unsupported: " + itos(version) + ".");
	}

	uint32_t pack_flags = 0;
	uint64_t file_base = 0;

	if (version == 2) {
		pack_flags = f->get_32();
		file_base = f->get_64();
	}

	bool enc_directory = (pack_flags & PACK_DIR_ENCRYPTED);
	bool rel_filebase = (pack_flags & PACK_REL_FILEBASE);

	for (int i = 0; i < 16; i++) {
		//reserved
		f->get_32();
	}

	uint32_t file_count = f->get_32();

	if (rel_filebase) {
		file_base += pck_start_pos;
	}

	if (enc_directory) {
		Ref<FileAccessEncrypted> fae = memnew(FileAccessEncrypted);
		if (fae.is_null()) {
			GDRESettings::get_singleton()->_set_error_encryption(true);
			ERR_FAIL_V_MSG(false, "Failed to instance FileAccessEncrypted??????.");
		}

		Vector<uint8_t> key;
		key.resize(32);
		for (int i = 0; i < key.size(); i++) {
			key.write[i] = script_encryption_key[i];
		}

		Error err = fae->open_and_parse(f, key, FileAccessEncrypted::MODE_READ, false);
		if (err) {
			GDRESettings::get_singleton()->_set_error_encryption(true);
			ERR_FAIL_V_MSG(false, "Can't open encrypted pack directory (PCK format version " + itos(version) + ", engine version " + itos(ver_major) + "." + itos(ver_minor) + "." + itos(ver_rev) + ").");
		}
		f = fae;
	}
	String ver_string;

	Ref<GodotVer> godot_ver;
	bool suspect_version = false;
	if (ver_major < 2) {
		// it is very unlikely that we will encounter Godot 1.x games in the wild.
		// This is likely a pck created with a creation tool.
		// We need to determine the version number from the binary resources.
		// (if it is 1.x, we'll determine that through the binary resources too)
		suspect_version = true;
	}
	if (ver_major < 3 || (ver_major == 3 && ver_minor < 2)) {
		// they only started writing the actual patch number in 3.2
		ver_string = itos(ver_major) + "." + itos(ver_minor);
	} else {
		ver_string = itos(ver_major) + "." + itos(ver_minor) + "." + itos(ver_rev);
	}
	godot_ver = GodotVer::parse(ver_string);

	// Everything worked, now set the data
	Ref<GDRESettings::PackInfo> pckinfo;
	pckinfo.instantiate();
	pckinfo->init(
			pck_path, godot_ver, version, pack_flags, file_base, file_count, is_exe ? GDRESettings::PackInfo::EXE : GDRESettings::PackInfo::PCK, enc_directory, suspect_version);
	GDRESettings::get_singleton()->add_pack_info(pckinfo);

	for (uint32_t i = 0; i < file_count; i++) {
		uint32_t sl = f->get_32();
		CharString cs;
		cs.resize(sl + 1);
		f->get_buffer((uint8_t *)cs.ptr(), sl);
		cs[sl] = 0;

		String path;
		path.append_utf8(cs.ptr());
		String p_file = path.get_file();
		ERR_FAIL_COND_V_MSG(p_file.begins_with("gdre_") && p_file != "gdre_export.log", false, "Don't try to extract the GDRE pack files, just download the source from github.");

		uint64_t ofs = file_base + f->get_64();
		uint64_t size = f->get_64();
		uint8_t md5[16];
		uint32_t flags = 0;
		f->get_buffer(md5, 16);
		if (version == 2) {
			flags = f->get_32();
		}
		if (flags & PACK_FILE_REMOVAL) { // The file was removed.
			GDREPackedData::get_singleton()->remove_path(path);
		} else {
			GDREPackedData::get_singleton()->add_path(pck_path, path, ofs + p_offset, size, md5, this, p_replace_files, (flags & PACK_FILE_ENCRYPTED), true);
		}
	}

	return true;
}
Ref<FileAccess> GDREPackedSource::get_file(const String &p_path, PackedData::PackedFile *p_file) {
	return memnew(FileAccessPack(p_path, *p_file));
}
