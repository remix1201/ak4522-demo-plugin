// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "effect/EffectProcessor.h"

#include <cmath>
#include <limits>

namespace ak4522demo {

namespace {

ChipPlugin::Layout effectLayout()
{
    using namespace juce;
    ChipPlugin::Layout layout;
    auto converter = std::make_unique<AudioProcessorParameterGroup>("converter", "Converter", "|");
    converter->addChild(std::make_unique<AudioParameterChoice>(
        ParameterID{id::dacRate, 1}, "DAC rate",
        StringArray{"16 kHz", "22.05 kHz", "24 kHz", "32 kHz", "44.1 kHz", "48 kHz"}, 4));
    converter->addChild(std::make_unique<AudioParameterChoice>(ParameterID{id::inputResolution, 1},
                                                               "Input resolution", StringArray{"20-bit", "16-bit"}, 0));
    converter->addChild(std::make_unique<AudioParameterBool>(ParameterID{id::inputDither, 1}, "Input dither", false));
    converter->addChild(std::make_unique<AudioParameterChoice>(ParameterID{id::path, 1}, "Path",
                                                               StringArray{"DAC", "DAC into ADC", "ADC"}, 0));
    layout.add(std::move(converter));
    ChipPlugin::addChipParameters(layout);
    auto host = ChipPlugin::pluginGroup();
    host->addChild(std::make_unique<AudioParameterFloat>(ParameterID{id::mix, 1}, "Mix",
                                                         NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 100.0f,
                                                         AudioParameterFloatAttributes{}.withLabel("%")));
    host->addChild(std::make_unique<AudioParameterBool>(ParameterID{id::delta, 1}, "Delta listen", false));
    host->addChild(std::make_unique<AudioParameterBool>(ParameterID{id::bypass, 1}, "Bypass", false));
    layout.add(std::move(host));
    return layout;
}

// Parameters whose change re-prepares the chip.
std::vector<juce::String> effectPrepareIds()
{
    return {id::dacRate, id::inputResolution, id::inputDither, id::path, id::profile, id::noise,
            id::distortion, id::mismatch, id::crosstalk, id::dcOffset, id::outputCircuit};
}

const char* pathName(ak4522::HostPath p)
{
    switch (p) {
    case ak4522::HostPath::dac: return "DAC";
    case ak4522::HostPath::loopback: return "DAC into ADC";
    case ak4522::HostPath::adc: return "ADC";
    }
    return "?";
}

} // namespace

EffectProcessor::EffectProcessor()
    : ChipPlugin(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true),
                 "effect", effectLayout(), effectPrepareIds())
{
}

bool EffectProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Stereo in, stereo out only (REQ-VST-01); there is no mono policy.
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorParameter* EffectProcessor::getBypassParameter() const
{
    return state_.getParameter(id::bypass);
}

void EffectProcessor::reset()
{
    resetPending_.store(true); // applied at the start of the next block
}

juce::String EffectProcessor::prepareChip(double hostRate, int maxBlock)
{
    ak4522::HostConfig c;
    c.hostRate = hostRate;
    c.maxBlockFrames = static_cast<std::size_t>(maxBlock);
    c.dac = chipConfig();
    c.dac.dacRate = effectDacRates[static_cast<std::size_t>(parameterValue(id::dacRate))];
    c.inputResolution = parameterValue(id::inputResolution) >= 0.5f ? ak4522::InputResolution::bits16
                                                                     : ak4522::InputResolution::bits20;
    c.inputDither = parameterValue(id::inputDither) >= 0.5f;
    c.ditherSeed = noiseSeed() ^ 0x4449544845520001ull;
    c.path = static_cast<ak4522::HostPath>(static_cast<int>(parameterValue(id::path)));
    c.exampleOutputCircuit = outputCircuitEnabled();

    if (const auto status = host_.prepare(c); status != ak4522::Status::ok) {
        setLatencySamples(0);
        tailSeconds_.store(0.0);
        return "Silent: " + juce::String(ak4522::toString(status).data()) + " (host "
               + juce::String(hostRate / 1000.0, 1) + " kHz)";
    }
    const auto latency = host_.latency();
    const int samples = static_cast<int>(latency.reportedHostFrames);
    setLatencySamples(samples);
    dry_.prepare(samples, 2);
    chunk_ = maxBlock;
    dryL_.assign(static_cast<std::size_t>(maxBlock), 0.0);
    dryR_.assign(static_cast<std::size_t>(maxBlock), 0.0);
    gain_.reset(hostRate, 0.02);
    mix_.reset(hostRate, 0.02);
    bypass_.reset(hostRate, 0.02);
    gain_.setCurrentAndTargetValue(outputGainFactor());
    mix_.setCurrentAndTargetValue(parameterValue(id::mix) / 100.0);
    bypass_.setCurrentAndTargetValue(parameterValue(id::bypass) >= 0.5f ? 1.0 : 0.0);
    resetPending_.store(false);

    // Tail policy (REQ-VST-02): infinite while noise plays; otherwise the
    // latency plus the time the slowest filter on the path takes to fall
    // 120 dB (docs/parameters.md).
    double settle = c.path == ak4522::HostPath::dac ? 0.1 : 3.0;
    if (c.exampleOutputCircuit) {
        settle = std::max(settle, 4.0);
    }
    tailSeconds_.store(noiseEnabled() ? std::numeric_limits<double>::infinity()
                                      : latency.totalSeconds + settle);
    setPrepared(true);
    return juce::String(pathName(c.path)) + " at " + juce::String(c.dac.dacRate / 1000.0, 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".")
           + " kHz, latency " + juce::String(samples) + " samples ("
           + juce::String(1000.0 * samples / hostRate, 2) + " ms)";
}

template <typename T>
void EffectProcessor::run(juce::AudioBuffer<T>& buffer, bool hostBypassed) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (!prepared() || buffer.getNumChannels() < 2) {
        buffer.clear();
        return;
    }
    if (resetPending_.exchange(false)) {
        host_.reset(); // host lifecycle reset, not the PD pin
        dry_.reset();
    }
    gain_.setTargetValue(outputGainFactor());
    mix_.setTargetValue(parameterValue(id::mix) / 100.0);
    bypass_.setTargetValue(hostBypassed || parameterValue(id::bypass) >= 0.5f ? 1.0 : 0.0);
    const bool delta = parameterValue(id::delta) >= 0.5f;

    // Parameter changes act at the start of the block: JUCE delivers one
    // value per block, so pin events cannot be placed inside it.
    std::array<ak4522::ControlEvent, 2> events{};
    std::size_t eventCount = controlEvents(0, events);

    for (int at = 0; at < n; at += chunk_) {
        const int k = std::min(chunk_, n - at);
        const auto frames = static_cast<std::size_t>(k);
        T* l = buffer.getWritePointer(0, at);
        T* r = buffer.getWritePointer(1, at);
        dry_.process(0, l, dryL_.data(), k);
        dry_.process(1, r, dryR_.data(), k);
        const auto status = host_.process(
            ak4522::StereoView<const T>{std::span<const T>(l, frames), std::span<const T>(r, frames)},
            ak4522::StereoView<T>{std::span<T>(l, frames), std::span<T>(r, frames)},
            std::span(events).first(eventCount));
        eventCount = 0;
        if (status != ak4522::Status::ok) {
            std::fill(l, l + k, T(0));
            std::fill(r, r + k, T(0));
            continue;
        }
        for (int i = 0; i < k; ++i) {
            const double g = gain_.getNextValue();
            const double m = mix_.getNextValue();
            const double b = bypass_.getNextValue();
            const auto blend = [&](double wet, double dry) {
                const double y = (delta ? wet - dry : m * wet + (1.0 - m) * dry) * g;
                return static_cast<T>((1.0 - b) * y + b * dry);
            };
            const auto si = static_cast<std::size_t>(i);
            l[i] = blend(static_cast<double>(l[i]), dryL_[si]);
            r[i] = blend(static_cast<double>(r[i]), dryR_[si]);
        }
    }
}

} // namespace ak4522demo

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ak4522demo::EffectProcessor();
}
