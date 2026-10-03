// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include <QSet>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include "helpers/WindowPlacementBuilders.h"

using PhosphorEngine::WindowPlacement;
using PhosphorEngine::WindowPlacementStore;
using PlasmaZones::TestHelpers::liveInstanceProbe;
using PlasmaZones::TestHelpers::makePlacement;

/**
 * @brief The store's live-sibling contract on the snap open path (#1106): a
 *        record bound to a still-open window is never consumed by a same-app
 *        sibling's open, and is the record such a sibling inherits its free
 *        size from. Plus the claim bookkeeping the audit of that change found
 *        wanting: a claim dies with its instance on both close funnels and
 *        with its record on every removal path, survives a rewrite of the
 *        record's id string, and never names the asker's own slot-less stub.
 *
 * Every live set is declared BEFORE the store it is probed from: the probe
 * captures it by reference (WindowPlacementBuilders.h).
 */
class TestWindowPlacementStoreSiblings : public QObject
{
    Q_OBJECT

    static WindowPlacement snappedApp(const QString& windowId, const QRect& freeRect = QRect(0, 0, 10, 10))
    {
        auto p = makePlacement(windowId, QStringLiteral("app"), WindowPlacement::stateSnapped(),
                               WindowPlacement::snapEngineId(), QStringLiteral("DP-1"));
        // makePlacement files geometry for free/floating states only; a
        // snapped record's float-back is the rect it had before its snap.
        p.freeGeometryByScreen.insert(QStringLiteral("DP-1"), freeRect);
        return p;
    }

    static WindowPlacement floatingApp(const QString& windowId, const QRect& rect = QRect(0, 0, 10, 10))
    {
        return makePlacement(windowId, QStringLiteral("app"), WindowPlacement::stateFloating(),
                             WindowPlacement::snapEngineId(), QStringLiteral("DP-1"), rect);
    }

    /// The slot-less geometry stub every open writes under the live uuid.
    static WindowPlacement stubFor(const QString& windowId, const QRect& spawn)
    {
        WindowPlacement p;
        p.windowId = windowId;
        p.appId = QStringLiteral("app");
        p.screenId = QStringLiteral("DP-1");
        p.freeGeometryByScreen.insert(QStringLiteral("DP-1"), spawn);
        return p;
    }

private Q_SLOTS:
    // take()'s appId FIFO (the snap engine's open-path consumer) applies the
    // same live exclusion takeForReopen does: a second instance opened beside
    // a snapped first must not consume the record just re-bound to the first.
    // With no other record it consumes nothing and is a fresh window; with a
    // closed sibling's record beside the live one it consumes THAT and leaves
    // the live one (skip and continue, not stop at the first live hit).
    void testTake_fifoSkipsRecordBoundToLiveSibling()
    {
        QSet<QString> liveInstances;
        WindowPlacementStore store;
        store.setLiveInstanceProbe(liveInstanceProbe(liveInstances));
        QVERIFY(store.record(snappedApp(QStringLiteral("app|first"))));
        liveInstances.insert(QStringLiteral("first"));

        QVERIFY2(!store.take(QStringLiteral("app|second"), QStringLiteral("app")).has_value(),
                 "a live sibling's record is never handed to a fresh same-app window");
        // The preferred pass sees the same exclusion: a predicate that ranks
        // the live record first must not smuggle it past the probe.
        QVERIFY2(!store
                      .take(QStringLiteral("app|second"), QStringLiteral("app"), {},
                            [](const WindowPlacement& p) {
                                return p.windowId == QStringLiteral("app|first");
                            })
                      .has_value(),
                 "the preferred pass is live-excluded too");
        QCOMPARE(store.size(), 1);

        // A closed sibling's record beside the live one is consumed, and the
        // live one stays.
        QVERIFY(store.record(floatingApp(QStringLiteral("app|gone"))));
        const auto consumed = store.take(QStringLiteral("app|second"), QStringLiteral("app"));
        QVERIFY(consumed.has_value());
        QCOMPARE(consumed->windowId, QStringLiteral("app|gone"));
        QVERIFY2(store.contains(QStringLiteral("app|first")), "the live sibling's record survives the take");
        // The window's OWN record is still its history, live or not (the
        // daemon-restart shape).
        QVERIFY(store.take(QStringLiteral("app|first"), QStringLiteral("app")).has_value());
        QCOMPARE(store.size(), 0);
    }

    // The sibling a fresh window inherits from is the earliest-recorded live
    // one by bucket POSITION (not the lowest sequence) that passes the
    // caller's accept, never the asking window's own instance, and there is
    // no sibling at all without a probe, without a live window, or without
    // an appId.
    void testPeekLiveSibling_earliestLiveOtherInstancePassingAccept()
    {
        QSet<QString> liveInstances;
        WindowPlacementStore store;
        // Without a probe liveness cannot be established, so there is no sibling.
        QVERIFY(store.record(snappedApp(QStringLiteral("app|a"))));
        QVERIFY(!store.peekLiveSibling(QStringLiteral("app|new"), QStringLiteral("app")).has_value());

        store.setLiveInstanceProbe(liveInstanceProbe(liveInstances));
        QVERIFY(store.record(snappedApp(QStringLiteral("app|b"), QRect(50, 50, 20, 20))));
        // Nothing live: no sibling. A closed sibling's record is reopen memory,
        // not a window to inherit from.
        QVERIFY(!store.peekLiveSibling(QStringLiteral("app|new"), QStringLiteral("app")).has_value());
        liveInstances.insert(QStringLiteral("a"));
        liveInstances.insert(QStringLiteral("b"));
        // An empty asker has no instance to be a sibling OF, and an empty
        // appId names no bucket.
        QVERIFY(!store.peekLiveSibling(QString(), QStringLiteral("app")).has_value());
        QVERIFY(!store.peekLiveSibling(QStringLiteral("app|new"), QString()).has_value());

        // Both live: the earliest-recorded wins (a before b), whatever their
        // sequence stamps say. Re-recording A with a real change gives A the
        // NEWEST sequence while it keeps its position, so a lowest-sequence
        // pick would answer b and a positional one still answers a.
        auto aAgain = snappedApp(QStringLiteral("app|a"), QRect(60, 60, 20, 20));
        QVERIFY(store.record(aAgain));
        const auto sibling = store.peekLiveSibling(QStringLiteral("app|new"), QStringLiteral("app"));
        QVERIFY(sibling.has_value());
        QCOMPARE(sibling->windowId, QStringLiteral("app|a"));
        // The asking window's own instance is never its own sibling.
        const auto other = store.peekLiveSibling(QStringLiteral("app|a"), QStringLiteral("app"));
        QVERIFY(other.has_value());
        QCOMPARE(other->windowId, QStringLiteral("app|b"));
        // accept skips a live sibling the caller cannot use and goes on to the
        // next, instead of settling for the first.
        const auto accepted =
            store.peekLiveSibling(QStringLiteral("app|new"), QStringLiteral("app"), [](const WindowPlacement& p) {
                return p.windowId != QStringLiteral("app|a");
            });
        QVERIFY(accepted.has_value());
        QCOMPARE(accepted->windowId, QStringLiteral("app|b"));
        // Non-consuming.
        QCOMPARE(store.size(), 2);

        // The order survives a save and load: the persisted bucket order is
        // the tie-break after a restart, so a is still first.
        WindowPlacementStore reloaded;
        reloaded.setLiveInstanceProbe(liveInstanceProbe(liveInstances));
        reloaded.deserialize(store.serialize());
        const auto persisted = reloaded.peekLiveSibling(QStringLiteral("app|new"), QStringLiteral("app"));
        QVERIFY(persisted.has_value());
        QCOMPARE(persisted->windowId, QStringLiteral("app|a"));
    }

    // peek() gains the exclusion on request only: the snap engine's
    // cross-screen gate peeks to predict what take() will consume, and the two
    // must agree on a live sibling's record. Plain peek() keeps answering it
    // (float-back geometry reads legitimately consult a live sibling), and
    // the exclusion is about LIVENESS: a closed sibling's record is still
    // answered, and an empty asker excludes every live record.
    void testPeek_excludeLiveSiblingsOnRequest()
    {
        QSet<QString> liveInstances{QStringLiteral("first")};
        WindowPlacementStore store;
        store.setLiveInstanceProbe(liveInstanceProbe(liveInstances));
        QVERIFY(store.record(snappedApp(QStringLiteral("app|first"))));

        QVERIFY(store.peek(QStringLiteral("app|second"), QStringLiteral("app")).has_value());
        QVERIFY(!store.peek(QStringLiteral("app|second"), QStringLiteral("app"), {}, /*excludeLiveSiblings=*/true)
                     .has_value());
        // The asker's own record is never excluded from itself.
        QVERIFY(store.peek(QStringLiteral("app|first"), QStringLiteral("app"), {}, /*excludeLiveSiblings=*/true)
                    .has_value());
        // An empty asker is nobody's own instance: every live record is other.
        QVERIFY(store.peek(QString(), QStringLiteral("app")).has_value());
        QVERIFY(!store.peek(QString(), QStringLiteral("app"), {}, /*excludeLiveSiblings=*/true).has_value());
        // Positive control: the same record, closed, is answered again.
        liveInstances.clear();
        QVERIFY2(store.peek(QStringLiteral("app|second"), QStringLiteral("app"), {}, /*excludeLiveSiblings=*/true)
                     .has_value(),
                 "the exclusion is about liveness, not about siblings");
    }

    // An open claim dies with its instance on every close funnel. The prune
    // backstop (a window that died without a close signal) drops the claims
    // of every instance missing from the alive set, and the inert close mark
    // still releases its own, so the record a dead window claimed never stays
    // unpairable for later same-app opens. Pinned through peek() AND through
    // take(), the two production readers of a claim.
    void testClaim_releasedByMarkInstanceClosedAndClear()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(floatingApp(QStringLiteral("app|old"))));
        // A fresh instance claims the closed sibling's record at open.
        QVERIFY(store.claimForOpen(QStringLiteral("app|claimer"), QStringLiteral("app")).has_value());
        // While the claim stands, another instance may not read that record.
        QVERIFY(!store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());

        // The claimer dies unobserved: the alive-set backstop. An alive set
        // naming the claimer keeps the claim, one without it drops it.
        QCOMPARE(store.releaseOpenClaimsExcept({QStringLiteral("claimer")}), 0);
        QVERIFY(!store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());
        QCOMPARE(store.releaseOpenClaimsExcept({QStringLiteral("other")}), 1);
        QVERIFY2(store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value(),
                 "a dead claimer must not keep a record unpairable");

        // The close mark releases the claim too, and reports no revoke.
        QVERIFY(store.claimForOpen(QStringLiteral("app|claimer1"), QStringLiteral("app")).has_value());
        QVERIFY(!store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());
        QVERIFY(!store.markInstanceClosed(QStringLiteral("app|claimer1")));
        QVERIFY(store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());

        // Same through clear(): the instance's own claim on a SIBLING's record
        // goes too, not only the claims naming the records clear() removes.
        QVERIFY(store.claimForOpen(QStringLiteral("app|claimer2"), QStringLiteral("app")).has_value());
        QVERIFY(!store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());
        store.clear(QStringLiteral("app|claimer2")); // removes nothing: it holds no record of its own
        QVERIFY(store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());
        QVERIFY2(store.take(QStringLiteral("app|other"), QStringLiteral("app")).has_value(),
                 "the consuming reader honours the release too");
    }

    // record() rewrites the stored windowId STRING on every merge, and the
    // open claim is keyed on that string. A same-bucket rewrite with a
    // different prefix (the bucket keys on the registry's class, the id on
    // whatever the caller passed) used to leave the claim on the old string,
    // locking the instance out of its own record.
    void testClaim_rekeyedOnSameBucketIdRewrite()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(floatingApp(QStringLiteral("app|old"))));
        const auto claimed = store.claimForOpen(QStringLiteral("app|mine"), QStringLiteral("app"));
        QVERIFY(claimed.has_value());
        QCOMPARE(claimed->windowId, QStringLiteral("app|old"));

        // Re-record the claimed record's instance under a different prefix,
        // same bucket: the string changes, the record stays.
        WindowPlacement rewritten = *claimed;
        rewritten.windowId = QStringLiteral("APP|old");
        rewritten.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(5, 5, 10, 10));
        QVERIFY(store.record(rewritten));

        // The claimer can still pair with it (claim re-keyed), and a sibling
        // still cannot (claim not lost).
        const auto mine = store.peek(QStringLiteral("app|mine"), QStringLiteral("app"));
        QVERIFY2(mine.has_value(), "the instance must not be locked out of the record it claimed");
        QCOMPARE(mine->windowId, QStringLiteral("APP|old"));
        QVERIFY(!store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());
    }

    // Capacity eviction drops the claim naming the evicted record. Without
    // that the claimer's pairing keeps naming a record that is gone and
    // pairingAllows, which fails open only when the reverse index has
    // forgotten the id too, locks the claimer out of every record.
    void testClaim_droppedByCapacityEviction()
    {
        WindowPlacementStore store;
        // No probe: the eviction's live tier is skipped and, with every
        // record restorable, the positional head (app|0) goes at MaxPerApp.
        for (int i = 0; i < WindowPlacementStore::MaxPerApp; ++i) {
            QVERIFY(store.record(snappedApp(QStringLiteral("app|%1").arg(i))));
        }
        // Claim the head by its own instance (step 1), so the eviction hits
        // exactly the claimed record.
        const auto claimed = store.claimForOpen(QStringLiteral("app|0"), QStringLiteral("app"));
        QVERIFY(claimed.has_value());
        QCOMPARE(claimed->windowId, QStringLiteral("app|0"));
        QVERIFY(store.record(snappedApp(QStringLiteral("app|overflow"))));
        QVERIFY2(!store.contains(QStringLiteral("app|0")), "the positional head is the last-resort victim");
        QVERIFY2(store.peek(QStringLiteral("app|0"), QStringLiteral("app")).has_value(),
                 "a claim on an evicted record must not lock its instance out of the rest");
        QVERIFY(store.take(QStringLiteral("app|0"), QStringLiteral("app")).has_value());
    }

    // The pure-float collapse and removeIf drop the claims naming the records
    // they prune, for the same reason.
    void testClaim_droppedByCollapseAndRemoveIf()
    {
        WindowPlacementStore store;
        // A closed floating sibling, claimed by an opener BEFORE the keeper is
        // recorded: the bucket claim picks the newest record, and the keeper
        // must not be it.
        QVERIFY(store.record(floatingApp(QStringLiteral("app|sibling"), QRect(0, 0, 300, 200))));
        const auto claimed = store.claimForOpen(QStringLiteral("app|claimer"), QStringLiteral("app"));
        QVERIFY(claimed.has_value());
        QCOMPARE(claimed->windowId, QStringLiteral("app|sibling"));
        QVERIFY(store.record(floatingApp(QStringLiteral("app|keep"), QRect(50, 50, 300, 200))));
        QVERIFY2(store.collapsePureFloatSiblings(QStringLiteral("app"), QStringLiteral("app|keep")),
                 "the closed pure-float sibling on the same screen is pruned");
        QVERIFY(!store.contains(QStringLiteral("app|sibling")));
        const auto readable = store.peek(QStringLiteral("app|claimer"), QStringLiteral("app"));
        QVERIFY2(readable.has_value(), "a claim on a collapsed record must not lock its instance out");
        QCOMPARE(readable->windowId, QStringLiteral("app|keep"));

        // removeIf: claim the keeper, remove it by predicate, and a later
        // record is still readable by the claimer.
        QVERIFY(store.claimForOpen(QStringLiteral("app|claimer2"), QStringLiteral("app")).has_value());
        QVERIFY(store.removeIf([](const WindowPlacement& p) {
            return p.windowId == QStringLiteral("app|keep");
        }));
        QVERIFY(store.record(floatingApp(QStringLiteral("app|later"))));
        const auto again = store.peek(QStringLiteral("app|claimer2"), QStringLiteral("app"));
        QVERIFY2(again.has_value(), "a claim on a removed record must not lock its instance out");
        QCOMPARE(again->windowId, QStringLiteral("app|later"));
    }

    // Every open on a tiling screen writes the window's own slot-less
    // geometry stub BEFORE the claim runs. The stub has geometry, so it is
    // "restorable", but it is not a placement: claiming it (in either step)
    // locked the opener out of the closed sibling's record it was meant to
    // reopen from.
    void testClaim_neverClaimsTheAskersOwnStub()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(floatingApp(QStringLiteral("app|old"), QRect(100, 100, 400, 300))));
        // The opener's stub is the NEWEST record in the bucket.
        QVERIFY(store.record(stubFor(QStringLiteral("app|new"), QRect(0, 0, 500, 500))));
        const auto claimed = store.claimForOpen(QStringLiteral("app|new"), QStringLiteral("app"));
        QVERIFY(claimed.has_value());
        QCOMPARE(claimed->windowId, QStringLiteral("app|old"));
        // And the tiling reopen path then consumes that sibling record.
        const auto reopened = store.takeForReopen(WindowPlacement::snapEngineId(), QStringLiteral("app|new"),
                                                  QStringLiteral("app"), QStringLiteral("DP-1"));
        QVERIFY2(reopened.has_value(), "the claim must pair the opener with the closed sibling, not its own stub");
        QCOMPARE(reopened->freeGeometryFor(QStringLiteral("DP-1")), QRect(100, 100, 400, 300));
    }

    // Step 1 keeps the two claim maps in lockstep: a claim another instance
    // held on this record is dropped before the record is re-claimed by its
    // own instance, or the other instance's pairing names a record the
    // reverse index attributes to someone else.
    void testClaim_ownRecordClaimDropsAForeignClaim()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(floatingApp(QStringLiteral("app|w"))));
        QVERIFY(store.claimForOpen(QStringLiteral("app|other"), QStringLiteral("app")).has_value());
        // The record's own instance claims it (a daemon-restart re-announce).
        const auto own = store.claimForOpen(QStringLiteral("app|w"), QStringLiteral("app"));
        QVERIFY(own.has_value());
        QCOMPARE(own->windowId, QStringLiteral("app|w"));
        // The other instance no longer holds a claim on it, so with the
        // record gone it can read a fresh one instead of being locked out.
        QVERIFY(store.take(QStringLiteral("app|w"), QStringLiteral("app")).has_value());
        QVERIFY(store.record(floatingApp(QStringLiteral("app|fresh"))));
        QVERIFY(store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());
    }

    // ── The reopen contract (restores happen only where a window opens) ──

    static WindowPlacement snappedOn(const QString& windowId, const QString& screenId)
    {
        return makePlacement(windowId, QStringLiteral("app"), WindowPlacement::stateSnapped(),
                             WindowPlacement::snapEngineId(), screenId);
    }

    // A window's OWN record is never a pending cross-screen restore: it is
    // compared at the virtual-screen level, and another screen is a leave.
    void testPendingCrossScreen_ownRecordNeverPending()
    {
        const auto own = snappedOn(QStringLiteral("app|w"), QStringLiteral("DP-1/vs:1"));
        const auto allSnapping = [](const QString&, int, const QString&) {
            return true;
        };
        QVERIFY(!PhosphorEngine::pendingCrossScreenSnapRestore(own, QStringLiteral("app|w"),
                                                               QStringLiteral("DP-1/vs:0"), allSnapping));
    }

    // A FIFO record (another instance) is pending only on another virtual
    // screen of the SAME output, and only when that screen runs the mode.
    void testPendingCrossScreen_fifoSameOutputOtherVs()
    {
        const auto fifo = snappedOn(QStringLiteral("app|old"), QStringLiteral("DP-1/vs:1"));
        const auto snapping = [](const QString&, int, const QString&) {
            return true;
        };
        const auto notSnapping = [](const QString&, int, const QString&) {
            return false;
        };
        QVERIFY(PhosphorEngine::pendingCrossScreenSnapRestore(fifo, QStringLiteral("app|new"),
                                                              QStringLiteral("DP-1/vs:0"), snapping));
        QVERIFY2(!PhosphorEngine::pendingCrossScreenSnapRestore(fifo, QStringLiteral("app|new"), QStringLiteral("DP-2"),
                                                                snapping),
                 "a record on another KWin output is never pulled");
        QVERIFY2(!PhosphorEngine::pendingCrossScreenSnapRestore(fifo, QStringLiteral("app|new"),
                                                                QStringLiteral("DP-1/vs:0"), notSnapping),
                 "a record on a screen of another mode is never pulled");
    }

    void testOwnRecordLeftScreen_rows()
    {
        auto own = snappedOn(QStringLiteral("app|w"), QStringLiteral("DP-1/vs:1"));
        QVERIFY(PhosphorEngine::ownRecordLeftScreen(own, QStringLiteral("DP-1/vs:0")));
        QVERIFY(!PhosphorEngine::ownRecordLeftScreen(own, QStringLiteral("DP-1/vs:1")));
        QVERIFY(!PhosphorEngine::ownRecordLeftScreen(own, QString()));
        auto unscreened = own;
        unscreened.screenId.clear();
        QVERIFY(!PhosphorEngine::ownRecordLeftScreen(unscreened, QStringLiteral("DP-2")));
        auto released = own;
        released.engines[QString(WindowPlacement::snapEngineId())].state = QString(WindowPlacement::stateReleased());
        QVERIFY2(!PhosphorEngine::ownRecordLeftScreen(released, QStringLiteral("DP-2")),
                 "a record whose slots are all released is already a verdict");
    }

    // Two instances of one app reopen on two monitors: each claims the record
    // of ITS monitor, whatever their order, instead of the first opener taking
    // the newest record wherever it lives.
    void testClaimForOpenOnOutput_eachInstanceKeepsItsMonitor()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(snappedOn(QStringLiteral("app|d1"), QStringLiteral("DP-1"))));
        QVERIFY(store.record(snappedOn(QStringLiteral("app|d2"), QStringLiteral("DP-2"))));
        const auto first =
            store.claimForOpen(QStringLiteral("app|n1"), QStringLiteral("app"), QStringLiteral("DP-1"), {});
        QVERIFY(first.has_value());
        QCOMPARE(first->windowId, QStringLiteral("app|d1"));
        const auto second =
            store.claimForOpen(QStringLiteral("app|n2"), QStringLiteral("app"), QStringLiteral("DP-2"), {});
        QVERIFY(second.has_value());
        QCOMPARE(second->windowId, QStringLiteral("app|d2"));
    }

    void testClaimForOpenOnOutput_claimsNothingOffOutput()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(snappedOn(QStringLiteral("app|d2"), QStringLiteral("DP-2"))));
        QVERIFY(!store.claimForOpen(QStringLiteral("app|n1"), QStringLiteral("app"), QStringLiteral("DP-1"), {})
                     .has_value());
        // Left untouched for an opener on its own output.
        const auto there =
            store.claimForOpen(QStringLiteral("app|n2"), QStringLiteral("app"), QStringLiteral("DP-2"), {});
        QVERIFY(there.has_value());
        QCOMPARE(there->windowId, QStringLiteral("app|d2"));
    }

    // The opening engine's predicate filters the siblings.
    void testClaimForOpenOnOutput_restorablePredicateFilters()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(snappedOn(QStringLiteral("app|d1"), QStringLiteral("DP-1"))));
        const auto never = [](const WindowPlacement&) {
            return false;
        };
        QVERIFY(!store.claimForOpen(QStringLiteral("app|n1"), QStringLiteral("app"), QStringLiteral("DP-1"), never)
                     .has_value());
    }

    // The window's own record is final under the contract: a captured own
    // record with nothing restorable claims nothing, never a sibling's.
    void testClaimForOpenOnOutput_ownSlotRecordIsFinal()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(snappedOn(QStringLiteral("app|sib"), QStringLiteral("DP-1"))));
        WindowPlacement own;
        own.windowId = QStringLiteral("app|w");
        own.appId = QStringLiteral("app");
        own.screenId = QStringLiteral("DP-1");
        own.engines[QString(WindowPlacement::snapEngineId())].state = QString(WindowPlacement::stateReleased());
        QVERIFY(store.record(own));
        QVERIFY(!store.claimForOpen(QStringLiteral("app|w"), QStringLiteral("app"), QStringLiteral("DP-1"), {})
                     .has_value());
        // The sibling's record is still free for another opener.
        QVERIFY(
            store.claimForOpen(QStringLiteral("app|n"), QStringLiteral("app"), QStringLiteral("DP-1"), {}).has_value());
    }

    // A claimer that died without a close signal stops reserving its record.
    void testReleaseOpenClaimsExcept_freesADeadClaimersRecord()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(snappedOn(QStringLiteral("app|d1"), QStringLiteral("DP-1"))));
        QVERIFY(store.claimForOpen(QStringLiteral("app|dead"), QStringLiteral("app"), QStringLiteral("DP-1"), {})
                    .has_value());
        QVERIFY(!store.claimForOpen(QStringLiteral("app|n"), QStringLiteral("app"), QStringLiteral("DP-1"), {})
                     .has_value());
        QCOMPARE(store.releaseOpenClaimsExcept({QStringLiteral("n")}), 1);
        const auto freed =
            store.claimForOpen(QStringLiteral("app|n"), QStringLiteral("app"), QStringLiteral("DP-1"), {});
        QVERIFY(freed.has_value());
        QCOMPARE(freed->windowId, QStringLiteral("app|d1"));
    }
};

QTEST_GUILESS_MAIN(TestWindowPlacementStoreSiblings)
#include "test_window_placement_store_siblings.moc"
