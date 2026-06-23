// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_lifecycle.cpp
 * @brief Unit tests for WindowTrackingService lifecycle: windowClosed and onLayoutChanged
 *
 * Tests cover:
 * 1. Window close -> pending zone persistence (P0 crash/data-loss)
 * 2. Pre-snap geometry stable ID migration on close
 * 3. Pre-float zone conversion on close
 * 4. PhosphorZones::Layout change -> stale assignment removal and resnap buffer
 * 5. State change signal emission
 *
 * WIRE FORMAT NOTE: These tests construct WTS without a WindowRegistry, so
 * they drive legacy-compat "appId|uuid" composite fixtures to exercise the
 * PhosphorIdentity::WindowId::extractAppId fallback path inside currentAppIdFor(). Production
 * daemons set a registry and receive bare instance ids — see
 * test_wts_registry_integration.cpp and test_wta_reactive_metadata.cpp for
 * coverage of the live path.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QSignalSpy>
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
#include "../helpers/LayoutRegistryTestHelpers.h"

#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PhosphorEngine::ZoneAssignmentEntry;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Test Class
// =========================================================================

class TestWtsLifecycle : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        // Pass nullptr as parent to avoid double-delete: cleanup() deletes manually
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
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
    // P0: Window Close -> Pending PhosphorZones::Zone Persistence
    // =====================================================================

    void testWindowClosed_persistsZoneToPending()
    {
        QString windowId = QStringLiteral("firefox|12345");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        QVERIFY(m_service->isWindowSnapped(windowId));

        m_service->windowClosed(windowId);

        QVERIFY(!m_service->isWindowSnapped(windowId));
        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
        QCOMPARE(m_service->pendingRestoreQueues().value(appId).first().zoneIds.first(), m_zoneIds[0]);
    }

    void testWindowClosed_floatingWindowNotPersisted()
    {
        QString windowId = QStringLiteral("firefox|12345");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        m_service->setWindowFloating(windowId, true);

        m_service->windowClosed(windowId);

        QVERIFY(!m_service->pendingRestoreQueues().contains(appId));
    }

    // testWindowClosed_preTileGeometryConvertedToStableId removed: the per-engine
    // unmanaged-geometry store and its windowId→appId alias copy on close were
    // collapsed into the unified WindowPlacementStore, whose record already lives in
    // its appId bucket (so the appId fallback finds the float-back on reopen with no
    // manual copy). Covered by the WindowPlacementStore appId-FIFO tests.

    void testWindowClosed_floatStateClearedOnClose()
    {
        QString windowId = QStringLiteral("org.kde.kate|55555");
        QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[1], QStringLiteral("DP-1"), 1);
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);

        QCOMPARE(m_service->preFloatZone(windowId), m_zoneIds[1]);
        QVERIFY(m_service->isWindowFloating(windowId));

        m_service->windowClosed(windowId);

        // Float state and pre-float zones should be fully cleared on close
        QVERIFY(!m_service->isWindowFloating(windowId));
        QVERIFY(!m_service->isWindowFloating(appId));
        QVERIFY(m_service->preFloatZone(appId).isEmpty());
    }

    void testWindowClosed_scheduleSaveStateCalled()
    {
        QString windowId = QStringLiteral("app|12345");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);

        QSignalSpy spy(m_service, &PhosphorPlacement::WindowTrackingService::stateChanged);
        m_service->windowClosed(windowId);

        QVERIFY(spy.count() >= 1);
    }

    void testWindowClosed_skipsPendingRestoreWhenPredicateRejects()
    {
        // Discussion #461 item 2: a window closing on a monitor/desktop the
        // user has disabled snapping for must not record a PendingRestore.
        // Without the gate, the entry resurfaces when the same app reopens
        // anywhere — yanking the window into a zone the user told us to
        // leave alone. The predicate returns false for the disabled screen
        // AND asserts the argument tuple so a future signature reshuffle
        // (swapping screenId/desktop, etc.) trips this test rather than
        // silently passing.
        const QString disabledScreen = QStringLiteral("AOC:24B2W1G5:116");
        int predicateCallCount = 0;
        QString lastScreenId;
        int lastDesktop = -1;
        m_service->setShouldTrackPredicate([&](const QString& screenId, int desktop) {
            ++predicateCallCount;
            lastScreenId = screenId;
            lastDesktop = desktop;
            return false;
        });

        const QString windowId = QStringLiteral("vesktop|deadbeef-0000-0000-0000-000000000001");
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        // Plain snapped setup — predicate gate behaviour holds without
        // depending on incidental float churn. Float-clearing on close has
        // its own dedicated test (testWindowClosed_floatStateClearedOnClose).
        m_service->assignWindowToZone(windowId, m_zoneIds[0], disabledScreen, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));

        QSignalSpy stateSpy(m_service, &PhosphorPlacement::WindowTrackingService::stateChanged);
        m_service->windowClosed(windowId);

        // Predicate-argument contract: exactly one invocation, and the
        // tuple it received matches what the placement library promised
        // to pass (current screen, current desktop). Comparing fields
        // individually gives a useful failure message — "Expected DP-1
        // got DP-2" instead of "everyCallMatched was false".
        QCOMPARE(predicateCallCount, 1);
        QCOMPARE(lastScreenId, disabledScreen);
        QCOMPARE(lastDesktop, 1);

        // The rest of windowClosed's cleanup must still run even when the
        // pending-restore write is suppressed: zone unassigned, floating
        // state cleared (both windowId and appId keys), stateChanged
        // emitted. A silent regression in any of these would leak just as
        // badly as the original bug.
        QVERIFY(!m_service->isWindowSnapped(windowId));
        QVERIFY(!m_service->pendingRestoreQueues().contains(appId));
        QVERIFY(!m_service->isWindowFloating(windowId));
        QVERIFY(!m_service->isWindowFloating(appId));
        QVERIFY(stateSpy.count() >= 1);
    }

    void testWindowClosed_predicateAcceptsEnabledContext()
    {
        // Sanity counterpart: when the predicate accepts the closing context,
        // the historical persist-on-close behavior is preserved. Same
        // accumulator pattern — a regression that fires the predicate
        // with the wrong tuple, or fires it more than once with mismatched
        // arguments, is caught.
        int predicateCallCount = 0;
        QString lastScreenId;
        int lastDesktop = -1;
        m_service->setShouldTrackPredicate([&](const QString& screenId, int desktop) {
            ++predicateCallCount;
            lastScreenId = screenId;
            lastDesktop = desktop;
            return true;
        });

        const QString windowId = QStringLiteral("firefox|cafef00d-0000-0000-0000-000000000001");
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        m_service->windowClosed(windowId);

        QCOMPARE(predicateCallCount, 1);
        QCOMPARE(lastScreenId, QStringLiteral("DP-1"));
        QCOMPARE(lastDesktop, 1);
        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
    }

    void testWindowClosed_persistsWhenPredicateUnset()
    {
        // Production daemons always wire a predicate via WTA, but unit tests
        // and library consumers may construct WTS without one. The header's
        // ShouldTrackPredicate contract promises "When unset, the service
        // behaves as if every context is active." Lock that explicitly with
        // a round-trip: install a rejecting predicate then clear it, and
        // confirm the unset-equivalent persist-everything behaviour is
        // restored. Catches a future bug where the setter only stores
        // non-empty functions, or where clearing leaks the prior predicate.
        m_service->setShouldTrackPredicate([](const QString&, int) {
            return false;
        });
        m_service->setShouldTrackPredicate({});

        const QString windowId = QStringLiteral("alacritty|11112222-3333-4444-5555-666677778888");
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        m_service->windowClosed(windowId);

        QVERIFY(m_service->pendingRestoreQueues().contains(appId));
    }

    void testWindowClosed_persistsZoneToPending_virtualScreen()
    {
        // Same as testWindowClosed_persistsZoneToPending but using a virtual screen ID.
        // Verifies that the pending restore queue entry records the virtual screen ID
        // rather than falling back to the physical screen ID.
        const QString windowId = QStringLiteral("konsole|abcdef12-0000-0000-0000-000000000001");
        const QString vsId = QStringLiteral("DP-1/vs:0");
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        m_service->assignWindowToZone(windowId, m_zoneIds[1], vsId, 1);
        QVERIFY(m_service->isWindowSnapped(windowId));
        QCOMPARE(m_service->zoneForWindow(windowId), m_zoneIds[1]);

        m_service->windowClosed(windowId);

        QVERIFY(!m_service->isWindowSnapped(windowId));
        QVERIFY(m_service->pendingRestoreQueues().contains(appId));

        const auto& queue = m_service->pendingRestoreQueues().value(appId);
        QVERIFY(!queue.isEmpty());

        const auto& entry = queue.first();
        QCOMPARE(entry.zoneIds.first(), m_zoneIds[1]);
        QCOMPARE(entry.screenId, vsId);
    }

    // =====================================================================
    // P0: PhosphorZones::Layout Change
    // =====================================================================

    void testOnLayoutChanged_staleAssignmentsRemoved()
    {
        QString windowId = QStringLiteral("app|12345");
        QString screen = QStringLiteral("DP-1");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], screen, 0);
        QVERIFY(m_service->isWindowSnapped(windowId));

        PhosphorZones::Layout* newLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->assignLayout(screen, m_layoutManager->currentVirtualDesktop(), QString(), newLayout);
        m_layoutManager->setActiveLayout(newLayout);

        m_service->onLayoutChanged();

        QVERIFY(!m_service->isWindowSnapped(windowId));
    }

    void testOnLayoutChanged_resnapBufferPopulated()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_service->assignWindowToZone(window1, m_zoneIds[0], QString(), 0);
        m_service->assignWindowToZone(window2, m_zoneIds[1], QString(), 0);

        PhosphorZones::Layout* newLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<ZoneAssignmentEntry> resnap = m_engine->calculateResnapFromPreviousLayout();
        // Two windows were assigned above, so the resnap buffer should contain
        // entries for both (mapped to the new layout's zones by relative position).
        // In headless mode zone geometry resolution may differ, but the buffer
        // must still be populated with the window IDs that were snapped.
        QVERIFY2(!resnap.isEmpty(), "Resnap buffer must contain entries for the previously-snapped windows");
        QCOMPARE(resnap.size(), 2);
    }

    void testResnapFromAutotileOrder_preClaimedZoneSkippedByPositionalFallback()
    {
        // window A has a recorded zone (zone[0]); window B has none, so it goes
        // through the positional fallback. A zone reserved by ANOTHER restore
        // producer (passed as preClaimedZoneIds) must be skipped by the fallback
        // so B never lands on the reserved zone — the two-windows-one-zone
        // collision this parameter exists to prevent.
        const QString winA = QStringLiteral("appA|aaaa");
        const QString winB = QStringLiteral("appB|bbbb");
        m_service->assignWindowToZone(winA, m_zoneIds[0], QString(), 0);

        const QStringList order{winA, winB};

        // Control: no pre-claim → B takes the first unclaimed zone (zone[1]).
        QVector<ZoneAssignmentEntry> noClaim = m_engine->calculateResnapEntriesFromAutotileOrder(order, QString());
        QVERIFY2(!noClaim.isEmpty(), "resnap must produce entries (zone geometry must resolve in fixture)");
        ZoneAssignmentEntry bNoClaim;
        for (const ZoneAssignmentEntry& e : noClaim) {
            if (e.windowId == winB)
                bNoClaim = e;
        }
        QCOMPARE(bNoClaim.targetZoneId, m_zoneIds[1]);

        // With zone[1] pre-claimed by another producer, B must avoid it and take
        // the next unclaimed zone (zone[2]).
        QVector<ZoneAssignmentEntry> claimed =
            m_engine->calculateResnapEntriesFromAutotileOrder(order, QString(), QStringList{m_zoneIds[1]});
        ZoneAssignmentEntry bClaimed;
        for (const ZoneAssignmentEntry& e : claimed) {
            if (e.windowId == winB)
                bClaimed = e;
        }
        QVERIFY2(bClaimed.targetZoneId != m_zoneIds[1], "B must not be assigned the pre-claimed zone");
        QCOMPARE(bClaimed.targetZoneId, m_zoneIds[2]);
        // A still restores to its OWN recorded zone regardless of the pre-claim.
        ZoneAssignmentEntry aClaimed;
        for (const ZoneAssignmentEntry& e : claimed) {
            if (e.windowId == winA)
                aClaimed = e;
        }
        QCOMPARE(aClaimed.targetZoneId, m_zoneIds[0]);
    }

    void testResnapFromAutotileOrder_preClaimedZoneStacksPass1Window()
    {
        // window A's OWN recorded zone is the one another producer reserved. Because
        // snapping supports multiple windows per zone, A STILL restores to its own
        // recorded zone (stacked with the reserver) — pass-1 does not yield on an
        // occupied zone; only the positional fallback avoids occupied zones.
        const QString winA = QStringLiteral("appA|aaaa");
        m_service->assignWindowToZone(winA, m_zoneIds[1], QString(), 0);

        QVector<ZoneAssignmentEntry> claimed =
            m_engine->calculateResnapEntriesFromAutotileOrder(QStringList{winA}, QString(), QStringList{m_zoneIds[1]});
        ZoneAssignmentEntry a;
        for (const ZoneAssignmentEntry& e : claimed) {
            if (e.windowId == winA)
                a = e;
        }
        QCOMPARE(a.targetZoneId, m_zoneIds[1]);
    }

    void testRecordFreeGeometry_refusesSnappedFrame()
    {
        // A snapped window's live frame IS the zone rect. Recording it as free
        // geometry would poison the float-back with the snapped geometry — the
        // single write point must refuse it.
        const QString windowId = QStringLiteral("firefox|cccc");
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);
        const QString screen = QStringLiteral("DP-1");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], screen, 0);
        QVERIFY(m_service->isWindowSnapped(windowId));
        QVERIFY(!m_service->isWindowFloating(windowId));

        m_service->recordFreeGeometry(windowId, screen, QRect(8, 894, 1588, 846), /*overwrite=*/true);

        const auto rec = m_service->placementStore().peek(windowId, appId);
        QVERIFY2(!rec || !rec->freeGeometryFor(screen).isValid(),
                 "snapped frame must NOT enter the shared free geometry");
    }

    void testRecordFreeGeometry_acceptsFloatingWithPreservedZone()
    {
        // A window floated AFTER snapping keeps its zone assignment (preserved for
        // restore), so isWindowSnapped stays true — but it is FLOATING, its frame is
        // a genuine free position, and it must be recorded. Guards "snapped AND not
        // floating", not "snapped".
        const QString windowId = QStringLiteral("firefox|dddd");
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);
        const QString screen = QStringLiteral("DP-1");

        m_service->assignWindowToZone(windowId, m_zoneIds[0], screen, 0);
        m_service->setWindowFloating(windowId, true);
        QVERIFY(m_service->isWindowFloating(windowId));

        m_service->recordFreeGeometry(windowId, screen, QRect(100, 100, 800, 600), /*overwrite=*/true);

        const auto rec = m_service->placementStore().peek(windowId, appId);
        QVERIFY(rec);
        QCOMPARE(rec->freeGeometryFor(screen), QRect(100, 100, 800, 600));
    }

    void testRecordFreeGeometry_firstCaptureWins_whenNotOverwrite()
    {
        // overwrite=false is the production capture path: the FIRST captured free
        // frame wins; a later non-overwrite write is ignored; overwrite=true replaces.
        const QString windowId = QStringLiteral("firefox|eeee");
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);
        const QString screen = QStringLiteral("DP-1");

        m_service->recordFreeGeometry(windowId, screen, QRect(10, 10, 400, 300), /*overwrite=*/false);
        m_service->recordFreeGeometry(windowId, screen, QRect(20, 20, 800, 600), /*overwrite=*/false);
        const auto rec = m_service->placementStore().peek(windowId, appId);
        QVERIFY(rec);
        QCOMPARE(rec->freeGeometryFor(screen), QRect(10, 10, 400, 300)); // first wins

        m_service->recordFreeGeometry(windowId, screen, QRect(30, 30, 500, 400), /*overwrite=*/true);
        const auto rec2 = m_service->placementStore().peek(windowId, appId);
        QVERIFY(rec2);
        QCOMPARE(rec2->freeGeometryFor(screen), QRect(30, 30, 500, 400)); // overwrite replaces
    }

    void testRecordedSnapZones_appIdFallbackAfterRelogin()
    {
        // After a relogin the window's uuid changes; the durable record stored under
        // the OLD uuid must still resolve for a NEW same-app window via the appId
        // bucket (the exact-uuid branch misses, the appId fallback hits).
        const QString oldId = QStringLiteral("firefox|old-uuid");
        PhosphorEngine::WindowPlacement p;
        p.windowId = oldId;
        p.appId = PhosphorIdentity::WindowId::extractAppId(oldId);
        PhosphorEngine::EngineSlot snap;
        snap.state = PhosphorEngine::WindowPlacement::stateSnapped();
        snap.zoneIds = QStringList{m_zoneIds[1]};
        p.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), snap);
        m_service->placementStore().record(p);

        const QString newId = QStringLiteral("firefox|new-uuid");
        QCOMPARE(m_service->recordedSnapZones(newId), QStringList{m_zoneIds[1]});
    }

    void testResnapFromAutotileOrder_sameAppInstancesEachKeepOwnZone()
    {
        // Two instances of the same app, one LIVE-assigned and one DURABLE-only (cold
        // cache, e.g. post-restart): each must resolve to its OWN recorded zone via the
        // exact-uuid path — never cross-routed through the shared appId bucket.
        const QString instA = QStringLiteral("firefox|aaaa");
        const QString instB = QStringLiteral("firefox|bbbb");

        m_service->assignWindowToZone(instA, m_zoneIds[0], QString(), 0);

        PhosphorEngine::WindowPlacement p;
        p.windowId = instB;
        p.appId = PhosphorIdentity::WindowId::extractAppId(instB);
        PhosphorEngine::EngineSlot snap;
        snap.state = PhosphorEngine::WindowPlacement::stateSnapped();
        snap.zoneIds = QStringList{m_zoneIds[2]};
        p.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), snap);
        m_service->placementStore().record(p);

        QCOMPARE(m_service->recordedSnapZones(instA), QStringList{m_zoneIds[0]});
        QCOMPARE(m_service->recordedSnapZones(instB), QStringList{m_zoneIds[2]});

        QVector<ZoneAssignmentEntry> entries =
            m_engine->calculateResnapEntriesFromAutotileOrder(QStringList{instA, instB}, QString());
        ZoneAssignmentEntry a;
        ZoneAssignmentEntry b;
        for (const ZoneAssignmentEntry& e : entries) {
            if (e.windowId == instA)
                a = e;
            if (e.windowId == instB)
                b = e;
        }
        QCOMPARE(a.targetZoneId, m_zoneIds[0]);
        QCOMPARE(b.targetZoneId, m_zoneIds[2]);
    }

    void testRecordedSnapZones_fallsBackToDurableRecordWhenLiveCold()
    {
        const QString windowId = QStringLiteral("firefox|aaaa");
        const QString appId = PhosphorIdentity::WindowId::extractAppId(windowId);

        // No live assignment and no record → empty.
        QVERIFY(m_service->recordedSnapZones(windowId).isEmpty());

        // Seed a DURABLE record with a snapped slot but NO live assignment — this is
        // the post-daemon-restart state, where the live zone cache is cold but the
        // persisted record survives.
        PhosphorEngine::WindowPlacement p;
        p.windowId = windowId;
        p.appId = appId;
        PhosphorEngine::EngineSlot snap;
        snap.state = PhosphorEngine::WindowPlacement::stateSnapped();
        snap.zoneIds = QStringList{m_zoneIds[2]};
        p.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), snap);
        m_service->placementStore().record(p);

        QCOMPARE(m_service->recordedSnapZones(windowId), QStringList{m_zoneIds[2]});

        // A live assignment takes precedence over the durable record.
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QString(), 0);
        QCOMPARE(m_service->recordedSnapZones(windowId), QStringList{m_zoneIds[0]});
    }

    void testRecordedSnapZones_ignoresNonSnappedDurableSlot()
    {
        // A record whose snap slot is FLOATING (not snapped) must not be treated as
        // a recorded snap zone — recordedSnapZones is for resnap restoration only.
        const QString windowId = QStringLiteral("firefox|bbbb");
        PhosphorEngine::WindowPlacement p;
        p.windowId = windowId;
        p.appId = PhosphorIdentity::WindowId::extractAppId(windowId);
        PhosphorEngine::EngineSlot snap;
        snap.state = PhosphorEngine::WindowPlacement::stateFloating();
        snap.zoneIds = QStringList{m_zoneIds[1]};
        p.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), snap);
        m_service->placementStore().record(p);

        QVERIFY(m_service->recordedSnapZones(windowId).isEmpty());
    }

    void testResnapFromAutotileOrder_multipleWindowsRecordedSameZoneStack()
    {
        // Two windows both recorded the SAME zone (a user-built multi-window-per-zone
        // stack). Both must restore to it — neither is dropped or relocated.
        const QString winA = QStringLiteral("appA|aaaa");
        const QString winB = QStringLiteral("appB|bbbb");
        m_service->assignWindowToZone(winA, m_zoneIds[2], QString(), 0);
        m_service->assignWindowToZone(winB, m_zoneIds[2], QString(), 0);

        QVector<ZoneAssignmentEntry> entries =
            m_engine->calculateResnapEntriesFromAutotileOrder(QStringList{winA, winB}, QString());
        ZoneAssignmentEntry a;
        ZoneAssignmentEntry b;
        for (const ZoneAssignmentEntry& e : entries) {
            if (e.windowId == winA)
                a = e;
            if (e.windowId == winB)
                b = e;
        }
        QCOMPARE(a.targetZoneId, m_zoneIds[2]);
        QCOMPARE(b.targetZoneId, m_zoneIds[2]);
    }

    void testOnLayoutChanged_floatingWindowsExcludedFromResnap()
    {
        QString windowId = QStringLiteral("app|12345");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QString(), 0);
        m_service->setWindowFloating(windowId, true);

        PhosphorZones::Layout* newLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<ZoneAssignmentEntry> resnap = m_engine->calculateResnapFromPreviousLayout();
        for (const ZoneAssignmentEntry& entry : resnap) {
            QVERIFY(entry.windowId != windowId);
        }
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsLifecycle)
#include "test_wts_lifecycle.moc"
