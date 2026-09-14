// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_desktop_membership.cpp
 *
 * The snap engine's per-desktop membership arm. Snapping places nothing by
 * itself, so adoption grants a window a store of its own on the desktop in
 * view, and what that buys is that snapping it there lands in ITS OWN
 * assignment instead of overwriting the zone it holds on another desktop.
 * The rest of the suite pins the bookkeeping around that: the persisted
 * per-desktop zone map, the close and release paths that have to clear every
 * member store, and the restore that seeds the other desktops' zones back.
 */

#include <QGuiApplication>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <memory>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;
using PhosphorEngine::WindowPlacement;
using PhosphorSnapEngine::SnapEngine;
using PhosphorSnapEngine::SnapState;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kScreen = QStringLiteral("DP-1");
const QString kWindow = QStringLiteral("app|11111111-2222-3333-4444-555555555555");
const QString kOther = QStringLiteral("app|66666666-7777-8888-9999-000000000000");

PhosphorEngine::DesktopSpan sticky()
{
    PhosphorEngine::DesktopSpan span;
    span.known = true;
    span.sticky = true;
    return span;
}

PhosphorEngine::DesktopSpan on(QSet<int> desktops)
{
    PhosphorEngine::DesktopSpan span;
    span.known = true;
    span.desktops = std::move(desktops);
    return span;
}

PhosphorEngine::DesktopSpanQuery spanOf(const PhosphorEngine::DesktopSpan& windowSpan)
{
    return [windowSpan](const QString& windowId) {
        return windowId == kWindow ? windowSpan : on({1});
    };
}
} // namespace

class TestSnapDesktopMembership : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        m_service->setSnapEngine(m_engine);
        installFullResolver();

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
        m_service->setSnapEngine(nullptr);
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

    // The defect the whole change exists for: a sticky window snapped on
    // desktop 1 and then on desktop 2 shared ONE zone across both. Adoption
    // gives desktop 2 its own store, and the second snap lands there.
    void adoptedDesktopGetsItsOwnZoneAssignment()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const auto result = m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QCOMPARE(result.adopted.size(), 1);
        QCOMPARE(result.adopted.first().first, kWindow);
        // Membership only: nothing is snapped on desktop 2 until the user
        // snaps it there.
        QVERIFY(!m_engine->stateForWindow(kWindow)->isWindowSnapped(kWindow));
        // The held key still answers, from the store that genuinely holds
        // the window, so the daemon's membership reconcile never reads an
        // adopted-but-unsnapped window as untracked.
        QVERIFY(m_engine->heldKeyForWindow(kWindow).has_value());
        QCOMPARE(m_engine->heldKeyForWindow(kWindow)->desktop, 1);

        snapOn(2, kWindow, m_zoneIds[1]);
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        // The primary membership follows the desktop in view.
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QCOMPARE(m_engine->heldKeyForWindow(kWindow)->desktop, 2);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QCOMPARE(m_engine->heldKeyForWindow(kWindow)->desktop, 1);
        // A second pass is idempotent.
        QVERIFY(m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky())).isEmpty());
    }

    // The capture that feeds the persisted record names every desktop's zone,
    // and does so whether the window is snapped or floating on the desktop
    // in view.
    void captureNamesEveryDesktopsZone()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        auto rec = m_engine->capturePlacement(kWindow);
        QVERIFY(rec.has_value());
        PhosphorEngine::EngineSlot slot = rec->slotFor(WindowPlacement::snapEngineId());
        QCOMPARE(slot.state, WindowPlacement::stateSnapped());
        QCOMPARE(slot.zonesByDesktop.value(1), QStringList{m_zoneIds[0]});
        QCOMPARE(slot.zonesByDesktop.value(2), QStringList{m_zoneIds[1]});

        // Floated on desktop 2, still snapped on desktop 1: the record must
        // keep saying so, or a restart brings the window back floating on
        // every desktop.
        m_engine->setWindowFloat(kWindow, true, kScreen);
        rec = m_engine->capturePlacement(kWindow);
        QVERIFY(rec.has_value());
        slot = rec->slotFor(WindowPlacement::snapEngineId());
        QCOMPARE(slot.state, WindowPlacement::stateFloating());
        QCOMPARE(slot.zonesByDesktop.value(1), QStringList{m_zoneIds[0]});
    }

    // Adopted into the desktop in view but not snapped there, the window is
    // still this engine's (a background store holds it), and its capture
    // says so: floating here, with the other desktops' zones kept, on the
    // screen the member store names.
    void adoptedButUnsnappedDesktopIsTrackedAndCapturesItsOtherZones()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QVERIFY(m_engine->isWindowTracked(kWindow));

        const auto rec = m_engine->capturePlacement(kWindow);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->screenId, kScreen);
        const PhosphorEngine::EngineSlot slot = rec->slotFor(WindowPlacement::snapEngineId());
        QCOMPARE(slot.state, WindowPlacement::stateFloating());
        QCOMPARE(slot.zonesByDesktop.value(1), QStringList{m_zoneIds[0]});
        QVERIFY(!slot.zonesByDesktop.contains(2));
    }

    // A span that stops covering a desktop releases that desktop's zone AND
    // forgets its persisted entry through the tracker, because the store
    // merges the map and a capture's silence would leave the zone on disk.
    void releaseClearsTheZoneAndForgetsThePersistedEntry()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        m_service->placementStore().record(*m_engine->capturePlacement(kWindow));
        QVERIFY(persistedZonesByDesktop(kWindow).contains(1));

        // Un-stuck onto desktop 2 while desktop 2 is in view.
        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QCOMPARE(result.released.size(), 1);
        QCOMPARE(result.released.first().second.desktop, 1);
        QVERIFY(zonesOn(1, kWindow).isEmpty());
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QVERIFY2(!persistedZonesByDesktop(kWindow).contains(1), "the forget must reach the record");
        QVERIFY(persistedZonesByDesktop(kWindow).contains(2));
    }

    // Unsnapping on one desktop forgets that desktop's persisted entry too:
    // "unsnapped here" is not representable in a capture (it names only
    // desktops with zones), so the forget is the only thing that keeps a
    // restart from snapping the window back into the zone it just left.
    void unsnapForgetsThatDesktopsPersistedEntry()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        m_service->placementStore().record(*m_engine->capturePlacement(kWindow));
        QVERIFY(persistedZonesByDesktop(kWindow).contains(2));

        m_service->unassignWindow(kWindow); // the desktop in view is 2
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        QVERIFY2(!persistedZonesByDesktop(kWindow).contains(2), "the unsnap must forget desktop 2's zone");
        QVERIFY(persistedZonesByDesktop(kWindow).contains(1));
    }

    // A closing window leaves EVERY member store, not the one in view: zone
    // occupancy is read across every store, so a leftover is a phantom
    // occupant of its zone on that desktop.
    void closingAMultiDesktopWindowClearsEveryStore()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        snapOn(1, kOther, m_zoneIds[2]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        m_engine->forgetWindow(kWindow);
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QVERIFY2(zonesOn(1, kWindow).isEmpty(), "the background store must drop the window");
        QVERIFY(!m_engine->heldKeyForWindow(kWindow).has_value());
        QVERIFY(!m_engine->isWindowTracked(kWindow));
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY2(!m_service->windowsInZone(m_zoneIds[0]).contains(kWindow), "no phantom zone occupant");
        QCOMPARE(zonesOn(1, kOther), QStringList{m_zoneIds[2]});
    }

    // The aggregate queries see a window in every member store: it occupies
    // its zone on each desktop, and the flat list names it once however many
    // stores hold it.
    void aggregateQueriesSeeEveryMemberStoreOnce()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        QVERIFY(m_service->windowsInZone(m_zoneIds[0]).contains(kWindow));
        QVERIFY(m_service->windowsInZone(m_zoneIds[1]).contains(kWindow));
        QCOMPARE(m_service->snappedWindows().count(kWindow), 1);
        // The store the window is NOT a member of holds nothing for it: the
        // eviction spares member stores only.
        QVERIFY(zonesOn(3, kWindow).isEmpty());
    }

    // Restore: the record names a zone per desktop; the desktop the window
    // is being placed onto takes its own zone and the others are seeded back
    // into their stores, so switching to them after a restart finds the
    // window placed. The registry knows which desktop that is (the effect
    // stamps it before the resolve), and it is not necessarily the one the
    // screen is showing.
    void restoreSeedsTheOtherDesktopsZones()
    {
        PhosphorEngine::WindowRegistry registry;
        registry.canonicalizeWindowId(kWindow);
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.title = QStringLiteral("t");
        meta.virtualDesktop = 2;
        registry.upsert(QStringLiteral("11111111-2222-3333-4444-555555555555"), meta);
        m_engine->setWindowRegistry(&registry);
        m_service->placementStore().record(multiDesktopRecord());

        // The screen shows desktop 1; the window is restored onto desktop 2.
        // The context gate is asked about the desktop being restored ONTO.
        int gatedDesktop = 0;
        m_engine->setShouldRestorePredicate([&gatedDesktop](const QString&, int desktop) {
            gatedDesktop = desktop;
            return true;
        });
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        const PhosphorEngine::SnapResult result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY(result.shouldSnap);
        QCOMPARE(result.zoneIds, QStringList{m_zoneIds[1]});
        QCOMPARE(result.virtualDesktop, 2);
        QCOMPARE(gatedDesktop, 2);
        // Desktop 1's zone came back into desktop 1's store, under a
        // membership, so the restore desktop's commit lands in its own.
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QVERIFY(m_engine->heldKeyForWindow(kWindow).has_value());

        // The commit the caller makes is pinned to the restore desktop, so
        // with the screen still showing desktop 1 it lands in desktop 2's
        // store and leaves desktop 1's zone alone.
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_service->assignWindowToZone(kWindow, result.zoneIds.first(), kScreen, result.virtualDesktop);
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        m_engine->setShouldRestorePredicate({});
        m_engine->setWindowRegistry(nullptr);
    }

    // The per-desktop restore decision. A record captured with the SNAPPED
    // desktop in view (state snapped, flat zoneIds z0, map {1: z0}) restored
    // onto desktop 2, where the window was unsnapped, must float there and
    // seed desktop 1, not re-snap z0 from the flat zoneIds; and a record
    // captured FLOATING on one desktop whose map names a zone for the
    // restore desktop must snap there.
    void restoreDecidesSnappedOrFloatedPerDesktop()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        auto captured = m_engine->capturePlacement(kWindow);
        QVERIFY(captured.has_value());
        QCOMPARE(captured->slotFor(WindowPlacement::snapEngineId()).state, WindowPlacement::stateSnapped());
        m_service->placementStore().record(*captured);
        m_engine->forgetWindow(kWindow);

        PhosphorEngine::WindowRegistry registry;
        registry.canonicalizeWindowId(kWindow);
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.title = QStringLiteral("t");
        meta.virtualDesktop = 2;
        registry.upsert(QStringLiteral("11111111-2222-3333-4444-555555555555"), meta);
        m_engine->setWindowRegistry(&registry);

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        PhosphorEngine::SnapResult result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY2(!result.shouldSnap, "desktop 2 was unsnapped at capture time");
        QVERIFY(m_engine->isFloating(kWindow));
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        // No pre-float zone on desktop 2: the flat zoneIds are desktop 1's,
        // and Meta+F on desktop 2 must not unfloat into them.
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QVERIFY(static_cast<SnapState*>(m_engine->stateForScreen(kScreen))->preFloatZones(kWindow).isEmpty());

        // The other leg: floating on desktop 1 at capture, zone z1 on desktop 2.
        m_engine->forgetWindow(kWindow);
        WindowPlacement floated = multiDesktopRecord();
        floated.engines[WindowPlacement::snapEngineId()].state = WindowPlacement::stateFloating();
        m_service->placementStore().record(floated);
        result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY2(result.shouldSnap, "the map names a zone on the restore desktop");
        QCOMPARE(result.zoneIds, QStringList{m_zoneIds[1]});
        QCOMPARE(result.virtualDesktop, 2);
        m_engine->setWindowRegistry(nullptr);
    }

    // A restore whose registry span no longer covers a desktop the record
    // names seeds nothing there and drops the persisted entry, instead of
    // placing a phantom the membership pass would only release again.
    void restoreSkipsDesktopsTheWindowIsNoLongerOn()
    {
        PhosphorEngine::WindowRegistry registry;
        registry.canonicalizeWindowId(kWindow);
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.title = QStringLiteral("t");
        meta.virtualDesktop = 2;
        meta.virtualDesktops = {2, 3};
        registry.upsert(QStringLiteral("11111111-2222-3333-4444-555555555555"), meta);
        m_engine->setWindowRegistry(&registry);
        WindowPlacement rec = multiDesktopRecord();
        rec.engines[WindowPlacement::snapEngineId()].zonesByDesktop.insert(3, {m_zoneIds[2]});
        m_service->placementStore().record(rec);

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const PhosphorEngine::SnapResult result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY(result.shouldSnap);
        QCOMPARE(result.zoneIds, QStringList{m_zoneIds[1]});
        QVERIFY2(zonesOn(1, kWindow).isEmpty(), "desktop 1 is not in the window's span");
        QCOMPARE(zonesOn(3, kWindow), QStringList{m_zoneIds[2]});
        QVERIFY(!persistedZonesByDesktop(kWindow).contains(1));
        m_engine->setWindowRegistry(nullptr);
    }

    // Floating on a desktop the window was adopted into but never snapped on
    // records the float's residence (screen and desktop) in that desktop's
    // store: a bare float bit would leave the capture screenless. The other
    // desktop's zone is untouched.
    void floatOnAnAdoptedDesktopRecordsItsResidence()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        m_engine->setWindowFloat(kWindow, true, kScreen);
        QVERIFY(m_engine->isFloating(kWindow));

        SnapState* d2 = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        QVERIFY(d2 != nullptr);
        QCOMPARE(d2->screenForWindow(kWindow), kScreen);
        QCOMPARE(d2->desktopForWindow(kWindow), 2);
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        const auto rec = m_engine->capturePlacement(kWindow);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->screenId, kScreen);
    }

    // The service's float path forgets the floated desktop's persisted entry
    // (its own unassign does the same), so a restart cannot seed the zone
    // back as a live snap on the desktop the user floated it on.
    void unsnapForFloatForgetsThatDesktopsPersistedEntry()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        m_service->placementStore().record(*m_engine->capturePlacement(kWindow));
        QVERIFY(persistedZonesByDesktop(kWindow).contains(2));

        m_service->unsnapForFloat(kWindow); // the desktop in view is 2
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        QVERIFY2(!persistedZonesByDesktop(kWindow).contains(2), "the float must forget desktop 2's zone");
        QVERIFY(persistedZonesByDesktop(kWindow).contains(1));
    }

    // A screen a tiling engine owns keeps its snap memberships as frozen
    // memory, and the membership pass must not re-commit them on a desktop
    // switch there: live, that re-commit fought the tiling engine's own
    // placement on every switch.
    void switchOnATilingScreenDoesNotReapplySnapZones()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        m_engine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Autotile;
        });
        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY(m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky())).isEmpty());
        QCOMPARE(resnapSpy.count(), 0);
        // The memberships are kept for the return to snapping.
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});

        m_engine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QVERIFY2(resnapSpy.count() > 0, "back in snapping mode the switch re-applies the desktop's zone");
        m_engine->setLiveModeResolver({});
    }

    // A handoff to a tiling engine releases the CONTEXT it took the window
    // in, not the window: a membership on another desktop still in snapping
    // mode keeps its zone, so switching there finds the window snapped and
    // switching back finds it tiled. With no such context left, the window
    // is forgotten outright, as before.
    void handoffReleaseKeepsTheOtherSnappingDesktop()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        // Desktop 1 flips to tiling and the tiling engine takes the window there.
        m_layoutManager->assignLayoutById(kScreen, 1, QString(), QStringLiteral("autotile:bsp"));
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->handoffRelease(kWindow);
        QVERIFY(zonesOn(1, kWindow).isEmpty());
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QVERIFY(m_engine->isWindowTracked(kWindow));
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QCOMPARE(m_engine->heldKeyForWindow(kWindow)->desktop, 2);

        // Desktop 2 flips too: nothing snapping is left and the window goes.
        m_layoutManager->assignLayoutById(kScreen, 2, QString(), QStringLiteral("autotile:bsp"));
        m_engine->handoffRelease(kWindow);
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QVERIFY(!m_engine->isWindowTracked(kWindow));
        QVERIFY(!m_engine->heldKeyForWindow(kWindow).has_value());
    }

    // Adoption puts back the zone the durable record remembers for the
    // desktop entered: a screen that went to tiling and came back released
    // every context, and the record's per-desktop map is what survives.
    void adoptionRestoresTheRememberedZoneForThatDesktop()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        m_service->placementStore().record(*m_engine->capturePlacement(kWindow));
        m_engine->forgetWindow(kWindow); // the handoff dropped every context

        // Re-snapped on desktop 1 (the return resnap); desktop 2 is entered.
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const auto result = m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QCOMPARE(result.adopted.size(), 1);
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
    }

    // Removing a desktop renumbers every bare desktop number the service
    // keeps, not only the persisted map: a pending restore queued across the
    // removal must land on the desktop it was queued for.
    void removingADesktopRenumbersPendingRestores()
    {
        PhosphorEngine::PendingRestore onThree;
        onThree.zoneIds = {m_zoneIds[0]};
        onThree.screenId = kScreen;
        onThree.virtualDesktop = 3;
        PhosphorEngine::PendingRestore onTwo = onThree;
        onTwo.virtualDesktop = 2;
        m_service->setPendingRestoreQueues({{QStringLiteral("app"), {onThree, onTwo}}});
        m_service->clearDirty();

        m_engine->renumberDesktopsAfterRemoval(2);
        const QList<PhosphorEngine::PendingRestore> queue =
            m_service->pendingRestoreQueues().value(QStringLiteral("app"));
        QCOMPARE(queue.size(), 2);
        QCOMPARE(queue.at(0).virtualDesktop, 2);
        QCOMPARE(queue.at(1).virtualDesktop, 0);
        QVERIFY2(m_service->peekDirty() != 0, "the shift must mark the service dirty");
    }

    // The close the daemon relays goes through the service, and has to reach
    // every member store the same way the engine's own forget does.
    void closeThroughTheServiceClearsEveryStore()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        m_service->windowClosed(kWindow);
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QVERIFY(zonesOn(1, kWindow).isEmpty());
        QVERIFY(!m_engine->heldKeyForWindow(kWindow).has_value());
        QVERIFY(!m_engine->isWindowTracked(kWindow));
    }

    // Without a registry answer the record's own desktop is the best
    // witness, ahead of whatever the screen happens to be showing.
    void restoreWithoutRegistryFollowsTheRecordsDesktop()
    {
        m_service->placementStore().record(multiDesktopRecord());
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const PhosphorEngine::SnapResult result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY(result.shouldSnap);
        QCOMPARE(result.zoneIds, QStringList{m_zoneIds[0]});
        QCOMPARE(result.virtualDesktop, 1);
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
    }

private:
    /// A record snapped in zone 0 on desktop 1 and zone 1 on desktop 2.
    WindowPlacement multiDesktopRecord() const
    {
        WindowPlacement rec;
        rec.windowId = kWindow;
        rec.appId = QStringLiteral("app");
        rec.screenId = kScreen;
        rec.virtualDesktop = 1;
        PhosphorEngine::EngineSlot slot;
        slot.state = WindowPlacement::stateSnapped();
        slot.zoneIds = {m_zoneIds[0]};
        slot.zonesByDesktop.insert(1, {m_zoneIds[0]});
        slot.zonesByDesktop.insert(2, {m_zoneIds[1]});
        rec.engines.insert(WindowPlacement::snapEngineId(), slot);
        return rec;
    }

    void installFullResolver()
    {
        PhosphorPlacement::WindowTrackingService::SnapStateResolver resolver;
        resolver.forWindow = [e = m_engine](const QString& id) {
            return e->stateForWindow(id);
        };
        resolver.forWindowOnScreen = [e = m_engine](const QString& id, const QString& s, int desktop) {
            return e->stateForWindowOnScreen(id, s, desktop);
        };
        resolver.forScreen = [e = m_engine](const QString& s) {
            return static_cast<SnapState*>(e->stateForScreen(s));
        };
        resolver.globals = [e = m_engine]() {
            return e->globalState();
        };
        resolver.allStates = [e = m_engine]() {
            return e->allSnapStates();
        };
        resolver.forgetWindow = [e = m_engine](const QString& id) {
            e->forgetWindow(id);
        };
        resolver.holdsWindow = [e = m_engine](const QString& id, const SnapState* state) {
            return e->holdsWindowInState(id, state);
        };
        m_service->setSnapStateResolver(resolver);
    }

    /// Snap @p windowId into @p zoneId on @p desktop of kScreen through the
    /// service, the way a commit does.
    void snapOn(int desktop, const QString& windowId, const QString& zoneId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        m_service->assignWindowToZone(windowId, zoneId, kScreen, desktop);
    }

    /// The zones @p windowId holds on @p desktop's store, read from that store.
    QStringList zonesOn(int desktop, const QString& windowId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        SnapState* state = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        return state ? state->zonesForWindow(windowId) : QStringList{};
    }

    QHash<int, QStringList> persistedZonesByDesktop(const QString& windowId) const
    {
        const auto rec = m_service->placementStore().peekExact(windowId);
        return rec ? rec->slotFor(WindowPlacement::snapEngineId()).zonesByDesktop : QHash<int, QStringList>{};
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestSnapDesktopMembership)
#include "test_snap_desktop_membership.moc"
