#pragma once
// To compare the size of two structs without the padding at the end
// TODO: this doesn't work if the arguments have a namespace qualifier
#include <type_traits>
#define CHECK_SIZE_MATCH_NO_PADDING(reference_type, our_type)                                     \
	namespace {                                                                                   \
	_Pragma("pack(push, 1)");                                                                     \
	struct _##reference_type##_without_padding : reference_type {};                               \
	struct _##our_type##_without_padding : our_type {};                                           \
	_Pragma("pack(pop)");                                                                         \
	static_assert(                                                                                \
			sizeof(_##our_type##_without_padding) == sizeof(_##reference_type##_without_padding), \
			"Size mismatch");                                                                     \
	}

// static member functions / free functions are the same
// if their types are the same
template <class T, class U>
struct has_same_signature : std::is_same<T, U> {};

// member functions have the same signature if they're two pointers to members
// with the same pointed-to type
template <class T, class C1, class C2>
struct has_same_signature<T C1::*, T C2::*> : std::true_type {};
