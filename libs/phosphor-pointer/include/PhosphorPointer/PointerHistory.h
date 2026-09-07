// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorPointer/PointerShaderContract.h>
#include <PhosphorPointer/PointerUniformExtension.h>
#include <PhosphorPointer/phosphorpointer_export.h>

#include <QPointF>
#include <QRectF>
#include <Qt>

#include <array>

namespace PhosphorPointerShaders {

/// The ring-buffer pointer sampler both hosts feed: the compositor from
/// `slotMouseChanged`, the preview from its figure-eight driver. It keeps the
/// last `kMaxTrailPoints` motion samples plus the last press and release, and
/// answers the per-frame questions the contract needs (`frameState`), whether
/// a chain still needs frames (`isLive`) and where it may paint
/// (`damageRect`).
///
/// Positions are in device px of the canvas the caller chose; timestamps are
/// caller-supplied milliseconds on one monotonic clock.
///
/// Sampling rule: `notePointer` appends a sample when its distance from the
/// newest is at least 1 device px OR the time gap is at least
/// `kMinSampleGapMs`; anything closer and sooner is dropped. Ages are
/// computed at `frameState` time. Velocity is the finite difference over the
/// newest two samples with a floor of `kVelocityMinDtSeconds` on dt, and
/// decays to zero once the newest sample is older than `kVelocityHoldMs`.
class PHOSPHORPOINTER_EXPORT PointerHistory
{
public:
    static constexpr int kCapacity = PointerShaderContract::kMaxTrailPoints;
    static constexpr double kMinSampleDistancePx = 1.0;
    static constexpr qint64 kMinSampleGapMs = 8;
    static constexpr double kVelocityMinDtSeconds = 1.0 / 30.0;
    static constexpr qint64 kVelocityHoldMs = 100;

    /// The "none this session" sentinel for press / release / idle ages.
    static constexpr double kNeverSeconds = 1.0e6;

    PointerHistory();

    /// Record a pointer position at @p nowMs (see the sampling rule above).
    void notePointer(const QPointF& devicePx, qint64 nowMs);

    /// Record a button transition from @p before to @p now at @p devicePx.
    /// A newly pressed button becomes the last press (left, then right, then
    /// middle when several land at once) and a newly released one the last
    /// release. The pressed-button mask always tracks @p now.
    void noteButtons(Qt::MouseButtons now, Qt::MouseButtons before, const QPointF& devicePx, qint64 nowMs);

    /// The contract tail for a frame at @p nowMs with canvas scale @p scale.
    /// `cursorRect` and `hasSprite` are left at their defaults for the host
    /// to fill.
    PointerFrameState frameState(qint64 nowMs, double scale) const;

    /// True while a motion or button event is younger than @p trailSeconds.
    bool isLive(qint64 nowMs, double trailSeconds) const;

    /// Bounding box of the trail samples, press and release points younger
    /// than @p trailSeconds, inflated by @p reachDevicePx on every side.
    /// Null when not live.
    QRectF damageRect(double reachDevicePx, qint64 nowMs, double trailSeconds) const;

    /// Newest sample position, or a null point when empty.
    QPointF newestPosition() const;

    int sampleCount() const
    {
        return m_count;
    }

    void reset();

private:
    struct Sample
    {
        QPointF pos;
        qint64 timeMs = 0;
        double speed = 0.0;
    };

    const Sample& sampleAt(int newestFirstIndex) const;
    static double secondsBetween(qint64 laterMs, qint64 earlierMs);

    std::array<Sample, kCapacity> m_ring{};
    int m_head = 0; ///< index of the newest sample (valid when m_count > 0)
    int m_count = 0;

    qint64 m_lastMotionMs = 0;
    bool m_hasMotion = false;

    QPointF m_pressPos;
    qint64 m_pressMs = 0;
    int m_pressButton = 0;
    bool m_hasPress = false;

    QPointF m_releasePos;
    qint64 m_releaseMs = 0;
    int m_releaseButton = 0;
    bool m_hasRelease = false;

    int m_buttonsMask = 0;
};

} // namespace PhosphorPointerShaders
