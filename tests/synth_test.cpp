// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// The instrument plugin: MIDI placed on the DAC frame timeline, block-size
// independence, the source clocks including 112 MHz / 2304, float and
// double, real-time safety, and state.

#include "support.h"
#include "synth/SynthProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <random>

using ak4522demo::SynthProcessor;
namespace id = ak4522demo::id;

namespace {

std::unique_ptr<SynthProcessor> make(double hostRate, int clock = 2, int block = 512)
{
    auto p = std::make_unique<SynthProcessor>();
    test::set(*p, id::clock, static_cast<float>(clock));
    p->setRateAndBufferSizeDetails(hostRate, block);
    p->prepareToPlay(hostRate, block);
    return p;
}

juce::MidiBuffer notes()
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 1000);
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.6f), 1001);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 9000);
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 12345);
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 20000);
    return midi;
}

std::vector<double> play(SynthProcessor& p, std::size_t frames, std::vector<int> sizes)
{
    auto midi = notes();
    return test::run<double>(p, std::vector<double>(frames, 0.0), std::move(sizes), false, &midi);
}

} // namespace

TEST_CASE("Only a stereo output is supported")
{
    SynthProcessor p;
    juce::AudioProcessor::BusesLayout stereo, mono;
    stereo.outputBuses.add(juce::AudioChannelSet::stereo());
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK(p.checkBusesLayoutSupported(stereo));
    CHECK_FALSE(p.checkBusesLayoutSupported(mono));
}

TEST_CASE("Every source clock prepares at every host rate")
{
    for (const double host : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
        for (int clock = 0; clock < 4; ++clock) {
            CAPTURE(host, clock);
            auto p = make(host, clock);
            CHECK(p->getLatencySamples() > 0);
            CHECK_FALSE(p->statusText().startsWith("Silent"));
            CHECK(p->statusText().contains("(extended)") == (clock == 3));
        }
    }
}

TEST_CASE("Silence in, silence out, until a note plays")
{
    auto p = make(48000.0);
    const auto out = play(*p, 24000, {512});
    for (std::size_t i = 0; i < 900; ++i) {
        REQUIRE(out[i] == 0.0); // nothing before the first note, minus the filter's pre-ringing
    }
    CHECK(test::rms(out, 2000, 8000) > 0.01);
}

TEST_CASE("Output does not depend on host block sizes, with MIDI at any offset")
{
    for (const int clock : {1, 3}) {
        for (const double host : {44100.0, 96000.0}) {
            CAPTURE(clock, host);
            std::mt19937 rng(static_cast<unsigned>(clock * 100 + static_cast<int>(host / 1000)));
            std::uniform_int_distribution<int> size(1, 512);
            std::vector<int> sizes(300);
            for (auto& s : sizes) {
                s = size(rng);
            }
            auto a = make(host, clock);
            auto b = make(host, clock);
            const auto one = play(*a, 24000, {512});
            const auto many = play(*b, 24000, sizes);
            double m = 0.0;
            for (std::size_t i = 0; i < one.size(); ++i) {
                m = std::max(m, std::abs(one[i] - many[i]));
            }
            CHECK(m == 0.0);
        }
    }
}

TEST_CASE("A note starts on the DAC frame its host time maps to")
{
    // The same note sent one host frame later at 96 kHz moves by at most one
    // DAC frame at 48 kHz, so the output moves by 0 or 2 host frames, never
    // by a fraction.
    auto at = [](int offset) {
        auto p = make(96000.0, 2);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), offset);
        return test::run<double>(*p, std::vector<double>(9600, 0.0), {512}, false, &midi);
    };
    const auto even = at(2000);
    const auto odd = at(2001);
    const auto later = at(2002);
    double moved = 0.0, shifted = 0.0;
    for (std::size_t i = 0; i + 2 < even.size(); ++i) {
        moved = std::max(moved, std::abs(even[i] - odd[i]));
        shifted = std::max(shifted, std::abs(even[i] - later[i + 2]));
    }
    // 2000 and 2001 map to DAC frames 1000 and 1001 (ceil), 2002 to 1001.
    CHECK(shifted == 0.0);
    CHECK(moved > 0.0);
}

TEST_CASE("16-bit source samples play")
{
    SynthProcessor p;
    test::set(p, id::sampleFormat, 1.0f);
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    CHECK(test::rms(play(p, 9600, {512}), 2000, 8000) > 0.01);
}

TEST_CASE("Float and double processing agree")
{
    auto f = make(48000.0);
    auto d = make(48000.0);
    auto midi = notes();
    const auto a = test::run<float>(*f, std::vector<double>(12000, 0.0), {512}, false, &midi);
    const auto b = test::run<double>(*d, std::vector<double>(12000, 0.0), {512}, false, &midi);
    double m = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        m = std::max(m, std::abs(a[i] - b[i]));
    }
    CHECK(m < 1e-6);
}

TEST_CASE("Rendering does not allocate")
{
    for (const int clock : {0, 3}) {
        auto p = make(96000.0, clock);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        midi.ensureSize(1024);
        test::countAllocations(true);
        for (int block = 0; block < 100; ++block) {
            midi.clear();
            if (block % 10 == 0) {
                midi.addEvent(juce::MidiMessage::noteOn(1, 48 + block / 10, 1.0f), 17);
            }
            if (block == 50) {
                test::set(*p, id::softMute, 1.0f);
            }
            p->processBlock(buffer, midi);
        }
        test::countAllocations(false);
        CHECK(test::allocationCount() == 0);
        CHECK(p->droppedMidiEvents() == 0);
    }
}

TEST_CASE("Synth state round-trips and effect state is rejected")
{
    SynthProcessor a;
    test::set(a, id::clock, 3.0f);
    test::set(a, id::waveform, 2.0f);
    juce::MemoryBlock saved;
    a.getStateInformation(saved);
    SynthProcessor b;
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(test::same(test::get(b, id::clock), 3.0f));
    CHECK(test::same(test::get(b, id::waveform), 2.0f));

    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    xml->setAttribute("plugin", "effect");
    juce::MemoryBlock wrong;
    juce::AudioProcessor::copyXmlToBinary(*xml, wrong);
    SynthProcessor c;
    c.setStateInformation(wrong.getData(), static_cast<int>(wrong.getSize()));
    CHECK(test::same(test::get(c, id::clock), 2.0f));
    CHECK(c.statusText().contains("Saved state ignored"));
}

TEST_CASE("Every parameter is grouped and described as in docs/parameters.md")
{
    auto p = make(48000.0);
    test::checkDescriptions(*p);
}

TEST_CASE("The editor opens")
{
    auto p = make(48000.0);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    REQUIRE(editor != nullptr);
    CHECK(editor->getWidth() > 0);
    CHECK(editor->getHeight() > 0);
}

TEST_CASE("The version line names the plugin, library, and commit versions")
{
    const auto text = ak4522demo::ChipPlugin::versionText();
    const auto library = ak4522::versionString();
    CHECK(text.startsWith(juce::String(JucePlugin_Name) + " " + JucePlugin_VersionString));
    CHECK(text.contains("ak4522 " + juce::String(library.data(), library.size())));
    CHECK(text.contains("commit "));
    CHECK(!text.endsWith("commit "));
}

TEST_CASE("The analog options are greyed out under the Specification profile")
{
    auto p = make(48000.0);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    const std::array<const char*, 6> analog{"analog", "noise", "distortion", "mismatch", "crosstalk", "dcOffset"};
    test::set(*p, "profile", 0.0f); // Specification
    test::poll(*editor);
    for (const auto* componentId : analog) {
        INFO(componentId);
        auto* c = test::findComponent(*editor, componentId);
        REQUIRE(c != nullptr);
        CHECK(!c->isEnabled());
    }
    CHECK(test::findComponent(*editor, "outputCircuit")->isEnabled());
    test::set(*p, "profile", 1.0f); // NominalAnalog
    test::poll(*editor);
    for (const auto* componentId : analog) {
        INFO(componentId);
        CHECK(test::findComponent(*editor, componentId)->isEnabled());
    }
}
