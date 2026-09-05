// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The by-id verbs the workspace overview drives: reorder and transfer by
// desktop id keep the generation and the trailing-empty invariant the way
// the current-workspace pair does, a reserved insert survives the destroy
// debounce until its window arrives, and a dynamic rename rides the name
// ledger.

#include "workspacereconcilerharness.h"

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorWorkspaces;
using WorkspaceTest::adoptTwoScreens;
using WorkspaceTest::id;

namespace {
const QString kA = QStringLiteral("A");
const QString kB = QStringLiteral("B");
}

class TestWorkspaceReconcilerOverview : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reorderByIdKeepsGenerationAndTrailingEmpty()
    {
        WorkspaceReconciler rec;
        adoptTwoScreens(rec);
        // A: [1 (occupied), 3 (trailing empty)].
        const quint64 before = rec.generation();
        QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
        // Moving the trailing empty to the front makes desktop 1 the last
        // entry, which is occupied: maintenance requests a new trailing empty.
        QVERIFY(rec.reorderWorkspace(id(3), 0));
        QCOMPARE(rec.map().sliceIndexOf(id(3)), 0);
        QCOMPARE(rec.map().sliceIndexOf(id(1)), 1);
        QVERIFY(rec.generation() > before);
        QCOMPARE(createSpy.count(), 1);
        // Refusals: same index, out of range, unknown id.
        QVERIFY(!rec.reorderWorkspace(id(3), 0));
        QVERIFY(!rec.reorderWorkspace(id(1), 5));
        QVERIFY(!rec.reorderWorkspace(id(99), 0));
    }

    void transferByIdRefusesToEmptyASliceAndClampsToTheTrailingEmpty()
    {
        WorkspaceReconciler rec;
        adoptTwoScreens(rec);
        // A: [1, 3], B: [2, 4] (3 and 4 trailing empties).
        QVERIFY(rec.transferWorkspace(id(1), kB, 5).isEmpty() == false);
        // Landed BEFORE B's trailing empty despite the oversized index.
        QCOMPARE(rec.map().ownerOf(id(1)), kB);
        QCOMPARE(rec.map().sliceIndexOf(id(1)), 1);
        QCOMPARE(rec.map().slice(kB).last().desktopId, id(4));
        // A is down to its trailing empty: it never gives up its last desktop.
        QCOMPARE(rec.map().sliceSize(kA), 1);
        QVERIFY(rec.transferWorkspace(id(3), kB, 0).isEmpty());
        // Unknown target, same screen, unowned id.
        QVERIFY(rec.transferWorkspace(id(2), QStringLiteral("Z"), 0).isEmpty());
        QVERIFY(rec.transferWorkspace(id(2), kB, 0).isEmpty());
        QVERIFY(rec.transferWorkspace(id(99), kA, 0).isEmpty());
    }

    void transferByIdSnapsTheSourceBackWhenItShowedTheMovedOne()
    {
        WorkspaceReconciler rec;
        adoptTwoScreens(rec);
        QSignalSpy setCurrent(&rec, &WorkspaceReconciler::requestSetCurrent);
        // A shows desktop 1 (adopted as current); moving it away must land A
        // on one of its own desktops.
        QCOMPARE(rec.transferWorkspace(id(1), kB, 0), id(1));
        QVERIFY(setCurrent.count() >= 1);
        QCOMPARE(setCurrent.last().at(0).toString(), kA);
        QCOMPARE(setCurrent.last().at(1).toString(), id(3));
    }

    void reservedInsertSurvivesTheDebounceUntilItsWindowArrives()
    {
        WorkspaceReconciler rec;
        adoptTwoScreens(rec);
        QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
        QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);
        // Insert at A's slot 1: between the occupied 1 and the trailing 3.
        QVERIFY(rec.requestInsertWorkspace(kA, 1));
        QCOMPARE(createSpy.count(), 1);
        rec.onKwinDesktopCreated(id(5));
        rec.onDesktopListSettled({id(1), id(5), id(3), id(2), id(4)});
        QCOMPARE(rec.map().ownerOf(id(5)), kA);
        QCOMPARE(rec.map().sliceIndexOf(id(5)), 1);
        QVERIFY(rec.isReserved(id(5)));
        // An empty dynamic desktop mid-slice is surplus, and the sweep armed
        // its debounce on settle. Past the debounce, no removal: reserved.
        QTest::qWait(WorkspaceReconciler::DestroyDebounceMs + 100);
        QCOMPARE(removeSpy.count(), 0);
        // The window lands: the reservation lifts, and the desktop is an
        // ordinary occupied one from here on.
        rec.onPopulationChanged(id(5), 1);
        QVERIFY(!rec.isReserved(id(5)));
        QTest::qWait(WorkspaceReconciler::DestroyDebounceMs + 100);
        QCOMPARE(removeSpy.count(), 0);
    }

    void releasedReservationIsSweptAsSurplus()
    {
        WorkspaceReconciler rec;
        adoptTwoScreens(rec);
        QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);
        QVERIFY(rec.requestInsertWorkspace(kA, 1));
        rec.onKwinDesktopCreated(id(5));
        rec.onDesktopListSettled({id(1), id(5), id(3), id(2), id(4)});
        QVERIFY(rec.isReserved(id(5)));
        // The move never delivered: the controller releases, and the empty
        // mid-slice desktop is destroyed like any other surplus one.
        rec.releaseReservation(id(5));
        QVERIFY(!rec.isReserved(id(5)));
        QTRY_COMPARE_WITH_TIMEOUT(removeSpy.count(), 1, WorkspaceReconciler::DestroyDebounceMs + 500);
        QCOMPARE(removeSpy.at(0).at(0).toString(), id(5));
    }

    void insertPastTheEndIsClampedAndUnknownScreenRefused()
    {
        WorkspaceReconciler rec;
        adoptTwoScreens(rec);
        QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
        QVERIFY(!rec.requestInsertWorkspace(QStringLiteral("Z"), 0));
        QVERIFY(rec.requestInsertWorkspace(kA, 99));
        QCOMPARE(createSpy.count(), 1);
        // The global position is A's slice end (after 3): the position KWin
        // is asked for is the second desktop of the global list.
        QCOMPARE(createSpy.at(0).at(0).toUInt(), 2u);
    }

    void renameRidesTheNameLedger()
    {
        WorkspaceReconciler rec;
        adoptTwoScreens(rec);
        QSignalSpy nameSpy(&rec, &WorkspaceReconciler::requestSetDesktopName);
        QVERIFY(rec.requestRename(id(1), QStringLiteral("mail")));
        QCOMPARE(nameSpy.count(), 1);
        QCOMPARE(nameSpy.at(0).at(0).toString(), id(1));
        QCOMPARE(nameSpy.at(0).at(1).toString(), QStringLiteral("mail"));
        // The same name in flight is not pushed twice.
        QVERIFY(rec.requestRename(id(1), QStringLiteral("mail")));
        QCOMPARE(nameSpy.count(), 1);
        QVERIFY(!rec.requestRename(id(99), QStringLiteral("x")));
    }

    void trailingEmptyOfIsPublic()
    {
        WorkspaceReconciler rec;
        adoptTwoScreens(rec);
        QCOMPARE(rec.trailingEmptyOf(kA), id(3));
        QCOMPARE(rec.trailingEmptyOf(kB), id(4));
        QVERIFY(rec.trailingEmptyOf(QStringLiteral("Z")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestWorkspaceReconcilerOverview)
#include "test_workspace_reconciler_overview.moc"
