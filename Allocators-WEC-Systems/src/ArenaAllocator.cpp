#include "ArenaAllocator.h"

#define WIN32_LEAN_AND_MEAN // Speed up compilation by excluding not used Win APIs
#include <windows.h>
#include <cstdint>
#include <new>

ArenaAllocator::ArenaAllocator(size_t ArenaSize) : totalSize(ArenaSize)
{
												// OS decides, Size, what to do, what we will do
	basePtr = static_cast<uint8_t*>(VirtualAlloc(nullptr, totalSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));

	if (basePtr == nullptr) {
		throw std::bad_alloc();
	}

	bumpPtr = basePtr;

	// Get page size from system
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	const size_t PAGESIZE = sysInfo.dwPageSize;

	volatile uint8_t* preFaulter = basePtr; // volatile disables optimizations, so that compiler does not delete this loop in Release mode

	// Pre-Faulting all the pages in our requested chunk of memory
	for (size_t i = 0; i < totalSize; i += PAGESIZE) {
		preFaulter[i] = 0;
	}	
}

ArenaAllocator::~ArenaAllocator()
{
    if (basePtr != nullptr) {
        // Free Memory
		VirtualFree(basePtr, 0, MEM_RELEASE);
        basePtr = nullptr;
    }
}

void* ArenaAllocator::Alloc(size_t size, size_t alignment)
{
	// Check if alignment is a power of 2
	if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
		return nullptr;
	}

	uintptr_t currentAddress = reinterpret_cast<uintptr_t>(bumpPtr);

	// Misalignment and padding using bitwise operators
	const size_t misalignment = currentAddress & (alignment - 1);
	const size_t padding = (misalignment == 0 ? 0 : alignment - misalignment);

	size_t usedSpace = GetUsedSpace();
	size_t remainingSpace = totalSize - usedSpace;

	// Check if padding space is available first, then safely substract (no wrap-around)
	if (padding > remainingSpace) return nullptr;
	if (size > remainingSpace - padding) return nullptr;

	bumpPtr = reinterpret_cast<uint8_t*>(currentAddress + padding + size);

	return reinterpret_cast<uint8_t*>(currentAddress + padding);
}

void ArenaAllocator::Reset()
{
	bumpPtr = basePtr;
}

size_t ArenaAllocator::GetUsedSpace() const
{
	return reinterpret_cast<uintptr_t>(bumpPtr) - reinterpret_cast<uintptr_t>(basePtr);
}
