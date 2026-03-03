// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
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
    void onWindowClosed(const QString& windowId, const QString& screenName);
    void onDaemonReady();

    /**
     * @brief Handle float toggle for autotile-managed windows.
     *
     * Checks whether the window has pre-autotile geometry (i.e., is managed
     * by the autotile engine). If so, performs the autotile-specific pre-save
     * and D-Bus call.
     *
     * @return true if handled (caller should return), false if not an autotile window
     */
    bool handleAutotileFloatToggle(KWin::EffectWindow* activeWindow, const QString& windowId,
                                    const QString& screenName);

    // D-Bus signal connections and settings
    void connectSignals();
    void loadSettings();

    // Cleanup: unmaximize all monocle-maximized windows (called on daemon loss / effect teardown)
    void restoreAllMonocleMaximized();

    // Settings update: toggle hide-title-bars with border restore on disable
    void updateHideTitleBarsSetting(bool enabled);

    // Screen accessors (for gating drag/snap/overlay behavior per-screen)
    bool isAutotileScreen(const QString& screenName) const;
    const QSet<QString>& autotileScreens() const { return m_autotileScreens; }

public Q_SLOTS:
    // Autotile D-Bus signal handlers
    void slotWindowsTileRequested(const QString& tileRequestsJson);
    void slotFocusWindowRequested(const QString& windowId);
    void slotEnabledChanged(bool enabled);
    void slotScreensChanged(const QStringList& screenNames);
    void slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenName);

    // Window state change handlers (connected per-window in setupWindowConnections)
    void slotWindowMinimizedChanged(KWin::EffectWindow* w);
    void slotWindowMaximizedStateChanged(KWin::EffectWindow* w, bool horizontal, bool vertical);
    void slotWindowFullScreenChanged(KWin::EffectWindow* w);

private:
    // ═══════════════════════════════════════════════════════════════════
    // Utility methods
    // ═══════════════════════════════════════════════════════════════════

    void setWindowBorderless(KWin::EffectWindow* w, const QString& windowId, bool borderless);
    void unmaximizeMonocleWindow(const QString& windowId);
    bool saveAndRecordPreAutotileGeometry(const QString& windowId, const QString& screenName,
                                          const QRectF& frame);
    void centerUndersizedAutotileWindows();

    /**
     * @brief Find key in saved geometries map for a window (exact or stable ID match)
     */
    static QString findSavedGeometryKey(const QHash<QString, QRectF>& savedGeometries,
                                        const QString& windowId);

    /**
     * @brief Check if we already have saved geometry for this window (exact or stable ID)
     */
    static bool hasSavedGeometryForWindow(const QHash<QString, QRectF>& savedGeometries,
                                          const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════
    // Member variables
    // ═══════════════════════════════════════════════════════════════════

    PlasmaZonesEffect* m_effect;

    QSet<QString> m_autotileScreens;
    QHash<QString, QHash<QString, QRectF>> m_preAutotileGeometries;
    QSet<QString> m_notifiedWindows;
    QSet<QString> m_pendingCloses;
    QSet<QString> m_minimizeFloatedWindows;
    uint64_t m_autotileStaggerGeneration = 0;
    QHash<QString, QRect> m_autotileTargetZones;
    QString m_pendingAutotileFocusWindowId;
    QSet<QString> m_monocleMaximizedWindows;
    int m_suppressMaximizeChanged = 0;
    QSet<QString> m_borderlessWindows;
    bool m_autotileHideTitleBars = false;
};

} // namespace PlasmaZones
