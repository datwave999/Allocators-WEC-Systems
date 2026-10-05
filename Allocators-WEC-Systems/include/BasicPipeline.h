#pragma once

#include "Pipeline.h"

#include <chrono>
#include <exception>
#include <iomanip>
#include <immintrin.h>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>

class MutexQueue
{
public:
    explicit MutexQueue(std::size_t size)
    {
        // Match SPSCQueue's effective capacity for the same queueSlots setting.
        std::size_t slots = 2;
        while (slots < size) {
            if (slots > std::numeric_limits<std::size_t>::max() / 2) throw std::length_error("MutexQueue size is too large");
            slots *= 2;
        }
        capacity = slots - 1;
    }

    ~MutexQueue()
    {
        // Destroy only after both workers have stopped.
        while (!queue.empty()) {
            delete queue.front();
            queue.pop();
        }
    }

    bool Push(PacketMetadata* packet)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.size() == capacity) return false;
        queue.push(packet);
        return true;
    }

    bool Pop(PacketMetadata*& packet)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return false;
        packet = queue.front();
        queue.pop();
        return true;
    }

private:
    std::queue<PacketMetadata*> queue;
    std::mutex mutex;
    std::size_t capacity;
};

inline bool RunBaselineProducer(MutexQueue& queue, const PipelineConfig& config, PipelineControl& control)
{
    std::uint64_t sequence = 0;
    try {
        for (std::size_t batch = 0; batch < config.batchCount; ++batch) {
            for (std::size_t i = 0; i < config.messagesPerBatch; ++i) {
                if (control.stopRequested.load(std::memory_order_relaxed)) return false;

                // Deletes an unpublished packet automatically on cancellation or error.
                std::unique_ptr<PacketMetadata> packet(new PacketMetadata{sequence, sequence + 1, sequence % 16, sequence & 1});
                while (!queue.Push(packet.get())) {
                    if (control.stopRequested.load(std::memory_order_relaxed)) return false;
                    _mm_pause();
                }
                packet.release(); // Ownership transfers to the queue/consumer.
                ++sequence;
            }

            while (control.completedBatches.load(std::memory_order_acquire) < batch + 1) {
                if (control.stopRequested.load(std::memory_order_relaxed)) return false;
                _mm_pause();
            }
        }
    }
    catch (...) {
        control.stopRequested.store(true, std::memory_order_relaxed);
        return false;
    }

    return true;
}

inline bool RunBaselineConsumer(MutexQueue& queue, const PipelineConfig& config, ConsumerStats& stats, PipelineControl& control)
{
    std::uint64_t expectedSequence = 0;
    try {
        for (std::size_t batch = 0; batch < config.batchCount; ++batch) {
            for (std::size_t i = 0; i < config.messagesPerBatch; ++i) {
                if (control.stopRequested.load(std::memory_order_relaxed)) return false;

                PacketMetadata* packet = nullptr;
                while (!queue.Pop(packet)) {
                    if (control.stopRequested.load(std::memory_order_relaxed)) return false;
                    _mm_pause();
                }

                if (packet == nullptr || packet->sequence != expectedSequence) {
                    delete packet;
                    control.stopRequested.store(true, std::memory_order_relaxed);
                    return false;
                }

                stats.checksum += packet->value;
                ++stats.messagesProcessed;
                ++expectedSequence;
                delete packet;
            }

            // Publish completion only after every packet in this batch is deleted.
            control.completedBatches.store(batch + 1, std::memory_order_release);
        }
    }
    catch (...) {
        control.stopRequested.store(true, std::memory_order_relaxed);
        return false;
    }

    return true;
}

inline bool RunTimedBaselinePipeline(const PipelineConfig& config, ConsumerStats& stats, double& elapsedSeconds)
{
    elapsedSeconds = 0;
    if (config.batchCount == 0 || config.messagesPerBatch == 0) {
        throw std::invalid_argument("Pipeline batch count and size must be nonzero");
    }
    if (config.batchCount > std::numeric_limits<std::uint64_t>::max() / config.messagesPerBatch) {
        throw std::length_error("Pipeline configuration is too large");
    }

    MutexQueue queue(config.queueSlots);
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
        producerSucceeded = RunBaselineProducer(queue, config, control);
        // Final acknowledgement means the consumer has deleted every packet.
        stopped = std::chrono::steady_clock::now();
    });

    std::thread consumer;
    try {
        consumer = std::thread([&]() {
            if (!waitForStart()) return;
            consumerSucceeded = RunBaselineConsumer(queue, config, stats, control);
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

    const std::uint64_t totalMessages = static_cast<std::uint64_t>(config.batchCount) * config.messagesPerBatch;
    const std::uint64_t expectedChecksum = (totalMessages % 2 == 0)
        ? (totalMessages / 2) * (totalMessages + 1) : totalMessages * (totalMessages / 2 + 1);
    return producerSucceeded && consumerSucceeded &&
        stats.messagesProcessed == totalMessages && stats.checksum == expectedChecksum;
}

inline int RunBaselineBenchmarks()
{
    try {
        PipelineConfig config;
        ConsumerStats stats;
        double elapsedSeconds = 0;
        if (!RunTimedBaselinePipeline(config, stats, elapsedSeconds)) {
            std::cerr << "Baseline count, sequence, or checksum validation failed\n";
            return 1;
        }
        if (elapsedSeconds <= 0) {
            std::cerr << "Baseline timing interval was too short to measure\n";
            return 1;
        }

        const double throughput = static_cast<double>(stats.messagesProcessed) / elapsedSeconds;
        std::cout << "\n==== Basic pipeline ====\n"
            << "Validation: PASS\n"
            << "Messages processed: " << stats.messagesProcessed << '\n'
            << "Checksum: " << stats.checksum << '\n'
            << "Elapsed: " << std::fixed << std::setprecision(6) << elapsedSeconds << " s\n"
            << "Throughput: " << std::setprecision(0) << throughput << " messages/s\n";
    }
    catch (const std::exception& error) {
        std::cerr << "Baseline pipeline failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
