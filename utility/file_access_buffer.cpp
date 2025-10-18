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

#include "file_access_buffer.h"

#include "core/os/memory.h"
#include "core/templates/vector.h"

Ref<FileAccess> FileAccessBuffer::create(ResizeBehavior p_resize_behavior) {
	return memnew(FileAccessBuffer(p_resize_behavior));
}

bool FileAccessBuffer::file_exists(const String &p_name) {
	return false;
}

Error FileAccessBuffer::open_new() {
	data.clear();
	pos = 0;
	real_size = 0;
	return OK;
}

Error FileAccessBuffer::open_custom(const Vector<uint8_t> &p_data) {
	data = p_data;
	pos = 0;
	real_size = p_data.size();
	return OK;
}

Error FileAccessBuffer::open_internal(const String &p_path, int p_mode_flags) {
	ERR_FAIL_V(ERR_UNAVAILABLE);
}

bool FileAccessBuffer::is_open() const {
	return true;
}

void FileAccessBuffer::seek(uint64_t p_position) {
	pos = p_position;
}

void FileAccessBuffer::seek_end(int64_t p_position) {
	pos = real_size + p_position;
}

uint64_t FileAccessBuffer::get_position() const {
	return pos;
}

uint64_t FileAccessBuffer::get_length() const {
	return real_size;
}

bool FileAccessBuffer::eof_reached() const {
	return pos >= static_cast<uint64_t>(real_size);
}

uint64_t FileAccessBuffer::get_buffer(uint8_t *p_dst, uint64_t p_length) const {
	if (!p_length) {
		return 0;
	}

	ERR_FAIL_NULL_V(p_dst, -1);

	uint64_t left = (uint64_t)MAX((int64_t)real_size - (int64_t)pos, 0LL);
	uint64_t read = MIN(p_length, left);

	if (read < p_length) {
		WARN_PRINT("Reading less data than requested");
	}

	memcpy(p_dst, &data[pos], read);
	pos += read;

	return read;
}

Error FileAccessBuffer::get_error() const {
	return pos >= static_cast<uint64_t>(real_size) ? ERR_FILE_EOF : OK;
}

Error FileAccessBuffer::resize(int64_t p_length) {
	data.resize_initialized(p_length);
	real_size = p_length;
	return OK;
}

void FileAccessBuffer::flush() {
}

bool FileAccessBuffer::store_buffer(const uint8_t *p_src, uint64_t p_length) {
	if (!p_length) {
		return true;
	}
	ERR_FAIL_NULL_V(p_src, false);
	real_size = MAX(real_size, pos + p_length);
	// check if data is large enough
	if (pos + p_length > static_cast<uint64_t>(data.size())) {
		if (resize_behavior == RESIZE_STRICT) {
			data.resize_uninitialized(pos + p_length);
		} else { // RESIZE_OPTIMIZED
			data.resize_uninitialized((pos + p_length + (MIN(p_length, static_cast<uint64_t>(16 * 1024)))));
		}
	}

	memcpy(data.ptrw() + pos, p_src, p_length);
	pos += p_length;

	return true;
}

String FileAccessBuffer::get_as_utf8_string(bool p_skip_cr) const {
	String s;
	s.append_utf8((const char *)data.ptr() + pos, real_size - pos, p_skip_cr);
	return s;
}

String FileAccessBuffer::whole_file_as_utf8_string(bool p_skip_cr) const {
	String s;
	s.append_utf8((const char *)data.ptr(), real_size, p_skip_cr);
	return s;
}

Error FileAccessBuffer::reserve(int64_t p_length) {
	if (p_length > data.size()) {
		data.resize_uninitialized(p_length);
	}
	return OK;
}

void FileAccessBuffer::set_auto_resize_behavior(ResizeBehavior p_resize_behavior) {
	resize_behavior = p_resize_behavior;
}

Vector<uint8_t> FileAccessBuffer::get_data() const {
	return data;
}

FileAccessBuffer::FileAccessBuffer(ResizeBehavior p_resize_behavior) :
		resize_behavior(p_resize_behavior) {}
