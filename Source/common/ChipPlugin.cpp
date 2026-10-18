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

// One parameter: its name, a control for it, and its description as a
// tooltip. A right click outside the control opens the host's menu for it.
//
// The row registers no parameter listener. A listener runs on whichever
// thread changes the value, often the audio thread under automation, and
// JUCE's attachments post a message from there, which may block. Instead the
// editor's timer calls poll() on the message thread, as JUCE's generic
// editor does; edits go to the host from the message thread only.
class ParameterRow : public juce::Component, public juce::SettableTooltipClient {
public:
    ParameterRow(juce::AudioProcessorEditor& editor, juce::RangedAudioParameter& parameter)
        : editor_(editor), parameter_(parameter)
    {
        setComponentID(parameter.getParameterID());
        tooltip_ = tooltipFor(parameter.getParameterID());
        setTooltip(tooltip_);
        name_.setText(parameter.getName(64), juce::dontSendNotification);
        name_.setJustificationType(juce::Justification::centredRight);
        name_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(name_);

        if (parameter.isBoolean()) {
            auto toggle = std::make_unique<juce::ToggleButton>();
            toggle->onClick = [this, t = toggle.get()] { edit(t->getToggleState() ? 1.0f : 0.0f); };
            controlTip_ = toggle.get();
            toggle_ = toggle.get();
            control_ = std::move(toggle);
        } else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&parameter)) {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList(choice->choices, 1);
            box->onChange = [this, b = box.get()] {
                edit(parameter_.convertTo0to1(static_cast<float>(b->getSelectedItemIndex())));
            };
            controlTip_ = box.get();
            comboBox_ = box.get();
            control_ = std::move(box);
        } else {
            // The slider works in the parameter's normalized range, so the
            // parameter's own skew and steps apply.
            auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 22);
            slider->setRange(0.0, 1.0);
            const auto suffix = parameter.getLabel().isEmpty() ? juce::String() : " " + parameter.getLabel();
            slider->textFromValueFunction = [this, suffix](double v) {
                return parameter_.getText(static_cast<float>(v), 0) + suffix;
            };
            slider->valueFromTextFunction = [this, suffix](const juce::String& text) {
                return static_cast<double>(parameter_.getValueForText(text.upToLastOccurrenceOf(suffix, false, false)));
            };
            slider->setDoubleClickReturnValue(true, parameter.getDefaultValue());
            slider->onDragStart = [this] { parameter_.beginChangeGesture(); };
            slider->onDragEnd = [this] { parameter_.endChangeGesture(); };
            slider->onValueChange = [this, sl = slider.get()] {
                const auto snapped = parameter_.convertTo0to1(
                    parameter_.convertFrom0to1(static_cast<float>(sl->getValue())));
                if (sl->isMouseButtonDown()) {
                    parameter_.setValueNotifyingHost(snapped); // inside the drag's gesture
                } else {
                    edit(snapped); // typed text, a double click, or the keyboard
                }
            };
            controlTip_ = slider.get();
            slider_ = slider.get();
            control_ = std::move(slider);
        }
        controlTip_->setTooltip(tooltip_);
        addAndMakeVisible(*control_);
        poll();
    }

    // Shows the parameter's current value. Message thread.
    void poll()
    {
        const float value = parameter_.getValue();
        if (juce::exactlyEqual(value, shown_)) {
            return;
        }
        shown_ = value;
        if (toggle_ != nullptr) {
            toggle_->setToggleState(value >= 0.5f, juce::dontSendNotification);
        } else if (comboBox_ != nullptr) {
            comboBox_->setSelectedItemIndex(juce::roundToInt(parameter_.convertFrom0to1(value)),
                                            juce::dontSendNotification);
        } else if (!slider_->isMouseButtonDown()) {
            slider_->setValue(value, juce::dontSendNotification);
        }
    }

    // Greys the row out when the parameter has no effect, and says why in
    // the tooltip. The value is kept.
    void setActive(bool active, const juce::String& whyInactive)
    {
        if (active == isEnabled() && whyInactive == whyInactive_) {
            return;
        }
        whyInactive_ = whyInactive;
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
    // One complete change, as a host expects it from a click.
    void edit(float normalized)
    {
        parameter_.beginChangeGesture();
        parameter_.setValueNotifyingHost(normalized);
        parameter_.endChangeGesture();
    }

    juce::AudioProcessorEditor& editor_;
    juce::RangedAudioParameter& parameter_;
    juce::Label name_;
    juce::String tooltip_;
    juce::String whyInactive_;
    float shown_ = -1.0f; // the value the control shows; -1 forces the first update
    std::unique_ptr<juce::Component> control_;
    juce::SettableTooltipClient* controlTip_ = nullptr;
    // Exactly one of these is the control.
    juce::ToggleButton* toggle_ = nullptr;
    juce::ComboBox* comboBox_ = nullptr;
    juce::Slider* slider_ = nullptr;
};

// The parameters in their groups, one heading per group, in layout order.
// Rows the plugin reports as inactive are greyed out, and so is a heading
// whose rows all are.
class ParameterList : public juce::Component {
public:
    ParameterList(juce::AudioProcessorEditor& editor, const ChipPlugin& plugin) : plugin_(plugin)
    {
        for (const auto* node : editor.processor.getParameterTree()) {
            if (const auto* group = node->getGroup()) {
                auto heading = std::make_unique<juce::Label>(group->getID(), group->getName());
                heading->setFont(juce::FontOptions(15.0f, juce::Font::bold));
                heading->setComponentID(group->getID());
                addAndMakeVisible(*heading);
                Section section{heading.get(), {}};
                items_.push_back(std::move(heading));
                for (auto* parameter : group->getParameters(true)) {
                    if (auto* row = add(editor, *parameter)) {
                        section.rows.push_back(row);
                    }
                }
                sections_.push_back(std::move(section));
            } else if (auto* parameter = node->getParameter()) {
                add(editor, *parameter);
            }
        }
        int h = 0;
        for (const auto& item : items_) {
            h += dynamic_cast<ParameterRow*>(item.get()) != nullptr ? ParameterRow::height : headingHeight;
        }
        setSize(width, h);
        poll();
    }

    // Shows current values and which rows have an effect. Message thread.
    void poll()
    {
        for (auto& section : sections_) {
            bool anyActive = section.rows.empty();
            for (auto* row : section.rows) {
                row->poll();
                const auto reason = plugin_.inactiveReason(row->getComponentID());
                row->setActive(reason.isEmpty(), reason);
                anyActive = anyActive || reason.isEmpty();
            }
            section.heading->setEnabled(anyActive);
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

    struct Section {
        juce::Label* heading;
        std::vector<ParameterRow*> rows;
    };

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

    const ChipPlugin& plugin_;
    std::vector<std::unique_ptr<juce::Component>> items_;
    std::vector<Section> sections_;
};

// The status line, the grouped controls, and the version line. One timer
// polls the parameters and the status.
class StatusEditor : public juce::AudioProcessorEditor, public ParameterPolling, private juce::Timer {
public:
    explicit StatusEditor(ChipPlugin& plugin) : AudioProcessorEditor(plugin), plugin_(plugin), list_(*this, plugin)
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
        startTimerHz(30);
    }

    void pollParameters() override
    {
        list_.poll();
        status_.setText(plugin_.statusText(), juce::dontSendNotification);
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

    void timerCallback() override { pollParameters(); }

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
        "analog", "Analog imperfections (NominalAnalog only)", "|");
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

juce::String ChipPlugin::inactiveReason(const juce::String& parameterId) const
{
    const bool analogOption = parameterId == id::noise || parameterId == id::distortion
                              || parameterId == id::mismatch || parameterId == id::crosstalk
                              || parameterId == id::dcOffset;
    if (analogOption && parameterValue(id::profile) < 0.5f) {
        return "Inactive: Profile is Specification.";
    }
    return {};
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
