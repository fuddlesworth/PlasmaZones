// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>

namespace PlasmaZones {

class SnapEngine;
class WindowTrackingAdaptor;

/**
 * @brief Signal relay from SnapEngine to WindowTrackingAdaptor D-Bus signals
 *
 * Mirrors AutotileAdaptor's pattern: engine signals are connected in the
 * constructor, providing a single place for all SnapEngine → D-Bus wiring.
 *
 * Unlike AutotileAdaptor (which is a QDBusAbstractAdaptor owning its own
 * D-Bus interface), SnapAdaptor relays through WindowTrackingAdaptor's
 * existing org.plasmazones.WindowTracking interface. The snap navigation
 * signals (moveWindowToZoneRequested, focusWindowInZoneRequested, etc.)
 * are intrinsically part of the window tracking domain — the KWin effect
 * listens for them on that interface.
 *
 * Pattern summary (both engines follow the same shape):
 *   AutotileEngine → AutotileAdaptor → org.plasmazones.Autotile D-Bus
 *   SnapEngine     → SnapAdaptor     → org.plasmazones.WindowTracking D-Bus
 *
 * @see AutotileAdaptor, SnapEngine, WindowTrackingAdaptor
 */
class PLASMAZONES_EXPORT SnapAdaptor : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct a SnapAdaptor
     *
     * Connects all SnapEngine signals to the corresponding
     * WindowTrackingAdaptor signals for D-Bus relay.
     *
     * @param engine SnapEngine to relay signals from (not owned)
     * @param adaptor WindowTrackingAdaptor to relay signals to (not owned)
     * @param parent Parent QObject (typically the daemon)
     */
    explicit SnapAdaptor(SnapEngine* engine, WindowTrackingAdaptor* adaptor, QObject* parent = nullptr);
    ~SnapAdaptor() override = default;

    /**
     * @brief Clear the engine pointer during shutdown
     *
     * Disconnects all signals. Mirrors AutotileAdaptor::clearEngine().
     * Called by Daemon::stop() before the SnapEngine unique_ptr is reset.
     */
    void clearEngine();

private:
    SnapEngine* m_engine = nullptr;
    WindowTrackingAdaptor* m_adaptor = nullptr;
};

} // namespace PlasmaZones
