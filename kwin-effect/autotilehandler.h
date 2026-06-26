// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorCompositor/AutotileState.h>
#include <PhosphorProtocol/AutotileMarshalling.h>

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

class QTimer;

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

// Targeted using-declarations, not a namespace-wide directive: headers must
// not leak the whole PhosphorCompositor namespace into every includer.
using PhosphorCompositor::BorderState;
namespace AutotileStateHelpers = PhosphorCompositor::AutotileStateHelpers;

class PlasmaZonesEffect;

/**
 * @brief Handles autotile integration for PlasmaZones
 *
 * Manages the autotile D-Bus interface, screen tracking, window tiling,
 * monocle mode, tiled-tracking for border rendering, and pre-autotile
 * geometry preservation. Title-bar (borderless) state is owned by the
 * effect's DecorationManager — this handler only acquires/releases its
 * per-screen Autotile ownership there.
 *
 * Delegates window lookups, geometry application, and animation back to the effect.
 */
class AutotileHandler : public QObject
{
    Q_OBJECT

public:
    explicit AutotileHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

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
    bool notifyWindowAdded(KWin::EffectWindow* w, bool knownFreeFloating = true);

    /**
     * @brief Batch-notify windows added to autotile screens
     *
     * Filters windows the same way as notifyWindowAdded, then sends one
     * windowsOpenedBatch D-Bus call instead of per-window windowOpened calls.
     * Used on daemon startup/restart and autotile toggle-on.
     *
     * @param windows List of candidate windows to process
     * @param screenFilter If non-empty, only process windows on these screens
     * @param resetNotified If true, remove windowId from m_notifiedWindows before processing
     *        (for re-announce on daemon restart / screen change)
     */
    void notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows, const QSet<QString>& screenFilter = {},
                                 bool resetNotified = false);

    /**
     * @brief Remove a window from this handler's autotile tracking.
     *
     * @param windowDestroyed True ONLY from the genuine window-destruction
     *        path (slotWindowClosed). The other callers (cross-screen
     *        transfer, desktop move, drag-bypass) pass a LIVE window: for
     *        those, recording an m_pendingCloses suppression entry would
     *        poison the window's next notifyWindowAdded — the entry exists
     *        solely to absorb the D-Bus ordering race where a real close
     *        overtakes an in-flight windowOpened.
     */
    void onWindowClosed(const QString& windowId, const QString& screenId, bool windowDestroyed = false);
    /// Tear down all effect-side autotile tracking for @p windowId (shared +
    /// KWin-specific state, incl. the pending cross-screen-restore connection)
    /// WITHOUT notifying the daemon. Shared by onWindowClosed (which adds the
    /// daemon windowClosed call) and the cross-mode-move marker path (where the
    /// daemon already relinquished the window via handoffRelease).
    void cleanupAutotileTracking(const QString& windowId, const QString& screenId);
    /// Drop a destroyed window's desktop-move geometry stash. Separate from
    /// onWindowClosed because the desktop-MOVE path calls onWindowClosed
    /// right after creating the stash (the window must look "closed" to this
    /// desktop's tiling) — only genuine destruction may clear it.
    void clearDesktopMoveStash(const QString& windowId)
    {
        m_savedPreAutotileForDesktopMove.remove(windowId);
    }
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
    void savePreAutotileForDesktopMove(const QString& windowId);
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

    // Settings update: toggle hide-title-bars with border restore on disable.
    // Returns true if the value actually changed (so the caller can skip a
    // redundant updateAllBorders() stacking-order walk on a no-op reload).
    bool updateHideTitleBarsSetting(bool enabled);

    // Focus follows mouse: focus autotile window under cursor
    void setFocusFollowsMouse(bool enabled);
    void handleCursorMoved(const QPointF& pos, const QString& screenId);

    // Screen accessors (for gating drag/snap/overlay behavior per-screen)
    bool isAutotileScreen(const QString& screenId) const;
    const QSet<QString>& autotileScreens() const
    {
        return m_autotileScreens;
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

    // Border rendering accessors — delegate to shared AutotileStateHelpers
    bool isTiledWindow(const QString& windowId) const
    {
        return AutotileStateHelpers::isTiledWindow(m_border, windowId);
    }
    /// Read-only view of the autotile border state. Carries the tiled-window
    /// set (border rendering membership + the IsTiled rule field) and the
    /// title-bar-hide flag; per-window border appearance is resolved from rules.
    const BorderState& borderState() const
    {
        return m_border;
    }

    // Set a window to re-activate after the next autotile raise loop completes.
    // Used by slotDaemonReady() to preserve focus of non-tiled windows (e.g. KCM).
    void setPendingReactivateWindow(KWin::EffectWindow* w)
    {
        m_pendingReactivateWindow = w;
    }

    /// Record a daemon-initiated cross-output move so the next outputChanged
    /// for @p windowId to @p targetScreenId updates bookkeeping only. See
    /// m_expectedOutputMove.
    void markExpectedOutputMove(const QString& windowId, const QString& targetScreenId)
    {
        m_expectedOutputMove.insert(windowId, targetScreenId);
    }

public Q_SLOTS:
    // Autotile D-Bus signal handlers
    void slotWindowsTileRequested(const PhosphorProtocol::TileRequestList& tileRequests);
    void slotFocusWindowRequested(const QString& windowId);
    void slotEnabledChanged(bool enabled);
    void slotScreensChanged(const QStringList& screenIds, bool isDesktopSwitch);
    void slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);

    // Window state change handlers (connected per-window in setupWindowConnections)
    void slotWindowMinimizedChanged(KWin::EffectWindow* w);
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
     * Updates the float cache, releases autotile's DecorationManager
     * ownership (the manager restores the title bar unless another owner
     * remains), clears tiled tracking, removes the border overlay, and
     * unmaximizes monocle. Used by both slotWindowFloatingChanged
     * (per-window D-Bus signal path) and slotWindowsTileRequested (batch float path).
     */
    void applyFloatCleanup(const QString& windowId);

    /**
     * @brief Check if a window is eligible for autotile notification
     *
     * Shared predicate used by both notifyWindowAdded and notifyWindowsAddedBatch
     * to keep filtering logic in sync.
     *
     * @return true if the window should be notified to the autotile daemon
     */
    bool isEligibleForAutotileNotify(KWin::EffectWindow* w) const;

    /**
     * @brief Cancel a pending debounced minimize→float commit.
     *
     * No-op if no timer is pending for the window. Called from the
     * unminimize path (to coalesce spurious cycles), from onWindowClosed
     * (so pending timers never fire against destroyed windows), and from
     * clearAllPendingMinimizeFloats (bulk teardown).
     */
    void cancelPendingMinimizeFloat(const QString& windowId);

    /**
     * @brief Cancel every pending debounced minimize→float commit.
     *
     * Called when autotile is disabled — in-flight timers should not
     * commit floats against a now-disabled engine.
     */
    void clearAllPendingMinimizeFloats();

    /**
     * @brief Save the pre-autotile free-float geometry for @p windowId.
     *
     * The caller passes the window's current frame. The default safety
     * guard skips the save when the window is not currently floating —
     * snapped/tiled windows have zone dimensions in frameGeometry() and
     * capturing them would poison the pre-tile entry. Invalid input and
     * deliberately-skipped saves are both silent no-ops (logged at debug);
     * no caller distinguishes them.
     *
     * @param knownFreeFloating Bypass the isWindowFloating guard when the
     *        caller knows the frame is authoritatively a free-float rect.
     *        Use true from the window-added paths (notifyWindowAdded and
     *        notifyWindowsAddedBatch) — fresh windows are NOT tracked in
     *        the FloatingCache yet, so isWindowFloating() returns false
     *        and would incorrectly reject the one-shot initial capture.
     */
    void saveAndRecordPreAutotileGeometry(const QString& windowId, const QString& screenId, const QRectF& frame,
                                          bool knownFreeFloating = false);

    /**
     * @brief All-bucket pre-autotile geometry lookup.
     *
     * Returns the first VALID rect found for @p windowId across every
     * screen's bucket in m_preAutotileGeometries, or an invalid QRectF if
     * none holds one. Readers must scan all buckets (not just the window's
     * current screen): a VS config change can re-resolve the window's
     * screen without moving its geometry bucket. Shared by the batch-float,
     * drag-to-float, cross-monitor-snapshot, desktop-move-stash, and
     * desktop-switch restore paths.
     *
     * @param bucketScreenId Optional out — receives the screen key of the
     *        bucket the rect was found under (unchanged when not found).
     */
    QRectF findPreAutotileGeometry(const QString& windowId, QString* bucketScreenId = nullptr) const;

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

    QSet<QString> m_autotileScreens;
    /// Bumped on every autotileScreensChanged signal. loadSettings' async
    /// Properties.Get reply captures the value at dispatch and discards
    /// itself if a signal landed in between — the signal carried a newer
    /// set AND ran the full per-screen transition handling the raw reply
    /// assignment lacks.
    quint64 m_screensSignalGeneration = 0;
    /// Pre-autotile frame geometry, keyed [screenId][windowId].
    ///
    /// Ownership: this is a local cache. The daemon's
    /// `WindowTrackingService::m_preTileGeometries` is the authoritative
    /// store and survives daemon restart. The effect populates this map on
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
    QHash<QString, QHash<QString, QRectF>> m_preAutotileGeometries;
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
    QHash<QString, QString> m_expectedOutputMove;
    QSet<QString> m_savedNotifiedForDesktopReturn; ///< windows removed from m_notifiedWindows on desktop switch
    /// Pre-autotile geometry preserved when a window is moved to another
    /// desktop. Keyed by windowId; value holds (sourceScreenId, frameRect)
    /// so a cross-desktop + cross-screen move can detect the screen change
    /// at restore time and skip the saved geometry (the rect is in the
    /// source screen's coordinate space and would land off-target on a
    /// different monitor).
    QHash<QString, QPair<QString, QRectF>> m_savedPreAutotileForDesktopMove;
    QSet<QString> m_pendingCloses;
    bool m_inOutputChanged = false; ///< re-entrancy guard for handleWindowOutputChanged
    QHash<QString, QMetaObject::Connection>
        m_pendingCrossScreenRestore; ///< windowId → deferred size-restore connection
    QSet<QString> m_minimizeFloatedWindows;
    // NOTE: title-bar (borderless) state is owned by the effect's
    // DecorationManager; this handler only tracks tiled membership for
    // border RENDERING via m_border.tiledWindowsByScreen.
    /// Pending debounced minimize→float commits, keyed by windowId. An entry
    /// is created when slotWindowMinimizedChanged sees minimized=true; if the
    /// matching unminimize arrives before the timer fires, the timer is
    /// cancelled and no D-Bus float is issued. This absorbs spurious
    /// minimize/unminimize cycles that KWin emits on tiled windows when
    /// plasmashell notification popups transiently change stacking.
    QHash<QString, QPointer<QTimer>> m_pendingMinimizeFloat;
    /// Global stagger epoch, bumped on a desktop/screen switch (slotScreensChanged)
    /// to cancel EVERY in-flight staggered apply — geometry computed for the old
    /// context must never land in the new one.
    uint64_t m_autotileStaggerGeneration = 0;
    /// Per-screen stagger generation. A retile bumps only its own screen(s), so a
    /// newer batch for the SAME screen supersedes an earlier one while a batch for
    /// a DIFFERENT screen leaves it untouched. Without this, the destination batch
    /// of a cross-output move (emitted microseconds after the source reflow)
    /// cancelled the source's still-staggered windows via the single global
    /// generation — they never moved, leaving a hole on the source monitor.
    QHash<QString, uint64_t> m_autotileStaggerGenByScreen;
    QHash<QString, QRect> m_autotileTargetZones;
    QHash<QString, QRect> m_centeredWaylandZones; ///< zones where Wayland windows were last centered
    QString m_pendingAutotileFocusWindowId;
    QPointer<KWin::EffectWindow> m_pendingReactivateWindow; ///< re-activate after raise loop (daemon restart)
    QSet<QString> m_monocleMaximizedWindows;
    int m_suppressMaximizeChanged = 0;
    // ── Focus follows mouse ──
    bool m_focusFollowsMouse = false;
    // ── Border state — uses shared BorderState from compositor-common ──
    BorderState m_border;
};

} // namespace PlasmaZones
