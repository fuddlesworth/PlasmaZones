// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVector>

#include <memory>

namespace PlasmaZones::ShaderRender {

/**
 * @brief Per-frame audio spectrum source for audio-reactive shaders.
 *
 * The runtime pulls a 1D spectrum from CAVA at frame time.  For
 * deterministic previews, we generate a synthetic spectrum here.
 * Modes:
 *
 *   silent — all zeros.  Audio shaders render in their idle/dormant
 *            state.  Fine for shaders that just want some non-zero
 *            spectrum to weight a hue but bad for ones that gate
 *            entire visual elements on bass/treble.
 *   sine   — every bar follows sin(2*pi*phase + offset_per_bar).
 *            Looks like steady mid-tempo music.  Default.
 *   noise  — bars filled with deterministic per-frame noise.  Looks
 *            like static / flat-spectrum noise.
 *   sweep  — a peak that walks from bass→treble across the duration.
 *            Useful for verifying that bass / mid / treble buckets
 *            light up as expected.
 *
 * Bar count matches the runtime's CAVA default (256).
 */
class AudioMock
{
public:
    static constexpr int kBarCount = 256;

    virtual ~AudioMock() = default;

    /**
     * @brief Fill outBars (sized kBarCount) with the spectrum for the
     * given frame.  Each bar is in [0, 1].
     */
    virtual void fillFrame(int frameIndex, int frameRate, QVector<float>& outBars) const = 0;
};

std::unique_ptr<AudioMock> makeAudioMock(const QString& mode);

} // namespace PlasmaZones::ShaderRender
