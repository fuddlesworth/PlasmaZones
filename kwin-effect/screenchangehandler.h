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
 * @brief Handles debounced screen geometry changes and window repositioning.
 *
 * Monitors virtualScreenGeometryChanged (resolution / monitor setup changes),
 * debounces rapid-fire signals, and requests updated window geometries from
 * the daemon when the screen size actually changes.
 *
 * Also handles the daemon's reapplyWindowGeometriesRequested signal.
 */
class ScreenChangeHandler : public QObject
{
    Q_OBJECT

public:
    explicit ScreenChangeHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    /// Stop the debounce timers (called from effect destructor)
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

    /// Hook @p w for work-area reporting when it is a panel (dock): connects
    /// its geometry-changed signal and schedules a report. A no-op for
    /// non-dock windows. Called for every `windowAdded` and once per
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

    /// Connected to `EffectsHandler::windowClosed` — schedules a work-area
    /// report when @p w is a panel (dock) so the strut it freed reaches the
    /// daemon. A no-op for non-dock windows.
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
};

} // namespace PlasmaZones
