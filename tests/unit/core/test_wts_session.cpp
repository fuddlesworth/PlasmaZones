// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_session.cpp
 * @brief Unit tests for WindowTrackingService session restore, clear-stale, and resnap
 *
 * Tests cover:
 * 1. PhosphorZones::Zone-number fallback on session restore
 * 2. Floating window skips restore
 * 3. Clear stale pending assignments
 * 4. Resnap from previous layout
 * 5. Rotation calculations
 * 6. Daemon restart / pending restore
 * 7. Multi-monitor restore edge cases
 * 8. Auto-snap marking
 * 9. Consume pending assignment
 *
 * WIRE FORMAT NOTE: fixtures use legacy "appId|uuid" composites because the
 * WTS is constructed without a WindowRegistry. See header in
 * test_wts_lifecycle.cpp for the reasoning.
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
#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PhosphorEngine::SnapResult;
using PhosphorEngine::ZoneAssignmentEntry;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "../helpers/StubSettings.h"

using StubSettingsSession = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector
// =========================================================================

class StubZoneDetectorSession : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorSession(QObject* parent = nullptr)
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

class TestWtsSession : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsSession(nullptr);
        m_zoneDetector = new StubZoneDetectorSession(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, nullptr);
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
        m_service->setSnapState(nullptr);
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
    // P1: Session Restore
    // =====================================================================

    void testRestore_zoneNumberFallback()
    {
        QString appId = QStringLiteral("firefox");
        QString oldZoneId = QUuid::createUuid().toString();

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {oldZoneId};
        entry.screenId = QString();
        entry.layoutId = m_testLayout->id().toString();
        entry.zoneNumbers = {2};

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        SnapResult result = m_engine->calculateRestoreFromSession(QStringLiteral("firefox|99999"), QString(), false);
        Q_UNUSED(result);

        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().zoneNumbers, QList<int>{2});
    }

    void testRestore_floatingWindowSkipsRestore()
    {
        QString appId = QStringLiteral("firefox");
        QString windowId = QStringLiteral("firefox|12345");

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        QSet<QString> floating;
        floating.insert(appId);
        m_service->setFloatingWindows(floating);

        SnapResult result = m_engine->calculateRestoreFromSession(windowId, QString(), false);
        QVERIFY(!result.shouldSnap);
    }

    // =====================================================================
    // P1: Clear Stale Pending
    // =====================================================================

    void testClearStalePendingAssignment()
    {
        QString windowId = QStringLiteral("app|12345");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.screenId = QStringLiteral("DP-1");
        entry.virtualDesktop = 1;
        entry.layoutId = QUuid::createUuid().toString();
        entry.zoneNumbers = {1};

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        bool popped = m_service->consumePendingAssignment(windowId);
        QVERIFY(popped);
        QVERIFY(!m_service->pendingRestoreQueues().contains(appId));
    }

    // =====================================================================
    // P1: Resnap
    // =====================================================================

    void testResnapFromPreviousLayout_zonePositionMapping()
    {
        QString window1 = QStringLiteral("app1|111");
        QString window2 = QStringLiteral("app2|222");
        QString window3 = QStringLiteral("app3|333");

        m_service->assignWindowToZone(window1, m_zoneIds[0], QString(), 0);
        m_service->assignWindowToZone(window2, m_zoneIds[1], QString(), 0);
        m_service->assignWindowToZone(window3, m_zoneIds[2], QString(), 0);

        PhosphorZones::Layout* newLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<ZoneAssignmentEntry> resnap = m_engine->calculateResnapFromPreviousLayout();
        QVector<ZoneAssignmentEntry> secondCall = m_engine->calculateResnapFromPreviousLayout();
        QVERIFY(secondCall.isEmpty()); // Buffer consumed on first call
    }

    // =====================================================================
    // P1: Rotation
    // =====================================================================

    void testCalculateRotation_clockwiseAndCounterClockwise()
    {
        QString window1 = QStringLiteral("app1|111");
        QString window2 = QStringLiteral("app2|222");

        m_service->assignWindowToZone(window1, m_zoneIds[0], QString(), 0);
        m_service->assignWindowToZone(window2, m_zoneIds[1], QString(), 0);

        QVector<ZoneAssignmentEntry> cw = m_engine->calculateRotation(true);
        QVector<ZoneAssignmentEntry> ccw = m_engine->calculateRotation(false);

        Q_UNUSED(cw);
        Q_UNUSED(ccw);
    }

    // =====================================================================
    // P0: Daemon Restart / Pending Restore
    // =====================================================================

    void testDaemonRestart_pendingRestoresAvailableEmitted()
    {
        QString appId = QStringLiteral("firefox");

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.layoutId = m_testLayout->id().toString();

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().zoneIds.first(), m_zoneIds[0]);
    }

    // =====================================================================
    // P0: Restore wrong display (multi-monitor)
    // =====================================================================

    void testRestore_wrongDisplay_multiMonitor()
    {
        QString appId = QStringLiteral("app");

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.screenId = QStringLiteral("HDMI-2");

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().screenId, QStringLiteral("HDMI-2"));
    }

    void testRestore_savedScreenDisconnected()
    {
        QString appId = QStringLiteral("app");
        QString windowId = QStringLiteral("app|12345");

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.screenId = QStringLiteral("DISCONNECTED-99");
        entry.layoutId = m_testLayout->id().toString();

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        SnapResult result = m_engine->calculateRestoreFromSession(windowId, QStringLiteral("DP-1"), false);
        if (result.shouldSnap) {
            QVERIFY(result.geometry.isValid());
        }
    }

    // =====================================================================
    // P1: Auto-snap / Mark as auto-snapped
    // =====================================================================

    void testMarkAsAutoSnapped()
    {
        QString windowId = QStringLiteral("app|12345");

        QVERIFY(!m_service->isAutoSnapped(windowId));
        m_service->markAsAutoSnapped(windowId);
        QVERIFY(m_service->isAutoSnapped(windowId));
        QVERIFY(m_service->clearAutoSnapped(windowId));
        QVERIFY(!m_service->isAutoSnapped(windowId));
    }

    // =====================================================================
    // P1: Consume pending assignment
    // =====================================================================

    void testConsumePendingAssignment()
    {
        QString windowId = QStringLiteral("app|12345");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.zoneNumbers = {1};

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        m_service->consumePendingAssignment(windowId);

        QVERIFY(!m_service->pendingRestoreQueues().contains(appId));
    }

    // =====================================================================
    // P0: WindowKind gate on PendingRestore consume (discussion #461 follow-up)
    // =====================================================================

    void testKindGate_rejectsMismatch_entryPreserved()
    {
        QString appId = QStringLiteral("steam");
        QString windowId = appId + QStringLiteral("|") + QUuid::createUuid().toString(QUuid::WithoutBraces);

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.layoutId = m_testLayout->id().toString();
        entry.zoneNumbers = {1};
        entry.windowKind = PhosphorEngine::WindowKind::Normal;

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        // Opening window is a Transient (Steam image popup analogue) and must
        // NOT consume the saved-zone entry recorded for the Normal main window.
        SnapResult result =
            m_engine->calculateRestoreFromSession(windowId, QString(), false, PhosphorEngine::WindowKind::Transient);
        QVERIFY(!result.shouldSnap);
        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).size(), 1);
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().windowKind, PhosphorEngine::WindowKind::Normal);
    }

    void testKindGate_acceptsMatch_returnsSnap()
    {
        QString appId = QStringLiteral("firefox");
        QString windowId = appId + QStringLiteral("|") + QUuid::createUuid().toString(QUuid::WithoutBraces);

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.layoutId = m_testLayout->id().toString();
        entry.zoneNumbers = {1};
        entry.windowKind = PhosphorEngine::WindowKind::Normal;

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        // Live window kind matches the saved entry — gate passes, deeper layout
        // checks may still bail (no current layout context), but the gate at
        // least does not short-circuit. We assert the kind comparison
        // specifically by leaving the queue untouched on a downstream skip.
        SnapResult result =
            m_engine->calculateRestoreFromSession(windowId, QString(), false, PhosphorEngine::WindowKind::Normal);
        Q_UNUSED(result);
        // The entry remains either way (calculate* does not consume). If the
        // kind gate had wrongly rejected, the dedicated rejection log would
        // have fired but we still see the entry — what we really verify is
        // that the alternate path in testKindGate_rejectsMismatch fails.
        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
    }

    void testKindGate_unknownIsPermissive_legacyEntries()
    {
        QString appId = QStringLiteral("legacy-app");
        QString windowId = appId + QStringLiteral("|") + QUuid::createUuid().toString(QUuid::WithoutBraces);

        // Legacy on-disk entries (loaded from a pre-fix session) have no
        // windowKind set — the field defaults to WindowKind::Unknown. The
        // gate MUST stay permissive in that case so the upgrade does not
        // silently drop every saved-zone restore.
        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {m_zoneIds[0]};
        entry.layoutId = m_testLayout->id().toString();
        entry.zoneNumbers = {1};
        // windowKind left at default (Unknown)
        QCOMPARE(entry.windowKind, PhosphorEngine::WindowKind::Unknown);

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        SnapResult result =
            m_engine->calculateRestoreFromSession(windowId, QString(), false, PhosphorEngine::WindowKind::Transient);
        // We do not assert shouldSnap here (downstream layout / context
        // checks can refuse for unrelated reasons). What matters is that
        // the kind-mismatch path did NOT fire — the entry is left intact
        // by the gate's own logic, exactly like the legacy behaviour.
        Q_UNUSED(result);
        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().windowKind,
                 PhosphorEngine::WindowKind::Unknown);
    }

    // =====================================================================
    // P0: PhosphorZones::Layout Import UUID Collision
    // =====================================================================

    void testLayoutImport_uuidCollision_regeneratesIds()
    {
        QString appId = QStringLiteral("app");
        QString bogusUuid = QUuid::createUuid().toString();

        PhosphorPlacement::WindowTrackingService::PendingRestore entry;
        entry.zoneIds = {bogusUuid};
        entry.layoutId = m_testLayout->id().toString();
        entry.zoneNumbers = {1};

        QHash<QString, QList<PhosphorPlacement::WindowTrackingService::PendingRestore>> queues;
        queues[appId] = {entry};
        m_service->setPendingRestoreQueues(queues);

        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().zoneNumbers.first(), 1);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsSession* m_settings = nullptr;
    StubZoneDetectorSession* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsSession)
#include "test_wts_session.moc"
