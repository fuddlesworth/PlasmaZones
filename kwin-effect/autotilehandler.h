// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <autotile_state.h>
#include <dbus_types.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class QTimer;

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/**
 * @brief Handles autotile integration for PlasmaZones
 *
 * Manages the autotile D-Bus interface, screen tracking, window tiling,
 * monocle mode, borderless state, and pre-autotile geometry preservation.
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

    void notifyWindowAdded(KWin::EffectWindow* w);

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

    void onWindowClosed(const QString& windowId, const QString& screenId);
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
     * @param screenId Screen the window was tiled on (pre-drag screen)
     * @param immediate Apply size restore synchronously during the interactive move
     */
    void handleDragToFloat(KWin::EffectWindow* w, const QString& windowId, const QString& screenId,
                           bool immediate = false);
    void savePreAutotileForDesktopMove(const QString& windowId, const QString& screenId);
    void handleWindowOutputChanged(KWin::EffectWindow* w);

    // D-Bus signal connections and settings
    void connectSignals();
    void loadSettings();

    // Cleanup: unmaximize all monocle-maximized windows (called on daemon loss / effect teardown)
    void restoreAllMonocleMaximized();

    // Cleanup: restore title bars and clear border state for all borderless windows
    void restoreAllBorderless();

    // Settings update: toggle hide-title-bars with border restore on disable
    void updateHideTitleBarsSetting(bool enabled);
    void updateShowBorderSetting(bool enabled);

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
    bool isBorderlessWindow(const QString& windowId) const
    {
        return AutotileStateHelpers::isBorderlessWindow(m_border, windowId);
    }
    bool isTiledWindow(const QString& windowId) const
    {
        return AutotileStateHelpers::isTiledWindow(m_border, windowId);
    }
    bool shouldShowBorderForWindow(const QString& windowId) const
    {
        return AutotileStateHelpers::shouldShowBorderForWindow(m_border, windowId);
    }
    int borderWidth() const
    {
        return m_border.width;
    }
    void setBorderWidth(int w)
    {
        m_border.width = w;
    }
    QColor borderColor() const
    {
        return m_border.color;
    }
    void setBorderColor(const QColor& c)
    {
        m_border.color = c;
    }
    QColor inactiveBorderColor() const
    {
        return m_border.inactiveColor;
    }
    void setInactiveBorderColor(const QColor& c)
    {
        m_border.inactiveColor = c;
    }
    int borderRadius() const
    {
        return m_border.radius;
    }
    void setBorderRadius(int r)
    {
        m_border.radius = r;
    }
    QRect applyBorderInset(const QRect& geo) const
    {
        return AutotileStateHelpers::applyBorderInset(geo, m_border.width);
    }
    bool shouldInsetForBorder(const QString& windowId, const QRect& geo) const
    {
        return AutotileStateHelpers::shouldInsetForBorder(m_border, windowId, geo);
    }
    std::optional<QRect> borderZoneGeometry(const QString& windowId) const
    {
        return AutotileStateHelpers::borderZoneGeometry(m_border, windowId);
    }
    QVector<QRect> allBorderZoneGeometries() const
    {
        return AutotileStateHelpers::allBorderZoneGeometries(m_border);
    }

    /**
     * @brief Extract pre-autotile geometry from one screen and inject into another.
     *
     * Used during virtual screen drag transfers where handleWindowOutputChanged
     * won't fire (same physical monitor). Snapshots the geometry before
     * onWindowClosed clears it, then injects into the target screen's map
     * after notifyWindowAdded.
     *
     * @param windowId The window being transferred
     * @param fromScreenId Source screen to extract geometry from
     * @param toScreenId Target screen to inject geometry into
     * @return true if geometry was transferred
     */
    bool transferPreAutotileGeometry(const QString& windowId, const QString& fromScreenId, const QString& toScreenId);

    // Invalidate pending stagger timers (call before triggering retile)
    void invalidateStaggerGeneration()
    {
        ++m_autotileStaggerGeneration;
    }

    /**
     * @brief Take the saved global stacking order snapshot (move semantics).
     *
     * Called by handleResnapToNewLayout to restore z-order after resnap.
     * Returns and clears the snapshot captured by slotScreensChanged.
     */
    QVector<QPointer<KWin::EffectWindow>> takeSavedGlobalStack();

    // Set a window to re-activate after the next autotile raise loop completes.
    // Used by slotDaemonReady() to preserve focus of non-tiled windows (e.g. KCM).
    void setPendingReactivateWindow(KWin::EffectWindow* w)
    {
        m_pendingReactivateWindow = w;
    }

public Q_SLOTS:
    // Autotile D-Bus signal handlers
    void slotWindowsTileRequested(const TileRequestList& tileRequests);
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

    /// Toggle a window's borderless state on a specific screen. screenId is
    /// REQUIRED for correctness: on per-VS retiles the effect must update
    /// only that screen's bucket so sibling-VS tracking is untouched. For
    /// the feature-disable path (where a bulk restore iterates all screens),
    /// callers pass the screen key from their enumeration; there is no
    /// "global" variant.
    void setWindowBorderless(KWin::EffectWindow* w, const QString& windowId, bool borderless, const QString& screenId);
    void unmaximizeMonocleWindow(const QString& windowId);

    /**
     * @brief Shared float-state cleanup for a window being floated
     *
     * Updates float cache, removes from tiled/borderless sets, restores title bars,
     * removes border, and unmaximizes monocle. Used by both slotWindowFloatingChanged
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
     * capturing them would poison the pre-tile entry.
     *
     * @param knownFreeFloating Bypass the isWindowFloating guard when the
     *        caller knows the frame is authoritatively a free-float rect.
     *        Use true from the window-added paths (notifyWindowAdded and
     *        notifyWindowsAddedBatch) — fresh windows are NOT tracked in
     *        the FloatingCache yet, so isWindowFloating() returns false
     *        and would incorrectly reject the one-shot initial capture.
     */
    bool saveAndRecordPreAutotileGeometry(const QString& windowId, const QString& screenId, const QRectF& frame,
                                          bool knownFreeFloating = false);
    void reportDiscoveredMinSize(const QString& windowId, int minWidth, int minHeight);

    // ═══════════════════════════════════════════════════════════════════
    // Member variables
    // ═══════════════════════════════════════════════════════════════════

    PlasmaZonesEffect* m_effect;

    QSet<QString> m_autotileScreens;
    /// Pre-autotile frame geometry, keyed [screenId][windowId].
    ///
    /// Ownership: this is a local cache. The daemon's
    /// `WindowTrackingService::m_preTileGeometries` is the authoritative
    /// store and survives daemon restart. The effect populates this map on
    /// the first autotile transition for a window so it can restore the
    /// frame instantly when the window leaves autotile mode (untile, mode
    /// switch, screen change) without waiting on a D-Bus round-trip.
    ///
    /// Layout: per-screen bucket mirrors `BorderState` so swap/rotate and
    /// cross-screen moves can transplant or drop a window's record by
    /// looking only at the source screen's bucket — see
    /// `transferPreAutotileGeometry()` in autotilehandler.cpp.
    QHash<QString, QHash<QString, QRectF>> m_preAutotileGeometries;
    QHash<QString, QStringList> m_savedSnapStackingOrder; ///< snap-mode stacking order, restored on autotile→snap
    QHash<QString, QStringList> m_savedAutotileStackingOrder; ///< autotile stacking order, restored on snap→autotile
    QSet<QString> m_notifiedWindows;
    QHash<QString, QString> m_notifiedWindowScreens; ///< windowId → screen ID at time of notification
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
    /// Pending debounced minimize→float commits, keyed by windowId. An entry
    /// is created when slotWindowMinimizedChanged sees minimized=true; if the
    /// matching unminimize arrives before the timer fires, the timer is
    /// cancelled and no D-Bus float is issued. This absorbs spurious
    /// minimize/unminimize cycles that KWin emits on tiled windows when
    /// plasmashell notification popups transiently change stacking.
    QHash<QString, QPointer<QTimer>> m_pendingMinimizeFloat;
    uint64_t m_autotileStaggerGeneration = 0;
    uint64_t m_restoreStaggerGeneration = 0;
    QVector<QPointer<KWin::EffectWindow>> m_savedGlobalStackForResnap; ///< z-order snapshot for resnap restore
    QHash<QString, QRect> m_autotileTargetZones;
    QHash<QString, QRect> m_centeredWaylandZones; ///< zones where Wayland windows were last centered
    QString m_pendingAutotileFocusWindowId;
    QPointer<KWin::EffectWindow> m_pendingReactivateWindow; ///< re-activate after raise loop (daemon restart)
    QSet<QString> m_monocleMaximizedWindows;
    int m_suppressMaximizeChanged = 0;
    // ── Focus follows mouse ──
    bool m_focusFollowsMouse = false;
    QString m_lastFocusFollowsMouseWindowId;
    // ── Border state — uses shared BorderState from compositor-common ──
    BorderState m_border;
};

} // namespace PlasmaZones
