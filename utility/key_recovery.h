#pragma once

#include "core/io/file_access.h"
#include "core/templates/vector.h"

class GDREKeyRecovery {
public:
	// Scans p_path for a 32 byte key that decrypts the index of the pack it contains.
	// Returns an empty array when the file has no encrypted index or no key is found.
	static Vector<uint8_t> recover_key(const String &p_path);
};
