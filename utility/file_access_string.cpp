/**************************************************************************/
/*  file_access_buffer.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "file_access_string.h"

#include "core/os/memory.h"
#include "core/templates/vector.h"

Ref<FileAccess> FileAccessString::create() {
	return memnew(FileAccessString());
}

bool FileAccessString::file_exists(const String &p_name) {
	return false;
}

Error FileAccessString::open_new() {
	data.clear();
	pos = 0;
	return OK;
}

Error FileAccessString::open_custom(const String &p_data) {
	data = p_data;
	pos = 0;
	return OK;
}

Error FileAccessString::open_internal(const String &p_path, int p_mode_flags) {
	path = p_path;
	if (p_mode_flags == FileAccess::WRITE) {
		return open_new();
	}
	pos = 0;
	return OK;
}

String FileAccessString::get_path() const {
	return path;
}

String FileAccessString::get_path_absolute() const {
	return path;
}

bool FileAccessString::is_open() const {
	return true;
}

void FileAccessString::seek(uint64_t p_position) {
	pos = p_position;
}

void FileAccessString::seek_end(int64_t p_position) {
	pos = data.length() + p_position;
}

uint64_t FileAccessString::get_position() const {
	return pos;
}

uint64_t FileAccessString::get_length() const {
	return data.length();
}

bool FileAccessString::eof_reached() const {
	return pos >= static_cast<uint64_t>(data.length());
}

uint64_t FileAccessString::get_buffer(uint8_t *p_dst, uint64_t p_length) const {
	ERR_FAIL_V_MSG(0, "Not implemented");
}

Error FileAccessString::get_error() const {
	return pos >= static_cast<uint64_t>(data.length()) ? ERR_FILE_EOF : OK;
}

Error FileAccessString::resize(int64_t p_length) {
	data.resize_uninitialized(p_length);
	return OK;
}

void FileAccessString::flush() {
}

bool FileAccessString::store_buffer(const uint8_t *p_src, uint64_t p_length) {
	// ERR_FAIL_V_MSG(false, "Not implemented");
	// WARN_PRINT_ONCE("FileAccessString::store_buffer() shouldn't be called!");
	String s = String::utf8((const char *)p_src, p_length);
	return store_string(s);
}

bool FileAccessString::store_8(uint8_t p_dest) {
	if (unlikely(p_dest >= 127)) {
		WARN_PRINT_ONCE("FileAccessString::store_8() called with a character that is not ASCII!");
	}
	data += (char32_t)p_dest;
	pos += 1;
	return true;
}

bool FileAccessString::store_string(const String &p_string) {
	data = data.insert(pos, p_string);
	pos += p_string.length();
	return true;
}

bool FileAccessString::store_line(const String &p_line) {
	return store_string(p_line + "\n");
}

String FileAccessString::get_as_utf8_string() const {
	String s = data.substr(pos);
	return data;
}

String FileAccessString::whole_file_as_utf8_string() const {
	return data;
}

Error FileAccessString::reserve(int64_t p_length) {
	data.reserve(p_length);
	return OK;
}

Vector<uint8_t> FileAccessString::get_data() const {
	CharString cs = data.utf8();
	Vector<uint8_t> ret;
	ret.resize_uninitialized(cs.length());
	memcpy(ret.ptrw(), cs.ptr(), cs.length());
	return ret;
}
