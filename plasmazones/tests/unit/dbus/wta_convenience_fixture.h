// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file wta_convenience_fixture.h
 * @brief Shared fixture for the WindowTrackingAdaptor convenience-surface
 *        tests, split across test_wta_convenience.cpp (window-state and
 *        float-restore surface), test_wta_routing.cpp (open routing and
 *        cross-mode handoff), test_wta_screen_changed.cpp (the screen-report
 *        keep/unsnap arm) and test_compositor_bridge.cpp to stay under the
 *        file-size ceiling. Plain
 *        (non-QObject) base so each test class keeps QObject first; the
 *        derived class forwards its init()/cleanup() slots here.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QRectF>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "dbus/windowtrackingadaptor/internal.h"
#include <PhosphorScreens/Manager.h>
#include "FakeScreenProvider.h"
#include "core/interfaces/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorRules/RuleStore.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorZones/AssignmentEntry.h>
#include "config/configdefaults.h"
#include <QUuid>
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "helpers/StubSettings.h"

using StubSettingsConvenience = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector
// =========================================================================

// The shared helper is the same inert placeholder this fixture used to spell
// out: WindowTrackingAdaptor only null-checks the detector and SnapEngine only
// stores it, so nothing here ever calls a detection method. createTestLayout
// comes from the same header.
#include "helpers/StubZoneDetector.h"

using StubZoneDetectorConvenience = PlasmaZones::StubZoneDetector;

// =========================================================================
// Test Class
// =========================================================================

class WtaConvenienceFixture
{
protected:
    void initFixture()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsConvenience(nullptr);
        m_zoneDetector = new StubZoneDetectorConvenience(nullptr);

        // WTA needs a parent QObject for QDBusAbstractAdaptor
        m_parent = new QObject(nullptr);
        m_wta =
            new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr, m_parent);

        m_snapEngine = new SnapEngine(m_layoutManager, m_wta->service(), m_zoneDetector, nullptr, nullptr);
        m_snapEngine->setEngineSettings(m_settings);
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        m_wta->service()->setSnapEngine(m_snapEngine);
        m_wta->setEngines(m_snapEngine, nullptr, nullptr);

        m_snapAdaptor = new SnapAdaptor(m_snapEngine, m_wta, m_settings, m_parent);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }

        m_screenId = QStringLiteral("DP-1");
    }

    void cleanupFixture()
    {
        // SnapAdaptor is owned by m_parent (QDBusAbstractAdaptor parent)
        // Clear engine before deleting to disconnect signals
        if (m_snapAdaptor) {
            m_snapAdaptor->clearEngine();
        }
        m_snapAdaptor = nullptr;
        // WTA is owned by m_parent (QDBusAbstractAdaptor parent). Detach the
        // borrowed engine from the service BEFORE deleting it so the service
        // never holds a dangling SnapEngine* (the local-WTA tests below detach
        // symmetrically).
        m_wta->service()->setSnapState(nullptr);
        m_wta->service()->setSnapEngine(nullptr);
        delete m_snapEngine;
        m_snapEngine = nullptr;
        delete m_parent;
        m_parent = nullptr;
        m_wta = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_testLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsConvenience* m_settings = nullptr;
    StubZoneDetectorConvenience* m_zoneDetector = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
    SnapAdaptor* m_snapAdaptor = nullptr;
    SnapEngine* m_snapEngine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QString m_screenId;
};
