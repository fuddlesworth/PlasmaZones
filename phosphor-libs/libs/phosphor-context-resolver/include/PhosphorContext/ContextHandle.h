// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorZones/AssignmentEntry.h>

#include <QString>

namespace PhosphorContext {

/**
 * @brief Frozen snapshot of "where am I right now?" for a single resolver query.
 *
 * Built once via `IContextResolver::handleFor(screenId)` (or one of its
 * siblings) and passed by const-reference into every effective-value and
 * gate query the caller needs. The snapshot pattern is deliberate:
 *
 *   1. **Consistency.** Every gate / value query against one handle uses the
 *      same (desktop, activity, mode) tuple, so a handler can't read a
 *      disable list for desktop N and then commit a layout against desktop
 *      N+1 because the user happened to virtual-desktop-switch mid-call.
 *
 *   2. **Pay-once cost.** `currentVirtualDesktop()` and `currentActivity()`
 *      each cross a virtual into PhosphorWorkspaces; resolving `modeFor()`
 *      crosses into ScreenModeRouter. The handle materialises those three
 *      reads exactly once per consumer call site instead of once per gate
 *      or value query inside that site.
 *
 *   3. **Single-line gate.** The handle is a value type whose presence at
 *      a call site documents the intent ("we are about to act on screen X
 *      with mode Y"). The accompanying `IContextResolver::isGated(handle)` call
 *      shrinks the typical 6-line `(modeFor → currentDesktop → currentActivity
 *      → isContextDisabled → isContextLocked)` chain to two lines.
 *
 * The struct is a value type — copy-able, no QObject, no signals — safe
 * to pass through Qt::QueuedConnection if a future caller wants to ferry
 * a snapshot across threads. (The two QString fields are implicitly
 * shared and copy-thread-safe; the struct itself is not a C POD because
 * QString has a non-trivial copy ctor.) Construction is reserved for
 * IContextResolver implementations; callers should not hand-build a
 * handle (the resolver's `mode` field has to come from the bound
 * `IModeProvider`, not a guess).
 */
struct ContextHandle
{
    /// The screen this context targets. Either a physical screen id, a
    /// virtual-screen id, or the empty string when the resolver was asked
    /// for an "active window" handle and no screen could be determined.
    QString screenId;

    /// The virtual desktop the user is currently on (1-based; 0 means
    /// "all desktops" / pinned in the Phosphor taxonomy).
    int virtualDesktop = 0;

    /// The current activity uuid. Empty when no activity manager is wired
    /// (headless tests) or when no activity is currently active.
    QString activity;

    /// The resolved mode for @ref screenId at snapshot time. Drives every
    /// disable / lock check that takes a Mode parameter, so the consumer
    /// does not re-look it up.
    PhosphorZones::AssignmentEntry::Mode mode = PhosphorZones::AssignmentEntry::Snapping;
};

} // namespace PhosphorContext
