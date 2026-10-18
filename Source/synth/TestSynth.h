// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// A small polyphonic test synth that runs at the DAC rate, as an emulated
// sound chip would: eight voices of sine, band-limited saw, or band-limited
// square, with a linear attack and release. MIDI arrives already placed on
// the DAC frame timeline. Real-time safe: fixed capacities, no allocation
// after prepare().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ak4522demo {

enum class Waveform : int { sine, saw, square };

class TestSynth {
public:
    struct Event {
        std::uint64_t frame = 0; // DAC frame at which it acts
        std::uint8_t status = 0;
        std::uint8_t data1 = 0;
        std::uint8_t data2 = 0;
    };
    static constexpr std::size_t voices = 8;
    static constexpr std::size_t eventCapacity = 1024;

    void prepare(double dacRate) noexcept
    {
        rate_ = dacRate;
        reset();
    }

    void reset() noexcept
    {
        for (auto& v : voice_) {
            v = {};
        }
        position_ = 0;
        head_ = tail_ = count_ = 0;
    }

    // Queues an event; returns false (and drops it) when the queue is full.
    // Frames must not decrease.
    bool queue(const Event& e) noexcept
    {
        if (count_ == eventCapacity) {
            return false;
        }
        events_[tail_] = e;
        tail_ = (tail_ + 1) % eventCapacity;
        ++count_;
        return true;
    }

    std::uint64_t position() const noexcept { return position_; }

    void setSound(Waveform w, double attackSeconds, double releaseSeconds) noexcept
    {
        waveform_ = w;
        attackStep_ = 1.0 / std::max(1.0, attackSeconds * rate_);
        releaseStep_ = 1.0 / std::max(1.0, releaseSeconds * rate_);
    }

    // Renders `frames` mono samples, normalized, into `out`.
    void render(double* out, std::size_t frames) noexcept
    {
        for (std::size_t i = 0; i < frames; ++i) {
            while (count_ > 0 && events_[head_].frame <= position_) {
                apply(events_[head_]);
                head_ = (head_ + 1) % eventCapacity;
                --count_;
            }
            double sum = 0.0;
            for (auto& v : voice_) {
                if (v.envelope > 0.0 || v.held) {
                    sum += v.velocity * v.envelope * oscillator(v);
                    step(v);
                }
            }
            out[i] = 0.25 * sum;
            ++position_;
        }
    }

private:
    struct Voice {
        int note = -1;
        bool held = false;
        double phase = 0.0;     // cycles, [0, 1)
        double increment = 0.0; // cycles per DAC frame
        double velocity = 0.0;
        double envelope = 0.0;
        std::uint64_t started = 0;
    };

    void apply(const Event& e) noexcept
    {
        const int type = e.status & 0xF0;
        if (type == 0x90 && e.data2 > 0) {
            noteOn(e.data1, e.data2);
        } else if (type == 0x80 || type == 0x90) {
            noteOff(e.data1);
        } else if (type == 0xB0 && (e.data1 == 120 || e.data1 == 123)) {
            for (auto& v : voice_) { // all sound off, all notes off
                v.held = false;
                if (e.data1 == 120) {
                    v.envelope = 0.0;
                }
            }
        }
    }

    void noteOn(int note, int velocity) noexcept
    {
        // A free voice, or else the oldest one.
        Voice* target = &voice_[0];
        for (auto& v : voice_) {
            if (!v.held && v.envelope == 0.0) {
                target = &v;
                break;
            }
            if (v.started < target->started) {
                target = &v;
            }
        }
        target->note = note;
        target->held = true;
        target->phase = 0.0;
        target->increment = 440.0 * std::pow(2.0, (note - 69) / 12.0) / rate_;
        target->velocity = velocity / 127.0;
        target->started = position_;
    }

    void noteOff(int note) noexcept
    {
        for (auto& v : voice_) {
            if (v.held && v.note == note) {
                v.held = false;
            }
        }
    }

    // PolyBLEP correction for a discontinuity at phase 0 (Valimaki and Huovilainen).
    static double blep(double t, double dt) noexcept
    {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }

    double oscillator(const Voice& v) const noexcept
    {
        constexpr double twoPi = 6.283185307179586;
        const double t = v.phase;
        const double dt = v.increment;
        switch (waveform_) {
        case Waveform::sine: return std::sin(twoPi * t);
        case Waveform::saw: return 2.0 * t - 1.0 - blep(t, dt);
        case Waveform::square: {
            const double half = t + 0.5 >= 1.0 ? t - 0.5 : t + 0.5;
            return (t < 0.5 ? 1.0 : -1.0) + blep(t, dt) - blep(half, dt);
        }
        }
        return 0.0;
    }

    void step(Voice& v) noexcept
    {
        v.phase += v.increment;
        if (v.phase >= 1.0) {
            v.phase -= 1.0;
        }
        v.envelope = v.held ? std::min(1.0, v.envelope + attackStep_) : std::max(0.0, v.envelope - releaseStep_);
    }

    double rate_ = 48000.0;
    Waveform waveform_ = Waveform::saw;
    double attackStep_ = 1.0;
    double releaseStep_ = 1.0;
    std::array<Voice, voices> voice_{};
    std::array<Event, eventCapacity> events_{};
    std::size_t head_ = 0, tail_ = 0, count_ = 0;
    std::uint64_t position_ = 0;
};

} // namespace ak4522demo
