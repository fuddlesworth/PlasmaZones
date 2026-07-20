// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "deferredwindowcommits.h"

#include <PhosphorCompositor/AutotileState.h>
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
namespace AutotileStateHelpers = PhosphorCompositor::AutotileStateHelpers;

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
 * The snap-mode counterpart to AutotileHandler. Owns the snap-side tiled
 * tracking (m_border, parallel to AutotileHandler::m_border) for
 * snap-committed windows. The tracking set feeds the IsSnapped rule field;
 * per-window border appearance and title-bar (borderless) state are resolved
 * from rules and applied via the effect's DecorationManager — this handler
 * does not touch decorations or resolve appearance itself.
 * Delegates window lookups back to the effect through the m_effect back-pointer.
 *
 * Built on the shared PhosphorCompositor BorderState + AutotileStateHelpers so
 * snap and autotile share one standardized tracking mechanism. The effect's
 * membership resolver (resolveSurfacePathFor) reads isTiledWindow() here
 * alongside AutotileHandler's so each window resolves to the decoration surface
 * path of the mode that manages it.
 */
class SnapHandler : public QObject
{
    Q_OBJECT

public:
    explicit SnapHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    // ── Snap border-state lifecycle (mirrors AutotileHandler's set) ──

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
    /// AutotileHandler::clearTiledTracking).
    void clearSnapTracking();
    /// Drop snap border/title-bar tracking for a window being destroyed. Pure
    /// bookkeeping — no setNoBorder/removeWindowDecoration, the window is going away.
    void onWindowClosed(const QString& windowId);

    // ── Snapping focus-follows-mouse (mirrors AutotileHandler) ──
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
    /// releaseSuppressionOnMiss: when the daemon resolves no zone, release the
    /// window's first-frame suppression. Pass false when something else will
    /// still reposition it on a miss (the autotile-screen path tiles it via
    /// onComplete) — there the suppression must hold through that reposition.
    void callResolveWindowRestore(KWin::EffectWindow* window, std::function<void()> onComplete = nullptr,
                                  bool releaseSuppressionOnMiss = true);
    /// Store a window's pre-snap (free-float) geometry with the daemon before a
    /// snap commit, so a later float toggle restores the original position.
    void ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId,
                                     const QRectF& preCapturedGeometry = QRectF());
    /// Send the one-way cancelSnap D-Bus call (drag cancelled by Escape or an
    /// external event). The daemon discards the in-flight snap.
    void callCancelSnap();

    // ── Snap minimize-float (mirrors AutotileHandler's minimize→float machine) ──
    /// Drive the snap-mode minimize→float state machine: on a snapping-mode
    /// screen, float a window when it minimizes and unfloat it when it
    /// unminimizes (autotile screens run AutotileHandler's own machine). When
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
    /// The unfloat side does NOT commit synchronously: it is deferred by an
    /// Effect::animationTime-scaled grace so the re-snap's geometry apply
    /// cannot land mid-flight and cancel KWin's own unminimize animation
    /// (discussion #816); see m_pendingUnminimizeUnfloat.
    void handleMinimizeChanged(KWin::EffectWindow* window, const QString& windowId, const QString& screenId,
                               bool minimized);

    /// Drop @p windowId from the minimize-float set (window closed). Returns
    /// true if it was present. Also cancels any deferred unfloat commit so a
    /// grace timer never fires against a destroyed window.
    bool removeMinimizeFloated(const QString& windowId)
    {
        cancelPendingUnminimizeUnfloat(windowId);
        return m_minimizeFloatedWindows.remove(windowId);
    }

    /// Cancel a pending deferred unminimize→unfloat commit. No-op if no timer
    /// is pending for the window. Called from the minimize edge (a re-minimize
    /// during the grace must leave the window minimize-floated) and from
    /// removeMinimizeFloated (window closed).
    void cancelPendingUnminimizeUnfloat(const QString& windowId)
    {
        m_pendingUnminimizeUnfloat.cancel(windowId);
    }

    // ── Tiled-membership accessor — delegates to shared AutotileStateHelpers ──
    // The snapped-window set feeds the IsSnapped rule field; per-window border
    // appearance and title-bar hiding are resolved from rules, not this state.
    bool isTiledWindow(const QString& windowId) const
    {
        return AutotileStateHelpers::isTiledWindow(m_border, windowId);
    }

public Q_SLOTS:
    // Snap D-Bus signal handlers, connected in
    // PlasmaZonesEffect::connectNavigationSignals (receiver = the SnapHandler
    // instance). Each delegates effect-level work back through m_effect.
    void slotSnapAllWindowsRequested(const QString& screenId);
    void slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId, int x, int y, int width,
                                               int height);
    void slotPendingRestoresAvailable();
    void slotSnapAssistReady(const QString& windowId, const QString& releaseScreenId,
                             const PhosphorProtocol::EmptyZoneList& emptyZones);

private:
    /// Deferred-commit body of the unminimize→unfloat edge: the restore-net
    /// queries (dispatched before the unfloat enters the same D-Bus send
    /// queue, so the daemon answers against pre-unfloat state) plus the
    /// unfloat itself. Called only from the grace timer in
    /// handleMinimizeChanged after revalidation; @p window is alive,
    /// unminimized, handleable, and on a snap-mode screen.
    void commitUnminimizeUnfloat(KWin::EffectWindow* window, const QString& windowId, const QString& screenId);

    PlasmaZonesEffect* m_effect;
    // Snapping focus-follows-mouse (Snapping.Behavior.FocusFollowsMouse). When
    // on, moving the cursor over a snapped window activates it. Mirrors autotile
    // FFM but scoped to the snap BorderState tiled set instead of autotile screens.
    bool m_focusFollowsMouse = false;
    // Snapping's own managed-window border state, parallel to
    // AutotileHandler::m_border. Populated at snap commit, cleared on
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
    // autotilehandler/tiling.cpp onComplete).
    BorderState m_border;
    // Single-shot instant-restore latency cache (appId → saved zone geometry +
    // screen), populated on daemon-ready and consumed on window-open.
    QHash<QString, CachedSnapRestore> m_restoreCache;
    // Snap-mode windows floated because they were minimized (mirrors
    // AutotileHandler::m_minimizeFloatedWindows). Removed on unminimize / close.
    // Deliberately NOT cleared on daemon restart, unlike the autotile twin
    // (AutotileHandler::onDaemonReady): the restore net in
    // commitUnminimizeUnfloat is snap's restart-recovery path, and it only
    // fires for windows still in this set — clearing on daemon-ready would
    // strand exactly the windows the net exists to recover.
    QSet<QString> m_minimizeFloatedWindows;
    // Pending deferred unminimize→unfloat commits, keyed by windowId — the
    // snap-mode mirror of AutotileHandler::m_pendingUnminimizeUnfloat, for the
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
