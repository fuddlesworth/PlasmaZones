// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include "core/platform/logging.h"

#include <QPointer>

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

    m_connections.reserve(11);

    // Navigation feedback (shared OSD path — both engines use the same signal)
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::navigationFeedback, adaptor,
                                 &WindowTrackingAdaptor::navigationFeedback));

    // Float state changes — through the relay chokepoint, never
    // signal-to-signal: a direct emission bypasses m_broadcastFloating and
    // desyncs the syncWindowFloating dedup gate (see relayWindowFloatingChanged).
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::windowFloatingChanged, adaptor,
                                 &WindowTrackingAdaptor::relayWindowFloatingChanged));

    // Daemon-driven geometry application, each with its placement purpose.
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::applyGeometryRequested, adaptor,
                                 [adaptor](const QString& windowId, int x, int y, int width, int height,
                                           const QString& zoneId, const QString& screenId, bool sizeOnly) {
                                     Q_EMIT adaptor->applyGeometryRequested(
                                         windowId, x, y, width, height, zoneId, screenId, sizeOnly,
                                         static_cast<int>(PhosphorProtocol::PlacementPurpose::UserVerb));
                                 }));
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::fullscreenHandBackRequested, adaptor,
                                 &WindowTrackingAdaptor::fullscreenHandBackRequested));
    // A re-statement is judged by the effect against the managed set it last
    // heard, like a batch (a maximize is kept only on a screen it does not
    // manage), and the desktop-switch pass states windows in the same turn
    // that queues the announce, so it is sequenced like the batches.
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::restatementGeometryRequested, this,
                                 [this](const QString& windowId, int x, int y, int width, int height,
                                        const QString& zoneId, const QString& screenId) {
                                     emitRestatement(windowId, QRect(x, y, width, height), zoneId, screenId);
                                 }));

    // Snap-all-windows (effect collects candidates, daemon calculates)
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::snapAllWindowsRequested, adaptor,
                                 &WindowTrackingAdaptor::snapAllWindowsRequested));

    // Batched resnap: the engine's own batches (emitBatchedResnap, the
    // autotile→snap transition, the desktop carry) are committed by the relay
    // as they are. The bus slot of the same shape, handleBatchedResnap, checks
    // an external caller's entries first.
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::resnapToNewLayoutRequested, this,
                                 &SnapAdaptor::applyEngineResnap));

    // Batched geometry application: rotate / resnap / snap-all paths build
    // a PhosphorProtocol::WindowGeometryList and emit it here. WTA's applyGeometriesBatch
    // signal is the D-Bus surface, reached in the same order as every other
    // snap batch (emitPlacementBatch).
    m_connections.append(connect(m_engine, &PhosphorSnapEngine::SnapEngine::applyGeometriesBatch, this,
                                 [this](const PhosphorProtocol::WindowGeometryList& geometries, const QString& action) {
                                     emitPlacementBatch(geometries, action);
                                 }));

    // Any placement of a window that reaches the wire supersedes the one held
    // for it behind the announce: sent after it, the held one would put the
    // window back where the newer command moved it from. Both of the
    // WindowTracking placement signals, whoever emits them.
    m_connections.append(
        connect(adaptor, &WindowTrackingAdaptor::applyGeometryRequested, this, [this](const QString& windowId) {
            supersedeHeldPlacements({windowId});
        }));
    m_connections.append(connect(adaptor, &WindowTrackingAdaptor::applyGeometriesBatch, this,
                                 [this](const PhosphorProtocol::WindowGeometryList& geometries) {
                                     QStringList windowIds;
                                     windowIds.reserve(geometries.size());
                                     for (const PhosphorProtocol::WindowGeometryEntry& entry : geometries) {
                                         windowIds.append(entry.windowId);
                                     }
                                     supersedeHeldPlacements(windowIds);
                                 }));

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
    // The sequencer borrows the TilingAdaptor, which clears its own held work
    // with the announce it voids; what this adaptor still holds names the
    // dead session's windows.
    m_announceSequencer = {};
    m_heldPlacementSerial.clear();
}

quint64 SnapAdaptor::holdPlacement(const QStringList& windowIds)
{
    const quint64 serial = ++m_lastHeldSerial;
    for (const QString& windowId : windowIds) {
        m_heldPlacementSerial.insert(windowId, serial);
    }
    return serial;
}

QSet<QString> SnapAdaptor::releaseHeldPlacement(const QStringList& windowIds, quint64 serial)
{
    QSet<QString> current;
    for (const QString& windowId : windowIds) {
        if (m_heldPlacementSerial.value(windowId) == serial) {
            current.insert(windowId);
        }
    }
    for (const QString& windowId : std::as_const(current)) {
        m_heldPlacementSerial.remove(windowId);
    }
    return current;
}

void SnapAdaptor::supersedeHeldPlacements(const QStringList& windowIds)
{
    if (m_heldPlacementSerial.isEmpty()) {
        return;
    }
    for (const QString& windowId : windowIds) {
        m_heldPlacementSerial.remove(windowId);
    }
}

void SnapAdaptor::emitPlacementBatch(PhosphorProtocol::WindowGeometryList geometries, const QString& action)
{
    if (!m_adaptor) {
        return;
    }
    // An empty batch places nothing and so has nothing to be judged against.
    if (!m_announceSequencer || geometries.isEmpty()) {
        Q_EMIT m_adaptor->applyGeometriesBatch(geometries, action);
        return;
    }
    QStringList windowIds;
    windowIds.reserve(geometries.size());
    for (const PhosphorProtocol::WindowGeometryEntry& entry : std::as_const(geometries)) {
        windowIds.append(entry.windowId);
    }
    const quint64 serial = holdPlacement(windowIds);
    m_announceSequencer(
        [guard = QPointer<SnapAdaptor>(this), geometries = std::move(geometries), windowIds, action, serial]() {
            if (!guard || !guard->m_adaptor) {
                return;
            }
            const QSet<QString> current = guard->releaseHeldPlacement(windowIds, serial);
            PhosphorProtocol::WindowGeometryList still;
            still.reserve(current.size());
            for (const PhosphorProtocol::WindowGeometryEntry& entry : geometries) {
                if (current.contains(entry.windowId)) {
                    still.append(entry);
                }
            }
            if (still.size() < geometries.size()) {
                qCDebug(lcDbusWindow) << "applyGeometriesBatch:" << geometries.size() - still.size()
                                      << "held placement(s) superseded before the announce went out";
            }
            if (!still.isEmpty()) {
                Q_EMIT guard->m_adaptor->applyGeometriesBatch(still, action);
            }
        });
}

void SnapAdaptor::emitRestatement(const QString& windowId, const QRect& geometry, const QString& zoneId,
                                  const QString& screenId)
{
    if (!m_adaptor) {
        return;
    }
    const auto emitNow = [](WindowTrackingAdaptor* adaptor, const QString& windowId, const QRect& geometry,
                            const QString& zoneId, const QString& screenId) {
        Q_EMIT adaptor->applyGeometryRequested(windowId, geometry.x(), geometry.y(), geometry.width(),
                                               geometry.height(), zoneId, screenId, false,
                                               static_cast<int>(PhosphorProtocol::PlacementPurpose::Restatement));
    };
    if (!m_announceSequencer) {
        emitNow(m_adaptor, windowId, geometry, zoneId, screenId);
        return;
    }
    const quint64 serial = holdPlacement({windowId});
    m_announceSequencer([guard = QPointer<SnapAdaptor>(this), emitNow, windowId, geometry, zoneId, screenId, serial]() {
        if (!guard || !guard->m_adaptor || guard->releaseHeldPlacement({windowId}, serial).isEmpty()) {
            return;
        }
        emitNow(guard->m_adaptor, windowId, geometry, zoneId, screenId);
    });
}

} // namespace PlasmaZones
