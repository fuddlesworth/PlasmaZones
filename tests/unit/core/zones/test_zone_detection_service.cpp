// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_zone_detection_service.cpp
 * @brief Unit tests for WindowTrackingService multi-zone and rotation UUID tests
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QRectF>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QScopedPointer>

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "config/configbackends.h"
#include "core/interfaces/interfaces.h"
#include "core/utils/utils.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using namespace PlasmaZones;
using PhosphorEngine::ZoneAssignmentEntry;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// ═══════════════════════════════════════════════════════════════════════════════
// Minimal Stub PhosphorZones::ZoneDetector
// ═══════════════════════════════════════════════════════════════════════════════

// Nothing here exercises detection — SnapEngine only stores the detector — so
// the shared inert stub does.
using StubZoneDetectorSvc = PlasmaZones::StubZoneDetector;

// ═══════════════════════════════════════════════════════════════════════════════
// Test Class
// ═══════════════════════════════════════════════════════════════════════════════

class TestZoneDetectionService : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════
    // P1: Multi-zone geometry with some invalid zones
    // ═══════════════════════════════════════════════════════════════════════

    void testMultiZoneGeometry_someZonesInvalid()
    {
        IsolatedConfigGuard guard;
        QScopedPointer<PhosphorZones::LayoutRegistry> layoutManager(
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        QScopedPointer<StubSettings> settings(new StubSettings(nullptr));
        QScopedPointer<StubZoneDetectorSvc> detector(new StubZoneDetectorSvc(nullptr));
        QScopedPointer<PhosphorPlacement::WindowTrackingService> service(
            new PhosphorPlacement::WindowTrackingService(layoutManager.data(), nullptr, nullptr));

        auto* layout = new PhosphorZones::Layout(QStringLiteral("Test"), layoutManager.data());
        auto* z1 = new PhosphorZones::Zone(layout);
        z1->setRelativeGeometry(QRectF(0.0, 0.0, 0.5, 1.0));
        z1->setZoneNumber(1);
        layout->addZone(z1);

        layoutManager->addLayout(layout);
        layoutManager->setActiveLayout(layout);

        QString validId = z1->id().toString();
        QString invalidId = QUuid::createUuid().toString();

        const QRect validOnly = service->multiZoneGeometry({validId}, QString());
        const QRect invalidOnly = service->multiZoneGeometry({invalidId}, QString());
        const QRect both = service->multiZoneGeometry({validId, invalidId}, QString());

        // The offscreen QPA platform supplies a primary QScreen, so the real
        // zone resolves to a usable rect.
        QVERIFY2(validOnly.isValid(), "the layout's own zone must resolve against the offscreen primary screen");

        // An id no layout owns contributes nothing to the union: alone it
        // yields an empty rect, and beside a real zone it leaves that zone's
        // rect exactly as it was.
        QVERIFY(invalidOnly.isEmpty());
        QCOMPARE(both, validOnly);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P1: Rotation UUID format mismatch fallback
    // ═══════════════════════════════════════════════════════════════════════

    void testCalculateRotation_uuidFormatMismatch()
    {
        IsolatedConfigGuard guard;
        QScopedPointer<PhosphorZones::LayoutRegistry> layoutManager(
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        QScopedPointer<StubSettings> settings(new StubSettings(nullptr));
        QScopedPointer<StubZoneDetectorSvc> detector(new StubZoneDetectorSvc(nullptr));
        QScopedPointer<PhosphorPlacement::WindowTrackingService> service(
            new PhosphorPlacement::WindowTrackingService(layoutManager.data(), nullptr, nullptr));
        QScopedPointer<SnapEngine> engine(
            new SnapEngine(layoutManager.data(), service.data(), detector.data(), nullptr, nullptr));
        engine->setEngineSettings(settings.data());
        service->setSnapState(engine->snapState());
        service->setSnapEngine(engine.data());

        auto* layout = new PhosphorZones::Layout(QStringLiteral("Test"), layoutManager.data());
        auto* z1 = new PhosphorZones::Zone(layout);
        z1->setRelativeGeometry(QRectF(0.0, 0.0, 0.5, 1.0));
        z1->setZoneNumber(1);
        layout->addZone(z1);

        auto* z2 = new PhosphorZones::Zone(layout);
        z2->setRelativeGeometry(QRectF(0.5, 0.0, 0.5, 1.0));
        z2->setZoneNumber(2);
        layout->addZone(z2);

        layoutManager->addLayout(layout);
        layoutManager->setActiveLayout(layout);

        QString zoneIdWithBraces = z1->id().toString(QUuid::WithBraces);
        QString zoneIdWithoutBraces = z1->id().toString(QUuid::WithoutBraces);

        // With two zones the rotation target is z2 in either direction, so the
        // two arms below differ only in the id FORMAT the assignment was
        // recorded under. Both must land on the same zone.
        const QString bracedTarget = z2->id().toString();

        // Assign window using with-braces format
        service->assignWindowToZone(QStringLiteral("app:win:123"), zoneIdWithBraces, QString(), 0);

        QVector<ZoneAssignmentEntry> result = engine->calculateRotation(true);
        QCOMPARE(result.size(), 1);
        QCOMPARE(result.first().windowId, QStringLiteral("app:win:123"));
        QCOMPARE(result.first().sourceZoneId, z1->id().toString());
        QCOMPARE(result.first().targetZoneId, bracedTarget);

        // Now try with without-braces format — the stored id no longer matches
        // any zone key directly, so this exercises the UUID-parse fallback.
        service->unassignWindow(QStringLiteral("app:win:123"));
        service->assignWindowToZone(QStringLiteral("app:win:456"), zoneIdWithoutBraces, QString(), 0);

        QVector<ZoneAssignmentEntry> result2 = engine->calculateRotation(false);
        QCOMPARE(result2.size(), 1);
        QCOMPARE(result2.first().windowId, QStringLiteral("app:win:456"));
        // The entry reports the canonical with-braces id regardless of the
        // format the assignment came in as.
        QCOMPARE(result2.first().sourceZoneId, z1->id().toString());
        QCOMPARE(result2.first().targetZoneId, bracedTarget);
    }
};

QTEST_MAIN(TestZoneDetectionService)
#include "test_zone_detection_service.moc"
