// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "common/ChipPlugin.h"

#include "BuildInfo.h"
#include "common/Descriptions.h"

#include <cmath>
#include <cstdlib>

namespace ak4522demo {

namespace {

constexpr const char* stateType = "AK4522DemoState";
// The group of NominalAnalog options, which do nothing under Specification.
constexpr const char* analogGroup = "analog";

// One parameter: its name, a control bound to it, and its description as a
// tooltip. A right click outside the control opens the host's menu for it.
class ParameterRow : public juce::Component, public juce::SettableTooltipClient {
public:
    ParameterRow(juce::AudioProcessorEditor& editor, juce::RangedAudioParameter& parameter)
        : editor_(editor), parameter_(parameter)
    {
        setComponentID(parameter.getParameterID());
        const auto tooltip = tooltipFor(parameter.getParameterID());
        tooltip_ = tooltip;
        setTooltip(tooltip);
        name_.setText(parameter.getName(64), juce::dontSendNotification);
        name_.setJustificationType(juce::Justification::centredRight);
        name_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(name_);

        if (parameter.isBoolean()) {
            auto toggle = std::make_unique<juce::ToggleButton>();
            toggle->setTooltip(tooltip);
            controlTip_ = toggle.get();
            button_ = std::make_unique<juce::ButtonParameterAttachment>(parameter, *toggle);
            control_ = std::move(toggle);
        } else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&parameter)) {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList(choice->choices, 1);
            box->setTooltip(tooltip);
            controlTip_ = box.get();
            comboBox_ = std::make_unique<juce::ComboBoxParameterAttachment>(parameter, *box);
            control_ = std::move(box);
        } else {
            auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 22);
            slider->setTooltip(tooltip);
            controlTip_ = slider.get();
            slider_ = std::make_unique<juce::SliderParameterAttachment>(parameter, *slider);
            if (parameter.getLabel().isNotEmpty()) {
                slider->setTextValueSuffix(" " + parameter.getLabel());
            }
            control_ = std::move(slider);
        }
        addAndMakeVisible(*control_);
    }

    // Greys the row out when the parameter has no effect, and says why in
    // the tooltip. The value is kept.
    void setActive(bool active, const juce::String& whyInactive)
    {
        setEnabled(active);
        const auto tip = active ? tooltip_ : tooltip_ + "\n" + whyInactive;
        setTooltip(tip);
        controlTip_->setTooltip(tip);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4, 3);
        name_.setBounds(area.removeFromLeft(nameWidth));
        area.removeFromLeft(8);
        control_->setBounds(area);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isPopupMenu()) {
            return;
        }
        if (auto* context = editor_.getHostContext()) {
            if (auto menu = context->getContextMenuForParameter(&parameter_)) {
                menu->getEquivalentPopupMenu().showMenuAsync(
                    juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
            }
        }
    }

    static constexpr int height = 32;
    static constexpr int nameWidth = 150;

private:
    juce::AudioProcessorEditor& editor_;
    juce::RangedAudioParameter& parameter_;
    juce::Label name_;
    juce::String tooltip_;
    std::unique_ptr<juce::Component> control_;
    juce::SettableTooltipClient* controlTip_ = nullptr;
    // One of these binds the control to the parameter.
    std::unique_ptr<juce::ButtonParameterAttachment> button_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> comboBox_;
    std::unique_ptr<juce::SliderParameterAttachment> slider_;
};

// The parameters in their groups, one heading per group, in layout order.
class ParameterList : public juce::Component {
public:
    explicit ParameterList(juce::AudioProcessorEditor& editor)
    {
        for (const auto* node : editor.processor.getParameterTree()) {
            if (const auto* group = node->getGroup()) {
                auto heading = std::make_unique<juce::Label>(group->getID(), group->getName());
                heading->setFont(juce::FontOptions(15.0f, juce::Font::bold));
                heading->setComponentID(group->getID());
                addAndMakeVisible(*heading);
                const bool analog = group->getID() == analogGroup;
                if (analog) {
                    analogHeading_ = heading.get();
                }
                items_.push_back(std::move(heading));
                for (auto* parameter : group->getParameters(true)) {
                    if (auto* row = add(editor, *parameter); row != nullptr && analog) {
                        analogRows_.push_back(row);
                    }
                }
            } else if (auto* parameter = node->getParameter()) {
                add(editor, *parameter);
            }
        }
        int h = 0;
        for (const auto& item : items_) {
            h += dynamic_cast<ParameterRow*>(item.get()) != nullptr ? ParameterRow::height : headingHeight;
        }
        setSize(width, h);

        // The analog options apply only to the NominalAnalog profile.
        for (auto* parameter : editor.processor.getParameters()) {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
                ranged != nullptr && ranged->getParameterID() == id::profile) {
                profile_ = std::make_unique<juce::ParameterAttachment>(
                    *ranged, [this](float value) { showAnalogActive(value >= 0.5f); });
                profile_->sendInitialUpdate();
            }
        }
    }

    void resized() override
    {
        int y = 0;
        for (const auto& item : items_) {
            const int h = dynamic_cast<ParameterRow*>(item.get()) != nullptr ? ParameterRow::height : headingHeight;
            item->setBounds(0, y, getWidth(), h);
            y += h;
        }
    }

    static constexpr int width = 480;

private:
    static constexpr int headingHeight = 30;

    ParameterRow* add(juce::AudioProcessorEditor& editor, juce::AudioProcessorParameter& parameter)
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(&parameter);
        if (ranged == nullptr) {
            return nullptr;
        }
        auto row = std::make_unique<ParameterRow>(editor, *ranged);
        auto* added = row.get();
        addAndMakeVisible(*row);
        items_.push_back(std::move(row));
        return added;
    }

    void showAnalogActive(bool active)
    {
        if (analogHeading_ != nullptr) {
            analogHeading_->setEnabled(active);
        }
        for (auto* row : analogRows_) {
            row->setActive(active, "Inactive: Profile is Specification.");
        }
    }

    std::vector<std::unique_ptr<juce::Component>> items_;
    juce::Label* analogHeading_ = nullptr;
    std::vector<ParameterRow*> analogRows_;
    std::unique_ptr<juce::ParameterAttachment> profile_; // last: destroyed first
};

// The status line above the grouped controls.
class StatusEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit StatusEditor(ChipPlugin& plugin) : AudioProcessorEditor(plugin), plugin_(plugin), list_(*this)
    {
        status_.setJustificationType(juce::Justification::centredLeft);
        status_.setText(plugin_.statusText(), juce::dontSendNotification);
        addAndMakeVisible(status_);
        view_.setViewedComponent(&list_, false);
        view_.setScrollBarsShown(true, false);
        addAndMakeVisible(view_);
        version_.setText(ChipPlugin::versionText(), juce::dontSendNotification);
        version_.setJustificationType(juce::Justification::centredLeft);
        version_.setFont(juce::FontOptions(12.0f));
        version_.setColour(juce::Label::textColourId,
                           getLookAndFeel().findColour(juce::Label::textColourId).withAlpha(0.6f));
        addAndMakeVisible(version_);
        const int scrollBar = view_.getScrollBarThickness();
        setSize(ParameterList::width + scrollBar,
                statusHeight + std::min(list_.getHeight(), maxListHeight) + versionHeight);
        startTimerHz(4);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        status_.setBounds(area.removeFromTop(statusHeight).reduced(8, 0));
        version_.setBounds(area.removeFromBottom(versionHeight).reduced(8, 0));
        view_.setBounds(area);
        list_.setSize(view_.getMaximumVisibleWidth(), list_.getHeight());
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    }

private:
    static constexpr int statusHeight = 28;
    static constexpr int maxListHeight = 560;
    static constexpr int versionHeight = 22;

    void timerCallback() override { status_.setText(plugin_.statusText(), juce::dontSendNotification); }

    ChipPlugin& plugin_;
    juce::Label status_;
    ParameterList list_;
    juce::Viewport view_;
    juce::Label version_;
    juce::TooltipWindow tooltips_{this, 500};
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
    auto pins = std::make_unique<AudioProcessorParameterGroup>("pins", "Chip pins", "|");
    pins->addChild(std::make_unique<AudioParameterChoice>(ParameterID{id::deemphasis, 1}, "De-emphasis",
                                                          StringArray{"Off", "32 kHz", "44.1 kHz", "48 kHz"}, 0));
    pins->addChild(std::make_unique<AudioParameterBool>(ParameterID{id::softMute, 1}, "Soft mute", false));
    layout.add(std::move(pins));

    auto model = std::make_unique<AudioProcessorParameterGroup>("model", "Model", "|");
    model->addChild(std::make_unique<AudioParameterChoice>(ParameterID{id::profile, 1}, "Profile",
                                                           StringArray{"Specification", "NominalAnalog"}, 0));
    layout.add(std::move(model));

    auto analog = std::make_unique<AudioProcessorParameterGroup>(
        analogGroup, "Analog imperfections (NominalAnalog only)", "|");
    analog->addChild(std::make_unique<AudioParameterBool>(ParameterID{id::noise, 1}, "Noise", true));
    analog->addChild(std::make_unique<AudioParameterBool>(ParameterID{id::distortion, 1}, "Distortion", true));
    analog->addChild(std::make_unique<AudioParameterBool>(ParameterID{id::mismatch, 1}, "Channel mismatch", true));
    analog->addChild(std::make_unique<AudioParameterBool>(ParameterID{id::crosstalk, 1}, "Crosstalk", true));
    analog->addChild(std::make_unique<AudioParameterBool>(ParameterID{id::dcOffset, 1}, "DC offset", false));
    layout.add(std::move(analog));

    auto board = std::make_unique<AudioProcessorParameterGroup>("board", "Board circuit (not part of the chip)", "|");
    board->addChild(std::make_unique<AudioParameterBool>(ParameterID{id::outputCircuit, 1}, "Fig.11 output circuit",
                                                         false));
    layout.add(std::move(board));
}

std::unique_ptr<juce::AudioProcessorParameterGroup> ChipPlugin::pluginGroup()
{
    using namespace juce;
    auto group = std::make_unique<AudioProcessorParameterGroup>("plugin", "Plugin (not part of the chip)", "|");
    group->addChild(std::make_unique<AudioParameterFloat>(ParameterID{id::outputGain, 1}, "Output gain",
                                                          NormalisableRange<float>{-24.0f, 12.0f, 0.1f}, 0.0f,
                                                          AudioParameterFloatAttributes{}.withLabel("dB")));
    return group;
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

juce::String ChipPlugin::versionText()
{
    const auto library = ak4522::versionString();
    return juce::String(JucePlugin_Name) + " " + JucePlugin_VersionString + "  |  ak4522 "
           + juce::String(library.data(), library.size()) + "  |  commit " + build::commit;
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
