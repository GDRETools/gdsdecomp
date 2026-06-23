class_name GDREStandardPackSource
extends PackSourceCustom

# This script re-implements the standard pack source logic for Godot PCK files.
# Use this as a base for your own custom pack source logic.

# Godot's packed file magic header ("GDPC" in ASCII, stored as little-endian in the file).
const PACK_HEADER_MAGIC: int = 0x43504447
# 'G', 'D', 'P', 'C' in ASCII.
var PACK_HEADER_MAGIC_BYTES: PackedByteArray = PackedByteArray([0x47, 0x44, 0x50, 0x43])
const PACK_FORMAT_VERSION_V2: int = 2
const PACK_FORMAT_VERSION_V3: int = 3
const PACK_FORMAT_VERSION_V4: int = 4

const CURRENT_PACK_FORMAT_VERSION: int = 4


const PACK_DIR_ENCRYPTED: int = 1 << 0
const PACK_REL_FILEBASE: int = 1 << 1
const PACK_SPARSE_BUNDLE: int = 1 << 2

const PACK_FILE_ENCRYPTED: int = 1 << 0
const PACK_FILE_REMOVAL: int = 1 << 1
const PACK_FILE_DELTA: int = 1 << 2

func open_encrypted_file(base: FileAccess, key: PackedByteArray) -> FileAccess:
	if GDRESettings.get_custom_decryptor():
		return FileAccessEncryptedCustom.create_and_parse_custom(GDRESettings.get_custom_decryptor(), base, key, FileAccessEncryptedCustom.MODE_READ, false)
	return FileAccessEncryptedCustom.create_and_parse_non_custom(base, key, FileAccessEncryptedCustom.MODE_READ, false)

func _try_open_pack(pck_path: String, p_replace_files: bool, p_offset: int, p_decryption_key: PackedByteArray) -> bool:
	var ext: String = pck_path.get_extension().to_lower()
	if ext == "apk" or ext == "zip":
		return false

	var f = FileAccess.open(pck_path, FileAccess.READ)
	f.seek(p_offset)

	var is_exe: bool = false
	var magic: int = f.get_32()
	var pck_size: int = f.get_length()
	if magic != PACK_HEADER_MAGIC:
		if (p_offset != 0):
			printerr("Loading self-contained executable with offset not supported.")
			return false # Loading self-contained executable with offset not supported.

		pck_size = PackSourceCustom.seek_pck_offset_from_exe(f, pck_path, PACK_HEADER_MAGIC_BYTES)
		if pck_size == -1:
			if ext == "pck":
				printerr("PCK header not found in pck file: " + pck_path)
			else:
				printerr("PCK header not found in executable file: " + pck_path)
			return false

		is_exe = true

	# We're right after the magic header, so we need to subtract 4 to get the start position of the pack.
	var pck_start_pos = f.get_position() - 4
	var pck_end_pos = pck_start_pos + pck_size

	var version: int = f.get_32()
	var ver_major: int = f.get_32()
	var ver_minor: int = f.get_32()
	var ver_patch: int = f.get_32()

	if version > CURRENT_PACK_FORMAT_VERSION:
		printerr("Pack version unsupported: " + str(version) + ". (engine version: " + str(ver_major) + "." + str(ver_minor) + "." + str(ver_patch) + ")")
		return false

	var pack_flags = 0
	var file_base = 0

	if version >= PACK_FORMAT_VERSION_V2:
		pack_flags = f.get_32()
		file_base = f.get_64()

	var enc_directory: bool = (pack_flags & PACK_DIR_ENCRYPTED)
	var rel_filebase: bool = (pack_flags & PACK_REL_FILEBASE)
	var sparse_bundle: bool = (pack_flags & PACK_SPARSE_BUNDLE)

	var salt: String = ""

	if (version == PACK_FORMAT_VERSION_V4) or (version == PACK_FORMAT_VERSION_V3) or (version == PACK_FORMAT_VERSION_V2 and rel_filebase):
		file_base += pck_start_pos

	if (version == PACK_FORMAT_VERSION_V4) or (version == PACK_FORMAT_VERSION_V3):
		# V3/v4: Read directory offset and skip reserved part of the header.
		var dir_offset: int = f.get_64() + pck_start_pos
		if sparse_bundle and enc_directory and version == PACK_FORMAT_VERSION_V4:
			# V4: Read encrypted directory salt.
			var salt_data: PackedByteArray = f.get_buffer(32)
			salt = salt_data.get_string_from_utf8()
		if dir_offset == 0:
			printerr("Directory offset is 0, this is not a valid PCK file")
			return false
		if dir_offset >= pck_end_pos:
			printerr("Directory offset is out of bounds: " + str(dir_offset) + " (file length: " + str(pck_end_pos) + ")")
			return false
		f.seek(dir_offset)
	elif version == PACK_FORMAT_VERSION_V2:
		# V2: Directory directly after the header.
		for i in range(16): # read 16 reserved 32-bit words
			f.get_32() # Reserved.

	# At directory start, read file count
	var file_count = f.get_32()

	if file_count > 0 and file_base >= pck_end_pos:
		printerr("file_base is out of bounds: " + str(file_base) + " (file length: " + str(pck_end_pos) + ")")
		return false

	# Read encrypted directory.
	if enc_directory:
		if p_decryption_key.is_empty():
			p_decryption_key = GDRESettings.get_encryption_key()
		var file = open_encrypted_file(f, p_decryption_key)
		if not file or file.get_error() != OK:
			printerr("Failed to open encrypted pack directory: " + str(file.get_error() if file else "Unknown error"))
			return false
		f = file

	var godot_ver = GodotVer.create_godotver(ver_major, ver_minor, ver_patch)

	var pack_info = PackInfo.new()
	pack_info.pack_file = pck_path
	pack_info.version = godot_ver
	pack_info.fmt_version = version
	pack_info.pack_flags = pack_flags
	pack_info.file_base = file_base
	pack_info.file_count = file_count
	pack_info.type = PackInfo.PCK if not is_exe else PackInfo.EXE
	pack_info.encrypted = enc_directory
	pack_info.suspect_version = false
	pack_info.non_standard_header = ""
	pack_info.app_version = ""

	GDRESettings.add_pack_info(pack_info)

	for i in range(file_count):
		var file_name_length = f.get_32()
		var file_path = f.get_buffer(file_name_length).get_string_from_utf8()
		var _file_offset = f.get_64()
		var ofs = file_base + _file_offset
		if version < PACK_FORMAT_VERSION_V3:
			ofs += p_offset
		var file_size = f.get_64()
		var md5 = f.get_buffer(16)
		var flags = 0
		if version >= PACK_FORMAT_VERSION_V2:
			flags = f.get_32()
		if flags & PACK_FILE_REMOVAL:
			GDREPackedData.remove_path(file_path)
		else:
			var encrypted = (flags & PACK_FILE_ENCRYPTED)
			var delta = (flags & PACK_FILE_DELTA)
			GDREPackedData.add_path(pck_path, file_path, ofs, file_size, md5, self, p_replace_files, encrypted, sparse_bundle, delta, salt)

	return true


func _get_file(p_path: String, p_file: PackedFile, p_decryption_key: PackedByteArray) -> FileAccess:
	var file: FileAccess = null

	if p_decryption_key.is_empty():
		p_decryption_key = GDRESettings.get_encryption_key()

	if p_file.bundle:
		file = PackSourceCustom.get_bundled_file(p_path, p_file, p_decryption_key)
		if not file or file.get_error() != OK:
			printerr("Failed to get bundled pack-referenced file: " + str(file.get_error() if file else "Unknown error"))
			return null
	else:
		if p_file.encrypted:
			var base = FileAccess.open(p_file.pack, FileAccess.READ)
			if not base or base.get_error() != OK:
				printerr("Failed to open pack-referenced file: " + str(base.get_error() if base else "Unknown error"))
				return null
			base.seek(p_file.offset)
			file = open_encrypted_file(base, p_decryption_key)
			if not file or file.get_error() != OK:
				printerr("Failed to open encrypted pack-referenced file: " + str(file.get_error() if file else "Unknown error"))
				return null
		else:
			file = PackSourceCustom.create_file_access_pck(p_path, p_file, p_decryption_key)
			if not file or file.get_error() != OK:
				printerr("Failed to create file access pack-referenced file: " + str(file.get_error() if file else "Unknown error"))
				return null
	if GDREPackedData.has_delta_patches(p_path):
		var file_patched = FileAccessPatchedGDRE.create(file)
		if not file_patched or file_patched.get_error() != OK:
			printerr("Failed to open delta patched pack-referenced file: " + str(file_patched.get_error() if file_patched else "Unknown error"))
			return null
		file = file_patched

	return file

	return null
