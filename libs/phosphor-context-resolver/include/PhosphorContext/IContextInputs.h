// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorcontextresolver_export.h"

#include <PhosphorZones/AssignmentEntry.h>

#include <QString>

namespace PhosphorContext {

/**
 * @brief Adapter interfaces the concrete @ref ContextResolver depends on.
 *
 * The resolver itself is a thin façade — it owns no state, holds non-owning
 * pointers to one of each interface here, and routes every public call into
 * the right combination of them. Splitting the dependencies into three
 * narrow interfaces (instead of a single fat @c IInputs) lets test fakes
 * stub one axis at a time and lets daemon-side wiring use whatever object
 * already implements each axis (typically the existing
 * @c VirtualDesktopManager + @c ActivityManager for workspace state, a
 * @c ScreenModeRouter shim for mode, and the existing @c ISettings for the
 * gate source).
 *
 * All three interfaces are read-only. The resolver never mutates its
 * inputs; consumers that need to write through to the underlying settings
 * (e.g. flipping a monitor-disable list) keep calling the writer surface
 * directly.
 */

/**
 * @brief Workspace-clock adapter.
 *
 * Supplies the "where is the user RIGHT NOW" axis values that every
 * context-handle freezes at snapshot time. The daemon-side implementation
 * forwards to PhosphorWorkspaces' VirtualDesktopManager and ActivityManager
 * respectively; the test fake holds a pair of mutable fields and lets the
 * test scenario drive them by hand.
 *
 * Returning `0` for desktop and the empty string for activity is the
 * documented "no workspace concept" fallback — the daemon's own
 * `Daemon::currentDesktop()` / `currentActivity()` use the same sentinels
 * when their respective manager pointers are null.
 */
class PHOSPHORCONTEXTRESOLVER_EXPORT IWorkspaceState
{
public:
    virtual ~IWorkspaceState() = default;

    /// 1-based desktop index. Returns 0 when "pinned to all desktops" or
    /// when no VirtualDesktopManager is wired.
    virtual int currentVirtualDesktop() const = 0;

    /// 1-based desktop index for a specific screen (Plasma 6.7 "switch desktops
    /// independently for each screen", #648). The default ignores the screen and
    /// returns the global currentVirtualDesktop(), so single-desktop setups and
    /// non-per-output implementers are unaffected.
    virtual int currentVirtualDesktopForScreen(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return currentVirtualDesktop();
    }

    /// Current activity uuid. Empty when no ActivityManager is wired or
    /// no activity is currently active.
    virtual QString currentActivity() const = 0;
};

/**
 * @brief Per-screen mode dispatcher adapter.
 *
 * Resolves an opaque screen id to its current `AssignmentEntry::Mode`
 * (Snapping / Autotile / future modes). Daemon-side this is a one-line
 * wrapper around `ScreenModeRouter::modeFor(screenId)`. Kept as a separate
 * interface so the resolver does not transitively pull in the entire
 * ScreenModeRouter API (which carries `IPlacementEngine*` references that
 * an LGPL lib should not see).
 *
 * Unknown screen ids must resolve to the implementation's documented
 * default mode (typically `Snapping` per the daemon's `ScreenModeRouter`
 * adapter, but the chosen default is observable through
 * `IContextResolver::globalHandle()`'s mode field and may be overridden
 * by test or future-provider implementations). The resolver does not
 * validate the screen id itself — that is the daemon's responsibility
 * upstream.
 */
class PHOSPHORCONTEXTRESOLVER_EXPORT IModeProvider
{
public:
    virtual ~IModeProvider() = default;

    /// Resolve @p screenId to a Mode. Implementations must return a
    /// deterministic value for any input — never an undefined-behaviour
    /// path. Concrete implementations document their own fallback:
    /// the daemon's `DaemonScreenModeAdapter` (contextresolverwiring.cpp)
    /// returns `AssignmentEntry::Snapping` when its router is null; when
    /// the router IS wired, the result for an empty / unknown screenId
    /// flows through `ScreenModeRouter::modeFor`, which may return any
    /// registered mode based on the cascade.
    /// The chosen value is observable through
    /// `IContextResolver::globalHandle()`'s mode field, so an adapter
    /// override (e.g. in tests) propagates transparently.
    virtual PhosphorZones::AssignmentEntry::Mode modeFor(const QString& screenId) const = 0;
};

/**
 * @brief Disable / lock cascade adapter.
 *
 * Exposes the four gate primitives the resolver composes into
 * `disabledReason(handle)` / `isLocked(handle)`. The daemon's
 * `IZoneVisualizationSettings` already implements three of the four
 * (`isMonitorDisabled` / `isDesktopDisabled` / `isActivityDisabled`); the
 * fourth (`isContextLocked`) is owned by the broader `ISettings` and
 * historically takes a pre-composed `screenIdOrName` lock-key string
 * (`Utils::contextLockKey(mode, screenId)`). Adapter implementations are
 * expected to do that composition internally so the resolver's public
 * API stays purely typed in `(Mode, screenId, desktop, activity)`.
 *
 * The cascade is **read-only**. Toggling a disable list or a lock still
 * goes through the original settings writer surface; the resolver does
 * not republish a mutation API here.
 */
class PHOSPHORCONTEXTRESOLVER_EXPORT IContextGateSource
{
public:
    virtual ~IContextGateSource() = default;

    /// Highest-priority "is this screen disabled regardless of (desktop,
    /// activity)?" predicate. Implementations route to
    /// `IZoneVisualizationSettings::isMonitorDisabled` (Mode-scoped).
    virtual bool isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId) const = 0;

    /// Per-desktop disable predicate. Implementations route to
    /// `IZoneVisualizationSettings::isDesktopDisabled` — desktop value `0`
    /// in this codebase means "pinned across all desktops" so a zero
    /// desktop must not match a per-desktop disable entry.
    virtual bool isDesktopDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId,
                                   int desktop) const = 0;

    /// Per-activity disable predicate. Implementations route to
    /// `IZoneVisualizationSettings::isActivityDisabled` — an empty
    /// activity string means "no activity manager is wired" and must
    /// short-circuit to `false` rather than match an empty entry in the
    /// disable list (which would mis-fire on headless test runs).
    virtual bool isActivityDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId,
                                    const QString& activity) const = 0;

    /// Per-context lock predicate — equivalent to `ISettings::isContextLocked`
    /// after the Mode has been folded into the screen-key via
    /// `Utils::contextLockKey`. The adapter performs that composition;
    /// the resolver passes typed `(mode, screenId, desktop, activity)`.
    virtual bool isContextLocked(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId, int desktop,
                                 const QString& activity) const = 0;
};

} // namespace PhosphorContext
