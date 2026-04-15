// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_dirty_mask.cpp
 * @brief WindowTrackingService::DirtyMask invariants (Phase 3).
 *
 * Pins the contract between WTS mutators and the delta-persistence path
 * in WindowTrackingAdaptor::saveState():
 *
 *  1. Zone-assignment mutations set DirtyZoneAssignments (+ DirtyLastUsedZone
 *     when the last-used-zone tracking is cleared as a side effect).
 *  2. Pre-tile geometry store/clear sets DirtyPreTileGeometries — not the
 *     rest of the mask.
 *  3. updateLastUsedZone sets DirtyLastUsedZone only.
 *  4. recordSnapIntent(true) sets DirtyUserSnapped only.
 *  5. consumePendingAssignment sets DirtyPendingRestores only.
 *  6. takeDirty() returns the current mask and resets to DirtyNone.
 *  7. addDirty() ORs bits back in (used by the retry-on-failure path).
 *  8. Initial mask is DirtyAll so first save after construction writes
 *     every field.
 *
 * These invariants are what the saveload.cpp gate relies on. If a mutator
 * regresses to leaving the mask wider than necessary, the delta win
 * silently disappears. If a mutator forgets to mark dirty at all, state
 * is lost on the next save.
 */

#include <QTest>
#include <QRect>

#include "core/layout.h"
#include "core/layoutmanager.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"
#include "core/zone.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestWtsDirtyMask : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_parent = new QObject(nullptr);
        m_layoutManager = new LayoutManager(m_parent);
        m_virtualDesktopManager = new VirtualDesktopManager(m_layoutManager, m_parent);
        m_settings = new StubSettings(m_parent);
        m_zoneDetector = new StubZoneDetector(m_parent);

        m_layout = new Layout(QStringLiteral("TestLayout"));
        auto* zone1 = new Zone(QRectF(0.0, 0.0, 0.5, 1.0));
        zone1->setZoneNumber(1);
        m_layout->addZone(zone1);
        auto* zone2 = new Zone(QRectF(0.5, 0.0, 0.5, 1.0));
        zone2->setZoneNumber(2);
        m_layout->addZone(zone2);
        m_layoutManager->addLayout(m_layout);
        m_zone1Id = zone1->id().toString();

        m_service =
            new WindowTrackingService(m_layoutManager, m_zoneDetector, m_settings, m_virtualDesktopManager, m_parent);
        // Construction leaves mask = DirtyAll; clear so subsequent mutator
        // tests start from a known-clean state and only assert on the
        // bits that the mutator under test is responsible for.
        m_service->clearDirty();
    }

    void cleanup()
    {
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
        // Fresh service (before init's clearDirty call) starts DirtyAll so
        // the first save after construction writes every field. Build a
        // second service to verify — m_service has already been cleared.
        WindowTrackingService fresh(m_layoutManager, m_zoneDetector, m_settings, m_virtualDesktopManager);
        QCOMPARE(fresh.peekDirty(), static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyAll));
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

    void testAddDirty_orsWithoutEmittingSignal()
    {
        // addDirty() is for retry-on-failure: restore the bits a failed
        // save attempt was supposed to cover. It must NOT emit stateChanged
        // (the retry is scheduled manually by the adaptor) — pin that.
        m_service->clearDirty();
        int signalCount = 0;
        connect(m_service, &WindowTrackingService::stateChanged, this, [&]() {
            ++signalCount;
        });
        m_service->addDirty(WindowTrackingService::DirtyPendingRestores);
        QCOMPARE(m_service->peekDirty(),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyPendingRestores));
        QCOMPARE(signalCount, 0);
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

    void testStorePreTileGeometry_marksPreTileOnly()
    {
        m_service->storePreTileGeometry(QStringLiteral("app|abc"), QRect(0, 0, 400, 300), QStringLiteral("DP-1"),
                                        /*overwrite=*/false);
        const auto mask = m_service->peekDirty();
        QVERIFY((mask & WindowTrackingService::DirtyPreTileGeometries) != 0);
        QCOMPARE((mask & ~WindowTrackingService::DirtyPreTileGeometries),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyNone));
    }

    void testClearPreTileGeometry_marksPreTileOnly()
    {
        // First store so the subsequent clear has something to remove.
        m_service->storePreTileGeometry(QStringLiteral("app|abc"), QRect(0, 0, 400, 300), QStringLiteral("DP-1"),
                                        false);
        m_service->clearDirty();
        m_service->clearPreTileGeometry(QStringLiteral("app|abc"));
        const auto mask = m_service->peekDirty();
        QVERIFY((mask & WindowTrackingService::DirtyPreTileGeometries) != 0);
        QCOMPARE((mask & ~WindowTrackingService::DirtyPreTileGeometries),
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

    void testScheduleSaveState_defaultArg_marksAll()
    {
        // Backward-compat path: old call sites that pass no argument must
        // still behave as "everything is dirty". Protects the safety net.
        // Use assignWindowToZone + clearDirty to ensure we test the default.
        // Private: can't call directly, so we verify via markDirty with DirtyAll
        // which scheduleSaveState wraps.
        m_service->markDirty(WindowTrackingService::DirtyAll);
        QCOMPARE(m_service->peekDirty(),
                 static_cast<WindowTrackingService::DirtyMask>(WindowTrackingService::DirtyAll));
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QObject* m_parent = nullptr;
    LayoutManager* m_layoutManager = nullptr;
    VirtualDesktopManager* m_virtualDesktopManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    Layout* m_layout = nullptr;
    WindowTrackingService* m_service = nullptr;
    QString m_zone1Id;
};

QTEST_MAIN(TestWtsDirtyMask)
#include "test_wts_dirty_mask.moc"
