#pragma once
#include <type_traits>

namespace internal {
_Pragma("pack(push, 1)");
template <typename T>
struct _checkSizeMatch : T {
};
_Pragma("pack(pop)");
} //namespace internal

template <typename T>
constexpr size_t size_of_no_padding = sizeof(internal::_checkSizeMatch<T>);

template <typename T, typename U>
struct sizes_match_no_padding : std::bool_constant<size_of_no_padding<T> == size_of_no_padding<U>> {
};

#define CHECK_SIZE_MATCH_NO_PADDING(reference_type, our_type)        \
	static_assert(                                                   \
			sizes_match_no_padding<our_type, reference_type>::value, \
			"Size mismatch");

// static_assert(sizeof(test_struct<int>) == sizeof(int) * 2, "Size mismatch");
// static member functions / free functions are the same
// if their types are the same
template <class T, class U>
struct has_same_signature : std::is_same<T, U> {};

// member functions have the same signature if they're two pointers to members
// with the same pointed-to type
template <class T, class C1, class C2>
struct has_same_signature<T C1::*, T C2::*> : std::true_type {};
