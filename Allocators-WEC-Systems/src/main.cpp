#include "../tests/TestRunner.h"
#include "../benchmarks/BenchmarkRunner.h"
#include "BasicPipeline.h"

int main()
{
	int testResult = RunArenaTests();
	testResult += RunSPSCTests();
	if (testResult != 0) return 1;

	int benchmarkResult = RunArenaBenchmarks();
	benchmarkResult += RunPipelineBenchmarks();
	benchmarkResult += RunBaselineBenchmarks();
	if (benchmarkResult != 0) return 1;

	return 0;
}
