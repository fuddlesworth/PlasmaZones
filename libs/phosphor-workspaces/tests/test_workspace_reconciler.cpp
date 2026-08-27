// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QSignalSpy>
#include <QTest>

using PhosphorWorkspaces::WorkspaceReconciler;

namespace {
QString id(int n)
{
    return QStringLiteral("{d%1}").arg(n);
}
}

/// Drives the reconciler with scripted notification sequences (no D-Bus).
/// The harness plays KWin: every requestCreateDesktop is answered by invoking
/// the created/settled callbacks the way VirtualDesktopManager would.
class TestWorkspaceReconciler : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // QSignalSpy must copy these signal argument types into QVariants.
        qRegisterMetaType<QHash<int, int>>("QHash<int,int>");
        qRegisterMetaType<QList<int>>("QList<int>");
    }
    void adoption_currentFirstThenContiguous();
    void adoption_appendsTrailingEmpty();
    void createOnOccupy_appendsNextEmpty();
    void destroyOnEmpty_debouncedAndRechecked();
    void destroyOnEmpty_namedExempt();
    void destroyOnEmpty_neverTrailingOrLast();
    void externalCreation_adoptedByFocusedScreen();
    void externalRemoval_followedAndRepaired();
    void renumber_computedFromIdDelta();
    void echo_ledgerSuppressesReactivePolicy();
    void snapBack_singleCorrectionNoLoop();
    void cap_suspendsTrailingEmpty();
    void screenRemoved_sliceReassigned();
    void verbQueries_sliceScopedNoWrap();
    void issueSetCurrent_singleInFlightPerScreen();
    void snapBack_correctsAndBreaksLoop();
    void reorderCurrentWorkspace_withinSlice();
    void transferCurrentWorkspace_reownsAndRepairs();
    void named_createdPinnedAndExempt();
    void named_claimByKWinName();
    void named_unnamedRevertsToDynamic();

private:
    /// Adopt a two-screen world: A owns {d1} (current), B owns {d2} (current).
    /// With an empty census both slices already end in an empty dynamic
    /// desktop, so adoption alone requests nothing; occupying each current
    /// desktop then drives the trailing-empty creates, answered so A has
    /// {d1(occupied), d3(empty)} and B has {d2(occupied), d4(empty)}.
    void adoptTwoScreens(WorkspaceReconciler& rec)
    {
        rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B")});
        rec.setFocusedScreen(QStringLiteral("A"));

        QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
        QHash<QString, QString> current;
        current.insert(QStringLiteral("A"), id(1));
        current.insert(QStringLiteral("B"), id(2));
        rec.adoptAll({id(1), id(2)}, current);
        QCOMPARE(createSpy.count(), 0); // both slices end in an empty desktop

        rec.onPopulationChanged(id(1), 1);
        rec.onPopulationChanged(id(2), 1);
        QCOMPARE(createSpy.count(), 2);
        // Answer them: A's lands as {d3} at global position 1, B's as {d4}.
        rec.onKwinDesktopCreated(id(3));
        rec.onKwinDesktopCreated(id(4));
        rec.onDesktopListSettled({id(1), id(3), id(2), id(4)});

        QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
        QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
        QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
        QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    }
};

void TestWorkspaceReconciler::adoption_currentFirstThenContiguous()
{
    WorkspaceReconciler rec;
    rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B")});

    // Four desktops; A shows d2, B shows d4. d1 precedes d2 → follows A's
    // segment start rule (leading run → first screen); d3 follows d2 → A.
    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(2));
    current.insert(QStringLiteral("B"), id(4));
    rec.adoptAll({id(1), id(2), id(3), id(4)}, current);

    QCOMPARE(rec.map().ownerOf(id(2)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    QCOMPARE(rec.map().ownerOf(id(1)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
}

void TestWorkspaceReconciler::adoption_appendsTrailingEmpty()
{
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    adoptTwoScreens(rec);
    // adoptTwoScreens already verified both creates were requested + realized;
    // maintenance is now quiet (both screens hold a trailing empty).
    QCOMPARE(createSpy.count(), 2);
}

void TestWorkspaceReconciler::createOnOccupy_appendsNextEmpty()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);

    // A window lands on A's trailing empty {d3} → a new empty is appended.
    rec.onPopulationChanged(id(3), 1);
    QCOMPARE(createSpy.count(), 1);
    // Position arithmetic: end of A's slice = global position 2.
    QCOMPARE(createSpy.first().first().toUInt(), 2u);

    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(5), id(2), id(4)});
    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 3);
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("A"));
}

void TestWorkspaceReconciler::destroyOnEmpty_debouncedAndRechecked()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    // Occupy A's trailing empty, realize the new one.
    rec.onPopulationChanged(id(3), 1);
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(5), id(2), id(4)});

    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);

    // {d3} (now mid-slice, occupied) empties → debounce, then remove.
    rec.onPopulationChanged(id(3), 0);
    QCOMPARE(removeSpy.count(), 0); // not before the debounce
    QTRY_COMPARE_WITH_TIMEOUT(removeSpy.count(), 1, 2000);
    QCOMPARE(removeSpy.first().first().toString(), id(3));

    // The adopt-if-lost re-check: a desktop that re-fills inside the debounce
    // window is NOT removed.
    rec.onKwinDesktopRemoved(id(3));
    rec.onDesktopListSettled({id(1), id(5), id(2), id(4)});
    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(1), 0);
    rec.onPopulationChanged(id(1), 2); // re-filled before the debounce fires
    QTest::qWait(WorkspaceReconciler::DestroyDebounceMs + 150);
    QCOMPARE(removeSpy.count(), 1); // no second remove
}

void TestWorkspaceReconciler::destroyOnEmpty_namedExempt()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    rec.map().setName(id(1), QStringLiteral("chat"));

    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);
    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(1), 0);
    QTest::qWait(WorkspaceReconciler::DestroyDebounceMs + 150);
    QCOMPARE(removeSpy.count(), 0);
}

void TestWorkspaceReconciler::destroyOnEmpty_neverTrailingOrLast()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);

    // The trailing empty {d3} bouncing 1→0 is invariant repair, not surplus.
    rec.onPopulationChanged(id(3), 1);
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(5), id(2), id(4)});
    rec.onPopulationChanged(id(5), 0); // trailing empty stays
    QTest::qWait(WorkspaceReconciler::DestroyDebounceMs + 150);
    for (const auto& call : removeSpy) {
        QVERIFY(call.first().toString() != id(5));
    }
}

void TestWorkspaceReconciler::externalCreation_adoptedByFocusedScreen()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    rec.setFocusedScreen(QStringLiteral("B"));

    // Unmatched desktopCreated (Pager/System Settings): adopt onto B, before
    // its trailing empty.
    rec.onKwinDesktopCreated(id(9));
    QCOMPARE(rec.map().ownerOf(id(9)), QStringLiteral("B"));
    QCOMPARE(rec.map().sliceIndexOf(id(9)), 1); // before trailing empty {d4}
}

void TestWorkspaceReconciler::externalRemoval_followedAndRepaired()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);

    // External removal of B's trailing empty {d4}: follow the fact, then
    // maintenance restores B's trailing empty.
    rec.onKwinDesktopRemoved(id(4));
    QVERIFY(rec.map().ownerOf(id(4)).isEmpty());
    rec.onDesktopListSettled({id(1), id(3), id(2)});
    QCOMPARE(createSpy.count(), 1);
}

void TestWorkspaceReconciler::renumber_computedFromIdDelta()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // last settled list: {d1} {d3} {d2} {d4}
    QSignalSpy renumberSpy(&rec, &WorkspaceReconciler::renumberComputed);

    // {d3} (global position 2) vanishes: 3→2, 4→3; removed = [2].
    rec.onDesktopListSettled({id(1), id(2), id(4)});
    QCOMPARE(renumberSpy.count(), 1);
    const auto oldToNew = renumberSpy.first().at(0).value<QHash<int, int>>();
    const auto removed = renumberSpy.first().at(1).value<QList<int>>();
    QCOMPARE(removed, QList<int>({2}));
    QCOMPARE(oldToNew.value(3), 2);
    QCOMPARE(oldToNew.value(4), 3);
    QVERIFY(!oldToNew.contains(1));
}

void TestWorkspaceReconciler::echo_ledgerSuppressesReactivePolicy()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    QSignalSpy foreignSpy(&rec, &WorkspaceReconciler::foreignSwitchDetected);

    // Our own create (from occupying the trailing empty) echoes back as
    // desktopCreated — matched, so NO adoption-by-focused-screen runs (it
    // realizes A's planned slice entry instead, even with focus on B).
    rec.setFocusedScreen(QStringLiteral("B"));
    rec.onPopulationChanged(id(3), 1);
    QCOMPARE(createSpy.count(), 1);
    rec.onKwinDesktopCreated(id(7));
    QCOMPARE(rec.map().ownerOf(id(7)), QStringLiteral("A"));

    // A matched SetCurrent echo triggers no foreign-switch policy: A showing
    // its own desktop is never foreign regardless.
    rec.onScreenDesktopReport(QStringLiteral("A"), 1);
    QCOMPARE(foreignSpy.count(), 0);
}

void TestWorkspaceReconciler::snapBack_singleCorrectionNoLoop()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // list: {d1} {d3} {d2} {d4}; {d2} owned by B

    QSignalSpy foreignSpy(&rec, &WorkspaceReconciler::foreignSwitchDetected);
    QSignalSpy setCurrentSpy(&rec, &WorkspaceReconciler::requestSetCurrent);

    // A externally switches onto B's desktop {d2} (global index 3).
    const bool matched = rec.onScreenDesktopReport(QStringLiteral("A"), 3);
    QVERIFY(!matched);
    QCOMPARE(foreignSpy.count(), 1);
    QCOMPARE(foreignSpy.first().at(0).toString(), QStringLiteral("A"));
    QCOMPARE(foreignSpy.first().at(1).toString(), id(2));
    QCOMPARE(foreignSpy.first().at(2).toString(), QStringLiteral("B"));

    // Phase 2 wires foreignSwitchDetected → requestSetCurrent. Simulate that
    // correction being issued and verify the loop-breaker: while the
    // SetCurrent ledger entry is open, further foreign reports for A are
    // queued (no second foreignSwitchDetected), and the correction's own echo
    // retires the entry without re-triggering policy.
    // (The reconciler exposes no direct issue API pre-Phase-2; the queueing
    // guard is exercised through the ledger by the Phase 2 wiring. Here we
    // pin the Phase 1 half: repeated identical foreign reports re-fire the
    // signal only per report arrival, and a matched echo never does.)
    rec.onScreenDesktopReport(QStringLiteral("A"), 3);
    QCOMPARE(foreignSpy.count(), 2); // unmatched external reports each surface
    QCOMPARE(setCurrentSpy.count(), 0); // Phase 1 issues no corrections itself
}

void TestWorkspaceReconciler::cap_suspendsTrailingEmpty()
{
    WorkspaceReconciler rec;
    rec.setDesktopCap(2);
    rec.onScreenOrderChanged({QStringLiteral("A")});
    QSignalSpy capSpy(&rec, &WorkspaceReconciler::capReached);
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);

    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    rec.adoptAll({id(1), id(2)}, current); // already at cap of 2

    // Both desktops occupied → trailing-empty append wanted, but capped.
    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(2), 1);
    QCOMPARE(createSpy.count(), 0);
    QCOMPARE(capSpy.count(), 1);
    // The hint fires once per episode.
    rec.onPopulationChanged(id(1), 2);
    QCOMPARE(capSpy.count(), 1);
}

void TestWorkspaceReconciler::screenRemoved_sliceReassigned()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1,d3}, B: {d2,d4}

    rec.onScreenRemoved(QStringLiteral("B"));
    QVERIFY(!rec.map().hasScreen(QStringLiteral("B")));
    // B's entries joined A before A's trailing empty: d1, d2, d4, d3.
    QCOMPARE(rec.map().ownerOf(id(2)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("A"));
    const auto slice = rec.map().slice(QStringLiteral("A"));
    QCOMPARE(slice.size(), 4);
    QCOMPARE(slice.last().desktopId, id(3)); // trailing empty stayed last
}

void TestWorkspaceReconciler::verbQueries_sliceScopedNoWrap()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // list: {d1} {d3} {d2} {d4}; A shows d1 (index 1)
    rec.onScreenDesktopReport(QStringLiteral("A"), 1);

    QCOMPARE(rec.currentDesktopIdOf(QStringLiteral("A")), id(1));
    QCOMPARE(rec.desktopIdAtOffset(QStringLiteral("A"), 1), id(3));
    QVERIFY(rec.desktopIdAtOffset(QStringLiteral("A"), -1).isEmpty()); // top edge, no wrap
    QCOMPARE(rec.desktopIdAtSliceIndex(QStringLiteral("A"), 1), id(3));
    QVERIFY(rec.desktopIdAtSliceIndex(QStringLiteral("A"), 2).isEmpty());
    // A screen showing a FOREIGN desktop resolves no offsets.
    rec.onScreenDesktopReport(QStringLiteral("A"), 3); // {d2}, B's
    QVERIFY(rec.desktopIdAtOffset(QStringLiteral("A"), 1).isEmpty());
}

void TestWorkspaceReconciler::issueSetCurrent_singleInFlightPerScreen()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    QSignalSpy setCurrentSpy(&rec, &WorkspaceReconciler::requestSetCurrent);

    QVERIFY(rec.issueSetCurrent(QStringLiteral("A"), id(3)));
    QVERIFY(!rec.issueSetCurrent(QStringLiteral("A"), id(1))); // one in flight
    QVERIFY(rec.issueSetCurrent(QStringLiteral("B"), id(4))); // other screens unaffected
    QCOMPARE(setCurrentSpy.count(), 2);

    // The echo (A now reports d3 = global index 2) retires the entry and
    // frees the screen for the next switch.
    QVERIFY(rec.onScreenDesktopReport(QStringLiteral("A"), 2));
    QVERIFY(rec.issueSetCurrent(QStringLiteral("A"), id(1)));
}

void TestWorkspaceReconciler::snapBack_correctsAndBreaksLoop()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // list: {d1} {d3} {d2} {d4}
    rec.onScreenDesktopReport(QStringLiteral("A"), 1); // remembers d1 as owned

    QSignalSpy setCurrentSpy(&rec, &WorkspaceReconciler::requestSetCurrent);

    // A lands on B's {d2} (index 3): snap-back targets the last owned d1.
    rec.onScreenDesktopReport(QStringLiteral("A"), 3);
    QVERIFY(rec.snapBack(QStringLiteral("A")));
    QCOMPARE(setCurrentSpy.count(), 1);
    QCOMPARE(setCurrentSpy.first().at(1).toString(), id(1));

    // The Pager re-asserts the foreign desktop while the correction is in
    // flight: snapBack refuses (single correction), no loop.
    rec.onScreenDesktopReport(QStringLiteral("A"), 3);
    QVERIFY(!rec.snapBack(QStringLiteral("A")));
    QCOMPARE(setCurrentSpy.count(), 1);

    // The correction's echo retires the ledger entry; A is home, snapBack is
    // a no-op (not a fresh correction).
    QVERIFY(rec.onScreenDesktopReport(QStringLiteral("A"), 1));
    QVERIFY(!rec.snapBack(QStringLiteral("A")));
    QCOMPARE(setCurrentSpy.count(), 1);
}

void TestWorkspaceReconciler::reorderCurrentWorkspace_withinSlice()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3(empty)}
    rec.onScreenDesktopReport(QStringLiteral("A"), 1);

    QVERIFY(rec.reorderCurrentWorkspace(QStringLiteral("A"), 1));
    QCOMPARE(rec.map().slice(QStringLiteral("A")).last().desktopId, id(1));
    QVERIFY(!rec.reorderCurrentWorkspace(QStringLiteral("A"), 1)); // edge
    QVERIFY(rec.reorderCurrentWorkspace(QStringLiteral("A"), -1));
    QCOMPARE(rec.map().slice(QStringLiteral("A")).first().desktopId, id(1));
}

void TestWorkspaceReconciler::transferCurrentWorkspace_reownsAndRepairs()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3}, B: {d2(occ), d4}
    rec.onScreenDesktopReport(QStringLiteral("A"), 1);

    QSignalSpy setCurrentSpy(&rec, &WorkspaceReconciler::requestSetCurrent);
    const QString moved = rec.transferCurrentWorkspace(QStringLiteral("A"), QStringLiteral("B"));
    QCOMPARE(moved, id(1));
    QCOMPARE(rec.map().ownerOf(id(1)), QStringLiteral("B"));
    // Inserted before B's trailing empty.
    QCOMPARE(rec.map().sliceIndexOf(id(1)), 1);
    // The source snapped back onto its own slice (d3, its only remaining).
    QCOMPARE(setCurrentSpy.count(), 1);
    QCOMPARE(setCurrentSpy.first().at(0).toString(), QStringLiteral("A"));
    QCOMPARE(setCurrentSpy.first().at(1).toString(), id(3));

    // A screen never gives up its last desktop.
    rec.onScreenDesktopReport(QStringLiteral("A"), 2); // A now shows d3
    QVERIFY(rec.transferCurrentWorkspace(QStringLiteral("A"), QStringLiteral("B")).isEmpty());
}

void TestWorkspaceReconciler::named_createdPinnedAndExempt()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3}, B: {d2(occ), d4}

    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    PhosphorWorkspaces::NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    chat.outputId = QStringLiteral("B");
    rec.applyNamedWorkspaces({chat}, {});

    QCOMPARE(createSpy.count(), 1);
    QCOMPARE(createSpy.first().at(1).toString(), QStringLiteral("chat"));
    // A second apply while the create is pending requests nothing extra.
    rec.applyNamedWorkspaces({chat}, {});
    QCOMPARE(createSpy.count(), 1);

    // Realize it: lands in B's slice before the trailing empty, named.
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(3), id(2), id(5), id(4)});
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("B"));
    QCOMPARE(rec.map().entryFor(id(5)).name, QStringLiteral("chat"));

    // Named + empty: destroy-exempt (it is mid-slice and empty).
    QSignalSpy removeSpy(&rec, &WorkspaceReconciler::requestRemoveDesktop);
    rec.onPopulationChanged(id(5), 1);
    rec.onPopulationChanged(id(5), 0);
    QTest::qWait(WorkspaceReconciler::DestroyDebounceMs + 150);
    for (const auto& call : removeSpy) {
        QVERIFY(call.first().toString() != id(5));
    }
}

void TestWorkspaceReconciler::named_claimByKWinName()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // last ids: {d1} {d3} {d2} {d4}

    // Restart-without-state-file shape: KWin still carries the name we
    // stamped last session on {d1} (a NON-trailing desktop — claiming a
    // trailing empty would legitimately trigger a trailing-empty repair
    // create); the declaration claims it, no name-carrying create.
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    PhosphorWorkspaces::NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    rec.applyNamedWorkspaces({chat},
                             {QStringLiteral("chat"), QStringLiteral("Desktop 2"), QStringLiteral("Desktop 3"),
                              QStringLiteral("Desktop 4")});
    QCOMPARE(createSpy.count(), 0);
    QCOMPARE(rec.map().entryFor(id(1)).name, QStringLiteral("chat"));
}

void TestWorkspaceReconciler::named_unnamedRevertsToDynamic()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    rec.map().setName(id(1), QStringLiteral("chat"));

    QSignalSpy renameSpy(&rec, &WorkspaceReconciler::requestSetDesktopName);
    rec.applyNamedWorkspaces({}, {});
    QCOMPARE(rec.map().entryFor(id(1)).name, QString());
    QCOMPARE(renameSpy.count(), 1);
    QCOMPARE(renameSpy.first().at(0).toString(), id(1));
    QVERIFY(renameSpy.first().at(1).toString().isEmpty());
}

QTEST_GUILESS_MAIN(TestWorkspaceReconciler)
#include "test_workspace_reconciler.moc"
