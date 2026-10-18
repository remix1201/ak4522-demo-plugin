// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "synth/SynthProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace ak4522demo {

namespace {

ChipPlugin::Layout synthLayout()
{
    using namespace juce;
    ChipPlugin::Layout layout;
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID{id::clock, 1}, "Source clock",
        StringArray{"32 kHz", "44.1 kHz", "48 kHz", "112 MHz / 2304 (48.611 kHz, extended)"}, 2));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id::sampleFormat, 1}, "Sample format",
                                                      StringArray{"20-bit", "16-bit"}, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id::waveform, 1}, "Waveform",
                                                      StringArray{"Sine", "Saw", "Square"}, 1));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::attack, 1}, "Attack",
                                                     NormalisableRange<float>{1.0f, 2000.0f, 1.0f, 0.4f}, 5.0f,
                                                     AudioParameterFloatAttributes{}.withLabel("ms")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::release, 1}, "Release",
                                                     NormalisableRange<float>{5.0f, 5000.0f, 1.0f, 0.4f}, 200.0f,
                                                     AudioParameterFloatAttributes{}.withLabel("ms")));
    ChipPlugin::addChipParameters(layout);
    return layout;
}

std::vector<juce::String> synthPrepareIds()
{
    return {id::clock, id::sampleFormat, id::profile, id::noise, id::distortion,
            id::mismatch, id::crosstalk, id::dcOffset, id::outputCircuit};
}

} // namespace

SynthProcessor::SynthProcessor()
    : ChipPlugin(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true), "synth",
                 synthLayout(), synthPrepareIds())
{
}

bool SynthProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
           && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SynthProcessor::reset()
{
    resetPending_.store(true);
}

juce::String SynthProcessor::prepareChip(double hostRate, int maxBlock)
{
    const auto clockIndex = static_cast<std::size_t>(parameterValue(id::clock));
    const ak4522::ExactRate clock = synthClocks[clockIndex];
    sixteenBit_ = parameterValue(id::sampleFormat) >= 0.5f;

    ak4522::DacStreamConfig c;
    c.hostRate = hostRate;
    c.dacRate = clock;
    c.format = sixteenBit_ ? ak4522::DacSampleFormat::int16 : ak4522::DacSampleFormat::float64;
    c.mode = ak4522::DacStreamMode::pull;
    c.maxBlockFrames = static_cast<std::size_t>(maxBlock);
    c.pull = &SynthProcessor::pull;
    c.pullContext = this;
    c.maxEventsPerBlock = pendingEvents_.size();
    c.exampleOutputCircuit = outputCircuitEnabled();
    c.dac = chipConfig();
    c.dac.allowExtendedRate = clock.hz() > ak4522::documentedMaxDacRate;

    if (const auto status = stream_.prepare(c); status != ak4522::Status::ok) {
        setLatencySamples(0);
        tailSeconds_.store(0.0);
        return "Silent: " + juce::String(ak4522::toString(status).data()) + " (host "
               + juce::String(hostRate / 1000.0, 1) + " kHz)";
    }
    // DAC frame j is presented at host frame j * hostRate / dacRate, so an
    // event at host frame t acts on DAC frame ceil(t * num / (den * hostRate)).
    const auto host = static_cast<std::uint64_t>(std::llround(hostRate));
    const std::uint64_t g = std::gcd(clock.numerator, clock.denominator * host);
    ratioNum_ = clock.numerator / g;
    ratioDen_ = clock.denominator * host / g;
    hostPosition_ = 0;
    pendingCount_ = 0;
    synth_.prepare(clock.hz());
    gain_.reset(hostRate, 0.02);
    gain_.setCurrentAndTargetValue(outputGainFactor());
    resetPending_.store(false);

    const auto latency = stream_.latency();
    const int samples = static_cast<int>(latency.reportedHostFrames);
    setLatencySamples(samples);
    const double release = parameterValue(id::release) / 1000.0;
    tailSeconds_.store(noiseEnabled() ? std::numeric_limits<double>::infinity()
                                      : latency.totalSeconds + release + (c.exampleOutputCircuit ? 4.0 : 0.1));
    setPrepared(true);
    return "Clock " + juce::String(clock.hz(), 1) + " Hz" + (c.dac.allowExtendedRate ? " (extended)" : "")
           + ", latency " + juce::String(samples) + " samples (" + juce::String(1000.0 * samples / hostRate, 2)
           + " ms)";
}

void SynthProcessor::pull(void* context, ak4522::DacPullBlock& block) noexcept
{
    auto& self = *static_cast<SynthProcessor*>(context);
    if (self.sixteenBit_) {
        // The synth renders doubles; 16-bit input is rounded and saturated.
        std::array<double, 256> scratch{};
        for (std::size_t at = 0; at < block.frames; at += scratch.size()) {
            const std::size_t k = std::min(scratch.size(), block.frames - at);
            self.synth_.render(scratch.data(), k);
            for (std::size_t i = 0; i < k; ++i) {
                const double v = std::clamp(std::round(scratch[i] * 32768.0), -32768.0, 32767.0);
                block.integers.left[at + i] = block.integers.right[at + i] = static_cast<std::int32_t>(v);
            }
        }
    } else {
        self.synth_.render(block.doubles.left.data(), block.frames);
        std::copy_n(block.doubles.left.begin(), block.frames, block.doubles.right.begin());
    }
    // Pin changes act on the first DAC frame of this request.
    const std::size_t n = std::min(self.pendingCount_, block.events.size());
    std::copy_n(self.pendingEvents_.begin(), n, block.events.begin());
    block.eventCount = n;
    self.pendingCount_ = 0;
}

template <typename T>
void SynthProcessor::run(juce::AudioBuffer<T>& buffer, juce::MidiBuffer& midi) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (!prepared() || buffer.getNumChannels() < 2) {
        buffer.clear();
        return;
    }
    if (resetPending_.exchange(false)) {
        stream_.reset();
        synth_.reset();
        hostPosition_ = 0;
        pendingCount_ = 0;
    }
    synth_.setSound(static_cast<Waveform>(static_cast<int>(parameterValue(id::waveform))),
                    parameterValue(id::attack) / 1000.0, parameterValue(id::release) / 1000.0);
    gain_.setTargetValue(outputGainFactor());

    for (const auto meta : midi) {
        const auto* raw = meta.data;
        if (meta.numBytes < 1 || meta.numBytes > 3) {
            continue; // sysex and other long messages are ignored
        }
        const std::uint64_t t = hostPosition_ + static_cast<std::uint64_t>(std::max(0, meta.samplePosition));
        TestSynth::Event e;
        e.frame = (t * ratioNum_ + ratioDen_ - 1) / ratioDen_;
        e.status = raw[0];
        e.data1 = meta.numBytes > 1 ? raw[1] : 0;
        e.data2 = meta.numBytes > 2 ? raw[2] : 0;
        if (!synth_.queue(e)) {
            droppedMidi_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    std::array<ak4522::ControlEvent, 2> changes{};
    const std::size_t changed = controlEvents(0, changes);
    for (std::size_t i = 0; i < changed && pendingCount_ < pendingEvents_.size(); ++i) {
        pendingEvents_[pendingCount_++] = changes[i];
    }

    T* l = buffer.getWritePointer(0);
    T* r = buffer.getWritePointer(1);
    const auto frames = static_cast<std::size_t>(n);
    if (stream_.render(ak4522::StereoView<T>{std::span<T>(l, frames), std::span<T>(r, frames)})
        != ak4522::Status::ok) {
        buffer.clear();
    }
    for (int i = 0; i < n; ++i) {
        const double g = gain_.getNextValue();
        l[i] = static_cast<T>(g * static_cast<double>(l[i]));
        r[i] = static_cast<T>(g * static_cast<double>(r[i]));
    }
    hostPosition_ += frames;
}

} // namespace ak4522demo

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ak4522demo::SynthProcessor();
}
