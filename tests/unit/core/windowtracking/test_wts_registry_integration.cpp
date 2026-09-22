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
 *   5. The instant-restore cache source (pendingRestoreGeometries) judges a
 *      record's liveness through the registry: composite record ids against
 *      bare registry keys, which is exactly the strip the probe performs.
 */

#include <QCoreApplication>
#include <QRect>
#include <QRectF>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <memory>

#include "core/interfaces/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "config/configbackends.h"
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorZones/Zone.h>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

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
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_registry = new PhosphorEngine::WindowRegistry(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
        m_snapState = new PhosphorSnapEngine::SnapState(QString(), nullptr);
        m_service->setSnapState(m_snapState);
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
        m_service->setSnapState(nullptr);
        delete m_snapState;
        m_snapState = nullptr;
        delete m_service;
        m_service = nullptr;
        delete m_registry;
        m_registry = nullptr;
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

    // ────────────────────────────────────────────────────────────────────
    // pendingRestoreGeometries — the effect's instant-restore cache source
    // ────────────────────────────────────────────────────────────────────

    // A snapped record whose window is still open (per the registry-backed
    // live probe) is that window's own placement, not a pending restore:
    // served through the appId-keyed cache it teleported a fresh second
    // instance into its open sibling's zone (#1106). Only records of closed
    // windows feed the cache, and each entry names its record's window so
    // the effect can drop one the daemon could not tell was live.
    void pendingRestoreGeometries_skipsRecordsOfLiveWindows()
    {
        // Positive control first: with no live window the snapped record IS
        // a pending restore and resolves to a real zone rect.
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("firefox|closed-uuid");
        rec.appId = QStringLiteral("firefox");
        rec.screenId = m_screenId;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{m_zoneIds[0]};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        QVERIFY(m_service->placementStore().record(rec));

        auto targets = m_service->pendingRestoreGeometries();
        QVERIFY2(targets.contains(QStringLiteral("firefox")), "a closed window's snapped record feeds the cache");
        QCOMPARE(targets.value(QStringLiteral("firefox")).size(), 1);
        QVERIFY(targets.value(QStringLiteral("firefox")).first().geometry.isValid());
        QCOMPARE(targets.value(QStringLiteral("firefox")).first().windowId, QStringLiteral("firefox|closed-uuid"));

        // The same record's window is live: no entry.
        m_registry->upsert(QStringLiteral("closed-uuid"), {QStringLiteral("firefox"), QString(), QString()});
        targets = m_service->pendingRestoreGeometries();
        QVERIFY2(!targets.contains(QStringLiteral("firefox")), "a live window's record is not a pending restore");

        // A closed record beside the live one feeds the cache whichever is
        // newer: the live record is skipped, not merely out-ranked.
        PhosphorEngine::WindowPlacement closed = rec;
        closed.windowId = QStringLiteral("firefox|other-closed");
        closed.engines[PhosphorEngine::WindowPlacement::snapEngineId()].zoneIds = QStringList{m_zoneIds[1]};
        QVERIFY(m_service->placementStore().record(closed));
        PhosphorEngine::WindowPlacement liveNewer = rec;
        liveNewer.windowId = QStringLiteral("firefox|live-newer");
        QVERIFY(m_service->placementStore().record(liveNewer));
        m_registry->upsert(QStringLiteral("live-newer"), {QStringLiteral("firefox"), QString(), QString()});
        targets = m_service->pendingRestoreGeometries();
        QCOMPARE(targets.value(QStringLiteral("firefox")).size(), 1);
        QCOMPARE(targets.value(QStringLiteral("firefox")).first().windowId, QStringLiteral("firefox|other-closed"));

        // Every closed record is served, NEWEST first: the open claim hands
        // the newest unclaimed record to the first opener, so the head is the
        // zone the resolve will confirm and the next opener gets the next.
        PhosphorEngine::WindowPlacement c1 = rec;
        c1.windowId = QStringLiteral("firefox|c1");
        QVERIFY(m_service->placementStore().record(c1));
        PhosphorEngine::WindowPlacement c2 = rec;
        c2.windowId = QStringLiteral("firefox|c2");
        QVERIFY(m_service->placementStore().record(c2));
        targets = m_service->pendingRestoreGeometries();
        const auto firefox = targets.value(QStringLiteral("firefox"));
        QCOMPARE(firefox.size(), 3);
        QCOMPARE(firefox.at(0).windowId, QStringLiteral("firefox|c2"));
        QCOMPARE(firefox.at(1).windowId, QStringLiteral("firefox|c1"));
        QCOMPARE(firefox.at(2).windowId, QStringLiteral("firefox|other-closed"));

        // A record naming a zone the context's layout no longer holds is
        // not served (the #1104 layout gate the async resolver applies).
        PhosphorEngine::WindowPlacement ghost = rec;
        ghost.windowId = QStringLiteral("firefox|ghost");
        ghost.engines[PhosphorEngine::WindowPlacement::snapEngineId()].zoneIds =
            QStringList{QUuid::createUuid().toString()};
        QVERIFY(m_service->placementStore().record(ghost));
        targets = m_service->pendingRestoreGeometries();
        QCOMPARE(targets.value(QStringLiteral("firefox")).size(), 3);
        QCOMPARE(targets.value(QStringLiteral("firefox")).first().windowId, QStringLiteral("firefox|c2"));
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    PhosphorSnapEngine::SnapState* m_snapState = nullptr;
    PhosphorEngine::WindowRegistry* m_registry = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QString m_screenId;
};

QTEST_MAIN(TestWtsRegistryIntegration)
#include "test_wts_registry_integration.moc"
