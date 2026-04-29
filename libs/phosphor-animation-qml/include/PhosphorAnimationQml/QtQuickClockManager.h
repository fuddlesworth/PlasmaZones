// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimationQml/phosphoranimationqml_export.h>

#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace PhosphorAnimation {

class IMotionClock;
class QtQuickClock;

/**
 * @brief Enforces "one QtQuickClock per QQuickWindow" within a single
 *        composition-root-owned manager instance.
 *
 * Phase 4 decision N. `QtQuickClock`'s class doc (Phase 3) established
 * the invariant: two clocks bound to the same `QQuickWindow` each
 * connect independently to `beforeRendering`, double-counting signal
 * dispatch cost without producing different readings. The manager
 * reifies that invariant in code:
 *
 *   auto* clock = QtQuickClockManager::defaultManager()->clockFor(myWindow);
 *
 * returns the same `IMotionClock*` for every call with the same
 * `QQuickWindow*` for the lifetime of that window. When the window is
 * destroyed, the manager's internal `QPointer` bookkeeping drops the
 * entry on next access; the clock itself is owned by a `unique_ptr`
 * in the manager and destroyed when the window goes away or the
 * manager itself is destroyed (composition-root teardown).
 *
 * ## Ownership: composition-root DI bridged through a QML service locator
 *
 * The manager is created and owned by the composition root (daemon,
 * editor, settings — each a separate process today). It is NOT a
 * Meyers singleton — its lifetime is bounded by the composition root,
 * not by the first call.
 *
 * QML consumers can't be handed the manager via constructor injection
 * (`PhosphorAnimatedValueBase`-derived types are created by the QML
 * engine), so the composition root publishes its locally-owned manager
 * via `setDefaultManager(...)` and `defaultManager()` reads it back —
 * a process-wide service locator narrowly scoped to the QML bridge.
 * Tests construct their own local manager per fixture and skip the
 * static. Same pattern as `PhosphorCurve::setDefaultRegistry`.
 *
 * ## Consumer shape
 *
 * `PhosphorMotionAnimation` (Phase 4 sub-commit 4) and the
 * `PhosphorAnimatedValueBase` family look up the manager via
 * `defaultManager()` and call `clockFor` from the enclosing
 * `Item.window`. Direct C++ consumers of `AnimatedValue<T>` from
 * QML-adjacent code (a custom `QQuickPaintedItem` that drives its
 * own animations) call this to obtain a clock without having to
 * manage the one-per-window bookkeeping.
 *
 * The manager does **not** expose itself to QML — it's a C++-only
 * plumbing handle. QML authors never need to see it; they write
 * `PhosphorMotionAnimation` and the animation subclass wires the
 * clock behind their back.
 *
 * ## Thread safety
 *
 * `defaultManager()` is callable from any thread. `clockFor()` MUST
 * be called on the thread that owns @p window (always the GUI thread
 * for a live `QQuickWindow`); it asserts this contract in debug
 * builds. The internal `std::mutex` guards the map mutation, but
 * the `QObject::connect(window, ...)` call the method performs
 * outside the lock dereferences @p window directly — letting a
 * non-owning thread race a concurrent window destruction there
 * would UAF inside Qt's connection machinery.
 *
 * The returned `IMotionClock*` is the `QtQuickClock` instance; its
 * own `now()` / `requestFrame()` thread-safety story (documented on
 * `QtQuickClock`) applies to subsequent use.
 *
 * ## Lifetime
 *
 * The manager holds `unique_ptr<QtQuickClock>` values keyed on raw
 * `QQuickWindow*`. Eager eviction wires a `QQuickWindow::destroyed`
 * lambda at `clockFor` time; the lambda drops the entry synchronously
 * so a recycled `QQuickWindow*` address never re-hits a stale clock.
 *
 * The composition root owns the manager via `unique_ptr` (typically
 * a member of the daemon / app object). The published default-handle
 * pointer must be cleared (`setDefaultManager(nullptr)`) before the
 * manager destructs so a successive composition (in tests, or a
 * daemon reconfigure cycle) does not dangle.
 *
 * ## Process-exit edge cases
 *
 * Even with composition-root ownership, two hazards remain:
 *
 *   1. **Dangling `destroyed` lambda.** Each `Entry` stores the
 *      `QMetaObject::Connection` for its window's `destroyed` signal.
 *      If a `QQuickWindow` somehow outlives this manager (e.g., a
 *      non-standard teardown where `QApplication` is destroyed AFTER
 *      the manager, or an embedded shell that leaks a window), the
 *      destroyed signal would dispatch the DirectConnection lambda
 *      into an already-destroyed `this`, UAF on `m_mutex`. The
 *      destructor disconnects every stored connection under the lock
 *      to close this path.
 *
 *   2. **Common `main()` shape — `QApplication app(argc, argv); … ;
 *      return app.exec();`** — makes `QApplication` a stack local.
 *      Its destructor runs BEFORE main()'s statics unwind, so every
 *      QQuickWindow is gone before any composition-root-owned manager
 *      destructs. Under that contract the hardening in (1) is a no-op.
 *      Embedded hosts that heap-allocate QApp or wire their own exit
 *      path are the teardowns that actually exercise the disconnect
 *      loop.
 *
 * A previous revision used a lazy-eviction-only design (no eager
 * `destroyed` hook) on the theory that stale entries consume
 * negligible memory and the Phase-3 `reapAnimationsForClock` hook
 * handles the animation side. That design was superseded once
 * address-reuse (Qt recycling a raw `QQuickWindow*` for a fresh
 * window) was identified as a ghost-entry hazard — the eager
 * eviction closes it.
 */
class PHOSPHORANIMATIONQML_EXPORT QtQuickClockManager
{
public:
    QtQuickClockManager();
    ~QtQuickClockManager();

    /// Publish @p manager as the process-wide default for QML-side
    /// consumers (`PhosphorAnimatedValueBase::resolveClock` /
    /// `PhosphorMotionAnimation`) to look up. Called once by each
    /// composition root after constructing its own manager; pass
    /// `nullptr` on teardown to drop the handle before the manager
    /// destructs.
    static void setDefaultManager(QtQuickClockManager* manager);

    /// Read-only view of the manager pointer published by
    /// `setDefaultManager`. Returns `nullptr` when no composition root
    /// has published yet — QML callsites then return a null clock and
    /// fall back to library-default fixed-duration animation.
    static QtQuickClockManager* defaultManager();

    /// Return the clock for @p window, constructing it on first call.
    /// Returns nullptr if @p window is nullptr. Subsequent calls with
    /// the same @p window return the same pointer.
    ///
    /// Must be called on the thread that owns @p window (always the
    /// GUI thread for a live `QQuickWindow`); the call installs a
    /// `destroyed` hook on @p window which dereferences the pointer
    /// outside the manager's lock. Asserted in debug builds.
    ///
    /// Stale-window handling: the manager tracks entries via
    /// `QPointer<QQuickWindow>`. A call after the window was destroyed
    /// detects the null-QPointer and evicts the entry before returning
    /// nullptr (the caller should not be routing animations to a
    /// destroyed window — this just stops the manager from returning
    /// a clock whose `requestFrame()` would target freed state).
    IMotionClock* clockFor(QQuickWindow* window);

    /// Drop the clock entry for @p window, if any. Called by the
    /// destructor-on-window-signal wiring (deferred — see class doc)
    /// and by tests that want to exercise construction + teardown.
    /// Firing `reapAnimationsForClock` on every controller that
    /// captured the dropped clock is the caller's responsibility —
    /// the manager has no visibility into which controllers captured
    /// what.
    void releaseClockFor(QQuickWindow* window);

    // ─── Test helpers ───

    /// Current number of active entries. Does not evict stale ones
    /// (opposed to `clockFor` which does). Lets tests assert the
    /// "one clock per window" contract without triggering eviction
    /// as a side effect.
    int entryCount() const;

    /// Drop every entry. Intended for unit-test teardown — production
    /// code must not call this (animations mid-flight on the dropped
    /// clocks would UAF on their next advance).
    void clearForTest();

    // Non-copyable — owns a registry of unique window->clock entries.
    QtQuickClockManager(const QtQuickClockManager&) = delete;
    QtQuickClockManager& operator=(const QtQuickClockManager&) = delete;

private:
    /// Atomic for the same reason as `PhosphorProfileRegistry::s_defaultRegistry`:
    /// concurrent QML loaders (multiple QQmlEngine instances on different
    /// threads — a background-prerender shell is the canonical case)
    /// cannot race on install-vs-read. Pointer loads are lock-free on
    /// every platform Qt supports; `relaxed` ordering is sufficient
    /// because the manager's own initialisation is synchronised by the
    /// composition root's construction.
    static std::atomic<QtQuickClockManager*> s_defaultManager;

    struct Entry
    {
        QPointer<QQuickWindow> window;
        std::unique_ptr<QtQuickClock> clock;
        /// Connection to the window's `destroyed` signal — lets the
        /// manager evict this entry eagerly when the window goes away,
        /// rather than waiting for another `clockFor` call to detect
        /// the stale QPointer. Without eager eviction, a raw pointer
        /// value that Qt later recycles for a DIFFERENT window would
        /// be served the old clock (the "ghost entry from address
        /// reuse" hazard the class doc flagged as a future sub-commit
        /// fix).
        QMetaObject::Connection destroyedConnection;
    };

    mutable std::mutex m_mutex;
    // std::unordered_map (not QHash) because `Entry` contains a
    // move-only `unique_ptr<QtQuickClock>` — Qt6's QHash still requires
    // copy-constructible values for its internal Node type. Same
    // rationale as `AnimationController::m_animations` in Phase 3.
    // Keyed on raw pointer so hash / equality are cheap. `QPointer`
    // inside `Entry` is consulted on lookup to detect stale windows;
    // keying on `QPointer` directly would be possible but hash /
    // comparison against raw `QQuickWindow*` keys in callers would
    // demand conversions on every lookup.
    std::unordered_map<QQuickWindow*, Entry> m_entries;
};

} // namespace PhosphorAnimation
