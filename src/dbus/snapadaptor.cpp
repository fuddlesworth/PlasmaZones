// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapadaptor.h"
#include "windowtrackingadaptor.h"
#include "snap/SnapEngine.h"
#include "core/logging.h"

namespace PlasmaZones {

SnapAdaptor::SnapAdaptor(SnapEngine* engine, WindowTrackingAdaptor* adaptor, ISettings* settings, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_engine(engine)
    , m_adaptor(adaptor)
    , m_settings(settings)
{
    if (!m_engine || !adaptor) {
        qCWarning(lcDbusWindow) << "SnapAdaptor created with null engine or adaptor";
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Signal relay: SnapEngine → WindowTrackingAdaptor D-Bus signals
    //
    // Each connection handle is stored in m_connections so clearEngine()
    // can disconnect exactly what this class wired — not a broad
    // disconnect(sender, receiver) that could nuke connections set up
    // elsewhere (e.g. a future direct WTA→SnapEngine observer).
    //
    // All snap navigation signals relay through org.plasmazones.WindowTracking
    // because the KWin effect listens for them on that D-Bus interface.
    // ═══════════════════════════════════════════════════════════════════════════

    m_connections.reserve(7);

    // Navigation feedback (shared OSD path — both engines use the same signal)
    m_connections.append(
        connect(m_engine, &SnapEngine::navigationFeedback, adaptor, &WindowTrackingAdaptor::navigationFeedback));

    // Float state changes
    m_connections.append(
        connect(m_engine, &SnapEngine::windowFloatingChanged, adaptor, &WindowTrackingAdaptor::windowFloatingChanged));

    // Daemon-driven geometry application (used by autotile float restore via SnapEngine)
    m_connections.append(connect(m_engine, &SnapEngine::applyGeometryRequested, adaptor,
                                 &WindowTrackingAdaptor::applyGeometryRequested));

    // Snap-all-windows (effect collects candidates, daemon calculates)
    m_connections.append(connect(m_engine, &SnapEngine::snapAllWindowsRequested, adaptor,
                                 &WindowTrackingAdaptor::snapAllWindowsRequested));

    // Batched resnap: emitBatchedResnap is called from the Daemon layer (autotile→snap
    // transition) which bypasses WTA navigation methods. Route through handleBatchedResnap
    // for proper bookkeeping (windowSnapped per entry) + applyGeometriesBatch emission.
    m_connections.append(
        connect(m_engine, &SnapEngine::resnapToNewLayoutRequested, this, &SnapAdaptor::handleBatchedResnap));

    // Batched geometry application: rotate / resnap / snap-all paths build
    // a WindowGeometryList and emit it here. WTA's applyGeometriesBatch
    // signal is the D-Bus surface.
    m_connections.append(
        connect(m_engine, &SnapEngine::applyGeometriesBatch, adaptor, &WindowTrackingAdaptor::applyGeometriesBatch));

    // Window activation: focus-in-direction and cycle operations resolve a
    // target window and ask the KWin effect to raise/focus it.
    m_connections.append(connect(m_engine, &SnapEngine::activateWindowRequested, adaptor,
                                 &WindowTrackingAdaptor::activateWindowRequested));

    qCDebug(lcDbusWindow) << "SnapAdaptor initialized with" << m_connections.size() << "signal connections";
}

void SnapAdaptor::clearEngine()
{
    // Targeted disconnects — removes only the connections this class
    // wired in the constructor. Safe to call even if engine/adaptor
    // are already destroyed; QMetaObject::Connection handles invalid
    // connections gracefully.
    for (const auto& conn : std::as_const(m_connections)) {
        QObject::disconnect(conn);
    }
    m_connections.clear();
    m_engine = nullptr;
    m_adaptor = nullptr;
}

void SnapAdaptor::setScreenModeRouter(ScreenModeRouter* router)
{
    m_screenModeRouter = router;
}

SnapEngine* SnapAdaptor::engine() const
{
    return m_engine;
}

} // namespace PlasmaZones
