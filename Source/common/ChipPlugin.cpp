// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "common/ChipPlugin.h"

#include <cmath>
#include <cstdlib>

namespace ak4522demo {

namespace {

constexpr const char* stateType = "AK4522DemoState";

// The status line and the generic parameter controls.
class StatusEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit StatusEditor(ChipPlugin& plugin) : AudioProcessorEditor(plugin), plugin_(plugin), controls_(plugin)
    {
        status_.setJustificationType(juce::Justification::centredLeft);
        status_.setText(plugin_.statusText(), juce::dontSendNotification);
        addAndMakeVisible(status_);
        addAndMakeVisible(controls_);
        setSize(std::max(controls_.getWidth(), 420), controls_.getHeight() + statusHeight);
        startTimerHz(4);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        status_.setBounds(area.removeFromTop(statusHeight).reduced(8, 0));
        controls_.setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    }

private:
    static constexpr int statusHeight = 28;

    void timerCallback() override { status_.setText(plugin_.statusText(), juce::dontSendNotification); }

    ChipPlugin& plugin_;
    juce::Label status_;
    juce::GenericAudioProcessorEditor controls_;
};

} // namespace

ChipPlugin::ChipPlugin(const BusesProperties& buses, const char* pluginKind, Layout layout,
                       std::vector<juce::String> prepareParameterIds)
    : AudioProcessor(buses),
      state_(*this, nullptr, stateType, std::move(layout)),
      pluginKind_(pluginKind),
      prepareIds_(std::move(prepareParameterIds)),
      noiseSeed_(ak4522::AnalogOptions{}.noiseSeed)
{
    for (const auto& parameterId : prepareIds_) {
        state_.addParameterListener(parameterId, this);
    }
    status_ = "Not prepared";
    startTimerHz(10);
}

ChipPlugin::~ChipPlugin()
{
    stopTimer();
    for (const auto& parameterId : prepareIds_) {
        state_.removeParameterListener(parameterId, this);
    }
}

void ChipPlugin::addChipParameters(Layout& layout)
{
    using namespace juce;
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id::deemphasis, 1}, "De-emphasis",
                                                      StringArray{"Off", "32 kHz", "44.1 kHz", "48 kHz"}, 0));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{id::softMute, 1}, "Soft mute", false));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id::profile, 1}, "Profile",
                                                      StringArray{"Specification", "NominalAnalog"}, 0));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{id::noise, 1}, "Noise", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{id::distortion, 1}, "Distortion", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{id::mismatch, 1}, "Channel mismatch", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{id::crosstalk, 1}, "Crosstalk", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{id::dcOffset, 1}, "DC offset", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{id::outputCircuit, 1}, "Fig.11 output circuit",
                                                    false));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::outputGain, 1}, "Output gain",
                                                     NormalisableRange<float>{-24.0f, 12.0f, 0.1f}, 0.0f,
                                                     AudioParameterFloatAttributes{}.withLabel("dB")));
}

float ChipPlugin::parameterValue(const char* parameterId) const noexcept
{
    return state_.getRawParameterValue(parameterId)->load(std::memory_order_relaxed);
}

ak4522::DacConfig ChipPlugin::chipConfig() const
{
    ak4522::DacConfig c;
    const bool nominal = parameterValue(id::profile) >= 0.5f;
    c.profile = nominal ? ak4522::ModelProfile::nominalAnalog : ak4522::ModelProfile::specification;
    if (nominal) {
        c.analog.noise = parameterValue(id::noise) >= 0.5f;
        c.analog.distortion = parameterValue(id::distortion) >= 0.5f;
        c.analog.channelMismatch = parameterValue(id::mismatch) >= 0.5f;
        c.analog.crosstalk = parameterValue(id::crosstalk) >= 0.5f;
        c.analog.dcOffset = parameterValue(id::dcOffset) >= 0.5f;
    }
    c.analog.noiseSeed = noiseSeed_;
    c.initialDeemphasis = deemphasisChoices[static_cast<std::size_t>(parameterValue(id::deemphasis))];
    c.initialSoftMute = parameterValue(id::softMute) >= 0.5f;
    return c;
}

double ChipPlugin::outputGainFactor() const noexcept
{
    const double db = std::round(static_cast<double>(parameterValue(id::outputGain)) * 10.0) / 10.0;
    return juce::Decibels::decibelsToGain(db);
}

bool ChipPlugin::outputCircuitEnabled() const
{
    return parameterValue(id::outputCircuit) >= 0.5f;
}

bool ChipPlugin::noiseEnabled() const
{
    return parameterValue(id::profile) >= 0.5f && parameterValue(id::noise) >= 0.5f;
}

std::size_t ChipPlugin::controlEvents(std::uint32_t offset, std::span<ak4522::ControlEvent, 2> out) noexcept
{
    std::size_t n = 0;
    const int dem = static_cast<int>(parameterValue(id::deemphasis));
    if (dem != lastDeemphasis_) {
        out[n++] = ak4522::ControlEvent::deemphasis(offset, deemphasisChoices[static_cast<std::size_t>(dem)]);
        lastDeemphasis_ = dem;
    }
    const int mute = parameterValue(id::softMute) >= 0.5f ? 1 : 0;
    if (mute != lastSoftMute_) {
        out[n++] = ak4522::ControlEvent::softMute(offset, mute == 1);
        lastSoftMute_ = mute;
    }
    return n;
}

void ChipPlugin::resetControls() noexcept
{
    lastDeemphasis_ = static_cast<int>(parameterValue(id::deemphasis));
    lastSoftMute_ = parameterValue(id::softMute) >= 0.5f ? 1 : 0;
}

bool ChipPlugin::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

void ChipPlugin::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    hostRate_ = sampleRate;
    maxBlock_ = std::max(maximumExpectedSamplesPerBlock, 1);
    needsPrepare_.store(false);
    rebuild();
}

void ChipPlugin::rebuild()
{
    if (hostRate_ <= 0.0) {
        return; // not yet given a rate by the host
    }
    prepared_.store(false, std::memory_order_release);
    const juce::String line = prepareChip(hostRate_, maxBlock_);
    resetControls();
    {
        const juce::ScopedLock lock(statusLock_);
        status_ = line;
    }
}

void ChipPlugin::setPrepared(bool ok) noexcept
{
    prepared_.store(ok, std::memory_order_release);
}

void ChipPlugin::parameterChanged(const juce::String&, float)
{
    needsPrepare_.store(true); // any thread, including the audio thread
}

void ChipPlugin::timerCallback()
{
    applyPendingChanges();
}

void ChipPlugin::applyPendingChanges()
{
    if (!needsPrepare_.exchange(false)) {
        return;
    }
    // suspendProcessing holds the callback lock, so processing is not running
    // while the chip is rebuilt; the host hears silence meanwhile.
    suspendProcessing(true);
    rebuild();
    suspendProcessing(false);
}

void ChipPlugin::getStateInformation(juce::MemoryBlock& dest)
{
    auto tree = state_.copyState();
    tree.setProperty("schema", schemaVersion, nullptr);
    tree.setProperty("plugin", pluginKind_, nullptr);
    tree.setProperty("noiseSeed", juce::String(static_cast<juce::uint64>(noiseSeed_)), nullptr);
    if (auto xml = tree.createXml()) {
        copyXmlToBinary(*xml, dest);
    }
}

void ChipPlugin::setStateInformation(const void* data, int size)
{
    const auto xml = getXmlFromBinary(data, size);
    juce::String problem;
    if (xml == nullptr || !xml->hasTagName(stateType)) {
        problem = "not an AK4522 demo state";
    } else if (xml->getStringAttribute("plugin") != pluginKind_) {
        problem = "saved by the other demo plugin";
    } else if (xml->getIntAttribute("schema", -1) != schemaVersion) {
        problem = "schema " + xml->getStringAttribute("schema", "missing") + ", expected "
                  + juce::String(schemaVersion);
    }
    if (problem.isNotEmpty()) {
        // Reject rather than guess (REQ-VST-01): keep the current settings.
        const juce::ScopedLock lock(statusLock_);
        stateNote_ = "Saved state ignored: " + problem;
        return;
    }
    noiseSeed_ = std::strtoull(xml->getStringAttribute("noiseSeed").toRawUTF8(), nullptr, 10);
    state_.replaceState(juce::ValueTree::fromXml(*xml));
    {
        const juce::ScopedLock lock(statusLock_);
        stateNote_ = {};
    }
    needsPrepare_.store(true);
}

juce::String ChipPlugin::statusText() const
{
    const juce::ScopedLock lock(statusLock_);
    return stateNote_.isEmpty() ? status_ : status_ + "  |  " + stateNote_;
}

juce::AudioProcessorEditor* ChipPlugin::createEditor()
{
    return new StatusEditor(*this);
}

void DryDelay::prepare(int delaySamples, int channels)
{
    lines_.assign(static_cast<std::size_t>(channels), std::vector<double>(static_cast<std::size_t>(delaySamples), 0.0));
    positions_.assign(static_cast<std::size_t>(channels), 0);
}

void DryDelay::reset() noexcept
{
    for (auto& line : lines_) {
        std::fill(line.begin(), line.end(), 0.0);
    }
    std::fill(positions_.begin(), positions_.end(), 0);
}

} // namespace ak4522demo
