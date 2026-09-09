// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorPointer/PointerFrameState.h>
#include <PhosphorPointer/PointerShaderContract.h>
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
/// newest two samples with a floor of `kVelocityMinDtSeconds` on dt. A
/// sample whose gap to the previous one is not a usable pairing records
/// speed 0 instead of dividing: a timestamp at or before the newest sample
/// (a clock that did not advance, or went backwards), and a gap of
/// `kVelocityHoldMs` or more (the pointer was parked, so the first move
/// after it starts the pairing fresh rather than being averaged over the
/// idle time). Every recency window in this class is a strict `<`: velocity
/// is reported while the newest sample is younger than `kVelocityHoldMs`,
/// the chain is live while an event is younger than `trailSeconds`, and the
/// damage rect includes a point while it is younger than `trailSeconds`. At
/// exactly the bound the window has closed.
class PHOSPHORPOINTER_EXPORT PointerHistory
{
public:
    static constexpr int kCapacity = PointerShaderContract::kMaxTrailPoints;
    static constexpr double kMinSampleDistancePx = 1.0;
    static constexpr qint64 kMinSampleGapMs = 8;
    /// Floor on the dt a speed is divided by, guarding against a zero or
    /// denormal gap between two events that arrive in the same millisecond.
    ///
    /// It must stay far below the real sampling interval. The distance is NOT
    /// floored alongside it, so whenever the sampler runs faster than
    /// 1/kVelocityMinDtSeconds every speed is under-reported by exactly
    /// dt/kVelocityMinDtSeconds. This was 1/30 s, which is slower than any
    /// real source: a 60 Hz motion stream reported half the true speed and a
    /// 125 Hz mouse a quarter of it, so `activationSpeed` and every other
    /// px-per-second parameter meant nothing like px per second. 1 ms clears
    /// a 1000 Hz mouse. Noise in the resulting figure is the exponential
    /// filter's job (pointerFilteredSpeed in pointer_lib.glsl), not this
    /// floor's.
    static constexpr double kVelocityMinDtSeconds = 0.001;
    /// How long after the newest sample the velocity is still reported, and
    /// the longest gap between two samples that still forms a speed pairing.
    /// Past it the pointer is parked and the next sample starts fresh: a 5 px
    /// move after ten idle seconds is a fresh move, not 0.5 px/s.
    static constexpr qint64 kVelocityHoldMs = 100;

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
    [[nodiscard]] PointerFrameState frameState(qint64 nowMs, double scale) const;

    /// True while a motion or button event is younger than @p trailSeconds.
    [[nodiscard]] bool isLive(qint64 nowMs, double trailSeconds) const;

    /// Bounding box of the trail samples, press and release points younger
    /// than @p trailSeconds, inflated by @p reachDevicePx on every side.
    /// Null when not live.
    [[nodiscard]] QRectF damageRect(double reachDevicePx, qint64 nowMs, double trailSeconds) const;

    /// Number of motion samples in the ring (0..kCapacity). The compositor
    /// seeds an empty ring from a buttons-only event so the first live frame
    /// after a reset has a pointer position to hand out.
    [[nodiscard]] int sampleCount() const
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
