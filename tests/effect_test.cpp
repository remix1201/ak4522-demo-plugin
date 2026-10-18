// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// The effect plugin against the adapter rules of the library's AGENTS.md
// (REQ-VST-01, REQ-VST-02, REQ-LAT-03): layouts, reported latency against
// the emitted audio, compensated bypass and mix, state, float and double,
// block sizes, real-time safety, and the tail policy.

#include "effect/EffectProcessor.h"
#include "support.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <random>

using ak4522demo::EffectProcessor;
namespace id = ak4522demo::id;

namespace {

constexpr double rate = 48000.0;

std::unique_ptr<EffectProcessor> make(double hostRate = rate, int block = 512)
{
    auto p = std::make_unique<EffectProcessor>();
    p->setRateAndBufferSizeDetails(hostRate, block);
    p->prepareToPlay(hostRate, block);
    return p;
}

std::vector<double> delayed(const std::vector<double>& x, int by)
{
    std::vector<double> y(x.size(), 0.0);
    for (std::size_t i = static_cast<std::size_t>(by); i < x.size(); ++i) {
        y[i] = x[i - static_cast<std::size_t>(by)];
    }
    return y;
}

double maxDifference(const std::vector<double>& a, const std::vector<double>& b)
{
    double m = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        m = std::max(m, std::abs(a[i] - b[i]));
    }
    return m;
}

} // namespace

TEST_CASE("Only stereo in and out is supported")
{
    EffectProcessor p;
    juce::AudioProcessor::BusesLayout stereo, mono, mixed;
    stereo.inputBuses.add(juce::AudioChannelSet::stereo());
    stereo.outputBuses.add(juce::AudioChannelSet::stereo());
    mono.inputBuses.add(juce::AudioChannelSet::mono());
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    mixed.inputBuses.add(juce::AudioChannelSet::mono());
    mixed.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(p.checkBusesLayoutSupported(stereo));
    CHECK_FALSE(p.checkBusesLayoutSupported(mono));
    CHECK_FALSE(p.checkBusesLayoutSupported(mixed));
}

TEST_CASE("Reported latency matches the emitted audio on every path")
{
    for (const int path : {0, 1, 2}) {
        for (const double hostRate : {44100.0, 48000.0, 96000.0}) {
            CAPTURE(path, hostRate);
            EffectProcessor p;
            test::set(p, id::path, static_cast<float>(path));
            p.setRateAndBufferSizeDetails(hostRate, 512);
            p.prepareToPlay(hostRate, 512);
            const int latency = p.getLatencySamples();
            REQUIRE(latency > 0);
            // A 1 kHz tone: its phase gives the delay to a fraction of a sample.
            const auto n = static_cast<std::size_t>(hostRate);
            const auto in = test::sine(n, 1000.0, hostRate);
            const auto out = test::run<double>(p, in, {512});
            const std::size_t from = n / 2, to = n; // whole cycles, after settling
            const double phase = std::arg(test::bin(out, 1000.0, hostRate, from, to)
                                          / test::bin(in, 1000.0, hostRate, from, to));
            double delay = -phase / (2.0 * test::pi * 1000.0) * hostRate;
            const double period = hostRate / 1000.0;
            delay += std::round((latency - delay) / period) * period;
            CHECK(std::abs(delay - latency) < 0.05);
        }
    }
}

TEST_CASE("Bypass is the input delayed by the reported latency")
{
    const auto in = test::sine(24000, 997.0, rate);
    SECTION("bypass parameter")
    {
        EffectProcessor p;
        test::set(p, id::bypass, 1.0f);
        p.setRateAndBufferSizeDetails(rate, 512);
        p.prepareToPlay(rate, 512);
        const auto out = test::run<double>(p, in, {512});
        CHECK(maxDifference(out, delayed(in, p.getLatencySamples())) == 0.0);
    }
    SECTION("host bypass")
    {
        auto p = make();
        test::run<double>(p.operator*(), std::vector<double>(4800, 0.0), {512}, true); // let the fade finish
        p->reset();
        const auto out = test::run<double>(*p, in, {512}, true);
        // The dry line kept running through the fade; after reset it restarts.
        const auto expected = delayed(in, p->getLatencySamples());
        CHECK(maxDifference(out, expected) < 1e-15);
    }
}

TEST_CASE("Mix at 0 percent is the aligned dry signal; delta is small for a passband tone")
{
    const auto in = test::sine(24000, 997.0, rate);
    {
        EffectProcessor p;
        test::set(p, id::mix, 0.0f);
        p.setRateAndBufferSizeDetails(rate, 512);
        p.prepareToPlay(rate, 512);
        CHECK(maxDifference(test::run<double>(p, in, {512}), delayed(in, p.getLatencySamples())) == 0.0);
    }
    {
        EffectProcessor p;
        test::set(p, id::delta, 1.0f);
        p.setRateAndBufferSizeDetails(rate, 512);
        p.prepareToPlay(rate, 512);
        const auto out = test::run<double>(p, in, {512});
        // Processed minus aligned dry: what the chip changes, here only its
        // small passband deviation, at least 30 dB below the tone.
        CHECK(20.0 * std::log10(test::rms(out, 12000, 24000) / test::rms(in, 12000, 24000)) < -30.0);
    }
}

TEST_CASE("State round-trips, including the noise seed")
{
    EffectProcessor a;
    test::set(a, id::dacRate, 3.0f);
    test::set(a, id::deemphasis, 2.0f);
    test::set(a, id::profile, 1.0f);
    test::set(a, id::outputGain, -6.0f);
    test::set(a, id::path, 1.0f);
    juce::MemoryBlock saved;
    a.getStateInformation(saved);

    EffectProcessor b;
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    for (const char* parameterId : {id::dacRate, id::deemphasis, id::profile, id::outputGain, id::path}) {
        CAPTURE(parameterId);
        CHECK(test::same(test::get(b, parameterId), test::get(a, parameterId)));
    }
    CHECK(b.noiseSeed() == a.noiseSeed());
}

TEST_CASE("Incompatible state is rejected and the settings are kept")
{
    EffectProcessor a;
    test::set(a, id::outputGain, -6.0f);
    juce::MemoryBlock saved;
    a.getStateInformation(saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(xml != nullptr);

    const auto reject = [](juce::XmlElement modified, const char* expected) {
        EffectProcessor b;
        test::set(b, id::outputGain, 3.0f);
        juce::MemoryBlock data;
        juce::AudioProcessor::copyXmlToBinary(modified, data);
        b.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
        CHECK(test::same(test::get(b, id::outputGain), 3.0f));
        CHECK(b.statusText().contains("Saved state ignored"));
        CHECK(b.statusText().contains(expected));
    };
    auto newer = *xml;
    newer.setAttribute("schema", 2);
    reject(newer, "schema 2");
    auto other = *xml;
    other.setAttribute("plugin", "synth");
    reject(other, "other demo plugin");
    reject(juce::XmlElement("SomethingElse"), "not an AK4522 demo state");
}

TEST_CASE("Float and double processing agree")
{
    // Float-representable input, so both paths quantize to the same 20-bit
    // codes; the remaining difference is float output rounding.
    auto in = test::sine(9600, 1234.0, rate);
    for (auto& v : in) {
        v = static_cast<double>(static_cast<float>(v));
    }
    auto f = make();
    auto d = make();
    CHECK(maxDifference(test::run<float>(*f, in, {512}), test::run<double>(*d, in, {512})) < 1e-6);
}

TEST_CASE("Output does not depend on host block sizes")
{
    for (const int path : {0, 2}) {
        CAPTURE(path);
        std::mt19937 rng(7);
        std::uniform_int_distribution<int> size(1, 512);
        std::vector<int> sizes(200);
        for (auto& s : sizes) {
            s = size(rng);
        }
        const auto in = test::sine(30000, 3000.0, rate);
        EffectProcessor a, b;
        for (auto* p : {&a, &b}) {
            test::set(*p, id::path, static_cast<float>(path));
            p->setRateAndBufferSizeDetails(rate, 512);
            p->prepareToPlay(rate, 512);
        }
        CHECK(maxDifference(test::run<double>(a, in, {512}), test::run<double>(b, in, sizes)) < 1e-12);
    }
}

TEST_CASE("Two instances run independently")
{
    const auto x = test::sine(9600, 500.0, rate); // 40 blocks of 240
    const auto y = test::sine(9600, 5000.0, rate);
    auto solo = make();
    const auto expected = test::run<double>(*solo, x, {240});
    auto a = make();
    auto b = make();
    std::vector<double> outA, outB;
    for (std::size_t at = 0; at < x.size(); at += 240) {
        const std::vector<double> xa(x.begin() + static_cast<long>(at), x.begin() + static_cast<long>(at + 240));
        const std::vector<double> yb(y.begin() + static_cast<long>(at), y.begin() + static_cast<long>(at + 240));
        const auto oa = test::run<double>(*a, xa, {240});
        test::run<double>(*b, yb, {240});
        outA.insert(outA.end(), oa.begin(), oa.end());
    }
    CHECK(maxDifference(outA, expected) == 0.0);
}

TEST_CASE("Processing does not allocate")
{
    for (const int path : {0, 1, 2}) {
        CAPTURE(path);
        EffectProcessor p;
        test::set(p, id::path, static_cast<float>(path));
        test::set(p, id::profile, 1.0f);
        p.setRateAndBufferSizeDetails(rate, 512);
        p.prepareToPlay(rate, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        test::countAllocations(true);
        for (int block = 0; block < 50; ++block) {
            for (int i = 0; i < 512; ++i) {
                buffer.setSample(0, i, 0.1f * std::sin(0.05f * static_cast<float>(i + 512 * block)));
                buffer.setSample(1, i, buffer.getSample(0, i));
            }
            if (block == 20) {
                test::set(p, id::deemphasis, 2.0f); // a pin change mid-stream
                test::set(p, id::softMute, 1.0f);
            }
            p.processBlock(buffer, midi);
        }
        test::countAllocations(false);
        CHECK(test::allocationCount() == 0);
    }
}

TEST_CASE("An unsupported host rate is reported and the output is silent")
{
    auto p = make(22050.0);
    CHECK(p->statusText().startsWith("Silent"));
    const auto out = test::run<double>(*p, test::sine(2048, 1000.0, 22050.0), {512});
    CHECK(test::rms(out, 0, out.size()) == 0.0);
}

TEST_CASE("A configuration change re-prepares off the audio thread")
{
    auto p = make();
    const int before = p->getLatencySamples();
    test::set(*p, id::path, 1.0f); // DAC into ADC: more chip delay
    p->applyPendingChanges(); // what the plugin's timer does
    CHECK(p->getLatencySamples() > before);
    CHECK(p->statusText().startsWith("DAC into ADC"));
}

TEST_CASE("Tail: finite settling without noise, infinite with it")
{
    auto clean = make();
    CHECK(std::isfinite(clean->getTailLengthSeconds()));
    CHECK(clean->getTailLengthSeconds() > 0.0);

    // The declared tail covers the impulse response: after it, the output
    // of an impulse is below -120 dBFS.
    std::vector<double> impulse(static_cast<std::size_t>(rate * (clean->getTailLengthSeconds() + 0.5)), 0.0);
    impulse[0] = 1.0;
    const auto out = test::run<double>(*clean, impulse, {512});
    const auto tailEnd = static_cast<std::size_t>(rate * clean->getTailLengthSeconds());
    double after = 0.0;
    for (std::size_t i = tailEnd; i < out.size(); ++i) {
        after = std::max(after, std::abs(out[i]));
    }
    CHECK(after < 1e-6);

    EffectProcessor noisy;
    test::set(noisy, id::profile, 1.0f); // NominalAnalog: noise on by default
    noisy.setRateAndBufferSizeDetails(rate, 512);
    noisy.prepareToPlay(rate, 512);
    CHECK(std::isinf(noisy.getTailLengthSeconds()));
}

TEST_CASE("Every parameter is grouped and described as in docs/parameters.md")
{
    auto p = make();
    test::checkDescriptions(*p);
}

TEST_CASE("The editor opens")
{
    auto p = make();
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
