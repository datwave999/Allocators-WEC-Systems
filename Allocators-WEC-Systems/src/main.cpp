#include <iostream>

#include "../tests/TestRunner.h"
#include "../benchmarks/BenchmarkRunner.h"
#include "ArenaAllocator.h"

int main()
{
	int failures = RunArenaTests();
	failures += RunSPSCTests();

	if (failures != 0) return 1;
	return RunArenaBenchmarks();
}
