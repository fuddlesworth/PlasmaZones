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
#include <QRect>
#include <QScopeGuard>
#include <QSignalSpy>
#include <memory>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include "FakeScreenProvider.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

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
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
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
        // Detach BOTH borrowed pointers before the engine dies so the service
        // never holds a dangling SnapEngine* (same discipline as
        // wta_convenience_fixture.h).
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
        // Guard both derefs: a bare .first() on an empty queue/list is UB, not
        // a test failure.
        const auto queue = m_service->pendingRestoreQueues().value(appId);
        QVERIFY(!queue.isEmpty());
        QVERIFY(!queue.first().zoneIds.isEmpty());
        QCOMPARE(queue.first().zoneIds.first(), m_zoneIds[0]);
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

        // Float state and pre-float zones should be fully cleared on close —
        // BOTH keys: the windowId-keyed runtime entry and the appId alias.
        QVERIFY(!m_service->isWindowFloating(windowId));
        QVERIFY(!m_service->isWindowFloating(appId));
        QVERIFY(m_service->preFloatZone(windowId).isEmpty());
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
        // RAII, not a trailing clear: every QVERIFY/QCOMPARE below RETURNS
        // from the slot on failure, so a clear written at the end is skipped
        // exactly when it matters — leaving the fixture-owned service holding a
        // callback that captures this slot's locals by reference, to be invoked
        // or destroyed after they are gone.
        const auto clearPredicate = qScopeGuard([this] {
            m_service->setShouldTrackPredicate({});
        });
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
        // RAII, not a trailing clear: every QVERIFY/QCOMPARE below RETURNS
        // from the slot on failure, so a clear written at the end is skipped
        // exactly when it matters — leaving the fixture-owned service holding a
        // callback that captures this slot's locals by reference, to be invoked
        // or destroyed after they are gone.
        const auto clearPredicate = qScopeGuard([this] {
            m_service->setShouldTrackPredicate({});
        });
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
        // The clear here is the SUBJECT of this test, not teardown: it is the
        // second half of the round-trip described above, so it must stay an
        // explicit call. (The other predicate slots use a scope guard, because
        // there the clear is genuinely cleanup that a failing assertion would
        // otherwise skip.)
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

    void testOnLayoutChanged_nonSnappingScreenKeepsAssignments()
    {
        // onLayoutChanged prunes assignments whose zones no longer exist in the
        // screen's effective layout. A screen owned by a NON-snapping engine
        // must be skipped entirely: neither autotile nor scrolling has a layout
        // entity of its own, so resolveLayoutForScreen would answer some
        // unrelated cascade layout and prune every assignment the screen is
        // holding for its eventual return to snapping.
        //
        // Both arms matter and only the control was covered before: every
        // existing onLayoutChanged test uses a snapping screen, so deleting
        // either half of the `isAutotile(id) || isScrolling(id)` predicate
        // failed nothing.
        const QString autotileScreen = QStringLiteral("DP-1");
        const QString scrollingScreen = QStringLiteral("HDMI-1");
        const QString snappingScreen = QStringLiteral("DP-2");
        const QString autotileWindow = QStringLiteral("app|aaaa");
        const QString scrollingWindow = QStringLiteral("app|bbbb");
        const QString snappingWindow = QStringLiteral("app|cccc");
        const int desktop = m_layoutManager->currentVirtualDesktop();

        m_service->assignWindowToZone(autotileWindow, m_zoneIds[0], autotileScreen, 0);
        m_service->assignWindowToZone(scrollingWindow, m_zoneIds[0], scrollingScreen, 0);
        m_service->assignWindowToZone(snappingWindow, m_zoneIds[0], snappingScreen, 0);
        QVERIFY(m_service->isWindowSnapped(autotileWindow));
        QVERIFY(m_service->isWindowSnapped(scrollingWindow));
        QVERIFY(m_service->isWindowSnapped(snappingWindow));

        // A layout whose zones do NOT include m_zoneIds[0], so every screen
        // resolving it has a genuinely stale assignment to prune.
        PhosphorZones::Layout* newLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        // EVERY screen must resolve newLayout, including the two about to become
        // non-snapping. Without this the cascade answered the fixture's original
        // 3-zone layout for them, m_zoneIds[0] still existed there, and the
        // assignment survived whether the non-snapping skip ran or not — the
        // test looked green while pinning nothing. These assignments must land
        // BEFORE the mode writes below, because assignLayout resets the mode.
        m_layoutManager->assignLayout(snappingScreen, desktop, QString(), newLayout);
        m_layoutManager->assignLayout(autotileScreen, desktop, QString(), newLayout);
        m_layoutManager->assignLayout(scrollingScreen, desktop, QString(), newLayout);

        // Now hand the first two screens to the non-snapping engines, keeping
        // newLayout in the snappingLayout slot — the lossless shape the daemon
        // writes on a mode flip.
        PhosphorZones::AssignmentEntry autotileEntry;
        autotileEntry.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotileEntry.tilingAlgorithm = QStringLiteral("bsp");
        autotileEntry.snappingLayout = newLayout->id().toString();
        m_layoutManager->setAssignmentEntryDirect(autotileScreen, desktop, QString(), autotileEntry);

        PhosphorZones::AssignmentEntry scrollingEntry;
        scrollingEntry.mode = PhosphorZones::AssignmentEntry::Scrolling;
        scrollingEntry.snappingLayout = newLayout->id().toString();
        m_layoutManager->setAssignmentEntryDirect(scrollingScreen, desktop, QString(), scrollingEntry);

        QCOMPARE(m_layoutManager->modeForScreen(autotileScreen, desktop), PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(m_layoutManager->modeForScreen(scrollingScreen, desktop), PhosphorZones::AssignmentEntry::Scrolling);
        // The sentinel ids are what the predicate under test inspects.
        QVERIFY(
            PhosphorLayout::LayoutId::isScrolling(m_layoutManager->assignmentIdForScreen(scrollingScreen, desktop)));

        // Retire the layout m_zoneIds came from. This is what makes the test
        // DISCRIMINATE rather than merely pass: for a non-snapping entry the
        // cascade ignores the snappingLayout slot and falls back to an unrelated
        // layout (exactly the hazard the production comment describes), and while
        // that fallback was the fixture's own 3-zone layout the zone still
        // existed, so the assignment survived with or without the skip. With it
        // gone, any screen that actually reaches resolveLayoutForScreen prunes.
        m_layoutManager->removeLayout(m_testLayout);
        m_testLayout = nullptr;

        m_service->onLayoutChanged();

        QVERIFY2(m_service->isWindowSnapped(autotileWindow), "an autotile screen's assignments must survive");
        QVERIFY2(m_service->isWindowSnapped(scrollingWindow), "a scrolling screen's assignments must survive");
        // Control: the snapping screen still prunes, so the skip above is a
        // genuine mode discrimination rather than onLayoutChanged doing nothing.
        QVERIFY2(!m_service->isWindowSnapped(snappingWindow), "a snapping screen's stale assignment must be pruned");
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

    void testValidatedUnmanagedGeometry_rejectsMisKeyedRecord()
    {
        // A record can be MIS-KEYED: a rect filed under one screen whose
        // coordinates describe another. Real sessions carry them (three in the
        // bundle from discussion #1028), written before recordFreeGeometry
        // refused the mismatch, so the read has to defend itself.
        //
        // The generic sanity check cannot catch this. isGeometryOnScreen asks
        // whether a rect is on ANY screen, and a mis-keyed rect is — the wrong
        // one — so it came back verbatim and moved the window there. That is
        // the cross-screen restore this resolver no longer performs, arriving
        // through the key instead of through a fallback.
        //
        // Needs REAL output geometry, so this builds its own service over a
        // FakeScreenProvider rather than using m_service (constructed with a
        // null ScreenManager, under which the guard fails open by design).
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
        provider.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();

        auto svc = std::make_unique<PhosphorPlacement::WindowTrackingService>(m_layoutManager, &manager, nullptr);
        // Same wiring the fixture gives m_service: setWindowFloating asserts on
        // hasSnapState().
        auto engine = std::make_unique<SnapEngine>(m_layoutManager, svc.get(), m_zoneDetector, nullptr, nullptr);
        engine->setEngineSettings(m_settings);
        svc->setSnapState(engine->snapState());
        svc->setSnapEngine(engine.get());

        const QString windowId = QStringLiteral("firefox|mis-keyed");

        svc->setWindowFloating(windowId, true);
        // Honest capture on DP-2 first, to prove the guard is not simply
        // refusing everything.
        svc->recordFreeGeometry(windowId, QStringLiteral("DP-2"), QRect(2000, 100, 800, 600), /*overwrite=*/true);
        QVERIFY2(svc->validatedUnmanagedGeometry(windowId, QStringLiteral("DP-2")).has_value(),
                 "a rect that does lie on its key screen must answer");

        // Now the mis-key: DP-1 coordinates filed under DP-2. The write point
        // refuses it, so the honest DP-2 rect survives untouched.
        svc->recordFreeGeometry(windowId, QStringLiteral("DP-2"), QRect(100, 100, 800, 600), /*overwrite=*/true);
        const auto still = svc->validatedUnmanagedGeometry(windowId, QStringLiteral("DP-2"));
        QVERIFY(still.has_value());
        QVERIFY2(still->x() >= 1920, "a rect that does not lie on DP-2 must not be filed under it");

        // The READ guard, which the block above does NOT reach: the writer
        // refuses the mis-key before the reader ever sees one, so driving this
        // through recordFreeGeometry can only ever test the write point. The
        // comment at the top of this test is about records that ALREADY EXIST
        // on disk from before that guard shipped, and the only way to stand one
        // up is to seed the store directly.
        const QString legacyId = QStringLiteral("firefox|legacy-mis-keyed");
        PhosphorEngine::WindowPlacement legacy;
        legacy.windowId = legacyId;
        legacy.appId = QStringLiteral("firefox");
        legacy.screenId = QStringLiteral("DP-2");
        // DP-1 coordinates under a DP-2 key — exactly what the old writer let
        // through.
        legacy.freeGeometryByScreen.insert(QStringLiteral("DP-2"), QRect(100, 100, 800, 600));
        QVERIFY(svc->placementStore().record(legacy));
        QVERIFY2(!svc->validatedUnmanagedGeometry(legacyId, QStringLiteral("DP-2")).has_value(),
                 "a record already on disk with a rect that does not lie on its key screen must be refused by the "
                 "READ, not merely by the write that no longer happens");
    }

    void testValidatedUnmanagedGeometry_isScreenLocal()
    {
        // "Float restore is screen-local by doctrine" — the phrase is
        // test_window_placement_store's, and the resolver used to contradict it
        // with a cross-screen fallback that re-centred another monitor's rect
        // onto the asking screen. That relocated a window the user had put
        // here, off a rect that only ever guessed: a window which has never
        // floated on this screen has no remembered spot here to return to.
        // Nothing is the honest answer, and it leaves the window alone.
        const QString windowId = QStringLiteral("firefox|screen-local");
        const QString screen = QStringLiteral("DP-1");

        m_service->setWindowFloating(windowId, true);
        m_service->recordFreeGeometry(windowId, screen, QRect(100, 100, 800, 600), /*overwrite=*/true);

        QVERIFY2(m_service->validatedUnmanagedGeometry(windowId, screen).has_value(),
                 "the screen the rect was captured on must answer");
        QVERIFY2(!m_service->validatedUnmanagedGeometry(windowId, QStringLiteral("DP-2")).has_value(),
                 "a rect remembered on DP-1 is not a float-back for DP-2");
    }

    void testValidatedUnmanagedGeometry_isPerWindow()
    {
        // No cross-instance share either. An app's bucket fills with dead
        // instances (MaxPerApp), so a live window with no record of its own
        // would otherwise be handed a ghost's remembered spot — discussion
        // #1028. Every caller of this resolver asks a per-window question.
        const QString recorded = QStringLiteral("konsole|has-a-record");
        const QString sibling = QStringLiteral("konsole|no-record");
        const QString screen = QStringLiteral("DP-1");

        m_service->setWindowFloating(recorded, true);
        m_service->recordFreeGeometry(recorded, screen, QRect(120, 140, 640, 480), /*overwrite=*/true);

        QVERIFY(m_service->validatedUnmanagedGeometry(recorded, screen).has_value());
        QVERIFY2(!m_service->validatedUnmanagedGeometry(sibling, screen).has_value(),
                 "a same-app sibling's float-back is not this window's");
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

    void testRecordFreeGeometry_firstCaptureWins_isPerWindow()
    {
        // First-capture-wins is a PER-WINDOW contract: a same-app sibling's
        // recorded free geometry must not suppress this window's own first
        // capture (it would never persist its own position while the sibling's
        // record lives).
        const QString screen = QStringLiteral("DP-1");
        m_service->recordFreeGeometry(QStringLiteral("firefox|sibling-geo"), screen, QRect(10, 10, 400, 300),
                                      /*overwrite=*/false);
        m_service->recordFreeGeometry(QStringLiteral("firefox|self-geo"), screen, QRect(50, 60, 700, 500),
                                      /*overwrite=*/false);
        const auto rec = m_service->placementStore().peekExact(QStringLiteral("firefox|self-geo"));
        QVERIFY2(rec.has_value(), "a sibling's record must not block this window's first capture");
        QCOMPARE(rec->freeGeometryFor(screen), QRect(50, 60, 700, 500));
    }

    void testRecordFreeGeometry_prefixMutationStillHonorsFirstCapture()
    {
        // An appId-prefix mutation must not defeat first-capture-wins: the
        // renamed window still owns its earlier record via the instance suffix,
        // so a non-overwrite capture keeps the FIRST geometry (and an explicit
        // overwrite still replaces it under the new id).
        const QString oldId = QStringLiteral("oldclass|renamed-instance");
        const QString newId = QStringLiteral("newclass|renamed-instance");
        const QString screen = QStringLiteral("DP-1");
        const QRect firstGeometry(10, 20, 500, 400);

        m_service->recordFreeGeometry(oldId, screen, firstGeometry, /*overwrite=*/false);
        m_service->recordFreeGeometry(newId, screen, QRect(90, 80, 900, 700), /*overwrite=*/false);

        auto rec = m_service->placementStore().peekExact(newId);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->freeGeometryFor(screen), firstGeometry);
        QCOMPARE(m_service->placementStore().size(), 1);

        const QRect replacement(30, 40, 600, 450);
        m_service->recordFreeGeometry(newId, screen, replacement, /*overwrite=*/true);
        rec = m_service->placementStore().peekExact(newId);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->windowId, newId);
        QCOMPARE(rec->appId, QStringLiteral("newclass"));
        QCOMPARE(rec->freeGeometryFor(screen), replacement);
    }

    void testRecordFloatingClose_neverInheritsSiblingEngineSlots()
    {
        // A record-less window closing floating must take recordFloatingClose's
        // synthesized-floating branch — grafting a same-app sibling's SNAPPED
        // slot under this windowId would make a reopen restore two windows into
        // the sibling's zone and corrupt the per-app FIFO distribution.
        PhosphorEngine::WindowPlacement sib;
        sib.windowId = QStringLiteral("firefox|sibling-close");
        sib.appId = QStringLiteral("firefox");
        PhosphorEngine::EngineSlot snap;
        snap.state = QString(PhosphorEngine::WindowPlacement::stateSnapped());
        snap.zoneIds = QStringList{m_zoneIds[0]};
        sib.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), snap);
        QVERIFY(m_service->placementStore().record(sib));

        m_service->recordFloatingClose(QStringLiteral("firefox|self-close"), QStringLiteral("DP-1"),
                                       QRect(30, 40, 600, 400));

        const auto rec = m_service->placementStore().peekExact(QStringLiteral("firefox|self-close"));
        QVERIFY(rec.has_value());
        // NOTE: the snap key here pins the UNWIRED-resolver fallback only —
        // owningModeEngineId defaults to snap without a daemon-wired
        // ModeEngineIdResolver. The wired behaviour (the slot keyed on the
        // close screen's owning engine) is pinned by the sibling test below.
        const PhosphorEngine::EngineSlot slot = rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        QCOMPARE(slot.state, QString(PhosphorEngine::WindowPlacement::stateFloating()));
        QVERIFY2(slot.zoneIds.isEmpty(), "the sibling's snapped slot must not be grafted under this windowId");
        // The sibling's own record is untouched.
        const auto sibRec = m_service->placementStore().peekExact(QStringLiteral("firefox|sibling-close"));
        QVERIFY(sibRec.has_value());
        QCOMPARE(sibRec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateSnapped()));
    }

    void testLiveInstanceProbe_registryWiredReopenSkipsOpenSiblingsRecord()
    {
        // The PRODUCTION probe (wired in the WTS constructor, registry-backed
        // via extractInstanceId): a reopen's appId fallback must consume the
        // non-live record and never one whose instance the registry still
        // holds — the exact no-steal exclusion the store's own unit tests
        // exercise with a hand-rolled probe.
        PhosphorEngine::WindowRegistry registry;
        // RAII: a failing assertion below would otherwise leave the
        // fixture-owned service pointing at this slot's stack registry.
        const auto clearRegistry = qScopeGuard([this] {
            m_service->setWindowRegistry(nullptr);
        });
        m_service->setWindowRegistry(&registry);
        registry.upsert(QStringLiteral("live-uuid"), PhosphorEngine::WindowMetadata{});

        PhosphorEngine::WindowPlacement liveRec;
        liveRec.windowId = QStringLiteral("term|live-uuid");
        liveRec.appId = QStringLiteral("term");
        liveRec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot liveSlot;
        liveSlot.state = QString(PhosphorEngine::WindowPlacement::stateFloating());
        liveRec.engines.insert(PhosphorEngine::WindowPlacement::scrollingEngineId(), liveSlot);
        liveRec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(50, 50, 400, 300));
        QVERIFY(m_service->placementStore().record(liveRec));

        PhosphorEngine::WindowPlacement deadRec = liveRec;
        deadRec.windowId = QStringLiteral("term|dead-uuid");
        deadRec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(10, 10, 400, 300));
        QVERIFY(m_service->placementStore().record(deadRec));

        // deadRec is NEWER by sequence, but even if it were older the live
        // record must be skipped; assert the consumed record is the dead one.
        const auto consumed = m_service->placementStore().takeForReopen(
            PhosphorEngine::WindowPlacement::scrollingEngineId(), QStringLiteral("term|new-uuid"),
            QStringLiteral("term"), QStringLiteral("DP-1"));
        QVERIFY(consumed.has_value());
        QCOMPARE(consumed->freeGeometryFor(QStringLiteral("DP-1")), QRect(10, 10, 400, 300));
        // The live window's record is untouched.
        QVERIFY(m_service->placementStore().peekExact(QStringLiteral("term|live-uuid")).has_value());
    }

    void testRecordFloatingClose_synthSlotKeyedOnOwningModeEngine()
    {
        // The wired-resolver behaviour the daemon relies on: the synthesized
        // float slot must land under the CLOSE SCREEN's owning engine, and a
        // record carrying only FOREIGN slots must still gain it — the tiling
        // reopen accepts read strictly their own slot, so a snap-keyed (or
        // absent) verdict re-tiles a window that closed floating. Deleting
        // either half of the fix fails this test.
        // RAII, same reason as the predicate guards above.
        const auto clearResolver = qScopeGuard([this] {
            m_service->setModeEngineIdResolver({});
        });
        m_service->setModeEngineIdResolver([](const QString&, const QString& screenId) -> QString {
            return screenId == QStringLiteral("DP-1") ? QString(PhosphorEngine::WindowPlacement::scrollingEngineId())
                                                      : QString(PhosphorEngine::WindowPlacement::snapEngineId());
        });

        // Case 1: record-less close — slot keyed on scrolling, not snap.
        m_service->recordFloatingClose(QStringLiteral("octopi|scroll-close"), QStringLiteral("DP-1"),
                                       QRect(30, 40, 600, 400));
        auto rec = m_service->placementStore().peekExact(QStringLiteral("octopi|scroll-close"));
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::scrollingEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateFloating()));
        QVERIFY(!rec->engines.contains(QString(PhosphorEngine::WindowPlacement::snapEngineId())));

        // Case 2: an existing record with only a FOREIGN slot still gains the
        // owning engine's float verdict (the old engines.isEmpty() gate
        // skipped exactly this shape), and the foreign slot survives.
        PhosphorEngine::WindowPlacement foreign;
        foreign.windowId = QStringLiteral("octopi|foreign-slot");
        foreign.appId = QStringLiteral("octopi");
        foreign.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot autotileSlot;
        autotileSlot.state = QString(PhosphorEngine::WindowPlacement::stateFloating());
        foreign.engines.insert(PhosphorEngine::WindowPlacement::autotileEngineId(), autotileSlot);
        foreign.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(1, 2, 300, 200));
        QVERIFY(m_service->placementStore().record(foreign));

        m_service->recordFloatingClose(QStringLiteral("octopi|foreign-slot"), QStringLiteral("DP-1"),
                                       QRect(30, 40, 600, 400));
        rec = m_service->placementStore().peekExact(QStringLiteral("octopi|foreign-slot"));
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::scrollingEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateFloating()));
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::autotileEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateFloating()));
    }

    void testRecordFloatingClose_prefixMutationKeepsOwnEngineSlots()
    {
        // Closing floating after an appId-prefix mutation must merge into the
        // window's OWN prior record (matched by instance suffix), preserving its
        // other engine slots rather than synthesizing a fresh floating-only record.
        const QString oldId = QStringLiteral("oldclass|closing-instance");
        const QString newId = QStringLiteral("newclass|closing-instance");
        PhosphorEngine::WindowPlacement existing;
        existing.windowId = oldId;
        existing.appId = QStringLiteral("oldclass");
        existing.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot snap;
        snap.state = QString(PhosphorEngine::WindowPlacement::stateSnapped());
        snap.zoneIds = QStringList{m_zoneIds[0]};
        existing.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), snap);
        QVERIFY(m_service->placementStore().record(existing));

        const QRect closeGeometry(70, 80, 700, 500);
        m_service->recordFloatingClose(newId, QStringLiteral("DP-1"), closeGeometry);

        QCOMPARE(m_service->placementStore().size(), 1);
        const auto rec = m_service->placementStore().peekExact(newId);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->windowId, newId);
        QCOMPARE(rec->appId, QStringLiteral("newclass"));
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateSnapped()));
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).zoneIds, QStringList{m_zoneIds[0]});
        QCOMPARE(rec->freeGeometryFor(QStringLiteral("DP-1")), closeGeometry);
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
        const QString floatedId = QStringLiteral("app|12345");
        // Non-floating CONTROL window: proves the resnap actually produced
        // entries, so the exclusion loop below cannot pass vacuously on an
        // empty list.
        const QString snappedId = QStringLiteral("app|control");
        m_service->assignWindowToZone(floatedId, m_zoneIds[0], QString(), 0);
        m_service->assignWindowToZone(snappedId, m_zoneIds[1], QString(), 0);
        m_service->setWindowFloating(floatedId, true);

        PhosphorZones::Layout* newLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(newLayout);
        m_layoutManager->setActiveLayout(newLayout);
        m_service->onLayoutChanged();

        QVector<ZoneAssignmentEntry> resnap = m_engine->calculateResnapFromPreviousLayout();
        QCOMPARE(resnap.size(), 1);
        QCOMPARE(resnap.first().windowId, snappedId);
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
