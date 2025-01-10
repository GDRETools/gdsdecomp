
#ifndef STD_ALLOCATOR_H
#define STD_ALLOCATOR_H

#include <core/os/memory.h>
#include <core/typedefs.h>

template <typename T>
class GodotStdAllocator {
public:
	using value_type = T;
	using pointer = T *;
	using const_pointer = const T *;
	using reference = T &;
	using const_reference = const T &;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

	GodotStdAllocator() noexcept = default;
	template <typename U>
	GodotStdAllocator(const GodotStdAllocator<U> &) noexcept {}

	pointer allocate(size_type n) {
		return static_cast<pointer>(Memory::alloc_static(n * sizeof(T), false));
	}

	// TODO: we might need to use memory.h's memdelete<T> here
	void deallocate(pointer p, size_type) {
		Memory::free_static(p, false);
	}

	template <typename U, typename... Args>
	void construct(U *p, Args &&...args) {
		::new ((void *)p) U(std::forward<Args>(args)...);
	}

	template <typename U>
	void destroy(U *p) {
		p->~U();
	}

	size_type max_size() const noexcept {
		return std::numeric_limits<size_type>::max() / sizeof(T);
	}

	pointer address(reference x) const noexcept {
		return std::addressof(x);
	}

	const_pointer address(const_reference x) const noexcept {
		return std::addressof(x);
	}

	template <typename U>
	struct rebind {
		using other = GodotStdAllocator<U>;
	};

	bool operator==(const GodotStdAllocator &) const noexcept {
		return true;
	}

	bool operator!=(const GodotStdAllocator &) const noexcept {
		return false;
	}
};
#endif //STD_ALLOCATOR_H
