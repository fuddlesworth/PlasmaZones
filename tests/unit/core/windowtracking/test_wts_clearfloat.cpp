// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_clearfloat.cpp
 * @brief Unit tests for PhosphorPlacement::WindowTrackingService::clearFloatingForSnap() and resolveUnfloatGeometry()
 *
 * Tests cover:
 * 1. clearFloatingForSnap: returns false for non-floating, true for floating,
 *    clears preFloatZone, idempotent, empty windowId
 * 2. resolveUnfloatGeometry: found=false for no pre-float zone, for an empty
 *    windowId, and (because this suite is guiless and has no QScreen) for a
 *    snapped-then-floated window whose capture is intact but cannot be turned
 *    into a rect. The found=true path lives in test_snap_unfloat_fallback.cpp.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QRectF>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "core/utils/utils.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using namespace PlasmaZones;
using PhosphorEngine::UnfloatResult;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "helpers/StubSettings.h"

using StubSettingsClearFloat = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector
// =========================================================================

// Nothing here exercises detection — SnapEngine only stores the detector — so
// the shared inert stub does. createTestLayout comes from the same header.
#include "helpers/StubZoneDetector.h"

using StubZoneDetectorClearFloat = PlasmaZones::StubZoneDetector;

// =========================================================================
// Test Class
// =========================================================================

class TestWtsClearFloat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsClearFloat(nullptr);
        m_zoneDetector = new StubZoneDetectorClearFloat(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        m_service->setSnapState(m_engine->snapState());
        m_service->setSnapEngine(m_engine);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
    }

    void cleanup()
    {
        // Detach BOTH borrowed pointers before the engine dies so the service
        // never holds a dangling SnapEngine* (same discipline as
        // wta_convenience_fixture.h).
        m_service->setSnapState(nullptr);
        m_service->setSnapEngine(nullptr);
        delete m_engine;
        m_engine = nullptr;
        delete m_service;
        m_service = nullptr;
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

    // =====================================================================
    // clearFloatingForSnap
    // =====================================================================

    void testClearFloatingForSnap_nonFloatingReturnsFalse()
    {
        // A window that is not floating should return false
        QString windowId = QStringLiteral("firefox|11111111-0000-0000-0000-000000000001");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);

        QCOMPARE(m_service->clearFloatingForSnap(windowId), false);
        QVERIFY(!m_service->isWindowFloating(windowId));
    }

    void testClearFloatingForSnap_floatingReturnsTrueAndClears()
    {
        // A floating window should return true and no longer be floating afterward
        QString windowId = QStringLiteral("firefox|11111111-0000-0000-0000-000000000002");
        m_service->setWindowFloating(windowId, true);
        QVERIFY(m_service->isWindowFloating(windowId));

        QCOMPARE(m_service->clearFloatingForSnap(windowId), true);
        QVERIFY(!m_service->isWindowFloating(windowId));
    }

    void testClearFloatingForSnap_clearsPreFloatZone()
    {
        // After clearing, pre-float zone data should be gone
        QString windowId = QStringLiteral("dolphin|11111111-0000-0000-0000-000000000003");

        // Snap window, then float it (which saves pre-float zone via unsnapForFloat)
        m_service->assignWindowToZone(windowId, m_zoneIds[1], QStringLiteral("DP-1"), 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);

        // Verify pre-float zone was saved
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[1]);
        QVERIFY(m_service->isWindowFloating(windowId));

        // clearFloatingForSnap should clear both floating and pre-float zone
        QCOMPARE(m_service->clearFloatingForSnap(windowId), true);
        QVERIFY(!m_service->isWindowFloating(windowId));
        QVERIFY(m_service->preFloatZone(windowId).isEmpty());
        QVERIFY(m_service->preFloatZones(windowId).isEmpty());
    }

    void testClearFloatingForSnap_idempotent()
    {
        // Second call should return false since the window is no longer floating
        QString windowId = QStringLiteral("konsole|11111111-0000-0000-0000-000000000004");
        m_service->setWindowFloating(windowId, true);

        QCOMPARE(m_service->clearFloatingForSnap(windowId), true);
        QCOMPARE(m_service->clearFloatingForSnap(windowId), false);
    }

    void testClearFloatingForSnap_emptyWindowId()
    {
        // Empty windowId should return false and not crash
        QCOMPARE(m_service->clearFloatingForSnap(QString()), false);
    }

    // =====================================================================
    // resolveUnfloatGeometry
    // =====================================================================

    void testResolveUnfloatGeometry_snappedThenFloated_yieldsNothingWithoutAScreen()
    {
        // This suite is QTEST_GUILESS_MAIN, so there is no QGuiApplication and
        // therefore no QScreen. resolveUnfloatGeometry finds the pre-float zone
        // but cannot turn it into a rect, so it reports found=false and returns
        // the zone list empty. That is the whole behaviour this slot can pin:
        // the pre-float capture itself is intact, and the geometry stage fails
        // closed rather than emitting a garbage rect.
        //
        // The found==true path (a real screen resolving the zone) is covered by
        // test_snap_unfloat_fallback.cpp.
        QString windowId = QStringLiteral("firefox|22222222-0000-0000-0000-000000000001");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);

        // The pre-float capture is the input resolveUnfloatGeometry reads.
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[0]);
        QCOMPARE(m_service->preFloatZones(windowId), QStringList{m_zoneIds[0]});

        UnfloatResult result = m_engine->resolveUnfloatGeometry(windowId, QStringLiteral("DP-1"));

        QCOMPARE(result.found, false);
        QVERIFY(result.zoneIds.isEmpty());
        QVERIFY(!result.geometry.isValid());

        // Failing to resolve must not consume the capture — a later unfloat on
        // a session that does have a screen still has its home zone.
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[0]);
    }

    void testResolveUnfloatGeometry_noPreFloatZone()
    {
        // A window with no pre-float zone should return found=false
        QString windowId = QStringLiteral("dolphin|22222222-0000-0000-0000-000000000002");
        m_service->setWindowFloating(windowId, true);

        UnfloatResult result = m_engine->resolveUnfloatGeometry(windowId, QStringLiteral("DP-1"));

        QCOMPARE(result.found, false);
        QVERIFY(result.zoneIds.isEmpty());
        QVERIFY(!result.geometry.isValid());
    }

    void testResolveUnfloatGeometry_emptyWindowId()
    {
        // Empty windowId should return found=false and not crash
        UnfloatResult result = m_engine->resolveUnfloatGeometry(QString(), QStringLiteral("DP-1"));

        QCOMPARE(result.found, false);
        QVERIFY(result.zoneIds.isEmpty());
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsClearFloat* m_settings = nullptr;
    StubZoneDetectorClearFloat* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_GUILESS_MAIN(TestWtsClearFloat)
#include "test_wts_clearfloat.moc"
