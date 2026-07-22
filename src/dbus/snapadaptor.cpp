// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapadaptor.h"
#include "windowtrackingadaptor.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include "core/platform/logging.h"

namespace PlasmaZones {

SnapAdaptor::SnapAdaptor(PhosphorSnapEngine::SnapEngine* engine, WindowTrackingAdaptor* adaptor, ISettings* settings,
                         QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_engine(engine)
    , m_adaptor(adaptor)
    , m_settings(settings)
{
    // Engine + adaptor + settings are mandatory — a misordered Daemon
    // wiring that constructs SnapAdaptor before its dependencies would
    // otherwise produce a silently-no-op D-Bus object that swallows every
    // method call. qFatal aborts unambiguously in both debug and release
    // builds, matching the sibling WindowTrackingAdaptor's defensive
    // pattern — a wiring regression is loud and immediate at construction,
    // not at the first D-Bus call.
    if (!m_engine || !m_adaptor || !m_settings) {
        qFatal(
            "SnapAdaptor: null dependency at construction "
            "(engine=%p, adaptor=%p, settings=%p) — daemon-wiring bug",
            static_cast<void*>(m_engine), static_cast<void*>(m_adaptor), static_cast<void*>(m_settings));
        return; // QMessageLogger::fatal IS [[noreturn]] on every non-MSVC build via
                // Q_NORETURN (see qlogging.h / qcompilerdetection.h), but MSVC builds
                // skip the annotation. Keep the explicit return so the dead-store
                // contract is encoded at the source level on every compiler and any
                // future change that turns one of these deps into a recoverable
                // optional can't silently fall through into the signal-wiring block
                // below with null pointers.
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
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::navigationFeedback, adaptor,
                                 &WindowTrackingAdaptor::navigationFeedback));

    // Float state changes — through the relay chokepoint, never
    // signal-to-signal: a direct emission bypasses m_broadcastFloating and
    // desyncs the setWindowFloating dedup gate (see relayWindowFloatingChanged).
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::windowFloatingChanged, adaptor,
                                 &WindowTrackingAdaptor::relayWindowFloatingChanged));

    // Daemon-driven geometry application (used by autotile float restore via SnapEngine)
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::applyGeometryRequested, adaptor,
                                 &WindowTrackingAdaptor::applyGeometryRequested));

    // Snap-all-windows (effect collects candidates, daemon calculates)
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::snapAllWindowsRequested, adaptor,
                                 &WindowTrackingAdaptor::snapAllWindowsRequested));

    // Batched resnap: emitBatchedResnap is called from the Daemon layer (autotile→snap
    // transition) which bypasses WTA navigation methods. Route through handleBatchedResnap
    // for proper bookkeeping (windowSnapped per entry) + applyGeometriesBatch emission.
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::resnapToNewLayoutRequested, this,
                                 &SnapAdaptor::handleBatchedResnap));

    // Batched geometry application: rotate / resnap / snap-all paths build
    // a PhosphorProtocol::WindowGeometryList and emit it here. WTA's applyGeometriesBatch
    // signal is the D-Bus surface.
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::applyGeometriesBatch, adaptor,
                                 &WindowTrackingAdaptor::applyGeometriesBatch));

    // Window activation: focus-in-direction and cycle operations resolve a
    // target window and ask the KWin effect to raise/focus it.
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::activateWindowRequested, adaptor,
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
    m_settings = nullptr;
    // Clear the late-bound context resolver too — symmetric with the
    // other late-bound deps above. A late-arriving D-Bus call that landed
    // between clearEngine() and ~SnapAdaptor would otherwise dereference
    // a possibly-stale pointer; the existing per-slot null guards now
    // catch the cleared state instead.
    m_contextResolver = nullptr;
}

PhosphorSnapEngine::SnapEngine* SnapAdaptor::engine() const
{
    return m_engine;
}

} // namespace PlasmaZones
