// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tilinghandlerbase.h"

#include <PhosphorCompositor/AutotileState.h>

#include <QColor>
#include <QHash>
#include <QList>
#include <QPointF>
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
class ScrollHandler : public TilingHandlerBase
{
    Q_OBJECT

public:
    explicit ScrollHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

protected:
    QString interfaceName() const override;
    QString screensProperty() const override;

public:
    // ═══════════════════════════════════════════════════════════════════
    // Integration points (called by PlasmaZonesEffect)
    // ═══════════════════════════════════════════════════════════════════

    /// Report a newly-mapped window to the scroll engine if it sits on a
    /// scroll-mode screen. No-op for windows on autotile/snap screens.
    ///
    /// @param focusOnAdd when true (a genuine window open), the window takes
    /// focus if the focus-new-windows setting is on. Re-add callers (screen
    /// change, sticky toggle, un-minimize) leave it false — a window merely
    /// re-entering the layout must not steal focus, e.g. on monitor hotplug.
    void notifyWindowAdded(KWin::EffectWindow* w, bool focusOnAdd = false);

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
    /// @p slotRect is the column's full rect (the daemon's resolved tile slot
    /// before any effect-side centering — see PlasmaZonesEffect::
    /// constrainToScrollSlot). @p appliedRect is the rect actually pushed via
    /// moveResize, which may be smaller and centered inside slotRect for a
    /// constrained (fixed-size) window. Drag-to-reorder anchor selection
    /// uses slotRect (column edges); drift detection uses appliedRect.
    void recordAppliedGeometry(const QString& windowId, const QRect& slotRect, const QRect& appliedRect);

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

    /// Focus-follows-mouse: when enabled, activate the topmost scroll-managed
    /// window under @p pos. @p screenId is the cursor's resolved screen. No-op
    /// off scroll screens or when the setting is off. Mirrors AutotileHandler.
    void handleCursorMoved(const QPointF& pos, const QString& screenId);

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
    /// Apply the show-border setting. Returns true iff the value changed, so
    /// the caller can skip a redundant full updateAllBorders() rebuild.
    bool updateShowBorderSetting(bool enabled)
    {
        if (m_border.showBorder == enabled) {
            return false;
        }
        m_border.showBorder = enabled;
        return true;
    }
    /// Toggle server-side title-bar hiding across every scroll-managed window.
    /// Returns true iff the value changed (a no-op when already at @p enabled).
    bool updateHideTitleBarsSetting(bool enabled);

    // ── Focus behavior ───────────────────────────────────────────────────
    // setFocusFollowsMouse(bool) is inherited from TilingHandlerBase.

    /// Enable/disable auto-focusing a window as it enters the scroll layout.
    void setFocusNewWindows(bool enabled)
    {
        m_focusNewWindows = enabled;
    }

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
    /// Hide the title bar of every currently-tracked scroll window (when the
    /// setting is on) and rebuild all borders. Used by the batch-add path; the
    /// leave path is handled per-window by clearDecoration(), so this only ever
    /// hides — it does not restore title bars for windows that left the set.
    void refreshDecorations();
    /// Hide the server-side title bar of every tracked scroll window. Minimized
    /// windows are skipped (onWindowMinimizedChanged re-hides them on restore).
    /// Shared by refreshDecorations() and updateHideTitleBarsSetting()'s ON path.
    void hideTitleBarsForTrackedWindows();
    /// Restore decoration (title bar + border) for a window no longer
    /// scroll-tracked. @p w may be null when the window has already closed.
    void clearDecoration(const QString& windowId, KWin::EffectWindow* w);

    // m_effect, m_notifiedWindows, m_notifiedWindowScreens, m_pendingCloses,
    // m_focusFollowsMouse is inherited from TilingHandlerBase.

    QSet<QString> m_scrollScreens; ///< Screens currently in scroll mode.
    /// windowId → geometry the daemon last resolved for it; the reference
    /// point for detecting app-initiated resizes. For constrained (fixed-size)
    /// windows this is the centered sub-rect, NOT the column rect — drag-to-
    /// reorder anchor selection MUST use m_slotGeometry below for column-edge
    /// comparisons, otherwise an in-column nudge of a constrained window can
    /// be misclassified as crossing into a sibling column.
    QHash<QString, QRect> m_appliedGeometry;
    /// windowId → COLUMN rect the daemon resolved for this window (the full
    /// tile slot before constrainToScrollSlot's centering). Drag-to-reorder
    /// (onWindowDragFinished) compares the drop position against these column
    /// edges so the anchor selection is correct for constrained-and-centered
    /// windows. Subset of m_notifiedWindows by the same invariant.
    QHash<QString, QRect> m_slotGeometry;
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
    /// Re-entrancy guard for flushReasserts. applySnapGeometry inside the
    /// flush loop emits windowFrameGeometryChanged synchronously, which
    /// re-enters onWindowFrameGeometryChanged for OTHER pending windows —
    /// without this guard a reasserting window's resize signal would let
    /// another pending window observe stale geometry and queue a redundant
    /// (or contradictory) re-assert mid-flush. Mirrors AutotileHandler::
    /// m_inOutputChanged. Manual bool + try/finally pattern (matching the
    /// autotile sister) rather than a QScopedValueRollback, so the guard's
    /// intent is visible inline.
    bool m_inFlushReasserts = false;
    /// Incremented on every daemon (re)connect. A D-Bus reply captures the
    /// epoch at call time and skips its rollback if the daemon reconnected
    /// meanwhile — onDaemonReady has already rebuilt the tracking sets.
    /// quint64 to match AutotileHandler::m_autotileStaggerGeneration — both
    /// are pure monotonic generation counters and the wider type pushes any
    /// theoretical wrap so far out it never matters in practice. Unsigned
    /// because the increment is a generation counter and unsigned wrap is
    /// well-defined, whereas signed overflow would be UB.
    quint64 m_daemonEpoch = 0;

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

    /// Auto-focus a window as it enters the scroll layout. Mirrors the
    /// ConfigDefaults::scrollFocusNewWindows() default until the real value
    /// arrives over D-Bus (daemon_bringup loadSettingAsync).
    bool m_focusNewWindows = true;
};

} // namespace PlasmaZones
