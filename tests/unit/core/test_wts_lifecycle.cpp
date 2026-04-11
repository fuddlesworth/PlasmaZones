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
 * 4. Layout change -> stale assignment removal and resnap buffer
 * 5. State change signal emission
 *
 * WIRE FORMAT NOTE: These tests construct WTS without a WindowRegistry, so
 * they drive legacy-compat "appId|uuid" composite fixtures to exercise the
 * Utils::extractAppId fallback path inside currentAppIdFor(). Production
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
#include "core/layoutmanager.h"
#include "core/interfaces.h"
#include "core/layout.h"
#include "core/zone.h"
#include "core/virtualdesktopmanager.h"
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "../helpers/StubSettings.h"

using StubSettingsLifecycle = StubSettings;

// =========================================================================
// Stub Zone Detector
// =========================================================================

class StubZoneDetectorLifecycle : public IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorLifecycle(QObject* parent = nullptr)
        : IZoneDetector(parent)
    {
    }
    Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(Layout* layout) override
    {
        m_layout = layout;
    }
    ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<Zone*> expandPaintedZonesToRect(const QVector<Zone*>&) const override
    {
        return {};
    }
    void highlightZone(Zone*) override
    {
    }
    void highlightZones(const QVector<Zone*>&) override
    {
    }
    void clearHighlights() override
    {
    }

private:
    Layout* m_layout = nullptr;
};

// =========================================================================
// Helper
// =========================================================================

static Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new Zone(layout);
        qreal x = static_cast<qreal>(i) / zoneCount;
        qreal w = 1.0 / zoneCount;
        zone->setRelativeGeometry(QRectF(x, 0.0, w, 1.0));
        zone->setZoneNumber(i + 1);
        layout->addZone(zone);
    }
    return layout;
}

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
        m_layoutManager = new LayoutManager(nullptr);
        m_settings = new StubSettingsLifecycle(nullptr);
        m_zoneDetector = new StubZoneDetectorLifecycle(nullptr);
        m_service = new WindowTrackingService(m_layoutManager, m_zoneDetector, m_settings, nullptr, nullptr);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (Zone* z : m_testLayout->zones()) {
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
    // P0: Window Close -> Pending Zone Persistence
    // =====================================================================

    void testWindowClosed_persistsZoneToPending()
    {
        QString windowId = QStringLiteral("firefox|12345");
        QString appId = Utils::extractAppId(windowId);

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
        QString appId = Utils::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        m_service->setWindowFloating(windowId, true);

        m_service->windowClosed(windowId);

        QVERIFY(!m_service->pendingRestoreQueues().contains(appId));
    }

    void testWindowClosed_preTileGeometryConvertedToStableId()
    {
        QString windowId = QStringLiteral("org.kde.dolphin|99999");
        QString appId = Utils::extractAppId(windowId);

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
        QString appId = Utils::extractAppId(windowId);

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

    // =====================================================================
    // P0: Layout Change
    // =====================================================================

    void testOnLayoutChanged_staleAssignmentsRemoved()
    {
        QString windowId = QStringLiteral("app|12345");
        QString screen = QStringLiteral("DP-1");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], screen, 0);
        QVERIFY(m_service->isWindowSnapped(windowId));

        Layout* newLayout = createTestLayout(2, m_layoutManager);
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

        Layout* newLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<RotationEntry> resnap = m_service->calculateResnapFromPreviousLayout();
        // In headless mode the buffer may be empty but the call must not crash.
        // The original assertion `resnap.size() >= 0` was always true;
        // we keep this as a smoke-test and document the limitation.
        Q_UNUSED(resnap);
    }

    void testOnLayoutChanged_floatingWindowsExcludedFromResnap()
    {
        QString windowId = QStringLiteral("app|12345");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QString(), 0);
        m_service->setWindowFloating(windowId, true);

        Layout* newLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<RotationEntry> resnap = m_service->calculateResnapFromPreviousLayout();
        for (const RotationEntry& entry : resnap) {
            QVERIFY(entry.windowId != windowId);
        }
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    LayoutManager* m_layoutManager = nullptr;
    StubSettingsLifecycle* m_settings = nullptr;
    StubZoneDetectorLifecycle* m_zoneDetector = nullptr;
    WindowTrackingService* m_service = nullptr;
    Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsLifecycle)
#include "test_wts_lifecycle.moc"
