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
#include "core/windowtrackingservice.h"
#include "snap/SnapEngine.h"
#include <PhosphorZones/LayoutRegistry.h>
#include "snap/SnapState.h"
#include "config/configbackends.h"
#include "core/interfaces.h"
#include "core/utils.h"
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// ═══════════════════════════════════════════════════════════════════════════════
// Minimal Stub PhosphorZones::ZoneDetector
// ═══════════════════════════════════════════════════════════════════════════════

class StubZoneDetectorSvc : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorSvc(QObject* parent = nullptr)
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
        QScopedPointer<PhosphorZones::LayoutRegistry> layoutManager(new PhosphorZones::LayoutRegistry(
            PlasmaZones::createAssignmentsBackend(), QStringLiteral("plasmazones/layouts")));
        QScopedPointer<StubSettings> settings(new StubSettings(nullptr));
        QScopedPointer<StubZoneDetectorSvc> detector(new StubZoneDetectorSvc(nullptr));
        QScopedPointer<WindowTrackingService> service(new WindowTrackingService(
            layoutManager.data(), detector.data(), nullptr, settings.data(), nullptr, nullptr));

        auto* layout = new PhosphorZones::Layout(QStringLiteral("Test"), layoutManager.data());
        auto* z1 = new PhosphorZones::Zone(layout);
        z1->setRelativeGeometry(QRectF(0.0, 0.0, 0.5, 1.0));
        z1->setZoneNumber(1);
        layout->addZone(z1);

        layoutManager->addLayout(layout);
        layoutManager->setActiveLayout(layout);

        QString validId = z1->id().toString();
        QString invalidId = QUuid::createUuid().toString();

        // multiZoneGeometry with one valid and one invalid zone.
        // In headless mode, geometry resolution fails for all zones (no QScreen),
        // but the method should not crash.
        QRect geo = service->multiZoneGeometry({validId, invalidId}, QString());
        Q_UNUSED(geo);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // P1: Rotation UUID format mismatch fallback
    // ═══════════════════════════════════════════════════════════════════════

    void testCalculateRotation_uuidFormatMismatch()
    {
        IsolatedConfigGuard guard;
        QScopedPointer<PhosphorZones::LayoutRegistry> layoutManager(new PhosphorZones::LayoutRegistry(
            PlasmaZones::createAssignmentsBackend(), QStringLiteral("plasmazones/layouts")));
        QScopedPointer<StubSettings> settings(new StubSettings(nullptr));
        QScopedPointer<StubZoneDetectorSvc> detector(new StubZoneDetectorSvc(nullptr));
        QScopedPointer<WindowTrackingService> service(new WindowTrackingService(
            layoutManager.data(), detector.data(), nullptr, settings.data(), nullptr, nullptr));
        QScopedPointer<SnapEngine> engine(
            new SnapEngine(layoutManager.data(), service.data(), detector.data(), settings.data(), nullptr, nullptr));
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

        // Assign window using with-braces format
        service->assignWindowToZone(QStringLiteral("app:win:123"), zoneIdWithBraces, QString(), 0);

        // calculateRotation should handle both formats without error
        QVector<ZoneAssignmentEntry> result = engine->calculateRotation(true);
        Q_UNUSED(result);

        // Now try with without-braces format
        service->unassignWindow(QStringLiteral("app:win:123"));
        service->assignWindowToZone(QStringLiteral("app:win:456"), zoneIdWithoutBraces, QString(), 0);

        QVector<ZoneAssignmentEntry> result2 = engine->calculateRotation(false);
        Q_UNUSED(result2);
    }
};

QTEST_MAIN(TestZoneDetectionService)
#include "test_zone_detection_service.moc"
