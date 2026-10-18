// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// The instrument: a test synth generating samples at the DAC's own clock,
// played through the emulated DAC with ak4522::DacStream in pull mode. MIDI
// is placed on the DAC frame timeline exactly, so notes start at the same
// source frame for any host rate or block size.

#include "common/ChipPlugin.h"
#include "synth/TestSynth.h"

#include <array>

namespace ak4522demo {

namespace id {
inline constexpr const char* clock = "clock";
inline constexpr const char* sampleFormat = "sampleFormat";
inline constexpr const char* waveform = "waveform";
inline constexpr const char* attack = "attack";
inline constexpr const char* release = "release";
} // namespace id

// Source clocks offered, in the order of the clock choices. The last is a
// 112 MHz clock divided by 2304, about 48.611 kHz, which needs the library's
// opt-in extended range.
inline constexpr std::array<ak4522::ExactRate, 4> synthClocks{
    ak4522::ExactRate{32000, 1}, ak4522::ExactRate{44100, 1}, ak4522::ExactRate{48000, 1},
    ak4522::ExactRate{112000000, 2304}};

class SynthProcessor : public ChipPlugin {
public:
    SynthProcessor();

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override { run(buffer, midi); }
    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) override { run(buffer, midi); }
    void reset() override;
    double getTailLengthSeconds() const override { return tailSeconds_.load(std::memory_order_relaxed); }

    // Diagnostics for tests: MIDI events dropped because the queue was full.
    std::uint64_t droppedMidiEvents() const noexcept { return droppedMidi_.load(std::memory_order_relaxed); }

private:
    juce::String prepareChip(double hostRate, int maxBlock) override;

    template <typename T>
    void run(juce::AudioBuffer<T>& buffer, juce::MidiBuffer& midi) noexcept;

    static void pull(void* context, ak4522::DacPullBlock& block) noexcept;

    ak4522::DacStream stream_;
    TestSynth synth_;
    bool sixteenBit_ = false;
    // Host time to DAC frame: frame = ceil(hostFrame * ratioNum_ / ratioDen_).
    std::uint64_t ratioNum_ = 1, ratioDen_ = 1;
    std::uint64_t hostPosition_ = 0;
    std::array<ak4522::ControlEvent, 8> pendingEvents_{};
    std::size_t pendingCount_ = 0;
    juce::SmoothedValue<double> gain_;
    std::atomic<double> tailSeconds_{0.0};
    std::atomic<bool> resetPending_{false};
    std::atomic<std::uint64_t> droppedMidi_{0};
};

} // namespace ak4522demo
