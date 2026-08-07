// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <cmath>

namespace PlasmaZones {

/// The strip shader pass's motion arithmetic, extracted from
/// StripTransitionManager so it is testable without a compositor: plain
/// numbers in, plain numbers out, no KWin types (see
/// tests/unit/test_strip_motion_sampler.cpp, which drives it with a
/// hand-rolled clock the way test_strip_view_animator drives the animator).
///
/// One sampler per armed output. It owns four responsibilities:
///
///  1. VELOCITY — a finite difference of the view offset across painted
///     frames, smoothed by a one-pole filter so per-frame noise does not
///     spike a velocity-driven blur.
///  2. BATCH-JUMP COMPENSATION — a wheel batch steps the spring's committed
///     value (and therefore the offset) by a full column width in ONE frame
///     with no time passing. That step is not visual motion (the drawn
///     position stays continuous by design), so the caller feeds the batch
///     delta in via compensateBatchJump() and the finite difference sees
///     only spring motion. Without this, every wheel notch spiked the
///     velocity by an order of magnitude and flipped its sign for a frame.
///  3. GAP DISCIPLINE — an interval above kMaxVelocityDtMs (a paint stall, a
///     desktop-transition preemption, DPMS) skips the velocity update
///     entirely and restamps the baseline. Dividing the full displacement by
///     a clamped dt would over-report velocity by the gap ratio, and
///     updating against a stale offset would spike it; both are refused.
///  4. TIME — iTime is the SUM of painted-frame dts (capped per frame), not
///     wall clock since activation, so a suspension gap does not leap a
///     time-driven pack forward by the whole gap on resume. Monotonic, never
///     rewinds.
///
/// It also owns the SETTLE FADE: the spring is cut at a nonzero velocity
/// (its settle band) and the smoothing filter lags besides, so the last
/// live frame still carries visible effect in every velocity-driven pack.
/// Instead of stopping there (a one-frame pop, worst after big flings), the
/// pass keeps painting for a short exponential fade of the last live
/// velocity down to nothing. holdsAfterSettle() answers whether the fade
/// (or its eligibility window) is still open; sampleSettleFade() steps it.
struct StripMotionSampler
{
    // ── Tuning ──
    /// One-pole velocity smoothing time constant.
    static constexpr qreal kVelocitySmoothingTauMs = 30.0;
    /// Longest interval the velocity estimator accepts; anything longer is
    /// a gap (stall/preemption) and skips the update.
    static constexpr qint64 kMaxVelocityDtMs = 100;
    /// Settle-fade decay constant and hard end (~4.4 tau, fade under 1.3%).
    static constexpr qreal kSettleFadeTauMs = 45.0;
    static constexpr qint64 kSettleFadeMaxMs = 200;
    /// The fade only starts if the pass painted this recently before the
    /// spring died — an entry idle longer than this settles silently.
    static constexpr qint64 kSettleStartWindowMs = 250;
    /// Below this speed (logical px/s) there is nothing visible to fade.
    static constexpr qreal kSettleVelocityEps = 1.0;

    // ── State ──
    /// Accumulated painted-frame time, ms. iTime = timeAccumMs / 1000.
    qint64 timeAccumMs = 0;
    /// Previous painted tick (pinned frame clock, ms); -1 = no baseline.
    qint64 lastPaintTimeMs = -1;
    /// Previous tick's view offset, logical px.
    qreal lastOffsetPx = 0.0;
    /// Smoothed velocity, logical px/s.
    qreal smoothedVelocity = 0.0;
    /// Settle-fade start (pinned clock, ms); -1 = not fading.
    qint64 settleFadeStartMs = -1;
    /// The velocity frozen at the fade's start, logical px/s.
    qreal settleVelocity = 0.0;

    /// A fresh leg begins (spring was not animating when the batch landed):
    /// drop every baseline so the new pass starts at iTime 0 with no stale
    /// velocity. Also the pack-swap reset — pack B must not inherit pack A's
    /// clock.
    void reset()
    {
        *this = StripMotionSampler();
    }

    /// The caller applied a batch delta to the spring's committed value
    /// while a leg was already live: shift the offset baseline by the same
    /// amount so the next finite difference sees only spring motion, not
    /// the committed step. No-op with no baseline (first frame measures
    /// nothing anyway).
    void compensateBatchJump(qreal deltaPx)
    {
        if (lastPaintTimeMs >= 0) {
            lastOffsetPx += deltaPx;
        }
    }

    /// One painted frame while the spring is LIVE. Returns the smoothed
    /// velocity (logical px/s) to upload. Cancels any settle fade — the
    /// spring restarting mid-fade resumes the live path seamlessly.
    qreal sampleLive(qreal offsetPx, qint64 nowMs)
    {
        if (lastPaintTimeMs >= 0) {
            const qint64 rawDt = nowMs - lastPaintTimeMs;
            if (rawDt > 0 && rawDt <= kMaxVelocityDtMs) {
                const qreal dtMs = qreal(rawDt);
                const qreal rawVelocity = (offsetPx - lastOffsetPx) / (dtMs / 1000.0);
                const qreal alpha = 1.0 - std::exp(-dtMs / kVelocitySmoothingTauMs);
                smoothedVelocity += alpha * (rawVelocity - smoothedVelocity);
                timeAccumMs += rawDt;
            }
            // rawDt > kMaxVelocityDtMs: a gap — no velocity update, no time
            // leap; the restamp below re-baselines both.
        }
        lastOffsetPx = offsetPx;
        lastPaintTimeMs = nowMs;
        settleFadeStartMs = -1;
        return smoothedVelocity;
    }

    /// Whether the pass should keep painting although the spring is dead:
    /// either a fade is in flight, or the entry painted recently enough
    /// with visible speed that a fade should begin.
    bool holdsAfterSettle(qint64 nowMs) const
    {
        if (settleFadeStartMs >= 0) {
            return nowMs - settleFadeStartMs < kSettleFadeMaxMs;
        }
        return lastPaintTimeMs >= 0 && (nowMs - lastPaintTimeMs) < kSettleStartWindowMs
            && std::abs(smoothedVelocity) > kSettleVelocityEps;
    }

    /// One painted frame of the settle fade. Starts the fade on the first
    /// call, advances iTime by the (capped) painted dt, and returns the
    /// velocity to upload: the frozen last live velocity decayed toward
    /// zero. Returns exactly 0.0 once the fade window has closed.
    qreal sampleSettleFade(qint64 nowMs)
    {
        if (settleFadeStartMs < 0) {
            settleFadeStartMs = nowMs;
            settleVelocity = smoothedVelocity;
        }
        const qint64 elapsed = nowMs - settleFadeStartMs;
        if (elapsed >= kSettleFadeMaxMs) {
            smoothedVelocity = 0.0;
            return 0.0;
        }
        if (lastPaintTimeMs >= 0) {
            timeAccumMs += qMin(nowMs - lastPaintTimeMs, kMaxVelocityDtMs);
        }
        lastPaintTimeMs = nowMs;
        const qreal faded = settleVelocity * std::exp(-qreal(elapsed) / kSettleFadeTauMs);
        smoothedVelocity = faded;
        return faded;
    }
};

} // namespace PlasmaZones
