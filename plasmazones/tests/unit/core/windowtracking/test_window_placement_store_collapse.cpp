// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include "helpers/WindowPlacementBuilders.h"

using PhosphorEngine::EngineSlot;
using PhosphorEngine::WindowPlacement;
using PhosphorEngine::WindowPlacementStore;
using PlasmaZones::TestHelpers::makePlacement;

/**
 * @brief collapsePureFloatSiblings coverage, split from
 *        test_window_placement_store.cpp for the file-size ceiling: the
 *        close-capture convergence that prunes stale pure-float duplicates
 *        while preserving managed placements, live siblings, and
 *        distinct-monitor float memory.
 */
class TestWindowPlacementStoreCollapse : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ── collapsePureFloatSiblings (close-capture convergence) ──

    void testCollapse_dropsSameScreenFloatDuplicate()
    {
        // Two pure-float records for one app on the same screen — the duplicate
        // state that makes a reopen "open in a different spot each time" under the
        // oldest-first take(). Collapsing keeps the named (closing) record only.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("dolphin|old"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1")));
        store.record(makePlacement(QStringLiteral("dolphin|new"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1")));
        // The superseded sibling closed earlier this session, which revokes its
        // reclaim credit. That is what marks it as stale duplicate memory rather
        // than as evidence the cross-screen reclaim still reads; a record that
        // KEPT its credit is never pruned. Production always reaches the collapse
        // through a close, so this is the real shape.
        QVERIFY(store.markInstanceClosed(QStringLiteral("dolphin|old")));

        QVERIFY(store.collapsePureFloatSiblings(QStringLiteral("dolphin"), QStringLiteral("dolphin|new")));

        // Exact-windowId checks (no appId): contains(id, appId) would pass on any
        // surviving bucket record, so it can't prove the RIGHT record was kept.
        QVERIFY(store.contains(QStringLiteral("dolphin|new")));
        QVERIFY(!store.contains(QStringLiteral("dolphin|old")));
    }

    void testCollapse_keepsManagedAndOtherScreenSiblings()
    {
        WindowPlacementStore store;
        // Snapped (managed) sibling — never pruned.
        store.record(makePlacement(QStringLiteral("dolphin|snapped"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1")));
        // Pure-float on a DIFFERENT screen — distinct memory, kept.
        store.record(makePlacement(QStringLiteral("dolphin|other"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S2")));
        // Pure-float on the SAME screen as the kept record — pruned.
        store.record(makePlacement(QStringLiteral("dolphin|dup"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1")));
        store.record(makePlacement(QStringLiteral("dolphin|keep"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1")));
        // Superseded by its own earlier close — see the credit note above.
        QVERIFY(store.markInstanceClosed(QStringLiteral("dolphin|dup")));

        QVERIFY(store.collapsePureFloatSiblings(QStringLiteral("dolphin"), QStringLiteral("dolphin|keep")));

        QVERIFY(store.contains(QStringLiteral("dolphin|keep"))); // exact survival
        QVERIFY(store.contains(QStringLiteral("dolphin|snapped"))); // managed — kept
        QVERIFY(store.contains(QStringLiteral("dolphin|other"))); // different screen — kept
        QVERIFY(!store.contains(QStringLiteral("dolphin|dup"))); // same-screen float dup — pruned
    }

    void testCollapse_noopWhenKeptRecordIsManaged()
    {
        // A managed (snapped) close must not prune float siblings.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("dolphin|float"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1")));
        store.record(makePlacement(QStringLiteral("dolphin|snapped"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1")));

        // A managed keep is a no-op: nothing pruned, so the store is unchanged.
        QVERIFY(!store.collapsePureFloatSiblings(QStringLiteral("dolphin"), QStringLiteral("dolphin|snapped")));

        QVERIFY(store.contains(QStringLiteral("dolphin|float")));
        QVERIFY(store.contains(QStringLiteral("dolphin|snapped")));
    }

    void testCollapse_noopWhenKeptRecordHasNoGeometry()
    {
        // A pure-float keep with NO captured free position must not prune its
        // siblings: with no shared-screen memory to converge on, collapsing
        // would discard the sibling's only remembered spot. The managed-keep
        // no-op above cannot pin this shape (a snapped keep trips the
        // pure-float check first). The outcome is doubly enforced today (the
        // empty-geometry early-out AND the geometry-keyed shares-screen scan);
        // this pins the CONTRACT so it survives either check being refactored
        // — e.g. a shares-screen scan that started counting the record's bare
        // screenId as coverage would prune the sibling and fail here.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("dolphin|sib"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1")));
        WindowPlacement bareKeep;
        bareKeep.windowId = QStringLiteral("dolphin|bare");
        bareKeep.appId = QStringLiteral("dolphin");
        bareKeep.screenId = QStringLiteral("S1");
        EngineSlot floatSlot;
        floatSlot.state = WindowPlacement::stateFloating();
        floatSlot.zoneIds = QStringList{QStringLiteral("z1")}; // restorable, but no free geometry
        bareKeep.engines.insert(WindowPlacement::snapEngineId(), floatSlot);
        QVERIFY(store.record(bareKeep));

        QVERIFY(!store.collapsePureFloatSiblings(QStringLiteral("dolphin"), QStringLiteral("dolphin|bare")));

        QVERIFY(store.contains(QStringLiteral("dolphin|sib")));
        QVERIFY(store.contains(QStringLiteral("dolphin|bare")));
    }

    void testCollapse_absorbsPrunedSiblingOtherScreenGeometry()
    {
        // A pruned same-screen duplicate may also hold a float position on a
        // DIFFERENT monitor the kept record lacks. That position must not be lost
        // — the kept record absorbs it before the duplicate is removed.
        WindowPlacementStore store;
        // DISTINCT rects per (record, screen) so fill-missing is
        // distinguishable from overwrite-all: the kept record's own S1 spot
        // must survive, and only the missing S2 spot is absorbed.
        const QRect sibS1(50, 60, 700, 500);
        const QRect sibS2(1970, 80, 640, 480);
        const QRect keepS1(110, 120, 800, 600);
        // Sibling floated on S1 then S2 — its single record accumulates both.
        store.record(makePlacement(QStringLiteral("dolphin|sib"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1"), sibS1));
        store.record(makePlacement(QStringLiteral("dolphin|sib"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S2"), sibS2));
        // Kept record floated only on S1.
        store.record(makePlacement(QStringLiteral("dolphin|keep"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1"), keepS1));
        // Superseded by its own earlier close — see the credit note above.
        QVERIFY(store.markInstanceClosed(QStringLiteral("dolphin|sib")));

        QVERIFY(store.collapsePureFloatSiblings(QStringLiteral("dolphin"), QStringLiteral("dolphin|keep")));

        QVERIFY(!store.contains(QStringLiteral("dolphin|sib"))); // S1-sharing duplicate pruned
        const auto kept = store.peek(QStringLiteral("dolphin|keep"), QStringLiteral("dolphin"));
        QVERIFY(kept.has_value());
        QCOMPARE(kept->freeGeometryFor(QStringLiteral("S1")), keepS1); // own spot kept, NOT overwritten
        QCOMPARE(kept->freeGeometryFor(QStringLiteral("S2")), sibS2); // missing spot absorbed
    }

    void testCollapse_transitiveCollapseIsOrderIndependent()
    {
        // bridge(S1+S2) connects keep(S1) to leaf(S2). The leaf shares NO screen
        // with the kept record directly — only through the screen the bridge
        // contributes. A naive single backward pass would process the leaf (newer,
        // higher index) before the bridge and miss it; the fixpoint re-scans after
        // absorbing the bridge's S2 and prunes the leaf too. Whole connected set
        // collapses into keep regardless of FIFO order.
        WindowPlacementStore store;
        // Distinct rects so absorb provenance is assertable (see the
        // absorb test above): keep's own S1 survives, and S2 comes from the
        // BRIDGE (absorbed first); the leaf's S2 is never absorbed over it.
        const QRect bridgeS1(30, 40, 500, 400);
        const QRect bridgeS2(2000, 50, 600, 450);
        const QRect leafS2(2200, 90, 640, 480);
        const QRect keepS1(130, 140, 820, 620);
        store.record(makePlacement(QStringLiteral("dolphin|bridge"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1"), bridgeS1));
        store.record(makePlacement(QStringLiteral("dolphin|bridge"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S2"), bridgeS2)); // bridge now S1+S2
        store.record(makePlacement(QStringLiteral("dolphin|leaf"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S2"), leafS2)); // newer than bridge
        store.record(makePlacement(QStringLiteral("dolphin|keep"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1"), keepS1)); // newest
        // Both superseded siblings closed earlier this session — see the credit
        // note in the first test. A credit-bearing record is never pruned.
        QVERIFY(store.markInstanceClosed(QStringLiteral("dolphin|bridge")));
        QVERIFY(store.markInstanceClosed(QStringLiteral("dolphin|leaf")));

        QVERIFY(store.collapsePureFloatSiblings(QStringLiteral("dolphin"), QStringLiteral("dolphin|keep")));

        QVERIFY(store.contains(QStringLiteral("dolphin|keep")));
        QVERIFY(!store.contains(QStringLiteral("dolphin|bridge"))); // shares S1 → pruned (S2 absorbed)
        QVERIFY(!store.contains(QStringLiteral("dolphin|leaf"))); // connected via absorbed S2 → pruned
        const auto kept = store.peek(QStringLiteral("dolphin|keep"), QStringLiteral("dolphin"));
        QVERIFY(kept.has_value());
        QCOMPARE(kept->freeGeometryFor(QStringLiteral("S1")), keepS1);
        QCOMPARE(kept->freeGeometryFor(QStringLiteral("S2")), bridgeS2);

        // The other FIFO order — leaf recorded BEFORE the bridge. NOTE: with
        // this ordering the backward prune scan reaches the bridge FIRST, so
        // this is the EASY case; the ORIGINAL ordering above (leaf after
        // bridge) is the one that exercises the fixpoint re-scan. The row is
        // still not redundant — bucket positions and sequences differ — it
        // just is not the discriminating permutation.
        WindowPlacementStore reversed;
        reversed.record(makePlacement(QStringLiteral("dolphin|leaf"), QStringLiteral("dolphin"),
                                      WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                      QStringLiteral("S2"), leafS2));
        reversed.record(makePlacement(QStringLiteral("dolphin|bridge"), QStringLiteral("dolphin"),
                                      WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                      QStringLiteral("S1"), bridgeS1));
        reversed.record(makePlacement(QStringLiteral("dolphin|bridge"), QStringLiteral("dolphin"),
                                      WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                      QStringLiteral("S2"), bridgeS2));
        reversed.record(makePlacement(QStringLiteral("dolphin|keep"), QStringLiteral("dolphin"),
                                      WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                      QStringLiteral("S1"), keepS1));
        QVERIFY(reversed.markInstanceClosed(QStringLiteral("dolphin|bridge")));
        QVERIFY(reversed.markInstanceClosed(QStringLiteral("dolphin|leaf")));

        QVERIFY(reversed.collapsePureFloatSiblings(QStringLiteral("dolphin"), QStringLiteral("dolphin|keep")));
        QVERIFY(reversed.contains(QStringLiteral("dolphin|keep")));
        QVERIFY(!reversed.contains(QStringLiteral("dolphin|bridge")));
        QVERIFY(!reversed.contains(QStringLiteral("dolphin|leaf")));
        const auto keptRev = reversed.peek(QStringLiteral("dolphin|keep"), QStringLiteral("dolphin"));
        QVERIFY(keptRev.has_value());
        QCOMPARE(keptRev->freeGeometryFor(QStringLiteral("S1")), keepS1);
        QCOMPARE(keptRev->freeGeometryFor(QStringLiteral("S2")), bridgeS2);
    }

    void testCollapse_prefixMutationStillFindsKeptRecord()
    {
        // The kept record is addressed with a MUTATED appId prefix (a window
        // that renamed its class mid-session closes under the new composite).
        // findKeep matches by instance, so the collapse must still resolve the
        // stored record and prune the same-screen duplicate — an exact-id
        // compare would silently no-op for exactly this renamed-window case.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("dolphin|old-uuid"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1")));
        store.record(makePlacement(QStringLiteral("dolphin|kept-uuid"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1")));
        // Superseded by its own earlier close — see the credit note above.
        QVERIFY(store.markInstanceClosed(QStringLiteral("dolphin|old-uuid")));

        QVERIFY(
            store.collapsePureFloatSiblings(QStringLiteral("dolphin"), QStringLiteral("org.kde.dolphin|kept-uuid")));

        QVERIFY(store.contains(QStringLiteral("dolphin|kept-uuid")));
        QVERIFY(!store.contains(QStringLiteral("dolphin|old-uuid")));
    }
};

QTEST_MAIN(TestWindowPlacementStoreCollapse)
#include "test_window_placement_store_collapse.moc"
