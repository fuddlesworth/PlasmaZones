// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include <QJsonArray>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>

using PhosphorEngine::EngineSlot;
using PhosphorEngine::WindowPlacement;
using PhosphorEngine::WindowPlacementStore;

namespace {
// Build a single-engine partial record: the calling engine's slot plus, for the
// un-managed states (free/floating), the shared per-screen free geometry the
// capture orchestrator would supply. record() merges these into the one record
// per window.
WindowPlacement make(const QString& windowId, const QString& appId, const QString& state, const QString& engine,
                     const QString& screen = QStringLiteral("DP-1"))
{
    WindowPlacement p;
    p.windowId = windowId;
    p.appId = appId;
    p.screenId = screen;
    EngineSlot slot;
    slot.state = state;
    if (state == WindowPlacement::stateSnapped()) {
        slot.zoneIds = QStringList{QStringLiteral("z1")};
    } else if (state == WindowPlacement::stateTiled()) {
        slot.order = 0;
    }
    p.engines.insert(engine, slot);
    if (state == WindowPlacement::stateFree() || state == WindowPlacement::stateFloating()) {
        p.freeGeometryByScreen.insert(screen, QRect(10, 20, 300, 400));
    }
    return p;
}
} // namespace

class TestWindowPlacementStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testRecordAndTake_exact()
    {
        WindowPlacementStore store;
        store.record(make(QStringLiteral("firefox|a"), QStringLiteral("firefox"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId()));
        QCOMPARE(store.size(), 1);

        auto p = store.take(QStringLiteral("firefox|a"), QStringLiteral("firefox"));
        QVERIFY(p.has_value());
        QCOMPARE(p->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateSnapped()));
        QCOMPARE(store.size(), 0); // consumed
    }

    void testWithinEngineReplace_recordMergesSlotByWindowId()
    {
        // Within one engine, re-recording the SAME windowId overwrites that engine's
        // slot in place. A floated→snapped window must not keep a stale snap "floating"
        // slot (the "snapped window floats on login" bug).
        WindowPlacementStore store;
        const QString id = QStringLiteral("settings|x");
        store.record(
            make(id, QStringLiteral("settings"), WindowPlacement::stateFloating(), WindowPlacement::snapEngineId()));
        store.record(
            make(id, QStringLiteral("settings"), WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));

        QCOMPARE(store.size(), 1); // one record per window
        auto p = store.take(id, QStringLiteral("settings"));
        QVERIFY(p.has_value());
        QCOMPARE(p->slotFor(WindowPlacement::snapEngineId()).state,
                 QString(WindowPlacement::stateSnapped())); // latest wins
    }

    void testPerEngineCoexistence_oneRecordTwoSlots()
    {
        // Per-mode state independence: the SAME window may be snapped in snapping mode
        // AND floated in autotile mode at once. Both live in ONE record under distinct
        // engine slots — recording the autotile slot does NOT clobber the snap slot.
        WindowPlacementStore store;
        const QString id = QStringLiteral("settings|x");
        store.record(
            make(id, QStringLiteral("settings"), WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        store.record(make(id, QStringLiteral("settings"), WindowPlacement::stateFloating(),
                          WindowPlacement::autotileEngineId()));

        QCOMPARE(store.size(), 1); // merged into a single per-window record

        auto p = store.peek(id, QStringLiteral("settings"));
        QVERIFY(p.has_value());
        QCOMPARE(p->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateSnapped()));
        QCOMPARE(p->slotFor(WindowPlacement::autotileEngineId()).state, QString(WindowPlacement::stateFloating()));
    }

    void testMerge_sharedFreeGeometryAndPerEngineSlots()
    {
        // The shared free/float geometry is ONE per window (keyed per screen) and is
        // preserved across engine-slot updates; recording a snap snapped slot must not
        // drop a free geometry captured earlier, nor the autotile slot.
        WindowPlacementStore store;
        const QString id = QStringLiteral("zed|1");
        // Autotile floats it on DP-1 (captures a free geometry on DP-1).
        store.record(make(id, QStringLiteral("zed"), WindowPlacement::stateFloating(),
                          WindowPlacement::autotileEngineId(), QStringLiteral("DP-1")));
        // Snap snaps it (a managed slot — no geometry of its own).
        store.record(make(id, QStringLiteral("zed"), WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId(),
                          QStringLiteral("DP-1")));

        auto p = store.peek(id, QStringLiteral("zed"));
        QVERIFY(p.has_value());
        QCOMPARE(p->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateSnapped()));
        QCOMPARE(p->slotFor(WindowPlacement::autotileEngineId()).state, QString(WindowPlacement::stateFloating()));
        QCOMPARE(p->freeGeometryFor(QStringLiteral("DP-1")), QRect(10, 20, 300, 400)); // survived the snap merge
    }

    void testRecord_returnsChangedOnlyOnContentChange()
    {
        // record() reports whether the store actually changed so the save loop can
        // settle: a content-identical re-capture (sequence aside) returns false.
        WindowPlacementStore store;
        const QString id = QStringLiteral("term|1");
        QVERIFY(store.record(
            make(id, QStringLiteral("term"), WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId())));
        // Identical content → no change.
        QVERIFY(!store.record(
            make(id, QStringLiteral("term"), WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId())));
        // Different state → changed.
        QVERIFY(store.record(
            make(id, QStringLiteral("term"), WindowPlacement::stateFloating(), WindowPlacement::snapEngineId())));
    }

    void testUuidExactBeforeAppIdFifo()
    {
        WindowPlacementStore store;
        // Two instances of the same app, different uuids/states.
        store.record(make(QStringLiteral("term|1"), QStringLiteral("term"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId()));
        store.record(make(QStringLiteral("term|2"), QStringLiteral("term"), WindowPlacement::stateFloating(),
                          WindowPlacement::snapEngineId()));

        // Exact uuid match takes its own record, not the FIFO head.
        auto p2 = store.take(QStringLiteral("term|2"), QStringLiteral("term"));
        QVERIFY(p2.has_value());
        QCOMPARE(p2->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateFloating()));

        // Remaining is term|1.
        auto p1 = store.take(QStringLiteral("term|unknown-uuid"), QStringLiteral("term"));
        QVERIFY(p1.has_value());
        QCOMPARE(p1->windowId, QStringLiteral("term|1")); // FIFO fallback
    }

    void testAcceptPredicate_rejectsWrongScreen()
    {
        WindowPlacementStore store;
        store.record(make(QStringLiteral("app|1"), QStringLiteral("app"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-2")));
        // Reopen on DP-1: a screen-matching predicate rejects the DP-2 record.
        auto none = store.take(QStringLiteral("app|new"), QStringLiteral("app"), [](const WindowPlacement& p) {
            return p.screenId == QStringLiteral("DP-1");
        });
        QVERIFY(!none.has_value());
        QCOMPARE(store.size(), 1); // left intact for when DP-2 is back
    }

    void testClearAndRemoveIf()
    {
        WindowPlacementStore store;
        store.record(make(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-1")));
        store.record(make(QStringLiteral("b|1"), QStringLiteral("b"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-2")));
        store.clear(QStringLiteral("a|1"));
        QVERIFY(!store.contains(QStringLiteral("a|1"), QStringLiteral("a")));
        QCOMPARE(store.size(), 1);

        const int removed = store.removeIf([](const WindowPlacement& p) {
            return p.screenId == QStringLiteral("DP-2");
        });
        QCOMPARE(removed, 1);
        QCOMPARE(store.size(), 0);
    }

    void testSerializeRoundTrip()
    {
        WindowPlacementStore store;
        WindowPlacement p = make(QStringLiteral("firefox|u"), QStringLiteral("firefox"),
                                 WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId());
        p.virtualDesktop = 3;
        p.activity = QStringLiteral("act-1");
        p.kind = PhosphorEngine::WindowKind::Normal;
        // A shared free geometry alongside the snapped slot (the float-back).
        p.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(11, 22, 333, 444));
        store.record(p);

        const QJsonObject json = store.serialize();
        QVERIFY(json.contains(QStringLiteral("firefox")));

        WindowPlacementStore reloaded;
        reloaded.deserialize(json);
        auto got = reloaded.take(QStringLiteral("firefox|u"), QStringLiteral("firefox"));
        QVERIFY(got.has_value());
        const EngineSlot snapSlot = got->slotFor(WindowPlacement::snapEngineId());
        QCOMPARE(snapSlot.state, QString(WindowPlacement::stateSnapped()));
        QCOMPARE(snapSlot.zoneIds.size(), 1);
        QCOMPARE(got->virtualDesktop, 3);
        QCOMPARE(got->activity, QStringLiteral("act-1"));
        QCOMPARE(got->kind, PhosphorEngine::WindowKind::Normal); // survives clampWindowKindFromWire
        QCOMPARE(got->freeGeometryFor(QStringLiteral("DP-1")), QRect(11, 22, 333, 444));
    }

    void testSerializeHonorsKeepPredicate()
    {
        WindowPlacementStore store;
        store.record(make(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-1")));
        store.record(make(QStringLiteral("b|1"), QStringLiteral("b"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-2")));
        // keep only DP-1 (mirrors the disabled-context gate).
        const QJsonObject json = store.serialize([](const WindowPlacement& p) {
            return p.screenId == QStringLiteral("DP-1");
        });
        QVERIFY(json.contains(QStringLiteral("a")));
        QVERIFY(!json.contains(QStringLiteral("b")));
    }

    void testPeek_appIdFallbackReturnsNewestBySequence()
    {
        // peek() with a non-matching uuid falls back to the appId bucket and must
        // return the NEWEST (highest sequence) record, not an arbitrary one.
        WindowPlacementStore store;
        store.record(make(QStringLiteral("term|old"), QStringLiteral("term"), WindowPlacement::stateTiled(),
                          WindowPlacement::autotileEngineId()));
        store.record(make(QStringLiteral("term|new"), QStringLiteral("term"), WindowPlacement::stateFloating(),
                          WindowPlacement::autotileEngineId()));

        auto p = store.peek(QStringLiteral("term|unknown"), QStringLiteral("term"));
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("term|new")); // newest by sequence
        QCOMPARE(store.size(), 2); // peek is non-consuming
    }

    void testTake_acceptPredicatePassesOnFifoCandidate()
    {
        // The accepting (not just rejecting) path: an appId-FIFO candidate that
        // satisfies the predicate is selected and consumed.
        WindowPlacementStore store;
        store.record(make(QStringLiteral("app|1"), QStringLiteral("app"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-2")));
        auto p = store.take(QStringLiteral("app|new"), QStringLiteral("app"), [](const WindowPlacement& r) {
            return r.screenId == QStringLiteral("DP-2");
        });
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("app|1"));
        QCOMPARE(store.size(), 0); // consumed
    }

    void testTake_preferredOutranksOlderAcceptedSibling()
    {
        // Regression: cross-screen snapped restore at login. A window snapped on
        // screen DP-1 reopens (new uuid) on screen DP-2 (KWin placed the session
        // window on a different output). The appId bucket holds an OLDER contentless
        // `free` record (empty screen) plus the NEWER `snapped` record on DP-1.
        // Without the `preferred` ranking, plain FIFO consumes the older free record
        // first and the window never returns to its zone. With it, the snapped
        // record wins even though it is newer AND on a different screen.
        WindowPlacementStore store;
        store.record(make(QStringLiteral("ghastty|old"), QStringLiteral("ghastty"), WindowPlacement::stateFree(),
                          WindowPlacement::snapEngineId(), QString())); // empty screen, older
        store.record(make(QStringLiteral("ghastty|snapped"), QStringLiteral("ghastty"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId(),
                          QStringLiteral("DP-1"))); // newer, snapped on DP-1

        const auto accept = [](const WindowPlacement& p) {
            if (p.slotFor(WindowPlacement::snapEngineId()).state == WindowPlacement::stateSnapped()) {
                return true; // snapped: eligible cross-screen
            }
            return p.screenId.isEmpty() || p.screenId == QStringLiteral("DP-2");
        };
        const auto preferred = [](const WindowPlacement& p) {
            return p.slotFor(WindowPlacement::snapEngineId()).state == WindowPlacement::stateSnapped();
        };

        auto p = store.take(QStringLiteral("ghastty|new"), QStringLiteral("ghastty"), accept, preferred);
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("ghastty|snapped"));
        QCOMPARE(p->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateSnapped()));
        QCOMPARE(p->screenId, QStringLiteral("DP-1")); // restores to its recorded screen, not the opening DP-2

        // The older free record remains for a later free/floating restore.
        QCOMPARE(store.size(), 1);
    }

    void testTake_preferredFallsBackToAcceptedWhenNoPreferredMatch()
    {
        // When no record satisfies `preferred`, the oldest merely-accepted record is
        // still consumed — `preferred` only re-ranks, it never filters.
        WindowPlacementStore store;
        store.record(make(QStringLiteral("app|1"), QStringLiteral("app"), WindowPlacement::stateFree(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-2")));
        const auto accept = [](const WindowPlacement& p) {
            return p.screenId.isEmpty() || p.screenId == QStringLiteral("DP-2");
        };
        const auto preferred = [](const WindowPlacement& p) {
            return p.slotFor(WindowPlacement::snapEngineId()).state == WindowPlacement::stateSnapped();
        };
        auto p = store.take(QStringLiteral("app|new"), QStringLiteral("app"), accept, preferred);
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("app|1"));
        QCOMPARE(store.size(), 0);
    }

    void testHasRestorableContent()
    {
        // Valid free/float geometry → content.
        QVERIFY(make(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateFree(),
                     WindowPlacement::snapEngineId())
                    .hasRestorableContent());
        QVERIFY(make(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateFloating(),
                     WindowPlacement::snapEngineId())
                    .hasRestorableContent());
        // A managed slot (snapped / tiled) → content even with no geometry.
        QVERIFY(make(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateSnapped(),
                     WindowPlacement::snapEngineId())
                    .hasRestorableContent());
        QVERIFY(make(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateTiled(),
                     WindowPlacement::autotileEngineId())
                    .hasRestorableContent());

        // A bare {free, no geometry, no zones} record — the residue a window leaves
        // when captured frame-less — has nothing to restore.
        WindowPlacement bare;
        bare.windowId = QStringLiteral("a|1");
        bare.appId = QStringLiteral("a");
        EngineSlot freeSlot;
        freeSlot.state = WindowPlacement::stateFree();
        bare.engines.insert(WindowPlacement::snapEngineId(), freeSlot);
        QVERIFY(!bare.hasRestorableContent());

        // CRITICAL (two-state model): snapping now defaults every unmanaged window to
        // `floating`, so a bare {floating, no geometry, no zones} record is the new
        // residue and MUST also be rejected — otherwise geometry-less floated records
        // flood the per-app FIFO and re-trigger the residue-eviction bug.
        WindowPlacement bareFloating;
        bareFloating.windowId = QStringLiteral("b|1");
        bareFloating.appId = QStringLiteral("b");
        EngineSlot bareFloatSlot;
        bareFloatSlot.state = WindowPlacement::stateFloating();
        bareFloating.engines.insert(WindowPlacement::snapEngineId(), bareFloatSlot);
        QVERIFY(!bareFloating.hasRestorableContent());

        // A floated-from-snap window keeping its pre-float zones (no geometry yet) is
        // still restorable — the zones let it resnap.
        WindowPlacement floatWithZones = bare;
        EngineSlot floatSlot;
        floatSlot.state = WindowPlacement::stateFloating();
        floatSlot.zoneIds = QStringList{QStringLiteral("z1")};
        floatWithZones.engines.insert(WindowPlacement::snapEngineId(), floatSlot);
        QVERIFY(floatWithZones.hasRestorableContent());

        // A free snap slot alongside a tiled autotile slot is restorable (autotile).
        WindowPlacement freeButTiled = bare;
        EngineSlot tiled;
        tiled.state = WindowPlacement::stateTiled();
        tiled.order = 2;
        freeButTiled.engines.insert(WindowPlacement::autotileEngineId(), tiled);
        QVERIFY(freeButTiled.hasRestorableContent());
    }

    void testTake_contentlessResidueRejectedSoRealPlacementWins()
    {
        // Regression (floating geometry restore on login): a window the user floated
        // and resized is captured LAST at logout, so its content-bearing `floating`
        // record (with freeGeometry) sits at the BACK of the appId FIFO, behind OLDER
        // contentless residue records left by earlier-closed windows of the same app.
        // The snap engine's restore ACCEPT predicate rejects contentless residue
        // (hasRestorableContent() == false), so the FIFO never consumes it ahead of
        // the real placement — the saved position is the one taken on reopen, and the
        // residue is left untouched (later drained by MaxPerApp eviction / save-time
        // hasRestorableContent filter).
        WindowPlacementStore store;
        // Older contentless free record (empty screen, no geometry) — residue.
        WindowPlacement residue;
        residue.windowId = QStringLiteral("kate|old");
        residue.appId = QStringLiteral("kate");
        EngineSlot freeSlot;
        freeSlot.state = WindowPlacement::stateFree();
        residue.engines.insert(WindowPlacement::snapEngineId(), freeSlot);
        store.record(residue);
        // Newer floating record carrying the resized geometry.
        store.record(make(QStringLiteral("kate|floated"), QStringLiteral("kate"), WindowPlacement::stateFloating(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-1")));

        // Mirror the engine's predicates: snapped records are eligible cross-screen;
        // contentless residue is rejected; other content is accepted (restore-unsnapped
        // on). The preference is snapped-on-snapping only (the stronger intent).
        const auto accept = [](const WindowPlacement& p) {
            if (p.slotFor(WindowPlacement::snapEngineId()).state == WindowPlacement::stateSnapped()) {
                return true;
            }
            return p.hasRestorableContent();
        };
        const auto preferred = [](const WindowPlacement& p) {
            return p.slotFor(WindowPlacement::snapEngineId()).state == WindowPlacement::stateSnapped();
        };

        auto p = store.take(QStringLiteral("kate|new"), QStringLiteral("kate"), accept, preferred);
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("kate|floated")); // the record with geometry, not the residue
        QVERIFY(p->anyFreeGeometry().isValid());
        QCOMPARE(store.size(), 1); // residue remains, rejected (not consumed)
    }

    void testTake_snappedSiblingPreferredOverOlderFloatingContent()
    {
        // The preferred ranking must keep a SNAPPED placement ahead of an OLDER
        // content-bearing floating sibling of the same app (snapping is the stronger
        // restore intent). Guards against a regression where broadening `preferred`
        // to "any free geometry" let the older floating record win by age — and, in
        // the cross-mode case, let a snap-screen open consume a record it could not use.
        WindowPlacementStore store;
        store.record(make(QStringLiteral("kate|floated"), QStringLiteral("kate"), WindowPlacement::stateFloating(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-1"))); // older, content (geometry)
        store.record(make(QStringLiteral("kate|snapped"), QStringLiteral("kate"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-1"))); // newer, snapped

        const auto accept = [](const WindowPlacement& p) {
            if (p.slotFor(WindowPlacement::snapEngineId()).state == WindowPlacement::stateSnapped()) {
                return true;
            }
            return p.hasRestorableContent();
        };
        const auto preferred = [](const WindowPlacement& p) {
            return p.slotFor(WindowPlacement::snapEngineId()).state == WindowPlacement::stateSnapped();
        };

        auto p = store.take(QStringLiteral("kate|new"), QStringLiteral("kate"), accept, preferred);
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("kate|snapped")); // snapped wins despite being newer
        QCOMPARE(store.size(), 1); // the older floating record remains for the next instance
    }

    void testSerializeDropsContentlessRecords()
    {
        // The save keep-predicate (hasRestorableContent) must keep records that carry
        // something to restore and drop bare {free, no geometry} residue, so it never
        // reaches disk to crowd the next session's FIFO.
        WindowPlacementStore store;
        store.record(make(QStringLiteral("good|1"), QStringLiteral("good"), WindowPlacement::stateFloating(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-1")));
        WindowPlacement residue;
        residue.windowId = QStringLiteral("noise|1");
        residue.appId = QStringLiteral("noise");
        EngineSlot freeSlot;
        freeSlot.state = WindowPlacement::stateFree();
        residue.engines.insert(WindowPlacement::snapEngineId(), freeSlot);
        store.record(residue);

        const QJsonObject json = store.serialize([](const WindowPlacement& p) {
            return p.hasRestorableContent();
        });
        QVERIFY(json.contains(QStringLiteral("good")));
        QVERIFY(!json.contains(QStringLiteral("noise")));
    }

    void testRecord_evictsOldestBeyondMaxPerApp()
    {
        // The per-app cap drops the OLDEST record when exceeded. 17 instances of one
        // app → the first (term|0) is evicted; term|1..16 survive.
        WindowPlacementStore store;
        for (int i = 0; i < 17; ++i) {
            store.record(make(QStringLiteral("term|%1").arg(i), QStringLiteral("term"), WindowPlacement::stateSnapped(),
                              WindowPlacement::snapEngineId()));
        }
        QCOMPARE(store.size(), 16); // capped at MaxPerApp
        QVERIFY(!store.contains(QStringLiteral("term|0"))); // oldest evicted
        QVERIFY(store.contains(QStringLiteral("term|16"))); // newest kept
    }

    void testRecord_appIdRenameMovesRecord()
    {
        // Re-recording the SAME windowId under a NEW appId drops the stale entry from
        // the old bucket and appends to the new one (mid-session class rename).
        WindowPlacementStore store;
        store.record(make(QStringLiteral("x|1"), QStringLiteral("oldapp"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId()));
        store.record(make(QStringLiteral("x|1"), QStringLiteral("newapp"), WindowPlacement::stateFloating(),
                          WindowPlacement::snapEngineId()));

        QCOMPARE(store.size(), 1); // not duplicated across buckets
        QCOMPARE(store.records().size(), 1); // exactly one record total (old bucket erased)
        // The old appId bucket is gone (empty appId arg = bucket-only check).
        QVERIFY(!store.contains(QString(), QStringLiteral("oldapp")));
        auto p = store.take(QStringLiteral("x|1"), QStringLiteral("newapp"));
        QVERIFY(p.has_value());
        QCOMPARE(p->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateFloating()));
    }

    void testRecord_appIdRenameWithOtherBucketsIntact()
    {
        // Rename path with MULTIPLE buckets present: dropping the renamed-from bucket
        // must not corrupt iteration or lose unrelated records (guards the record()
        // erase-then-continue path). Several other apps surround the renamed window so
        // the emptied bucket is not necessarily the last in hash order.
        WindowPlacementStore store;
        for (int i = 0; i < 5; ++i) {
            store.record(make(QStringLiteral("keep%1|u").arg(i), QStringLiteral("keep%1").arg(i),
                              WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        }
        store.record(make(QStringLiteral("w|1"), QStringLiteral("renamefrom"), WindowPlacement::stateTiled(),
                          WindowPlacement::autotileEngineId()));
        QCOMPARE(store.size(), 6);

        store.record(make(QStringLiteral("w|1"), QStringLiteral("renameto"), WindowPlacement::stateFloating(),
                          WindowPlacement::autotileEngineId()));

        QCOMPARE(store.size(), 6); // 5 keepers + the moved record (no duplication)
        QVERIFY(!store.contains(QString(), QStringLiteral("renamefrom"))); // old bucket erased
        QVERIFY(store.contains(QStringLiteral("w|1"), QStringLiteral("renameto")));
        for (int i = 0; i < 5; ++i) {
            QVERIFY(store.contains(QStringLiteral("keep%1|u").arg(i))); // unrelated records intact
        }
    }

    void testDeserialize_skipsCorruptKeysAndStructurelessWindowIds()
    {
        // A bucket key containing '|' (corrupt identity) and a record whose windowId
        // has no `appId|uuid` separator (structureless/forged) must both be dropped.
        // A windowId whose embedded class differs from the bucket appId is NOT dropped
        // (legitimate registry appId drift, e.g. Electron re-broadcasting WM_CLASS).
        WindowPlacement good = make(QStringLiteral("vesktop|u"), QStringLiteral("vesktop"),
                                    WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId());
        WindowPlacement drifted = make(QStringLiteral("oldclass|u2"), QStringLiteral("vesktop"),
                                       WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId());
        WindowPlacement structureless = make(QStringLiteral("nosep"), QStringLiteral("vesktop"),
                                             WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId());
        QJsonObject root;
        root[QStringLiteral("vesktop")] = QJsonArray{good.toJson(), drifted.toJson(), structureless.toJson()};
        root[QStringLiteral("corrupt|key")] = QJsonArray{good.toJson()};

        WindowPlacementStore store;
        store.deserialize(root);
        QCOMPARE(store.size(), 2); // good + drifted survive; structureless + corrupt-key dropped
        QVERIFY(store.contains(QStringLiteral("vesktop|u")));
        QVERIFY(store.contains(QStringLiteral("oldclass|u2"))); // drift preserved
        QVERIFY(!store.contains(QStringLiteral("nosep")));
    }

    void testTiledRecord_roundTripPreservesOrder()
    {
        // The engine-agnostic claim: an autotile slot (its own state + order) round-
        // trips through serialize/deserialize unchanged.
        WindowPlacementStore store;
        WindowPlacement p = make(QStringLiteral("ghostty|u"), QStringLiteral("ghostty"), WindowPlacement::stateTiled(),
                                 WindowPlacement::autotileEngineId());
        EngineSlot slot = p.slotFor(WindowPlacement::autotileEngineId());
        slot.order = 3;
        p.engines.insert(WindowPlacement::autotileEngineId(), slot);
        store.record(p);

        WindowPlacementStore reloaded;
        reloaded.deserialize(store.serialize());
        auto got = reloaded.take(QStringLiteral("ghostty|u"), QStringLiteral("ghostty"));
        QVERIFY(got.has_value());
        const EngineSlot gotSlot = got->slotFor(WindowPlacement::autotileEngineId());
        QCOMPARE(gotSlot.state, QString(WindowPlacement::stateTiled()));
        QCOMPARE(gotSlot.order, 3);
    }
};

QTEST_MAIN(TestWindowPlacementStore)
#include "test_window_placement_store.moc"
