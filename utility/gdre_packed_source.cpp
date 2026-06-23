#include "gdre_packed_source.h"
#include "core/version_generated.gen.h"
#include "crypto/file_access_encrypted_custom.h"
#include "file_access_apk.h"
#include "file_access_gdre.h"
#include "gdre_settings.h"

#include "core/io/file_access_encrypted.h"
#include "core/io/file_access_pack.h"
#include "utility/file_access_patched_gdre.h"

static_assert(PACK_FORMAT_VERSION == GDREPackedSource::CURRENT_PACK_FORMAT_VERSION, "Pack format version changed.");

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
namespace {
static inline bool check_magic(const Ref<FileAccess> &f, const PackedByteArray &custom_magic) {
	if (!custom_magic.is_empty()) {
		for (int i = 0; i < custom_magic.size(); i++) {
			if (f->get_8() != custom_magic[i]) {
				return false;
			}
		}
		return true;
	} else {
		return f->get_32() == PACK_HEADER_MAGIC;
	}
}
} // namespace

bool GDREPackedSource::_get_exe_embedded_pck_info(Ref<FileAccess> f, const String &p_path, EXEPCKInfo &r_info, const PackedByteArray &custom_magic) {
	bool pck_header_found = false;
	if (f.is_null()) {
		return false;
	}

	pck_header_found = p_path.get_extension().to_lower() == "exe" ? get_pck_section_info_windows(f, r_info) : get_pck_section_info_unix(f, r_info);
	if (pck_header_found && r_info.pck_embed_off != 0) {
		r_info.pck_actual_off = r_info.pck_embed_off;
		// Search for the header, in case PCK start and section have different alignment.
		for (int i = 0; i < 8; i++) {
			f->seek(r_info.pck_actual_off);

			if (check_magic(f, custom_magic)) {
				uint64_t ret_pos = f->get_position();
				f->seek(r_info.pck_embed_off + r_info.pck_embed_size - 4);
				if (check_magic(f, custom_magic)) {
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

		if (check_magic(f, custom_magic)) {
			f->seek(f->get_position() - 12);
			r_info.pck_actual_size = f->get_64();
			r_info.pck_embed_size = r_info.pck_actual_size + 12; // pck_size + magic at the end
			f->seek(f->get_position() - r_info.pck_actual_size - 8);
			r_info.pck_embed_off = f->get_position();
			r_info.pck_actual_off = r_info.pck_embed_off;
			if (check_magic(f, custom_magic)) {
				return true;
			}
		}
	}
	r_info.pck_actual_off = 0;
	r_info.pck_actual_size = 0;
	return false;
}

bool GDREPackedSource::seek_offset_from_exe(Ref<FileAccess> f, const String &p_path, uint64_t &r_pck_size, const PackedByteArray &custom_magic) {
	EXEPCKInfo info;
	auto ret = _get_exe_embedded_pck_info(f, p_path, info, custom_magic);
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
	r_pck_size = info.pck_embed_size;
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
	uint64_t file_length;
	return seek_offset_from_exe(f, p_path, file_length);
}

bool GDREPackedSource::is_magic_ascii(uint32_t magic) {
	for (int i = 0; i < 4; i++) {
		// printable ASCII characters
		uint32_t c = magic & 0xFF;
		if (c < 32 || c > 127) {
			return false;
		}
		magic >>= 8;
	}
	return true;
}

String GDREPackedSource::get_magic_ascii(uint32_t magic) {
	// little-endian to ASCII
	return String::chr(magic & 0xFF) + String::chr((magic >> 8) & 0xFF) + String::chr((magic >> 16) & 0xFF) + String::chr((magic >> 24) & 0xFF);
}

bool GDREPackedSource::try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset, const Vector<uint8_t> &p_decryption_key) {
	String ext = p_path.get_extension().to_lower();
	if (ext == "apk" || ext == "zip") {
		return false;
	}
	String pck_path = p_path;
	Ref<FileAccess> f = FileAccess::open(pck_path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), false, "Failed to open pack file: " + pck_path);

	f->seek(p_offset);

	bool is_exe = false;
	uint32_t magic = f->get_32();
	bool suspect_magic = false;
	String non_standard_header;

	uint64_t pck_size = f->get_length();

	if (magic != PACK_HEADER_MAGIC) {
		if (ext == "pck" && is_magic_ascii(magic)) {
			suspect_magic = true;
			non_standard_header = get_magic_ascii(magic);
			WARN_PRINT("PCK has suspect magic: " + get_magic_ascii(magic));
		} else {
			// Loading with offset feature not supported for self contained exe files.
			if (p_offset != 0) {
				ERR_FAIL_V_MSG(false, "Loading self-contained executable with offset not supported.");
			}

			if (!seek_offset_from_exe(f, pck_path, pck_size)) {
				ERR_FAIL_COND_V_MSG(ext == "pck", false, "PCK header not found in pck file: " + pck_path);
				return false;
			}
			is_exe = true;
		}
	}

	int64_t pck_start_pos = f->get_position() - 4;
	uint64_t pck_end_pos = pck_start_pos + pck_size;

	uint32_t version = f->get_32();
	uint32_t ver_major = f->get_32();
	uint32_t ver_minor = f->get_32();
	uint32_t ver_patch = f->get_32(); // patch number, did not start getting set to anything other than 0 until 3.2

	if (version > CURRENT_PACK_FORMAT_VERSION) {
		ERR_FAIL_V_MSG(false, "Pack version unsupported: " + itos(version) + ". (engine version: " + itos(ver_major) + "." + itos(ver_minor) + "." + itos(ver_patch) + ")");
	}

	if (suspect_magic) {
		if (ver_major > GODOT_VERSION_MAJOR || (ver_major == 0)) {
			ERR_FAIL_V_MSG(false, "PCK ver_major is suspicious, not continuing: " + itos(version) + ". (engine version: " + itos(ver_major) + "." + itos(ver_minor) + "." + itos(ver_patch) + ")");
		}
	}

	uint32_t pack_flags = 0;
	uint64_t file_base = 0;

	if (version >= PACK_FORMAT_VERSION_V2) {
		pack_flags = f->get_32();
		file_base = f->get_64();
	}

	bool enc_directory = (pack_flags & PACK_DIR_ENCRYPTED);
	bool rel_filebase = (pack_flags & PACK_REL_FILEBASE);
	bool sparse_bundle = (pack_flags & PACK_SPARSE_BUNDLE);
	String salt;

	if ((version == PACK_FORMAT_VERSION_V4) || (version == PACK_FORMAT_VERSION_V3) || (version == PACK_FORMAT_VERSION_V2 && rel_filebase)) {
		file_base += pck_start_pos;
	}

#define DEBUG_PCK_INFO()                                                                             \
	String("PCK version: ") + itos(version) +                                                        \
			", engine version: " + itos(ver_major) + "." + itos(ver_minor) + "." + itos(ver_patch) + \
			", enc_directory: " + (enc_directory ? "true" : "false") +                               \
			", rel_filebase: " + (rel_filebase ? "true" : "false") +                                 \
			", filebase: " + String::num_int64(file_base) +                                          \
			", pck_start_pos: " + String::num_int64(pck_start_pos) + ")."

	if (version == PACK_FORMAT_VERSION_V3 || version == PACK_FORMAT_VERSION_V4) {
		// V3/v4: Read directory offset and skip reserved part of the header.
		uint64_t dir_offset = f->get_64() + pck_start_pos;
		if (sparse_bundle && enc_directory && version == PACK_FORMAT_VERSION_V4) {
			// V4: Read encrypted directory salt.
			Vector<uint8_t> salt_data = f->get_buffer(32);
			salt.append_latin1(Span((const char *)salt_data.ptr(), salt_data.size()));
		}
		ERR_FAIL_COND_V_MSG(dir_offset == 0, false,
				"Directory offset is 0, this is not a valid PCK file\n" + DEBUG_PCK_INFO());
		ERR_FAIL_COND_V_MSG(dir_offset >= pck_end_pos, false, "Directory offset is out of bounds: " + String::num_int64(dir_offset) + " (file length: " + String::num_int64(pck_end_pos) + ")");
		f->seek(dir_offset);
	} else if (version <= PACK_FORMAT_VERSION_V2) {
		// V2: Directory directly after the header.
		for (int i = 0; i < 16; i++) {
			f->get_32(); // Reserved.
		}
	}
#undef DEBUG_PCK_INFO

	uint32_t file_count = f->get_32();
	ERR_FAIL_COND_V_MSG(file_count > 0 && file_base >= pck_end_pos, false, "file_base is out of bounds: " + String::num_int64(file_base) + " (file length: " + String::num_int64(pck_size) + ")");
	if (enc_directory) {
		Vector<uint8_t> key = GDRESettings::get_singleton()->get_encryption_key();
		Error err = OK;
		if (GDRESettings::get_singleton()->get_custom_decryptor().is_valid()) {
			Ref<FileAccessEncryptedCustom> fae = FileAccessEncryptedCustom::create(GDRESettings::get_singleton()->get_custom_decryptor());
			err = fae->open_and_parse(f, key, FileAccessEncryptedCustom::MODE_READ, false);
			f = fae;
		} else {
			Ref<FileAccessEncrypted> fae = memnew(FileAccessEncrypted);
			err = fae->open_and_parse(f, key, FileAccessEncrypted::MODE_READ, false);
			f = fae;
		}
		if (err) {
			GDRESettings::get_singleton()->_set_error_encryption(true);
			ERR_FAIL_V_MSG(false, "Can't open encrypted pack directory (PCK format version " + itos(version) + ", engine version " + itos(ver_major) + "." + itos(ver_minor) + "." + itos(ver_patch) + ").");
		}
	}

	// Set Pack info before reading the file list.
	String ver_string;

	Ref<GodotVer> godot_ver;
	bool suspect_version = false;
	if (ver_major < 2 || ver_major > 25) { // 1.x or Redot
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
		ver_string = itos(ver_major) + "." + itos(ver_minor) + "." + itos(ver_patch);
	}
	godot_ver = GodotVer::parse(ver_string);

	// Everything worked, now set the data
	Ref<PackInfo> pckinfo;
	pckinfo.instantiate();
	pckinfo->init(
			pck_path, godot_ver, version, pack_flags, file_base, file_count, is_exe ? PackInfo::EXE : PackInfo::PCK, enc_directory, suspect_version, non_standard_header);
	GDRESettings::get_singleton()->add_pack_info(pckinfo);

	bool opened_encrypted_file = false;
	bool open_encrypted_success = false;
	int64_t encrypted_file_count = 0;
	// Read the file list.
	for (uint32_t i = 0; i < file_count; i++) {
		uint32_t sl = f->get_32();
		CharString cs;
		cs.resize_uninitialized(sl + 1);
		f->get_buffer((uint8_t *)cs.ptr(), sl);
		cs[sl] = 0;

		String path;
		path.append_utf8(cs.ptr());
		String p_file = path.get_file();
		ERR_FAIL_COND_V_MSG(p_file.begins_with("gdre_") && p_file != "gdre_export.log", false, "Don't try to extract the GDRE pack files, just download the source from github.");

		// TODO: Ask bruvzg about whether or not p_offset is needed here.
		uint64_t ofs = file_base + f->get_64() + (version >= PACK_FORMAT_VERSION_V3 ? 0 : p_offset);
		uint64_t size = f->get_64();
		uint8_t md5[16];
		uint32_t flags = 0;
		// check if the file offset is out of bounds for pcks with suspect magic
		ERR_FAIL_COND_V_MSG(suspect_magic && (ofs + size > pck_end_pos), false, "File offset is out of bounds: " + String::num_int64(ofs) + " (file length: " + String::num_int64(pck_end_pos) + ")");
		f->get_buffer(md5, 16);
		if (version >= PACK_FORMAT_VERSION_V2) {
			flags = f->get_32();
		}
		if (flags & PACK_FILE_REMOVAL) { // The file was removed.
			GDREPackedData::get_singleton()->remove_path(path);
		} else {
			bool encrypted = (flags & PACK_FILE_ENCRYPTED);
			bool delta = (flags & PACK_FILE_DELTA);
			GDREPackedData::get_singleton()->add_path(pck_path, path, ofs, size, md5, this, p_replace_files, encrypted, sparse_bundle, delta, salt);
			if (encrypted) {
				encrypted_file_count++;
				if (!opened_encrypted_file && size > 0 && !delta) {
					opened_encrypted_file = true;
					// try opening the file
					PackedData::PackedFile pf;
					pf.pack = pck_path;
					pf.offset = ofs;
					pf.size = size;
					memcpy(pf.md5, md5, 16);
					pf.src = this;
					pf.encrypted = encrypted;
					pf.bundle = sparse_bundle;
					pf.delta = delta;
					Ref<FileAccess> fa = get_file(path, &pf);
					if (fa.is_null() || fa->get_error() != OK) {
						WARN_PRINT("Can't open encrypted files in PCK!");
						GDRESettings::get_singleton()->_set_error_encryption(true);
					} else {
						open_encrypted_success = true;
					}
				}
			}
		}
	}
	if (opened_encrypted_file && !open_encrypted_success) {
		WARN_PRINT("Can't decrypt " + itos(encrypted_file_count) + " encrypted files in PCK!");
	}

	return true;
}
namespace {
static inline Ref<FileAccess> open_encrypted_file(PackedData::PackedFile *p_file, const String &p_path, const Vector<uint8_t> &p_decryption_key) {
	Ref<FileAccess> base = FileAccess::open(p_file->pack, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(base.is_null(), nullptr, vformat("Can't open pack-referenced file '%s'.", String(p_path)));
	base->seek(p_file->offset);
	if (GDRESettings::get_singleton()->get_custom_decryptor().is_valid()) {
		Ref<FileAccessEncryptedCustom> fae = FileAccessEncryptedCustom::create(GDRESettings::get_singleton()->get_custom_decryptor());
		ERR_FAIL_COND_V_MSG(fae.is_null(), nullptr, vformat("Can't open encrypted pack-referenced file '%s'.", String(p_path)));
		Error err = fae->open_and_parse(base, p_decryption_key, FileAccessEncryptedCustom::MODE_READ, false);
		ERR_FAIL_COND_V_MSG(err, nullptr, vformat("Can't open encrypted pack-referenced file '%s'.", String(p_path)));
		return fae;
	}
	Ref<FileAccessEncrypted> fae = memnew(FileAccessEncrypted);
	ERR_FAIL_COND_V_MSG(fae.is_null(), nullptr, vformat("Can't open encrypted pack-referenced file '%s'.", String(p_path)));
	Error err = fae->open_and_parse(base, p_decryption_key, FileAccessEncrypted::MODE_READ, false);
	ERR_FAIL_COND_V_MSG(err, nullptr, vformat("Can't open encrypted pack-referenced file '%s'.", String(p_path)));
	return fae;
}
} // namespace

Ref<FileAccess> GDREPackedSource::get_bundled_file(const String &p_path, PackedData::PackedFile *p_file, const Vector<uint8_t> &p_decryption_key) {
	String simplified_path = p_path.simplify_path();
	String search_path = simplified_path;
	auto pf = PackedData::PackedFile(*p_file);
	pf.offset = 0;

	Ref<FileAccess> file;

	if (!pf.salt.is_empty()) {
		search_path = "res://" + (simplified_path + pf.salt).sha256_text();
	}

	if (APKArchive::get_singleton() && APKArchive::get_singleton()->file_exists(search_path)) {
		// APKArchive ignores the pf file, so no need to modify it
		file = APKArchive::get_singleton()->get_file(search_path, &pf);
	} else if (DirSource::get_singleton() && DirSource::get_singleton()->file_exists(search_path)) {
		pf.pack = DirSource::get_singleton()->get_pack_path(search_path);
		file = DirSource::get_singleton()->get_file(search_path, &pf);
	}

	ERR_FAIL_COND_V_MSG(file.is_null(), nullptr, vformat("APKArchive or DirSource doesn't contain sparse pack-referenced file '%s'.", p_path));

	if (pf.encrypted) {
		file = open_encrypted_file(&pf, p_path, p_decryption_key);
	}

	return file;
}

Ref<FileAccess> GDREPackedSource::get_file(const String &p_path, PackedData::PackedFile *p_file, const Vector<uint8_t> &p_decryption_key) {
	// if we call the constructor for FileAccessPack if it's a bundle,
	// it'll cause an infinite loop; we need to just create the thing ourselves
	Ref<FileAccess> file;
	Vector<uint8_t> decryption_key = p_decryption_key.is_empty() ? GDRESettings::get_singleton()->get_encryption_key() : p_decryption_key;
	if (p_file->bundle) {
		file = get_bundled_file(p_path, p_file, decryption_key);
		ERR_FAIL_COND_V_MSG(file.is_null(), nullptr, vformat("Can't open bundled pack-referenced file '%s'.", String(p_path)));
	} else {
		if (p_file->encrypted && GDRESettings::get_singleton()->get_custom_decryptor().is_valid()) {
			file = open_encrypted_file(p_file, p_path, decryption_key);
			ERR_FAIL_COND_V_MSG(file.is_null(), nullptr, vformat("Can't open encrypted pack-referenced file '%s'.", String(p_path)));
		} else {
			// otherwise...
			file = Ref<FileAccess>(memnew(FileAccessPack(p_path, *p_file, decryption_key)));
		}
	}

	if (GDREPackedData::get_singleton()->has_delta_patches(p_path)) {
		Ref<FileAccessPatchedGDRE> file_patched;
		file_patched.instantiate();
		Error err = file_patched->open_custom(file);
		ERR_FAIL_COND_V(err != OK, Ref<FileAccess>());
		file = file_patched;
	}

	return file;
}
