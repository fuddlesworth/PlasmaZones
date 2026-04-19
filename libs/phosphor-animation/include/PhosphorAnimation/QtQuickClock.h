// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QPointer>

#include <atomic>
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
 *
 * ## Cross-thread read safety
 *
 * The `beforeRendering` slot fires on the Qt Quick render thread; any
 * consumer calling `now()` from a different thread (e.g., a QML scene
 * animation driven from the GUI thread) would otherwise race on a
 * plain 64-bit field. The cached timestamp is held as
 * `std::atomic<int64_t>` with `memory_order_relaxed` load/store so the
 * cross-thread read is well-defined even though the common case (both
 * reader and writer on the render thread) pays only the cost of a
 * plain aligned load/store on x86_64.
 *
 * ## Teardown race
 *
 * `Qt::DirectConnection` slots execute synchronously on the signal's
 * emitting thread; Qt's implicit `~QObject` disconnect does not
 * *join* already-running slot bodies. Destroying the clock on the
 * GUI thread while the render thread was mid-slot would otherwise
 * write to a freed cache. The adapter mitigates this internally —
 * explicit disconnect first, mutex-drained slot body, guard flag for
 * any emission that slips through. Consumers do not need to
 * externally synchronise destruction, but tearing down a clock while
 * the render thread is still active is still wasteful (the drain
 * blocks the GUI thread on a render-thread slot). Prefer destroying
 * clocks after the scene graph has been torn down.
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
    const void* epochIdentity() const override;

    /// The QQuickWindow this clock is bound to. May be null.
    QQuickWindow* window() const;

private:
    class SignalAdapter; // QObject helper defined in the .cpp
    friend class SignalAdapter;

    // Cached timestamp captured in the `beforeRendering` slot so
    // `now()` returns the vsync-aligned reading for the frame being
    // rendered. Written from the render thread by the SignalAdapter;
    // may be read from the GUI thread (QML animation drivers that
    // don't honour the render-thread-only contract). Stored as
    // atomic<int64_t>; the writer uses release and the reader uses
    // acquire so a cross-thread reader sees a well-published value
    // rather than just a tear-free one.
    //
    // `mutable` because the logically-const `now()` reader CAS-seeds
    // the cache on the pre-handoff fallback path to guarantee
    // monotonicity across the fallback→cache transition. Updating an
    // internal cache from a const observer is the canonical mutable
    // use case; the logical const contract ("now() returns the current
    // time") is unaffected.
    mutable std::atomic<std::chrono::nanoseconds::rep> m_nowCache{0};
    // Set the first time `beforeRendering` fires — the render loop
    // has become responsible for advancing `m_nowCache`. Until then
    // `now()` falls back to reading `steady_clock` directly so an
    // animation bound to a never-shown `QQuickWindow` (test mode,
    // hidden offscreen render loop) doesn't freeze at the prime
    // timestamp with dt=0 on every advance.
    std::atomic<bool> m_renderLoopActive{false};
    QPointer<QQuickWindow> m_window;
    std::unique_ptr<SignalAdapter> m_adapter;
};

} // namespace PhosphorAnimation
