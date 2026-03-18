// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <optional>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

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
    bool isAutotileScreen(const QString& screenName) const;
    const QSet<QString>& autotileScreens() const
    {
        return m_autotileScreens;
    }

    // Border rendering accessors
    bool isBorderlessWindow(const QString& windowId) const
    {
        return m_border.borderlessWindows.contains(windowId);
    }
    bool isTiledWindow(const QString& windowId) const
    {
        return m_border.tiledWindows.contains(windowId);
    }
    bool shouldShowBorderForWindow(const QString& windowId) const
    {
        return isBorderlessWindow(windowId) || (m_border.showBorder && isTiledWindow(windowId));
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
    QRect applyBorderInset(const QRect& geo) const;
    bool shouldInsetForBorder(const QString& windowId, const QRect& geo) const;
    std::optional<QRect> borderZoneGeometry(const QString& windowId) const;
    QVector<QRect> allBorderZoneGeometries() const;

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
    void slotWindowsTileRequested(const QString& tileRequestsJson);
    void slotFocusWindowRequested(const QString& windowId);
    void slotEnabledChanged(bool enabled);
    void slotScreensChanged(const QStringList& screenNames, bool isDesktopSwitch);
    void slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenName);

    // Window state change handlers (connected per-window in setupWindowConnections)
    void slotWindowMinimizedChanged(KWin::EffectWindow* w);
    void slotWindowMaximizedStateChanged(KWin::EffectWindow* w, bool horizontal, bool vertical);
    void slotWindowFullScreenChanged(KWin::EffectWindow* w);
    void slotWindowFrameGeometryChanged(KWin::EffectWindow* w, const QRectF& oldGeometry);

private:
    // ═══════════════════════════════════════════════════════════════════
    // Utility methods
    // ═══════════════════════════════════════════════════════════════════

    void setWindowBorderless(KWin::EffectWindow* w, const QString& windowId, bool borderless);
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
    bool saveAndRecordPreAutotileGeometry(const QString& windowId, const QString& screenName, const QRectF& frame);
    bool shouldApplyBorderInset(const QString& windowId) const;
    void reportDiscoveredMinSize(const QString& windowId, int minWidth, int minHeight);

    /**
     * @brief Find key in saved geometries map for a window (exact or stable ID match)
     */
    static QString findSavedGeometryKey(const QHash<QString, QRectF>& savedGeometries, const QString& windowId);

    /**
     * @brief Check if we already have saved geometry for this window (exact or stable ID)
     */
    static bool hasSavedGeometryForWindow(const QHash<QString, QRectF>& savedGeometries, const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════
    // Member variables
    // ═══════════════════════════════════════════════════════════════════

    PlasmaZonesEffect* m_effect;

    QSet<QString> m_autotileScreens;
    QHash<QString, QHash<QString, QRectF>> m_preAutotileGeometries;
    QHash<QString, QStringList> m_savedSnapStackingOrder; ///< snap-mode stacking order, restored on autotile→snap
    QHash<QString, QStringList> m_savedAutotileStackingOrder; ///< autotile stacking order, restored on snap→autotile
    QSet<QString> m_notifiedWindows;
    QHash<QString, QString> m_notifiedWindowScreens; ///< windowId → screen name at time of notification
    QSet<QString> m_savedNotifiedForDesktopReturn; ///< windows removed from m_notifiedWindows on desktop switch
    QHash<QString, QRectF>
        m_savedPreAutotileForDesktopMove; ///< pre-autotile geometries for windows moved to another desktop
    QSet<QString> m_pendingCloses;
    bool m_inOutputChanged = false; ///< re-entrancy guard for handleWindowOutputChanged
    QHash<QString, QMetaObject::Connection>
        m_pendingCrossScreenRestore; ///< windowId → deferred size-restore connection
    QSet<QString> m_minimizeFloatedWindows;
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
    // ── Border state (logically grouped for SRP clarity) ──
    struct BorderState
    {
        QSet<QString> borderlessWindows;
        QSet<QString> tiledWindows; ///< all currently tiled windows (for showBorder without hideTitleBars)
        QHash<QString, QRect> zoneGeometries;
        bool hideTitleBars = false;
        bool showBorder = false;
        int width = 2;
        int radius = 0;
        QColor color;
        QColor inactiveColor;
    } m_border;
};

} // namespace PlasmaZones
