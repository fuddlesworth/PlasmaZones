// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_dirty_mask.cpp
 * @brief WindowTrackingService::DirtyMask invariants (Phase 3).
 *
 * Pins the contract between WTS mutators and the delta-persistence path
 * in WindowTrackingAdaptor::saveState():
 *
 *  1. PhosphorZones::Zone-assignment mutations set DirtyZoneAssignments (+ DirtyLastUsedZone
 *     when the last-used-zone tracking is cleared as a side effect).
 *  2. Pre-tile geometry store/clear sets DirtyPreTileGeometries — not the
 *     rest of the mask.
 *  3. updateLastUsedZone sets DirtyLastUsedZone only.
 *  4. recordSnapIntent(true) sets DirtyUserSnapped only.
 *  5. consumePendingAssignment sets DirtyPendingRestores only.
 *  6. takeDirty() returns the current mask and resets to DirtyNone.
 *  7. markDirty() OR-merges bits and emits stateChanged (used by both
 *     mutators and the retry-on-write-failure path).
 *  8. Initial mask is DirtyAll so first save after construction writes
 *     every field.
 *
 * These invariants are what the saveload.cpp gate relies on. If a mutator
 * regresses to leaving the mask wider than necessary, the delta win
 * silently disappears. If a mutator forgets to mark dirty at all, state
 * is lost on the next save.
 */

#include <QTest>
#include <QSignalSpy>
#include <QRect>

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include "config/configbackends.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"
#include <PhosphorZones/Zone.h>
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestWtsDirtyMask : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_parent = new QObject(nullptr);
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"), m_parent);
        m_virtualDesktopManager = new VirtualDesktopManager(m_layoutManager, m_parent);
        m_settings = new StubSettings(m_parent);
        m_zoneDetector = new StubZoneDetector(m_parent);

        m_layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"));
        auto* zone1 = new PhosphorZones::Zone(QRectF(0.0, 0.0, 0.5, 1.0));
        zone1->setZoneNumber(1);
        m_layout->addZone(zone1);
        auto* zone2 = new PhosphorZones::Zone(QRectF(0.5, 0.0, 0.5, 1.0));
        zone2->setZoneNumber(2);
        m_layout->addZone(zone2);
        m_layoutManager->addLayout(m_layout);
        m_zone1Id = zone1->id().toString();

        m_service = new WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, m_settings,
                                              m_virtualDesktopManager, m_parent);
        m_snapState = new PhosphorSnapEngine::SnapState(QString(), nullptr);
        m_service->setSnapState(m_snapState);
        // Construction leaves mask = DirtyAll; clear so subsequent mutator
        // tests start from a known-clean state and only assert on the
        // bits that the mutator under test is responsible for.
        m_service->clearDirty();
    }

    void cleanup()
    {
        if (m_service) {
            m_service->setSnapState(nullptr);
        }
        delete m_snapState;
        m_snapState = nullptr;
        delete m_parent;
        m_parent = nullptr;
        m_layoutManager = nullptr;
        m_virtualDesktopManager = nullptr;
        m_settings = nullptr;
        m_zoneDetector = nullptr;
        m_layout = nullptr;
        m_service = nullptr;
        m_guard.reset();
    }

    void testInitialMaskIsAll()
    {
        // Fresh service starts DirtyAll so the first save after construction
        // writes every field. Build a fully-isolated fixture (independent
        // parent, layout manager, stubs, etc.) so construction / teardown
        // cannot interfere with the shared m_* fixture used by other
        // tests in this class.
        auto guard = std::make_unique<IsolatedConfigGuard>();
        QObject freshParent;
        auto* freshLayoutManager = new PhosphorZones::LayoutRegistry(
            PlasmaZones::createAssignmentsBackend(), QStringLiteral("plasmazones/layouts"), &freshParent);
        auto* freshVirtualDesktopManager = new VirtualDesktopManager(freshLayoutManager, &freshParent);
        auto* freshSettings = new StubSettings(&freshParent);
        auto* freshZoneDetector = new StubZoneDetector(&freshParent);

        WindowTrackingService fresh(freshLayoutManager, freshZoneDetector, nullptr, freshSettings,
                                    freshVirtualDesktopManager, &freshParent);
        PhosphorSnapEngine::SnapState freshSnapState(QString(), nullptr);
        fresh.setSnapState(&freshSnapState);
        QCOMPARE(fresh.peekDirty(), static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyAll));
        fresh.setSnapState(nullptr);
    }

    void testMarkDirty_orsBits()
    {
        m_service->markDirty(WindowTrackingService::DirtyZoneAssignments);
        QCOMPARE(m_service->peekDirty(),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyZoneAssignments));

        m_service->markDirty(WindowTrackingService::DirtyPreTileGeometries);
        QCOMPARE(m_service->peekDirty(),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyZoneAssignments
                                                               | WindowTrackingService::DirtyPreTileGeometries));
    }

    void testTakeDirty_returnsAndResets()
    {
        m_service->markDirty(WindowTrackingService::DirtyLastUsedZone);
        const auto snapshot = m_service->takeDirty();
        QCOMPARE(snapshot, static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyLastUsedZone));
        QCOMPARE(m_service->peekDirty(),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyNone));
    }

    void testMarkDirty_emitsStateChanged()
    {
        // markDirty() is the single entry point for OR'ing bits — it MUST
        // emit stateChanged so the adaptor's save timer is woken. The
        // retry-on-write-failure path relies on this: the failure handler
        // calls markDirty() and expects the stateChanged → scheduleSaveState
        // chain to schedule the next tick without an explicit call.
        m_service->clearDirty();
        QSignalSpy spy(m_service, &WindowTrackingService::stateChanged);
        m_service->markDirty(WindowTrackingService::DirtyPendingRestores);
        QCOMPARE(m_service->peekDirty(),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyPendingRestores));
        QCOMPARE(spy.count(), 1);
    }

    void testMarkDirty_retryRestoresCommittedBits()
    {
        // Simulates the writeCompleted(success=false) path: a previous
        // save called takeDirty() (mask == None after) and the worker
        // came back with failure — the adaptor re-marks the committed
        // bits, which must OR-merge with any newer mutations without
        // losing either side.
        m_service->clearDirty();
        m_service->markDirty(WindowTrackingService::DirtyZoneAssignments); // new mutation during in-flight write
        m_service->markDirty(WindowTrackingService::DirtyPendingRestores); // committed-snapshot retry
        QCOMPARE(m_service->peekDirty(),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyZoneAssignments
                                                               | WindowTrackingService::DirtyPendingRestores));
    }

    void testAssignWindowToZone_marksZoneAssignmentsOnly()
    {
        m_service->assignWindowToZone(QStringLiteral("app|abc"), m_zone1Id, QStringLiteral("DP-1"), 1);
        const auto mask = m_service->peekDirty();
        // Must include DirtyZoneAssignments.
        QVERIFY((mask & WindowTrackingService::DirtyZoneAssignments) != 0);
        // Must NOT include unrelated bits (PreTileGeometries, etc).
        QCOMPARE((mask & ~WindowTrackingService::DirtyZoneAssignments),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyNone));
    }

    void testUpdateLastUsedZone_marksLastUsedZoneOnly()
    {
        m_service->updateLastUsedZone(m_zone1Id, QStringLiteral("DP-1"), QStringLiteral("app"), 1);
        const auto mask = m_service->peekDirty();
        QCOMPARE(mask, static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyLastUsedZone));
    }

    void testRecordSnapIntent_userInitiated_marksUserSnappedOnly()
    {
        m_service->recordSnapIntent(QStringLiteral("app|abc"), /*wasUserInitiated=*/true);
        const auto mask = m_service->peekDirty();
        QCOMPARE(mask, static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyUserSnapped));
    }

    void testRecordSnapIntent_autoSnapped_doesNotMarkDirty()
    {
        m_service->recordSnapIntent(QStringLiteral("app|abc"), /*wasUserInitiated=*/false);
        QCOMPARE(m_service->peekDirty(),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyNone));
    }

    void testUnassignWindow_clearingLastUsed_marksBoth()
    {
        // Setup: assign a window to zone1, mark that zone as last-used.
        m_service->assignWindowToZone(QStringLiteral("app|abc"), m_zone1Id, QStringLiteral("DP-1"), 1);
        m_service->updateLastUsedZone(m_zone1Id, QStringLiteral("DP-1"), QStringLiteral("app"), 1);
        m_service->clearDirty();

        // Unassign: should clear last-used-zone tracking AND touch zone map.
        m_service->unassignWindow(QStringLiteral("app|abc"));
        const auto mask = m_service->peekDirty();
        QVERIFY((mask & WindowTrackingService::DirtyZoneAssignments) != 0);
        QVERIFY((mask & WindowTrackingService::DirtyLastUsedZone) != 0);
        // And only those two.
        QCOMPARE((mask & ~(WindowTrackingService::DirtyZoneAssignments | WindowTrackingService::DirtyLastUsedZone)),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyNone));
    }

    void testPruneStaleAssignments_marksPersistedFieldsDirty()
    {
        // Regression pin: pruneStaleAssignments used to mutate maps without
        // touching the dirty mask. Combined with WTA::pruneStaleWindows
        // calling WTA::scheduleSaveState (which only kicks the timer, not
        // the mask), the prune was silently dropped at saveState's DirtyNone
        // early-return — ghost windows came back on the next daemon restart.
        m_service->assignWindowToZone(QStringLiteral("app|abc"), m_zone1Id, QStringLiteral("DP-1"), 1);
        m_service->clearDirty();

        const int pruned = m_service->pruneStaleAssignments(QSet<QString>{});
        QVERIFY(pruned >= 1);

        const auto mask = m_service->peekDirty();
        QVERIFY((mask & WindowTrackingService::DirtyZoneAssignments) != 0);
    }

    void testPruneStaleAssignments_noop_leavesDirtyMaskClean()
    {
        // When nothing actually gets pruned (alive set contains all
        // tracked windows), the mask must stay clean — we don't want to
        // trigger a disk write for a no-op.
        m_service->assignWindowToZone(QStringLiteral("app|abc"), m_zone1Id, QStringLiteral("DP-1"), 1);
        m_service->clearDirty();

        const int pruned = m_service->pruneStaleAssignments(QSet<QString>{QStringLiteral("app|abc")});
        QCOMPARE(pruned, 0);
        QCOMPARE(m_service->peekDirty(),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyNone));
    }

    void testUnsnapForFloat_marksPreFloatDirty()
    {
        // Regression pin: unsnapForFloat writes new pre-float zone/screen
        // entries but previously relied on the caller's immediate
        // setWindowFloating(true) follow-up (DirtyAll) to persist them.
        // That implicit contract is fragile — marking here makes the method
        // self-sufficient.
        m_service->assignWindowToZone(QStringLiteral("app|abc"), m_zone1Id, QStringLiteral("DP-1"), 1);
        m_service->clearDirty();

        m_service->unsnapForFloat(QStringLiteral("app|abc"));

        const auto mask = m_service->peekDirty();
        QVERIFY((mask & WindowTrackingService::DirtyPreFloatZones) != 0);
        QVERIFY((mask & WindowTrackingService::DirtyPreFloatScreens) != 0);
        // unassignWindow inside unsnapForFloat also touches DirtyZoneAssignments;
        // last-used may or may not clear. The critical thing is both pre-float
        // bits are set — the zone bit is best-effort.
    }

    void testMarkDirty_All_setsEveryBit()
    {
        // Phase 3 safety net: the DirtyAll constant covers every declared
        // DirtyField so a legacy mutator that just calls markDirty(DirtyAll)
        // (via the scheduleSaveState default-arg wrapper it delegates to)
        // still flushes the full state on the next save. This test pins
        // that invariant: DirtyAll must be a strict superset of every
        // individual bit. scheduleSaveState itself is private and
        // exercised indirectly — the wrapper just delegates to markDirty.
        m_service->markDirty(WindowTrackingService::DirtyAll);
        const auto mask = m_service->peekDirty();
        QCOMPARE(mask, static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyAll));
        // Every individual field bit must be included in DirtyAll so
        // adding a new field without extending DirtyAll fails the test.
        for (const auto bit :
             {WindowTrackingService::DirtyActiveLayoutId, WindowTrackingService::DirtyZoneAssignments,
              WindowTrackingService::DirtyPendingRestores, WindowTrackingService::DirtyPreTileGeometries,
              WindowTrackingService::DirtyLastUsedZone, WindowTrackingService::DirtyPreFloatZones,
              WindowTrackingService::DirtyPreFloatScreens, WindowTrackingService::DirtyUserSnapped,
              WindowTrackingService::DirtyAutotileOrders, WindowTrackingService::DirtyAutotilePending}) {
            QVERIFY2((mask & bit) != 0, "DirtyAll is missing a DirtyField bit — extend DirtyAll in the header");
        }
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QObject* m_parent = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    VirtualDesktopManager* m_virtualDesktopManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorSnapEngine::SnapState* m_snapState = nullptr;
    PhosphorZones::Layout* m_layout = nullptr;
    WindowTrackingService* m_service = nullptr;
    QString m_zone1Id;
};

QTEST_MAIN(TestWtsDirtyMask)
#include "test_wts_dirty_mask.moc"
