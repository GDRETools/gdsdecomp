/**************************************************************************/
/*  mesh.h                                                                */
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

#pragma once
#include "core/io/resource.h"
#include "core/math/face3.h"
#include "core/math/triangle_mesh.h"
#include "scene/resources/material.h"
#include "servers/rendering_server.h"

#include "utility/resource_info.h"

#ifndef PHYSICS_3D_DISABLED
#include "scene/resources/3d/shape_3d.h"

class ConcavePolygonShape3D;
class ConvexPolygonShape3D;
class Shape3D;
#endif // PHYSICS_3D_DISABLED
class MeshConvexDecompositionSettings;

#include "scene/resources/mesh.h"

class FakeMesh : public Mesh {
	GDCLASS(FakeMesh, Mesh);
	RES_BASE_EXTENSION("mesh");

	PackedStringArray _get_blend_shape_names() const;
	void _set_blend_shape_names(const PackedStringArray &p_names);

	Array _get_surfaces() const;
	void _set_surfaces(const Array &p_data);
	Ref<Mesh> shadow_mesh;

private:
	struct Surface {
		uint64_t format = 0;
		int array_length = 0;
		int index_array_length = 0;
		PrimitiveType primitive = PrimitiveType::PRIMITIVE_MAX;

		String name;
		AABB aabb;
		Ref<Material> material;
		bool is_2d = false;
	};
	Vector<Surface> surfaces;
	mutable RID mesh;
	AABB aabb;
	BlendShapeMode blend_shape_mode = BLEND_SHAPE_MODE_RELATIVE;
	Vector<StringName> blend_shapes;
	AABB custom_aabb;

	// fake members
public:
	Vector<RS::SurfaceData> surface_data;
	Vector<Array> surface_arrays;
	ResourceInfo::LoadType load_type = ResourceInfo::LoadType::ERR;

	_FORCE_INLINE_ void _create_if_empty() const;
	void _recompute_aabb();

protected:
	virtual bool _is_generated() const { return false; }

	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
	bool surface_index_0 = false;

	virtual void reset_state() override;

	static void _bind_methods();

public:
	void add_surface_from_arrays(PrimitiveType p_primitive, const Array &p_arrays, const TypedArray<Array> &p_blend_shapes = TypedArray<Array>(), const Dictionary &p_lods = Dictionary(), BitField<ArrayFormat> p_flags = 0);

	void add_surface(BitField<ArrayFormat> p_format, PrimitiveType p_primitive, const Vector<uint8_t> &p_array, const Vector<uint8_t> &p_attribute_array, const Vector<uint8_t> &p_skin_array, int p_vertex_count, const Vector<uint8_t> &p_index_array, int p_index_count, const AABB &p_aabb, const Vector<uint8_t> &p_blend_shape_data = Vector<uint8_t>(), const Vector<AABB> &p_bone_aabbs = Vector<AABB>(), const Vector<RS::SurfaceData::LOD> &p_lods = Vector<RS::SurfaceData::LOD>(), const Vector4 p_uv_scale = Vector4());

	Array surface_get_arrays(int p_surface) const override;
	TypedArray<Array> surface_get_blend_shape_arrays(int p_surface) const override;
	Dictionary surface_get_lods(int p_surface) const override;

	void add_blend_shape(const StringName &p_name);
	int get_blend_shape_count() const override;
	StringName get_blend_shape_name(int p_index) const override;
	void set_blend_shape_name(int p_index, const StringName &p_name) override;
	void clear_blend_shapes();

	void set_blend_shape_mode(BlendShapeMode p_mode);
	BlendShapeMode get_blend_shape_mode() const;

	void surface_update_vertex_region(int p_surface, int p_offset, const Vector<uint8_t> &p_data);
	void surface_update_attribute_region(int p_surface, int p_offset, const Vector<uint8_t> &p_data);
	void surface_update_skin_region(int p_surface, int p_offset, const Vector<uint8_t> &p_data);

	int get_surface_count() const override;

	void surface_remove(int p_surface);
	void clear_surfaces();

	void surface_set_custom_aabb(int p_idx, const AABB &p_aabb); //only recognized by driver

	int surface_get_array_len(int p_idx) const override;
	int surface_get_array_index_len(int p_idx) const override;
	BitField<ArrayFormat> surface_get_format(int p_idx) const override;
	PrimitiveType surface_get_primitive_type(int p_idx) const override;

	virtual void surface_set_material(int p_idx, const Ref<Material> &p_material) override;
	virtual Ref<Material> surface_get_material(int p_idx) const override;

	int surface_find_by_name(const String &p_name) const;
	void surface_set_name(int p_idx, const String &p_name);
	String surface_get_name(int p_idx) const;

	void set_custom_aabb(const AABB &p_custom);
	AABB get_custom_aabb() const;

	AABB get_aabb() const override;
	virtual RID get_rid() const override;

	void regen_normal_maps();

	Error lightmap_unwrap(const Transform3D &p_base_transform = Transform3D(), float p_texel_size = 0.05);
	Error lightmap_unwrap_cached(const Transform3D &p_base_transform, float p_texel_size, const Vector<uint8_t> &p_src_cache, Vector<uint8_t> &r_dst_cache, bool p_generate_cache = true);

	virtual void reload_from_file() override;

	void set_shadow_mesh(const Ref<Resource> &p_mesh);
	Ref<Resource> get_shadow_mesh() const;

	FakeMesh();

	~FakeMesh();
};
