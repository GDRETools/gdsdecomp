#pragma once
// To compare the size of two structs without the padding at the end
// TODO: this doesn't work if the arguments have a namespace qualifier
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
