// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// What both demo plugins share: the chip parameters, versioned state, the
// off-thread re-preparation that configuration changes need, and a generic
// editor with grouped controls, tooltips, and a status line. Parameter IDs and the state schema are listed in
// docs/parameters.md; keep the two in step.

#include <ak4522/ak4522.hpp>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ak4522demo {

namespace id {
// Chip controls, applied as pin events on the DAC frame timeline.
inline constexpr const char* deemphasis = "deemphasis";
inline constexpr const char* softMute = "softMute";
// Configuration: a change re-prepares the chip off the audio thread.
inline constexpr const char* profile = "profile";
inline constexpr const char* noise = "noise";
inline constexpr const char* distortion = "distortion";
inline constexpr const char* mismatch = "mismatch";
inline constexpr const char* crosstalk = "crosstalk";
inline constexpr const char* dcOffset = "dcOffset";
inline constexpr const char* outputCircuit = "outputCircuit";
// Host behavior after the chip.
inline constexpr const char* outputGain = "outputGain";
} // namespace id

// Saved-state format. Bump schemaVersion whenever parameters or their
// meaning change; older state is then rejected, not reinterpreted (REQ-VST-01).
inline constexpr int schemaVersion = 1;

// Base of both plugins. Derived classes add their own parameters through
// createLayout's `extra`, and implement prepareChip() and the processing.
class ChipPlugin : public juce::AudioProcessor,
                   private juce::AudioProcessorValueTreeState::Listener,
                   private juce::Timer {
public:
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    ChipPlugin(const BusesProperties& buses, const char* pluginKind, Layout layout,
               std::vector<juce::String> prepareParameterIds);
    ~ChipPlugin() override;

    // The chip parameters both plugins share, in their groups.
    static void addChipParameters(Layout& layout);
    // The "Plugin" group, holding output gain; a plugin adds its own host
    // parameters to it before adding it to the layout last.
    static std::unique_ptr<juce::AudioProcessorParameterGroup> pluginGroup();

    // State: the parameter tree plus schema, plugin kind, and noise seed.
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    // One line for the editor: configuration and latency, or why the plugin
    // is silent. Message thread.
    juce::String statusText() const;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override;
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool supportsDoublePrecisionProcessing() const override { return true; }

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}

    // Re-prepares the chip if a configuration parameter or the state changed.
    // Message thread; the timer calls it ten times a second.
    void applyPendingChanges();

    juce::AudioProcessorValueTreeState& parameters() noexcept { return state_; }
    std::uint64_t noiseSeed() const noexcept { return noiseSeed_; }

protected:
    // Builds the chip from the current parameters for `hostRate` and
    // `maxBlock`. Runs off the audio thread, never during processing. Returns
    // the status line; sets the latency itself.
    virtual juce::String prepareChip(double hostRate, int maxBlock) = 0;

    // The chip configuration both plugins share, from the parameters.
    ak4522::DacConfig chipConfig() const;
    bool outputCircuitEnabled() const;
    // Output gain as a factor. The parameter's float range does not return 0 dB
    // exactly, so the value is snapped to its 0.1 dB steps first.
    double outputGainFactor() const noexcept;
    bool noiseEnabled() const;

    // Pin events for parameter changes since the last call, at `offset`.
    // Audio thread; returns how many it wrote (at most 2).
    std::size_t controlEvents(std::uint32_t offset, std::span<ak4522::ControlEvent, 2> out) noexcept;
    void resetControls() noexcept; // after a prepare: the chip latched the pins

    float parameterValue(const char* parameterId) const noexcept;
    bool prepared() const noexcept { return prepared_.load(std::memory_order_acquire); }
    void setPrepared(bool ok) noexcept; // from prepareChip(): processing may run

    juce::AudioProcessorValueTreeState state_;

private:
    void parameterChanged(const juce::String& parameterId, float newValue) override;
    void timerCallback() override;
    void rebuild();

    const char* pluginKind_;
    std::vector<juce::String> prepareIds_;
    std::uint64_t noiseSeed_;
    double hostRate_ = 0.0;
    int maxBlock_ = 0;
    std::atomic<bool> needsPrepare_{false};
    std::atomic<bool> prepared_{false};
    int lastDeemphasis_ = -1;
    int lastSoftMute_ = -1;

    mutable juce::CriticalSection statusLock_;
    juce::String status_;
    juce::String stateNote_;
};

// Pin selection for each choice index of the deemphasis parameter.
inline constexpr std::array<ak4522::DeemphasisSelect, 4> deemphasisChoices{
    ak4522::DeemphasisSelect::off, ak4522::DeemphasisSelect::fs32000, ak4522::DeemphasisSelect::fs44100,
    ak4522::DeemphasisSelect::fs48000};

// A fixed delay of whole samples, sized in prepare(). Delays the dry signal
// by the plugin's reported latency so bypass and mix line up with the
// processed output (REQ-VST-02, REQ-LAT-03).
class DryDelay {
public:
    void prepare(int delaySamples, int channels);
    void reset() noexcept;
    // Writes `in` to the line and the sample from `delay` ago to `out`.
    template <typename T>
    void process(int channel, const T* in, double* out, int n) noexcept
    {
        auto& line = lines_[static_cast<std::size_t>(channel)];
        auto& pos = positions_[static_cast<std::size_t>(channel)];
        const auto size = static_cast<int>(line.size());
        if (size == 0) { // no latency: the dry signal needs no delay
            for (int i = 0; i < n; ++i) {
                out[i] = static_cast<double>(in[i]);
            }
            return;
        }
        for (int i = 0; i < n; ++i) {
            const double x = static_cast<double>(in[i]);
            out[i] = line[static_cast<std::size_t>(pos)];
            line[static_cast<std::size_t>(pos)] = x;
            pos = pos + 1 == size ? 0 : pos + 1;
        }
    }

private:
    std::vector<std::vector<double>> lines_;
    std::vector<int> positions_;
};

} // namespace ak4522demo
