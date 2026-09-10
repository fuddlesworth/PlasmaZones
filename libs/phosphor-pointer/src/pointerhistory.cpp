// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerHistory.h>

#include <QLineF>
#include <QVector2D>
#include <QVector4D>

#include <algorithm>
#include <cmath>

namespace PhosphorPointerShaders {

namespace {

int buttonCode(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return 1;
    case Qt::RightButton:
        return 2;
    case Qt::MiddleButton:
        return 3;
    default:
        return 0;
    }
}

int buttonsMask(Qt::MouseButtons buttons)
{
    int mask = 0;
    if (buttons & Qt::LeftButton) {
        mask |= 1;
    }
    if (buttons & Qt::RightButton) {
        mask |= 2;
    }
    if (buttons & Qt::MiddleButton) {
        mask |= 4;
    }
    return mask;
}

/// First of left / right / middle present in @p set, or NoButton.
Qt::MouseButton firstOf(Qt::MouseButtons set)
{
    for (Qt::MouseButton b : {Qt::LeftButton, Qt::RightButton, Qt::MiddleButton}) {
        if (set & b) {
            return b;
        }
    }
    return Qt::NoButton;
}

} // namespace

PointerHistory::PointerHistory() = default;

double PointerHistory::secondsBetween(qint64 laterMs, qint64 earlierMs)
{
    return static_cast<double>(laterMs - earlierMs) / 1000.0;
}

const PointerHistory::Sample& PointerHistory::sampleAt(int newestFirstIndex) const
{
    return m_ring[static_cast<size_t>((m_head - newestFirstIndex + kCapacity) % kCapacity)];
}

void PointerHistory::setTrailSeconds(double seconds)
{
    m_trailSeconds = std::max(0.0, seconds);
    // kCapacity samples span kCapacity - 1 gaps. Rounded up so the ring
    // covers at least the window rather than falling one sample short of it.
    const auto spread = static_cast<qint64>(std::ceil(m_trailSeconds * 1000.0 / (kCapacity - 1)));
    m_sampleIntervalMs = std::max(kMinSampleGapMs, spread);
}

double PointerHistory::speedOver(const QPointF& earlierPos, qint64 earlierMs, const QPointF& devicePx, qint64 nowMs)
{
    // Speed only over a usable pairing. A gap at or past the hold is the
    // pointer coming back from parked, and dividing over the idle time would
    // report the first move as a crawl and open a speed-gated pack one sample
    // late, so it records 0 and lets the next pair speak. A gap of zero or
    // less never reaches here from notePointer (such an event carries the
    // previous speed instead); the guard stays so the function is safe for
    // any caller.
    const qint64 gapMs = nowMs - earlierMs;
    if (gapMs <= 0 || gapMs >= kVelocityHoldMs) {
        return 0.0;
    }
    const double dt = std::max(secondsBetween(nowMs, earlierMs), kVelocityMinDtSeconds);
    return QLineF(earlierPos, devicePx).length() / dt;
}

void PointerHistory::notePointer(const QPointF& devicePx, qint64 nowMs)
{
    // Every speed here is read over the previous ACCEPTED motion event,
    // whether that one was appended or folded into the head. Before the
    // interval existed every event was a slot, so the previous slot was the
    // previous event; keeping the pairing at the event level keeps speed and
    // velocity meaning what they meant while the ring itself is spaced out.
    const bool hasPrev = m_hasMotion;
    const QPointF prevPos = m_lastEventPos;
    const qint64 prevMs = m_lastEventMs;
    // An event stamped at or before the previous accepted one has no dt to
    // divide over. It carries that event's speed and does NOT become the
    // pairing: the next event then pairs across both moves and reads the
    // true speed, where scoring it here would have left a zero on the head
    // for every event that shared a millisecond (see the header comment).
    const bool sameTick = hasPrev && nowMs - prevMs <= 0;
    // A park (the hold or more since the previous event) starts a stroke.
    const bool strokeStart = !hasPrev || (!sameTick && nowMs - prevMs >= kVelocityHoldMs);
    // The speed is only needed on the paths that accept the event, so the
    // sqrt is not paid for a sub-pixel drop or a stationary append.
    const auto scoreSpeed = [&] {
        return !hasPrev ? 0.0 : sameTick ? m_lastEventSpeed : speedOver(prevPos, prevMs, devicePx, nowMs);
    };
    const auto accept = [&](double speed) {
        if (sameTick) {
            m_lastMotionMs = nowMs;
            return;
        }
        acceptEvent(devicePx, nowMs, speed, hasPrev, prevPos, prevMs);
    };
    if (m_count > 0) {
        Sample& newest = m_ring[static_cast<size_t>(m_head)];
        // Measured against the last ACCEPTED event rather than the head slot:
        // a creep of under a pixel per sample interval would otherwise leave
        // stationary appends at each drifted position, never counting as
        // motion while the pointer was plainly moving. Against the event the
        // drift accumulates and is a move once it reaches a pixel.
        const QPointF motionOrigin = hasPrev ? prevPos : newest.pos;
        const bool moved = QLineF(motionOrigin, devicePx).length() >= kMinSampleDistancePx;
        // Measured from the append, not the last refresh, or a continuously
        // moving pointer would refresh forever and never fill the ring. A
        // gap of zero or less is inside the interval too: the compositor's
        // clock is whole milliseconds and a fast mouse lands several events
        // in the append's own millisecond, so letting those append would
        // hand each one a slot and fill the ring the interval exists to
        // spread.
        const qint64 gapMs = nowMs - newest.anchorMs;
        if (gapMs < m_sampleIntervalMs) {
            // Inside the sample interval. The slot is not appended to, or the
            // ring would fill with a few ms of motion and the tail could never
            // reach the window; but the pointer has still moved, so the head
            // follows it in place. Its speed is re-read against the sample
            // behind it, the pairing a fresh append would have used.
            if (!moved) {
                // A sub-pixel drift is not motion: it neither moves the head
                // nor becomes the pairing for the next event.
                return;
            }
            const double speed = scoreSpeed();
            newest.pos = devicePx;
            newest.timeMs = nowMs;
            newest.speed = speed;
            newest.motion = true;
            // A stroke can begin on a refresh too: a park shorter than the
            // interval but past the hold lands in the same slot.
            newest.strokeStart = newest.strokeStart || strokeStart;
            accept(speed);
            return;
        }
        if (!moved) {
            // The interval has passed with the pointer still. The slot lands
            // (the ring's minimum sampling rate is what gives a slow drag a
            // line to draw from) but it is not motion: the idle clock keeps
            // running, the pairing stays put and the filter skips it, so a
            // resting pointer reads the same on the preview, which feeds
            // every tick, as on the compositor, which feeds nothing.
            m_head = (m_head + 1) % kCapacity;
            m_ring[static_cast<size_t>(m_head)] = Sample{devicePx, nowMs, nowMs, 0.0, false, false};
            m_count = std::min(m_count + 1, kCapacity);
            return;
        }
    }
    const double speed = scoreSpeed();
    m_head = m_count > 0 ? (m_head + 1) % kCapacity : 0;
    m_ring[static_cast<size_t>(m_head)] = Sample{devicePx, nowMs, nowMs, speed, true, strokeStart};
    m_count = std::min(m_count + 1, kCapacity);
    accept(speed);
}

void PointerHistory::acceptEvent(const QPointF& devicePx, qint64 nowMs, double speed, bool hasPrev,
                                 const QPointF& prevPos, qint64 prevMs)
{
    m_hasPrevEvent = hasPrev;
    m_prevEventPos = prevPos;
    m_prevEventMs = prevMs;
    m_lastEventPos = devicePx;
    m_lastEventMs = nowMs;
    m_lastEventSpeed = speed;
    m_lastMotionMs = nowMs;
    m_hasMotion = true;
}

double PointerHistory::filteredSpeed() const
{
    // The newest MOTION sample: slots appended while the pointer rested sit
    // in front of it and are skipped, so a rest neither decays nor resets
    // the figure on either host.
    int newest = 0;
    while (newest < m_count && !sampleAt(newest).motion) {
        ++newest;
    }
    if (newest >= m_count) {
        return 0.0;
    }
    // Back to the stroke's start, marked at the event that followed a park.
    int oldest = newest;
    for (int i = newest; i < m_count; ++i) {
        if (!sampleAt(i).motion) {
            continue;
        }
        oldest = i;
        if (sampleAt(i).strokeStart) {
            break;
        }
    }
    // The stroke's first sample carries speed 0 by construction (it has no
    // usable pairing: it is the first event, or the one after a park), so it
    // is the boundary but not the seed. The seed is the oldest sample with a
    // scored speed, or a short stroke would read as a fraction of its speed
    // (a three-sample nudge after a park at 71 percent of it). Only when the
    // stroke start is the single motion sample is its 0 the answer.
    int seed = oldest;
    if (seed > newest && sampleAt(seed).strokeStart) {
        for (int i = seed - 1; i >= newest; --i) {
            if (sampleAt(i).motion) {
                seed = i;
                break;
            }
        }
    }
    // The same exponential filter the upstream windtrail effect runs on its
    // own sampler (a = 0.46), seeded from that sample rather than 0 so a
    // short stroke is not biased toward standing still.
    double filtered = sampleAt(seed).speed;
    for (int i = seed - 1; i >= newest; --i) {
        if (!sampleAt(i).motion) {
            continue;
        }
        filtered = filtered * 0.54 + sampleAt(i).speed * 0.46;
    }
    return std::max(filtered, 0.0);
}

void PointerHistory::seedPosition(const QPointF& devicePx, qint64 nowMs)
{
    if (m_count > 0) {
        return;
    }
    // A position without motion: the slot lands so the first live frame has
    // a pointer to hand out, but the idle clock, the pairing and the filter
    // are untouched, exactly as a rest slot appended by a host that feeds
    // every tick. Seeding through notePointer would have made a click after
    // a reset count as a move on the compositor alone.
    m_head = 0;
    m_ring[0] = Sample{devicePx, nowMs, nowMs, 0.0, false, false};
    m_count = 1;
}

void PointerHistory::noteButtons(Qt::MouseButtons now, Qt::MouseButtons before, const QPointF& devicePx, qint64 nowMs)
{
    const Qt::MouseButton pressed = firstOf(now & ~before);
    if (pressed != Qt::NoButton) {
        m_pressPos = devicePx;
        m_pressMs = nowMs;
        m_pressButton = buttonCode(pressed);
        m_hasPress = true;
    }
    const Qt::MouseButton released = firstOf(before & ~now);
    if (released != Qt::NoButton) {
        m_releasePos = devicePx;
        m_releaseMs = nowMs;
        m_releaseButton = buttonCode(released);
        m_hasRelease = true;
    }
    m_buttonsMask = buttonsMask(now);
}

PointerFrameState PointerHistory::frameState(qint64 nowMs, double scale) const
{
    PointerFrameState s;
    s.scale = scale;
    s.buttons = m_buttonsMask;
    s.filteredSpeed = filteredSpeed();

    // Strict `<` on the hold, like every other window here (see the header's
    // sampling contract). Read over the last two accepted events, the pairing
    // notePointer scored the head's speed from, so the two agree; a pair
    // recorded as speed 0 must not become a velocity here.
    if (m_hasPrevEvent && nowMs - m_lastEventMs < kVelocityHoldMs) {
        const qint64 pairGapMs = m_lastEventMs - m_prevEventMs;
        if (pairGapMs > 0 && pairGapMs < kVelocityHoldMs) {
            const double dt = std::max(secondsBetween(m_lastEventMs, m_prevEventMs), kVelocityMinDtSeconds);
            s.velocity = QVector2D(static_cast<float>((m_lastEventPos.x() - m_prevEventPos.x()) / dt),
                                   static_cast<float>((m_lastEventPos.y() - m_prevEventPos.y()) / dt));
        }
    }

    if (m_hasPress) {
        s.pressPos = m_pressPos;
        s.pressSecondsSince = std::max(0.0, secondsBetween(nowMs, m_pressMs));
        s.pressButton = m_pressButton;
    }
    if (m_hasRelease) {
        s.releasePos = m_releasePos;
        s.releaseSecondsSince = std::max(0.0, secondsBetween(nowMs, m_releaseMs));
        s.releaseButton = m_releaseButton;
    }
    if (m_hasMotion) {
        s.idleSeconds = std::max(0.0, secondsBetween(nowMs, m_lastMotionMs));
    }

    // Filled in place: the frame state's storage is fixed at the contract
    // capacity, which the ring never exceeds, so no allocation per frame.
    s.trailCount = m_count;
    for (int i = 0; i < m_count; ++i) {
        const Sample& sample = sampleAt(i);
        s.trail[static_cast<size_t>(i)] = QVector4D(
            static_cast<float>(sample.pos.x()), static_cast<float>(sample.pos.y()),
            static_cast<float>(std::max(0.0, secondsBetween(nowMs, sample.timeMs))), static_cast<float>(sample.speed));
    }
    return s;
}

bool PointerHistory::isLive(qint64 nowMs, double trailSeconds) const
{
    const auto young = [&](bool has, qint64 atMs) {
        return has && secondsBetween(nowMs, atMs) < trailSeconds;
    };
    return young(m_hasMotion, m_lastMotionMs) || young(m_hasPress, m_pressMs) || young(m_hasRelease, m_releaseMs);
}

QRectF PointerHistory::damageRect(double reachDevicePx, qint64 nowMs, double trailSeconds) const
{
    if (!isLive(nowMs, trailSeconds)) {
        return {};
    }
    bool any = false;
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    const auto include = [&](const QPointF& p) {
        if (!any) {
            minX = maxX = p.x();
            minY = maxY = p.y();
            any = true;
            return;
        }
        minX = std::min(minX, p.x());
        minY = std::min(minY, p.y());
        maxX = std::max(maxX, p.x());
        maxY = std::max(maxY, p.y());
    };
    for (int i = 0; i < m_count; ++i) {
        const Sample& sample = sampleAt(i);
        if (secondsBetween(nowMs, sample.timeMs) < trailSeconds) {
            include(sample.pos);
        }
    }
    if (m_hasPress && secondsBetween(nowMs, m_pressMs) < trailSeconds) {
        include(m_pressPos);
    }
    if (m_hasRelease && secondsBetween(nowMs, m_releaseMs) < trailSeconds) {
        include(m_releasePos);
    }
    if (!any) {
        // Live on motion recency alone (the newest sample is the pointer): a
        // ring with samples always has at least the newest inside the window,
        // so this only happens with no samples at all.
        return {};
    }
    const double r = std::max(0.0, reachDevicePx);
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).adjusted(-r, -r, r, r);
}

void PointerHistory::reset()
{
    // The window is host configuration, not pointer state: a reset on an
    // output crossing must not drop the sampler back to the 8 ms floor.
    const double trail = m_trailSeconds;
    *this = PointerHistory();
    setTrailSeconds(trail);
}

} // namespace PhosphorPointerShaders
