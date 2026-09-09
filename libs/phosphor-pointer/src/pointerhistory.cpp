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

void PointerHistory::notePointer(const QPointF& devicePx, qint64 nowMs)
{
    double speed = 0.0;
    if (m_count > 0) {
        const Sample& newest = sampleAt(0);
        const double dist = QLineF(newest.pos, devicePx).length();
        const qint64 gapMs = nowMs - newest.timeMs;
        if (dist < kMinSampleDistancePx && gapMs < kMinSampleGapMs) {
            return;
        }
        // Speed only over a usable pairing. A gap of zero or less is a clock
        // that stalled or stepped back, and dividing by the dt floor would
        // turn a few px into thousands of px/s. A gap at or past the hold is
        // the pointer coming back from parked, and dividing over the idle
        // time would report the first move as a crawl and open a speed-gated
        // pack one sample late. Both record 0 and let the next pair speak.
        if (gapMs > 0 && gapMs < kVelocityHoldMs) {
            const double dt = std::max(secondsBetween(nowMs, newest.timeMs), kVelocityMinDtSeconds);
            speed = dist / dt;
        }
    }
    m_head = m_count > 0 ? (m_head + 1) % kCapacity : 0;
    m_ring[static_cast<size_t>(m_head)] = Sample{devicePx, nowMs, speed};
    m_count = std::min(m_count + 1, kCapacity);
    m_lastMotionMs = nowMs;
    m_hasMotion = true;
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

    // Strict `<` on the hold, like every other window here (see the header's
    // sampling contract). The pairing test mirrors notePointer's: a pair the
    // sampler recorded as speed 0 must not become a velocity here.
    if (m_count >= 2 && nowMs - sampleAt(0).timeMs < kVelocityHoldMs) {
        const Sample& a = sampleAt(0);
        const Sample& b = sampleAt(1);
        const qint64 pairGapMs = a.timeMs - b.timeMs;
        if (pairGapMs > 0 && pairGapMs < kVelocityHoldMs) {
            const double dt = std::max(secondsBetween(a.timeMs, b.timeMs), kVelocityMinDtSeconds);
            s.velocity = QVector2D(static_cast<float>((a.pos.x() - b.pos.x()) / dt),
                                   static_cast<float>((a.pos.y() - b.pos.y()) / dt));
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
    *this = PointerHistory();
}

} // namespace PhosphorPointerShaders
