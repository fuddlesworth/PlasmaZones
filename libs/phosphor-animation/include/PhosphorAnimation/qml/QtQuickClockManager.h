// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>

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
 * @brief Per-process singleton that enforces "one QtQuickClock per QQuickWindow."
 *
 * Phase 4 decision N. `QtQuickClock`'s class doc (Phase 3) established
 * the invariant: two clocks bound to the same `QQuickWindow` each
 * connect independently to `beforeRendering`, double-counting signal
 * dispatch cost without producing different readings. The manager
 * reifies that invariant in code:
 *
 *   auto* clock = QtQuickClockManager::instance().clockFor(myWindow);
 *
 * returns the same `IMotionClock*` for every call with the same
 * `QQuickWindow*` for the lifetime of that window. When the window is
 * destroyed, the manager's internal `QPointer` bookkeeping drops the
 * entry on next access; the clock itself is owned by a `unique_ptr`
 * in the manager and destroyed when the window goes away or the
 * manager teardown runs (process exit).
 *
 * ## Consumer shape
 *
 * `PhosphorMotionAnimation` (Phase 4 sub-commit 4) looks up its clock
 * here from its enclosing `Item.window`. Direct C++ consumers of
 * `AnimatedValue<T>` from QML-adjacent code (a custom
 * `QQuickPaintedItem` that drives its own animations) call this to
 * obtain a clock without having to manage the one-per-window
 * bookkeeping.
 *
 * The manager does **not** expose itself to QML — it's a C++-only
 * plumbing singleton. QML authors never need to see it; they write
 * `PhosphorMotionAnimation` and the animation subclass wires the
 * clock behind their back.
 *
 * ## Thread safety
 *
 * `instance()` is callable from any thread. `clockFor()` MUST be
 * called on the thread that owns @p window (always the GUI thread
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
 * The manager itself is a process-wide singleton (Meyers) kept alive
 * for the entire process lifetime. It holds `unique_ptr<QtQuickClock>`
 * values keyed on raw `QQuickWindow*`. When a window is destroyed,
 * Qt's internal teardown deletes the window first; the manager
 * discovers the stale key on its next `clockFor` call (via `QPointer`
 * null check) and evicts the entry. Process-exit cleanup runs the
 * manager's destructor which tears down every remaining clock.
 *
 * A more eager teardown (subscribe to `QQuickWindow::destroyed` and
 * evict on signal) was considered and rejected for sub-commit 2 —
 * the lazy evict is sufficient given that stale entries consume
 * negligible memory and the Phase-3 `reapAnimationsForClock` hook
 * handles the animation side at output/window teardown anyway. A
 * future sub-commit can wire the signal if profiling shows the
 * lazy path is a bottleneck.
 */
class PHOSPHORANIMATION_EXPORT QtQuickClockManager
{
public:
    /// Process-wide singleton accessor. Meyers-scoped — thread-safe
    /// initialisation under C++17's static-local init guarantees.
    static QtQuickClockManager& instance();

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

    // Non-copyable — singleton.
    QtQuickClockManager(const QtQuickClockManager&) = delete;
    QtQuickClockManager& operator=(const QtQuickClockManager&) = delete;

private:
    QtQuickClockManager();
    ~QtQuickClockManager();

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
