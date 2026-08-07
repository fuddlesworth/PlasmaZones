// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include <QJsonArray>
#include <QSet>

#include <PhosphorIdentity/WindowId.h>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include "helpers/WindowPlacementBuilders.h"

using PhosphorEngine::EngineSlot;
using PhosphorEngine::WindowPlacement;
using PhosphorEngine::WindowPlacementStore;
using PlasmaZones::TestHelpers::makePlacement;

class TestWindowPlacementStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testRecordAndTake_exact()
    {
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("firefox|a"), QStringLiteral("firefox"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
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
        store.record(makePlacement(id, QStringLiteral("settings"), WindowPlacement::stateFloating(),
                                   WindowPlacement::snapEngineId()));
        store.record(makePlacement(id, QStringLiteral("settings"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId()));

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
        store.record(makePlacement(id, QStringLiteral("settings"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId()));
        store.record(makePlacement(id, QStringLiteral("settings"), WindowPlacement::stateFloating(),
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
        store.record(makePlacement(id, QStringLiteral("zed"), WindowPlacement::stateFloating(),
                                   WindowPlacement::autotileEngineId(), QStringLiteral("DP-1")));
        // Snap snaps it (a managed slot — no geometry of its own).
        store.record(makePlacement(id, QStringLiteral("zed"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-1")));

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
        QVERIFY(store.record(makePlacement(id, QStringLiteral("term"), WindowPlacement::stateSnapped(),
                                           WindowPlacement::snapEngineId())));
        // Identical content → no change.
        QVERIFY(!store.record(makePlacement(id, QStringLiteral("term"), WindowPlacement::stateSnapped(),
                                            WindowPlacement::snapEngineId())));
        // Different state → changed.
        QVERIFY(store.record(makePlacement(id, QStringLiteral("term"), WindowPlacement::stateFloating(),
                                           WindowPlacement::snapEngineId())));
    }

    void testUuidExactBeforeAppIdFifo()
    {
        WindowPlacementStore store;
        // Two instances of the same app, different uuids/states.
        store.record(makePlacement(QStringLiteral("term|1"), QStringLiteral("term"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId()));
        store.record(makePlacement(QStringLiteral("term|2"), QStringLiteral("term"), WindowPlacement::stateFloating(),
                                   WindowPlacement::snapEngineId()));

        // Exact uuid match takes its own record, not the FIFO head.
        auto p2 = store.take(QStringLiteral("term|2"), QStringLiteral("term"));
        QVERIFY(p2.has_value());
        QCOMPARE(p2->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateFloating()));

        // No exact match for the unknown uuid → the appId fallback hands out the
        // sole remaining record (term|1 — with one record left, ordering is not
        // exercised here, only the fallback itself).
        auto p1 = store.take(QStringLiteral("term|unknown-uuid"), QStringLiteral("term"));
        QVERIFY(p1.has_value());
        QCOMPARE(p1->windowId, QStringLiteral("term|1"));
    }

    void testAcceptPredicate_rejectsWrongScreen()
    {
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("app|1"), QStringLiteral("app"), WindowPlacement::stateSnapped(),
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
        store.record(makePlacement(QStringLiteral("a|a-instance"), QStringLiteral("a"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-1")));
        store.record(makePlacement(QStringLiteral("b|b-instance"), QStringLiteral("b"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-2")));
        store.clear(QStringLiteral("a|a-instance"));
        QVERIFY(!store.contains(QStringLiteral("a|a-instance"), QStringLiteral("a")));
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
        WindowPlacement p = makePlacement(QStringLiteral("firefox|u"), QStringLiteral("firefox"),
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
        store.record(makePlacement(QStringLiteral("a|a-instance"), QStringLiteral("a"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-1")));
        store.record(makePlacement(QStringLiteral("b|b-instance"), QStringLiteral("b"), WindowPlacement::stateSnapped(),
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
        store.record(makePlacement(QStringLiteral("term|old"), QStringLiteral("term"), WindowPlacement::stateTiled(),
                                   WindowPlacement::autotileEngineId()));
        store.record(makePlacement(QStringLiteral("term|new"), QStringLiteral("term"), WindowPlacement::stateFloating(),
                                   WindowPlacement::autotileEngineId()));

        auto p = store.peek(QStringLiteral("term|unknown"), QStringLiteral("term"));
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("term|new")); // newest by sequence
        QCOMPARE(store.size(), 2); // peek is non-consuming
    }

    void testPeekExact_neverFallsBackToAppIdSibling()
    {
        // peekExact is the per-window read for LIVE windows: the exact-uuid
        // branch only, never the appId-FIFO fallback — a same-app sibling's
        // record must not answer for a record-less window (the sibling zone /
        // float-state bleed the live-state readers were exposed to).
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("term|sibling"), QStringLiteral("term"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));

        QVERIFY(store.peek(QStringLiteral("term|other"), QStringLiteral("term")).has_value()); // fallback would bleed
        QVERIFY(!store.peekExact(QStringLiteral("term|other")).has_value()); // exact-only refuses
        const auto own = store.peekExact(QStringLiteral("term|sibling"));
        QVERIFY(own.has_value());
        QCOMPARE(own->windowId, QStringLiteral("term|sibling"));
        QCOMPARE(store.size(), 1); // non-consuming
    }

    void testTake_acceptPredicatePassesOnFifoCandidate()
    {
        // The accepting (not just rejecting) path: an appId-FIFO candidate that
        // satisfies the predicate is selected and consumed.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("app|1"), QStringLiteral("app"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-2")));
        auto p = store.take(QStringLiteral("app|new"), QStringLiteral("app"), [](const WindowPlacement& r) {
            return r.screenId == QStringLiteral("DP-2");
        });
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("app|1"));
        QCOMPARE(store.size(), 0); // consumed
    }

    void testTakeForReopen_fifoMatchRebindsToLiveUuid()
    {
        // The shared reopen resolve: a close/reopen (fresh uuid) consumes the
        // appId-FIFO record AND re-records it bound to the live uuid, so the
        // engine slots + free geometry survive the reopen instead of being
        // consumed away.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("app|old"), QStringLiteral("app"), WindowPlacement::stateFloating(),
                                   WindowPlacement::scrollingEngineId()));

        auto p = store.takeForReopen(WindowPlacement::scrollingEngineId(), QStringLiteral("app|new"),
                                     QStringLiteral("app"), QStringLiteral("DP-1"));
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("app|new")); // already re-bound
        QCOMPARE(store.size(), 1); // re-recorded, not consumed away

        // The re-recorded copy answers an exact lookup under the LIVE uuid.
        auto rebound = store.peekExact(QStringLiteral("app|new"));
        QVERIFY(rebound.has_value());
        QCOMPARE(rebound->slotFor(WindowPlacement::scrollingEngineId()).state,
                 QString(WindowPlacement::stateFloating()));
        QVERIFY(!store.peekExact(QStringLiteral("app|old")).has_value());
    }

    void testTakeForReopen_rejectedExactRecordIsFinal()
    {
        // A LIVE window whose own record carries this engine's slot but fails
        // the shared accept (tiled in another context) must NOT fall through
        // to a sibling's FIFO record: consuming it would re-bind the sibling's
        // placement under this window's id, where the store's merge overwrites
        // the window's own other-context slot.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("app|live"), QStringLiteral("app"), WindowPlacement::stateTiled(),
                                   WindowPlacement::autotileEngineId(), QStringLiteral("DP-2")));
        store.record(makePlacement(QStringLiteral("app|sibling"), QStringLiteral("app"), WindowPlacement::stateTiled(),
                                   WindowPlacement::autotileEngineId(), QStringLiteral("DP-1")));

        // Asking for DP-1 rejects app|live's own tiled-on-DP-2 record.
        auto p = store.takeForReopen(WindowPlacement::autotileEngineId(), QStringLiteral("app|live"),
                                     QStringLiteral("app"), QStringLiteral("DP-1"));
        QVERIFY(!p.has_value());
        QCOMPARE(store.size(), 2); // sibling untouched
        QVERIFY(store.peekExact(QStringLiteral("app|sibling")).has_value());
    }

    void testTakeForReopen_slotlessExactStubDoesNotVetoFifo()
    {
        // The octopi float-reopen regression: every fresh open writes a
        // geometry-only, engine-slot-less record under the LIVE uuid (the
        // pre-tile free-geometry capture) BEFORE the engine's restore runs. A
        // record that says nothing about the asking engine is not a verdict —
        // the exact-final gate must fall through to the appId FIFO, where the
        // window's real floating record waits.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("octopi|old"), QStringLiteral("octopi"),
                                   WindowPlacement::stateFloating(), WindowPlacement::scrollingEngineId(),
                                   QStringLiteral("DP-1"), QRect(100, 100, 400, 300)));
        WindowPlacement stub;
        stub.windowId = QStringLiteral("octopi|new");
        stub.appId = QStringLiteral("octopi");
        stub.screenId = QStringLiteral("DP-1");
        stub.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(0, 0, 500, 500)); // spawn frame
        store.record(stub);
        QCOMPARE(store.size(), 2);

        auto p = store.takeForReopen(WindowPlacement::scrollingEngineId(), QStringLiteral("octopi|new"),
                                     QStringLiteral("octopi"), QStringLiteral("DP-1"));
        QVERIFY(p.has_value());
        QCOMPARE(p->slotFor(WindowPlacement::scrollingEngineId()).state, QString(WindowPlacement::stateFloating()));

        // Re-bound and merged into the live window's record: the remembered
        // float-back wins over the stub's spawn frame.
        const auto rebound = store.peekExact(QStringLiteral("octopi|new"));
        QVERIFY(rebound.has_value());
        QCOMPARE(rebound->slotFor(WindowPlacement::scrollingEngineId()).state,
                 QString(WindowPlacement::stateFloating()));
        QCOMPARE(rebound->freeGeometryFor(QStringLiteral("DP-1")), QRect(100, 100, 400, 300));
        QCOMPARE(store.size(), 1); // FIFO record consumed into the live record
    }

    void testTakeForReopen_newestFirstWithoutStealingLiveRebound()
    {
        // The appId fallback consumes the NEWEST record ("the most recent
        // placement is current truth", peek()'s rule) — and the live-instance
        // probe is what keeps a SECOND instance of the same app from stealing
        // the record just re-bound to the first: multi-instance distribution
        // holds via liveness, not via oldest-first.
        WindowPlacementStore store;
        QSet<QString> liveInstances;
        store.setLiveInstanceProbe([&liveInstances](const QString& windowId) {
            return liveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        });
        store.record(makePlacement(QStringLiteral("app|a"), QStringLiteral("app"), WindowPlacement::stateFloating(),
                                   WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"), QRect(0, 0, 10, 10)));
        store.record(makePlacement(QStringLiteral("app|b"), QStringLiteral("app"), WindowPlacement::stateFloating(),
                                   WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"),
                                   QRect(50, 50, 10, 10)));

        liveInstances.insert(QStringLiteral("n1"));
        auto first = store.takeForReopen(WindowPlacement::scrollingEngineId(), QStringLiteral("app|n1"),
                                         QStringLiteral("app"), QStringLiteral("DP-1"));
        QVERIFY(first.has_value());
        QCOMPARE(first->freeGeometryFor(QStringLiteral("DP-1")), QRect(50, 50, 10, 10)); // newest first

        liveInstances.insert(QStringLiteral("n2"));
        auto second = store.takeForReopen(WindowPlacement::scrollingEngineId(), QStringLiteral("app|n2"),
                                          QStringLiteral("app"), QStringLiteral("DP-1"));
        QVERIFY(second.has_value());
        QCOMPARE(second->freeGeometryFor(QStringLiteral("DP-1")),
                 QRect(0, 0, 10, 10)); // NOT the record now bound to live n1
        QCOMPARE(store.size(), 2);
    }

    void testTakeForReopen_newestRecordOutranksStaleGraveyard()
    {
        // The octopi regression's second act: years of gate-vetoed reopens
        // left a graveyard of stale TILED records ahead of the fresh FLOATING
        // close record. Oldest-first consumed the graveyard head (re-tiling
        // the window at a months-old column); newest-first must return the
        // last close's floating record.
        WindowPlacementStore store;
        for (int i = 0; i < 5; ++i) {
            auto stale = makePlacement(QStringLiteral("octopi|stale%1").arg(i), QStringLiteral("octopi"),
                                       WindowPlacement::stateTiled(), WindowPlacement::scrollingEngineId(),
                                       QStringLiteral("DP-1"));
            stale.engines[QString(WindowPlacement::scrollingEngineId())].order = i;
            store.record(stale);
        }
        store.record(makePlacement(QStringLiteral("octopi|fresh"), QStringLiteral("octopi"),
                                   WindowPlacement::stateFloating(), WindowPlacement::scrollingEngineId(),
                                   QStringLiteral("DP-1"), QRect(870, 811, 1626, 813)));

        auto p = store.takeForReopen(WindowPlacement::scrollingEngineId(), QStringLiteral("octopi|new"),
                                     QStringLiteral("octopi"), QStringLiteral("DP-1"));
        QVERIFY(p.has_value());
        QCOMPARE(p->slotFor(WindowPlacement::scrollingEngineId()).state, QString(WindowPlacement::stateFloating()));
        QCOMPARE(p->freeGeometryFor(QStringLiteral("DP-1")), QRect(870, 811, 1626, 813));
        // The consumed record is the FLOATING one, not a graveyard entry: a
        // floating slot never carries an order, while every stale record does.
        QCOMPARE(p->slotFor(WindowPlacement::scrollingEngineId()).order, -1);
        // The graveyard itself is untouched — 5 stale records plus the
        // re-bound live one.
        QCOMPARE(store.size(), 6);
        for (int i = 0; i < 5; ++i) {
            QVERIFY(store.peekExact(QStringLiteral("octopi|stale%1").arg(i)).has_value());
        }
    }

    void testTakeForReopen_tiledRecordIsNeverConsumed()
    {
        // FLOATING slots only: a TILED record in the matching context is not
        // consumed — it stays in the store as the exact-final evidence that
        // the window closed tiled (order restore was removed deliberately).
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("app|old"), QStringLiteral("app"), WindowPlacement::stateTiled(),
                                   WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"), QRect(), 2));

        auto p = store.takeForReopen(WindowPlacement::scrollingEngineId(), QStringLiteral("app|new"),
                                     QStringLiteral("app"), QStringLiteral("DP-1"));
        QVERIFY(!p.has_value());
        QCOMPARE(store.size(), 1); // untouched
        QVERIFY(store.peekExact(QStringLiteral("app|old")).has_value());
    }

    void testTakeForReopen_newestFirstSurvivesSerializeRoundTrip()
    {
        // The login shape of the graveyard scenario goes through
        // serialize/deserialize first, where bucket POSITION order and
        // SEQUENCE order deliberately diverge — the newest-first pick must
        // key on the reloaded sequences, not on position.
        WindowPlacementStore store;
        for (int i = 0; i < 3; ++i) {
            store.record(makePlacement(QStringLiteral("octopi|stale%1").arg(i), QStringLiteral("octopi"),
                                       WindowPlacement::stateTiled(), WindowPlacement::scrollingEngineId(),
                                       QStringLiteral("DP-1"), QRect(), i));
        }
        store.record(makePlacement(QStringLiteral("octopi|fresh"), QStringLiteral("octopi"),
                                   WindowPlacement::stateFloating(), WindowPlacement::scrollingEngineId(),
                                   QStringLiteral("DP-1"), QRect(870, 811, 1626, 813)));

        WindowPlacementStore reloaded;
        reloaded.deserialize(store.serialize());
        auto p = reloaded.takeForReopen(WindowPlacement::scrollingEngineId(), QStringLiteral("octopi|new"),
                                        QStringLiteral("octopi"), QStringLiteral("DP-1"));
        QVERIFY(p.has_value());
        QCOMPARE(p->slotFor(WindowPlacement::scrollingEngineId()).state, QString(WindowPlacement::stateFloating()));
    }

    void testDeserialize_seqlessLegacyBucketConsumesLastInArray()
    {
        // A legacy or hand-edited bucket whose records carry no "seq" key
        // deserializes every record with sequence 0; the stable replay then
        // decides which one counts as newest. Pin the current deterministic
        // outcome (last-in-array wins the newest-first pick) so a quiet
        // std::sort swap cannot silently randomise login restores.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("app|a"), QStringLiteral("app"), WindowPlacement::stateFloating(),
                                   WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"), QRect(0, 0, 10, 10)));
        store.record(makePlacement(QStringLiteral("app|b"), QStringLiteral("app"), WindowPlacement::stateFloating(),
                                   WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"),
                                   QRect(50, 50, 10, 10)));
        QJsonObject serialized = store.serialize();
        QJsonObject stripped;
        for (auto it = serialized.constBegin(); it != serialized.constEnd(); ++it) {
            QJsonArray arr;
            const QJsonArray in = it.value().toArray();
            for (const QJsonValue& v : in) {
                QJsonObject rec = v.toObject();
                rec.remove(QStringLiteral("seq"));
                arr.append(rec);
            }
            stripped.insert(it.key(), arr);
        }
        WindowPlacementStore reloaded;
        reloaded.deserialize(stripped);
        auto p = reloaded.takeForReopen(WindowPlacement::scrollingEngineId(), QStringLiteral("app|new"),
                                        QStringLiteral("app"), QStringLiteral("DP-1"));
        QVERIFY(p.has_value());
        QCOMPARE(p->freeGeometryFor(QStringLiteral("DP-1")), QRect(50, 50, 10, 10)); // last in array
    }

    void testTake_preferredOutranksOlderAcceptedSibling()
    {
        // Regression: cross-screen snapped restore at login. A window snapped on
        // screen DP-1 reopens (new uuid) on screen DP-2 (KWin placed the session
        // window on a different output). The appId bucket holds an OLDER `free`
        // record on an empty screen plus the NEWER `snapped` record on DP-1.
        // Without the `preferred` ranking, plain FIFO consumes the older free record
        // first and the window never returns to its zone. With it, the snapped
        // record wins even though it is newer AND on a different screen.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("ghastty|old"), QStringLiteral("ghastty"),
                                   WindowPlacement::stateFree(), WindowPlacement::snapEngineId(),
                                   QString())); // empty screen, older
        store.record(makePlacement(QStringLiteral("ghastty|snapped"), QStringLiteral("ghastty"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId(),
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
        // When no record satisfies `preferred`, the OLDEST merely-accepted
        // record is still consumed — `preferred` only re-ranks, it never
        // filters. Two records, so the ordering half of that claim is
        // actually exercised rather than asserted into a single-record store.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("app|1"), QStringLiteral("app"), WindowPlacement::stateFree(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-2")));
        store.record(makePlacement(QStringLiteral("app|2"), QStringLiteral("app"), WindowPlacement::stateFree(),
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
        QCOMPARE(store.size(), 1);
    }

    void testHasRestorableContent()
    {
        // Valid free/float geometry → content.
        QVERIFY(makePlacement(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateFree(),
                              WindowPlacement::snapEngineId())
                    .hasRestorableContent());
        QVERIFY(makePlacement(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateFloating(),
                              WindowPlacement::snapEngineId())
                    .hasRestorableContent());
        // A managed slot (snapped / tiled) → content even with no geometry.
        QVERIFY(makePlacement(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateSnapped(),
                              WindowPlacement::snapEngineId())
                    .hasRestorableContent());
        QVERIFY(makePlacement(QStringLiteral("a|1"), QStringLiteral("a"), WindowPlacement::stateTiled(),
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
        store.record(makePlacement(QStringLiteral("kate|floated"), QStringLiteral("kate"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("DP-1")));

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
        store.record(makePlacement(QStringLiteral("kate|floated"), QStringLiteral("kate"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("DP-1"))); // older, content (geometry)
        store.record(makePlacement(QStringLiteral("kate|snapped"), QStringLiteral("kate"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("DP-1"))); // newer, snapped

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
        store.record(makePlacement(QStringLiteral("good|good-instance"), QStringLiteral("good"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("DP-1")));
        WindowPlacement residue;
        residue.windowId = QStringLiteral("noise|noise-instance");
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
        // The per-app cap drops the OLDEST record when everything is
        // restorable. MaxPerApp + 1 instances of one app → the first is
        // evicted; the rest survive. Derived from the constant so a retune
        // cannot silently turn this into a non-boundary test.
        WindowPlacementStore store;
        for (int i = 0; i < WindowPlacementStore::MaxPerApp + 1; ++i) {
            store.record(makePlacement(QStringLiteral("term|%1").arg(i), QStringLiteral("term"),
                                       WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        }
        QCOMPARE(store.size(), WindowPlacementStore::MaxPerApp);
        QVERIFY(!store.contains(QStringLiteral("term|0"))); // oldest evicted
        QVERIFY(store.contains(QStringLiteral("term|%1").arg(WindowPlacementStore::MaxPerApp))); // newest kept
    }

    void testRecord_evictionPrefersContentlessResidueOverRestorable()
    {
        // The residue tier is the documented starvation guard: a contentless
        // record (bare free slot, no geometry, no zones) must be the victim
        // even when it is NOT the positional head, and the positionally
        // oldest RESTORABLE record must survive. A mutation to plain
        // remove-first passes the sibling test above but fails here.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("term|keep0"), QStringLiteral("term"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        store.record(makePlacement(QStringLiteral("term|keep1"), QStringLiteral("term"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        // Contentless residue at index 2 (geometry-less free slot).
        store.record(makePlacement(QStringLiteral("term|residue"), QStringLiteral("term"), WindowPlacement::stateFree(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-1"), QRect()));
        // Fill to EXACTLY MaxPerApp (2 keeps + 1 residue + the fills) so the
        // eviction fires once, on the overflow record below.
        for (int i = 3; i < WindowPlacementStore::MaxPerApp; ++i) {
            store.record(makePlacement(QStringLiteral("term|fill%1").arg(i), QStringLiteral("term"),
                                       WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        }
        QCOMPARE(store.size(), WindowPlacementStore::MaxPerApp);

        store.record(makePlacement(QStringLiteral("term|overflow"), QStringLiteral("term"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        QCOMPARE(store.size(), WindowPlacementStore::MaxPerApp);
        QVERIFY(!store.peekExact(QStringLiteral("term|residue")).has_value()); // residue evicted
        QVERIFY(store.peekExact(QStringLiteral("term|keep0")).has_value()); // positional head survives
        QVERIFY(store.peekExact(QStringLiteral("term|overflow")).has_value());
    }

    void testRecord_evictionSparesLiveWindowsRecord()
    {
        // The middle tier: with no contentless residue, the oldest record NOT
        // bound to a still-open window is evicted — deleting a LIVE window's
        // record leaves that window recordless, the same harm the reopen
        // fallback's live probe guards against.
        WindowPlacementStore store;
        QSet<QString> liveInstances{QStringLiteral("live")};
        store.setLiveInstanceProbe([&liveInstances](const QString& windowId) {
            return liveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        });
        store.record(makePlacement(QStringLiteral("term|live"), QStringLiteral("term"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId()));
        for (int i = 1; i < WindowPlacementStore::MaxPerApp; ++i) {
            store.record(makePlacement(QStringLiteral("term|dead%1").arg(i), QStringLiteral("term"),
                                       WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        }
        store.record(makePlacement(QStringLiteral("term|overflow"), QStringLiteral("term"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        QCOMPARE(store.size(), WindowPlacementStore::MaxPerApp);
        QVERIFY(store.peekExact(QStringLiteral("term|live")).has_value()); // live head spared
        QVERIFY(!store.peekExact(QStringLiteral("term|dead1")).has_value()); // oldest non-live evicted
    }

    void testRecord_appIdRenameMovesRecord()
    {
        // Re-recording the SAME live instance under a NEW appId and composite
        // prefix drops the stale entry from the old bucket and moves it to the
        // new one (mid-session class rename).
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("oldapp|1"), QStringLiteral("oldapp"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        store.record(makePlacement(QStringLiteral("newapp|1"), QStringLiteral("newapp"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId()));

        QCOMPARE(store.size(), 1); // not duplicated across buckets
        QCOMPARE(store.records().size(), 1); // exactly one record total (old bucket erased)
        // The old appId bucket is gone (empty windowId arg + appId = "does the
        // bucket hold ANY record" check).
        QVERIFY(!store.contains(QString(), QStringLiteral("oldapp")));
        auto p = store.take(QStringLiteral("newapp|1"), QStringLiteral("newapp"));
        QVERIFY(p.has_value());
        QCOMPARE(p->windowId, QStringLiteral("newapp|1"));
        QCOMPARE(p->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateFloating()));
    }

    void testTake_prefixMutationUsesOwnInstanceBeforeSiblingFifo()
    {
        // A reopened window whose appId prefix mutated (oldclass → newclass) must
        // still take ITS OWN prior record via the instance suffix, not fall back
        // to FIFO and steal a sibling's record from the new-prefix bucket.
        WindowPlacementStore store;
        store.record(makePlacement(QStringLiteral("oldclass|own-instance"), QStringLiteral("oldclass"),
                                   WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId()));
        store.record(makePlacement(QStringLiteral("newclass|sibling"), QStringLiteral("newclass"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId()));

        const auto own = store.take(QStringLiteral("newclass|own-instance"), QStringLiteral("newclass"));
        QVERIFY(own.has_value());
        QCOMPARE(own->windowId, QStringLiteral("oldclass|own-instance"));
        QCOMPARE(own->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateSnapped()));

        const auto sibling = store.take(QStringLiteral("newclass|new-instance"), QStringLiteral("newclass"));
        QVERIFY(sibling.has_value());
        QCOMPARE(sibling->windowId, QStringLiteral("newclass|sibling"));
    }

    void testRecord_appIdRenameWithOtherBucketsIntact()
    {
        // Rename path with MULTIPLE buckets present: dropping the renamed-from bucket
        // must not corrupt iteration or lose unrelated records (guards the record()
        // erase-then-continue path). Several other apps surround the renamed window so
        // the emptied bucket is not necessarily the last in hash order.
        WindowPlacementStore store;
        for (int i = 0; i < 5; ++i) {
            store.record(makePlacement(QStringLiteral("keep%1|keep-instance-%1").arg(i),
                                       QStringLiteral("keep%1").arg(i), WindowPlacement::stateSnapped(),
                                       WindowPlacement::snapEngineId()));
        }
        store.record(makePlacement(QStringLiteral("renamefrom|1"), QStringLiteral("renamefrom"),
                                   WindowPlacement::stateTiled(), WindowPlacement::autotileEngineId()));
        QCOMPARE(store.size(), 6);

        store.record(makePlacement(QStringLiteral("renameto|1"), QStringLiteral("renameto"),
                                   WindowPlacement::stateFloating(), WindowPlacement::autotileEngineId()));

        QCOMPARE(store.size(), 6); // 5 keepers + the moved record (no duplication)
        // Empty windowId arg = bucket-level emptiness check: old bucket erased.
        QVERIFY(!store.contains(QString(), QStringLiteral("renamefrom")));
        // peekExact + appId compare, not the two-arg contains (which passes
        // on ANY record in the bucket and so cannot pin the central claim:
        // that THIS record moved into the renamed bucket).
        const auto moved = store.peekExact(QStringLiteral("renameto|1"));
        QVERIFY(moved.has_value());
        QCOMPARE(moved->appId, QStringLiteral("renameto"));
        for (int i = 0; i < 5; ++i) {
            QVERIFY(store.contains(QStringLiteral("keep%1|keep-instance-%1").arg(i))); // unrelated records intact
        }
    }

    void testDeserialize_skipsCorruptKeysAndStructurelessWindowIds()
    {
        // A bucket key containing '|' (corrupt identity) and a record whose windowId
        // has no `appId|uuid` separator (structureless/forged) must both be dropped.
        // A windowId whose embedded class differs from the bucket appId is NOT dropped
        // (legitimate registry appId drift, e.g. Electron re-broadcasting WM_CLASS).
        WindowPlacement good = makePlacement(QStringLiteral("vesktop|u"), QStringLiteral("vesktop"),
                                             WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId());
        WindowPlacement drifted = makePlacement(QStringLiteral("oldclass|u2"), QStringLiteral("vesktop"),
                                                WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId());
        WindowPlacement structureless = makePlacement(QStringLiteral("nosep"), QStringLiteral("vesktop"),
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

    void testDeserialize_mergesDuplicateInstanceAcrossPrefixes()
    {
        // Two persisted records for the SAME live instance under different appId
        // prefixes must merge into one on reload, with the higher-sequence record
        // winning identity AND any conflicting engine slot (replay is oldest to
        // newest, so the newer record's slots land last).
        WindowPlacement older = makePlacement(QStringLiteral("oldclass|same-instance"), QStringLiteral("oldclass"),
                                              WindowPlacement::stateSnapped(), WindowPlacement::snapEngineId());
        older.sequence = 4;
        WindowPlacement newer = makePlacement(QStringLiteral("newclass|same-instance"), QStringLiteral("newclass"),
                                              WindowPlacement::stateTiled(), WindowPlacement::autotileEngineId());
        newer.sequence = 9;
        // Conflicting snap slot on the newer record: sequence precedence must be
        // observable as an overwrite, not just a union of disjoint engines.
        EngineSlot newerSnap;
        newerSnap.state = WindowPlacement::stateFloating();
        newer.engines.insert(WindowPlacement::snapEngineId(), newerSnap);

        QJsonObject root;
        root[QStringLiteral("oldclass")] = QJsonArray{older.toJson()};
        root[QStringLiteral("newclass")] = QJsonArray{newer.toJson()};

        WindowPlacementStore store;
        store.deserialize(root);

        QCOMPARE(store.size(), 1);
        // Empty windowId arg = bucket-level emptiness check.
        QVERIFY(!store.contains(QString(), QStringLiteral("oldclass")));
        const auto merged = store.peekExact(QStringLiteral("newclass|same-instance"));
        QVERIFY(merged.has_value());
        QCOMPARE(merged->windowId, QStringLiteral("newclass|same-instance"));
        QCOMPARE(merged->appId, QStringLiteral("newclass"));
        // Sequence 9 wins the contested snap slot (Floating over the older
        // record's Snapped); the uncontested autotile slot is unioned in.
        QCOMPARE(merged->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateFloating()));
        QCOMPARE(merged->slotFor(WindowPlacement::autotileEngineId()).state, QString(WindowPlacement::stateTiled()));
    }

    void testTiledRecord_roundTripPreservesOrder()
    {
        // The engine-agnostic claim: an autotile slot (its own state + order) round-
        // trips through serialize/deserialize unchanged.
        WindowPlacementStore store;
        WindowPlacement p = makePlacement(QStringLiteral("ghostty|u"), QStringLiteral("ghostty"),
                                          WindowPlacement::stateTiled(), WindowPlacement::autotileEngineId());
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

    void testDeserialize_overCapBucketIsCappedByReplay()
    {
        // The per-app cap moved out of deserialize into record()'s replay
        // path; a persisted bucket exceeding MaxPerApp (hand-edited file, or
        // a cap lowered between versions) must still come back capped.
        WindowPlacementStore store;
        for (int i = 0; i < WindowPlacementStore::MaxPerApp; ++i) {
            store.record(makePlacement(QStringLiteral("dolphin|u%1").arg(i), QStringLiteral("dolphin"),
                                       WindowPlacement::stateFloating(), WindowPlacement::snapEngineId()));
        }
        QJsonObject serialized = store.serialize();
        // Splice extra entries into the persisted bucket beyond the cap, each
        // with a DISTINCT windowId: same-id copies would take record()'s
        // sameWindowInstance merge branch on replay and never append, so the
        // cap (evictForCapacity on the replay path) would pass vacuously.
        QJsonArray bucket = serialized.value(QStringLiteral("dolphin")).toArray();
        const QJsonObject younger = bucket.last().toObject();
        for (int i = 0; i < 4; ++i) {
            QJsonObject extra = younger;
            extra.insert(QStringLiteral("windowId"), QStringLiteral("dolphin|extra%1").arg(i));
            bucket.append(extra);
        }
        serialized.insert(QStringLiteral("dolphin"), bucket);

        WindowPlacementStore reloaded;
        reloaded.deserialize(serialized);
        QCOMPARE(reloaded.size(), WindowPlacementStore::MaxPerApp);
    }

    void testRecord_renameIntoFullBucket_evictsAndAppendsNewest()
    {
        // A mid-session appId rename moves the record into the new bucket;
        // when that bucket is already at capacity the OLDEST entry is evicted
        // and the moved record lands NEWEST in the FIFO.
        WindowPlacementStore store;
        for (int i = 0; i < WindowPlacementStore::MaxPerApp; ++i) {
            store.record(makePlacement(QStringLiteral("newapp|f%1").arg(i), QStringLiteral("newapp"),
                                       WindowPlacement::stateFloating(), WindowPlacement::snapEngineId()));
        }
        store.record(makePlacement(QStringLiteral("oldapp|renamer"), QStringLiteral("oldapp"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId()));

        // Rename: same instance re-recorded under the full bucket's appId.
        QVERIFY(store.record(makePlacement(QStringLiteral("newapp|renamer"), QStringLiteral("newapp"),
                                           WindowPlacement::stateFloating(), WindowPlacement::snapEngineId())));

        QCOMPARE(store.size(), WindowPlacementStore::MaxPerApp);
        QVERIFY(!store.contains(QStringLiteral("newapp|f0"))); // oldest evicted
        QVERIFY(store.contains(QStringLiteral("newapp|renamer")));
        // FIFO position: the moved record is NEWEST, so the appId-FIFO
        // fallback hands out a surviving older sibling first.
        const auto oldest = store.take(QStringLiteral("newapp|fresh-uuid"), QStringLiteral("newapp"));
        QVERIFY(oldest.has_value());
        QCOMPARE(oldest->windowId, QStringLiteral("newapp|f1"));
    }

    void testDeserialize_preservesAppIdFifoOrder()
    {
        // The per-app bucket's POSITIONAL order is load-bearing for take()'s
        // direct consumers (the snap paths consume oldest-first) and for the
        // eviction's last-resort tier; the tiling reopen path distributes by
        // sequence + live probe instead. The positional ordering must survive
        // a serialize → deserialize round trip — a bucket rebuilt in a
        // different order silently rotates which reopened instance receives
        // which snap placement.
        WindowPlacementStore store;
        const QRect r1(10, 10, 300, 200);
        const QRect r2(20, 20, 310, 210);
        const QRect r3(30, 30, 320, 220);
        store.record(makePlacement(QStringLiteral("dolphin|u1"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1"), r1));
        store.record(makePlacement(QStringLiteral("dolphin|u2"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S2"), r2));
        store.record(makePlacement(QStringLiteral("dolphin|u3"), QStringLiteral("dolphin"),
                                   WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                   QStringLiteral("S1"), r3));

        WindowPlacementStore reloaded;
        reloaded.deserialize(store.serialize());

        // Consume via the appId FIFO with fresh (unmatched) uuids so only the
        // bucket order decides: oldest first, in the original record order.
        const auto first = reloaded.take(QStringLiteral("dolphin|new1"), QStringLiteral("dolphin"));
        QVERIFY(first.has_value());
        QCOMPARE(first->windowId, QStringLiteral("dolphin|u1"));
        const auto second = reloaded.take(QStringLiteral("dolphin|new2"), QStringLiteral("dolphin"));
        QVERIFY(second.has_value());
        QCOMPARE(second->windowId, QStringLiteral("dolphin|u2"));
        const auto third = reloaded.take(QStringLiteral("dolphin|new3"), QStringLiteral("dolphin"));
        QVERIFY(third.has_value());
        QCOMPARE(third->windowId, QStringLiteral("dolphin|u3"));
    }

    void testPendingCrossScreenSnapRestore_requiresDifferentScreen()
    {
        // The defer/claim reciprocity predicate is CROSS-SCREEN only. A
        // snapped record whose recorded screen equals the opening screen
        // must never defer, even when the record's own (desktop, activity)
        // context still resolves to snapping mode — the regression was a
        // window recorded snapped on desktop 1 opening into the same
        // screen's desktop-2 scrolling context: scroll deferred, snap's
        // reciprocal gate also stood down, and the window stranded
        // unmanaged at its stale zone rect on top of the strip.
        const auto alwaysSnapping = [](const QString&, int, const QString&) {
            return true;
        };
        WindowPlacement p =
            makePlacement(QStringLiteral("firefox|a"), QStringLiteral("firefox"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-1"));
        p.virtualDesktop = 1;

        // Same screen: never a cross-screen restore, whatever the resolver says.
        QVERIFY(!PhosphorEngine::pendingCrossScreenSnapRestore(p, QStringLiteral("DP-1"), alwaysSnapping));
        // Unscreened record: nothing to restore across, same verdict.
        WindowPlacement unscreened = p;
        unscreened.screenId.clear();
        QVERIFY(!PhosphorEngine::pendingCrossScreenSnapRestore(unscreened, QStringLiteral("DP-1"), alwaysSnapping));
        // Genuinely different screen: defers when the recorded context is
        // snapping, claims nothing when it is not.
        QVERIFY(PhosphorEngine::pendingCrossScreenSnapRestore(p, QStringLiteral("DP-2"), alwaysSnapping));
        QVERIFY(!PhosphorEngine::pendingCrossScreenSnapRestore(p, QStringLiteral("DP-2"),
                                                               [](const QString&, int, const QString&) {
                                                                   return false;
                                                               }));
        // The resolver runs against the RECORD's context, not the opener's.
        bool sawRecordContext = false;
        PhosphorEngine::pendingCrossScreenSnapRestore(
            p, QStringLiteral("DP-2"), [&](const QString& screen, int desktop, const QString& activity) {
                sawRecordContext = (screen == QStringLiteral("DP-1")) && desktop == 1 && activity.isEmpty();
                return true;
            });
        QVERIFY(sawRecordContext);
        // A non-snapped snap slot never defers.
        WindowPlacement floating =
            makePlacement(QStringLiteral("firefox|b"), QStringLiteral("firefox"), WindowPlacement::stateFloating(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-1"));
        QVERIFY(!PhosphorEngine::pendingCrossScreenSnapRestore(floating, QStringLiteral("DP-2"), alwaysSnapping));
    }

    void testPendingCrossScreenManagedRestore_perEngineSlotAndState()
    {
        // The generalized N-way predicate: each engine's verdict keys on ITS
        // OWN slot in ITS OWN managed state. A scrolling-tiled record is a
        // scrolling claim, never a snap or autotile one, and a floating slot
        // never earns a cross-screen pull for any engine (float restore is
        // screen-local by doctrine).
        const auto always = [](const QString&, int, const QString&) {
            return true;
        };
        WindowPlacement p =
            makePlacement(QStringLiteral("kitty|a"), QStringLiteral("kitty"), WindowPlacement::stateTiled(),
                          WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"));

        // Scrolling-tiled on DP-1, opening on DP-2: the scrolling claim fires.
        QVERIFY(PhosphorEngine::pendingCrossScreenManagedRestore(
            p, WindowPlacement::scrollingEngineId(), WindowPlacement::stateTiled(), QStringLiteral("DP-2"), always));
        // Same record read through the OTHER engines' keys: no claim.
        QVERIFY(!PhosphorEngine::pendingCrossScreenManagedRestore(
            p, WindowPlacement::autotileEngineId(), WindowPlacement::stateTiled(), QStringLiteral("DP-2"), always));
        QVERIFY(!PhosphorEngine::pendingCrossScreenManagedRestore(
            p, WindowPlacement::snapEngineId(), WindowPlacement::stateSnapped(), QStringLiteral("DP-2"), always));
        // Same screen: never cross-screen, whatever the resolver says.
        QVERIFY(!PhosphorEngine::pendingCrossScreenManagedRestore(
            p, WindowPlacement::scrollingEngineId(), WindowPlacement::stateTiled(), QStringLiteral("DP-1"), always));
        // Recorded context no longer in the engine's mode: no claim.
        QVERIFY(!PhosphorEngine::pendingCrossScreenManagedRestore(p, WindowPlacement::scrollingEngineId(),
                                                                  WindowPlacement::stateTiled(), QStringLiteral("DP-2"),
                                                                  [](const QString&, int, const QString&) {
                                                                      return false;
                                                                  }));
        // A floating slot does not satisfy the TILED managed-state key. (The
        // stronger doctrine — a float never earns a cross-screen pull — is a
        // property of the CALLERS, which never pass stateFloating() as the
        // managed state, not of this function.)
        WindowPlacement floatRec =
            makePlacement(QStringLiteral("kitty|b"), QStringLiteral("kitty"), WindowPlacement::stateFloating(),
                          WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"));
        QVERIFY(!PhosphorEngine::pendingCrossScreenManagedRestore(floatRec, WindowPlacement::scrollingEngineId(),
                                                                  WindowPlacement::stateTiled(), QStringLiteral("DP-2"),
                                                                  always));
        // The generalized form forwards the RECORD's context, not the
        // opener's — the whole N-way agreement rests on it, and the snap
        // specialization only inherits the property transitively.
        WindowPlacement ctxRec =
            makePlacement(QStringLiteral("kitty|d"), QStringLiteral("kitty"), WindowPlacement::stateTiled(),
                          WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"));
        ctxRec.virtualDesktop = 4;
        ctxRec.activity = QStringLiteral("act-a");
        bool sawRecordCtx = false;
        PhosphorEngine::pendingCrossScreenManagedRestore(
            ctxRec, WindowPlacement::scrollingEngineId(), WindowPlacement::stateTiled(), QStringLiteral("DP-2"),
            [&](const QString& screen, int desktop, const QString& activity) {
                sawRecordCtx = screen == QStringLiteral("DP-1") && desktop == 4 && activity == QStringLiteral("act-a");
                return true;
            });
        QVERIFY(sawRecordCtx);
        // The snap specialization is the same predicate keyed on (snap,
        // snapped) — compared on BOTH sides, since agreeing only where both
        // are true would also hold for a specialization keyed on the wrong
        // state.
        WindowPlacement snapRec =
            makePlacement(QStringLiteral("kitty|c"), QStringLiteral("kitty"), WindowPlacement::stateSnapped(),
                          WindowPlacement::snapEngineId(), QStringLiteral("DP-1"));
        QCOMPARE(PhosphorEngine::pendingCrossScreenSnapRestore(snapRec, QStringLiteral("DP-2"), always),
                 PhosphorEngine::pendingCrossScreenManagedRestore(snapRec, WindowPlacement::snapEngineId(),
                                                                  WindowPlacement::stateSnapped(),
                                                                  QStringLiteral("DP-2"), always));
        QCOMPARE(PhosphorEngine::pendingCrossScreenSnapRestore(snapRec, QStringLiteral("DP-1"), always),
                 PhosphorEngine::pendingCrossScreenManagedRestore(snapRec, WindowPlacement::snapEngineId(),
                                                                  WindowPlacement::stateSnapped(),
                                                                  QStringLiteral("DP-1"), always));
        QCOMPARE(PhosphorEngine::pendingCrossScreenSnapRestore(floatRec, QStringLiteral("DP-2"), always),
                 PhosphorEngine::pendingCrossScreenManagedRestore(floatRec, WindowPlacement::snapEngineId(),
                                                                  WindowPlacement::stateSnapped(),
                                                                  QStringLiteral("DP-2"), always));
    }

    void testRecordContextMatchesLive_honoursSentinels()
    {
        // The claim's grant-vs-insert context guard. Concrete contexts must
        // agree; the sticky/unknown sentinels (desktop 0, empty activity)
        // are compatible with anything — the same desktop-agnostic reading
        // that granted the mode verdict in the first place. Comparing them
        // literally would refuse every sticky and never-captured-desktop
        // record, which is most of what a login restore carries.
        WindowPlacement p =
            makePlacement(QStringLiteral("kitty|a"), QStringLiteral("kitty"), WindowPlacement::stateTiled(),
                          WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"));
        p.virtualDesktop = 0;
        p.activity.clear();
        QVERIFY(PhosphorEngine::recordContextMatchesLive(p, 3, QStringLiteral("act-a")));

        p.virtualDesktop = 3;
        QVERIFY(PhosphorEngine::recordContextMatchesLive(p, 3, QStringLiteral("act-a")));
        QVERIFY(!PhosphorEngine::recordContextMatchesLive(p, 1, QStringLiteral("act-a")));
        // A live context that is itself unknown cannot refuse.
        QVERIFY(PhosphorEngine::recordContextMatchesLive(p, 0, QString()));

        p.activity = QStringLiteral("act-a");
        QVERIFY(PhosphorEngine::recordContextMatchesLive(p, 3, QStringLiteral("act-a")));
        QVERIFY(!PhosphorEngine::recordContextMatchesLive(p, 3, QStringLiteral("act-b")));
    }

    void testPeekForReclaim_excludesLiveSiblingsButKeepsOwnRecord()
    {
        // The claim's lookup. peek() has no live-instance exclusion by
        // design (float-back reads legitimately consult a live sibling), so
        // the reclaim needs its own: a record bound to a still-OPEN sibling
        // describes a different window and must never justify a
        // cross-screen pull. The window's OWN record always wins, live or
        // not — that is its history.
        WindowPlacementStore store;
        QSet<QString> live;
        store.setLiveInstanceProbe([&live](const QString& windowId) {
            return live.contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        });

        WindowPlacement sibling =
            makePlacement(QStringLiteral("term|open"), QStringLiteral("term"), WindowPlacement::stateTiled(),
                          WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"));
        QVERIFY(store.record(sibling));
        live.insert(QStringLiteral("open"));

        // A fresh instance: the live sibling's record is not evidence.
        QVERIFY2(!store.peekForReclaim(QStringLiteral("term|fresh"), QStringLiteral("term")).has_value(),
                 "a live sibling's record must be excluded from a reclaim lookup");
        // Plain peek still returns it — the exclusion is reclaim-specific.
        QVERIFY(store.peek(QStringLiteral("term|fresh"), QStringLiteral("term")).has_value());

        // The window's own record wins even while the probe calls it live.
        const auto own = store.peekForReclaim(QStringLiteral("term|open"), QStringLiteral("term"));
        QVERIFY(own.has_value());
        QCOMPARE(own->windowId, QStringLiteral("term|open"));

        // Once the sibling closes, its record is available again.
        live.clear();
        QVERIFY(store.peekForReclaim(QStringLiteral("term|fresh"), QStringLiteral("term")).has_value());
        // An empty appId has no bucket, so no reclaim verdict is possible.
        QVERIFY(!store.peekForReclaim(QStringLiteral("term|fresh"), QString()).has_value());
    }

    void testReleaseEngineSlot_downgradesOnlyThatEnginesSlot()
    {
        // The handoff-release primitive. Slots are otherwise only merged,
        // never cleared, so a slot left behind by an engine that gave the
        // window up reads as a false home to the cross-screen reclaim.
        WindowPlacementStore store;
        WindowPlacement rec =
            makePlacement(QStringLiteral("term|a"), QStringLiteral("term"), WindowPlacement::stateTiled(),
                          WindowPlacement::scrollingEngineId(), QStringLiteral("DP-1"));
        PhosphorEngine::EngineSlot snapSlot;
        snapSlot.state = QString(WindowPlacement::stateSnapped());
        snapSlot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(WindowPlacement::snapEngineId(), snapSlot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(10, 10, 400, 300));
        QVERIFY(store.record(rec));

        QVERIFY(store.releaseEngineSlot(QStringLiteral("term|a"), WindowPlacement::scrollingEngineId()));
        const auto after = store.peekExact(QStringLiteral("term|a"));
        QVERIFY(after.has_value());
        // DOWNGRADED, not removed: the slot must still be PRESENT so
        // takeForReopen's exact-final gate keeps recognising this instance as
        // one the engine has seen (removing it made a released window
        // indistinguishable from a fresh one, which then consumed a sibling's
        // floating record), while no longer being MANAGED so the cross-screen
        // reclaim cannot read it as a home.
        const PhosphorEngine::EngineSlot released = after->slotFor(WindowPlacement::scrollingEngineId());
        QVERIFY2(!released.isEmpty(), "the slot must remain present for the exact-final gate");
        QCOMPARE(released.state, QString(WindowPlacement::stateReleased()));
        QVERIFY(released.zoneIds.isEmpty());
        QCOMPARE(released.order, -1);
        QVERIFY2(released.state != QString(WindowPlacement::stateTiled()),
                 "a released slot must not satisfy the reclaim's managed-state key");
        // Everything else untouched.
        QCOMPARE(after->slotFor(WindowPlacement::snapEngineId()).state, QString(WindowPlacement::stateSnapped()));
        QCOMPARE(after->freeGeometryFor(QStringLiteral("DP-1")), QRect(10, 10, 400, 300));
        QCOMPARE(after->screenId, QStringLiteral("DP-1"));

        // Idempotent, and a no-op for an unknown window.
        QVERIFY(!store.releaseEngineSlot(QStringLiteral("term|a"), WindowPlacement::scrollingEngineId()));
        QVERIFY(!store.releaseEngineSlot(QStringLiteral("nope|x"), WindowPlacement::snapEngineId()));

        // A released slot no longer earns a cross-screen pull.
        const auto always = [](const QString&, int, const QString&) {
            return true;
        };
        QVERIFY(!PhosphorEngine::pendingCrossScreenManagedRestore(*after, WindowPlacement::scrollingEngineId(),
                                                                  WindowPlacement::stateTiled(), QStringLiteral("DP-2"),
                                                                  always));
    }
};

QTEST_MAIN(TestWindowPlacementStore)
#include "test_window_placement_store.moc"
