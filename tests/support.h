// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

#include "common/ChipPlugin.h"
#include "common/Descriptions.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace test {

void countAllocations(bool on) noexcept;
std::size_t allocationCount() noexcept;

inline constexpr double pi = 3.14159265358979323846;

// Sets a parameter by its real value, as a host would.
inline void set(juce::AudioProcessor& p, const char* parameterId, float value)
{
    for (auto* parameter : p.getParameters()) {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            ranged != nullptr && ranged->getParameterID() == parameterId) {
            ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
            return;
        }
    }
    FAIL("no parameter " << parameterId);
}

inline float get(juce::AudioProcessor& p, const char* parameterId)
{
    for (auto* parameter : p.getParameters()) {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            ranged != nullptr && ranged->getParameterID() == parameterId) {
            return ranged->convertFrom0to1(ranged->getValue());
        }
    }
    FAIL("no parameter " << parameterId);
    return 0.0f;
}

// Every parameter sits in a group and has a description, and
// docs/parameters.md carries that description and its origin word for word,
// in the form "(`id`): text _origin_".
inline void checkDescriptions(juce::AudioProcessor& p)
{
    const auto docs = juce::File(AK4522_DEMO_PARAMETERS_DOC).loadFileAsString();
    REQUIRE(docs.isNotEmpty());
    for (const auto* node : p.getParameterTree()) {
        CHECK(node->getGroup() != nullptr);
    }
    for (auto* parameter : p.getParameters()) {
        const auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        REQUIRE(ranged != nullptr);
        const auto id = ranged->getParameterID();
        const auto* d = ak4522demo::findDescription(id);
        INFO("parameter " << id);
        REQUIRE(d != nullptr);
        const auto line = "(`" + id + "`): " + d->text + " _" + ak4522demo::originText(d->origin) + "_";
        INFO("expected in docs/parameters.md: " << line);
        CHECK(docs.contains(line));
    }
}

// The component with this ID anywhere inside `parent`, or nullptr.
inline juce::Component* findComponent(juce::Component& parent, const juce::String& componentId)
{
    for (auto* child : parent.getChildren()) {
        if (child->getComponentID() == componentId) {
            return child;
        }
        if (auto* found = findComponent(*child, componentId)) {
            return found;
        }
    }
    return nullptr;
}

// Updates an open editor from the parameters now, as its timer would.
inline void poll(juce::AudioProcessorEditor& editor)
{
    auto* polling = dynamic_cast<ak4522demo::ParameterPolling*>(&editor);
    REQUIRE(polling != nullptr);
    polling->pollParameters();
}

// The control of type T in the editor row for a parameter, or nullptr.
template <typename T>
T* control(juce::Component& editor, const juce::String& parameterId)
{
    if (auto* row = findComponent(editor, parameterId)) {
        for (auto* child : row->getChildren()) {
            if (auto* c = dynamic_cast<T*>(child)) {
                return c;
            }
        }
    }
    return nullptr;
}

inline bool same(float a, float b)
{
    return std::abs(a - b) < 1e-6f;
}

// Runs `input` through `p` in blocks of the given sizes (cycled), stereo
// with the same signal in both channels; returns the left output.
template <typename T>
std::vector<double> run(juce::AudioProcessor& p, const std::vector<double>& input, std::vector<int> sizes,
                        bool bypassed = false, juce::MidiBuffer* midiAt = nullptr)
{
    std::vector<double> out;
    out.reserve(input.size());
    juce::AudioBuffer<T> buffer(2, *std::max_element(sizes.begin(), sizes.end()));
    juce::MidiBuffer midi;
    std::size_t at = 0;
    for (std::size_t b = 0; at < input.size(); ++b) {
        const int n = std::min<int>(sizes[b % sizes.size()], static_cast<int>(input.size() - at));
        buffer.setSize(2, n, false, false, true);
        for (int i = 0; i < n; ++i) {
            buffer.setSample(0, i, static_cast<T>(input[at + static_cast<std::size_t>(i)]));
            buffer.setSample(1, i, static_cast<T>(input[at + static_cast<std::size_t>(i)]));
        }
        midi.clear();
        if (midiAt != nullptr) {
            for (const auto meta : *midiAt) {
                const auto t = static_cast<std::size_t>(meta.samplePosition);
                if (t >= at && t < at + static_cast<std::size_t>(n)) {
                    midi.addEvent(meta.getMessage(), static_cast<int>(t - at));
                }
            }
        }
        if (bypassed) {
            p.processBlockBypassed(buffer, midi);
        } else {
            p.processBlock(buffer, midi);
        }
        for (int i = 0; i < n; ++i) {
            out.push_back(static_cast<double>(buffer.getSample(0, i)));
        }
        at += static_cast<std::size_t>(n);
    }
    return out;
}

inline std::vector<double> sine(std::size_t n, double freq, double rate, double amplitude = 0.5)
{
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = amplitude * std::sin(2.0 * pi * freq * static_cast<double>(i) / rate);
    }
    return x;
}

// Phase of `x` at `freq` over [from, to), by a single DFT bin.
inline std::complex<double> bin(const std::vector<double>& x, double freq, double rate, std::size_t from,
                                std::size_t to)
{
    std::complex<double> sum{};
    for (std::size_t i = from; i < to; ++i) {
        sum += x[i] * std::polar(1.0, -2.0 * pi * freq * static_cast<double>(i) / rate);
    }
    return sum;
}

inline double rms(const std::vector<double>& x, std::size_t from, std::size_t to)
{
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        s += x[i] * x[i];
    }
    return std::sqrt(s / static_cast<double>(to - from));
}

} // namespace test
