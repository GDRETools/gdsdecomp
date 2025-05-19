/**************************************************************************/
/*  mesh.cpp                                                              */
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

#include "fake_mesh.h"

#include "core/math/convex_hull.h"
#include "core/templates/pair.h"
#include "scene/resources/surface_tool.h"

#ifndef PHYSICS_3D_DISABLED
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#endif // PHYSICS_3D_DISABLED

#include "compat/resource_loader_compat.h"
#include "core/io/missing_resource.h"
#include "utility/resource_info.h"

namespace {
Ref<Material> get_material(Variant d, ResourceInfo::LoadType load_type) {
	Ref<MissingResource> mr = d;
	if (mr.is_valid()) {
		return ResourceCompatConverter::get_real_from_missing_resource(mr, load_type);
	} else {
		return d;
	}
}

} //namespace

enum OldArrayType {
	OLD_ARRAY_VERTEX,
	OLD_ARRAY_NORMAL,
	OLD_ARRAY_TANGENT,
	OLD_ARRAY_COLOR,
	OLD_ARRAY_TEX_UV,
	OLD_ARRAY_TEX_UV2,
	OLD_ARRAY_BONES,
	OLD_ARRAY_WEIGHTS,
	OLD_ARRAY_INDEX,
	OLD_ARRAY_MAX,
};

enum OldArrayFormat {
	/* OLD_ARRAY FORMAT FLAGS */
	OLD_ARRAY_FORMAT_VERTEX = 1 << OLD_ARRAY_VERTEX, // mandatory
	OLD_ARRAY_FORMAT_NORMAL = 1 << OLD_ARRAY_NORMAL,
	OLD_ARRAY_FORMAT_TANGENT = 1 << OLD_ARRAY_TANGENT,
	OLD_ARRAY_FORMAT_COLOR = 1 << OLD_ARRAY_COLOR,
	OLD_ARRAY_FORMAT_TEX_UV = 1 << OLD_ARRAY_TEX_UV,
	OLD_ARRAY_FORMAT_TEX_UV2 = 1 << OLD_ARRAY_TEX_UV2,
	OLD_ARRAY_FORMAT_BONES = 1 << OLD_ARRAY_BONES,
	OLD_ARRAY_FORMAT_WEIGHTS = 1 << OLD_ARRAY_WEIGHTS,
	OLD_ARRAY_FORMAT_INDEX = 1 << OLD_ARRAY_INDEX,

	OLD_ARRAY_COMPRESS_BASE = (OLD_ARRAY_INDEX + 1),
	OLD_ARRAY_COMPRESS_VERTEX = 1 << (OLD_ARRAY_VERTEX + (int32_t)OLD_ARRAY_COMPRESS_BASE), // mandatory
	OLD_ARRAY_COMPRESS_NORMAL = 1 << (OLD_ARRAY_NORMAL + (int32_t)OLD_ARRAY_COMPRESS_BASE),
	OLD_ARRAY_COMPRESS_TANGENT = 1 << (OLD_ARRAY_TANGENT + (int32_t)OLD_ARRAY_COMPRESS_BASE),
	OLD_ARRAY_COMPRESS_COLOR = 1 << (OLD_ARRAY_COLOR + (int32_t)OLD_ARRAY_COMPRESS_BASE),
	OLD_ARRAY_COMPRESS_TEX_UV = 1 << (OLD_ARRAY_TEX_UV + (int32_t)OLD_ARRAY_COMPRESS_BASE),
	OLD_ARRAY_COMPRESS_TEX_UV2 = 1 << (OLD_ARRAY_TEX_UV2 + (int32_t)OLD_ARRAY_COMPRESS_BASE),
	OLD_ARRAY_COMPRESS_BONES = 1 << (OLD_ARRAY_BONES + (int32_t)OLD_ARRAY_COMPRESS_BASE),
	OLD_ARRAY_COMPRESS_WEIGHTS = 1 << (OLD_ARRAY_WEIGHTS + (int32_t)OLD_ARRAY_COMPRESS_BASE),
	OLD_ARRAY_COMPRESS_INDEX = 1 << (OLD_ARRAY_INDEX + (int32_t)OLD_ARRAY_COMPRESS_BASE),

	OLD_ARRAY_FLAG_USE_2D_VERTICES = OLD_ARRAY_COMPRESS_INDEX << 1,
	OLD_ARRAY_FLAG_USE_16_BIT_BONES = OLD_ARRAY_COMPRESS_INDEX << 2,
	OLD_ARRAY_FLAG_USE_DYNAMIC_UPDATE = OLD_ARRAY_COMPRESS_INDEX << 3,
	OLD_ARRAY_FLAG_USE_OCTAHEDRAL_COMPRESSION = OLD_ARRAY_COMPRESS_INDEX << 4,
};

#ifndef DISABLE_DEPRECATED
static Array _convert_old_array(const Array &p_old) {
	Array new_array;
	new_array.resize(Mesh::ARRAY_MAX);
	new_array[Mesh::ARRAY_VERTEX] = p_old[OLD_ARRAY_VERTEX];
	new_array[Mesh::ARRAY_NORMAL] = p_old[OLD_ARRAY_NORMAL];
	new_array[Mesh::ARRAY_TANGENT] = p_old[OLD_ARRAY_TANGENT];
	new_array[Mesh::ARRAY_COLOR] = p_old[OLD_ARRAY_COLOR];
	new_array[Mesh::ARRAY_TEX_UV] = p_old[OLD_ARRAY_TEX_UV];
	new_array[Mesh::ARRAY_TEX_UV2] = p_old[OLD_ARRAY_TEX_UV2];
	new_array[Mesh::ARRAY_BONES] = p_old[OLD_ARRAY_BONES];
	new_array[Mesh::ARRAY_WEIGHTS] = p_old[OLD_ARRAY_WEIGHTS];
	new_array[Mesh::ARRAY_INDEX] = p_old[OLD_ARRAY_INDEX];
	return new_array;
}

static Mesh::PrimitiveType _old_primitives[7] = {
	Mesh::PRIMITIVE_POINTS,
	Mesh::PRIMITIVE_LINES,
	Mesh::PRIMITIVE_LINE_STRIP,
	Mesh::PRIMITIVE_LINES,
	Mesh::PRIMITIVE_TRIANGLES,
	Mesh::PRIMITIVE_TRIANGLE_STRIP,
	Mesh::PRIMITIVE_TRIANGLE_STRIP
};
#endif // DISABLE_DEPRECATED
namespace {
void _fix_array_compatibility(const Vector<uint8_t> &p_src, uint64_t p_old_format, uint64_t p_new_format, uint32_t p_elements, Vector<uint8_t> &vertex_data, Vector<uint8_t> &attribute_data, Vector<uint8_t> &skin_data) {
	uint32_t dst_vertex_stride;
	uint32_t dst_normal_tangent_stride;
	uint32_t dst_attribute_stride;
	uint32_t dst_skin_stride;
	uint32_t dst_offsets[Mesh::ARRAY_MAX];
	RenderingServer::get_singleton()->mesh_surface_make_offsets_from_format(p_new_format & (~RS::ARRAY_FORMAT_INDEX), p_elements, 0, dst_offsets, dst_vertex_stride, dst_normal_tangent_stride, dst_attribute_stride, dst_skin_stride);

	vertex_data.resize((dst_vertex_stride + dst_normal_tangent_stride) * p_elements);
	attribute_data.resize(dst_attribute_stride * p_elements);
	skin_data.resize(dst_skin_stride * p_elements);

	uint8_t *dst_vertex_ptr = vertex_data.ptrw();
	uint8_t *dst_attribute_ptr = attribute_data.ptrw();
	uint8_t *dst_skin_ptr = skin_data.ptrw();

	const uint8_t *src_vertex_ptr = p_src.ptr();
	uint32_t src_vertex_stride = p_src.size() / p_elements;

	uint32_t src_offset = 0;
	for (uint32_t j = 0; j < OLD_ARRAY_INDEX; j++) {
		if (!(p_old_format & (1ULL << j))) {
			continue;
		}
		switch (j) {
			case OLD_ARRAY_VERTEX: {
				if (p_old_format & OLD_ARRAY_FLAG_USE_2D_VERTICES) {
					if (p_old_format & OLD_ARRAY_COMPRESS_VERTEX) {
						for (uint32_t i = 0; i < p_elements; i++) {
							const uint16_t *src = (const uint16_t *)&src_vertex_ptr[i * src_vertex_stride];
							float *dst = (float *)&dst_vertex_ptr[i * dst_vertex_stride];
							dst[0] = Math::half_to_float(src[0]);
							dst[1] = Math::half_to_float(src[1]);
						}
						src_offset += sizeof(uint16_t) * 2;
					} else {
						for (uint32_t i = 0; i < p_elements; i++) {
							const float *src = (const float *)&src_vertex_ptr[i * src_vertex_stride];
							float *dst = (float *)&dst_vertex_ptr[i * dst_vertex_stride];
							dst[0] = src[0];
							dst[1] = src[1];
						}
						src_offset += sizeof(float) * 2;
					}
				} else {
					if (p_old_format & OLD_ARRAY_COMPRESS_VERTEX) {
						for (uint32_t i = 0; i < p_elements; i++) {
							const uint16_t *src = (const uint16_t *)&src_vertex_ptr[i * src_vertex_stride];
							float *dst = (float *)&dst_vertex_ptr[i * dst_vertex_stride];
							dst[0] = Math::half_to_float(src[0]);
							dst[1] = Math::half_to_float(src[1]);
							dst[2] = Math::half_to_float(src[2]);
						}
						src_offset += sizeof(uint16_t) * 4; //+pad
					} else {
						for (uint32_t i = 0; i < p_elements; i++) {
							const float *src = (const float *)&src_vertex_ptr[i * src_vertex_stride];
							float *dst = (float *)&dst_vertex_ptr[i * dst_vertex_stride];
							dst[0] = src[0];
							dst[1] = src[1];
							dst[2] = src[2];
						}
						src_offset += sizeof(float) * 3;
					}
				}
			} break;
			case OLD_ARRAY_NORMAL: {
				if (p_old_format & OLD_ARRAY_FLAG_USE_OCTAHEDRAL_COMPRESSION) {
					if ((p_old_format & OLD_ARRAY_COMPRESS_NORMAL) && (p_old_format & OLD_ARRAY_FORMAT_TANGENT) && (p_old_format & OLD_ARRAY_COMPRESS_TANGENT)) {
						for (uint32_t i = 0; i < p_elements; i++) {
							const int8_t *src = (const int8_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
							int16_t *dst = (int16_t *)&dst_vertex_ptr[i * dst_normal_tangent_stride + dst_offsets[Mesh::ARRAY_NORMAL]];

							dst[0] = (int16_t)CLAMP(src[0] / 127.0f * 32767, -32768, 32767);
							dst[1] = (int16_t)CLAMP(src[1] / 127.0f * 32767, -32768, 32767);
						}
						src_offset += sizeof(int8_t) * 2;
					} else {
						for (uint32_t i = 0; i < p_elements; i++) {
							const int16_t *src = (const int16_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
							int16_t *dst = (int16_t *)&dst_vertex_ptr[i * dst_normal_tangent_stride + dst_offsets[Mesh::ARRAY_NORMAL]];

							dst[0] = src[0];
							dst[1] = src[1];
						}
						src_offset += sizeof(int16_t) * 2;
					}
				} else { // No Octahedral compression
					if (p_old_format & OLD_ARRAY_COMPRESS_NORMAL) {
						for (uint32_t i = 0; i < p_elements; i++) {
							const int8_t *src = (const int8_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
							const Vector3 original_normal(src[0], src[1], src[2]);
							Vector2 res = original_normal.octahedron_encode();

							uint16_t *dst = (uint16_t *)&dst_vertex_ptr[i * dst_normal_tangent_stride + dst_offsets[Mesh::ARRAY_NORMAL]];
							dst[0] = (uint16_t)CLAMP(res.x * 65535, 0, 65535);
							dst[1] = (uint16_t)CLAMP(res.y * 65535, 0, 65535);
						}
						src_offset += sizeof(uint8_t) * 4; // 1 byte padding
					} else {
						for (uint32_t i = 0; i < p_elements; i++) {
							const float *src = (const float *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
							const Vector3 original_normal(src[0], src[1], src[2]);
							Vector2 res = original_normal.octahedron_encode();

							uint16_t *dst = (uint16_t *)&dst_vertex_ptr[i * dst_normal_tangent_stride + dst_offsets[Mesh::ARRAY_NORMAL]];
							dst[0] = (uint16_t)CLAMP(res.x * 65535, 0, 65535);
							dst[1] = (uint16_t)CLAMP(res.y * 65535, 0, 65535);
						}
						src_offset += sizeof(float) * 3;
					}
				}

			} break;
			case OLD_ARRAY_TANGENT: {
				if (p_old_format & OLD_ARRAY_FLAG_USE_OCTAHEDRAL_COMPRESSION) {
					if (p_old_format & OLD_ARRAY_COMPRESS_TANGENT) { // int8 SNORM -> uint16 UNORM
						for (uint32_t i = 0; i < p_elements; i++) {
							const int8_t *src = (const int8_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
							uint16_t *dst = (uint16_t *)&dst_vertex_ptr[i * dst_normal_tangent_stride + dst_offsets[Mesh::ARRAY_TANGENT]];

							dst[0] = (uint16_t)CLAMP((src[0] / 127.0f * .5f + .5f) * 65535, 0, 65535);
							dst[1] = (uint16_t)CLAMP((src[1] / 127.0f * .5f + .5f) * 65535, 0, 65535);
						}
						src_offset += sizeof(uint8_t) * 2;
					} else { // int16 SNORM -> uint16 UNORM
						for (uint32_t i = 0; i < p_elements; i++) {
							const int16_t *src = (const int16_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
							uint16_t *dst = (uint16_t *)&dst_vertex_ptr[i * dst_normal_tangent_stride + dst_offsets[Mesh::ARRAY_TANGENT]];

							dst[0] = (uint16_t)CLAMP((src[0] / 32767.0f * .5f + .5f) * 65535, 0, 65535);
							dst[1] = (uint16_t)CLAMP((src[1] / 32767.0f * .5f + .5f) * 65535, 0, 65535);
						}
						src_offset += sizeof(uint16_t) * 2;
					}
				} else { // No Octahedral compression
					if (p_old_format & OLD_ARRAY_COMPRESS_TANGENT) {
						for (uint32_t i = 0; i < p_elements; i++) {
							const int8_t *src = (const int8_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
							const Vector3 original_tangent(src[0], src[1], src[2]);
							Vector2 res = original_tangent.octahedron_tangent_encode(src[3]);

							uint16_t *dst = (uint16_t *)&dst_vertex_ptr[i * dst_normal_tangent_stride + dst_offsets[Mesh::ARRAY_NORMAL]];
							dst[0] = (uint16_t)CLAMP(res.x * 65535, 0, 65535);
							dst[1] = (uint16_t)CLAMP(res.y * 65535, 0, 65535);
							if (dst[0] == 0 && dst[1] == 65535) {
								// (1, 1) and (0, 1) decode to the same value, but (0, 1) messes with our compression detection.
								// So we sanitize here.
								dst[0] = 65535;
							}
						}
						src_offset += sizeof(uint8_t) * 4;
					} else {
						for (uint32_t i = 0; i < p_elements; i++) {
							const float *src = (const float *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
							const Vector3 original_tangent(src[0], src[1], src[2]);
							Vector2 res = original_tangent.octahedron_tangent_encode(src[3]);

							uint16_t *dst = (uint16_t *)&dst_vertex_ptr[i * dst_normal_tangent_stride + dst_offsets[Mesh::ARRAY_NORMAL]];
							dst[0] = (uint16_t)CLAMP(res.x * 65535, 0, 65535);
							dst[1] = (uint16_t)CLAMP(res.y * 65535, 0, 65535);
							if (dst[0] == 0 && dst[1] == 65535) {
								// (1, 1) and (0, 1) decode to the same value, but (0, 1) messes with our compression detection.
								// So we sanitize here.
								dst[0] = 65535;
							}
						}
						src_offset += sizeof(float) * 4;
					}
				}
			} break;
			case OLD_ARRAY_COLOR: {
				if (p_old_format & OLD_ARRAY_COMPRESS_COLOR) {
					for (uint32_t i = 0; i < p_elements; i++) {
						const uint32_t *src = (const uint32_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
						uint32_t *dst = (uint32_t *)&dst_attribute_ptr[i * dst_attribute_stride + dst_offsets[Mesh::ARRAY_COLOR]];

						*dst = *src;
					}
					src_offset += sizeof(uint32_t);
				} else {
					for (uint32_t i = 0; i < p_elements; i++) {
						const float *src = (const float *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
						uint8_t *dst = (uint8_t *)&dst_attribute_ptr[i * dst_attribute_stride + dst_offsets[Mesh::ARRAY_COLOR]];

						dst[0] = uint8_t(CLAMP(src[0] * 255.0, 0.0, 255.0));
						dst[1] = uint8_t(CLAMP(src[1] * 255.0, 0.0, 255.0));
						dst[2] = uint8_t(CLAMP(src[2] * 255.0, 0.0, 255.0));
						dst[3] = uint8_t(CLAMP(src[3] * 255.0, 0.0, 255.0));
					}
					src_offset += sizeof(float) * 4;
				}
			} break;
			case OLD_ARRAY_TEX_UV: {
				if (p_old_format & OLD_ARRAY_COMPRESS_TEX_UV) {
					for (uint32_t i = 0; i < p_elements; i++) {
						const uint16_t *src = (const uint16_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
						float *dst = (float *)&dst_attribute_ptr[i * dst_attribute_stride + dst_offsets[Mesh::ARRAY_TEX_UV]];

						dst[0] = Math::half_to_float(src[0]);
						dst[1] = Math::half_to_float(src[1]);
					}
					src_offset += sizeof(uint16_t) * 2;
				} else {
					for (uint32_t i = 0; i < p_elements; i++) {
						const float *src = (const float *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
						float *dst = (float *)&dst_attribute_ptr[i * dst_attribute_stride + dst_offsets[Mesh::ARRAY_TEX_UV]];

						dst[0] = src[0];
						dst[1] = src[1];
					}
					src_offset += sizeof(float) * 2;
				}

			} break;
			case OLD_ARRAY_TEX_UV2: {
				if (p_old_format & OLD_ARRAY_COMPRESS_TEX_UV2) {
					for (uint32_t i = 0; i < p_elements; i++) {
						const uint16_t *src = (const uint16_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
						float *dst = (float *)&dst_attribute_ptr[i * dst_attribute_stride + dst_offsets[Mesh::ARRAY_TEX_UV2]];

						dst[0] = Math::half_to_float(src[0]);
						dst[1] = Math::half_to_float(src[1]);
					}
					src_offset += sizeof(uint16_t) * 2;
				} else {
					for (uint32_t i = 0; i < p_elements; i++) {
						const float *src = (const float *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
						float *dst = (float *)&dst_attribute_ptr[i * dst_attribute_stride + dst_offsets[Mesh::ARRAY_TEX_UV2]];

						dst[0] = src[0];
						dst[1] = src[1];
					}
					src_offset += sizeof(float) * 2;
				}
			} break;
			case OLD_ARRAY_BONES: {
				if (p_old_format & OLD_ARRAY_FLAG_USE_16_BIT_BONES) {
					for (uint32_t i = 0; i < p_elements; i++) {
						const uint16_t *src = (const uint16_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
						uint16_t *dst = (uint16_t *)&dst_skin_ptr[i * dst_skin_stride + dst_offsets[Mesh::ARRAY_BONES]];

						dst[0] = src[0];
						dst[1] = src[1];
						dst[2] = src[2];
						dst[3] = src[3];
					}
					src_offset += sizeof(uint16_t) * 4;
				} else {
					for (uint32_t i = 0; i < p_elements; i++) {
						const uint8_t *src = (const uint8_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
						uint16_t *dst = (uint16_t *)&dst_skin_ptr[i * dst_skin_stride + dst_offsets[Mesh::ARRAY_BONES]];

						dst[0] = src[0];
						dst[1] = src[1];
						dst[2] = src[2];
						dst[3] = src[3];
					}
					src_offset += sizeof(uint8_t) * 4;
				}
			} break;
			case OLD_ARRAY_WEIGHTS: {
				if (p_old_format & OLD_ARRAY_COMPRESS_WEIGHTS) {
					for (uint32_t i = 0; i < p_elements; i++) {
						const uint16_t *src = (const uint16_t *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
						uint16_t *dst = (uint16_t *)&dst_skin_ptr[i * dst_skin_stride + dst_offsets[Mesh::ARRAY_WEIGHTS]];

						dst[0] = src[0];
						dst[1] = src[1];
						dst[2] = src[2];
						dst[3] = src[3];
					}
					src_offset += sizeof(uint16_t) * 4;
				} else {
					for (uint32_t i = 0; i < p_elements; i++) {
						const float *src = (const float *)&src_vertex_ptr[i * src_vertex_stride + src_offset];
						uint16_t *dst = (uint16_t *)&dst_skin_ptr[i * dst_skin_stride + dst_offsets[Mesh::ARRAY_WEIGHTS]];

						dst[0] = uint16_t(CLAMP(src[0] * 65535.0, 0, 65535.0));
						dst[1] = uint16_t(CLAMP(src[1] * 65535.0, 0, 65535.0));
						dst[2] = uint16_t(CLAMP(src[2] * 65535.0, 0, 65535.0));
						dst[3] = uint16_t(CLAMP(src[3] * 65535.0, 0, 65535.0));
					}
					src_offset += sizeof(float) * 4;
				}
			} break;
			default: {
			}
		}
	}
}
} //namespace

bool FakeMesh::_set(const StringName &p_name, const Variant &p_value) {
	String sname = p_name;

	if (sname.begins_with("surface_")) {
		int sl = sname.find_char('/');
		if (sl == -1) {
			return false;
		}
		int idx = sname.substr(8, sl - 8).to_int();

		String what = sname.get_slicec('/', 1);
		if (what == "material") {
			surfaces.write[idx].material = get_material(p_value, load_type);
		} else if (what == "name") {
			surfaces.write[idx].name = p_value;
		}
		return true;
	}

#ifndef DISABLE_DEPRECATED
	// Kept for compatibility from 3.x to 4.0.
	if (!sname.begins_with("surfaces")) {
		return false;
	}

	WARN_DEPRECATED_MSG(vformat(
			"Mesh uses old surface format, which is deprecated (and loads slower). Consider re-importing or re-saving the scene. Path: \"%s\"",
			get_path()));

	int idx = sname.get_slicec('/', 1).to_int();
	String what = sname.get_slicec('/', 2);

	if (idx == surfaces.size()) {
		//create
		Dictionary d = p_value;
		ERR_FAIL_COND_V(!d.has("primitive"), false);

		if (d.has("arrays")) {
			//oldest format (2.x)
			ERR_FAIL_COND_V(!d.has("morph_arrays"), false);
			Array morph_arrays = d["morph_arrays"];
			for (int i = 0; i < morph_arrays.size(); i++) {
				morph_arrays[i] = _convert_old_array(morph_arrays[i]);
			}
			add_surface_from_arrays(_old_primitives[int(d["primitive"])], _convert_old_array(d["arrays"]), morph_arrays);

		} else if (d.has("array_data")) {
			//print_line("array data (old style");
			//older format (3.x)
			Vector<uint8_t> array_data = d["array_data"];
			Vector<uint8_t> array_index_data;
			if (d.has("array_index_data")) {
				array_index_data = d["array_index_data"];
			}

			ERR_FAIL_COND_V(!d.has("format"), false);
			uint64_t old_format = d["format"];

			uint32_t primitive = d["primitive"];

			primitive = _old_primitives[primitive]; //compatibility

			ERR_FAIL_COND_V(!d.has("vertex_count"), false);
			int vertex_count = d["vertex_count"];

			uint64_t new_format = ARRAY_FORMAT_VERTEX | ARRAY_FLAG_FORMAT_CURRENT_VERSION;

			if (old_format & OLD_ARRAY_FORMAT_NORMAL) {
				new_format |= ARRAY_FORMAT_NORMAL;
			}
			if (old_format & OLD_ARRAY_FORMAT_TANGENT) {
				new_format |= ARRAY_FORMAT_TANGENT;
			}
			if (old_format & OLD_ARRAY_FORMAT_COLOR) {
				new_format |= ARRAY_FORMAT_COLOR;
			}
			if (old_format & OLD_ARRAY_FORMAT_TEX_UV) {
				new_format |= ARRAY_FORMAT_TEX_UV;
			}
			if (old_format & OLD_ARRAY_FORMAT_TEX_UV2) {
				new_format |= ARRAY_FORMAT_TEX_UV2;
			}
			if (old_format & OLD_ARRAY_FORMAT_BONES) {
				new_format |= ARRAY_FORMAT_BONES;
			}
			if (old_format & OLD_ARRAY_FORMAT_WEIGHTS) {
				new_format |= ARRAY_FORMAT_WEIGHTS;
			}
			if (old_format & OLD_ARRAY_FORMAT_INDEX) {
				new_format |= ARRAY_FORMAT_INDEX;
			}
			if (old_format & OLD_ARRAY_FLAG_USE_2D_VERTICES) {
				new_format |= OLD_ARRAY_FLAG_USE_2D_VERTICES;
			}

			Vector<uint8_t> vertex_array;
			Vector<uint8_t> attribute_array;
			Vector<uint8_t> skin_array;

			_fix_array_compatibility(array_data, old_format, new_format, vertex_count, vertex_array, attribute_array, skin_array);

			int index_count = 0;
			if (d.has("index_count")) {
				index_count = d["index_count"];
			}

			Vector<uint8_t> blend_shapes_new;

			if (d.has("blend_shape_data")) {
				Array blend_shape_data = d["blend_shape_data"];
				for (int i = 0; i < blend_shape_data.size(); i++) {
					Vector<uint8_t> blend_vertex_array;
					Vector<uint8_t> blend_attribute_array;
					Vector<uint8_t> blend_skin_array;

					Vector<uint8_t> shape = blend_shape_data[i];
					_fix_array_compatibility(shape, old_format, new_format, vertex_count, blend_vertex_array, blend_attribute_array, blend_skin_array);

					blend_shapes_new.append_array(blend_vertex_array);
				}
			}

			//clear unused flags
			print_verbose("Mesh format pre-conversion: " + itos(old_format));

			print_verbose("Mesh format post-conversion: " + itos(new_format));

			ERR_FAIL_COND_V(!d.has("aabb"), false);
			AABB aabb_new = d["aabb"];

			Vector<AABB> bone_aabb;
			if (d.has("skeleton_aabb")) {
				Array baabb = d["skeleton_aabb"];
				bone_aabb.resize(baabb.size());

				for (int i = 0; i < baabb.size(); i++) {
					bone_aabb.write[i] = baabb[i];
				}
			}

			add_surface(new_format, PrimitiveType(primitive), vertex_array, attribute_array, skin_array, vertex_count, array_index_data, index_count, aabb_new, blend_shapes_new, bone_aabb);

		} else {
			ERR_FAIL_V(false);
		}

		if (d.has("material")) {
			surfaces.write[idx].material = get_material(d["material"], load_type);
		}
		if (d.has("name")) {
			surfaces.write[idx].name = d["name"];
		}

		return true;
	}
#endif // DISABLE_DEPRECATED

	return false;
}

void FakeMesh::_set_blend_shape_names(const PackedStringArray &p_names) {
	ERR_FAIL_COND(surfaces.size() > 0);

	blend_shapes.resize(p_names.size());
	for (int i = 0; i < p_names.size(); i++) {
		blend_shapes.write[i] = p_names[i];
	}

	// if (mesh.is_valid()) {
	// 	RS::get_singleton()->mesh_set_blend_shape_count(mesh, blend_shapes.size());
	// }
}

PackedStringArray FakeMesh::_get_blend_shape_names() const {
	PackedStringArray sarr;
	sarr.resize(blend_shapes.size());
	for (int i = 0; i < blend_shapes.size(); i++) {
		sarr.write[i] = blend_shapes[i];
	}
	return sarr;
}

Array FakeMesh::_get_surfaces() const {
	if (mesh.is_null()) {
		return Array();
	}

	Array ret;
	for (int i = 0; i < surfaces.size(); i++) {
		// RenderingServer::SurfaceData surface = RS::get_singleton()->mesh_get_surface(mesh, i);
		const RenderingServer::SurfaceData &surface = surface_data[i];
		Dictionary data;
		data["format"] = surface.format;
		data["primitive"] = surface.primitive;
		data["vertex_data"] = surface.vertex_data;
		data["vertex_count"] = surface.vertex_count;
		if (surface.skin_data.size()) {
			data["skin_data"] = surface.skin_data;
		}
		if (surface.attribute_data.size()) {
			data["attribute_data"] = surface.attribute_data;
		}
		data["aabb"] = surface.aabb;
		data["uv_scale"] = surface.uv_scale;
		if (surface.index_count) {
			data["index_data"] = surface.index_data;
			data["index_count"] = surface.index_count;
		};

		Array lods;
		for (int j = 0; j < surface.lods.size(); j++) {
			lods.push_back(surface.lods[j].edge_length);
			lods.push_back(surface.lods[j].index_data);
		}

		if (lods.size()) {
			data["lods"] = lods;
		}

		Array bone_aabbs;
		for (int j = 0; j < surface.bone_aabbs.size(); j++) {
			bone_aabbs.push_back(surface.bone_aabbs[j]);
		}
		if (bone_aabbs.size()) {
			data["bone_aabbs"] = bone_aabbs;
		}

		if (surface.blend_shape_data.size()) {
			data["blend_shapes"] = surface.blend_shape_data;
		}

		if (surfaces[i].material.is_valid()) {
			data["material"] = surfaces[i].material;
		}

		if (!surfaces[i].name.is_empty()) {
			data["name"] = surfaces[i].name;
		}

		if (surfaces[i].is_2d) {
			data["2d"] = true;
		}

		ret.push_back(data);
	}

	return ret;
}

void FakeMesh::_create_if_empty() const {
	// if (!mesh.is_valid()) {
	// 	mesh = RS::get_singleton()->mesh_create();
	// 	RS::get_singleton()->mesh_set_blend_shape_mode(mesh, (RS::BlendShapeMode)blend_shape_mode);
	// 	RS::get_singleton()->mesh_set_blend_shape_count(mesh, blend_shapes.size());
	// 	RS::get_singleton()->mesh_set_path(mesh, get_path());
	// }
}

void FakeMesh::_set_surfaces(const Array &p_surfaces) {
	// Vector<RS::SurfaceData> surface_data;
	surface_data.clear();

	Vector<Ref<Material>> surface_materials;
	Vector<String> surface_names;
	Vector<bool> surface_2d;

	for (int i = 0; i < p_surfaces.size(); i++) {
		RS::SurfaceData surface;
		Dictionary d = p_surfaces[i];
		ERR_FAIL_COND(!d.has("format"));
		ERR_FAIL_COND(!d.has("primitive"));
		ERR_FAIL_COND(!d.has("vertex_data"));
		ERR_FAIL_COND(!d.has("vertex_count"));
		ERR_FAIL_COND(!d.has("aabb"));
		surface.format = d["format"];
		surface.primitive = RS::PrimitiveType(int(d["primitive"]));
		surface.vertex_data = d["vertex_data"];
		surface.vertex_count = d["vertex_count"];
		if (d.has("attribute_data")) {
			surface.attribute_data = d["attribute_data"];
		}
		if (d.has("skin_data")) {
			surface.skin_data = d["skin_data"];
		}
		surface.aabb = d["aabb"];

		if (d.has("uv_scale")) {
			surface.uv_scale = d["uv_scale"];
		}

		if (d.has("index_data")) {
			ERR_FAIL_COND(!d.has("index_count"));
			surface.index_data = d["index_data"];
			surface.index_count = d["index_count"];
		}

		if (d.has("lods")) {
			Array lods = d["lods"];
			ERR_FAIL_COND(lods.size() & 1); //must be even
			for (int j = 0; j < lods.size(); j += 2) {
				RS::SurfaceData::LOD lod;
				lod.edge_length = lods[j + 0];
				lod.index_data = lods[j + 1];
				surface.lods.push_back(lod);
			}
		}

		if (d.has("bone_aabbs")) {
			Array bone_aabbs = d["bone_aabbs"];
			for (int j = 0; j < bone_aabbs.size(); j++) {
				surface.bone_aabbs.push_back(bone_aabbs[j]);
			}
		}

		if (d.has("blend_shapes")) {
			surface.blend_shape_data = d["blend_shapes"];
		}

		Ref<Material> material;
		if (d.has("material")) {
			// material = d["material"];
			material = get_material(d["material"], load_type);
			if (material.is_valid()) {
				surface.material = material->get_rid();
			}
		}

		String surf_name;
		if (d.has("name")) {
			surf_name = d["name"];
		}

		bool _2d = false;
		if (d.has("2d")) {
			_2d = d["2d"];
		}

#ifndef DISABLE_DEPRECATED
		uint64_t surface_version = surface.format & (ARRAY_FLAG_FORMAT_VERSION_MASK << ARRAY_FLAG_FORMAT_VERSION_SHIFT);
		if (surface_version != ARRAY_FLAG_FORMAT_CURRENT_VERSION) {
			RS::get_singleton()->fix_surface_compatibility(surface, get_path());
			surface_version = surface.format & (RS::ARRAY_FLAG_FORMAT_VERSION_MASK << RS::ARRAY_FLAG_FORMAT_VERSION_SHIFT);
			ERR_FAIL_COND_MSG(surface_version != RS::ARRAY_FLAG_FORMAT_CURRENT_VERSION,
					vformat("Surface version provided (%d) does not match current version (%d).",
							(surface_version >> RS::ARRAY_FLAG_FORMAT_VERSION_SHIFT) & RS::ARRAY_FLAG_FORMAT_VERSION_MASK,
							(RS::ARRAY_FLAG_FORMAT_CURRENT_VERSION >> RS::ARRAY_FLAG_FORMAT_VERSION_SHIFT) & RS::ARRAY_FLAG_FORMAT_VERSION_MASK));
		}
#endif

		surface_data.push_back(surface);
		surface_materials.push_back(material);
		surface_names.push_back(surf_name);
		surface_2d.push_back(_2d);
	}

	// if (mesh.is_valid()) {
	// 	//if mesh exists, it needs to be updated
	// 	RS::get_singleton()->mesh_clear(mesh);
	// 	for (int i = 0; i < surface_data.size(); i++) {
	// 		RS::get_singleton()->mesh_add_surface(mesh, surface_data[i]);
	// 	}
	// } else {
	// 	// if mesh does not exist (first time this is loaded, most likely),
	// 	// we can create it with a single call, which is a lot more efficient and thread friendly
	// 	mesh = RS::get_singleton()->mesh_create_from_surfaces(surface_data, blend_shapes.size());
	// 	RS::get_singleton()->mesh_set_blend_shape_mode(mesh, (RS::BlendShapeMode)blend_shape_mode);
	// 	RS::get_singleton()->mesh_set_path(mesh, get_path());
	// }

	for (int i = 0; i < surface_data.size(); i++) {
		surface_arrays.push_back(RenderingServer::get_singleton()->mesh_create_arrays_from_surface_data(surface_data[i]));
	}

	surfaces.clear();

	aabb = AABB();
	for (int i = 0; i < surface_data.size(); i++) {
		Surface s;
		s.aabb = surface_data[i].aabb;
		if (i == 0) {
			aabb = s.aabb;
		} else {
			aabb.merge_with(s.aabb);
		}

		s.material = surface_materials[i];
		s.is_2d = surface_2d[i];
		s.name = surface_names[i];

		s.format = surface_data[i].format;
		s.primitive = PrimitiveType(surface_data[i].primitive);
		s.array_length = surface_data[i].vertex_count;
		s.index_array_length = surface_data[i].index_count;

		surfaces.push_back(s);
	}
}

bool FakeMesh::_get(const StringName &p_name, Variant &r_ret) const {
	if (_is_generated()) {
		return false;
	}

	String sname = p_name;
	if (sname.begins_with("surface_")) {
		int sl = sname.find_char('/');
		if (sl == -1) {
			return false;
		}
		int idx = sname.substr(8, sl - 8).to_int();
		String what = sname.get_slicec('/', 1);
		if (what == "material") {
			r_ret = surface_get_material(idx);
		} else if (what == "name") {
			r_ret = surface_get_name(idx);
		}
		return true;
	}

	return false;
}

void FakeMesh::reset_state() {
	clear_surfaces();
	clear_blend_shapes();

	aabb = AABB();
	blend_shape_mode = BLEND_SHAPE_MODE_RELATIVE;
	custom_aabb = AABB();
}

void FakeMesh::_get_property_list(List<PropertyInfo> *p_list) const {
	if (_is_generated()) {
		return;
	}

	for (int i = 0; i < surfaces.size(); i++) {
		p_list->push_back(PropertyInfo(Variant::STRING, "surface_" + itos(i) + "/name", PROPERTY_HINT_NO_NODEPATH, "", PROPERTY_USAGE_EDITOR));
		if (surfaces[i].is_2d) {
			p_list->push_back(PropertyInfo(Variant::OBJECT, "surface_" + itos(i) + "/material", PROPERTY_HINT_RESOURCE_TYPE, "CanvasItemMaterial,ShaderMaterial", PROPERTY_USAGE_EDITOR));
		} else {
			p_list->push_back(PropertyInfo(Variant::OBJECT, "surface_" + itos(i) + "/material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial", PROPERTY_USAGE_EDITOR));
		}
	}
}

void FakeMesh::_recompute_aabb() {
	// regenerate AABB
	aabb = AABB();

	for (int i = 0; i < surfaces.size(); i++) {
		if (i == 0) {
			aabb = surfaces[i].aabb;
		} else {
			aabb.merge_with(surfaces[i].aabb);
		}
	}
}

// TODO: Need to add binding to add_surface using future MeshSurfaceData object.
void FakeMesh::add_surface(BitField<ArrayFormat> p_format, PrimitiveType p_primitive, const Vector<uint8_t> &p_array, const Vector<uint8_t> &p_attribute_array, const Vector<uint8_t> &p_skin_array, int p_vertex_count, const Vector<uint8_t> &p_index_array, int p_index_count, const AABB &p_aabb, const Vector<uint8_t> &p_blend_shape_data, const Vector<AABB> &p_bone_aabbs, const Vector<RS::SurfaceData::LOD> &p_lods, const Vector4 p_uv_scale) {
	_create_if_empty();

	Surface s;
	s.aabb = p_aabb;
	s.is_2d = p_format & ARRAY_FLAG_USE_2D_VERTICES;
	s.primitive = p_primitive;
	s.array_length = p_vertex_count;
	s.index_array_length = p_index_count;
	s.format = p_format;

	surfaces.push_back(s);
	_recompute_aabb();

	RS::SurfaceData sd;
	sd.format = p_format;
	sd.primitive = RS::PrimitiveType(p_primitive);
	sd.aabb = p_aabb;
	sd.vertex_count = p_vertex_count;
	sd.vertex_data = p_array;
	sd.attribute_data = p_attribute_array;
	sd.skin_data = p_skin_array;
	sd.index_count = p_index_count;
	sd.index_data = p_index_array;
	sd.blend_shape_data = p_blend_shape_data;
	sd.bone_aabbs = p_bone_aabbs;
	sd.lods = p_lods;
	sd.uv_scale = p_uv_scale;

	surface_data.push_back(sd);
	Array arr = RenderingServer::get_singleton()->mesh_create_arrays_from_surface_data(sd);
	surface_arrays.push_back(arr);

	clear_cache();
	notify_property_list_changed();
	emit_changed();
}

void FakeMesh::add_surface_from_arrays(PrimitiveType p_primitive, const Array &p_arrays, const TypedArray<Array> &p_blend_shapes, const Dictionary &p_lods, BitField<ArrayFormat> p_flags) {
	ERR_FAIL_COND(p_blend_shapes.size() != blend_shapes.size());
	ERR_FAIL_COND(p_arrays.size() != ARRAY_MAX);

	RS::SurfaceData surface;

	Error err = RS::get_singleton()->mesh_create_surface_data_from_arrays(&surface, (RenderingServer::PrimitiveType)p_primitive, p_arrays, p_blend_shapes, p_lods, p_flags);
	ERR_FAIL_COND(err != OK);

	/* Debug code.
	print_line("format: " + itos(surface.format));
	print_line("aabb: " + surface.aabb);
	print_line("array size: " + itos(surface.vertex_data.size()));
	print_line("vertex count: " + itos(surface.vertex_count));
	print_line("index size: " + itos(surface.index_data.size()));
	print_line("index count: " + itos(surface.index_count));
	print_line("primitive: " + itos(surface.primitive));
	*/

	add_surface(surface.format, PrimitiveType(surface.primitive), surface.vertex_data, surface.attribute_data, surface.skin_data, surface.vertex_count, surface.index_data, surface.index_count, surface.aabb, surface.blend_shape_data, surface.bone_aabbs, surface.lods, surface.uv_scale);
}

Array FakeMesh::surface_get_arrays(int p_surface) const {
	ERR_FAIL_INDEX_V(p_surface, surfaces.size(), Array());
	// return RenderingServer::get_singleton()->mesh_surface_get_arrays(mesh, p_surface);
	return surface_arrays[p_surface];
}

TypedArray<Array> FakeMesh::surface_get_blend_shape_arrays(int p_surface) const {
	ERR_FAIL_INDEX_V(p_surface, surfaces.size(), TypedArray<Array>());
	const RenderingServer::SurfaceData &sd = surface_data[p_surface];
	ERR_FAIL_COND_V(sd.vertex_count == 0, Array());
	Vector<uint8_t> blend_shape_data = sd.blend_shape_data;
	if (blend_shape_data.size() > 0) {
		uint32_t bs_offsets[RS::ARRAY_MAX];
		uint32_t bs_format = (sd.format & RS::ARRAY_FORMAT_BLEND_SHAPE_MASK);
		uint32_t vertex_elem_size;
		uint32_t normal_elem_size;
		uint32_t attrib_elem_size;
		uint32_t skin_elem_size;
		RenderingServer::get_singleton()->mesh_surface_make_offsets_from_format(bs_format, sd.vertex_count, 0, bs_offsets, vertex_elem_size, normal_elem_size, attrib_elem_size, skin_elem_size);
		int divisor = (vertex_elem_size + normal_elem_size) * sd.vertex_count;
		ERR_FAIL_COND_V((blend_shape_data.size() % divisor) != 0, Array());
		uint32_t blend_shape_count = blend_shape_data.size() / divisor;
		ERR_FAIL_COND_V(blend_shape_count != blend_shapes.size(), Array());
		TypedArray<Array> blend_shape_array;
		blend_shape_array.resize(blend_shapes.size());
		for (uint32_t i = 0; i < blend_shape_count; i++) {
			Vector<uint8_t> unused;
			RS::SurfaceData fake_sd;
			fake_sd.format = bs_format;
			fake_sd.vertex_data = blend_shape_data.slice(i * divisor, (i + 1) * divisor);
			fake_sd.uv_scale = sd.uv_scale;
			fake_sd.vertex_count = sd.vertex_count;
			fake_sd.aabb = sd.aabb;
			fake_sd.uv_scale = sd.uv_scale;
			fake_sd.index_count = 0;
			blend_shape_array.set(i, RenderingServer::get_singleton()->mesh_create_arrays_from_surface_data(sd));
		}
		return blend_shape_array;
	} else {
		return TypedArray<Array>();
	}
}

Dictionary FakeMesh::surface_get_lods(int p_surface) const {
	ERR_FAIL_INDEX_V(p_surface, surfaces.size(), Dictionary());
	// return RenderingServer::get_singleton()->mesh_surface_get_lods(mesh, p_surface);
	Dictionary ret;
	auto &sd = surface_data[p_surface];
	for (int i = 0; i < sd.lods.size(); i++) {
		Vector<int> lods;
		if (sd.vertex_count <= 65536) {
			uint32_t lc = sd.lods[i].index_data.size() / 2;
			lods.resize(lc);
			const uint8_t *r = sd.lods[i].index_data.ptr();
			const uint16_t *rptr = (const uint16_t *)r;
			int *w = lods.ptrw();
			for (uint32_t j = 0; j < lc; j++) {
				w[j] = rptr[i];
			}
		} else {
			uint32_t lc = sd.lods[i].index_data.size() / 4;
			lods.resize(lc);
			const uint8_t *r = sd.lods[i].index_data.ptr();
			const uint32_t *rptr = (const uint32_t *)r;
			int *w = lods.ptrw();
			for (uint32_t j = 0; j < lc; j++) {
				w[j] = rptr[i];
			}
		}
		ret[sd.lods[i].edge_length] = lods;
	}
	return ret;
}

int FakeMesh::get_surface_count() const {
	return surfaces.size();
}

void FakeMesh::add_blend_shape(const StringName &p_name) {
	ERR_FAIL_COND_MSG(surfaces.size(), "Can't add a shape key count if surfaces are already created.");

	StringName shape_name = p_name;

	if (blend_shapes.has(shape_name)) {
		int count = 2;
		do {
			shape_name = String(p_name) + " " + itos(count);
			count++;
		} while (blend_shapes.has(shape_name));
	}

	blend_shapes.push_back(shape_name);

	// if (mesh.is_valid()) {
	// 	RS::get_singleton()->mesh_set_blend_shape_count(mesh, blend_shapes.size());
	// }
}

int FakeMesh::get_blend_shape_count() const {
	return blend_shapes.size();
}

StringName FakeMesh::get_blend_shape_name(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, blend_shapes.size(), StringName());
	return blend_shapes[p_index];
}

void FakeMesh::set_blend_shape_name(int p_index, const StringName &p_name) {
	ERR_FAIL_INDEX(p_index, blend_shapes.size());

	StringName shape_name = p_name;
	int found = blend_shapes.find(shape_name);
	if (found != -1 && found != p_index) {
		int count = 2;
		do {
			shape_name = String(p_name) + " " + itos(count);
			count++;
		} while (blend_shapes.has(shape_name));
	}

	blend_shapes.write[p_index] = shape_name;
}

void FakeMesh::clear_blend_shapes() {
	ERR_FAIL_COND_MSG(surfaces.size(), "Can't set shape key count if surfaces are already created.");

	blend_shapes.clear();

	// if (mesh.is_valid()) {
	// 	RS::get_singleton()->mesh_set_blend_shape_count(mesh, 0);
	// }
}

void FakeMesh::set_blend_shape_mode(BlendShapeMode p_mode) {
	blend_shape_mode = p_mode;
	// if (mesh.is_valid()) {
	// 	RS::get_singleton()->mesh_set_blend_shape_mode(mesh, (RS::BlendShapeMode)p_mode);
	// }
}

FakeMesh::BlendShapeMode FakeMesh::get_blend_shape_mode() const {
	return blend_shape_mode;
}

int FakeMesh::surface_get_array_len(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, surfaces.size(), -1);
	return surfaces[p_idx].array_length;
}

int FakeMesh::surface_get_array_index_len(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, surfaces.size(), -1);
	return surfaces[p_idx].index_array_length;
}

BitField<Mesh::ArrayFormat> FakeMesh::surface_get_format(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, surfaces.size(), 0);
	return surfaces[p_idx].format;
}

FakeMesh::PrimitiveType FakeMesh::surface_get_primitive_type(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, surfaces.size(), PRIMITIVE_LINES);
	return surfaces[p_idx].primitive;
}

void FakeMesh::surface_set_material(int p_idx, const Ref<Material> &p_material) {
	ERR_FAIL_INDEX(p_idx, surfaces.size());
	if (surfaces[p_idx].material == p_material) {
		return;
	}
	surfaces.write[p_idx].material = p_material;
	// RenderingServer::get_singleton()->mesh_surface_set_material(mesh, p_idx, p_material.is_null() ? RID() : p_material->get_rid());

	// emit_changed();
}

int FakeMesh::surface_find_by_name(const String &p_name) const {
	for (int i = 0; i < surfaces.size(); i++) {
		if (surfaces[i].name == p_name) {
			return i;
		}
	}
	return -1;
}

void FakeMesh::surface_set_name(int p_idx, const String &p_name) {
	ERR_FAIL_INDEX(p_idx, surfaces.size());

	surfaces.write[p_idx].name = p_name;
	// emit_changed();
}

String FakeMesh::surface_get_name(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, surfaces.size(), String());
	return surfaces[p_idx].name;
}

void FakeMesh::surface_update_vertex_region(int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	// ERR_FAIL_INDEX(p_surface, surfaces.size());
	// RS::get_singleton()->mesh_surface_update_vertex_region(mesh, p_surface, p_offset, p_data);
	// emit_changed();
}

void FakeMesh::surface_update_attribute_region(int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	// ERR_FAIL_INDEX(p_surface, surfaces.size());
	// RS::get_singleton()->mesh_surface_update_attribute_region(mesh, p_surface, p_offset, p_data);
	// emit_changed();
}

void FakeMesh::surface_update_skin_region(int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	// ERR_FAIL_INDEX(p_surface, surfaces.size());
	// RS::get_singleton()->mesh_surface_update_skin_region(mesh, p_surface, p_offset, p_data);
	// emit_changed();
}

void FakeMesh::surface_set_custom_aabb(int p_idx, const AABB &p_aabb) {
	ERR_FAIL_INDEX(p_idx, surfaces.size());
	surfaces.write[p_idx].aabb = p_aabb;
	// set custom aabb too?
	// emit_changed();
}

Ref<Material> FakeMesh::surface_get_material(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, surfaces.size(), Ref<Material>());
	return surfaces[p_idx].material;
}

RID FakeMesh::get_rid() const {
	_create_if_empty();
	return mesh;
}

AABB FakeMesh::get_aabb() const {
	return aabb;
}

void FakeMesh::clear_surfaces() {
	// if (!mesh.is_valid()) {
	// 	return;
	// }
	// RS::get_singleton()->mesh_clear(mesh);
	surfaces.clear();
	aabb = AABB();
}

void FakeMesh::surface_remove(int p_surface) {
	ERR_FAIL_INDEX(p_surface, surfaces.size());
	// RS::get_singleton()->mesh_surface_remove(mesh, p_surface);
	surfaces.remove_at(p_surface);
	surface_data.remove_at(p_surface);
	surface_arrays.remove_at(p_surface);

	clear_cache();
	_recompute_aabb();
	// notify_property_list_changed();
	// emit_changed();
}

void FakeMesh::set_custom_aabb(const AABB &p_custom) {
	_create_if_empty();
	custom_aabb = p_custom;
	// RS::get_singleton()->mesh_set_custom_aabb(mesh, custom_aabb);
	// emit_changed();
}

AABB FakeMesh::get_custom_aabb() const {
	return custom_aabb;
}

void FakeMesh::regen_normal_maps() {
	if (surfaces.is_empty()) {
		return;
	}
	Vector<Ref<SurfaceTool>> surfs;
	Vector<uint64_t> formats;
	for (int i = 0; i < get_surface_count(); i++) {
		Ref<SurfaceTool> st = memnew(SurfaceTool);
		st->create_from(Ref<FakeMesh>(this), i);
		surfs.push_back(st);
		formats.push_back(surface_get_format(i));
	}

	clear_surfaces();

	for (int i = 0; i < surfs.size(); i++) {
		surfs.write[i]->generate_tangents();
		surfs.write[i]->commit(Ref<FakeMesh>(this), formats[i]);
	}
}

//dirty hack
extern bool (*array_mesh_lightmap_unwrap_callback)(float p_texel_size, const float *p_vertices, const float *p_normals, int p_vertex_count, const int *p_indices, int p_index_count, const uint8_t *p_cache_data, bool *r_use_cache, uint8_t **r_mesh_cache, int *r_mesh_cache_size, float **r_uv, int **r_vertex, int *r_vertex_count, int **r_index, int *r_index_count, int *r_size_hint_x, int *r_size_hint_y);

namespace {
struct ArrayMeshLightmapSurface {
	Ref<Material> material;
	LocalVector<SurfaceTool::Vertex> vertices;
	Mesh::PrimitiveType primitive = Mesh::PrimitiveType::PRIMITIVE_MAX;
	uint64_t format = 0;
};
} //namespace

Error FakeMesh::lightmap_unwrap(const Transform3D &p_base_transform, float p_texel_size) {
	Vector<uint8_t> null_cache;
	return lightmap_unwrap_cached(p_base_transform, p_texel_size, null_cache, null_cache, false);
}

Error FakeMesh::lightmap_unwrap_cached(const Transform3D &p_base_transform, float p_texel_size, const Vector<uint8_t> &p_src_cache, Vector<uint8_t> &r_dst_cache, bool p_generate_cache) {
	ERR_FAIL_NULL_V(array_mesh_lightmap_unwrap_callback, ERR_UNCONFIGURED);
	ERR_FAIL_COND_V_MSG(blend_shapes.size() != 0, ERR_UNAVAILABLE, "Can't unwrap mesh with blend shapes.");
	ERR_FAIL_COND_V_MSG(p_texel_size <= 0.0f, ERR_PARAMETER_RANGE_ERROR, "Texel size must be greater than 0.");

	LocalVector<float> vertices;
	LocalVector<float> normals;
	LocalVector<int> indices;
	LocalVector<float> uv;
	LocalVector<Pair<int, int>> uv_indices;

	Vector<ArrayMeshLightmapSurface> lightmap_surfaces;

	// Keep only the scale
	Basis basis = p_base_transform.get_basis();
	Vector3 scale = Vector3(basis.get_column(0).length(), basis.get_column(1).length(), basis.get_column(2).length());

	Transform3D transform;
	transform.scale(scale);

	Basis normal_basis = transform.basis.inverse().transposed();

	for (int i = 0; i < get_surface_count(); i++) {
		ArrayMeshLightmapSurface s;
		s.primitive = surface_get_primitive_type(i);

		ERR_FAIL_COND_V_MSG(s.primitive != Mesh::PRIMITIVE_TRIANGLES, ERR_UNAVAILABLE, "Only triangles are supported for lightmap unwrap.");
		s.format = surface_get_format(i);
		ERR_FAIL_COND_V_MSG(!(s.format & ARRAY_FORMAT_NORMAL), ERR_UNAVAILABLE, "Normals are required for lightmap unwrap.");

		Array arrays = surface_get_arrays(i);
		s.material = surface_get_material(i);
		SurfaceTool::create_vertex_array_from_arrays(arrays, s.vertices, &s.format);

		PackedVector3Array rvertices = arrays[Mesh::ARRAY_VERTEX];
		int vc = rvertices.size();

		PackedVector3Array rnormals = arrays[Mesh::ARRAY_NORMAL];

		int vertex_ofs = vertices.size() / 3;

		vertices.resize((vertex_ofs + vc) * 3);
		normals.resize((vertex_ofs + vc) * 3);
		uv_indices.resize(vertex_ofs + vc);

		for (int j = 0; j < vc; j++) {
			Vector3 v = transform.xform(rvertices[j]);
			Vector3 n = normal_basis.xform(rnormals[j]).normalized();

			vertices[(j + vertex_ofs) * 3 + 0] = v.x;
			vertices[(j + vertex_ofs) * 3 + 1] = v.y;
			vertices[(j + vertex_ofs) * 3 + 2] = v.z;
			normals[(j + vertex_ofs) * 3 + 0] = n.x;
			normals[(j + vertex_ofs) * 3 + 1] = n.y;
			normals[(j + vertex_ofs) * 3 + 2] = n.z;
			uv_indices[j + vertex_ofs] = Pair<int, int>(i, j);
		}

		PackedInt32Array rindices = arrays[Mesh::ARRAY_INDEX];
		int ic = rindices.size();

		float eps = 1.19209290e-7F; // Taken from xatlas.h
		if (ic == 0) {
			for (int j = 0; j < vc / 3; j++) {
				Vector3 p0 = transform.xform(rvertices[j * 3 + 0]);
				Vector3 p1 = transform.xform(rvertices[j * 3 + 1]);
				Vector3 p2 = transform.xform(rvertices[j * 3 + 2]);

				if ((p0 - p1).length_squared() < eps || (p1 - p2).length_squared() < eps || (p2 - p0).length_squared() < eps) {
					continue;
				}

				indices.push_back(vertex_ofs + j * 3 + 0);
				indices.push_back(vertex_ofs + j * 3 + 1);
				indices.push_back(vertex_ofs + j * 3 + 2);
			}

		} else {
			for (int j = 0; j < ic / 3; j++) {
				Vector3 p0 = transform.xform(rvertices[rindices[j * 3 + 0]]);
				Vector3 p1 = transform.xform(rvertices[rindices[j * 3 + 1]]);
				Vector3 p2 = transform.xform(rvertices[rindices[j * 3 + 2]]);

				if ((p0 - p1).length_squared() < eps || (p1 - p2).length_squared() < eps || (p2 - p0).length_squared() < eps) {
					continue;
				}

				indices.push_back(vertex_ofs + rindices[j * 3 + 0]);
				indices.push_back(vertex_ofs + rindices[j * 3 + 1]);
				indices.push_back(vertex_ofs + rindices[j * 3 + 2]);
			}
		}

		lightmap_surfaces.push_back(s);
	}

	//unwrap

	bool use_cache = p_generate_cache; // Used to request cache generation and to know if cache was used
	uint8_t *gen_cache;
	int gen_cache_size;
	float *gen_uvs;
	int *gen_vertices;
	int *gen_indices;
	int gen_vertex_count;
	int gen_index_count;
	int size_x;
	int size_y;

	bool ok = array_mesh_lightmap_unwrap_callback(p_texel_size, vertices.ptr(), normals.ptr(), vertices.size() / 3, indices.ptr(), indices.size(), p_src_cache.ptr(), &use_cache, &gen_cache, &gen_cache_size, &gen_uvs, &gen_vertices, &gen_vertex_count, &gen_indices, &gen_index_count, &size_x, &size_y);

	if (!ok) {
		return ERR_CANT_CREATE;
	}

	clear_surfaces();

	//create surfacetools for each surface..
	LocalVector<Ref<SurfaceTool>> surfaces_tools;

	for (int i = 0; i < lightmap_surfaces.size(); i++) {
		Ref<SurfaceTool> st;
		st.instantiate();
		st->begin(Mesh::PRIMITIVE_TRIANGLES);
		st->set_material(lightmap_surfaces[i].material);
		surfaces_tools.push_back(st); //stay there
	}

	print_verbose("Mesh: Gen indices: " + itos(gen_index_count));

	//go through all indices
	for (int i = 0; i < gen_index_count; i += 3) {
		ERR_FAIL_INDEX_V(gen_vertices[gen_indices[i + 0]], (int)uv_indices.size(), ERR_BUG);
		ERR_FAIL_INDEX_V(gen_vertices[gen_indices[i + 1]], (int)uv_indices.size(), ERR_BUG);
		ERR_FAIL_INDEX_V(gen_vertices[gen_indices[i + 2]], (int)uv_indices.size(), ERR_BUG);

		ERR_FAIL_COND_V(uv_indices[gen_vertices[gen_indices[i + 0]]].first != uv_indices[gen_vertices[gen_indices[i + 1]]].first || uv_indices[gen_vertices[gen_indices[i + 0]]].first != uv_indices[gen_vertices[gen_indices[i + 2]]].first, ERR_BUG);

		int surface = uv_indices[gen_vertices[gen_indices[i + 0]]].first;

		for (int j = 0; j < 3; j++) {
			SurfaceTool::Vertex v = lightmap_surfaces[surface].vertices[uv_indices[gen_vertices[gen_indices[i + j]]].second];

			if (lightmap_surfaces[surface].format & ARRAY_FORMAT_COLOR) {
				surfaces_tools[surface]->set_color(v.color);
			}
			if (lightmap_surfaces[surface].format & ARRAY_FORMAT_TEX_UV) {
				surfaces_tools[surface]->set_uv(v.uv);
			}
			if (lightmap_surfaces[surface].format & ARRAY_FORMAT_NORMAL) {
				surfaces_tools[surface]->set_normal(v.normal);
			}
			if (lightmap_surfaces[surface].format & ARRAY_FORMAT_TANGENT) {
				Plane t;
				t.normal = v.tangent;
				t.d = v.binormal.dot(v.normal.cross(v.tangent)) < 0 ? -1 : 1;
				surfaces_tools[surface]->set_tangent(t);
			}
			if (lightmap_surfaces[surface].format & ARRAY_FORMAT_BONES) {
				surfaces_tools[surface]->set_bones(v.bones);
			}
			if (lightmap_surfaces[surface].format & ARRAY_FORMAT_WEIGHTS) {
				surfaces_tools[surface]->set_weights(v.weights);
			}

			Vector2 uv2(gen_uvs[gen_indices[i + j] * 2 + 0], gen_uvs[gen_indices[i + j] * 2 + 1]);
			surfaces_tools[surface]->set_uv2(uv2);

			surfaces_tools[surface]->add_vertex(v.vertex);
		}
	}

	//generate surfaces
	for (unsigned int i = 0; i < surfaces_tools.size(); i++) {
		surfaces_tools[i]->index();
		surfaces_tools[i]->commit(Ref<FakeMesh>((FakeMesh *)this), lightmap_surfaces[i].format);
	}

	set_lightmap_size_hint(Size2(size_x, size_y));

	if (gen_cache_size > 0) {
		r_dst_cache.resize(gen_cache_size);
		memcpy(r_dst_cache.ptrw(), gen_cache, gen_cache_size);
		memfree(gen_cache);
	}

	if (!use_cache) {
		// Cache was not used, free the buffers
		memfree(gen_vertices);
		memfree(gen_indices);
		memfree(gen_uvs);
	}

	return OK;
}

void FakeMesh::set_shadow_mesh(const Ref<Resource> &p_mesh) {
	ERR_FAIL_COND_MSG(p_mesh == this, "Cannot set a mesh as its own shadow mesh.");
	Ref<MissingResource> mr = p_mesh;
	if (mr.is_valid()) {
		Ref<MissingResource> m_res = mr;
		if (ResourceCompatConverter::is_external_resource(mr)) {
			Error err;
			m_res = ResourceCompatLoader::fake_load(mr->get_path(), "", &err);
			ERR_FAIL_COND_MSG(err != OK || !m_res.is_valid(), "Failed to load material: " + mr->get_path());
		}
		Ref<FakeMesh> fake_mesh;
		fake_mesh.instantiate();
		fake_mesh->load_type = load_type;

		shadow_mesh = ResourceCompatConverter::set_real_from_missing_resource(m_res, fake_mesh, load_type);
	} else {
		shadow_mesh = p_mesh;
	}
	// if (shadow_mesh.is_valid()) {
	// 	RS::get_singleton()->mesh_set_shadow_mesh(mesh, shadow_mesh->get_rid());
	// } else {
	// 	RS::get_singleton()->mesh_set_shadow_mesh(mesh, RID());
	// }
}

Ref<FakeMesh> FakeMesh::get_shadow_mesh() const {
	return shadow_mesh;
}

void FakeMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_blend_shape", "name"), &FakeMesh::add_blend_shape);
	ClassDB::bind_method(D_METHOD("get_blend_shape_count"), &FakeMesh::get_blend_shape_count);
	ClassDB::bind_method(D_METHOD("get_blend_shape_name", "index"), &FakeMesh::get_blend_shape_name);
	ClassDB::bind_method(D_METHOD("set_blend_shape_name", "index", "name"), &FakeMesh::set_blend_shape_name);
	ClassDB::bind_method(D_METHOD("clear_blend_shapes"), &FakeMesh::clear_blend_shapes);
	ClassDB::bind_method(D_METHOD("set_blend_shape_mode", "mode"), &FakeMesh::set_blend_shape_mode);
	ClassDB::bind_method(D_METHOD("get_blend_shape_mode"), &FakeMesh::get_blend_shape_mode);

	ClassDB::bind_method(D_METHOD("add_surface_from_arrays", "primitive", "arrays", "blend_shapes", "lods", "flags"), &FakeMesh::add_surface_from_arrays, DEFVAL(Array()), DEFVAL(Dictionary()), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("clear_surfaces"), &FakeMesh::clear_surfaces);
	ClassDB::bind_method(D_METHOD("surface_remove", "surf_idx"), &FakeMesh::surface_remove);
	ClassDB::bind_method(D_METHOD("surface_update_vertex_region", "surf_idx", "offset", "data"), &FakeMesh::surface_update_vertex_region);
	ClassDB::bind_method(D_METHOD("surface_update_attribute_region", "surf_idx", "offset", "data"), &FakeMesh::surface_update_attribute_region);
	ClassDB::bind_method(D_METHOD("surface_update_skin_region", "surf_idx", "offset", "data"), &FakeMesh::surface_update_skin_region);
	ClassDB::bind_method(D_METHOD("surface_get_array_len", "surf_idx"), &FakeMesh::surface_get_array_len);
	ClassDB::bind_method(D_METHOD("surface_get_array_index_len", "surf_idx"), &FakeMesh::surface_get_array_index_len);
	ClassDB::bind_method(D_METHOD("surface_get_format", "surf_idx"), &FakeMesh::surface_get_format);
	ClassDB::bind_method(D_METHOD("surface_get_primitive_type", "surf_idx"), &FakeMesh::surface_get_primitive_type);
	ClassDB::bind_method(D_METHOD("surface_find_by_name", "name"), &FakeMesh::surface_find_by_name);
	ClassDB::bind_method(D_METHOD("surface_set_name", "surf_idx", "name"), &FakeMesh::surface_set_name);
	ClassDB::bind_method(D_METHOD("surface_get_name", "surf_idx"), &FakeMesh::surface_get_name);
	// #ifndef PHYSICS_3D_DISABLED
	// 	ClassDB::bind_method(D_METHOD("create_trimesh_shape"), &FakeMesh::create_trimesh_shape);
	// 	ClassDB::bind_method(D_METHOD("create_convex_shape", "clean", "simplify"), &FakeMesh::create_convex_shape, DEFVAL(true), DEFVAL(false));
	// #endif // PHYSICS_3D_DISABLED
	// 	ClassDB::bind_method(D_METHOD("create_outline", "margin"), &FakeMesh::create_outline);
	ClassDB::bind_method(D_METHOD("regen_normal_maps"), &FakeMesh::regen_normal_maps);
	ClassDB::set_method_flags(get_class_static(), StringName("regen_normal_maps"), METHOD_FLAGS_DEFAULT | METHOD_FLAG_EDITOR);
	ClassDB::bind_method(D_METHOD("lightmap_unwrap", "transform", "texel_size"), &FakeMesh::lightmap_unwrap);
	ClassDB::set_method_flags(get_class_static(), StringName("lightmap_unwrap"), METHOD_FLAGS_DEFAULT | METHOD_FLAG_EDITOR);
	// ClassDB::bind_method(D_METHOD("generate_triangle_mesh"), &FakeMesh::generate_triangle_mesh);

	ClassDB::bind_method(D_METHOD("set_custom_aabb", "aabb"), &FakeMesh::set_custom_aabb);
	ClassDB::bind_method(D_METHOD("get_custom_aabb"), &FakeMesh::get_custom_aabb);

	ClassDB::bind_method(D_METHOD("set_shadow_mesh", "mesh"), &FakeMesh::set_shadow_mesh);
	ClassDB::bind_method(D_METHOD("get_shadow_mesh"), &FakeMesh::get_shadow_mesh);

	ClassDB::bind_method(D_METHOD("_set_blend_shape_names", "blend_shape_names"), &FakeMesh::_set_blend_shape_names);
	ClassDB::bind_method(D_METHOD("_get_blend_shape_names"), &FakeMesh::_get_blend_shape_names);

	ClassDB::bind_method(D_METHOD("_set_surfaces", "surfaces"), &FakeMesh::_set_surfaces);
	ClassDB::bind_method(D_METHOD("_get_surfaces"), &FakeMesh::_get_surfaces);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "_blend_shape_names", PROPERTY_HINT_NO_NODEPATH, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL), "_set_blend_shape_names", "_get_blend_shape_names");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "_surfaces", PROPERTY_HINT_NO_NODEPATH, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL), "_set_surfaces", "_get_surfaces");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_shape_mode", PROPERTY_HINT_ENUM, "Normalized,Relative"), "set_blend_shape_mode", "get_blend_shape_mode");
	ADD_PROPERTY(PropertyInfo(Variant::AABB, "custom_aabb", PROPERTY_HINT_NO_NODEPATH, "suffix:m"), "set_custom_aabb", "get_custom_aabb");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shadow_mesh", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), "set_shadow_mesh", "get_shadow_mesh");
}

void FakeMesh::reload_from_file() {
	// RenderingServer::get_singleton()->mesh_clear(mesh);
	surfaces.clear();
	clear_blend_shapes();
	clear_cache();

	Resource::reload_from_file();

	notify_property_list_changed();
}

FakeMesh::FakeMesh() {
	//mesh is now created on demand
	//mesh = RenderingServer::get_singleton()->mesh_create();
}

FakeMesh::~FakeMesh() {
	if (mesh.is_valid()) {
		ERR_FAIL_NULL(RenderingServer::get_singleton());
		RenderingServer::get_singleton()->free(mesh);
	}
}
