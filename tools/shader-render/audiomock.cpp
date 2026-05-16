// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audiomock.h"

#include <cmath>

namespace PlasmaZones::ShaderRender {
namespace {

class SilentMock : public AudioMock
{
public:
    void fillFrame(int, int, QVector<float>& outBars) const override
    {
        outBars.fill(0.0f, kBarCount);
    }
};

class SineMock : public AudioMock
{
public:
    void fillFrame(int frameIndex, int frameRate, QVector<float>& outBars) const override
    {
        outBars.resize(kBarCount);
        // 0.5 Hz fundamental * a per-bar offset so the bars don't all
        // pulse in unison — gives the spectrum visible motion.
        const double t = static_cast<double>(frameIndex) / std::max(1, frameRate);
        constexpr double kFundamental = 0.5;
        for (int i = 0; i < kBarCount; ++i) {
            const double phase = 2.0 * M_PI * (kFundamental * t + i * 0.013);
            // Bias positive so 0 ≤ bar ≤ 1, with a frequency-rolloff
            // weighting (low freqs louder than high) that mirrors
            // typical music spectra better than a flat sine.
            const double rolloff = 1.0 - std::pow(static_cast<double>(i) / kBarCount, 0.6);
            outBars[i] = static_cast<float>(0.5 * (1.0 + std::sin(phase)) * rolloff);
        }
    }
};

class NoiseMock : public AudioMock
{
public:
    void fillFrame(int frameIndex, int, QVector<float>& outBars) const override
    {
        outBars.resize(kBarCount);
        // xorshift32 seeded by frame so noise is deterministic but
        // changes per frame.
        uint32_t state = static_cast<uint32_t>(frameIndex + 1) * 0x9E3779B9u;
        for (int i = 0; i < kBarCount; ++i) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            outBars[i] = (state >> 8) / static_cast<float>(0x00FFFFFF);
        }
    }
};

class SweepMock : public AudioMock
{
public:
    void fillFrame(int frameIndex, int frameRate, QVector<float>& outBars) const override
    {
        outBars.fill(0.0f, kBarCount);
        // 4-second loop: peak walks bass → treble → bass.
        const double cyclePos = std::fmod(static_cast<double>(frameIndex) / frameRate, 4.0) / 4.0;
        const double peakPos = (cyclePos < 0.5 ? cyclePos : 1.0 - cyclePos) * 2.0;
        const int peak = static_cast<int>(peakPos * (kBarCount - 1));
        constexpr int kHalfWidth = 32;
        for (int i = 0; i < kBarCount; ++i) {
            const int d = std::abs(i - peak);
            if (d <= kHalfWidth) {
                outBars[i] = 1.0f - static_cast<float>(d) / kHalfWidth;
            }
        }
    }
};

} // namespace

std::unique_ptr<AudioMock> makeAudioMock(const QString& mode)
{
    if (mode == QLatin1String("silent"))
        return std::make_unique<SilentMock>();
    if (mode == QLatin1String("sine"))
        return std::make_unique<SineMock>();
    if (mode == QLatin1String("noise"))
        return std::make_unique<NoiseMock>();
    if (mode == QLatin1String("sweep"))
        return std::make_unique<SweepMock>();
    return nullptr;
}

} // namespace PlasmaZones::ShaderRender
