#include "../tests/TestRunner.h"
#include "../benchmarks/BenchmarkRunner.h"

int main()
{
	int failures = RunArenaTests();
	failures += RunSPSCTests();

	if (failures != 0) return 1;
	int benchmarkResult = RunArenaBenchmarks();
	if (benchmarkResult != 0) return benchmarkResult;

	return RunPipelineBenchmarks();
}
