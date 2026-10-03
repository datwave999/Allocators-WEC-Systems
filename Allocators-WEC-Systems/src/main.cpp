#include <iostream>

#include "../tests/TestRunner.h"
#include "ArenaAllocator.h"

int main()
{
	const int failures = RunArenaTests();
	return failures == 0 ? 0 : 1;
}