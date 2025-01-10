
#ifndef GD_PARALLEL_HASHMAP_H
#define GD_PARALLEL_HASHMAP_H

#include "external/parallel_hashmap/phmap.h"
#include "std_allocator.h"
#include "std_hash.h"

template <class T>
using ParallelHashMapHasher = godot_hash<T>;
template <class T>
using ParallelHashMapAllocator = GodotStdAllocator<T>;
template <class T>
using ParallelHashMapEqualTo = godot_hashmap_equal_to<T>;

template <class T,
		class Hash = ParallelHashMapHasher<T>,
		class Eq = ParallelHashMapEqualTo<T>,
		class Alloc = ParallelHashMapAllocator<T>>
using FlatHashSet = phmap::flat_hash_set<T, Hash, Eq, Alloc>;

template <class K, class V,
		class Hash = ParallelHashMapHasher<K>,
		class Eq = ParallelHashMapEqualTo<K>,
		class Alloc = ParallelHashMapAllocator<
				phmap::Pair<const K, V>>>
using FlatHashMap = phmap::flat_hash_map<K, V, Hash, Eq, Alloc>;

template <class T,
		class Hash = ParallelHashMapHasher<T>,
		class Eq = ParallelHashMapEqualTo<T>,
		class Alloc = ParallelHashMapAllocator<T>>
using NodeHashSet = phmap::node_hash_set<T, Hash, Eq, Alloc>;

template <class Key, class Value,
		class Hash = ParallelHashMapHasher<Key>,
		class Eq = ParallelHashMapEqualTo<Key>,
		class Alloc = ParallelHashMapAllocator<
				phmap::Pair<const Key, Value>>>
using NodeHashMap = phmap::node_hash_map<Key, Value, Hash, Eq, Alloc>;

template <class T,
		class Hash = ParallelHashMapHasher<T>,
		class Eq = ParallelHashMapEqualTo<T>,
		class Alloc = ParallelHashMapAllocator<T>,
		size_t N = 4, // 2**N submaps
		class Mutex = BinaryMutex> // use std::mutex to enable internal locks
using ParallelFlatHashSet = phmap::parallel_flat_hash_set<T, Hash, Eq, Alloc, N, Mutex>;

template <class K, class V,
		class Hash = ParallelHashMapHasher<K>,
		class Eq = ParallelHashMapEqualTo<K>,
		class Alloc = ParallelHashMapAllocator<
				phmap::Pair<const K, V>>,
		size_t N = 4, // 2**N submaps
		class Mutex = BinaryMutex> // use std::mutex to enable internal locks
using ParallelFlatHashMap = phmap::parallel_flat_hash_map<K, V, Hash, Eq, Alloc, N, Mutex>;

template <class T,
		class Hash = ParallelHashMapHasher<T>,
		class Eq = ParallelHashMapEqualTo<T>,
		class Alloc = ParallelHashMapAllocator<T>,
		size_t N = 4, // 2**N submaps
		class Mutex = BinaryMutex> // use std::mutex to enable internal locks
using ParallelNodeHashSet = phmap::parallel_node_hash_set<T, Hash, Eq, Alloc, N, Mutex>;

template <class Key, class Value,
		class Hash = ParallelHashMapHasher<Key>,
		class Eq = ParallelHashMapEqualTo<Key>,
		class Alloc = ParallelHashMapAllocator<
				phmap::Pair<const Key, Value>>,
		size_t N = 4, // 2**N submaps
		class Mutex = BinaryMutex> // use std::mutex to enable internal locks
using ParallelNodeHashMap = phmap::parallel_node_hash_map<Key, Value, Hash, Eq, Alloc, N, Mutex>;

#endif //GD_PARALLEL_HASHMAP_H
