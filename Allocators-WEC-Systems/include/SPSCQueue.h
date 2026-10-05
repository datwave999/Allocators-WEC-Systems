#pragma once

#include <atomic>
#include <cstddef>
#include <limits>
#include <stdexcept>

template <typename T>
class SPSCQueue
{
public:
    explicit SPSCQueue(std::size_t size)
    {
        // Round up to power of 2
        while (slotCount < size) {
            if (slotCount > std::numeric_limits<std::size_t>::max() / 2) throw std::length_error("SPSCQueue size is too large");
            slotCount *= 2;
        }
        buffer = new T*[slotCount]{0};
    }

    ~SPSCQueue()
    {
        delete[] buffer;
    }

    std::size_t Capacity() const noexcept
    {
        return slotCount - 1;
    }

    // Producer Thread
    bool Push(T* pointer) noexcept
    {
        std::size_t currentBack = back.load(std::memory_order_relaxed);

        std::size_t nextBack = (currentBack + 1) & (slotCount - 1);

        if (nextBack == front.load(std::memory_order_acquire)) {
            return false;
        }

        buffer[currentBack] = pointer;

        back.store(nextBack, std::memory_order_release);
        return true;
    }

    // Consumer Thread
    bool Pop(T*& pointer) noexcept
    {
        std::size_t currentFront = front.load(std::memory_order_relaxed);

        if (currentFront == back.load(std::memory_order_acquire)) {
            return false;
        }

        pointer = buffer[currentFront];

        front.store((currentFront + 1) & (slotCount - 1), std::memory_order_release);
        return true;
    }

private:
    static_assert(std::atomic<std::size_t>::is_always_lock_free, "This platform does not support lock-free queue indices");

    // Make the starting indices of these variables a multiple of 64 (unique cache lines)
    alignas(64) std::atomic<std::size_t> back{0};
    alignas(64) std::atomic<std::size_t> front{0};

    alignas(64) T** buffer = nullptr;
    std::size_t slotCount = 2;
};