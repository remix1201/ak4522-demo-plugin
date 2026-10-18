// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// The effect: host audio through the emulated chip with ak4522::HostProcessor,
// on the DAC path, the DAC into the ADC, or the ADC alone. Adds host behavior
// outside the chip model: output gain, a latency-aligned wet/dry mix, a delta
// listen mode, and compensated bypass.

#include "common/ChipPlugin.h"

#include <array>

namespace ak4522demo {

namespace id {
inline constexpr const char* dacRate = "dacRate";
inline constexpr const char* inputResolution = "inputResolution";
inline constexpr const char* inputDither = "inputDither";
inline constexpr const char* path = "path";
inline constexpr const char* mix = "mix";
inline constexpr const char* delta = "delta";
inline constexpr const char* bypass = "bypass";
} // namespace id

// DAC rates offered, in the order of the dacRate choices.
inline constexpr std::array<double, 6> effectDacRates{16000.0, 22050.0, 24000.0, 32000.0, 44100.0, 48000.0};

class EffectProcessor : public ChipPlugin {
public:
    EffectProcessor();

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { run(buffer, false); }
    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override { run(buffer, false); }
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { run(buffer, true); }
    void processBlockBypassed(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override { run(buffer, true); }
    void reset() override;
    double getTailLengthSeconds() const override { return tailSeconds_.load(std::memory_order_relaxed); }
    juce::AudioProcessorParameter* getBypassParameter() const override;
    // Adds: DC offset does nothing on the ADC path, which has no DAC.
    juce::String inactiveReason(const juce::String& parameterId) const override;

private:
    juce::String prepareChip(double hostRate, int maxBlock) override;

    template <typename T>
    void run(juce::AudioBuffer<T>& buffer, bool hostBypassed) noexcept;

    ak4522::HostProcessor host_;
    DryDelay dry_;
    std::vector<double> dryL_, dryR_;
    int chunk_ = 0;
    juce::SmoothedValue<double> gain_, mix_, bypass_;
    std::atomic<double> tailSeconds_{0.0};
    std::atomic<bool> resetPending_{false};
};

} // namespace ak4522demo
