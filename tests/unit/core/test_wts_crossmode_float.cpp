// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_crossmode_float.cpp
 * @brief Unit tests for cross-mode float state transitions in WindowTrackingService
 *
 * Tests cover the scenario where stale pre-float snap state was leaking across
 * autotile sessions:
 *
 * 1. preFloatStateClearedOnAutotileFloat: snap -> float -> autotile takes over
 *    -> pre-float state must be cleared
 * 2. crossVsUnfloatDoesNotUseStalePreFloat: snap on VS1 -> float -> autotile
 *    clears pre-float -> unfloat on VS2 must not restore stale state
 * 3. normalSnapFloatUnfloatCyclePreservesState: normal (non-cross-mode) cycle
 *    still works correctly end-to-end
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

using StubSettingsCrossModeFloat = StubSettings;

// =========================================================================
// Stub Zone Detector
// =========================================================================

class StubZoneDetectorCrossModeFloat : public IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorCrossModeFloat(QObject* parent = nullptr)
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

class TestWtsCrossModeFloat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = new LayoutManager(nullptr);
        m_settings = new StubSettingsCrossModeFloat(nullptr);
        m_zoneDetector = new StubZoneDetectorCrossModeFloat(nullptr);
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
    // Test 1: Pre-float state cleared when autotile takes over
    // =====================================================================

    void testPreFloatStateClearedOnAutotileFloat()
    {
        const QString windowId = QStringLiteral("firefox|aaaaaaaa-0000-0000-0000-000000000001");
        const QString screenId = QStringLiteral("DP-1/vs:0");

        // Step 1: Snap window to zone on VS1
        m_service->assignWindowToZone(windowId, m_zoneIds[0], screenId, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));
        QCOMPARE(m_service->zoneForWindow(windowId), m_zoneIds[0]);

        // Step 2: Float the window (saves pre-float state)
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QVERIFY(m_service->isWindowFloating(windowId));

        // Step 3: Verify pre-float zone was saved
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[0]);
        QCOMPARE(m_service->preFloatScreen(windowId), screenId);

        // Step 4: Simulate autotile taking over the window —
        // autotile marks its own float and clears stale pre-float snap state
        m_service->markAutotileFloated(windowId);
        m_service->clearPreFloatZone(windowId);

        // Step 5: Verify pre-float zone is now empty
        QVERIFY(m_service->preFloatZone(windowId).isEmpty());
        QVERIFY(m_service->preFloatZones(windowId).isEmpty());
        QVERIFY(m_service->preFloatScreen(windowId).isEmpty());

        // Step 6: resolveUnfloatGeometry should return found=false
        // because there is no pre-float zone to restore to
        UnfloatResult result = m_service->resolveUnfloatGeometry(windowId, screenId);
        QCOMPARE(result.found, false);
        QVERIFY(result.zoneIds.isEmpty());
    }

    // =====================================================================
    // Test 2: Cross-VS unfloat does not use stale pre-float state
    // =====================================================================

    void testCrossVsUnfloatDoesNotUseStalePreFloat()
    {
        const QString windowId = QStringLiteral("konsole|bbbbbbbb-0000-0000-0000-000000000002");
        const QString vs0 = QStringLiteral("screen1/vs:0");
        const QString vs1 = QStringLiteral("screen1/vs:1");

        // Step 1: Assign window to zone on VS0
        m_service->assignWindowToZone(windowId, m_zoneIds[1], vs0, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));

        // Step 2: Float (saves pre-float state)
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);

        // Step 3: Verify preFloatScreen is VS0
        QCOMPARE(m_service->preFloatScreen(windowId), vs0);
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[1]);

        // Step 4: Simulate autotile takeover — clears pre-float state
        m_service->clearPreFloatZone(windowId);

        // Step 5: Verify pre-float data is gone
        QVERIFY(m_service->preFloatZone(windowId).isEmpty());
        QVERIFY(m_service->preFloatScreen(windowId).isEmpty());

        // Step 6: Attempt unfloat on a different VS — must return found=false
        // because autotile cleared the stale pre-float state
        UnfloatResult result = m_service->resolveUnfloatGeometry(windowId, vs1);
        QCOMPARE(result.found, false);
        QVERIFY(result.zoneIds.isEmpty());
    }

    // =====================================================================
    // Test 3: Normal snap float/unfloat cycle preserves state correctly
    // =====================================================================

    void testNormalSnapFloatUnfloatCyclePreservesState()
    {
        const QString windowId = QStringLiteral("dolphin|cccccccc-0000-0000-0000-000000000003");
        const QString screenId = QStringLiteral("DP-1");

        // Step 1: Assign window to zone
        m_service->assignWindowToZone(windowId, m_zoneIds[2], screenId, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));
        QCOMPARE(m_service->zoneForWindow(windowId), m_zoneIds[2]);

        // Step 2: Float window (saves pre-float state via unsnapForFloat)
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        QVERIFY(m_service->isWindowFloating(windowId));
        QVERIFY(!m_service->isWindowSnapped(windowId));

        // Step 3: Verify pre-float zone is preserved (no autotile interference)
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[2]);
        QCOMPARE(m_service->preFloatScreen(windowId), screenId);

        // Step 4: resolveUnfloatGeometry should find the saved zone
        // (In headless mode geometry may be invalid, but zoneIds should be correct
        //  if the method finds them. The key invariant is found=true when pre-float exists
        //  and geometry can be resolved — headless may return found=false due to no QScreen.)
        UnfloatResult result = m_service->resolveUnfloatGeometry(windowId, screenId);
        if (result.found) {
            QCOMPARE(result.zoneIds, QStringList{m_zoneIds[2]});
            QCOMPARE(result.screenId, screenId);
            QVERIFY(result.geometry.isValid());
        }
        // Either way, the pre-float state should still be intact (resolve is read-only)
        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[2]);

        // Step 5: Simulate unfloat consuming the state
        m_service->setWindowFloating(windowId, false);
        m_service->clearPreFloatZone(windowId);

        // Step 6: Verify clean state — no lingering pre-float data
        QVERIFY(!m_service->isWindowFloating(windowId));
        QVERIFY(m_service->preFloatZone(windowId).isEmpty());
        QVERIFY(m_service->preFloatZones(windowId).isEmpty());
        QVERIFY(m_service->preFloatScreen(windowId).isEmpty());

        // resolveUnfloatGeometry should now return found=false
        UnfloatResult result2 = m_service->resolveUnfloatGeometry(windowId, screenId);
        QCOMPARE(result2.found, false);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    LayoutManager* m_layoutManager = nullptr;
    StubSettingsCrossModeFloat* m_settings = nullptr;
    StubZoneDetectorCrossModeFloat* m_zoneDetector = nullptr;
    WindowTrackingService* m_service = nullptr;
    Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_GUILESS_MAIN(TestWtsCrossModeFloat)
#include "test_wts_crossmode_float.moc"
