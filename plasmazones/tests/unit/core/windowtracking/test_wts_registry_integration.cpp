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
 *      bare registry keys, which is exactly the strip the probe performs. It
 *      serves only the record and the context the async resolve will use.
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
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
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

    // ────────────────────────────────────────────────────────────────────
    // isWindowSticky — the compositor's report, else the registry's (F610)
    // ────────────────────────────────────────────────────────────────────

    // A window that was already on every desktop when the daemon registered
    // never sends a sticky report; its metadata push answers.
    void isWindowSticky_registryAnswersWhenNeverReported()
    {
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.isSticky = true;
        m_registry->upsert(QStringLiteral("sticky-uuid"), meta);
        QVERIFY(m_service->isWindowSticky(QStringLiteral("sticky-uuid")));
    }

    // A live report reaches the daemon before the metadata push, so it wins.
    void isWindowSticky_reportOutranksRegistry()
    {
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.isSticky = true;
        m_registry->upsert(QStringLiteral("sticky-uuid"), meta);
        m_service->setWindowSticky(QStringLiteral("sticky-uuid"), false);
        QVERIFY(!m_service->isWindowSticky(QStringLiteral("sticky-uuid")));
    }

    void isWindowSticky_unknownIsFalse()
    {
        QVERIFY(!m_service->isWindowSticky(QStringLiteral("never-seen")));
        PhosphorPlacement::WindowTrackingService bare(m_layoutManager, nullptr, nullptr);
        QVERIFY(!bare.isWindowSticky(QStringLiteral("never-seen")));
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
    }

    // A record naming a zone of a layout that is registered but not the one
    // its context runs is not served: zone geometry finds the zone in ANY
    // loaded layout, and the async resolver refuses it (#1104).
    void pending_ghostOfAnotherRegisteredLayout()
    {
        PhosphorZones::Layout* other = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(other);
        QVERIFY(m_service->placementStore().record(
            snappedRecord(QStringLiteral("app|ghost"), m_screenId, {other->zones().first()->id().toString()})));
        QVERIFY(!m_service->pendingRestoreGeometries().contains(QStringLiteral("app")));
        // Control: the layout the context runs is served.
        QVERIFY(m_service->placementStore().record(
            snappedRecord(QStringLiteral("other|held"), m_screenId, {m_zoneIds[0]}, QStringLiteral("other"))));
        QVERIFY(m_service->pendingRestoreGeometries().contains(QStringLiteral("other")));
    }

    // A screen a tiling engine runs is that engine's to place on.
    void pending_tilingContextIsSkipped()
    {
        QVERIFY(m_service->placementStore().record(snappedRecord(QStringLiteral("app|t"), m_screenId, {m_zoneIds[0]})));
        QVERIFY(m_service->pendingRestoreGeometries().contains(QStringLiteral("app")));
        PhosphorZones::AssignmentEntry entry;
        entry.mode = PhosphorZones::AssignmentEntry::Autotile;
        entry.tilingAlgorithm = QStringLiteral("bsp");
        m_layoutManager->setAssignmentEntryDirect(m_screenId, 0, QString(), entry);
        QVERIFY(!m_service->pendingRestoreGeometries().contains(QStringLiteral("app")));
    }

    // A single-desktop record from a desktop the screen does not show is left
    // to the resolve; a record on every desktop (0) is served.
    void pending_singleDesktopRecordFromAnotherDesktopIsSkipped()
    {
        PhosphorWorkspaces::VirtualDesktopManager vdm;
        vdm.updateScreenDesktop(m_screenId, 1);
        auto service = localService(&vdm);
        auto elsewhere = snappedRecord(QStringLiteral("app|d2"), m_screenId, {m_zoneIds[0]});
        elsewhere.virtualDesktop = 2;
        QVERIFY(service->placementStore().record(elsewhere));
        QVERIFY(!service->pendingRestoreGeometries().contains(QStringLiteral("app")));
        auto everywhere = snappedRecord(QStringLiteral("all|d0"), m_screenId, {m_zoneIds[1]}, QStringLiteral("all"));
        QVERIFY(service->placementStore().record(everywhere));
        QVERIFY(service->pendingRestoreGeometries().contains(QStringLiteral("all")));
        service->setSnapState(nullptr);
    }

    // A floating snap slot still carrying its pre-float zones is not a snap
    // the resolve restores.
    void pending_floatingSlotIsSkipped()
    {
        auto rec = snappedRecord(QStringLiteral("app|f"), m_screenId, {m_zoneIds[0]});
        rec.engines[PhosphorEngine::WindowPlacement::snapEngineId()].state =
            PhosphorEngine::WindowPlacement::stateFloating();
        QVERIFY(m_service->placementStore().record(rec));
        QVERIFY(!m_service->pendingRestoreGeometries().contains(QStringLiteral("app")));
    }

    // A record on several desktops answers with the zone of the desktop its
    // screen shows, as the resolver restores it (F130), and with no zone
    // there it is not served.
    void pending_multiDesktopRecordServesTheShownDesktopsZone()
    {
        PhosphorWorkspaces::VirtualDesktopManager vdm;
        vdm.updateScreenDesktop(m_screenId, 2);
        auto service = localService(&vdm);
        auto rec = snappedRecord(QStringLiteral("app|multi"), m_screenId, {m_zoneIds[0]});
        rec.virtualDesktop = 1;
        rec.engines[PhosphorEngine::WindowPlacement::snapEngineId()].zonesByDesktop = {{1, {m_zoneIds[0]}},
                                                                                       {2, {m_zoneIds[1]}}};
        QVERIFY(service->placementStore().record(rec));
        const auto targets = service->pendingRestoreGeometries().value(QStringLiteral("app"));
        QCOMPARE(targets.size(), 1);
        QCOMPARE(targets.first().geometry, service->resolveZoneGeometry({m_zoneIds[1]}, m_screenId));
        QVERIFY(targets.first().geometry != service->resolveZoneGeometry({m_zoneIds[0]}, m_screenId));

        auto onlyOne = snappedRecord(QStringLiteral("one|solo"), m_screenId, {m_zoneIds[0]}, QStringLiteral("one"));
        onlyOne.virtualDesktop = 1;
        onlyOne.engines[PhosphorEngine::WindowPlacement::snapEngineId()].zonesByDesktop = {{1, {m_zoneIds[0]}}};
        QVERIFY(service->placementStore().record(onlyOne));
        QVERIFY(!service->pendingRestoreGeometries().contains(QStringLiteral("one")));
        service->setSnapState(nullptr);
    }

    // The layout gate reads the activity in view, not the record's: a record
    // from activity A in A's layout opens into activity B's layout (F205).
    void pending_layoutGateReadsTheLiveActivity()
    {
        PhosphorZones::Layout* layoutB = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layoutB);
        m_layoutManager->assignLayout(m_screenId, 0, QStringLiteral("act-a"), m_testLayout);
        m_layoutManager->assignLayout(m_screenId, 0, QStringLiteral("act-b"), layoutB);
        m_layoutManager->setCurrentActivity(QStringLiteral("act-b"));

        auto inA = snappedRecord(QStringLiteral("a|rec"), m_screenId, {m_zoneIds[0]}, QStringLiteral("a"));
        inA.activity = QStringLiteral("act-a");
        QVERIFY(m_service->placementStore().record(inA));
        auto inB = snappedRecord(QStringLiteral("b|rec"), m_screenId, {layoutB->zones().first()->id().toString()},
                                 QStringLiteral("b"));
        inB.activity = QStringLiteral("act-a");
        QVERIFY(m_service->placementStore().record(inB));

        const auto targets = m_service->pendingRestoreGeometries();
        m_layoutManager->setCurrentActivity(QString());
        QVERIFY2(!targets.contains(QStringLiteral("a")), "a zone of the layout another activity runs is not served");
        QVERIFY2(targets.contains(QStringLiteral("b")), "a zone of the layout in view is served");
    }

    // On each output the list stops at the first newer record the open claim
    // would take instead, here a floated sibling with a float spot (F285).
    // Every record has its own instance part: the store keys by instance.
    void pending_newerFloatedSiblingCutsTheOutput()
    {
        const QString dp2 = QStringLiteral("DP-2");
        const QRect spot(100, 100, 400, 300);
        // (i) Older snapped, newer floated on the same output: nothing served.
        QVERIFY(m_service->placementStore().record(
            snappedRecord(QStringLiteral("i|si"), m_screenId, {m_zoneIds[0]}, QStringLiteral("i"))));
        QVERIFY(m_service->placementStore().record(
            floatedRecord(QStringLiteral("i|fi"), m_screenId, m_screenId, spot, QStringLiteral("i"))));
        // (ii) The floated record on another output cuts nothing here.
        QVERIFY(m_service->placementStore().record(
            snappedRecord(QStringLiteral("ii|sii"), m_screenId, {m_zoneIds[0]}, QStringLiteral("ii"))));
        QVERIFY(m_service->placementStore().record(
            floatedRecord(QStringLiteral("ii|fii"), dp2, dp2, spot, QStringLiteral("ii"))));
        // (iii) Oldest snapped, then floated, then newest snapped: the newest only.
        QVERIFY(m_service->placementStore().record(
            snappedRecord(QStringLiteral("iii|s2iii"), m_screenId, {m_zoneIds[1]}, QStringLiteral("iii"))));
        QVERIFY(m_service->placementStore().record(
            floatedRecord(QStringLiteral("iii|fiii"), m_screenId, m_screenId, spot, QStringLiteral("iii"))));
        QVERIFY(m_service->placementStore().record(
            snappedRecord(QStringLiteral("iii|s1iii"), m_screenId, {m_zoneIds[0]}, QStringLiteral("iii"))));
        // (iv) An unscreened floated record with a spot on this output cuts it.
        QVERIFY(m_service->placementStore().record(
            snappedRecord(QStringLiteral("iv|siv"), m_screenId, {m_zoneIds[0]}, QStringLiteral("iv"))));
        QVERIFY(m_service->placementStore().record(
            floatedRecord(QStringLiteral("iv|fiv"), QString(), m_screenId, spot, QStringLiteral("iv"))));

        const auto targets = m_service->pendingRestoreGeometries();
        QVERIFY2(!targets.contains(QStringLiteral("i")), "a newer floated sibling on the output cuts the snap");
        QCOMPARE(targets.value(QStringLiteral("ii")).size(), 1);
        QCOMPARE(targets.value(QStringLiteral("ii")).first().windowId, QStringLiteral("ii|sii"));
        QCOMPARE(targets.value(QStringLiteral("iii")).size(), 1);
        QCOMPARE(targets.value(QStringLiteral("iii")).first().windowId, QStringLiteral("iii|s1iii"));
        QVERIFY2(!targets.contains(QStringLiteral("iv")), "an unscreened floated sibling with a spot here cuts it");
    }

private:
    static PhosphorEngine::WindowPlacement snappedRecord(const QString& windowId, const QString& screenId,
                                                         const QStringList& zones,
                                                         const QString& appId = QStringLiteral("app"))
    {
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = windowId;
        rec.appId = appId;
        rec.screenId = screenId;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = zones;
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        return rec;
    }

    static PhosphorEngine::WindowPlacement floatedRecord(const QString& windowId, const QString& screenId,
                                                         const QString& spotScreen, const QRect& spot,
                                                         const QString& appId)
    {
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = windowId;
        rec.appId = appId;
        rec.screenId = screenId;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(spotScreen, spot);
        return rec;
    }

    /// A service on @p vdm sharing the fixture's layouts, registry and snap
    /// state. The caller detaches the snap state before it returns.
    std::unique_ptr<PhosphorPlacement::WindowTrackingService>
    localService(PhosphorWorkspaces::VirtualDesktopManager* vdm) const
    {
        auto service = std::make_unique<PhosphorPlacement::WindowTrackingService>(m_layoutManager, nullptr, vdm);
        service->setSnapState(m_snapState);
        service->setWindowRegistry(m_registry);
        return service;
    }

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
