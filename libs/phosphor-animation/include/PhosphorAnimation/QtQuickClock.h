// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QPointer>

#include <chrono>
#include <memory>

class QQuickWindow;

namespace PhosphorAnimation {

/**
 * @brief Qt Quick adapter implementing `IMotionClock`.
 *
 * Ships only when the library is built with `PHOSPHOR_ANIMATION_QUICK=ON`
 * — the flag gates both the header install and the `Qt6::Quick` link
 * dependency so core consumers that don't need QML motion (the KWin
 * compositor adapter, headless tools, daemon) don't pull in Qt Quick
 * transitively.
 *
 * Bound to one `QQuickWindow` per instance. Multi-window QML shells
 * construct one `QtQuickClock` per top-level window and route their
 * `AnimatedValue<T>` instances through the matching clock — same
 * per-output phase-locking rationale as `CompositorClock`.
 *
 * ## Driver model
 *
 * The clock subscribes to `QQuickWindow::beforeRendering` — fires once
 * per scene-graph render pass on the render thread. The connected
 * lambda latches the current `std::chrono::steady_clock::now()` into
 * the clock's cached timestamp, so `now()` returns a vsync-aligned
 * reading for the frame currently being rendered.
 *
 * `requestFrame()` forwards to `QQuickWindow::update()`, which is the
 * Qt Quick equivalent of `effects->addRepaint()` — schedules another
 * render pass from whichever thread the call lands on (Qt's `update()`
 * is thread-safe by design).
 *
 * ## Refresh rate
 *
 * `refreshRate()` reads from `window->screen()->refreshRate()` when
 * available. Returns 0.0 (unknown) if the window is not yet shown or
 * has no screen attached — the usual state during construction.
 *
 * ## Monotonicity
 *
 * `std::chrono::steady_clock` is monotonic by contract, so the clamp
 * `CompositorClock` applies for KWin's (rarely) regressing presentTime
 * is unnecessary here.
 */
class PHOSPHORANIMATION_EXPORT QtQuickClock final : public IMotionClock
{
public:
    /// Construct a clock bound to @p window. The clock subscribes to
    /// the window's `beforeRendering` signal immediately — construction
    /// does not require the window to be shown. @p window may be null
    /// for testing; in that mode `now()` stays at zero and
    /// `requestFrame()` is a no-op.
    explicit QtQuickClock(QQuickWindow* window);
    ~QtQuickClock() override;

    // IMotionClock
    std::chrono::nanoseconds now() const override;
    qreal refreshRate() const override;
    void requestFrame() override;

    /// The QQuickWindow this clock is bound to. May be null.
    QQuickWindow* window() const;

private:
    class SignalAdapter; // QObject helper defined in the .cpp
    friend class SignalAdapter;

    // Cached timestamp captured in the `beforeRendering` slot so
    // `now()` returns the vsync-aligned reading for the frame being
    // rendered. Written from the render thread by the SignalAdapter;
    // read from the same thread by AnimatedValue::advance().
    std::chrono::nanoseconds m_nowCache{0};
    QPointer<QQuickWindow> m_window;
    std::unique_ptr<SignalAdapter> m_adapter;
};

} // namespace PhosphorAnimation
