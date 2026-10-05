#include "ArenaAllocator.h"
#include "BenchmarkRunner.h"

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

constexpr size_t KiB = 1024;
constexpr size_t MiB = 1024 * KiB;
constexpr size_t Repetitions = 100;

// Measures VirtualAlloc and pre-faulting together, excluding destruction.
int ArenaCreation() {
	const size_t sizes[] = {256 * KiB, MiB, 16 * MiB, 64 * MiB, 256 * MiB};
	double totalTime = 0;
	size_t totalSize = 0;
	for (size_t i : sizes) totalSize += i;

	for (const size_t size : sizes) {
		double totalNs = 0;

		for (size_t repetition = 0; repetition < Repetitions; ++repetition) {
			const auto start = std::chrono::steady_clock::now();
			ArenaAllocator arena(size);
			const auto stop = std::chrono::steady_clock::now();
			
			if (arena.GetUsedSpace() != 0) return 1;
			totalNs += std::chrono::duration<double, std::nano>(stop - start).count();
		}

		const double averageMs = totalNs / Repetitions / 1000000;
		std::cout << size / KiB << " Kib: " << averageMs << " ms\n";

		totalTime += averageMs;
	}

	std::cout << "Average creation time per MiB: " << totalTime * MiB / totalSize << " ms\n";

	return 0;
}

int Allocation() {
	const size_t allocSizes[] = {1, 8, 32, 64, 256, 1024, 4096};
	const int numAlloc = 50000;
	double totalTime = 0;

	std::cout << "8 Byte Alignment:\n";

	for (const size_t size : allocSizes) {
		ArenaAllocator arena(256 * MiB);
		double totalNs = 0;
		void* volatile allocation = nullptr;

		for (size_t repetition = 0; repetition < Repetitions; ++repetition) {
			const auto start = std::chrono::steady_clock::now();
			for (size_t i = 0; i < numAlloc; i++) allocation = arena.Alloc(size, 8);
			const auto stop = std::chrono::steady_clock::now();

			if (allocation == nullptr) return 1;
			totalNs += std::chrono::duration<double, std::nano>(stop - start).count() / numAlloc;

			arena.Reset();
		}

		const double averageNs = totalNs / Repetitions;
		std::cout << size << " bytes: " << averageNs << " ns/allocation\n";

		totalTime += averageNs;
	}

	std::cout << "Average Allocation Time (8 Byte Alignment): " << totalTime / 7 << "ns\n";
	totalTime = 0;

	std::cout << "1 Byte Alignment:\n";

	for (const size_t size : allocSizes) {
		ArenaAllocator arena(256 * MiB);
		double totalNs = 0;
		void* volatile allocation = nullptr;

		for (size_t repetition = 0; repetition < Repetitions; ++repetition) {
			const auto start = std::chrono::steady_clock::now();
			for (size_t i = 0; i < numAlloc; i++) allocation = arena.Alloc(size, 1);
			const auto stop = std::chrono::steady_clock::now();

			if (allocation == nullptr) return 1;
			totalNs += std::chrono::duration<double, std::nano>(stop - start).count() / numAlloc;

			arena.Reset();
		}

		const double averageNs = totalNs / Repetitions;
		std::cout << size << " bytes: " << averageNs << " ns/allocation\n";

		totalTime += averageNs;
	}

	std::cout << "Average Allocation Time (1 Byte Alignment): " << totalTime / 7 << "ns\n";
	totalTime = 0;

	return 0;
}

int Reset() {
	const size_t sizes[] = {1, 8, 32, 64, 256, 1024, 4096};
	const size_t arenaSize = 16 * KiB;
	const size_t arenaCount = 1024;
	std::vector<std::unique_ptr<ArenaAllocator>> arenas;
	arenas.reserve(arenaCount);
	for (size_t i = 0; i < arenaCount; ++i) {
		arenas.push_back(std::make_unique<ArenaAllocator>(arenaSize));
	}

	double totalNs = 0;
	// Volatile function pointer (prevent compiler optimization)
	void (ArenaAllocator::* volatile resetFunction)() = &ArenaAllocator::Reset;

	for (size_t repetition = 0; repetition < Repetitions; ++repetition) {

		for (const auto& arena : arenas) {
			for (const size_t size : sizes) {
				if (arena->Alloc(size, 8) == nullptr) return 1;
			}
			const size_t remaining = arenaSize - arena->GetUsedSpace();
			if (arena->Alloc(remaining, 1) == nullptr) return 1;
			if (arena->GetUsedSpace() != arenaSize) return 1;
		}

		const auto start = std::chrono::steady_clock::now();
		for (const auto& arena : arenas) {
			(arena.get()->*resetFunction)();
		}
		const auto stop = std::chrono::steady_clock::now();

		for (const auto& arena : arenas) {
			if (arena->GetUsedSpace() != 0) return 1;
		}
		totalNs += std::chrono::duration<double, std::nano>(stop - start).count();
	}

	std::cout << "Average reset time: " << totalNs / Repetitions / arenaCount << " ns\n";

	return 0;
}

int RunArenaBenchmarks()
{
	struct Benchmark {
		const char* name;
		int (*run)();
	};

	const Benchmark benchmarks[] = {
		{"VirtualAlloc & pre-faulting", ArenaCreation},
		{"Allocation", Allocation},
		{"Reset", Reset},
	};

	std::cout << "\n==== Arena Benchmarks ====\n";

	for (const auto& benchmark : benchmarks) {
		std::cout << '\n' << benchmark.name << '\n';
		try {
			if (benchmark.run() != 0) {
				std::cerr << "Benchmark failed: " << benchmark.name << '\n';
				return 1;
			}
		}
		catch (const std::exception& error) {
			std::cerr << "Benchmark failed: " << benchmark.name << ": " << error.what() << '\n';
			return 1;
		}
	}

	std::cout << "\n==============================\n";

	return 0;
}
