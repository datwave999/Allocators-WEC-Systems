#include "SPSCQueue.h"
#include "TestRunner.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {

bool Construction() {
    const std::size_t sizes[] = {0, 1, 2, 3, 4, 5, 8, 9, 1000};
    const std::size_t capacities[] = {1, 1, 1, 3, 3, 7, 7, 15, 1023};

    for (std::size_t i = 0; i < 9; ++i) {
        SPSCQueue<int> queue(sizes[i]);
        if (queue.Capacity() != capacities[i]) {
            std::cerr << "  Incorrect capacity for size " << sizes[i] << '\n';
            return false;
        }
    }

    try {
        SPSCQueue<int> queue(std::numeric_limits<std::size_t>::max());
    }
    catch (const std::length_error&) {
        return true;
    }

    std::cerr << "  Oversized request did not throw length_error\n";
    return false;
}

bool Empty() {
    SPSCQueue<int> queue(4);
    int* pointer = nullptr;

    if (queue.Pop(pointer)) {
        std::cerr << "  Empty pop succeeded\n";
        return false;
    }
    
    return true;
}

bool SinglePointer() {
    SPSCQueue<int> queue(2);
    int value = 42;
    int* pointer = nullptr;

    if (!queue.Push(&value) || !queue.Pop(pointer)) {
        std::cerr << "  Single push or pop failed\n";
        return false;
    }
    if (pointer != &value || *pointer != 42) {
        std::cerr << "  Pointer or payload did not match\n";
        return false;
    }
    if (queue.Pop(pointer) || pointer != &value) {
        std::cerr << "  Queue was not empty after the single pop\n";
        return false;
    }
    return true;
}

bool FIFOAndWraparound() {
    SPSCQueue<int> queue(4);
    int values[] = {10, 20, 30};
    int* pointer = nullptr;

    for (int repetition = 0; repetition < 10; ++repetition) {
        for (int i = 0; i < 3; ++i) {
            if (!queue.Push(&values[i])) {
                std::cerr << "  Push failed before the queue was full\n";
                return false;
            }
        }
        if (queue.Push(&values[0])) {
            std::cerr << "  Push succeeded while full\n";
            return false;
        }

        for (int i = 0; i < 3; ++i) {
            if (!queue.Pop(pointer) || pointer != &values[i]) {
                std::cerr << "  Pop failed or FIFO order was incorrect\n";
                return false;
            }
        }
        if (queue.Pop(pointer)) {
            std::cerr << "  Pop succeeded while empty\n";
            return false;
        }
    }
    return true;
}

bool Concurrent() {
    const int count = 1000;
    int values[count]{0};
    SPSCQueue<int> queue(8);
    bool correctOrder = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);

    std::thread producer([&]() {
        for (int i = 0; i < count; ++i) {
            values[i] = i;
            while (!queue.Push(&values[i])) {
                if (std::chrono::steady_clock::now() >= deadline) return;
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer;
    try {
        consumer = std::thread([&]() {
            for (int expected = 0; expected < count; ++expected) {
                int* pointer = nullptr;
                while (!queue.Pop(pointer)) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        correctOrder = false;
                        return;
                    }
                    std::this_thread::yield();
                }
                if (pointer != &values[expected] || *pointer != expected) {
                    correctOrder = false;
                }
            }
        });
    }
    catch (...) {
        producer.join();
        throw;
    }

    producer.join();
    consumer.join();

    int* pointer = nullptr;
    if (!correctOrder || queue.Pop(pointer)) {
        std::cerr << "  Integer transfer timed out or values arrived out of order\n";
        return false;
    }
    return true;
}


}

int RunSPSCTests() {
    struct Test {
        const char* name;
        bool (*run)();
    };

    const Test tests[] = {
        {"Construction and capacity", Construction},
        {"Empty queue", Empty},
        {"Single pointer", SinglePointer},
        {"FIFO, full queue and wraparound", FIFOAndWraparound},
        {"Concurrent transfer", Concurrent},
    };

    std::cout << "\n==== SPSC Tests ====\n";

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
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
            ++failures;
        }
    }
    std::cout << "SPSC tests: " << failures << " failed\n";
    return failures;
}
