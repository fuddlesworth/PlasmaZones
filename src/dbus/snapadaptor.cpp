// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapadaptor.h"
#include "windowtrackingadaptor.h"
#include "snap/SnapEngine.h"
#include "core/logging.h"

namespace PlasmaZones {

SnapAdaptor::SnapAdaptor(SnapEngine* engine, WindowTrackingAdaptor* adaptor, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
    , m_adaptor(adaptor)
{
    if (!m_engine || !adaptor) {
        qCWarning(lcDbusWindow) << "SnapAdaptor created with null engine or adaptor";
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Signal relay: SnapEngine → WindowTrackingAdaptor D-Bus signals
    //
    // Mirrors AutotileAdaptor's constructor pattern (10 signal connections).
    // All snap navigation signals relay through org.plasmazones.WindowTracking
    // because the KWin effect listens for them on that D-Bus interface.
    // ═══════════════════════════════════════════════════════════════════════════

    // Navigation feedback (shared OSD path — both engines use the same signal)
    connect(m_engine, &SnapEngine::navigationFeedback, adaptor, &WindowTrackingAdaptor::navigationFeedback);

    // Float state changes
    connect(m_engine, &SnapEngine::windowFloatingChanged, adaptor, &WindowTrackingAdaptor::windowFloatingChanged);

    // Daemon-driven geometry application (used by autotile float restore via SnapEngine)
    connect(m_engine, &SnapEngine::applyGeometryRequested, adaptor, &WindowTrackingAdaptor::applyGeometryRequested);

    // Snap-all-windows (effect collects candidates, daemon calculates)
    connect(m_engine, &SnapEngine::snapAllWindowsRequested, adaptor, &WindowTrackingAdaptor::snapAllWindowsRequested);

    // Batched resnap: emitBatchedResnap is called from the Daemon layer (autotile→snap
    // transition) which bypasses WTA navigation methods. Route through handleBatchedResnap
    // for proper bookkeeping (windowSnapped per entry) + applyGeometriesBatch emission.
    connect(m_engine, &SnapEngine::resnapToNewLayoutRequested, adaptor, &WindowTrackingAdaptor::handleBatchedResnap);

    // Batched geometry application: rotate / resnap / snap-all paths build
    // a WindowGeometryList and emit it here. WTA's applyGeometriesBatch
    // signal is the D-Bus surface.
    connect(m_engine, &SnapEngine::applyGeometriesBatch, adaptor, &WindowTrackingAdaptor::applyGeometriesBatch);

    // Window activation: focus-in-direction and cycle operations resolve a
    // target window and ask the KWin effect to raise/focus it.
    connect(m_engine, &SnapEngine::activateWindowRequested, adaptor, &WindowTrackingAdaptor::activateWindowRequested);

    qCDebug(lcDbusWindow) << "SnapAdaptor initialized with 7 signal connections";
}

void SnapAdaptor::clearEngine()
{
    if (m_engine && m_adaptor) {
        // All connections are m_engine → m_adaptor (not m_engine → this),
        // so we must disconnect from the actual receiver.
        disconnect(m_engine, nullptr, m_adaptor, nullptr);
    }
    m_engine = nullptr;
    m_adaptor = nullptr;
}

} // namespace PlasmaZones
