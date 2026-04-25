// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_convenience.cpp
 * @brief Unit tests for WindowTrackingAdaptor convenience methods:
 *        moveWindowToZone, swapWindowsById, getWindowState, getAllWindowStates,
 *        and windowStateChanged signal emission.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QSignalSpy>
#include <QRectF>
#include <memory>

#include "core/windowtrackingservice.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/virtualdesktopmanager.h"
#include "dbus/snapadaptor.h"
#include "dbus/windowtrackingadaptor.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "../helpers/StubSettings.h"

using StubSettingsConvenience = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector
// =========================================================================

class StubZoneDetectorConvenience : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorConvenience(QObject* parent = nullptr)
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
// Helpers
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

class TestWtaConvenience : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsConvenience(nullptr);
        m_zoneDetector = new StubZoneDetectorConvenience(nullptr);

        // WTA needs a parent QObject for QDBusAbstractAdaptor
        m_parent = new QObject(nullptr);
        m_wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, m_parent);

        m_snapEngine = new SnapEngine(m_layoutManager, m_wta->service(), m_zoneDetector, nullptr, nullptr);
        m_snapEngine->setEngineSettings(m_settings);
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        m_wta->service()->setSnapEngine(m_snapEngine);
        m_wta->setEngines(m_snapEngine, nullptr);

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

    void cleanup()
    {
        // SnapAdaptor is owned by m_parent (QDBusAbstractAdaptor parent)
        // Clear engine before deleting to disconnect signals
        if (m_snapAdaptor) {
            m_snapAdaptor->clearEngine();
        }
        m_snapAdaptor = nullptr;
        // WTA is owned by m_parent (QDBusAbstractAdaptor parent)
        m_wta->service()->setSnapState(nullptr);
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

    // =====================================================================
    // moveWindowToZone
    // =====================================================================

    void testMoveWindowToZone_validZone_emitsApplyGeometry()
    {
        QString windowId = QStringLiteral("firefox|12345");

        // Assign a screen mapping so resolveScreenForSnap works
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_snapAdaptor->moveWindowToZone(windowId, m_zoneIds[0]);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), windowId);
        // Geometry args (x, y, width, height) should have valid dimensions
        QVERIFY(spy.at(0).at(3).toInt() > 0); // width
        QVERIFY(spy.at(0).at(4).toInt() > 0); // height
        QCOMPARE(spy.at(0).at(5).toString(), m_zoneIds[0]);
    }

    void testMoveWindowToZone_invalidZone_noSignal()
    {
        QString windowId = QStringLiteral("firefox|12345");
        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_snapAdaptor->moveWindowToZone(windowId, QStringLiteral("nonexistent-zone-id"));

        QCOMPARE(spy.count(), 0);
    }

    void testMoveWindowToZone_emptyWindowId_noSignal()
    {
        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_snapAdaptor->moveWindowToZone(QString(), m_zoneIds[0]);

        QCOMPARE(spy.count(), 0);
    }

    // =====================================================================
    // swapWindowsById
    // =====================================================================

    void testSwapWindowsById_twoSnappedWindows_emitsTwoApplyGeometry()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        // Snap both windows to different zones via the WTA's windowSnapped slot
        m_snapEngine->commitSnap(window1, m_zoneIds[0], m_screenId);
        m_snapEngine->commitSnap(window2, m_zoneIds[1], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_snapAdaptor->swapWindowsById(window1, window2);

        QCOMPARE(spy.count(), 2);

        // Window1 should move to zone2, window2 to zone1
        QCOMPARE(spy.at(0).at(0).toString(), window1);
        QCOMPARE(spy.at(0).at(5).toString(), m_zoneIds[1]);
        QCOMPARE(spy.at(1).at(0).toString(), window2);
        QCOMPARE(spy.at(1).at(5).toString(), m_zoneIds[0]);
    }

    void testSwapWindowsById_oneNotSnapped_noSignal()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        // Only snap window1
        m_snapEngine->commitSnap(window1, m_zoneIds[0], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_snapAdaptor->swapWindowsById(window1, window2);

        QCOMPARE(spy.count(), 0);
    }

    // =====================================================================
    // getWindowState
    // =====================================================================

    void testGetWindowState_snappedWindow_returnsStruct()
    {
        QString windowId = QStringLiteral("firefox|12345");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);

        WindowStateEntry state = m_wta->getWindowState(windowId);
        QCOMPARE(state.windowId, windowId);
        QCOMPARE(state.zoneId, m_zoneIds[0]);
        QCOMPARE(state.screenId, m_screenId);
        QCOMPARE(state.isFloating, false);
    }

    void testGetWindowState_floatingWindow_returnsFloatingTrue()
    {
        QString windowId = QStringLiteral("firefox|12345");

        // Snap then float
        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);
        m_wta->setWindowFloating(windowId, true);

        WindowStateEntry state = m_wta->getWindowState(windowId);
        QCOMPARE(state.isFloating, true);
    }

    void testGetWindowState_unknownWindow_returnsEmptyZone()
    {
        QString windowId = QStringLiteral("unknown|99999");

        WindowStateEntry state = m_wta->getWindowState(windowId);
        QVERIFY(state.zoneId.isEmpty());
    }

    // =====================================================================
    // getAllWindowStates
    // =====================================================================

    void testGetAllWindowStates_multipleWindows_returnsList()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_snapEngine->commitSnap(window1, m_zoneIds[0], m_screenId);
        m_snapEngine->commitSnap(window2, m_zoneIds[1], m_screenId);

        WindowStateList allStates = m_wta->getAllWindowStates();
        QCOMPARE(allStates.size(), 2);

        // Collect all window IDs from the list
        QStringList windowIds;
        for (const auto& ws : allStates) {
            windowIds.append(ws.windowId);
        }
        QVERIFY(windowIds.contains(window1));
        QVERIFY(windowIds.contains(window2));
    }

    // =====================================================================
    // windowStateChanged signal
    // =====================================================================

    void testWindowStateChanged_emittedOnSnap()
    {
        QString windowId = QStringLiteral("firefox|12345");

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);

        QVERIFY(spy.count() >= 1);

        // Find the "snapped" emission
        bool foundSnapped = false;
        for (int i = 0; i < spy.count(); ++i) {
            auto state = spy.at(i).at(1).value<WindowStateEntry>();
            if (state.changeType == QLatin1String("snapped")) {
                QCOMPARE(spy.at(i).at(0).toString(), windowId);
                foundSnapped = true;
                break;
            }
        }
        QVERIFY(foundSnapped);
    }

    void testWindowStateChanged_emittedOnUnsnap()
    {
        QString windowId = QStringLiteral("firefox|12345");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        m_snapEngine->uncommitSnap(windowId);

        QVERIFY(spy.count() >= 1);

        bool foundUnsnapped = false;
        for (int i = 0; i < spy.count(); ++i) {
            auto state = spy.at(i).at(1).value<WindowStateEntry>();
            if (state.changeType == QLatin1String("unsnapped")) {
                QCOMPARE(spy.at(i).at(0).toString(), windowId);
                foundUnsnapped = true;
                break;
            }
        }
        QVERIFY(foundUnsnapped);
    }

    void testWindowStateChanged_emittedOnFloat()
    {
        QString windowId = QStringLiteral("firefox|12345");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        m_wta->setWindowFloating(windowId, true);

        QVERIFY(spy.count() >= 1);

        bool foundFloated = false;
        for (int i = 0; i < spy.count(); ++i) {
            auto state = spy.at(i).at(1).value<WindowStateEntry>();
            if (state.changeType == QLatin1String("floated")) {
                QCOMPARE(spy.at(i).at(0).toString(), windowId);
                foundFloated = true;
                break;
            }
        }
        QVERIFY(foundFloated);
    }

private:
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

QTEST_MAIN(TestWtaConvenience)
#include "test_wta_convenience.moc"
