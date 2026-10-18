// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// Test entry point: JUCE needs its message manager for parameters, timers,
// and state, and the tests count heap allocations on the audio path.

#include "support.h"

#include <catch2/catch_session.hpp>

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<bool> counting{false};
std::atomic<std::size_t> allocations{0};
} // namespace

namespace test {
void countAllocations(bool on) noexcept
{
    allocations.store(0);
    counting.store(on);
}
std::size_t allocationCount() noexcept
{
    return allocations.load();
}
} // namespace test

void* operator new(std::size_t size)
{
    if (counting.load(std::memory_order_relaxed)) {
        allocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    return Catch::Session().run(argc, argv);
}
