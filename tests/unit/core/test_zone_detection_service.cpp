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
#include "core/layoutmanager.h"
#include "core/interfaces.h"
#include "core/utils.h"
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// ═══════════════════════════════════════════════════════════════════════════════
// Minimal Stub ZoneDetector
// ═══════════════════════════════════════════════════════════════════════════════

class StubZoneDetectorSvc : public IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorSvc(QObject* parent = nullptr)
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
        QScopedPointer<LayoutManager> layoutManager(new LayoutManager(nullptr));
        QScopedPointer<StubSettings> settings(new StubSettings(nullptr));
        QScopedPointer<StubZoneDetectorSvc> detector(new StubZoneDetectorSvc(nullptr));
        QScopedPointer<WindowTrackingService> service(
            new WindowTrackingService(layoutManager.data(), detector.data(), settings.data(), nullptr, nullptr));

        auto* layout = new Layout(QStringLiteral("Test"), layoutManager.data());
        auto* z1 = new Zone(layout);
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
        QScopedPointer<LayoutManager> layoutManager(new LayoutManager(nullptr));
        QScopedPointer<StubSettings> settings(new StubSettings(nullptr));
        QScopedPointer<StubZoneDetectorSvc> detector(new StubZoneDetectorSvc(nullptr));
        QScopedPointer<WindowTrackingService> service(
            new WindowTrackingService(layoutManager.data(), detector.data(), settings.data(), nullptr, nullptr));

        auto* layout = new Layout(QStringLiteral("Test"), layoutManager.data());
        auto* z1 = new Zone(layout);
        z1->setRelativeGeometry(QRectF(0.0, 0.0, 0.5, 1.0));
        z1->setZoneNumber(1);
        layout->addZone(z1);

        auto* z2 = new Zone(layout);
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
        QVector<ZoneAssignmentEntry> result = service->calculateRotation(true);
        Q_UNUSED(result);

        // Now try with without-braces format
        service->unassignWindow(QStringLiteral("app:win:123"));
        service->assignWindowToZone(QStringLiteral("app:win:456"), zoneIdWithoutBraces, QString(), 0);

        QVector<ZoneAssignmentEntry> result2 = service->calculateRotation(false);
        Q_UNUSED(result2);
    }
};

QTEST_MAIN(TestZoneDetectionService)
#include "test_zone_detection_service.moc"
