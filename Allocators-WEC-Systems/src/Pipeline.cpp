#include "Pipeline.h"

#include <memory>
#include <immintrin.h>
#include <limits>
#include <stdexcept>
#include <thread>

PacketMetadata* AllocatePacket(ArenaAllocator& arena, std::uint64_t sequence)
{
    void* storage = arena.Alloc(sizeof(PacketMetadata), alignof(PacketMetadata));
    if (storage == nullptr) return nullptr;

    // Construct the packet in arena storage                                        simulate a packet using sequence
    return std::construct_at(static_cast<PacketMetadata*>(storage), PacketMetadata{sequence, sequence + 1, sequence % 16, sequence & 1});
}

bool ProduceBatch(ArenaAllocator& arena, SPSCQueue<PacketMetadata>& queue, std::uint64_t firstSequence, std::size_t messageCount, std::atomic<bool>& stopRequested)
{
    for (std::size_t i = 0; i < messageCount; ++i) {
        if (stopRequested.load(std::memory_order_relaxed)) return false;

        PacketMetadata* packet = AllocatePacket(arena, firstSequence + i);
        if (packet == nullptr) {
            // Tell the consumer to stop waiting for the rest of this batch.
            stopRequested.store(true, std::memory_order_relaxed);
            return false;
        }

        while (!queue.Push(packet)) {
            if (stopRequested.load(std::memory_order_relaxed)) return false;
            _mm_pause(); // CPU spin-wait (does not put thread to sleep)
        }
    }

    return true;
}

bool ConsumeBatch(SPSCQueue<PacketMetadata>& queue, std::uint64_t firstSequence, std::size_t messageCount, ConsumerStats& stats, std::atomic<bool>& stopRequested)
{
    std::size_t processed = 0;
    while (processed < messageCount) {
        if (stopRequested.load(std::memory_order_relaxed)) return false;

        PacketMetadata* packet = nullptr;
        while (!queue.Pop(packet)) {
            if (stopRequested.load(std::memory_order_relaxed)) return false;
            _mm_pause();
        }

        if (packet == nullptr || packet->sequence != firstSequence + processed) {
            stopRequested.store(true, std::memory_order_relaxed); // wrong value read
            return false;
        }

        stats.checksum += packet->value;
        ++stats.messagesProcessed;
        ++processed;
    }

    return true;
}

bool RunProducer(ArenaAllocator& arena, SPSCQueue<PacketMetadata>& queue, const PipelineConfig& config, PipelineControl& control)
{
    std::uint64_t firstSequence = 0;
    for (std::size_t batch = 0; batch < config.batchCount; ++batch) {
        if (!ProduceBatch(arena, queue, firstSequence, config.messagesPerBatch, control.stopRequested)) return false;

        // Wait for the consumer to complete processing
        while (control.completedBatches.load(std::memory_order_acquire) < batch + 1) {
            if (control.stopRequested.load(std::memory_order_relaxed)) return false;
            _mm_pause();
        }

        arena.Reset();
        firstSequence += config.messagesPerBatch;
    }

    return true;
}

bool RunConsumer(SPSCQueue<PacketMetadata>& queue, const PipelineConfig& config, ConsumerStats& stats, PipelineControl& control)
{
    std::uint64_t firstSequence = 0;
    for (std::size_t batch = 0; batch < config.batchCount; ++batch) {
        if (!ConsumeBatch(queue, firstSequence, config.messagesPerBatch, stats, control.stopRequested)) return false;

        // Release, to gurantee all payloads completely processes
        control.completedBatches.store(batch + 1, std::memory_order_release);
        firstSequence += config.messagesPerBatch;
    }

    return true;
}

bool RunPipeline(const PipelineConfig& config, ConsumerStats& stats)
{
    if (config.batchCount == 0 || config.messagesPerBatch == 0) {
        throw std::invalid_argument("Pipeline batch count and size must be nonzero");
    }
    if (config.messagesPerBatch > std::numeric_limits<std::size_t>::max() / sizeof(PacketMetadata) || config.batchCount > std::numeric_limits<std::uint64_t>::max() / config.messagesPerBatch) {
        throw std::length_error("Pipeline configuration is too large");
    }

    const std::uint64_t totalMessages = static_cast<std::uint64_t>(config.batchCount) * config.messagesPerBatch;
    ArenaAllocator arena(config.messagesPerBatch * sizeof(PacketMetadata));
    SPSCQueue<PacketMetadata> queue(config.queueSlots);
    PipelineControl control;
    stats = {};

    // Each result is written by its worker and read only after that worker joins.
    bool producerSucceeded = false;
    bool consumerSucceeded = false;
    std::thread producer([&]() {
        producerSucceeded = RunProducer(arena, queue, config, control);
    });

    std::thread consumer;
    try {
        consumer = std::thread([&]() {
            consumerSucceeded = RunConsumer(queue, config, stats, control);
        });
    }
    catch (...) {
        control.stopRequested.store(true, std::memory_order_relaxed);
        producer.join();
        throw;
    }

    producer.join();
    consumer.join();

    // calculated expected sum
    const std::uint64_t expectedChecksum = (totalMessages % 2 == 0)
        ? (totalMessages / 2) * (totalMessages + 1) : totalMessages * (totalMessages / 2 + 1);

    return producerSucceeded && consumerSucceeded && stats.messagesProcessed == totalMessages && stats.checksum == expectedChecksum;
}
