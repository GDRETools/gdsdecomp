
#ifndef STD_HASH_H
#define STD_HASH_H
#include <core/typedefs.h>
#include <core/variant/variant.h>
#include <external/parallel_hashmap/phmap_utils.h>

// Disabling this for now unless we upgrade to c++20 at some point
#define _NON_MSVC_CONSTEXPR_

struct HashMapHasher64 {
	static _FORCE_INLINE_ constexpr uint64_t splitmix64(uint64_t x) {
		x += 0x9e3779b97f4a7c15;
		x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
		x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
		return x ^ (x >> 31);
	}

	template <typename T>
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Ref<T> &p_ref) {
		return std::hash<T *>{}(p_ref.operator->());
	}

	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const String &p) {
		return splitmix64(p.hash());
	}

	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const StringName &p) {
		return splitmix64(p.hash());
	}

	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const NodePath &p) {
		return splitmix64(p.hash());
	}

	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const CharString &p) {
		return splitmix64(String::hash(p.get_data()));
	}

	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Char16String &p) {
		auto chr = p.get_data();
		uint32_t hashv = 5381;
		uint32_t c = *chr++;

		while (c) {
			hashv = ((hashv << 5) + hashv) + c; /* hash * 33 + c */
			c = *chr++;
		}

		return splitmix64(hashv);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const RID &p) {
		return std::hash<uint64_t>{}(p.get_id());
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Vector2i &p) {
		return phmap::HashState().combine(0, p.x, p.y);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Vector3i &p) {
		return phmap::HashState().combine(0, p.x, p.y, p.z);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Vector4i &p) {
		return phmap::HashState().combine(0, p.x, p.y, p.z, p.w);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Vector2 &p) {
		return phmap::HashState().combine(0, p.x, p.y);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Vector3 &p) {
		return phmap::HashState().combine(0, p.x, p.y, p.z);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Vector4 &p) {
		return phmap::HashState().combine(0, p.x, p.y, p.z, p.w);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Rect2i &p) {
		return phmap::HashState().combine(0, p.position.x, p.position.y, p.size.x, p.size.y);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Rect2 &p) {
		return phmap::HashState().combine(0, p.position.x, p.position.y, p.size.x, p.size.y);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const AABB &p) {
		return phmap::HashState().combine(0, p.position.x, p.position.y, p.position.z, p.size.x, p.size.y, p.size.z);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Basis &p) {
		return phmap::HashState().combine(0, p.rows[0].x, p.rows[0].y, p.rows[0].z, p.rows[1].x, p.rows[1].y, p.rows[1].z, p.rows[2].x, p.rows[2].y, p.rows[2].z);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Transform2D &p) {
		return phmap::HashState().combine(0, p.columns[0].x, p.columns[0].y, p.columns[1].x, p.columns[1].y, p.columns[2].x, p.columns[2].y);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Transform3D &p) {
		return phmap::HashState().combine(0, p.basis.rows[0].x, p.basis.rows[0].y, p.basis.rows[0].z, p.basis.rows[1].x, p.basis.rows[1].y, p.basis.rows[1].z, p.basis.rows[2].x, p.basis.rows[2].y, p.basis.rows[2].z, p.origin.x, p.origin.y, p.origin.z);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Color &p) {
		return phmap::HashState().combine(0, p.r, p.g, p.b, p.a);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Plane &p) {
		return phmap::HashState().combine(0, p.normal.x, p.normal.y, p.normal.z, p.d);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Quaternion &p) {
		return phmap::HashState().combine(0, p.x, p.y, p.z, p.w);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const ObjectID &p) {
		return std::hash<uint64_t>{}(p);
	}
	static _FORCE_INLINE_ _NON_MSVC_CONSTEXPR_ std::size_t hash(const Projection &p) {
		return phmap::HashState().combine(0, p.columns[0].x, p.columns[0].y, p.columns[0].z, p.columns[0].w, p.columns[1].x, p.columns[1].y, p.columns[1].z, p.columns[1].w, p.columns[2].x, p.columns[2].y, p.columns[2].z, p.columns[2].w, p.columns[3].x, p.columns[3].y, p.columns[3].z, p.columns[3].w);
	}
};

#ifdef GODOT_STD_HASH_USE_32
using GodotStdHasher = HashMapHasherDefault;
#else
using GodotStdHasher = HashMapHasher64;
#endif

template <typename T, typename Enable = void>
struct godot_hash {
	_NON_MSVC_CONSTEXPR_ std::size_t operator()(const T &p) const noexcept {
		// Fallback hash function for types without a defined hash function
		return std::hash<T>{}(p);
	}
};

// Specialization for types that have a hash function in GodotStdHasher
template <typename T>
struct godot_hash<T, std::void_t<decltype(GodotStdHasher::hash(std::declval<T>()))>> {
	_NON_MSVC_CONSTEXPR_ std::size_t operator()(const T &p) const noexcept {
		return GodotStdHasher::hash(p);
	}
};

// std::hash specializations for Godot types
#ifdef GODOT_INJECT_STD_HASH

namespace std {

template <typename T>

struct hash<Ref<T>> {
	_NON_MSVC_CONSTEXPR_ std::size_t operator()(const Ref<T> &p) const noexcept {
		return GodotStdHasher::hash(p);
	}
};

#define _MK_STD_HASH(T)                                                          \
	template <>                                                                  \
	struct hash<T> {                                                             \
		_NON_MSVC_CONSTEXPR_ std::size_t operator()(const T &p) const noexcept { \
			return GodotStdHasher::hash(p);                                      \
		}                                                                        \
	}
_MK_STD_HASH(String);
_MK_STD_HASH(CharString);
_MK_STD_HASH(StringName);
_MK_STD_HASH(RID);
_MK_STD_HASH(NodePath);
_MK_STD_HASH(ObjectID);
_MK_STD_HASH(Vector2i);
_MK_STD_HASH(Vector3i);
_MK_STD_HASH(Vector4i);
_MK_STD_HASH(Vector2);
_MK_STD_HASH(Vector3);
_MK_STD_HASH(Vector4);
_MK_STD_HASH(Rect2i);
_MK_STD_HASH(Rect2);
_MK_STD_HASH(AABB);

#undef _MK_STD_HASH
} //namespace std
#endif //GODOT_CPP_USE_STD_HASH

template <typename T>
struct godot_hashmap_equal_to {
	bool operator()(const T &p_lhs, const T &p_rhs) const noexcept {
		return HashMapComparatorDefault<T>::compare(p_lhs, p_rhs);
	}
};

#endif //STD_HASH_H
