// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/WindowMarshalling.h>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QTimer>

namespace KWin {
class EffectWindow;
class LogicalOutput;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/**
 * @brief Liaises screen and work-area changes between KWin and the daemon.
 *
 * Monitors virtualScreenGeometryChanged (resolution / monitor setup changes),
 * debounces rapid-fire signals, and requests updated window geometries from
 * the daemon when the screen size actually changes. Also handles the daemon's
 * reapplyWindowGeometriesRequested signal.
 *
 * Additionally reports KWin's authoritative per-screen work area
 * (`clientArea(MaximizeArea)`) to the daemon: it tracks panel (dock) windows
 * and pushes a fresh snapshot whenever a panel is added, removed, or resized,
 * or the screen layout changes — see @ref scheduleClientAreaReport.
 *
 * It also owns the settle of an output change. A window crossing outputs or
 * virtual screens while the screen change is in progress is not acted on at
 * once: the crossing is deferred, and when the change settles every window
 * KWin moved is reported to the daemon (WindowTracking.reportOutputSettle).
 * The daemon classifies each one (an evacuee of an output that went away, a
 * window KWin returned to its parked place, a placement to re-assert, or the
 * user's move) and only the user moves replay as crossings. The state of each
 * window on an output that goes away is recorded here as it goes, because
 * KWin's own restore runs right after and changes it.
 */
class ScreenChangeHandler : public QObject
{
    Q_OBJECT

public:
    explicit ScreenChangeHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    /// Stop the debounce timer and suppress any pending client-area report
    /// (called from effect destructor).
    void stop();

    /// Schedule a push of KWin's authoritative per-screen work area
    /// (`clientArea(MaximizeArea)`) to the daemon. Coalesces every call made
    /// within one event-loop turn into a single queued report — so a burst
    /// of dock add/close/resize signals (session startup, a panel-editor
    /// drag) produces one push, and that push runs after KWin's synchronous
    /// strut recompute for the turn. Safe to call before the daemon bridge
    /// is registered — the report no-ops until it is, and the daemon-ready
    /// path schedules another.
    void scheduleClientAreaReport();

    /// Hook @p w for work-area reporting when it may reserve screen-edge
    /// space: connects its geometry-changed signal and schedules a report.
    /// Matches docks AND unmovable layer-shell surfaces — a third-party
    /// shell's panel (e.g. phosphor-shell) carries an exclusive zone but is
    /// NOT isDock() to KWin, and missing it leaves the daemon's compositor
    /// work-area override stale when that panel resizes. A no-op for every
    /// other window. Called for every `windowAdded` and once per
    /// already-mapped window at effect startup so pre-existing panels are
    /// covered too.
    void trackDockWindow(KWin::EffectWindow* w);

    /// True while a screen change is pending (debounce timer running, its
    /// settle report in flight, or a reapply in progress). A crossing seen
    /// meanwhile is deferred to the settle (@ref deferCrossing).
    bool isScreenChangeInProgress() const
    {
        return m_pendingScreenChange || m_settleInFlight || m_reapplyInProgress;
    }

    /// Record the state of every window on @p output as it goes away, under
    /// the id @p screenId it was published with. Called from the effect's
    /// screen-removed handler, which runs before KWin moves the windows and
    /// restores their older state. Members of the output's strip are
    /// recorded too, wherever their parked frame sits. A record is written
    /// once per (window, output) and kept until the daemon drops the park.
    void captureEvacuees(KWin::LogicalOutput* output, const QString& screenId);
    /// An output came (back): its records are classified again at the next
    /// settle, and windows on it are not announced to the tiling engines
    /// before that (@ref holdsUnclassifiedRecord).
    void noteOutputAdded(KWin::LogicalOutput* output);
    /// Defer @p w's crossing from @p oldScreenId to the settle. A window
    /// crossing twice keeps the first old screen.
    void deferCrossing(KWin::EffectWindow* w, const QString& oldScreenId);
    /// Whether @p w is on an output it has a record for that the next settle
    /// has not classified yet. Such a window is not announced to the tiling
    /// engines: the settle re-seats it in its parked place or announces it.
    bool holdsUnclassifiedRecord(KWin::EffectWindow* w) const;
    /// @p w's announce was skipped for an unclassified record; the settle
    /// announces it unless its verdict placed it.
    void noteSkippedAnnounce(KWin::EffectWindow* w);
    /// Drop everything held for @p w (the window is gone).
    void forgetWindow(KWin::EffectWindow* w);
    /// Drop every record: the daemon that held the parks is gone.
    void dropEvacueeRecords();

    /// The body of a crossing between physical outputs, run at once outside
    /// a screen change and replayed at its settle inside one.
    void applyOutputCrossing(KWin::EffectWindow* w, const QString& oldScreenId, const QString& newScreenId);
    /// The body of a crossing between virtual screens of one output.
    void applyVirtualScreenCrossing(KWin::EffectWindow* w, const QString& oldScreenId, const QString& newScreenId);
    /// Tell the daemon @p w crossed screens on its own: windowScreenChanged
    /// when no tiling engine runs either end, windowCrossedScreens otherwise.
    void reportCrossing(KWin::EffectWindow* w, const QString& oldScreenId, const QString& newScreenId);

public Q_SLOTS:
    void slotScreenGeometryChanged();
    void slotReapplyWindowGeometriesRequested();
    /// The daemon dropped @p windowId's park for the output @p outputUuid.
    void slotParkDropped(const QString& windowId, const QString& outputUuid);

    /// Latch the screen-change-in-progress flag and take the settle's
    /// baseline as soon as KWin emits screenAdded / screenRemoved, before the
    /// per-window outputChanged signals it fires for the windows it moves can
    /// reach @ref isScreenChangeInProgress (discussion #527 follow-up). The
    /// 500 ms debounce and client-area report mirror
    /// @ref slotScreenGeometryChanged, so one settle covers both.
    void slotScreenLayoutChanged();

    /// Connected to `EffectsHandler::windowClosed` — schedules a work-area
    /// report when @p w may have reserved screen-edge space (same match as
    /// @ref trackDockWindow) so the strut it freed reaches the daemon. A
    /// no-op for every other window.
    void onWindowClosed(KWin::EffectWindow* w);

private:
    void applyScreenGeometryChange();
    /// Snap-mode only: resolves the daemon's zone-assigned windows (the
    /// keep-windows-in-zones repair after a resolution change).
    /// Scroll-managed windows are skipped at APPLY time by the
    /// isManagedScreen guard (m_managedScreens is the autotile+scrolling
    /// union), not filtered at the reply boundary; the scrolling
    /// fullscreen-exit repair goes through Scrolling.reapplyWindowGeometry
    /// instead (sent from the tiling handler's fullscreen-exit slot).
    void fetchAndApplyWindowGeometries();
    void applyWindowGeometries(const PhosphorProtocol::WindowGeometryList& geometries);

    /// Push KWin's `clientArea(MaximizeArea)` for every output to the daemon.
    /// Runs as the queued continuation scheduled by @ref scheduleClientAreaReport.
    void reportClientArea();

    /// What the settle compares: where KWin has a window and the KWin state
    /// that tells a window it moved from one the user touched.
    struct WindowSnapshot
    {
        QString outputUuid;
        QString screenId; ///< PlasmaZones physical id
        QRect frame;
        int moveResizeCount = 0;
        int maximizeMode = 0;
        int quickTileMode = 0;
        bool fullscreen = false;
        bool operator==(const WindowSnapshot&) const = default;
    };
    struct EvacueeRecord
    {
        WindowSnapshot state; ///< S0: as it was when its output went away
        bool classified = false;
    };
    using DeferredCrossings = QList<QPair<QPointer<KWin::EffectWindow>, QString>>;

    WindowSnapshot snapshotOf(KWin::EffectWindow* w) const;
    /// Snapshot every handled window as a screen change begins.
    void takeBaseline();
    /// Report the windows KWin moved during this screen change, then apply
    /// the daemon's verdicts and replay the crossings it calls user moves.
    void sendSettleReport();
    PhosphorProtocol::OutputSettleRowList buildSettleRows() const;
    void applySettleVerdicts(const PhosphorProtocol::OutputSettleVerdictList& verdicts,
                             const DeferredCrossings& crossings, const QList<QPointer<KWin::EffectWindow>>& skipped);
    /// Replay crossings deferred since the last settle, once nothing is in progress.
    void replayDeferredCrossings();
    void replayCrossing(KWin::EffectWindow* w, const QString& oldScreenId);
    /// An evacuee: it floats where KWin put it, keeping KWin's state.
    void applyEvacuee(KWin::EffectWindow* w, const QString& screenId);
    /// KWin returned it to the output it was parked for, and the daemon
    /// re-seated it there.
    void applyReadopt(KWin::EffectWindow* w, const QString& screenId);
    bool onPlaceholderOutput(KWin::EffectWindow* w) const;

    PlasmaZonesEffect* m_effect;
    QTimer m_screenChangeDebounce;
    bool m_pendingScreenChange = false;
    QRect m_lastVirtualScreenGeometry;
    bool m_reapplyInProgress = false;
    bool m_reapplyPending = false;

    // Set while a queued reportClientArea() is pending. Collapses a burst of
    // dock signals within one event-loop turn into a single report — see
    // scheduleClientAreaReport().
    bool m_clientAreaReportQueued = false;

    // Set by stop() from the effect destructor. Suppresses both new schedules
    // and an already-queued report that would otherwise fire a stray D-Bus
    // call between stop() and this handler's destruction.
    bool m_stopped = false;

    // The output-change settle. Raw window keys are dropped by forgetWindow
    // on windowDeleted, so none outlives its window.
    QHash<KWin::EffectWindow*, QHash<QString, EvacueeRecord>> m_evacueeRecords; ///< keyed by output uuid
    QHash<KWin::EffectWindow*, WindowSnapshot> m_settleBaseline;
    QHash<KWin::EffectWindow*, QString> m_deferredCrossings; ///< the first old screen
    QSet<KWin::EffectWindow*> m_skippedAnnounces;
    bool m_outputSetChanged = false;
    bool m_settleInFlight = false;
};

} // namespace PlasmaZones
