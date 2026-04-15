// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>

namespace PlasmaZones {

class SnapEngine;
class WindowTrackingAdaptor;

/**
 * @brief Signal relay from SnapEngine to WindowTrackingAdaptor
 *
 * Connects SnapEngine's remaining signals (navigationFeedback, windowFloatingChanged,
 * applyGeometryRequested, resnapToNewLayoutRequested, snapAllWindowsRequested) to
 * the WTA for D-Bus relay to the KWin effect.
 *
 * Navigation operations (move, focus, swap, rotate, cycle, snap-by-number) are now
 * daemon-driven: the Daemon routes through WTA methods directly, which compute
 * geometry internally and emit applyGeometryRequested / activateWindowRequested.
 * The SnapEngine's IEngineLifecycle interface is narrowed to lifecycle-only
 * methods — navigation is not part of the interface (see iwindowengine.h).
 *
 * @see SnapEngine, WindowTrackingAdaptor
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
