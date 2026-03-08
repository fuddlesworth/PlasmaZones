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

    // Zone navigation signals (effect-first: daemon → engine → signal → effect)
    connect(m_engine, &SnapEngine::moveWindowToZoneRequested, adaptor,
            &WindowTrackingAdaptor::moveWindowToZoneRequested);
    connect(m_engine, &SnapEngine::focusWindowInZoneRequested, adaptor,
            &WindowTrackingAdaptor::focusWindowInZoneRequested);
    connect(m_engine, &SnapEngine::swapWindowsRequested, adaptor, &WindowTrackingAdaptor::swapWindowsRequested);
    connect(m_engine, &SnapEngine::rotateWindowsRequested, adaptor, &WindowTrackingAdaptor::rotateWindowsRequested);

    // Daemon-driven geometry application
    connect(m_engine, &SnapEngine::applyGeometryRequested, adaptor, &WindowTrackingAdaptor::applyGeometryRequested);

    // Layout change resnap
    connect(m_engine, &SnapEngine::resnapToNewLayoutRequested, adaptor,
            &WindowTrackingAdaptor::resnapToNewLayoutRequested);

    // In-zone cycling
    connect(m_engine, &SnapEngine::cycleWindowsInZoneRequested, adaptor,
            &WindowTrackingAdaptor::cycleWindowsInZoneRequested);

    // Snap-all-windows
    connect(m_engine, &SnapEngine::snapAllWindowsRequested, adaptor, &WindowTrackingAdaptor::snapAllWindowsRequested);

    qCDebug(lcDbusWindow) << "SnapAdaptor initialized with 10 signal connections";
}

void SnapAdaptor::clearEngine()
{
    if (m_engine && m_adaptor) {
        // All 10 connections are m_engine → m_adaptor (not m_engine → this),
        // so we must disconnect from the actual receiver.
        disconnect(m_engine, nullptr, m_adaptor, nullptr);
    }
    m_engine = nullptr;
    m_adaptor = nullptr;
}

} // namespace PlasmaZones
