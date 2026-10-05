#include "Pipeline.h"
#include "BenchmarkRunner.h"

#include <chrono>
#include <exception>
#include <iomanip>
#include <immintrin.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

// Uses the same workers as RunPipeline, with timing around processing only.
bool RunTimedPipeline(const PipelineConfig& config, ConsumerStats& stats, double& elapsedSeconds)
{
    elapsedSeconds = 0;
    if (config.batchCount == 0 || config.messagesPerBatch == 0) {
        throw std::invalid_argument("Pipeline batch count and size must be nonzero");
    }
    if (config.messagesPerBatch > std::numeric_limits<std::size_t>::max() / sizeof(PacketMetadata) ||
        config.batchCount > std::numeric_limits<std::uint64_t>::max() / config.messagesPerBatch) {
        throw std::length_error("Pipeline configuration is too large");
    }

    const std::uint64_t totalMessages = static_cast<std::uint64_t>(config.batchCount) * config.messagesPerBatch;
    ArenaAllocator arena(config.messagesPerBatch * sizeof(PacketMetadata));
    SPSCQueue<PacketMetadata> queue(config.queueSlots);
    PipelineControl control;
    stats = {};

    bool producerSucceeded = false;
    bool consumerSucceeded = false;
    alignas(64) std::atomic<std::size_t> readyWorkers{0};
    alignas(64) std::atomic<bool> startProcessing{false};
    std::chrono::steady_clock::time_point stopped;

    auto waitForStart = [&]() {
        readyWorkers.fetch_add(1, std::memory_order_release);
        while (!startProcessing.load(std::memory_order_acquire)) {
            if (control.stopRequested.load(std::memory_order_relaxed)) return false;
            _mm_pause();
        }
        return true;
    };

    std::thread producer([&]() {
        if (!waitForStart()) return;
        producerSucceeded = RunProducer(arena, queue, config, control);
        // RunProducer has observed the final acknowledgement and reset the arena.
        stopped = std::chrono::steady_clock::now();
    });

    std::thread consumer;
    try {
        consumer = std::thread([&]() {
            if (!waitForStart()) return;
            consumerSucceeded = RunConsumer(queue, config, stats, control);
        });
    }
    catch (...) {
        control.stopRequested.store(true, std::memory_order_relaxed);
        producer.join();
        throw;
    }

    while (readyWorkers.load(std::memory_order_acquire) != 2) _mm_pause();
    const auto started = std::chrono::steady_clock::now();
    startProcessing.store(true, std::memory_order_release);

    producer.join();
    consumer.join();
    elapsedSeconds = std::chrono::duration<double>(stopped - started).count();

    const std::uint64_t expectedChecksum = (totalMessages % 2 == 0) ? (totalMessages / 2) * (totalMessages + 1) : totalMessages * (totalMessages / 2 + 1);

    return producerSucceeded && consumerSucceeded && stats.messagesProcessed == totalMessages && stats.checksum == expectedChecksum;
}

int RunPipelineBenchmarks()
{
    try {
        PipelineConfig config;
        ConsumerStats stats;
        double elapsedSeconds = 0;
        if (!RunTimedPipeline(config, stats, elapsedSeconds)) {
            std::cerr << "Pipeline count, sequence, or checksum validation failed\n";
            return 1;
        }

        const double throughput = static_cast<double>(stats.messagesProcessed) / elapsedSeconds;
        std::cout << "\n==== Arena + SPSC pipeline ====\n"
            << "Validation: PASS\n"
            << "Messages processed: " << stats.messagesProcessed << '\n'
            << "Checksum: " << stats.checksum << '\n'
            << "Elapsed: " << std::fixed << std::setprecision(6) << elapsedSeconds << " s\n"
            << "Throughput: " << std::setprecision(0) << throughput << " messages/s\n";
    }
    catch (const std::exception& error) {
        std::cerr << "Pipeline failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
