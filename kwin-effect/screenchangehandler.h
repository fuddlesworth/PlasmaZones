// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

public Q_SLOTS:
    void slotScreenGeometryChanged();
    void slotReapplyWindowGeometriesRequested();

private:
    void applyScreenGeometryChange();
    void fetchAndApplyWindowGeometries();
    void applyWindowGeometriesFromJson(const QString& geometriesJson);

    PlasmaZonesEffect* m_effect;
    QTimer m_screenChangeDebounce;
    bool m_pendingScreenChange = false;
    QRect m_lastVirtualScreenGeometry;
    bool m_reapplyInProgress = false;
    bool m_reapplyPending = false;
};

} // namespace PlasmaZones
