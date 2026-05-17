// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/**
 * @brief Routes window lifecycle to the niri-style scroll engine.
 *
 * The scroll-mode counterpart to AutotileHandler's lifecycle-reporting
 * surface: tracks which screens are in scroll mode (from the daemon's
 * @c org.plasmazones.Scroll @c scrollScreensChanged signal) and reports
 * open / close / focus for windows on those screens to that interface.
 *
 * Scroll mode is geometry-agnostic on the effect side — resolved window
 * geometry arrives over the shared WindowTracking applyGeometriesBatch
 * path — so this class deliberately has none of AutotileHandler's
 * borders / monocle / pre-tile-geometry machinery. AutotileHandler knows
 * nothing of scroll and this class knows nothing of autotile; the daemon's
 * screen sets are disjoint, so each handler acts only on its own screens.
 */
class ScrollHandler : public QObject
{
    Q_OBJECT

public:
    explicit ScrollHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    // ═══════════════════════════════════════════════════════════════════
    // Integration points (called by PlasmaZonesEffect)
    // ═══════════════════════════════════════════════════════════════════

    /// Report a newly-mapped window to the scroll engine if it sits on a
    /// scroll-mode screen. No-op for windows on autotile/snap screens.
    void notifyWindowAdded(KWin::EffectWindow* w);

    /**
     * @brief Batch-report windows on scroll screens in one D-Bus call.
     *
     * Used on effect startup and daemon (re)connect instead of per-window
     * windowOpened round-trips.
     *
     * @param windows Candidate windows to process.
     * @param resetNotified Drop each window from the notified set before
     *        processing, so a daemon restart re-announces existing windows.
     */
    void notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows, bool resetNotified = false);

    /// Report a window close to the scroll engine if it was on a scroll screen.
    void onWindowClosed(const QString& windowId, const QString& screenId);

    /// Report a minimize/restore for a scroll-mode window. A window restored
    /// from minimize without ever having been reported open (it opened
    /// minimized) is treated as a fresh open.
    void onWindowMinimizedChanged(KWin::EffectWindow* w);

    /// React to a window moving between screens (monitor hotplug, "move to
    /// screen", or a virtual-screen crossing). Drops it from the old strip
    /// and adds it to the new one when either is scroll mode.
    void handleWindowOutputChanged(KWin::EffectWindow* w);

    /// Report a focus change to the scroll engine. No-op off scroll screens.
    void notifyWindowFocused(const QString& windowId, const QString& screenId);

    /// Daemon (re)connected: clear stale tracking, re-subscribe, re-query.
    void onDaemonReady();

    // D-Bus signal connections and initial state query
    void connectSignals();
    void loadSettings();

    /// Whether @p screenId is currently in scroll mode.
    bool isScrollScreen(const QString& screenId) const
    {
        return m_scrollScreens.contains(screenId);
    }

    /// Whether @p windowId has been reported open to the scroll engine.
    bool isTrackedWindow(const QString& windowId) const
    {
        return m_notifiedWindows.contains(windowId);
    }

public Q_SLOTS:
    /// Daemon told us the scroll-mode screen set changed.
    void slotScrollScreensChanged(const QStringList& screenIds);

private:
    /// Tracked windows currently reported as being on @p screenId.
    QStringList trackedWindowsOnScreen(const QString& screenId) const;

    PlasmaZonesEffect* m_effect;

    QSet<QString> m_scrollScreens; ///< Screens currently in scroll mode.
    QSet<QString> m_notifiedWindows; ///< Windows reported open to the scroll engine.
    QHash<QString, QString> m_notifiedWindowScreens; ///< windowId → screen ID at report time.
    /// Windows closed before their windowOpened D-Bus call resolved; the
    /// matching open is suppressed when it arrives (D-Bus ordering race).
    QSet<QString> m_pendingCloses;
};

} // namespace PlasmaZones
