// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "workspacereconcilerharness.h"

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QSignalSpy>
#include <QTest>

using PhosphorWorkspaces::WorkspaceReconciler;
using WorkspaceTest::adoptTwoScreens;
using WorkspaceTest::id;
using WorkspaceTest::openTwoConcurrentCreates;

/// The structural behaviour of the reconciler: adoption, ownership, the verbs,
/// named workspaces and hotplug. Driven with scripted notification sequences
/// (no D-Bus); the harness plays KWin, answering every requestCreateDesktop by
/// invoking the created/settled callbacks the way VirtualDesktopManager would.
/// The timing-driven paths (the destroy debounce, ledger expiry, the cap probe
/// and the removal-refusal budget) live in test_workspace_ledger.
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
    void adoption_trailingEmptyIsUnnamedAndSettles();
    void createOnOccupy_appendsNextEmpty();
    void externalCreation_adoptedByFocusedScreen();
    void externalRemoval_followedAndRepaired();
    void renumber_computedFromIdDelta();
    void echo_ledgerSuppressesReactivePolicy();
    void foreign_everyUnmatchedReportSurfaces();
    void screenRemoved_sliceReassigned();
    void verbQueries_sliceScopedNoWrap();
    void issueSetCurrent_singleInFlightPerScreen();
    void snapBack_correctsAndBreaksLoop();
    void reorderCurrentWorkspace_withinSlice();
    void transferCurrentWorkspace_reownsAndRepairs();
    void hotplug_homeStampAndMigrateBack();
    void restore_candidateReconciledAgainstReality();
    void named_createdPinnedAndExempt();
    void named_pinTransfersRealizedWorkspaceAndClearsHome();
    void named_pinDeclinedWhenSourceHoldsItsLastDesktop();
    void named_declaredPositionMovesARealizedWorkspace();
    void named_claimByKWinName();
    void named_unnamedRevertsToDynamic();
    void named_createEchoFifoMismatchHealedByKWinNames();
    void named_placeholderNameNeverClaims();
    void named_explicitPositionStaysBeforeTrailingEmpty();
    void settled_emptyListIsIgnored();
    void screenAdded_freshScreenGetsItsOwnDesktop();
    void screenAdded_bothHotplugSignalOrdersGetADesktop();
    void create_secondPendingSurvivesTheFirstsSettle();
    void create_settledBeforeEchoLandsOnRequestingScreen();
    void create_settledMatchesByPositionNotRequestOrder();
    void create_threeConcurrentSettleLandOnTheirOwnScreens();
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

void TestWorkspaceReconciler::adoption_trailingEmptyIsUnnamedAndSettles()
{
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    adoptTwoScreens(rec);

    // What the trailing-empty invariant actually claims: each slice ends in a
    // desktop that is empty AND dynamic, since a named one is destroy-exempt
    // and could never play the role.
    for (const QString& screenId : {QStringLiteral("A"), QStringLiteral("B")}) {
        const auto slice = rec.map().slice(screenId);
        QVERIFY(!slice.isEmpty());
        QVERIFY(slice.last().name.isEmpty());
        QCOMPARE(rec.map().sliceIndexOf(slice.last().desktopId), slice.size() - 1);
    }

    // And that it SETTLES: a repeat of the same settled list asks for nothing
    // more, so maintenance is not quietly re-requesting on every reply.
    QCOMPARE(createSpy.count(), 2);
    rec.onDesktopListSettled({id(1), id(3), id(2), id(4)});
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

void TestWorkspaceReconciler::foreign_everyUnmatchedReportSurfaces()
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

    // Detection is per report arrival: with no correction in flight, a repeat
    // of the same external switch surfaces again rather than being swallowed
    // as a duplicate. (The loop-breaker that suppresses the repeats WHILE a
    // correction is open is pinned by snapBack_correctsAndBreaksLoop.)
    rec.onScreenDesktopReport(QStringLiteral("A"), 3);
    QCOMPARE(foreignSpy.count(), 2);
    // Detection alone issues nothing: the correction is the caller's call.
    QCOMPARE(setCurrentSpy.count(), 0);
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
    // frees the screen for the next switch — which really is ISSUED, not just
    // accepted by the return value.
    QVERIFY(rec.onScreenDesktopReport(QStringLiteral("A"), 2));
    QVERIFY(rec.issueSetCurrent(QStringLiteral("A"), id(1)));
    QCOMPARE(setCurrentSpy.count(), 3);
    QCOMPARE(setCurrentSpy.last().at(0).toString(), QStringLiteral("A"));
    QCOMPARE(setCurrentSpy.last().at(1).toString(), id(1));
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

void TestWorkspaceReconciler::hotplug_homeStampAndMigrateBack()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3}, B: {d2(occ), d4}

    // Unplug B: its slice fosters onto A, home-stamped.
    rec.onScreenRemoved(QStringLiteral("B"));
    QCOMPARE(rec.map().ownerOf(id(2)), QStringLiteral("A"));
    QCOMPARE(rec.map().entryFor(id(2)).homeScreenId, QStringLiteral("B"));
    QCOMPARE(rec.map().entryFor(id(4)).homeScreenId, QStringLiteral("B"));
    // A's own entries carry no home stamp.
    QVERIFY(rec.map().entryFor(id(1)).homeScreenId.isEmpty());

    // Replug B: the displaced entries migrate home in order, stamps cleared.
    rec.onScreenAdded(QStringLiteral("B"));
    QCOMPARE(rec.map().ownerOf(id(2)), QStringLiteral("B"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    QVERIFY(rec.map().entryFor(id(2)).homeScreenId.isEmpty());
    QCOMPARE(rec.map().slice(QStringLiteral("B")).first().desktopId, id(2));
    // A keeps its own slice intact.
    QCOMPARE(rec.map().ownerOf(id(1)), QStringLiteral("A"));
}

void TestWorkspaceReconciler::restore_candidateReconciledAgainstReality()
{
    // Previous session: A owned {d1(named chat), d2}; B owned {d9}.
    PhosphorWorkspaces::WorkspaceMap candidate;
    candidate.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    PhosphorWorkspaces::WorkspaceEntry chat;
    chat.desktopId = id(1);
    chat.name = QStringLiteral("chat");
    candidate.insert(QStringLiteral("A"), 0, chat);
    PhosphorWorkspaces::WorkspaceEntry d2;
    d2.desktopId = id(2);
    candidate.insert(QStringLiteral("A"), 1, d2);
    PhosphorWorkspaces::WorkspaceEntry d9;
    d9.desktopId = id(9);
    candidate.insert(QStringLiteral("B"), 0, d9);

    WorkspaceReconciler rec;
    QVERIFY(rec.map().fromJson(candidate.toJson(1, {}, nullptr, /*includeState=*/true)));
    // The restore carries the screen order itself; asserted HERE, because the
    // reorder below would hand it the same order and mask a parse that lost it.
    QCOMPARE(rec.map().screenOrder(), QStringList({QStringLiteral("A"), QStringLiteral("B")}));
    rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B")});

    // Reality: {d9} vanished while the daemon was down; {d5} is new.
    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    current.insert(QStringLiteral("B"), id(5));
    rec.adoptAll({id(1), id(2), id(5)}, current);

    QCOMPARE(rec.map().entryFor(id(1)).name, QStringLiteral("chat")); // kept
    QCOMPARE(rec.map().ownerOf(id(2)), QStringLiteral("A")); // kept
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("B")); // adopted (B shows it)
    QVERIFY(rec.map().ownerOf(id(9)).isEmpty()); // vanished id dropped
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
    QCOMPARE(removeSpy.count(), 0);
}

void TestWorkspaceReconciler::named_claimByKWinName()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // last ids: {d1} {d3} {d2} {d4}

    // Restart-without-state-file shape: KWin still carries the name we
    // stamped last session on {d1} (a NON-trailing desktop — claiming a
    // trailing empty would legitimately trigger a trailing-empty repair
    // create); the declaration claims it, no name-carrying create.
    // The name list is KWin's RAW form: an unnamed desktop is an EMPTY entry,
    // never a "Desktop N" placeholder.
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    PhosphorWorkspaces::NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    rec.applyNamedWorkspaces({chat}, {QStringLiteral("chat"), QString(), QString(), QString()});
    QCOMPARE(createSpy.count(), 0);
    QCOMPARE(rec.map().entryFor(id(1)).name, QStringLiteral("chat"));
}

void TestWorkspaceReconciler::named_placeholderNameNeverClaims()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // last ids: {d1} {d3} {d2} {d4}, all unnamed in KWin

    // A workspace a user happened to call "Desktop 2" must NOT claim the
    // second desktop just because a display placeholder would read that way.
    // Raw names are empty for every unnamed desktop, so nothing matches and
    // the declaration is created instead.
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    PhosphorWorkspaces::NamedWorkspace decoy;
    decoy.name = QStringLiteral("Desktop 2");
    rec.applyNamedWorkspaces({decoy}, {QString(), QString(), QString(), QString()});

    QCOMPARE(createSpy.count(), 1);
    QCOMPARE(createSpy.first().at(1).toString(), QStringLiteral("Desktop 2"));
    for (const QString& desktopId : {id(1), id(2), id(3), id(4)}) {
        QVERIFY(rec.map().entryFor(desktopId).name.isEmpty());
    }
}

void TestWorkspaceReconciler::named_explicitPositionStaysBeforeTrailingEmpty()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3(trailing empty)}

    // An out-of-range explicit position must clamp to the slot BEFORE the
    // trailing empty. Landing behind it would demote the empty desktop to
    // mid-slice (maintenance then reaps it) and hand the trailing role to a
    // named workspace, which is destroy-exempt — the invariant would never
    // recover.
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    PhosphorWorkspaces::NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    chat.outputId = QStringLiteral("A");
    chat.position = 9;
    rec.applyNamedWorkspaces({chat}, {});
    QCOMPARE(createSpy.count(), 1);
    // A's slice starts at global position 0 and holds two desktops, so the
    // slot before its trailing empty is global position 1.
    QCOMPARE(createSpy.first().first().toUInt(), 1u);

    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(5), id(3), id(2), id(4)});
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("A"));
    QCOMPARE(rec.map().sliceIndexOf(id(5)), 1);
    // The trailing empty is still last, and still the unnamed one.
    const auto slice = rec.map().slice(QStringLiteral("A"));
    QCOMPARE(slice.last().desktopId, id(3));
    QVERIFY(slice.last().name.isEmpty());
}

void TestWorkspaceReconciler::settled_emptyListIsIgnored()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1,d3}, B: {d2,d4}

    // A failed upstream read surfaces as an empty list. KWin never has zero
    // desktops, so acting on it would drop every slice and have the engines
    // reap all per-desktop state. Refuse it and ask for a resync instead.
    QSignalSpy resyncSpy(&rec, &WorkspaceReconciler::resyncRequested);
    QSignalSpy renumberSpy(&rec, &WorkspaceReconciler::renumberComputed);
    rec.onDesktopListSettled({});

    QCOMPARE(renumberSpy.count(), 0);
    QCOMPARE(resyncSpy.count(), 1);
    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
    QCOMPARE(rec.map().ownerOf(id(1)), QStringLiteral("A"));
    // And the verbs still resolve against the kept list.
    rec.onScreenDesktopReport(QStringLiteral("A"), 1);
    QCOMPARE(rec.currentDesktopIdOf(QStringLiteral("A")), id(1));
}

void TestWorkspaceReconciler::screenAdded_freshScreenGetsItsOwnDesktop()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);

    // A monitor with no hotplug history joins: it owns nothing, so the
    // slice-never-empty invariant asks for a desktop of its own.
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    rec.onScreenAdded(QStringLiteral("C"));
    QCOMPARE(createSpy.count(), 1);
    QVERIFY(rec.hasPendingStructuralOps()); // the Create is open

    rec.onKwinDesktopCreated(id(6));
    rec.onDesktopListSettled({id(1), id(3), id(2), id(4), id(6)});
    QCOMPARE(rec.map().ownerOf(id(6)), QStringLiteral("C"));
    QVERIFY(!rec.hasPendingStructuralOps()); // and retired once it landed
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

void TestWorkspaceReconciler::named_createEchoFifoMismatchHealedByKWinNames()
{
    using PhosphorWorkspaces::NamedWorkspace;
    WorkspaceReconciler rec;
    adoptTwoScreens(rec);
    rec.setFocusedScreen(QStringLiteral("A"));

    QList<NamedWorkspace> declarations;
    NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    declarations.append(chat);
    rec.applyNamedWorkspaces(declarations, {QString(), QString(), QString(), QString()});

    // An EXTERNAL creation races our named create and steals the FIFO match:
    // {d7} is realized under the declared name while OUR desktop {d8} (the
    // one KWin actually created with the name) adopts as external.
    rec.onKwinDesktopCreated(id(7));
    rec.onKwinDesktopCreated(id(8));
    rec.onDesktopListSettled({id(1), id(3), id(7), id(2), id(4), id(8)});
    QCOMPARE(rec.map().entryFor(id(7)).name, QStringLiteral("chat"));

    // Re-verifying against KWin's OWN name list (what the controller does
    // after every settle) moves the identity to the desktop that really
    // carries the name.
    // Raw KWin names: only the desktop KWin really named carries a string.
    const QStringList kwinNames{QString(), QString(), QString(), QString(), QString(), QStringLiteral("chat")};
    rec.applyNamedWorkspaces(declarations, kwinNames);
    QCOMPARE(rec.map().entryFor(id(8)).name, QStringLiteral("chat"));
    QVERIFY(rec.map().entryFor(id(7)).name.isEmpty());
}

void TestWorkspaceReconciler::create_secondPendingSurvivesTheFirstsSettle()
{
    // Two Creates open at once, one landing. The settle must retire ONLY the
    // op the landed desktop answered: retiring a second by arithmetic (the id
    // count grew by one, so "one create landed") left B's echo with no ledger
    // entry, which adopted B's desktop onto the FOCUSED screen and let
    // maintainScreen request a duplicate in the meantime.
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    openTwoConcurrentCreates(rec, createSpy);

    // A's create lands: echo first, then the settled list.
    rec.onKwinDesktopCreated(id(3));
    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
    rec.onDesktopListSettled({id(1), id(3), id(2)});

    // B's Create is still open: no duplicate request, and B's slice is
    // untouched while it waits.
    QCOMPARE(createSpy.count(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 1);

    // B's echo lands on the screen that ASKED, not on the focused screen.
    rec.onKwinDesktopCreated(id(4));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    rec.onDesktopListSettled({id(1), id(3), id(2), id(4)});
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    QCOMPARE(createSpy.count(), 2);
}

void TestWorkspaceReconciler::create_settledBeforeEchoLandsOnRequestingScreen()
{
    // The other order: both creates land and the settled list arrives with no
    // id-only echo at all (KWin's desktopCreated lost, or the refresh simply
    // won the race). Each new id consumes the open Create of matching RANK
    // among the requested global positions (the echo path is the FIFO one), so
    // each desktop lands on the screen that asked for it instead of being
    // adopted onto the focused one. Here the two rankings happen to agree;
    // create_settledMatchesByPositionNotRequestOrder is the case where they do
    // not.
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    openTwoConcurrentCreates(rec, createSpy);

    rec.onDesktopListSettled({id(1), id(3), id(2), id(4)});

    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
    // Both ops were consumed, so maintenance is quiet again.
    QCOMPARE(createSpy.count(), 2);
}

void TestWorkspaceReconciler::create_settledMatchesByPositionNotRequestOrder()
{
    // Same settle-beats-the-echo race, but B's population change fires first,
    // so the ledger reads [Create(B), Create(A)] while KWin's list still puts
    // A's new desktop (d3, index 1) ahead of B's (d4, index 3). Matching the
    // oldest request first would hand d3 to B and d4 to A — each desktop in
    // the wrong screen's slice, and invisible afterwards because both screens
    // still end in a trailing empty. The match is by requested POSITION.
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
    openTwoConcurrentCreates(rec, createSpy, /*populateBFirst=*/true);

    rec.onDesktopListSettled({id(1), id(3), id(2), id(4)});

    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
    QCOMPARE(createSpy.count(), 2);
}

void TestWorkspaceReconciler::create_threeConcurrentSettleLandOnTheirOwnScreens()
{
    // Three screens repairing their trailing empty in ONE maintenance pass,
    // all answered by a single settle with no echoes. globalPosition is taken
    // at request time and requestCreateAt does not mutate the map, so the
    // three requests record 1, 2 and 3 while the settled list puts the new
    // desktops at indices 1, 3 and 5. That uniform low drift is what breaks a
    // per-id nearest-distance match: for d5 (index 3) B is |2-3|=1 and C is
    // |3-3|=0, so C would win a slot that belongs to B and the two screens
    // would swap desktops silently, each still ending in a trailing empty.
    // Pairing by rank is immune to the drift.
    WorkspaceReconciler rec;
    QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);

    rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    rec.setFocusedScreen(QStringLiteral("A"));

    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    current.insert(QStringLiteral("B"), id(2));
    current.insert(QStringLiteral("C"), id(3));
    rec.adoptAll({id(1), id(2), id(3)}, current);
    QCOMPARE(createSpy.count(), 0);

    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(2), 1);
    rec.onPopulationChanged(id(3), 1);
    QCOMPARE(createSpy.count(), 3);

    rec.onDesktopListSettled({id(1), id(4), id(2), id(5), id(3), id(6)});

    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("B"));
    QCOMPARE(rec.map().ownerOf(id(6)), QStringLiteral("C"));
    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("C")).size(), 2);
    // Every op was consumed, so maintenance asks for nothing more.
    QCOMPARE(createSpy.count(), 3);
}

void TestWorkspaceReconciler::named_pinTransfersRealizedWorkspaceAndClearsHome()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3}, B: {d2(occ), d4}
    rec.setFocusedScreen(QStringLiteral("A"));

    // Realize "chat" unpinned, so it lands on the focused screen.
    PhosphorWorkspaces::NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    rec.applyNamedWorkspaces({chat}, {});
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(5), id(3), id(2), id(4)});
    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("A"));

    // A past hotplug left a home stamp on it, and A is currently SHOWING it
    // (global index 2).
    rec.map().setHomeScreen(id(5), QStringLiteral("A"));
    rec.onScreenDesktopReport(QStringLiteral("A"), 2);

    QSignalSpy foreignSpy(&rec, &WorkspaceReconciler::foreignSwitchDetected);
    chat.outputId = QStringLiteral("B");
    rec.applyNamedWorkspaces({chat}, {});

    QCOMPARE(rec.map().ownerOf(id(5)), QStringLiteral("B"));
    // A pin is a deliberate placement, so it outranks hotplug memory: leaving
    // the stamp would yank the workspace back off B on the next replug of A.
    QVERIFY(rec.map().entryFor(id(5)).homeScreenId.isEmpty());
    // A was left sitting on a desktop it no longer owns, which is exactly the
    // owner-wins case the verb-side transfer path also raises.
    QCOMPARE(foreignSpy.count(), 1);
    QCOMPARE(foreignSpy.first().at(0).toString(), QStringLiteral("A"));
    QCOMPARE(foreignSpy.first().at(1).toString(), id(5));
    QCOMPARE(foreignSpy.first().at(2).toString(), QStringLiteral("B"));
}

void TestWorkspaceReconciler::named_pinDeclinedWhenSourceHoldsItsLastDesktop()
{
    WorkspaceReconciler rec;
    rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B")});
    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    current.insert(QStringLiteral("B"), id(2));
    rec.adoptAll({id(1), id(2)}, current); // one desktop each

    // Claim {d1} by the name KWin still carries for it.
    PhosphorWorkspaces::NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    rec.applyNamedWorkspaces({chat}, {QStringLiteral("chat"), QString()});
    QCOMPARE(rec.map().entryFor(id(1)).name, QStringLiteral("chat"));

    // Pinning it to B would empty A's slice, which no repair can refill at the
    // cap, so the transfer is declined and the workspace stays put.
    chat.outputId = QStringLiteral("B");
    rec.applyNamedWorkspaces({chat}, {QStringLiteral("chat"), QString()});
    QCOMPARE(rec.map().ownerOf(id(1)), QStringLiteral("A"));
    QCOMPARE(rec.map().sliceSize(QStringLiteral("A")), 1);
}

void TestWorkspaceReconciler::named_declaredPositionMovesARealizedWorkspace()
{
    WorkspaceReconciler rec;
    adoptTwoScreens(rec); // A: {d1(occ), d3}

    PhosphorWorkspaces::NamedWorkspace chat;
    chat.name = QStringLiteral("chat");
    chat.outputId = QStringLiteral("A");
    rec.applyNamedWorkspaces({chat}, {});
    rec.onKwinDesktopCreated(id(5));
    rec.onDesktopListSettled({id(1), id(5), id(3), id(2), id(4)});
    QCOMPARE(rec.map().sliceIndexOf(id(5)), 1);

    // The declared position applies to a workspace that is ALREADY realized on
    // the screen it belongs to, not only at creation and pin time — that gap
    // is what made the settings Position control do nothing.
    chat.position = 0;
    rec.applyNamedWorkspaces({chat}, {});
    QCOMPARE(rec.map().sliceIndexOf(id(5)), 0);
    QCOMPARE(rec.map().slice(QStringLiteral("A")).last().desktopId, id(3));

    // Idempotent: re-applying the same declaration moves nothing and announces
    // nothing, so this cannot become a per-settle reorder loop.
    QSignalSpy mapSpy(&rec, &WorkspaceReconciler::mapChanged);
    rec.applyNamedWorkspaces({chat}, {});
    QCOMPARE(rec.map().sliceIndexOf(id(5)), 0);
    QCOMPARE(mapSpy.count(), 0);

    // An out-of-range position clamps to the last slot BEFORE the trailing
    // empty; landing behind it would hand the trailing role to a destroy-exempt
    // workspace and the invariant would never recover.
    chat.position = 9;
    rec.applyNamedWorkspaces({chat}, {});
    QCOMPARE(rec.map().sliceIndexOf(id(5)), 1);
    QCOMPARE(rec.map().slice(QStringLiteral("A")).last().desktopId, id(3));
}

void TestWorkspaceReconciler::screenAdded_bothHotplugSignalOrdersGetADesktop()
{
    // A hotplug reaches the reconciler as two calls, and which one arrives
    // first depends on how the controller recomputes its screen order. Both
    // orders must end with the new output holding a workspace of its own.
    {
        // Order 1: the recomputed order already names the new screen.
        WorkspaceReconciler rec;
        adoptTwoScreens(rec);
        QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
        rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
        rec.onScreenAdded(QStringLiteral("C"));
        QCOMPARE(createSpy.count(), 1);
        QVERIFY(rec.map().screenOrder().contains(QStringLiteral("C")));

        rec.onKwinDesktopCreated(id(6));
        rec.onDesktopListSettled({id(1), id(3), id(2), id(4), id(6)});
        QCOMPARE(rec.map().ownerOf(id(6)), QStringLiteral("C"));
    }
    {
        // Order 2: screenAdded first, the order recomputed afterwards.
        WorkspaceReconciler rec;
        adoptTwoScreens(rec);
        QSignalSpy createSpy(&rec, &WorkspaceReconciler::requestCreateDesktop);
        rec.onScreenAdded(QStringLiteral("C"));
        rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
        QCOMPARE(createSpy.count(), 1);

        rec.onKwinDesktopCreated(id(6));
        rec.onDesktopListSettled({id(1), id(3), id(2), id(4), id(6)});
        QCOMPARE(rec.map().ownerOf(id(6)), QStringLiteral("C"));
    }
}

QTEST_GUILESS_MAIN(TestWorkspaceReconciler)
#include "test_workspace_reconciler.moc"
