#include <iostream>
#include <exception>

#include "ArenaAllocator.h"
#include "TestRunner.h"

bool InitialStateAndReset() {
	const size_t SizeKB = 64;
	ArenaAllocator arena(1024 * SizeKB);

	if (arena.GetUsedSpace() != 0) {
		std::cerr << "  Initial state was not empty\n";
		return false;
	}

	// Use some space before resetting
	uint8_t* first = static_cast<uint8_t*>(arena.Alloc(16, 1));

	if (first == nullptr) {
		std::cerr << "  Initial allocation returned nullptr\n";
		return false;
	}

	if (arena.GetUsedSpace() != 16) {
		std::cerr << "  Incorrect used space before reset\n";
		return false;
	}

	arena.Reset();

	if (arena.GetUsedSpace() != 0) {
		std::cerr << "  Reset did not clear used space\n";
		return false;
	}

	// The next allocation should reuse the start of the arena
	uint8_t* reused = static_cast<uint8_t*>(arena.Alloc(16, 1));

	if (reused == nullptr || reused != first) {
		std::cerr << "  Reset did not make the arena start reusable\n";
		return false;
	}

	if (arena.GetUsedSpace() != 16) {
		std::cerr << "  Incorrect used space after reuse\n";
		return false;
	}

	// Double reset should be fine
	arena.Reset();
	arena.Reset();

	if (arena.GetUsedSpace() != 0) {
		std::cerr << "  Repeated reset did not leave the arena empty\n";
		return false;
	}

	return true;
}

bool ReadWrite() {
	const size_t SizeKB = 128;
	ArenaAllocator arena(1024 * SizeKB);

	// Allocate Space
	int n = 5;
	int* arr = static_cast<int*>(arena.Alloc(n * sizeof(int), alignof(int)));

	if (arr == nullptr) {
		std::cerr << "  Allocation returned nullptr\n";
		return false;
	}

	// Write
	for (int i = 0; i < n; i++) {
		arr[i] = i * 2;
	}

	// Read
	for (int i = 0; i < n; i++) {
		if (arr[i] != i * 2) {
			std::cerr << "  Read did not match what was written\n";
			return false;
		}
	}

	// Allocate another array and check that the ranges do not overlap
	int* second = static_cast<int*>(arena.Alloc(n * sizeof(int), alignof(int)));

	if (second == nullptr) {
		std::cerr << "  Second allocation returned nullptr\n";
		return false;
	}

	if (reinterpret_cast<uintptr_t>(second) < reinterpret_cast<uintptr_t>(arr) + n * sizeof(int)) {
		std::cerr << "  Array allocations overlapped or moved backwards\n";
		return false;
	}

	if (arena.GetUsedSpace() != 2 * n * sizeof(int)) {
		std::cerr << "  Incorrect used space after second array allocation\n";
		return false;
	}

	return true;
}

bool Alignment() {
	const int N = 7;
	size_t alignments[N] = { 1, 2, 4, 8, 16, 32, 64 };

	const size_t SizeKB = 256;

	for (size_t i = 0; i < N; i++) {
		ArenaAllocator arena(1024 * SizeKB);

		// Move bump once
		if (arena.Alloc(1, 1) == nullptr) {
			std::cerr << "  Initial allocation returned nullptr\n";
			return false;
		}
		// Allocate current alignment
		void* ptr = arena.Alloc(8, alignments[i]);

		if (ptr == nullptr) {
			std::cerr << "  Allocation returned nullptr\n";
			return false;
		}

		if (reinterpret_cast<uintptr_t>(ptr) % alignments[i] != 0) {
			std::cerr << "  Allocation happened with wrong alignment: " << alignments[i] << '\n';
			return false;
		}
	}

	// Invalid alignment must fail without changing allocator state
	ArenaAllocator arena(1024 * SizeKB);

	if (arena.Alloc(8, 1) == nullptr) {
		std::cerr << "  Initial allocation returned nullptr\n";
		return false;
	}

	const size_t usedSpace = arena.GetUsedSpace();

	if (arena.Alloc(1, 3) != nullptr) {
		std::cerr << "  Invalid alignment did not fail\n";
		return false;
	}

	if (arena.GetUsedSpace() != usedSpace) {
		std::cerr << "  Invalid alignment changed used space\n";
		return false;
	}

	return true;
}

bool Capacity() {
	const size_t SizeKB = 64;
	const size_t ArenaSize = 1024 * SizeKB;
	ArenaAllocator arena(ArenaSize);

	// Fill the arena without alignment padding
	void* ptr = arena.Alloc(ArenaSize, 1);

	if (ptr == nullptr) {
		std::cerr << "  Exact capacity allocation returned nullptr\n";
		return false;
	}

	if (arena.GetUsedSpace() != ArenaSize) {
		std::cerr << "  Incorrect used space after filling arena\n";
		return false;
	}

	// No space should remain for another byte
	if (arena.Alloc(1, 1) != nullptr) {
		std::cerr << "  Allocation succeeded after arena was exhausted\n";
		return false;
	}

	// Use a fresh arena to check failure with capacity still available
	ArenaAllocator partialArena(ArenaSize);

	// Use some space before the failed request
	if (partialArena.Alloc(8, 1) == nullptr) {
		std::cerr << "  Initial allocation returned nullptr\n";
		return false;
	}

	const size_t usedSpace = partialArena.GetUsedSpace();

	if (usedSpace != 8) {
		std::cerr << "  Incorrect used space before failed requests\n";
		return false;
	}

	// Check if even 1 more byte is allowed
	if (partialArena.Alloc(ArenaSize - usedSpace + 1, 1) != nullptr) {
		std::cerr << "  Oversized allocation succeeded\n";
		return false;
	}

	if (partialArena.GetUsedSpace() != usedSpace) {
		std::cerr << "  Failed allocation changed used space\n";
		return false;
	}

	return true;
}

bool PaddingExceedsCapacity() {
	const size_t ArenaSize = 16;
	ArenaAllocator arena(ArenaSize);

	// Leave one byte available at offset 15
	void* ptr = arena.Alloc(15, 1);

	if (ptr == nullptr) {
		std::cerr << "  Initial allocation returned nullptr\n";
		return false;
	}

	// Confirm the base alignment needed by this test (Handeled by OS)
	if (reinterpret_cast<uintptr_t>(ptr) % 32 != 0) {
		std::cerr << "  Arena base was not 32-byte aligned\n";
		return false;
	}

	const size_t usedSpace = arena.GetUsedSpace();

	// Alignment 32 at offset 15 needs 17 bytes of padding
	if (arena.Alloc(1, 32) != nullptr) {
		std::cerr << "  Allocation succeeded when padding exceeded capacity\n";
		return false;
	}

	if (arena.GetUsedSpace() != usedSpace) {
		std::cerr << "  Failed padding request changed used space\n";
		return false;
	}

	return true;
}

int RunArenaTests()
{
	struct Test {
		const char* name;
		bool (*run)();
	};

	const Test tests[] = {
		{"Initial state and reset", InitialStateAndReset},
		{"Read/write", ReadWrite},
		{"Alignment", Alignment},
		{"Capacity", Capacity},
		{"Padding exceeds capacity", PaddingExceedsCapacity},
	};

	std::cout << "\n==== Arena Tests ====\n";

	int failures = 0;

	for (const auto& test : tests) {
		std::cout << "Running " << test.name << "...\n";

		try {
			if (test.run()) {
				std::cout << "[PASS] " << test.name << '\n';
			}
			else {
				std::cout << "[FAIL] " << test.name << '\n';
				++failures;
			}
		}
		catch (const std::exception& error) {
			std::cerr << "[FAIL] " << test.name
				<< ": " << error.what() << '\n';
			++failures;
		}
	}

	std::cout << "Arena tests: " << failures << " failed\n";
	return failures;
}
