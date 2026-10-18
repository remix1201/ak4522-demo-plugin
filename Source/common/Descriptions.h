// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// One description per parameter, shown as the editor's tooltips. The section
// "What each control does" in docs/parameters.md uses the same wording, and
// the tests check that it does. Chip descriptions stay to one line; the
// library's documentation, linked from docs/parameters.md, is the full
// account (AGENTS.md).

#include <juce_core/juce_core.h>

#include <array>

namespace ak4522demo {

// Where a parameter's behavior comes from. Every description says so.
enum class Origin {
    chip,    // AK4522 behavior, modeled by the library
    library, // a library feature that is not a chip control
    plugin,  // host behavior added by these plugins
};

struct Description {
    const char* parameterId;
    Origin origin;
    const char* text;
};

inline constexpr std::array<Description, 22> descriptions{{
    // Shared by both plugins.
    {"deemphasis", Origin::chip,
     "The chip's 50/15 us de-emphasis filter for 32, 44.1, or 48 kHz material, selected as the DEM pins would."},
    {"softMute", Origin::chip,
     "Ramps the output to silence over 1024 DAC frames, and back over the same time when released, as the SMUTE "
     "pin does."},
    {"profile", Origin::library,
     "Specification is the clean, deterministic model of the datasheet response. NominalAnalog adds the synthetic "
     "imperfections switched below."},
    {"noise", Origin::library,
     "Synthetic noise at the datasheet's typical level. NominalAnalog only. While it plays, the plugin's tail is "
     "infinite."},
    {"distortion", Origin::library,
     "Synthetic 2nd and 3rd harmonic distortion at the datasheet's typical level. NominalAnalog only."},
    {"mismatch", Origin::library, "A fixed 0.2 dB gain difference between the channels. NominalAnalog only."},
    {"crosstalk", Origin::library,
     "Leakage between the channels that rises with frequency, like capacitive coupling. NominalAnalog only."},
    {"dcOffset", Origin::library,
     "A small DC offset on the DAC output. NominalAnalog only; the ADC and the Fig.11 circuit remove it."},
    {"outputCircuit", Origin::library,
     "The datasheet's Fig.11 board circuit after the chip: two 0.589 Hz high-pass sections around a gain of -1.8 "
     "(+5.1 dB). Not part of the chip."},
    {"outputGain", Origin::plugin, "Level after the chip and the Fig.11 circuit, smoothed over 20 ms."},

    // AK4522 DAC (effect).
    {"dacRate", Origin::chip,
     "The chip's sample rate. Host audio is converted to it, passes through the chip, and is converted back to "
     "the host rate."},
    {"inputResolution", Origin::library,
     "The word length host audio is quantized to before the chip: 20-bit, the chip's full input, or 16-bit."},
    {"inputDither", Origin::library,
     "Adds TPDF dither before host audio is quantized to the chip's input."},
    {"path", Origin::library,
     "What host audio passes through: the DAC, the DAC's analog output into the ADC, or the ADC alone."},
    {"mix", Origin::plugin,
     "Balance of processed and dry signal. The dry signal is delayed by the reported latency so the two line up."},
    {"delta", Origin::plugin,
     "Plays the processed signal minus the aligned dry signal: only what the chip changes."},
    {"bypass", Origin::plugin,
     "Passes the input through, delayed by the reported latency, so switching does not shift the timing."},

    // AK4522 Synth (instrument).
    {"clock", Origin::chip,
     "The rate at which the test synth's samples reach the DAC. 112 MHz / 2304 is above the datasheet's 48 kHz "
     "limit and uses the library's extended range."},
    {"sampleFormat", Origin::library,
     "The word length of the test synth's samples: 20-bit, the chip's full input, or 16-bit."},
    {"waveform", Origin::plugin, "The test synth's oscillator shape."},
    {"attack", Origin::plugin, "The test synth's fade-in time at the start of each note."},
    {"release", Origin::plugin, "The test synth's fade-out time after each note ends."},
}};

// The line that labels a description's origin, in tooltips and the docs.
inline const char* originText(Origin origin) noexcept
{
    switch (origin) {
    case Origin::chip: return "AK4522 behavior, from the ak4522 library.";
    case Origin::library: return "From the ak4522 library; not a chip control.";
    case Origin::plugin: return "Plugin behavior; not part of the AK4522.";
    }
    return "";
}

inline const Description* findDescription(const juce::String& parameterId) noexcept
{
    for (const auto& d : descriptions) {
        if (parameterId == d.parameterId) {
            return &d;
        }
    }
    return nullptr;
}

// The tooltip: the description, then its origin.
inline juce::String tooltipFor(const juce::String& parameterId)
{
    const auto* d = findDescription(parameterId);
    return d == nullptr ? juce::String() : juce::String(d->text) + "\n" + originText(d->origin);
}

} // namespace ak4522demo
