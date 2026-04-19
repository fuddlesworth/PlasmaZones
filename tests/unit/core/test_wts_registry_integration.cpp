// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_registry_integration.cpp
 * @brief Integration test: WTS with a live WindowRegistry and bare instance
 *        ids as the wire format — the production configuration.
 *
 * The other test_wts_* files drive legacy "appId|uuid" composites into WTS
 * without a registry attached, exercising the compat-fallback path inside
 * currentAppIdFor(). This file validates the production configuration:
 *
 *   1. WTS is constructed with a WindowRegistry pointer.
 *   2. Window ids are bare instance ids (just a UUID string).
 *   3. Per-window class lookups go through the registry, returning the
 *      latest known class even after a mid-session rename.
 *   4. Legacy fixtures (composite ids in config files) still work because
 *      currentAppIdFor() strips the composite form via extractInstanceId
 *      before consulting the registry.
 */

#include <QCoreApplication>
#include <QRect>
#include <QRectF>
#include <QSignalSpy>
#include <QTest>
#include <memory>

#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include "core/layoutmanager.h"
#include "core/windowregistry.h"
#include "core/windowtrackingservice.h"
#include <PhosphorZones/Zone.h>

#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/StubSettings.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class StubZoneDetectorRegIntegration : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorRegIntegration(QObject* parent = nullptr)
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

static PhosphorZones::Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new PhosphorZones::Zone(layout);
        const qreal x = static_cast<qreal>(i) / zoneCount;
        const qreal w = 1.0 / zoneCount;
        zone->setRelativeGeometry(QRectF(x, 0.0, w, 1.0));
        zone->setZoneNumber(i + 1);
        layout->addZone(zone);
    }
    return layout;
}

class TestWtsRegistryIntegration : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = makePzLayoutManager(nullptr).release();
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetectorRegIntegration(nullptr);
        m_registry = new WindowRegistry(nullptr);
        m_service = new WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr);
        m_service->setWindowRegistry(m_registry);

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
        delete m_service;
        m_service = nullptr;
        delete m_registry;
        m_registry = nullptr;
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

    // ────────────────────────────────────────────────────────────────────
    // currentAppIdFor — the primary lookup function
    // ────────────────────────────────────────────────────────────────────

    void currentAppIdFor_bareInstanceId_returnsRegistryValue()
    {
        const QString instanceId = QStringLiteral("cef1ba31-3316-4f05-84f5-ef627674b504");
        m_registry->upsert(instanceId, {QStringLiteral("firefox"), QString(), QString()});

        QCOMPARE(m_service->currentAppIdFor(instanceId), QStringLiteral("firefox"));
    }

    void currentAppIdFor_returnsLiveClassAfterRename()
    {
        const QString instanceId = QStringLiteral("emby-uuid");
        m_registry->upsert(instanceId, {QStringLiteral("emby-beta"), QString(), QString()});
        QCOMPARE(m_service->currentAppIdFor(instanceId), QStringLiteral("emby-beta"));

        // Mid-session rename — registry upsert, same instance id.
        m_registry->upsert(instanceId, {QStringLiteral("media.emby.client.beta"), QString(), QString()});

        // WTS must see the NEW class immediately — no staleness.
        QCOMPARE(m_service->currentAppIdFor(instanceId), QStringLiteral("media.emby.client.beta"));
    }

    void currentAppIdFor_legacyComposite_stripsAndConsultsRegistry()
    {
        // Legacy call sites (disk fixtures being loaded, tests that use
        // composites) still arrive at currentAppIdFor. The implementation
        // must strip the composite down to the instance id portion and
        // THEN consult the registry — so a composite whose cached class
        // differs from the registry's live one returns the live one.
        const QString instanceId = QStringLiteral("emby-uuid");
        const QString legacyComposite = QStringLiteral("emby-beta|") + instanceId;

        m_registry->upsert(instanceId, {QStringLiteral("media.emby.client.beta"), QString(), QString()});

        QCOMPARE(m_service->currentAppIdFor(legacyComposite), QStringLiteral("media.emby.client.beta"));
    }

    void currentAppIdFor_unknownInstance_fallsBackToStringParsing()
    {
        // For an instance id the registry has never seen (e.g. during
        // startup before the bridge has called setWindowMetadata), WTS
        // falls back to string parsing so callers that pass a legacy
        // composite still get something sensible.
        const QString composite = QStringLiteral("firefox|12345");
        QCOMPARE(m_service->currentAppIdFor(composite), QStringLiteral("firefox"));

        // A bare instance id the registry doesn't know about returns
        // the whole string (no '|' to split on) — this is the documented
        // pass-through behavior, letting callers compare equality.
        const QString bare = QStringLiteral("unknown-uuid");
        QCOMPARE(m_service->currentAppIdFor(bare), bare);
    }

    // ────────────────────────────────────────────────────────────────────
    // Full snap flow using bare instance ids
    // ────────────────────────────────────────────────────────────────────

    void snapFlow_withBareInstanceIds_worksEndToEnd()
    {
        const QString instanceId = QStringLiteral("firefox-uuid");
        m_registry->upsert(instanceId, {QStringLiteral("firefox"), QString(), QString()});

        m_service->assignWindowToZone(instanceId, m_zoneIds[0], m_screenId, 1);
        QVERIFY(m_service->isWindowSnapped(instanceId));
        QCOMPARE(m_service->zoneForWindow(instanceId), m_zoneIds[0]);

        // Unsnap for float — should preserve pre-float zone
        m_service->unsnapForFloat(instanceId);
        QCOMPARE(m_service->preFloatZone(instanceId), m_zoneIds[0]);
    }

    void windowClosed_withBareInstanceId_persistsUnderCurrentClass()
    {
        const QString instanceId = QStringLiteral("firefox-uuid");
        m_registry->upsert(instanceId, {QStringLiteral("firefox"), QString(), QString()});

        m_service->assignWindowToZone(instanceId, m_zoneIds[1], m_screenId, 1);
        m_service->windowClosed(instanceId);

        // Pending restore entry keyed by CURRENT class name from the
        // registry, not by the instance id. That's what lets a new
        // instance (with a different uuid) pick it up on next launch.
        QVERIFY(m_service->pendingRestoreQueues().contains(QStringLiteral("firefox")));
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    LayoutManager* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetectorRegIntegration* m_zoneDetector = nullptr;
    WindowRegistry* m_registry = nullptr;
    WindowTrackingService* m_service = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QString m_screenId;
};

QTEST_MAIN(TestWtsRegistryIntegration)
#include "test_wts_registry_integration.moc"
