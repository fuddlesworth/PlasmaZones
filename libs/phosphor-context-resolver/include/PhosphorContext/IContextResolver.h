// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "ContextHandle.h"
#include "DisabledReason.h"
#include "phosphorcontextresolver_export.h"

#include <QString>

namespace PhosphorContext {

/**
 * @brief The single resolver façade every Phosphor consumer goes through
 *        for "what does this screen look like RIGHT NOW under the disable /
 *        lock / mode cascade?"
 *
 * Before this interface, every adaptor, daemon handler, and effect-side
 * consumer hand-stitched the same chain:
 *
 * @code
 *   const auto mode = m_screenModeRouter->modeFor(screenId);
 *   const int desktop = m_layoutManager->currentVirtualDesktop();
 *   const QString activity = m_layoutManager->currentActivity();
 *   if (isContextDisabled(m_settings.get(), mode, screenId, desktop, activity))
 *       return;
 *   if (m_settings->isContextLocked(
 *           Utils::contextLockKey(static_cast<int>(mode), screenId),
 *           desktop, activity))
 *       return;
 * @endcode
 *
 * Many such chains existed across the daemon's navigation / start / OSD
 * paths and the three D-Bus adaptors (SnapAdaptor, WindowDragAdaptor,
 * WindowTrackingAdaptor). Each one had to be updated whenever the
 * disable cascade changed shape, when a new mode was added, or when
 * the lock key composition was tweaked. The cascade shape was
 * duplicated; the snapshot point was inconsistent (handlers could
 * read desktop N at line 5 and act on desktop N+1 at line 12 if the
 * user virtual-desktop-switched mid-handler). OverlayService is the
 * remaining consumer still on the legacy inline cascade — see
 * `src/daemon/daemon.h::contextResolver()` for the migration status.
 * The KWin effect does not have a cascade to migrate; effect-side
 * paint state has no disable/lock gate.
 *
 * `IContextResolver` collapses the chain into:
 *
 * @code
 *   const auto ctx = m_contextResolver->handleFor(screenId);
 *   if (m_contextResolver->isGated(ctx))
 *       return;
 * @endcode
 *
 * The handle freezes desktop / activity / mode at one moment; the gate
 * call walks the same cascade as before but through one well-tested code
 * path. Adding a new override axis (a future "per-window-class disable",
 * a per-application override, a per-virtual-desktop layer of locks) gains
 * one method on this interface and one implementation in the concrete
 * resolver — every consumer site stays untouched as long as `isGated(ctx)`
 * keeps its current meaning.
 *
 * @section ownership Ownership and lifetime
 *
 * The resolver does NOT own its inputs. Construct it after the daemon's
 * VirtualDesktopManager / ActivityManager / ScreenModeRouter / ISettings
 * exist, and tear it down BEFORE any of them. The recommended wiring is a
 * `std::unique_ptr<PhosphorContext::ContextResolver>` data-member on
 * Daemon, constructed in `Daemon::init()` after the routers and
 * destroyed during `Daemon::~Daemon()` before the routers.
 *
 * @section threading Threading
 *
 * The resolver is GUI-thread only. The bound `IWorkspaceState` and
 * `IModeProvider` implementations cross into QObject-based daemon services
 * that live on the GUI thread and have no documented cross-thread API.
 * Calling the resolver from any other thread is undefined behaviour.
 */
class PHOSPHORCONTEXTRESOLVER_EXPORT IContextResolver
{
public:
    virtual ~IContextResolver() = default;

    // ── Snapshot construction ───────────────────────────────────────────

    /**
     * @brief Build a frozen handle targeting @p screenId.
     *
     * Resolves the bound mode provider exactly once for @p screenId and
     * captures the current virtual desktop + activity. The returned
     * handle is safe to pass into every gate / value query on this
     * resolver and to retain across a single event-loop tick — long
     * enough for a drag-drop handler to commit, short enough that the
     * user cannot virtual-desktop-switch underneath it.
     *
     * Empty @p screenId is permitted (the resulting handle has empty
     * screenId and mode falls back to the documented default per
     * IModeProvider's contract). Consumers that rely on a non-empty
     * screenId should validate before constructing the handle.
     */
    virtual ContextHandle handleFor(const QString& screenId) const = 0;

    /**
     * @brief Build a handle without a screen scope.
     *
     * Returns a handle whose `screenId` is empty. The `mode` field is
     * resolved via `IModeProvider::modeFor(QString())` — per IModeProvider's
     * documented empty-screen contract, that returns the bound provider's
     * default mode (today `Snapping` for the daemon's `ScreenModeRouter`
     * adapter, but other providers may override). Callers reading the
     * handle's mode must NOT assume `Snapping`; query the field directly.
     *
     * Intended for adaptors that want to gate-check on `(desktop,
     * activity)` only. Currently no production caller exercises this
     * path — every consumer in the daemon and the D-Bus adaptors has a
     * concrete screen at the call site and uses `handleFor(screenId)`.
     * Kept on the interface for the documented future use-case and to
     * pin the empty-screen mode-provider contract under test
     * (`globalHandleHasNoScreenButSnapshotsWorkspace`).
     */
    virtual ContextHandle globalHandle() const = 0;

    /**
     * @brief Build a handle that overrides the mode resolution.
     *
     * Specialised entry point for handlers that already know which mode
     * they want to query (e.g. `HANDLE_AUTOTILE_ONLY`-class shortcut
     * handlers that only fire in autotile mode). Skips the mode provider
     * and stamps @p mode directly. Use sparingly — preferring
     * `handleFor(screenId)` keeps the mode dispatch in one place.
     */
    virtual ContextHandle handleForMode(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode) const = 0;

    /**
     * @brief Build a handle for a PERSISTED context (not "right now").
     *
     * Specialised for `(WindowTrackingService::isPersistedContextDisabled)`-style
     * checks where the desktop/activity come from a persisted entry on
     * disk, not the live workspace state. The screen's mode is still
     * resolved through the mode provider — the disable-list mode axis
     * keys on the screen's CURRENT routing, not its routing at persist
     * time. Use only for persisted lookups; `handleFor(screenId)` remains
     * the right call for live handlers.
     */
    virtual ContextHandle handleForPersisted(const QString& screenId, int virtualDesktop,
                                             const QString& activity) const = 0;

    // ── Raw workspace-axis readers ─────────────────────────────────────
    //
    // These re-cross the bound `IWorkspaceState` on every call — they are
    // NOT cached snapshots of an earlier `handleFor()` result. Use the
    // matching field on a `ContextHandle` if consistency with a prior
    // snapshot is required (e.g. when composing a (screenId, desktop,
    // activity) tuple that must agree byte-for-byte with the disabled-
    // context check the same tick made).
    //
    // No production caller currently uses these — consumers that need a
    // live workspace value call `m_layoutManager->currentVirtualDesktop()`
    // / `m_activityManager->currentActivity()` directly. Kept on the
    // interface to expose the snapshot-vs-live distinction in the API
    // surface (so a future consumer doesn't reach into `IWorkspaceState`
    // and bypass the snapshot semantics by accident) and to support
    // testing of the raw-read path (`rawWorkspaceAccessorsMatchSnapshot`).

    /// Live read of the workspace's current virtual desktop, equivalent
    /// to calling the bound `IWorkspaceState::currentVirtualDesktop`.
    virtual int currentVirtualDesktop() const = 0;

    /// Live read of the workspace's current activity, equivalent to
    /// calling the bound `IWorkspaceState::currentActivity`.
    virtual QString currentActivity() const = 0;

    // ── Gate queries (the cascade collapse) ────────────────────────────

    /**
     * @brief The disable cascade — Monitor → Desktop → Activity, in
     *        priority order.
     *
     * Returns the highest-priority reason the context is disabled, or
     * @ref DisabledReason::NotDisabled when every leg passes. The Mode
     * axis is taken from @p handle (no separate argument); callers that
     * need to probe a mode different from the snapshot must build a
     * second handle via @ref handleForMode.
     */
    virtual DisabledReason disabledReason(const ContextHandle& handle) const = 0;

    /// Truthy convenience over @ref disabledReason. Equivalent to
    /// `disabledReason(handle) != DisabledReason::NotDisabled`.
    bool isDisabled(const ContextHandle& handle) const
    {
        return disabledReason(handle) != DisabledReason::NotDisabled;
    }

    /**
     * @brief The per-context lock check.
     *
     * Wraps `ISettings::isContextLocked` with the Mode → lock-key
     * composition the adapter performs internally. Consumers that
     * previously called `Utils::contextLockKey(static_cast<int>(mode),
     * screenId)` and fed the result into `isContextLocked` now go through
     * this method directly with a typed handle.
     */
    virtual bool isLocked(const ContextHandle& handle) const = 0;

    /**
     * @brief Composite convenience that ORs the disable and lock legs.
     *
     * Returns true iff either `isDisabled(handle)` or `isLocked(handle)`
     * trips. Provided as a one-liner for consumers that need both
     * checks at the same site — today's actual consumers gate on only
     * one or the other and call those methods directly (see
     * `Daemon::isFocusedContextGated` in src/daemon/daemon/navigation.cpp
     * for the disable-only pattern, the `layoutPickerSelected` lambda
     * in src/daemon/daemon/start.cpp for the lock-only pattern). Kept
     * on the interface for symmetry with the documented cascade collapse
     * and to support future call sites that genuinely need both gates
     * atomically.
     */
    bool isGated(const ContextHandle& handle) const
    {
        return isDisabled(handle) || isLocked(handle);
    }
};

} // namespace PhosphorContext
