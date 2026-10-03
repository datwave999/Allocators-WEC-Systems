#include<iostream>
#include<thread>
#include<chrono>

#include"ArenaAllocator.h"


void TestArena() {
	const size_t SizeMB = 2048;
	ArenaAllocator arena(1024 * 1024 * SizeMB);

	int n = 5;
	int* arr = static_cast<int*>(arena.Alloc(n * sizeof(int), alignof(int)));

	for (int i = 0; i < n; i++) {
		arr[i] = i * 2;
	}

	std::cout << "Size after allocating array: " << arena.GetUsedSpace() << std::endl;

	for (int i = 0; i < 5; i++) {
		std::cout << arr[i] << std::endl;
	}

	arena.Reset();

	std::cout << "Size after reset: " << arena.GetUsedSpace() << std::endl;

	// Sleep to check Ram Usage
	std::this_thread::sleep_for(std::chrono::seconds(60));
}