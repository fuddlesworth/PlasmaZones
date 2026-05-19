// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorCompositor/AutotileState.h>

#include <QColor>
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

    /// React to a window's all-desktops (sticky) state changing. Scroll mode
    /// never tiles a sticky window — it cannot occupy every per-desktop strip
    /// at once — so a window pinned to all desktops is dropped from its strip,
    /// and one that is un-pinned is re-tiled if it now belongs on a scroll
    /// screen.
    void handleWindowStickyChanged(KWin::EffectWindow* w);

    /// Record the geometry the daemon last resolved for a scroll window, so
    /// an app-initiated resize away from it can be detected and corrected.
    void recordAppliedGeometry(const QString& windowId, const QRect& geometry);

    /// React to a scroll window's frame geometry changing. An app resizing
    /// itself out of its tile slot is re-asserted (debounced); the strip owns
    /// scroll-window geometry.
    void onWindowFrameGeometryChanged(KWin::EffectWindow* w);

    /// React to a window's interactive move/resize starting. Records whether
    /// the interaction is a resize — `isUserResize()` is reliable at the start
    /// signal but not necessarily at finish — so onWindowDragFinished can tell
    /// a drag-to-reorder from a resize.
    void onWindowMoveResizeStarted(KWin::EffectWindow* w);

    /// React to a scroll window's interactive move/resize finishing. A drag
    /// that released over another column's span reorders the dragged window's
    /// column to the drop position (drag-to-reorder); the strip then re-resolves
    /// and snaps the window into its new slot. A resize is ignored.
    void onWindowDragFinished(KWin::EffectWindow* w);

    /// Report a focus change to the scroll engine. No-op off scroll screens.
    void notifyWindowFocused(const QString& windowId, const QString& screenId);

    /// Daemon (re)connected: clear stale tracking, re-subscribe, re-query.
    void onDaemonReady();

    // D-Bus signal connections and initial state query
    void connectSignals();
    void loadSettings();

    // ── Border decoration ───────────────────────────────────────────────
    // The KWin effect's generic border machinery (PlasmaZonesEffect::
    // updateWindowBorder) consults these so a scroll-mode column is decorated
    // exactly like an autotile window — just from this handler's settings.
    // Only the scalar fields of BorderState are used; membership is tracked
    // by m_notifiedWindows, not BorderState's per-screen buckets.

    /// True when scroll mode manages @p windowId — used by the effect to pick
    /// which handler's border settings apply to a window.
    bool isTiledWindow(const QString& windowId) const
    {
        return m_notifiedWindows.contains(windowId);
    }
    /// True when @p windowId is a scroll-managed window AND borders are on.
    bool shouldShowBorderForWindow(const QString& windowId) const
    {
        return m_border.showBorder && m_notifiedWindows.contains(windowId);
    }
    int borderWidth() const
    {
        return m_border.width;
    }
    QColor borderColor() const
    {
        return m_border.color;
    }
    QColor inactiveBorderColor() const
    {
        return m_border.inactiveColor;
    }
    int borderRadius() const
    {
        return m_border.radius;
    }
    void setBorderWidth(int w)
    {
        m_border.width = w;
    }
    void setBorderColor(const QColor& c)
    {
        m_border.color = c;
    }
    void setInactiveBorderColor(const QColor& c)
    {
        m_border.inactiveColor = c;
    }
    void setBorderRadius(int r)
    {
        m_border.radius = r;
    }
    void updateShowBorderSetting(bool enabled)
    {
        m_border.showBorder = enabled;
    }
    /// Toggle server-side title-bar hiding across every scroll-managed window.
    void updateHideTitleBarsSetting(bool enabled);

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

    /// Whether @p w should be tiled by scroll mode: the mode-generic tiling
    /// predicate, plus scroll's exclusion of all-desktops (sticky) windows.
    bool isEligibleForScroll(KWin::EffectWindow* w) const;

    /// Tracked windows currently reported as being on @p screenId.
    QStringList trackedWindowsOnScreen(const QString& screenId) const;
    /// Debounced re-assert: snap every drifted window back to the geometry the
    /// daemon resolved for it.
    void flushReasserts();

    /// Hide or restore @p w's server-side title bar, tracking the change in
    /// m_borderlessWindows so the toggle and a window leaving scroll mode can
    /// reverse it. CSD windows (no server decoration) are skipped. @p w may be
    /// null — the tracking entry is still dropped so it cannot leak.
    void setWindowBorderless(KWin::EffectWindow* w, const QString& windowId, bool borderless);
    /// Re-apply title-bar hiding to the current scroll window set (when the
    /// setting is on) and rebuild every border. Called after the tracked-window
    /// set changes.
    void refreshDecorations();
    /// Restore decoration (title bar + border) for a window no longer
    /// scroll-tracked. @p w may be null when the window has already closed.
    void clearDecoration(const QString& windowId, KWin::EffectWindow* w);

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
    /// Windows currently in an interactive *resize* (recorded at the start
    /// signal). onWindowDragFinished consults this to skip resizes — only a
    /// move is a drag-to-reorder.
    QSet<QString> m_interactiveResize;
    /// Windows with a drag-reorder (windowDropped) in flight to the daemon.
    /// Drift re-asserts are suppressed for them until the daemon's re-resolve
    /// lands (recordAppliedGeometry clears the flag), so a stale re-assert
    /// cannot snap the window back to its pre-drag slot mid-reorder.
    QSet<QString> m_reorderPending;
    /// Windows already re-asserted since the daemon last resolved them — caps
    /// re-assertion at one attempt per resolve episode so a window that cannot
    /// hit the exact tile rect (X11 size increments) does not loop.
    QSet<QString> m_reasserted;
    QTimer* m_reassertTimer = nullptr;
    /// Incremented on every daemon (re)connect. A D-Bus reply captures the
    /// epoch at call time and skips its rollback if the daemon reconnected
    /// meanwhile — onDaemonReady has already rebuilt the tracking sets.
    int m_daemonEpoch = 0;

    /// Scroll-mode border decoration settings. Only the scalar fields are used
    /// (showBorder / hideTitleBars / width / radius / color / inactiveColor) —
    /// the effect's generic border machinery reads them via the accessors
    /// above; per-window membership is m_notifiedWindows, not BorderState's
    /// per-screen buckets.
    PhosphorCompositor::BorderState m_border;
    /// Scroll windows whose server-side title bar this handler hid
    /// (setNoBorder(true)) — the subset of m_notifiedWindows decorated while
    /// hideTitleBars is on. A window is on exactly one screen, so a flat set
    /// suffices (autotile needs per-screen buckets only for virtual screens).
    QSet<QString> m_borderlessWindows;
};

} // namespace PlasmaZones
