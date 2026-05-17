// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>

class QTimer;

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

    /// Record the geometry the daemon last resolved for a scroll window, so
    /// an app-initiated resize away from it can be detected and corrected.
    void recordAppliedGeometry(const QString& windowId, const QRect& geometry);

    /// React to a scroll window's frame geometry changing. An app resizing
    /// itself out of its tile slot is re-asserted (debounced); the strip owns
    /// scroll-window geometry.
    void onWindowFrameGeometryChanged(KWin::EffectWindow* w);

    /// Report a focus change to the scroll engine. No-op off scroll screens.
    void notifyWindowFocused(const QString& windowId, const QString& screenId);

    /// Daemon (re)connected: clear stale tracking, re-subscribe, re-query.
    void onDaemonReady();

    // D-Bus signal connections and initial state query
    void connectSignals();
    void loadSettings();

public Q_SLOTS:
    /// Daemon told us the scroll-mode screen set changed.
    void slotScrollScreensChanged(const QStringList& screenIds);

private:
    /**
     * @brief Batch-report windows on scroll screens in one D-Bus call.
     *
     * Used on effect startup and daemon (re)connect instead of per-window
     * windowOpened round-trips. Internal — driven by loadSettings() and
     * slotScrollScreensChanged(). Windows already in m_notifiedWindows are
     * skipped; a caller needing a full re-announce (daemon reconnect) clears
     * the notified set first — see onDaemonReady().
     *
     * @param windows Candidate windows to process.
     */
    void notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows);

    /// Tracked windows currently reported as being on @p screenId.
    QStringList trackedWindowsOnScreen(const QString& screenId) const;
    /// Debounced re-assert: snap every drifted window back to the geometry the
    /// daemon resolved for it.
    void flushReasserts();

    PlasmaZonesEffect* m_effect;

    QSet<QString> m_scrollScreens; ///< Screens currently in scroll mode.
    QSet<QString> m_notifiedWindows; ///< Windows reported open to the scroll engine.
    QHash<QString, QString> m_notifiedWindowScreens; ///< windowId → screen ID at report time.
    /// Windows closed before their windowOpened D-Bus call resolved; the
    /// matching open is suppressed when it arrives (D-Bus ordering race).
    QSet<QString> m_pendingCloses;
    /// windowId → geometry the daemon last resolved for it; the reference
    /// point for detecting app-initiated resizes.
    QHash<QString, QRect> m_appliedGeometry;
    QSet<QString> m_reassertPending; ///< Windows awaiting a debounced re-assert.
    /// Windows already re-asserted since the daemon last resolved them — caps
    /// re-assertion at one attempt per resolve episode so a window that cannot
    /// hit the exact tile rect (X11 size increments) does not loop.
    QSet<QString> m_reasserted;
    QTimer* m_reassertTimer = nullptr;
    /// Incremented on every daemon (re)connect. A D-Bus reply captures the
    /// epoch at call time and skips its rollback if the daemon reconnected
    /// meanwhile — onDaemonReady has already rebuilt the tracking sets.
    int m_daemonEpoch = 0;
};

} // namespace PlasmaZones
