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
/// Sampling rule: the ring has `kCapacity` slots and has to cover the pack's
/// whole `trailSeconds` window with them, so `notePointer` appends a new
/// sample only when the gap since the newest one is at least the sample
/// interval (`sampleIntervalMs`, derived from `setTrailSeconds`: the window
/// spread over `kCapacity - 1` gaps, never under `kMinSampleGapMs`). An event
/// inside that gap that has moved at least 1 device px REFRESHES the newest
/// sample in place, so index 0 is always exactly where the pointer is, and a
/// sub-pixel drift inside the gap is dropped. "Moved" is measured against
/// the last ACCEPTED event (the head slot when there is none), so a creep of
/// under a pixel per interval accumulates into a move rather than ageing
/// the idle clock. (A stamp behind the previous event whose gap to the
/// slot's append is still past the interval appends a slot; that needs a
/// clock that stepped back by more than an interval, which neither host's
/// monotonic clock does.) Without the interval a 1000 Hz
/// mouse filled all 32 slots in 32 ms, and a pack's tail could never be
/// longer than that however long its `length` parameter asked for. A sample
/// appended once the interval has passed WITHOUT the pointer having moved a
/// pixel (the preview feeds a resting pointer every tick) keeps the ring's
/// minimum sampling rate but is not motion: it neither resets the idle clock
/// nor becomes a velocity pairing, so a resting pointer idles the same way
/// on both hosts. Ages are computed at `frameState` time. Velocity is the
/// finite difference over the last two ACCEPTED events (appended or folded
/// into the head alike) with a floor of `kVelocityMinDtSeconds` on dt, and
/// the head sample's speed is scored over the same pairing so the two agree.
/// An event stamped at or before the previous accepted one (the same
/// millisecond, or a clock that stepped backwards) is not a usable pairing
/// (nothing can be divided over it), so it carries the previous event's
/// speed and leaves the pairing where it was, and the next event pairs
/// across both moves; a multi-kHz mouse then reads its true speed instead of
/// a zero for every event that shared a millisecond. Both real clocks are
/// monotonic, so the backwards case is the same-millisecond case in
/// practice. A gap of `kVelocityHoldMs` or more (the pointer was parked)
/// records speed 0, starts the pairing fresh rather than averaging the move
/// over the idle time, and marks the sample as the START OF A STROKE. The
/// frame state also carries `filteredSpeed`, the same exponential filter the
/// shared shader library used to walk per fragment, computed here once per
/// frame over the current stroke: the motion samples back from the head to
/// the nearest stroke start. Marking the start at the event, from the event
/// gap, is what makes this independent of how far apart the ring's slots
/// are: a shader-side walk over sample ages had to guess a pause from a
/// gap, and at a long window every slot gap looked like one. Samples
/// appended while the pointer rested are not motion and are skipped, so the
/// preview (which feeds a resting pointer every tick) holds the filtered
/// speed across a rest exactly as the compositor does; the packs' own idle
/// fades end the drawing, not the gate.
///
/// The interval is one per chain, from its LONGEST `trailSeconds`, so a
/// chain that mixes a short pack with a long one samples at the long pack's
/// spacing and the short pack draws its tail from a few slots plus the
/// refreshed head. A per-layer spread would need a ring per layer.
/// Every recency window in this class is a strict `<`: velocity
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
    /// filter's job (`filteredSpeed()`, which the shader library reads as
    /// pointerFilteredSpeed()), not this floor's.
    static constexpr double kVelocityMinDtSeconds = 0.001;
    /// How long after the newest sample the velocity is still reported, and
    /// the longest gap between two samples that still forms a speed pairing.
    /// Past it the pointer is parked and the next sample starts fresh: a 5 px
    /// move after ten idle seconds is a fresh move, not 0.5 px/s.
    static constexpr qint64 kVelocityHoldMs = 100;

    PointerHistory();

    /// The window the ring must span, in seconds: the longest `trailSeconds`
    /// of the packs this history feeds. Sets the sample interval (see the
    /// sampling rule above). 0 (the default) means the `kMinSampleGapMs`
    /// floor alone. Survives `reset()`.
    void setTrailSeconds(double seconds);
    [[nodiscard]] double trailSeconds() const
    {
        return m_trailSeconds;
    }
    /// Minimum gap between two appended samples, in ms.
    [[nodiscard]] qint64 sampleIntervalMs() const
    {
        return m_sampleIntervalMs;
    }

    /// Record a pointer position at @p nowMs (see the sampling rule above).
    void notePointer(const QPointF& devicePx, qint64 nowMs);

    /// Give an EMPTY ring a position without recording motion: the slot
    /// lands (age 0, speed 0, not a motion sample) so the first live frame
    /// has a pointer to hand out, while the idle clock, the velocity pairing
    /// and the filtered speed stay untouched. The compositor seeds from a
    /// buttons-only event after a reset this way; a ring with samples is
    /// left alone.
    void seedPosition(const QPointF& devicePx, qint64 nowMs);

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

    void reset();

private:
    struct Sample
    {
        QPointF pos;
        qint64 timeMs = 0;
        /// When this slot was appended. An in-place refresh moves timeMs but
        /// not this, so the interval is measured from the append and the ring
        /// keeps filling while the pointer moves.
        qint64 anchorMs = 0;
        double speed = 0.0;
        /// False for a slot appended while the pointer rested (the interval
        /// passed with no movement of a pixel): it holds the ring's minimum
        /// sampling rate but is not motion, so the filter skips it.
        bool motion = true;
        /// True when the event that appended or refreshed this slot came
        /// after a park (`kVelocityHoldMs` or more since the previous
        /// accepted event): the stroke the filter walks begins here.
        bool strokeStart = false;
    };

    const Sample& sampleAt(int newestFirstIndex) const;
    static double secondsBetween(qint64 laterMs, qint64 earlierMs);
    /// Speed of a move from @p earlier to @p devicePx at @p nowMs, or 0 when
    /// the pair is not a usable pairing (see the header comment).
    static double speedOver(const QPointF& earlierPos, qint64 earlierMs, const QPointF& devicePx, qint64 nowMs);
    /// Record @p devicePx at @p nowMs as the newest accepted motion event,
    /// with the previous one (if @p hasPrev) as its velocity pairing and
    /// @p speed as the speed scored over that pairing.
    void acceptEvent(const QPointF& devicePx, qint64 nowMs, double speed, bool hasPrev, const QPointF& prevPos,
                     qint64 prevMs);
    /// The exponentially filtered speed over the current stroke (see the
    /// header comment), in device px/s.
    [[nodiscard]] double filteredSpeed() const;

    double m_trailSeconds = 0.0;
    qint64 m_sampleIntervalMs = kMinSampleGapMs;

    std::array<Sample, kCapacity> m_ring{};
    int m_head = 0; ///< index of the newest sample (valid when m_count > 0)
    int m_count = 0;

    qint64 m_lastMotionMs = 0;
    bool m_hasMotion = false;

    /// The last two accepted motion events, the pairing velocity is read
    /// over. Kept apart from the ring because an event inside the sample
    /// interval folds into the head slot rather than taking one of its own.
    QPointF m_lastEventPos;
    qint64 m_lastEventMs = 0;
    double m_lastEventSpeed = 0.0;
    QPointF m_prevEventPos;
    qint64 m_prevEventMs = 0;
    bool m_hasPrevEvent = false;

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
