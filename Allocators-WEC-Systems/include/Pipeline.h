#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ArenaAllocator.h"
#include "SPSCQueue.h"

// Fixed-size message as out test payload
struct PacketMetadata
{
    std::uint64_t sequence;
    std::uint64_t value;   
    std::uint64_t source;  
    std::uint64_t flags;   
};

struct PipelineConfig
{
    std::size_t batchCount = 128;
    std::size_t messagesPerBatch = 65536;

    // SPSCQueue with 4095 slots.
    std::size_t queueSlots = 4096;
};

// Updated only by the consumer. Read from other threads after synchronization.
struct ConsumerStats
{
    std::uint64_t messagesProcessed = 0;
    std::uint64_t checksum = 0;
};

// Shared by both workers
struct PipelineControl
{
    alignas(64) std::atomic<std::size_t> completedBatches{0};
    alignas(64) std::atomic<bool> stopRequested{false};
};

// (PRODUCER)
PacketMetadata* AllocatePacket(ArenaAllocator& arena, std::uint64_t sequence);
bool ProduceBatch(ArenaAllocator& arena, SPSCQueue<PacketMetadata>& queue, std::uint64_t firstSequence, std::size_t messageCount, std::atomic<bool>& stopRequested);

// (CONSUMER)
bool ConsumeBatch(SPSCQueue<PacketMetadata>& queue, std::uint64_t firstSequence, std::size_t messageCount, ConsumerStats& stats, std::atomic<bool>& stopRequested);

// Run concurrently with the same config and control
bool RunProducer(ArenaAllocator& arena, SPSCQueue<PacketMetadata>& queue, const PipelineConfig& config, PipelineControl& control);
bool RunConsumer(SPSCQueue<PacketMetadata>& queue, const PipelineConfig& config, ConsumerStats& stats, PipelineControl& control);

// Multi-threaded pipeline runner
bool RunPipeline(const PipelineConfig& config, ConsumerStats& stats);
