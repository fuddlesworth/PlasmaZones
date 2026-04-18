// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QtGlobal>

#include <chrono>

namespace PhosphorAnimation {

/**
 * @brief Abstract clock interface for the motion runtime.
 *
 * Phase 3's unification of the animation runtime (see the design doc
 * under Roadmap → Phase 3) hinges on this interface. Every
 * `AnimatedValue<T>` reads `now()` once per `advance()` tick to derive
 * the frame's `dt` and step its `CurveState` toward target. The clock's
 * concrete implementation binds to whichever paint loop drives it —
 * `prePaintScreen` for the KWin adapter (`CompositorClock`),
 * `QQuickWindow::beforeRendering` for the QML adapter (`QtQuickClock`,
 * Phase 3 sub-commit 5).
 *
 * ## Why per-output / per-window scope
 *
 * Multi-monitor compositors run mixed refresh rates (60 Hz + 144 Hz
 * being the common case) that must phase-lock independently — a single
 * process-wide clock produces visible beating and double-stepping
 * because both outputs would poll the same `now()` on their own vsync.
 * Each consumer constructs one clock instance *per output* (or *per
 * `QQuickWindow`*) and routes the animations that belong to that
 * output through the matching clock. Window migrations between outputs
 * re-point the animation at the destination output's clock at the
 * migration boundary — a `retarget()` call with the new clock
 * substituted in `MotionSpec`.
 *
 * ## Pull model + outgoing `requestFrame()`
 *
 * The clock does not push per-frame notifications. Consumers pull via
 * `now()` during their own paint cycle. The one outgoing edge on the
 * clock is `requestFrame()` — the portable equivalent of KWin's
 * `effects->addRepaint()` and QtQuick's `QQuickWindow::update()`. Call
 * this when an animation wants to be stepped on the next frame but the
 * paint loop has no other reason to tick (e.g., an `AnimatedValue` was
 * just `start()`-ed or `retarget()`-ed and needs a first tick to latch
 * its `startTime`). Idempotent within a single frame — implementations
 * coalesce multiple requests into one scheduled tick so the
 * `AnimatedValue` owner doesn't need to de-duplicate.
 *
 * ## Thread safety
 *
 * Every `IMotionClock` method is GUI-thread-only. Concrete clocks live
 * on the compositor / QtQuick render thread; cross-thread access is
 * outside the supported contract. Consumers that need to drive
 * animations from worker threads must serialize through the owning
 * paint-loop thread before calling any clock method.
 */
class PHOSPHORANIMATION_EXPORT IMotionClock
{
public:
    virtual ~IMotionClock() = default;

    IMotionClock(const IMotionClock&) = delete;
    IMotionClock& operator=(const IMotionClock&) = delete;
    IMotionClock(IMotionClock&&) = delete;
    IMotionClock& operator=(IMotionClock&&) = delete;

    /**
     * @brief Current clock reading.
     *
     * Steady-clock semantics: monotonically non-decreasing, immune to
     * wall-clock adjustments. The absolute value is arbitrary (each
     * clock chooses its own epoch); consumers only use *differences*
     * between consecutive readings to derive `dt`. Nanosecond
     * precision matches both KWin's presentTime (delivered as
     * milliseconds today, upcast losslessly here) and QtQuick's
     * high-resolution timing, so the same interface can drive both
     * without unit-conversion friction at call sites.
     *
     * Implementations MUST guarantee monotonicity. If the underlying
     * source could run backwards (e.g., a rare KWin presentTime reset
     * on output hotplug), the implementation latches the last-observed
     * maximum and clamps `now()` to that until the source catches up.
     */
    virtual std::chrono::nanoseconds now() const = 0;

    /**
     * @brief Nominal refresh rate in Hz, or zero if unknown.
     *
     * Consumers use this as a *hint* for frame-budget sizing (e.g.,
     * picking a reasonable sample count for a curve's overshoot bound
     * or a stability substep count for stiff springs) — not as a
     * contract for `now()` cadence. The clock itself does not
     * guarantee that `now()` advances in 1/refreshRate() increments;
     * vsync misses, output switching, and dynamic refresh all violate
     * that cadence, and downstream code must not assume otherwise.
     *
     * Zero is the documented "unknown" sentinel. Compositors return
     * zero for disabled or virtual outputs; QtQuick returns zero when
     * the render loop has not yet bound to a surface.
     */
    virtual qreal refreshRate() const = 0;

    /**
     * @brief Ask the driver to schedule another paint tick.
     *
     * The portable replacement for `KWin::effects->addRepaint()` and
     * `QQuickWindow::update()`. An `AnimatedValue` calls this after
     * `start()` / `retarget()` / `cancel()` to guarantee the paint
     * loop ticks at least once more so the value can advance or
     * finalize. Idempotent within a single frame — implementations
     * coalesce multiple requests into the same scheduled tick.
     *
     * The damage region is intentionally *not* a parameter here.
     * Per-animation damage is the consumer's responsibility via
     * `MotionSpec<T>::onValueChanged` (see Phase 3 decision J in the
     * design doc). `requestFrame()` is purely "wake me next frame";
     * the compositor's own damage tracking plus the value-change
     * callback drive what gets invalidated.
     */
    virtual void requestFrame() = 0;

protected:
    // Protected default-ctor keeps derived classes constructible while
    // preventing `IMotionClock` instances from being stack-allocated by
    // consumers as a no-op fallback — every concrete clock must be an
    // actual driver binding.
    IMotionClock() = default;
};

} // namespace PhosphorAnimation
