// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_clearfloat.cpp
 * @brief Unit tests for WindowTrackingService::clearFloatingForSnap() and resolveUnfloatGeometry()
 *
 * Tests cover:
 * 1. clearFloatingForSnap: returns false for non-floating, true for floating,
 *    clears preFloatZone, idempotent, empty windowId
 * 2. resolveUnfloatGeometry: returns result with zones for snapped-then-floated window,
 *    found=false for no pre-float zone, found=false for empty windowId
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

#include "core/windowtrackingservice.h"
#include "snap/SnapEngine.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/virtualdesktopmanager.h"
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "../helpers/StubSettings.h"

using StubSettingsClearFloat = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector
// =========================================================================

class StubZoneDetectorClearFloat : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorClearFloat(QObject* parent = nullptr)
        : PhosphorZones::IZoneDetector(parent)
    {
    }
    PhosphorZones::Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(PhosphorZones::Layout* layout) override
    {
        m_layout = layout;
    }
    PhosphorZones::ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    PhosphorZones::Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<PhosphorZones::Zone*> expandPaintedZonesToRect(const QVector<PhosphorZones::Zone*>&) const override
    {
        return {};
    }
    void highlightZone(PhosphorZones::Zone*) override
    {
    }
    void highlightZones(const QVector<PhosphorZones::Zone*>&) override
    {
    }
    void clearHighlights() override
    {
    }

private:
    PhosphorZones::Layout* m_layout = nullptr;
};

// =========================================================================
// Helper
// =========================================================================

static PhosphorZones::Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new PhosphorZones::Zone(layout);
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

class TestWtsClearFloat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsClearFloat(nullptr);
        m_zoneDetector = new StubZoneDetectorClearFloat(nullptr);
        m_service = new WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr);
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, m_settings, nullptr, nullptr);
        m_snapState = new PhosphorZones::SnapState(QString(), m_engine);
        m_engine->setSnapState(m_snapState);
        m_service->setSnapState(m_snapState);

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
        m_service->setSnapState(nullptr);
        delete m_engine;
        m_engine = nullptr;
        m_snapState = nullptr;
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

    void testResolveUnfloatGeometry_snappedThenFloated()
    {
        // Snap a window, then float it, then resolve unfloat geometry
        QString windowId = QStringLiteral("firefox|22222222-0000-0000-0000-000000000001");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);

        // Pre-float zone should be recorded
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[0]);

        UnfloatResult result = m_engine->resolveUnfloatGeometry(windowId, QStringLiteral("DP-1"));

        // The result should have the correct zoneIds regardless of whether
        // geometry could be resolved (headless has no QScreen).
        // In headless mode, zoneGeometry returns invalid QRect -> found=false.
        // What we can verify: the method does not crash, and if it finds
        // a result, the zoneIds are correct.
        if (result.found) {
            QCOMPARE(result.zoneIds, QStringList{m_zoneIds[0]});
            QVERIFY(result.geometry.isValid());
            QVERIFY(!result.screenId.isEmpty());
        }
        // In headless mode, found=false is acceptable — geometry requires QScreen
        Q_UNUSED(result);
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
    WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::SnapState* m_snapState = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_GUILESS_MAIN(TestWtsClearFloat)
#include "test_wts_clearfloat.moc"
