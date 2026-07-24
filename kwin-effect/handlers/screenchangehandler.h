// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/WindowMarshalling.h>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QTimer>

namespace KWin {
class EffectWindow;
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

    /// True while a screen geometry change is pending (debounce timer running
    /// or reapply in progress). Used to suppress false windowScreenChanged
    /// D-Bus calls during monitor connect/disconnect/standby transitions.
    bool isScreenChangeInProgress() const
    {
        return m_pendingScreenChange || m_reapplyInProgress;
    }

public Q_SLOTS:
    void slotScreenGeometryChanged();
    void slotReapplyWindowGeometriesRequested();

    /// Latch the screen-change-in-progress flag as soon as KWin emits
    /// screenAdded / screenRemoved, before the per-window outputChanged
    /// signals it fires for windows reassigned by the layout change can
    /// reach @ref isScreenChangeInProgress checks elsewhere. Closes the
    /// race between outputChanged and virtualScreenGeometryChanged
    /// (discussion #527 follow-up). The 500 ms debounce + client-area
    /// report scheduling mirror @ref slotScreenGeometryChanged so the
    /// usual settle path still runs once virtualScreenGeometryChanged
    /// catches up.
    void slotScreenLayoutChanged();

    /// Connected to `EffectsHandler::windowClosed` — schedules a work-area
    /// report when @p w may have reserved screen-edge space (same match as
    /// @ref trackDockWindow) so the strut it freed reaches the daemon. A
    /// no-op for every other window.
    void onWindowClosed(KWin::EffectWindow* w);

private:
    void applyScreenGeometryChange();
    void fetchAndApplyWindowGeometries();
    void applyWindowGeometries(const PhosphorProtocol::WindowGeometryList& geometries);

    /// Push KWin's `clientArea(MaximizeArea)` for every output to the daemon.
    /// Runs as the queued continuation scheduled by @ref scheduleClientAreaReport.
    void reportClientArea();

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
};

} // namespace PlasmaZones
