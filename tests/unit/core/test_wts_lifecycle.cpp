// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_lifecycle.cpp
 * @brief Unit tests for WindowTrackingService lifecycle: windowClosed and onLayoutChanged
 *
 * Tests cover:
 * 1. Window close -> pending zone persistence (P0 crash/data-loss)
 * 2. Pre-snap geometry stable ID migration on close
 * 3. Pre-float zone conversion on close
 * 4. PhosphorZones::Layout change -> stale assignment removal and resnap buffer
 * 5. State change signal emission
 *
 * WIRE FORMAT NOTE: These tests construct WTS without a WindowRegistry, so
 * they drive legacy-compat "appId|uuid" composite fixtures to exercise the
 * PhosphorIdentity::WindowId::extractAppId fallback path inside currentAppIdFor(). Production
 * daemons set a registry and receive bare instance ids — see
 * test_wts_registry_integration.cpp and test_wta_reactive_metadata.cpp for
 * coverage of the live path.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QSignalSpy>
#include <QRectF>
#include <memory>

#include "core/windowtrackingservice.h"
#include <PhosphorZones/LayoutRegistry.h>
#include "config/configbackends.h"
#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/virtualdesktopmanager.h"
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"

#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

using StubSettingsLifecycle = StubSettings;

// =========================================================================
// Test Class
// =========================================================================

class TestWtsLifecycle : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        // Pass nullptr as parent to avoid double-delete: cleanup() deletes manually
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsLifecycle(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_service = new WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr);

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
    // P0: Window Close -> Pending PhosphorZones::Zone Persistence
    // =====================================================================

    void testWindowClosed_persistsZoneToPending()
    {
        QString windowId = QStringLiteral("firefox|12345");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        QVERIFY(m_service->isWindowSnapped(windowId));

        m_service->windowClosed(windowId);

        QVERIFY(!m_service->isWindowSnapped(windowId));
        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().zoneIds.first(), m_zoneIds[0]);
    }

    void testWindowClosed_floatingWindowNotPersisted()
    {
        QString windowId = QStringLiteral("firefox|12345");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        m_service->setWindowFloating(windowId, true);

        m_service->windowClosed(windowId);

        QVERIFY(!m_service->pendingRestoreQueues().contains(appId));
    }

    void testWindowClosed_preTileGeometryConvertedToStableId()
    {
        QString windowId = QStringLiteral("org.kde.dolphin|99999");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->storePreTileGeometry(windowId, QRect(100, 200, 800, 600));
        QVERIFY(m_service->hasPreTileGeometry(windowId));

        m_service->windowClosed(windowId);

        QVERIFY(m_service->hasPreTileGeometry(appId));
        auto geo = m_service->preTileGeometry(appId);
        QVERIFY(geo.has_value());
        QCOMPARE(geo->x(), 100);
        QCOMPARE(geo->width(), 800);
    }

    void testWindowClosed_floatStateClearedOnClose()
    {
        QString windowId = QStringLiteral("org.kde.kate|55555");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[1], QStringLiteral("DP-1"), 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);

        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[1]);
        QVERIFY(m_service->isWindowFloating(windowId));

        m_service->windowClosed(windowId);

        // Float state and pre-float zones should be fully cleared on close
        QVERIFY(!m_service->isWindowFloating(windowId));
        QVERIFY(!m_service->isWindowFloating(appId));
        QVERIFY(m_service->preFloatZone(appId).isEmpty());
    }

    void testWindowClosed_scheduleSaveStateCalled()
    {
        QString windowId = QStringLiteral("app|12345");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);

        QSignalSpy spy(m_service, &WindowTrackingService::stateChanged);
        m_service->windowClosed(windowId);

        QVERIFY(spy.count() >= 1);
    }

    void testWindowClosed_persistsZoneToPending_virtualScreen()
    {
        // Same as testWindowClosed_persistsZoneToPending but using a virtual screen ID.
        // Verifies that the pending restore queue entry records the virtual screen ID
        // rather than falling back to the physical screen ID.
        const QString windowId = QStringLiteral("konsole|abcdef12-0000-0000-0000-000000000001");
        const QString vsId = QStringLiteral("DP-1/vs:0");
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[1], vsId, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));
        QCOMPARE(m_service->zoneForWindow(windowId), m_zoneIds[1]);

        m_service->windowClosed(windowId);

        QVERIFY(!m_service->isWindowSnapped(windowId));
        QVERIFY(m_service->pendingRestoreQueues().contains(appId));

        const auto& queue = m_service->pendingRestoreQueues().value(appId);
        QVERIFY(!queue.isEmpty());

        const auto& entry = queue.first();
        QCOMPARE(entry.zoneIds.first(), m_zoneIds[1]);
        QCOMPARE(entry.screenId, vsId);
    }

    // =====================================================================
    // P0: PhosphorZones::Layout Change
    // =====================================================================

    void testOnLayoutChanged_staleAssignmentsRemoved()
    {
        QString windowId = QStringLiteral("app|12345");
        QString screen = QStringLiteral("DP-1");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], screen, 0);
        QVERIFY(m_service->isWindowSnapped(windowId));

        PhosphorZones::Layout* newLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->assignLayout(screen, m_layoutManager->currentVirtualDesktop(), QString(), newLayout);
        m_layoutManager->setActiveLayout(newLayout);

        m_service->onLayoutChanged();

        QVERIFY(!m_service->isWindowSnapped(windowId));
    }

    void testOnLayoutChanged_resnapBufferPopulated()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_service->assignWindowToZone(window1, m_zoneIds[0], QString(), 0);
        m_service->assignWindowToZone(window2, m_zoneIds[1], QString(), 0);

        PhosphorZones::Layout* newLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<ZoneAssignmentEntry> resnap = m_service->calculateResnapFromPreviousLayout();
        // Two windows were assigned above, so the resnap buffer should contain
        // entries for both (mapped to the new layout's zones by relative position).
        // In headless mode zone geometry resolution may differ, but the buffer
        // must still be populated with the window IDs that were snapped.
        QVERIFY2(!resnap.isEmpty(), "Resnap buffer must contain entries for the previously-snapped windows");
        QCOMPARE(resnap.size(), 2);
    }

    void testOnLayoutChanged_floatingWindowsExcludedFromResnap()
    {
        QString windowId = QStringLiteral("app|12345");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QString(), 0);
        m_service->setWindowFloating(windowId, true);

        PhosphorZones::Layout* newLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<ZoneAssignmentEntry> resnap = m_service->calculateResnapFromPreviousLayout();
        for (const ZoneAssignmentEntry& entry : resnap) {
            QVERIFY(entry.windowId != windowId);
        }
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsLifecycle* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    WindowTrackingService* m_service = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsLifecycle)
#include "test_wts_lifecycle.moc"
