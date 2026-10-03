#pragma once

#include <cstdint>
#include <cstddef>

class ArenaAllocator
{
public:
	ArenaAllocator(size_t ArenaSize);
	~ArenaAllocator();

	// No Copies allowed (preventing double destruction)
	ArenaAllocator(const ArenaAllocator& other) = delete;
	ArenaAllocator& operator=(const ArenaAllocator& other) = delete;

	void* Alloc(size_t size, size_t alignment);
	void Reset();
	
	size_t GetUsedSpace() const;

private:
	uint8_t* basePtr;
	uint8_t* bumpPtr;
	size_t totalSize;
};