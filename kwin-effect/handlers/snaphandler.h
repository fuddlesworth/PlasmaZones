// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "compositor/deferredwindowcommits.h"

#include <PhosphorCompositor/TilingState.h>
#include <PhosphorProtocol/ZoneTypes.h>

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

// Targeted using-declarations, not a namespace-wide directive: headers must
// not leak the whole PhosphorCompositor namespace into every includer.
// (Re-declaring the same alias/using in a sibling header is well-formed.)
using PhosphorCompositor::BorderState;
namespace TilingStateHelpers = PhosphorCompositor::TilingStateHelpers;

class PlasmaZonesEffect;

/// Pre-computed snap restore target for a pending app (appId → geometry + saved
/// screen). Fetched once from the daemon on ready; consumed single-shot in
/// PlasmaZonesEffect::slotWindowAdded for instant teleport (no D-Bus round-trip
/// visible flash). The screenId lets the effect tell "cached saved zone is on
/// snap-mode screen X" from "current KWin placement is autotile screen Y" — we
/// trust the saved screen, not the placement, so cross-VS / cross-monitor
/// restores work.
struct CachedSnapRestore
{
    QRect geometry;
    QString screenId;
};

/**
 * @brief Handles snapping integration for PlasmaZones.
 *
 * The snap-mode counterpart to TilingHandler. Owns the snap-side tiled
 * tracking (m_border, parallel to TilingHandler::m_border) for
 * snap-committed windows. The tracking set feeds the IsSnapped rule field;
 * per-window border appearance and title-bar (borderless) state are resolved
 * from rules and applied via the effect's DecorationManager — this handler
 * does not touch decorations or resolve appearance itself.
 * Delegates window lookups back to the effect through the m_effect back-pointer.
 *
 * Built on the shared PhosphorCompositor BorderState + TilingStateHelpers so
 * snap and autotile share one standardized tracking mechanism. The effect's
 * membership resolver (resolveSurfacePathFor) reads isTiledWindow() here
 * alongside TilingHandler's so each window resolves to the decoration surface
 * path of the mode that manages it.
 */
class SnapHandler : public QObject
{
    Q_OBJECT

public:
    explicit SnapHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    // ── Snap border-state lifecycle (mirrors TilingHandler's set) ──

    /// Record @p windowId as snap-committed on @p screenId (idempotent) and
    /// (re)draw its border. Title-bar hiding is driven by rules.
    void markWindowSnapped(const QString& windowId, const QString& screenId);
    /// Drop @p windowId from the snap set on every screen, remove its border,
    /// and clear its zone-cache entry (the IsSnapped / Zone rule-fact source)
    /// so placement-scoped rules re-resolve immediately. Title-bar restores
    /// flow through the rule path.
    void clearWindowSnapped(const QString& windowId);
    /// Drop all snap tiled-tracking bookkeeping. Physical title-bar restores
    /// are the DecorationManager's job — teardown callers pair this with
    /// DecorationManager::restoreAll() (symmetric with
    /// TilingHandler::clearTiledTracking).
    void clearSnapTracking();
    /// Drop snap border/title-bar tracking for a window being destroyed. Pure
    /// bookkeeping — no setNoBorder/removeWindowDecoration, the window is going away.
    void onWindowClosed(const QString& windowId);

    // ── Snapping focus-follows-mouse (mirrors TilingHandler) ──
    void setFocusFollowsMouse(bool enabled);
    /// Activate the topmost snapped window under the cursor when FFM is on.
    /// No-op unless the window directly under the cursor is snapped (occlusion
    /// guard), so a dialog/popup floating over a snapped window keeps focus.
    /// Called from PlasmaZonesEffect::slotMouseChanged when not dragging.
    void handleCursorMoved(const QPointF& pos, const QString& screenId);

    // ── Snap restore cache (instant snap-restore-on-open latency cache) ──
    // Populated from the daemon's pending restores on daemon-ready; consumed
    // single-shot in PlasmaZonesEffect::slotWindowAdded for flash-free teleport.
    void clearRestoreCache()
    {
        m_restoreCache.clear();
    }
    void cacheRestore(const QString& appId, const CachedSnapRestore& entry)
    {
        m_restoreCache.insert(appId, entry);
    }
    bool restoreCacheEmpty() const
    {
        return m_restoreCache.isEmpty();
    }
    int restoreCacheSize() const
    {
        return m_restoreCache.size();
    }
    void invalidateRestore(const QString& appId)
    {
        m_restoreCache.remove(appId);
    }
    /// Look up and REMOVE the restore entry for @p appId (single-shot consume).
    /// Returns nullopt if none. The entry is erased on lookup regardless of
    /// whether the caller ends up applying it.
    std::optional<CachedSnapRestore> takeRestore(const QString& appId)
    {
        auto it = m_restoreCache.find(appId);
        if (it == m_restoreCache.end()) {
            return std::nullopt;
        }
        const CachedSnapRestore entry = it.value();
        m_restoreCache.erase(it);
        return entry;
    }

    // ── Snap restore-on-open orchestration ──
    /// Ask the daemon whether @p window has a saved zone and apply it (async) —
    /// the async counterpart of the instant restore-cache teleport.
    /// onComplete receives snapApplied: true when the daemon resolved a zone
    /// and the geometry was applied. Callers that go on to notify autotile
    /// must thread !snapApplied into knownFreeFloating — a zone-placed
    /// window's live frame is the zone rect, and reporting it as a known free
    /// frame persists the zone rect as the float-back geometry.
    /// releaseSuppressionOnMiss: when the daemon resolves no zone, release the
    /// window's first-frame suppression. Pass false when something else will
    /// still reposition it on a miss (the autotile-screen path tiles it via
    /// onComplete) — there the suppression must hold through that reposition.
    /// isOpenPath: whether this resolve is driven by a window OPEN (session
    /// restore, bring-up re-announce, deferred-routing flush) as opposed to
    /// the unminimize-orphan and pending-restores-sweep drivers. Threaded to
    /// the daemon, which gates the cross-screen tile reclaim on it — an
    /// unminimize must never teleport a window across monitors.
    void callResolveWindowRestore(KWin::EffectWindow* window,
                                  std::function<void(bool snapApplied)> onComplete = nullptr,
                                  bool releaseSuppressionOnMiss = true, bool isOpenPath = true);
    /// Store a window's pre-snap (free-float) geometry with the daemon before a
    /// snap commit, so a later float toggle restores the original position.
    void ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId,
                                     const QRectF& preCapturedGeometry = QRectF());
    /// Send the one-way cancelSnap D-Bus call (drag cancelled by Escape or an
    /// external event). The daemon discards the in-flight snap.
    void callCancelSnap();

    // ── Snap minimize-float (mirrors TilingHandler's minimize→float machine) ──
    /// Drive the snap-mode minimize→float state machine: on a snapping-mode
    /// screen, float a window when it minimizes and unfloat it when it
    /// unminimizes. The autotile-screen gate is DIRECTION-ASYMMETRIC by
    /// design: a MINIMIZE on an autotile screen bails outright (that screen's
    /// machine owns the float), but an UNMINIMIZE on an autotile screen is
    /// NOT gated at entry — the adoption/transfer paths below must still run
    /// for a float this handler owns from before the screen's mode flipped.
    /// When
    /// unminimizing a window this session minimize-floated, it also retries
    /// snap restore if the daemon tracks the window as neither snapped nor
    /// floating — every restore pass (daemon-ready, pendingRestoresAvailable)
    /// skips minimized windows, so a window minimized across a daemon restart
    /// would otherwise stay stranded at whatever geometry KWin unminimizes it
    /// to. In a normal minimize cycle the daemon still holds the window in its
    /// floating set (minimize-float unsnaps), so the neither-set check keeps
    /// the net inert and the plain unfloat re-snaps as before. Called from
    /// PlasmaZonesEffect::slotWindowMinimizedChanged after the shared minimize
    /// shader event.
    /// The normal same-mode unfloat is deferred by an Effect::animationTime-
    /// scaled grace so the re-snap's geometry apply cannot land mid-flight and
    /// cancel KWin's own unminimize animation (discussion #816). A float
    /// adopted across a mode boundary dispatches immediately AND withholds the
    /// first frames via restore suppression: its current geometry belongs to
    /// the other mode, and the resnap that corrects it only lands after a
    /// D-Bus round trip — without the suppression KWin would play the restore
    /// against the stale frame and then hop. See m_pendingUnminimizeUnfloat.
    void handleMinimizeChanged(KWin::EffectWindow* window, const QString& windowId, const QString& screenId,
                               bool minimized);
    /// Cross-mode transfer entry point over handleMinimizeChanged's
    /// unminimize path (mirror of TilingHandler::offerMinimizeEdge).
    /// Returns false when the entry gate would refuse the window (the screen
    /// moved back under autotile) WITHOUT forwarding; true after forwarding.
    /// TilingHandler's became-snap hand-offs must use this so a refused
    /// transfer re-arms on the sender instead of stranding the suspension.
    bool offerMinimizeEdge(KWin::EffectWindow* window, const QString& windowId, const QString& screenId);
    void retryVisibleMinimizeFloats();

    /// Whether snap owns a temporary minimize-float for @p windowId.
    bool isMinimizeFloated(const QString& windowId) const
    {
        return m_minimizeFloatedWindows.contains(windowId) || m_unfloatInFlight.contains(windowId);
    }

    /// Take ownership of a minimize-float relinquished by the autotile
    /// handler (cross-screen move of a still-minimized window onto a
    /// snap-mode screen): the unminimize edge on that screen must find an
    /// owner or the window stays floating until the next mode toggle.
    void adoptMinimizeFloated(const QString& windowId)
    {
        m_minimizeFloatedWindows.insert(windowId);
    }

    /// Retry-budget hand-off — see TilingHandler's twin for rationale.
    int unfloatRetryBudgetUsed(const QString& windowId) const
    {
        return m_unfloatRetryAttempts.value(windowId);
    }
    void seedUnfloatRetryBudget(const QString& windowId, int attemptsUsed)
    {
        if (attemptsUsed > m_unfloatRetryAttempts.value(windowId)) {
            m_unfloatRetryAttempts.insert(windowId, attemptsUsed);
        }
    }

    /// Drop @p windowId from the minimize-float set and cancel either deferred
    /// edge. Returns true if it was present. Used by close cleanup,
    /// authoritative visible unfloat, and cross-mode adoption.
    bool removeMinimizeFloated(const QString& windowId)
    {
        cancelPendingMinimizeFloat(windowId);
        cancelPendingUnminimizeUnfloat(windowId);
        m_unfloatRetryAttempts.remove(windowId);
        const bool owned = m_minimizeFloatedWindows.remove(windowId);
        return m_unfloatInFlight.remove(windowId) > 0 || owned;
    }

    bool hasPendingUnminimizeUnfloat(const QString& windowId) const
    {
        return m_pendingUnminimizeUnfloat.contains(windowId);
    }

    bool hasUnfloatInFlight(const QString& windowId) const
    {
        return m_unfloatInFlight.contains(windowId);
    }

    /// Cancel a pending deferred unminimize→unfloat commit. No-op if no timer
    /// is pending for the window. Called from the minimize edge (a re-minimize
    /// during the grace must leave the window minimize-floated) and from
    /// removeMinimizeFloated (window closed, or an authoritative external
    /// unfloat via the daemon's windowFloatingChanged echo).
    void cancelPendingUnminimizeUnfloat(const QString& windowId)
    {
        m_pendingUnminimizeUnfloat.cancel(windowId);
    }

    // ── Tiled-membership accessor — delegates to shared TilingStateHelpers ──
    // The snapped-window set feeds the IsSnapped rule field; per-window border
    // appearance and title-bar hiding are resolved from rules, not this state.
    bool isTiledWindow(const QString& windowId) const
    {
        return TilingStateHelpers::isTiledWindow(m_border, windowId);
    }

public Q_SLOTS:
    // Snap D-Bus signal handlers, connected in
    // PlasmaZonesEffect::connectNavigationSignals (receiver = the SnapHandler
    // instance). Each delegates effect-level work back through m_effect.
    void slotSnapAllWindowsRequested(const QString& screenId);
    void slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId, int x, int y, int width,
                                               int height);
    void slotPendingRestoresAvailable();
    /// Re-drive the snap restore for windows parked by armDesktopArrivalRestore
    /// that the just-arrived desktop has brought into view. Connected to KWin's
    /// desktopChanged.
    ///
    /// The snapping counterpart of the autotile desktop-return catch-scan in
    /// TilingHandler::slotScreensChanged: snapping has no membership set the
    /// effect can consult, so where the catch-scan can safely re-announce
    /// anything it does not already track, this arm carries its own list.
    void slotDesktopChangedRestoreArrivals();
    void slotSnapAssistReady(const QString& windowId, const QString& releaseScreenId,
                             const PhosphorProtocol::EmptyZoneList& emptyZones);

public:
    /// Park @p windowId for a snap restore once the desktop it was just moved to
    /// comes into view. Called from PlasmaZonesEffect::slotWindowDesktopMoveRequested
    /// after the move, and ONLY when the target desktop is not the one on screen —
    /// a window moved onto the visible desktop needs no deferral.
    void armDesktopArrivalRestore(const QString& windowId);

    /// Drop @p windowId from the desktop-arrival park (window closed, or the
    /// daemon placed it by another route).
    void cancelDesktopArrivalRestore(const QString& windowId)
    {
        m_awaitingDesktopArrivalRestore.remove(windowId);
    }

    /// Drain ONE window's desktop-arrival park, if it has one and has arrived.
    /// Returns true when the restore was actually dispatched, so a caller that
    /// has its own placement to run for the same window can stand down rather
    /// than racing this one — two placement answers for a single window resolve
    /// in D-Bus reply order, which is nobody's intent.
    bool drainDesktopArrivalFor(const QString& windowId, KWin::EffectWindow* window);

private:
    void cancelPendingMinimizeFloat(const QString& windowId)
    {
        m_pendingMinimizeFloat.cancel(windowId);
    }

    /// Deferred-commit body of the unminimize→unfloat edge: the restore-net
    /// queries (dispatched before the unfloat enters the same D-Bus send
    /// queue, so the daemon answers against pre-unfloat state) plus the
    /// unfloat itself. Called from the grace timer after revalidation, or
    /// immediately when cross-mode adoption must replace the other mode's
    /// geometry at the unminimize edge.
    void commitUnminimizeUnfloat(KWin::EffectWindow* window, const QString& windowId, const QString& screenId);
    void scheduleUnminimizeUnfloatRetry(const QString& windowId);

    PlasmaZonesEffect* m_effect;
    // Snapping focus-follows-mouse (Snapping.Behavior.FocusFollowsMouse). When
    // on, moving the cursor over a snapped window activates it. Mirrors autotile
    // FFM but scoped to the snap BorderState tiled set instead of autotile screens.
    bool m_focusFollowsMouse = false;
    // Snapping's own managed-window border state, parallel to
    // TilingHandler::m_border. Populated at snap commit, cleared on
    // float / unsnap / close.
    //
    // KEYING WARNING: this set is per-SCREEN, but the daemon's snap membership
    // is per-(screen, desktop, activity). It stays correct ONLY because every
    // mutation is per-window (mark/clear at commit funnels) — there is no
    // batch diff. Never add an "untile whatever is absent from this batch"
    // cleanup here without gating on isOnCurrentDesktop AND
    // isOnCurrentActivity: a window absent from the current context's batch
    // is usually snapped in a SIBLING desktop's or activity's state, and
    // clearing it flips the tiled appearance scope and restores its title
    // bar mid-switch (the autotile #808 bug; see the gated diff in
    // tilinghandler/tiling.cpp onComplete).
    BorderState m_border;
    // Single-shot instant-restore latency cache (appId → saved zone geometry +
    // screen), populated on daemon-ready and consumed on window-open.
    QHash<QString, CachedSnapRestore> m_restoreCache;
    // Snap-mode windows floated because they were minimized (mirrors
    // TilingHandler::m_minimizeFloatedWindows). Removed on unminimize / close.
    // Deliberately NOT cleared on daemon restart, unlike the autotile twin
    // (TilingHandler::clearPerSessionDaemonState): the restore net in
    // commitUnminimizeUnfloat is snap's restart-recovery path, and it only
    // fires for windows still in this set — clearing on daemon-ready would
    // strand exactly the windows the net exists to recover.
    QSet<QString> m_minimizeFloatedWindows;
    QHash<QString, quint64> m_unfloatInFlight;
    quint64 m_unfloatRequestGeneration = 0;
    QHash<QString, int> m_unfloatRetryAttempts;
    // Snap membership retained only as minimize provenance while daemon-owned
    // visual placement state is unavailable.
    QSet<QString> m_restartSnapCandidates;
    // Windows the daemon relocated to a virtual desktop that was NOT in view,
    // awaiting the desktop switch that brings them into sight so their snap
    // restore can run in the context they actually landed in.
    //
    // Deliberately a SET OF EXACTLY THOSE WINDOWS rather than a sweep over
    // everything on the arriving desktop. The obvious implementation — reuse
    // slotPendingRestoresAvailable's shape, skipping the daemon's tracked set —
    // is wrong here: WindowTrackingService::snappedWindows() lists only windows
    // assigned to a ZONE, so every FLOATED window looks untracked to it. That is
    // harmless for a once-per-daemon-session retry net, but on a signal that
    // fires every time the user changes desktop it would re-drive the float
    // restore continually and drag each floated window back to its recorded
    // position, undoing any move the user had made since.
    QSet<QString> m_awaitingDesktopArrivalRestore;
    // Pending debounced minimize→float commits. Shares the compositor's
    // spurious minimize-pair window with the shader and autotile paths.
    DeferredWindowCommits m_pendingMinimizeFloat{this};
    // Pending deferred unminimize→unfloat commits, keyed by windowId — the
    // snap-mode mirror of TilingHandler::m_pendingUnminimizeUnfloat, for the
    // same reason: the unfloat re-snaps the window (the daemon applies its
    // zone geometry), and a moveResize landing mid-flight cancels KWin's own
    // unminimize animation (discussion #816). Deferred by an
    // Effect::animationTime-scaled grace and revalidated at fire time; a
    // re-minimize during the grace cancels it, and an authoritative external
    // unfloat (the daemon's windowFloatingChanged echo) cancels it via
    // removeMinimizeFloated.
    DeferredWindowCommits m_pendingUnminimizeUnfloat{this};
};

} // namespace PlasmaZones
