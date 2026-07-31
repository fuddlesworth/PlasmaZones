// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "compositor/deferredwindowcommits.h"

#include <PhosphorCompositor/TilingState.h>
#include <PhosphorProtocol/AutotileMarshalling.h>

#include <QHash>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

class QAction;
class QTimer;

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

// Targeted using-declarations, not a namespace-wide directive: headers must
// not leak the whole PhosphorCompositor namespace into every includer.
using PhosphorCompositor::BorderState;
namespace TilingStateHelpers = PhosphorCompositor::TilingStateHelpers;

class PlasmaZonesEffect;

/**
 * @brief Handles tiling-family engine integration (autotile + scrolling) for PlasmaZones
 *
 * Manages the autotile D-Bus interface, screen tracking, window tiling,
 * monocle mode, tiled-tracking for border rendering, and pre-autotile
 * geometry preservation. Title-bar (borderless) state is owned by the
 * effect's DecorationManager and driven by rules — this handler does
 * not touch decorations.
 *
 * Delegates window lookups, geometry application, and animation back to the effect.
 */
class TilingHandler : public QObject
{
    Q_OBJECT

public:
    explicit TilingHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    // ═══════════════════════════════════════════════════════════════════
    // Integration points (called by PlasmaZonesEffect)
    // ═══════════════════════════════════════════════════════════════════

    /// Returns true when the window was on an autotile screen and a
    /// `windowOpened` D-Bus call was issued (i.e. the daemon is expected
    /// to tile it, producing a moveResize). Returns false when the call
    /// was filtered out locally (ineligible window, non-autotile screen,
    /// already-notified) — callers that depend on a follow-up tile must
    /// not wait for one in that case (used by the first-frame open
    /// suppression path to release suppression immediately on a no-op).
    ///
    /// @p knownFreeFloating governs the pre-autotile geometry capture: the
    /// genuine window-opened/spawn path passes `true` (the frame is KWin's
    /// spawn geometry and the FloatingCache is not yet populated, so the
    /// isWindowFloating() guard must be bypassed). RE-ADD callers (a window
    /// already known to the engine being re-announced — cross-screen
    /// transfer, desktop-return catch-scan) pass `false`: the frame may be
    /// a tiled zone rect, so the floating guard MUST run and reject it,
    /// otherwise the tiled rect would be persisted as the window's
    /// free-floating geometry and clobber the daemon's real float-back.
    bool notifyWindowAdded(KWin::EffectWindow* w, bool knownFreeFloating);

    /**
     * @brief Batch-notify windows added to engine-managed screens
     *
     * Filters windows the same way as notifyWindowAdded, then sends one
     * windowsOpenedBatch D-Bus call instead of per-window windowOpened calls.
     * Used on daemon startup/restart and autotile toggle-on.
     *
     * @param windows List of candidate windows to process
     * @param screenFilter If non-empty, only process windows on these screens
     * @param resetNotified If true, remove windowId from m_notifiedWindows before processing
     *        (for re-announce on daemon restart / screen change)
     * @param enteringAutotile True when the screen has just changed from snap
     *        mode. Already-minimized windows then need an immediate first
     *        autotile placement when they become visible.
     */
    void notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows, const QSet<QString>& screenFilter = {},
                                 bool resetNotified = false, bool enteringAutotile = false);

    /// Remove a window from this handler's autotile tracking and notify the daemon.
    /// INTENT NOTE: an earlier m_pendingCloses guard (deduping a close racing
    /// its own re-announce) was deliberately deleted — window ids are
    /// uuid-based, so a closed id can never be re-announced, and the
    /// regression the guard defended against is unreachable. Do not
    /// reintroduce it as "missing hardening".
    void onWindowClosed(const QString& windowId, const QString& screenId);
    /// Tear down all effect-side autotile tracking for @p windowId (shared +
    /// KWin-specific state, incl. the pending cross-screen-restore connection)
    /// WITHOUT notifying the daemon. Shared by onWindowClosed (which adds the
    /// daemon windowClosed call) and the cross-mode-move marker path (where the
    /// daemon already relinquished the window via handoffRelease).
    void cleanupAutotileTracking(const QString& windowId, const QString& screenId);
    /// Drop @p windowId from the minimize-float set and cancel EITHER
    /// deferred edge — the minimize debounce and the unminimize commit —
    /// mirroring SnapHandler::removeMinimizeFloated, plus the untiled-marker
    /// drop (snap has no untiled-marker counterpart). Returns true if
    /// the window was tracked. Callers: close cleanup, the effect's
    /// authoritative visible unfloat (slotWindowFloatingChanged, including
    /// its dual-hold repair), and the cross-mode adoption hops (snap's
    /// adoption paths and countermand hand-offs).
    bool removeMinimizeFloated(const QString& windowId)
    {
        cancelPendingMinimizeFloat(windowId);
        cancelPendingUnminimizeUnfloat(windowId);
        m_untiledMinimizeFloats.remove(windowId);
        m_unfloatRetryAttempts.remove(windowId);
        const bool owned = m_minimizeFloatedWindows.remove(windowId);
        return m_unfloatInFlight.remove(windowId) > 0 || owned;
    }

    /// Whether this handler currently owns @p windowId's minimize-float
    /// marker (mirrors SnapHandler::isMinimizeFloated). Includes the
    /// in-flight unfloat interval: dispatchUnminimizeUnfloat moves the id
    /// from the marker set into m_unfloatInFlight, and during that window
    /// the handler still owns the float — the dual-hold repair and the
    /// capture guards must not treat it as unowned.
    bool isMinimizeFloated(const QString& windowId) const
    {
        return m_minimizeFloatedWindows.contains(windowId) || m_unfloatInFlight.contains(windowId);
    }

    /// Take ownership of a minimize-float relinquished by the snap handler
    /// (cross-mode countermand / hand-off on a screen autotile now owns).
    /// @p untiled marks the window's rect as belonging to the prior mode so
    /// its unminimize commits immediately instead of through the animation
    /// grace.
    void adoptMinimizeFloated(const QString& windowId, bool untiled)
    {
        m_minimizeFloatedWindows.insert(windowId);
        if (untiled) {
            m_untiledMinimizeFloats.insert(windowId);
        }
    }

    /// Retry-budget hand-off for cross-mode adoptions: the budget is a
    /// per-WINDOW cap on daemon unfloat attempts, and it must survive the
    /// ownership hop or a persistently-refused unfloat ping-ponging between
    /// handlers resets to a fresh budget on every bounce.
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
    bool hasPendingUnminimizeUnfloat(const QString& windowId) const
    {
        return m_pendingUnminimizeUnfloat.contains(windowId);
    }
    bool hasUnfloatInFlight(const QString& windowId) const
    {
        return m_unfloatInFlight.contains(windowId);
    }
    /// Drop a destroyed window's desktop-move geometry stash. Separate from
    /// onWindowClosed because the desktop-MOVE path calls onWindowClosed
    /// right after creating the stash (the window must look "closed" to this
    /// desktop's tiling) — only genuine destruction may clear it.
    void clearDesktopMoveStash(const QString& windowId)
    {
        m_savedPreTileForDesktopMove.remove(windowId);
    }
    bool isScreenQueryPending() const
    {
        return m_initialScreenQueryPending;
    }
    void deferWindowRouting(KWin::EffectWindow* window, bool canSnapRestore);
    void onDaemonReady();

    /**
     * @brief Handle autotile drag-to-float: restore border and pre-autotile size
     *
     * Called synchronously at drag-stop time. Restores the KWin border (title bar),
     * clears tiling state, and defers a size-only restore to the next event loop
     * tick (after KWin finishes the interactive move).
     *
     * When @p immediate is true (drag-start path), the size restore is applied
     * synchronously with allowDuringDrag=true so the user sees the window return
     * to its free-floating size as soon as they start dragging a tile — matching
     * snap-mode behavior. When false (drag-stop path), the restore is deferred
     * via QTimer::singleShot so it runs after KWin has finished the interactive
     * move and the window's frame geometry reflects the actual drop position.
     *
     * @param w The window being floated (may be null for cross-screen drops)
     * @param windowId Stable window identifier
     * @param immediate Apply size restore synchronously during the interactive move
     */
    void handleDragToFloat(KWin::EffectWindow* w, const QString& windowId, bool immediate = false);
    void savePreTileForDesktopMove(const QString& windowId);
    void handleWindowOutputChanged(KWin::EffectWindow* w);

    // D-Bus signal connections and settings
    void connectSignals();
    void loadSettings();

    // Cleanup: unmaximize all monocle-maximized windows (called on daemon loss / effect teardown)
    void restoreAllMonocleMaximized();

    /// Cleanup: drop all autotile tiled-tracking bookkeeping. Physical
    /// title-bar restores are the DecorationManager's job — teardown callers
    /// pair this with DecorationManager::restoreAll().
    void clearTiledTracking();

    // Focus follows mouse: focus the managed window under the cursor.
    // Autotile and scrolling screens carry independent flags (per-mode
    // settings); handleCursorMoved routes per screen.
    void setFocusFollowsMouse(bool enabled);
    void setScrollingFocusFollowsMouse(bool enabled);
    void handleCursorMoved(const QPointF& pos, const QString& screenId);

    // Meta+wheel column focus configuration. Disabling genuinely releases
    // the axis chords back to the compositor (updateScrollWheelShortcuts'
    // want predicate); inverting flips the wheel direction.
    void setWheelFocusEnabled(bool enabled);
    void setWheelFocusInverted(bool inverted);

    // Screen accessors (for gating drag/snap/overlay behavior per-screen)
    bool isManagedScreen(const QString& screenId) const;

    /// The engine-authoritative screen for a window the scrolling engine is
    /// ACTIVELY TILING, or empty. Parked scroll frames sit inside a
    /// neighbouring output's geometry by design, so position-derived screen
    /// resolution must yield to this for strip-placed windows. Gated on
    /// tiled membership: a FLOATED window on a scrolling screen is never
    /// parked — its frame is its real position — and answering for it would
    /// pin every consumer (drag drop, rule Mode stamp, minimize routing) to
    /// the screen it floated away from.
    /// Second half of the invariant, folded in (not left to call sites):
    /// the answer only holds while the tracked screen's physical output is
    /// still CONNECTED — on unplug/DPMS-off KWin reassigns the window and
    /// the daemon's scrollingScreensChanged lags, and until it lands every
    /// id-keyed consumer would keep resolving to the dead output.
    QString scrollTrackedScreenFor(const QString& windowId) const;

    /// Cheap gate for callers that want to skip scroll-specific work in a
    /// session with no scrolling screens at all.
    bool hasScrollingScreens() const
    {
        return !m_scrollingScreens.isEmpty();
    }

    /// Daemon-loss teardown: drop the dead session's scrolling snapshot.
    /// Deliberately NOT the live chokepoint — setScrollingScreens schedules
    /// a border sweep, which on this path would re-create rule-matched
    /// decorations one event-loop turn AFTER the handler's
    /// clearAllDecorations, undoing the teardown. The generation bump still
    /// voids in-flight property replies.
    ///
    /// CALL-SITE CONTRACT: the ONE sanctioned caller is the effect's
    /// serviceUnregistered teardown (lifecycle_wiring_daemon.cpp), which
    /// runs invalidateAllRuleCaches immediately after — this clear changes
    /// the Mode discriminator, and skipping the invalidate would leave rule
    /// verdicts memoised against the dead session's stamp. A second caller
    /// must carry the same pairing or use setScrollingScreens.
    void clearScrollingScreensForTeardown()
    {
        ++m_scrollingScreensGeneration;
        m_scrollingScreens.clear();
        // Release Meta+wheel with the dead session (no repaint interplay,
        // unlike the border sweep this path deliberately skips): a consumed
        // axis chord with no daemon to serve it would just eat input.
        updateScrollWheelShortcuts();
    }

    /// True when @p screenId runs the SCROLLING engine. A subset of the
    /// engine-managed set the daemon publishes as managedScreens (which is
    /// the union of both tiling-family engines); tracked separately so
    /// window rule queries can stamp Mode "scrolling" instead of "tiling".
    ///
    /// Intersected with the union at READ time. The union and the
    /// discriminator arrive as two independent signals with no ordering
    /// guarantee, so between them m_scrollingScreens can transiently name a
    /// screen the union has already dropped; answering true there stamped
    /// Mode "scrolling" for an unmanaged screen and forwarded focusColumn to
    /// an engine that no longer owns it. Note it does NOT un-consume
    /// Meta+wheel: updateScrollWheelShortcuts keys registration on the RAW
    /// m_scrollingScreens, so KWin still swallows the chord in that window;
    /// the wheel handler simply no-ops instead of acting.
    bool isScrollingScreen(const QString& screenId) const
    {
        return m_scrollingScreens.contains(screenId) && m_managedScreens.contains(screenId);
    }

    /// The set this discriminator actually answers over.
    ///
    /// Because the answer is an INTERSECTION, it can change when EITHER input
    /// moves. setScrollingScreens invalidates the rule caches on its own
    /// writes, but m_managedScreens is assigned raw by slotScreensChanged and
    /// by loadSettings' reply, and those two arrive independently of the
    /// scrolling-screens signal. Every writer of m_managedScreens must
    /// therefore snapshot this before and compare after — otherwise a screen
    /// that enters the union AFTER being marked scrolling leaves every
    /// `Mode == "scrolling"` verdict memoised as non-matching for the session.
    QSet<QString> scrollingScreenIntersection() const
    {
        return m_scrollingScreens & m_managedScreens;
    }

    /// Check if a window is tracked by the autotile handler (in m_notifiedWindows).
    bool isTrackedWindow(const QString& windowId) const
    {
        return m_notifiedWindows.contains(windowId);
    }

    /**
     * @brief Update the notified screen ID for a tracked window.
     *
     * Called after virtual screen config changes re-resolve window screen IDs,
     * so that slotWindowFrameGeometryChanged does not compare against stale
     * screen IDs and trigger spurious cross-VS transfers.
     *
     * No-op if the window is not in m_notifiedWindowScreens.
     */
    void updateNotifiedScreen(const QString& windowId, const QString& newScreenId)
    {
        auto it = m_notifiedWindowScreens.find(windowId);
        if (it != m_notifiedWindowScreens.end()) {
            it.value() = newScreenId;
        }
    }

    // Tiled-membership accessor — delegates to shared TilingStateHelpers. The
    // membership set feeds the IsTiled rule field; per-window border appearance
    // and title-bar hiding are resolved from rules, not from this state.
    bool isTiledWindow(const QString& windowId) const
    {
        return TilingStateHelpers::isTiledWindow(m_border, windowId);
    }

    // Tiled-set mutators that re-resolve the window's rules whenever its overall
    // tiled status flips (IsTiled is a match field). Welding the invalidation to
    // the write mirrors NavigationHandler's snap/float setters, so a caller can't
    // change tiling membership and forget to re-resolve a tiled-scoped border /
    // title-bar / opacity rule. markWindowTiled only invalidates on a genuine
    // not-tiled→tiled transition (re-adding an already-tiled window is a no-op),
    // so a cross-screen transfer that first runs removeFromOtherScreens does
    // invalidate (the window is briefly not-tiled), while a same-screen re-assert
    // does not.
    void markWindowTiled(const QString& screenId, const QString& windowId);
    void clearWindowTiledAllScreens(const QString& windowId);
    void clearWindowTiledOnScreen(const QString& screenId, const QString& windowId);

    // Set a window to re-activate after the next autotile raise loop completes.
    // Used by slotDaemonReady() to preserve focus of non-tiled windows (e.g. KCM).
    void setPendingReactivateWindow(KWin::EffectWindow* w)
    {
        m_pendingReactivateWindow = w;
    }

    /// Record a daemon-initiated cross-output move so the next outputChanged
    /// for @p windowId to @p targetScreenId updates bookkeeping only. See
    /// m_expectedOutputMove.
    ///
    /// @p sourceScreenId is the screen the daemon moved the window off. It is
    /// authoritative and must be used whenever it is non-empty: a daemon arm
    /// site that runs after the handoff has already pushed the destination's
    /// tiles, so by now m_notifiedWindowScreens names the DESTINATION.
    ///
    /// An empty source falls back to the notified screen. That is correct for
    /// the arm sites that fire ahead of any placement work, and it is what an
    /// older daemon that does not put the source on the wire produces — in
    /// which case the fallback is blind for a post-handoff arm exactly as
    /// described above, and stillTiledOnSource degrades to always-false.
    void markExpectedOutputMove(const QString& windowId, const QString& targetScreenId, const QString& sourceScreenId)
    {
        const QString source = sourceScreenId.isEmpty() ? m_notifiedWindowScreens.value(windowId) : sourceScreenId;
        m_expectedOutputMove.insert(windowId, ExpectedOutputMove{targetScreenId, source});
    }

public Q_SLOTS:
    // Autotile D-Bus signal handlers
    void slotWindowsTileRequested(const PhosphorProtocol::TileRequestList& tileRequests);
    void slotFocusWindowRequested(const QString& windowId);
    void slotEnabledChanged(bool enabled);
    void slotScreensChanged(const QStringList& screenIds, bool isDesktopSwitch);
    void slotScrollingScreensChanged(const QStringList& screenIds);
    void slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);

    // Window state change handlers (connected per-window in setupWindowConnections)
    void slotWindowMinimizedChanged(KWin::EffectWindow* w);
    /// Cross-mode transfer entry point over slotWindowMinimizedChanged.
    /// Returns false when the ENTRY GATES would refuse the window
    /// (unhandleable / not on an autotile screen) WITHOUT forwarding the
    /// edge; true after forwarding. SnapHandler's became-autotile hand-offs
    /// must use this instead of the raw slot: a silently refused transfer
    /// left ownership with the sender while the edge was already spent, so
    /// the suspension stranded until the next minimize cycle.
    bool offerMinimizeEdge(KWin::EffectWindow* w);
    void slotWindowMaximizedStateChanged(KWin::EffectWindow* w, bool horizontal, bool vertical);
    void slotWindowFullScreenChanged(KWin::EffectWindow* w);
    void slotWindowFrameGeometryChanged(KWin::EffectWindow* w, const QRectF& oldGeometry);

private:
    // ═══════════════════════════════════════════════════════════════════
    // Utility methods
    // ═══════════════════════════════════════════════════════════════════

    void unmaximizeMonocleWindow(const QString& windowId);

    /**
     * @brief Shared float-state cleanup for a window being floated
     *
     * Updates the float cache, clears tiled tracking, removes the border
     * overlay, and unmaximizes monocle (title-bar restores flow through the
     * rule path). Used by the per-window D-Bus signal, batch float, and
     * drag-to-float paths.
     */
    void applyFloatCleanup(const QString& windowId);

    /**
     * @brief Check if a window is eligible for autotile notification
     *
     * Shared predicate used by both notifyWindowAdded and notifyWindowsAddedBatch
     * to keep filtering logic in sync.
     *
     * @param rejectedOnlyBecauseMinimized When non-null, set to true if the
     *        window passed every other gate and was rejected solely for being
     *        minimized — the batch announce uses this to claim the window as
     *        minimize-floated instead of silently dropping it (a daemon-side
     *        mode-transition seed would otherwise tile it).
     * @return true if the window should be notified to the autotile daemon
     */
    bool isEligibleForTilingNotify(KWin::EffectWindow* w, bool* rejectedOnlyBecauseMinimized = nullptr) const;

    /**
     * @brief Claim a window that was already minimized at batch-announce time
     *        as minimize-floated.
     *
     * A window minimized BEFORE a batch announcement never gets a
     * minimizedChanged edge for that transition. Marking it minimize-floated
     * keeps it out of the live tiling geometry until its later unminimize.
     * Mode-entry batches commit that first placement immediately, while daemon
     * re-announcements retain the normal animation grace for windows already
     * parked at an autotile rect.
     *
     * Skips windows the daemon already tracks as floating (a user float or
     * another mode's minimize-float record) — mirroring the runtime minimize
     * path, we never claim ownership of a float we did not create.
     */
    void claimAlreadyMinimizedAsFloated(KWin::EffectWindow* w, const QString& windowId,
                                        const QSet<QString>& screenFilter, bool enteringAutotile);

    /**
     * @brief Cancel a pending debounced minimize→float commit.
     *
     * No-op if no timer is pending for the window. Called from the
     * unminimize path (to coalesce spurious cycles), from
     * removeMinimizeFloated, and from cleanupAutotileTracking's per-window
     * teardown (so pending timers never fire against destroyed windows);
     * bulk teardown goes through the helper's cancelAll in
     * clearAllPendingMinimizeFloats instead.
     */
    void cancelPendingMinimizeFloat(const QString& windowId);

    /**
     * @brief Cancel a pending deferred unminimize→unfloat commit.
     *
     * No-op if no timer is pending for the window. Called from the minimize
     * path (a re-minimize during the grace must leave the window
     * minimize-floated), from cleanupAutotileTracking (window closed), and
     * from removeMinimizeFloated (authoritative external unfloat via the
     * daemon's windowFloatingChanged echo); bulk teardown goes through the
     * helper's cancelAll in clearAllPendingMinimizeFloats instead.
     */
    void cancelPendingUnminimizeUnfloat(const QString& windowId);

    /**
     * @brief Cancel every pending debounced minimize→float commit.
     *
     * Called when autotile is disabled — in-flight timers should not
     * commit floats against a now-disabled engine.
     */
    void clearAllPendingMinimizeFloats();

    /// Move active minimize ownership into the asynchronous unfloat interval.
    /// Returns whether the daemon request may be dispatched.
    bool beginUnminimizeUnfloat(const QString& windowId);
    /// Returns whether the daemon unfloat was actually dispatched. False
    /// (window not ours, or daemon unregistered) means the daemon still
    /// considers the window floating — callers must not announce it as
    /// tileable via notifyWindowAdded on that branch.
    bool dispatchUnminimizeUnfloat(const QString& windowId, const QString& screenId);
    void scheduleUnminimizeUnfloatRetry(const QString& windowId);
    QSet<QString> completeDeferredWindowRoutes();

    /**
     * @brief Save the pre-autotile free-float geometry for @p windowId.
     *
     * The caller passes the window's current frame in @p frameIn; it is corrected
     * for a maximized/fullscreen window via PlasmaZonesEffect::freeGeometryForCapture
     * (the pre-maximize restore rect) so a full-monitor frame is never stored as the
     * free-float geometry. The default safety guard skips the save when the window is
     * not currently floating — snapped/tiled windows have zone dimensions in
     * frameGeometry() and capturing them would poison the pre-tile entry. Invalid
     * input and deliberately-skipped saves are both silent no-ops (logged at debug);
     * no caller distinguishes them.
     *
     * @param w The window, used to read its maximize/fullscreen restore rect.
     * @param knownFreeFloating Bypass the isWindowFloating guard when the
     *        caller knows the frame is authoritatively a free-float rect.
     *        Use true only for a genuine window-open event. Fresh windows are
     *        not tracked in the FloatingCache yet, so isWindowFloating()
     *        returns false and would incorrectly reject the initial capture.
     */
    void saveAndRecordPreTileGeometry(const QString& windowId, const QString& screenId, KWin::EffectWindow* w,
                                      const QRectF& frameIn, bool knownFreeFloating = false);

    /**
     * @brief All-bucket pre-autotile geometry lookup.
     *
     * Returns the first VALID rect found for @p windowId across every
     * screen's bucket in m_preTileGeometries, or an invalid QRectF if
     * none holds one. Readers must scan all buckets (not just the window's
     * current screen): a VS config change can re-resolve the window's
     * screen without moving its geometry bucket. Shared by the batch-float,
     * drag-to-float, cross-monitor-snapshot, desktop-move-stash, and
     * desktop-switch restore paths.
     *
     * @param bucketScreenId Optional out — receives the screen key of the
     *        bucket the rect was found under (unchanged when not found).
     */
    QRectF findPreTileGeometry(const QString& windowId, QString* bucketScreenId = nullptr) const;

    /**
     * @brief Async daemon-side pre-tile geometry restore for a desktop-switch
     *        orphan with no local pre-autotile bucket entry.
     *
     * Fires getValidatedPreTileGeometry and applies the returned rect once
     * the reply lands, unless ANY owner took the window back during the
     * round-trip (re-notified, autotile screen, snap commit, float, user
     * move/resize, desktop switched again). Callers must only invoke this
     * for windows that were verifiably autotile-managed (tracked in
     * m_notifiedWindows at demotion time) — the daemon store is mode-shared,
     * appId-fuzzy and session-persisted, so an ungated call can teleport a
     * never-autotiled window.
     */
    void requestDaemonPreTileRestore(KWin::EffectWindow* w, const QString& windowId);

    /**
     * @brief Declared compositor min-size for @p w, rounded up to ints.
     *
     * Returns 0×0 when the window declares none. Internal windows (our own
     * overlays) are skipped entirely — KWin's InternalWindow::minSize()
     * segfaults when the backing QWindow is null (discussion #511).
     */
    static QSize declaredMinSize(KWin::EffectWindow* w);

    void reportDiscoveredMinSize(const QString& windowId, int minWidth, int minHeight);

    // ═══════════════════════════════════════════════════════════════════
    // Member variables
    // ═══════════════════════════════════════════════════════════════════

    PlasmaZonesEffect* m_effect;

    QSet<QString> m_managedScreens;
    /// Screens running the scrolling engine — a pure mode discriminator
    /// (see isScrollingScreen); all lifecycle gating keys on
    /// m_managedScreens, which carries the union.
    QSet<QString> m_scrollingScreens;
    /// Authoritative write for the scrolling discriminator. A screen that
    /// flips engine WITHIN the managed union transits no managedScreensChanged,
    /// so this re-announces the flipped screens' windows to hand them to the
    /// new engine. Pass @p announceFlipped false only for BRING-UP clears
    /// (onDaemonReady), where the tracking maps are about to be reset and
    /// loadSettings owns the re-announce — announcing there desyncs the
    /// daemon's view from the effect's until that batch lands.
    void setScrollingScreens(const QSet<QString>& newSet, bool announceFlipped = true);
    /// Meta+wheel axis shortcuts for column focus (niri's Mod+wheel).
    /// Registered while ANY screen runs the scrolling engine, unregistered
    /// (by destroying the QActions — KWin drops an axis shortcut with its
    /// action) when none does, so Meta+wheel is only consumed in sessions
    /// that can actually use it. The trigger itself re-gates on the
    /// CURSOR's screen being a scrolling screen.
    void updateScrollWheelShortcuts();
    void wheelFocusColumn(int delta);
    QList<QAction*> m_scrollWheelActions;
    /// Pause the effect's own focus-follows-mouse after an ENGINE-driven
    /// strip movement on a scrolling screen (tile batch or activation).
    /// Scrolling slides other columns under a stationary pointer, and the
    /// next incidental cursor twitch would re-focus whatever landed there,
    /// yanking focus straight off the column the user just navigated to.
    /// Cleared once the cursor moves deliberately (beyond the radius) from
    /// the position it held when the strip moved.
    void suppressFfmUntilCursorMoves();
    QPointF m_ffmSuppressAnchor;
    bool m_ffmSuppressPending = false;
    /// Bumped on every managedScreensChanged signal. loadSettings' async
    /// Properties.Get reply captures the value at dispatch and discards
    /// itself if a signal landed in between — the signal carried a newer
    /// set AND ran the full per-screen transition handling the raw reply
    /// assignment lacks.
    quint64 m_screensSignalGeneration = 0;
    quint64 m_screenQueryGeneration = 0;
    bool m_initialScreenQueryPending = false;
    QSet<QString> m_pendingFreshWindows;
    struct DeferredWindowRoute
    {
        QPointer<KWin::EffectWindow> window;
        bool canSnapRestore = false;
    };
    QHash<QString, DeferredWindowRoute> m_deferredWindowRoutes;
    /// Same stale-reply guard for the scrolling-screens property fetch.
    /// Bumped by setScrollingScreens on EVERY authoritative write (live
    /// signal, property reply, daemon-restart clear) — even an identical
    /// set voids in-flight replies dispatched earlier, since the writer is
    /// always newer than the reply. All writes to m_scrollingScreens go
    /// through setScrollingScreens so the rule-cache invalidate + border
    /// sweep on a genuine change cannot be bypassed.
    quint64 m_scrollingScreensGeneration = 0;
    /// Pre-autotile frame geometry, keyed [screenId][windowId].
    ///
    /// Ownership: this is a local cache. The daemon's unified
    /// `WindowPlacementStore` record is authoritative and survives daemon
    /// restart. The effect populates this map on
    /// the first autotile transition for a window so it can restore the
    /// frame instantly when the window leaves autotile mode (untile, mode
    /// switch, screen change) without waiting on a D-Bus round-trip.
    ///
    /// Layout: per-screen bucket mirrors `BorderState` so swap/rotate can
    /// drop a screen's records wholesale. Readers that need a window's rect
    /// regardless of which screen it was captured under (a VS config change
    /// can re-resolve the notified screen without moving the bucket) scan
    /// ALL buckets — see the desktop-switch Pass-2 scan in signals.cpp and
    /// the cross-monitor snapshot in handleWindowOutputChanged.
    QHash<QString, QHash<QString, QRectF>> m_preTileGeometries;
    QHash<QString, QStringList> m_savedAutotileStackingOrder; ///< autotile stacking order, restored on snap→autotile
    QSet<QString> m_notifiedWindows;
    QHash<QString, QString> m_notifiedWindowScreens; ///< windowId → screen ID at time of notification
    /// Daemon-initiated cross-output moves: windowId → expected destination
    /// screen. Set when the daemon emits windowOutputMoveExpected (it has
    /// already migrated its tiling state and reflowed both outputs). Consumed
    /// one-shot by handleWindowOutputChanged on the matching outputChanged so
    /// that transfer only updates bookkeeping + decoration, never re-issues
    /// windowClosed/windowOpened. A stale entry (no outputChanged ever arrives,
    /// or a different destination) is cleared on the next outputChanged for the
    /// window and on close.
    struct ExpectedOutputMove
    {
        QString targetScreenId;
        /// Screen the window left. Comes from the daemon when it knows it, else
        /// falls back to the notified screen at arm time. Read live membership
        /// against this id to tell a genuine handoff (source bucket already
        /// emptied) from a strip parking hop (still tiled on the source) — see
        /// markExpectedOutputMove.
        QString sourceScreenId;
    };
    QHash<QString, ExpectedOutputMove> m_expectedOutputMove;
    QSet<QString> m_savedNotifiedForDesktopReturn; ///< windows removed from m_notifiedWindows on desktop switch
    /// Pre-autotile geometry preserved when a window is moved to another
    /// desktop. Keyed by windowId; value holds (sourceScreenId, frameRect)
    /// so a cross-desktop + cross-screen move can detect the screen change
    /// at restore time and skip the saved geometry (the rect is in the
    /// source screen's coordinate space and would land off-target on a
    /// different monitor).
    QHash<QString, QPair<QString, QRectF>> m_savedPreTileForDesktopMove;
    bool m_inOutputChanged = false; ///< re-entrancy guard for handleWindowOutputChanged
    QHash<QString, QMetaObject::Connection>
        m_pendingCrossScreenRestore; ///< windowId → deferred size-restore connection
    QSet<QString> m_minimizeFloatedWindows;
    /// Ownership after an unfloat dispatch and before its authoritative echo.
    /// The generation rejects completions from a countermanded older request.
    /// A re-minimize countermand moves the window back to the active set.
    QHash<QString, quint64> m_unfloatInFlight;
    quint64 m_unfloatRequestGeneration = 0;
    QHash<QString, int> m_unfloatRetryAttempts;
    /// Subset of m_minimizeFloatedWindows claimed at batch-announce time
    /// (already minimized when the screen entered autotile). These windows
    /// still carry geometry from the prior mode, so their unminimize commits
    /// immediately instead of through the deferred animation grace.
    QSet<QString> m_untiledMinimizeFloats;
    // NOTE: title-bar (borderless) state is owned by the effect's
    // DecorationManager; this handler only tracks tiled membership for
    // border RENDERING via m_border.tiledWindowsByScreen.
    /// Pending debounced minimize→float commits, keyed by windowId. An entry
    /// is created when slotWindowMinimizedChanged sees minimized=true; if the
    /// matching unminimize arrives before the timer fires, the timer is
    /// cancelled and no D-Bus float is issued. This absorbs spurious
    /// minimize/unminimize cycles that KWin emits on tiled windows when
    /// plasmashell notification popups transiently change stacking.
    DeferredWindowCommits m_pendingMinimizeFloat{this};
    /// Pending deferred unminimize→unfloat commits, keyed by windowId — the
    /// restore-side mirror of m_pendingMinimizeFloat, with a different reason:
    /// the unfloat triggers a retile whose applyWindowGeometry moveResize,
    /// landing mid-flight, CANCELS KWin's own unminimize animation (magic
    /// lamp / squash; kwin's maximize script likewise self-cancels on a
    /// mid-flight resize) — the discussion #816 restore stutter. The commit
    /// is deferred past the stock animation (Effect::animationTime-scaled
    /// grace) and revalidated at fire time; a re-minimize during the grace
    /// cancels it, leaving the window minimize-floated as before.
    DeferredWindowCommits m_pendingUnminimizeUnfloat{this};
    /// Global stagger epoch, bumped ONLY on a desktop switch (slotScreensChanged)
    /// to cancel EVERY in-flight staggered apply — geometry computed for the old
    /// desktop must never land in the new one. Plain managed-set changes must
    /// not bump it: a mode toggle's own managedScreensChanged lands in the same
    /// burst as its tile batch, and a blanket bump voided that batch after its
    /// first synchronous entry (screen sat half-tiled until a manual
    /// float/unfloat). Non-desktop invalidation is per-screen: removed screens
    /// in slotScreensChanged, flipped screens in setScrollingScreens.
    uint64_t m_tileStaggerGeneration = 0;
    /// Per-screen stagger generation. A retile bumps only its own screen(s), so a
    /// newer batch for the SAME screen supersedes an earlier one while a batch for
    /// a DIFFERENT screen leaves it untouched. Without this, the destination batch
    /// of a cross-output move (emitted microseconds after the source reflow)
    /// cancelled the source's still-staggered windows via the single global
    /// generation — they never moved, leaving a hole on the source monitor.
    QHash<QString, uint64_t> m_tileStaggerGenByScreen;
    QHash<QString, QRect> m_tileTargetZones;
    QHash<QString, QRect> m_centeredWaylandZones; ///< zones where Wayland windows were last centered
    QString m_pendingAutotileFocusWindowId;
    QPointer<KWin::EffectWindow> m_pendingReactivateWindow; ///< re-activate after raise loop (daemon restart)
    QSet<QString> m_monocleMaximizedWindows;
    int m_suppressMaximizeChanged = 0;
    // ── Focus follows mouse ──
    // Per-mode pair: m_focusFollowsMouse is the autotile flag
    // (autotileFocusFollowsMouse), m_scrollingFocusFollowsMouse the
    // scrolling one. handleCursorMoved picks per screen via
    // isScrollingScreen — one shared boolean made the two settings fight
    // over the union of managed screens.
    bool m_focusFollowsMouse = false;
    bool m_scrollingFocusFollowsMouse = false;
    // ── Meta+wheel column focus ──
    bool m_wheelFocusEnabled = true;
    bool m_wheelFocusInverted = false;
    // ── Border state — uses shared BorderState from compositor-common ──
    BorderState m_border;
};

} // namespace PlasmaZones
