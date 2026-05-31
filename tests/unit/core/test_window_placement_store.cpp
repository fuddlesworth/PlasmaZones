// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include <QJsonArray>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>

using PhosphorEngine::WindowPlacement;
using PhosphorEngine::WindowPlacementStore;

namespace {
WindowPlacement make(const QString& windowId, const QString& appId, const QString& state, const QString& engine,
                     const QString& screen = QStringLiteral("DP-1"))
{
    WindowPlacement p;
    p.windowId = windowId;
    p.appId = appId;
    p.stateId = state;
    p.engineId = engine;
    p.screenId = screen;
    p.geometry = QRect(10, 20, 300, 400);
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
                          QStringLiteral("snap")));
        QCOMPARE(store.size(), 1);

        auto p = store.take(QStringLiteral("firefox|a"), QStringLiteral("firefox"));
        QVERIFY(p.has_value());
        QCOMPARE(p->stateId, QString(WindowPlacement::stateSnapped()));
        QCOMPARE(store.size(), 0); // consumed
    }

    void testMutualExclusivity_recordReplacesByWindowId()
    {
        // The core invariant: re-recording the SAME windowId overwrites its prior
        // record. A floated→snapped window must not keep its stale float record
        // (the "snapped window floats on login" bug).
        WindowPlacementStore store;
        const QString id = QStringLiteral("settings|x");
        store.record(make(id, QStringLiteral("settings"), WindowPlacement::stateFloated(), QStringLiteral("snap")));
        store.record(make(id, QStringLiteral("settings"), WindowPlacement::stateSnapped(), QStringLiteral("snap")));

        QCOMPARE(store.size(), 1); // not accumulated
        auto p = store.take(id, QStringLiteral("settings"));
        QVERIFY(p.has_value());
        QCOMPARE(p->stateId, QString(WindowPlacement::stateSnapped())); // latest wins
        QVERIFY(!store.contains(id, QStringLiteral("settings")));
    }

    void testUuidExactBeforeAppIdFifo()
    {
        WindowPlacementStore store;
        // Two instances of the same app, different uuids/states.
        store.record(make(QStringLiteral("term|1"), QStringLiteral("term"), WindowPlacement::stateSnapped(),
                          QStringLiteral("snap")));
        store.record(make(QStringLiteral("term|2"), QStringLiteral("term"), WindowPlacement::stateFloated(),
                          QStringLiteral("snap")));

        // Exact uuid match takes its own record, not the FIFO head.
        auto p2 = store.take(QStringLiteral("term|2"), QStringLiteral("term"));
        QVERIFY(p2.has_value());
        QCOMPARE(p2->stateId, QString(WindowPlacement::stateFloated()));

        // Remaining is term|1.
        auto p1 = store.take(QStringLiteral("term|unknown-uuid"), QStringLiteral("term"));
        QVERIFY(p1.has_value());
        QCOMPARE(p1->windowId, QStringLiteral("term|1")); // FIFO fallback
    }

    void testAcceptPredicate_rejectsWrongScreen()
    {
        WindowPlacementStore store;
        store.record(make(QStringLiteral("app|1"), QStringLiteral("app"), WindowPlacement::stateSnapped(),
                          QStringLiteral("snap"), QStringLiteral("DP-2")));
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
                          QStringLiteral("snap"), QStringLiteral("DP-1")));
        store.record(make(QStringLiteral("b|1"), QStringLiteral("b"), WindowPlacement::stateSnapped(),
                          QStringLiteral("snap"), QStringLiteral("DP-2")));
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
                                 WindowPlacement::stateSnapped(), QStringLiteral("snap"));
        p.virtualDesktop = 3;
        p.activity = QStringLiteral("act-1");
        p.kind = PhosphorEngine::WindowKind::Normal;
        p.engineData = QJsonObject{{QStringLiteral("zoneIds"), QJsonArray{QStringLiteral("z1")}}};
        store.record(p);

        const QJsonObject json = store.serialize();
        QVERIFY(json.contains(QStringLiteral("firefox")));

        WindowPlacementStore reloaded;
        reloaded.deserialize(json);
        auto got = reloaded.take(QStringLiteral("firefox|u"), QStringLiteral("firefox"));
        QVERIFY(got.has_value());
        QCOMPARE(got->stateId, QString(WindowPlacement::stateSnapped()));
        QCOMPARE(got->engineId, QStringLiteral("snap"));
        QCOMPARE(got->virtualDesktop, 3);
        QCOMPARE(got->activity, QStringLiteral("act-1"));
        QCOMPARE(got->kind, PhosphorEngine::WindowKind::Normal); // survives clampWindowKindFromWire
        QCOMPARE(got->geometry, QRect(10, 20, 300, 400));
        QCOMPARE(got->engineData.value(QStringLiteral("zoneIds")).toArray().size(), 1);
    }

    void testSerializeHonorsKeepPredicate()
    {
        WindowPlacementStore store;
        store.record(make(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateSnapped(),
                          QStringLiteral("snap"), QStringLiteral("DP-1")));
        store.record(make(QStringLiteral("b|1"), QStringLiteral("b"), WindowPlacement::stateSnapped(),
                          QStringLiteral("snap"), QStringLiteral("DP-2")));
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
        store.record(make(QStringLiteral("term|old"), QStringLiteral("term"), WindowPlacement::stateAutotiled(),
                          QStringLiteral("autotile")));
        store.record(make(QStringLiteral("term|new"), QStringLiteral("term"), WindowPlacement::stateFloated(),
                          QStringLiteral("autotile")));

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
                          QStringLiteral("snap"), QStringLiteral("DP-2")));
        auto p = store.take(QStringLiteral("app|new"), QStringLiteral("app"), [](const WindowPlacement& r) {
            return r.screenId == QStringLiteral("DP-2");
        });
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("app|1"));
        QCOMPARE(store.size(), 0); // consumed
    }

    void testRecord_evictsOldestBeyondMaxPerApp()
    {
        // The per-app cap drops the OLDEST record when exceeded. 17 instances of one
        // app → the first (term|0) is evicted; term|1..16 survive.
        WindowPlacementStore store;
        for (int i = 0; i < 17; ++i) {
            store.record(make(QStringLiteral("term|%1").arg(i), QStringLiteral("term"), WindowPlacement::stateSnapped(),
                              QStringLiteral("snap")));
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
                          QStringLiteral("snap")));
        store.record(make(QStringLiteral("x|1"), QStringLiteral("newapp"), WindowPlacement::stateFloated(),
                          QStringLiteral("snap")));

        QCOMPARE(store.size(), 1); // not duplicated across buckets
        QCOMPARE(store.records().size(), 1); // exactly one record total (old bucket erased)
        // The old appId bucket is gone (empty appId arg = bucket-only check).
        QVERIFY(!store.contains(QString(), QStringLiteral("oldapp")));
        auto p = store.take(QStringLiteral("x|1"), QStringLiteral("newapp"));
        QVERIFY(p.has_value());
        QCOMPARE(p->stateId, QString(WindowPlacement::stateFloated()));
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
                              WindowPlacement::stateSnapped(), QStringLiteral("snap")));
        }
        store.record(make(QStringLiteral("w|1"), QStringLiteral("renamefrom"), WindowPlacement::stateAutotiled(),
                          QStringLiteral("autotile")));
        QCOMPARE(store.size(), 6);

        store.record(make(QStringLiteral("w|1"), QStringLiteral("renameto"), WindowPlacement::stateFloated(),
                          QStringLiteral("autotile")));

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
                                    WindowPlacement::stateSnapped(), QStringLiteral("snap"));
        WindowPlacement drifted = make(QStringLiteral("oldclass|u2"), QStringLiteral("vesktop"),
                                       WindowPlacement::stateSnapped(), QStringLiteral("snap"));
        WindowPlacement structureless = make(QStringLiteral("nosep"), QStringLiteral("vesktop"),
                                             WindowPlacement::stateSnapped(), QStringLiteral("snap"));
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

    void testAutotiledRecord_roundTripPreservesPosition()
    {
        // The engine-agnostic claim: an autotile record (its own engineId + stateId +
        // position payload) round-trips through serialize/deserialize unchanged.
        WindowPlacementStore store;
        WindowPlacement p = make(QStringLiteral("ghostty|u"), QStringLiteral("ghostty"),
                                 WindowPlacement::stateAutotiled(), QStringLiteral("autotile"));
        p.engineData = QJsonObject{{QStringLiteral("position"), 3}};
        store.record(p);

        WindowPlacementStore reloaded;
        reloaded.deserialize(store.serialize());
        auto got = reloaded.take(QStringLiteral("ghostty|u"), QStringLiteral("ghostty"));
        QVERIFY(got.has_value());
        QCOMPARE(got->engineId, QStringLiteral("autotile"));
        QCOMPARE(got->stateId, QString(WindowPlacement::stateAutotiled()));
        QCOMPARE(got->engineData.value(QStringLiteral("position")).toInt(), 3);
    }
};

QTEST_MAIN(TestWindowPlacementStore)
#include "test_window_placement_store.moc"
