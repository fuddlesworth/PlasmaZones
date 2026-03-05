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

    // Cleanup: restore title bars and clear border state for all borderless windows
    void restoreAllBorderless();

    // Settings update: toggle hide-title-bars with border restore on disable
    void updateHideTitleBarsSetting(bool enabled);

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
    QRect applyBorderInset(const QRect& geo) const;
    bool shouldInsetForBorder(const QString& windowId, const QRect& geo) const;
    std::optional<QRect> borderZoneGeometry(const QString& windowId) const;
    QVector<QRect> allBorderZoneGeometries() const;

    // Invalidate pending stagger timers (call before triggering retile)
    void invalidateStaggerGeneration()
    {
        ++m_autotileStaggerGeneration;
    }

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
    bool saveAndRecordPreAutotileGeometry(const QString& windowId, const QString& screenName, const QRectF& frame);
    void centerUndersizedAutotileWindows();
    bool shouldApplyBorderInset(const QString& windowId) const;

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
    QSet<QString> m_notifiedWindows;
    QSet<QString> m_pendingCloses;
    QSet<QString> m_minimizeFloatedWindows;
    uint64_t m_autotileStaggerGeneration = 0;
    QHash<QString, QRect> m_autotileTargetZones;
    QString m_pendingAutotileFocusWindowId;
    QSet<QString> m_monocleMaximizedWindows;
    int m_suppressMaximizeChanged = 0;
    // ── Border state (logically grouped for SRP clarity) ──
    struct BorderState
    {
        QSet<QString> borderlessWindows;
        QHash<QString, QRect> zoneGeometries;
        bool hideTitleBars = false;
        int width = 2;
        QColor color;
    } m_border;
};

} // namespace PlasmaZones
