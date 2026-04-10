// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <dbus_types.h>
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

    /// Stop the debounce timer (called from effect destructor)
    void stop();

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

private:
    void applyScreenGeometryChange();
    void fetchAndApplyWindowGeometries();
    void applyWindowGeometries(const WindowGeometryList& geometries);

    PlasmaZonesEffect* m_effect;
    QTimer m_screenChangeDebounce;
    bool m_pendingScreenChange = false;
    QRect m_lastVirtualScreenGeometry;
    bool m_reapplyInProgress = false;
    bool m_reapplyPending = false;
};

} // namespace PlasmaZones
