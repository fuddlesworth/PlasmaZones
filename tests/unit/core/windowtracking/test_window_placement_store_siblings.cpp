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
 *        wanting: a claim dies with its instance on both close funnels, and
 *        survives a rewrite of the record's id string.
 */
class TestWindowPlacementStoreSiblings : public QObject
{
    Q_OBJECT

    static WindowPlacement snappedApp(const QString& windowId, const QRect& rect = QRect(0, 0, 10, 10))
    {
        return makePlacement(windowId, QStringLiteral("app"), WindowPlacement::stateSnapped(),
                             WindowPlacement::snapEngineId(), QStringLiteral("DP-1"), rect);
    }

private Q_SLOTS:
    // take()'s appId FIFO (the snap engine's open-path consumer) applies the
    // same live exclusion takeForReopen does: a second instance opened beside
    // a snapped first must not consume the record just re-bound to the first.
    // With no other record it consumes nothing and is a fresh window.
    void testTake_fifoSkipsRecordBoundToLiveSibling()
    {
        WindowPlacementStore store;
        QSet<QString> liveInstances;
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

        // The window's OWN record is still its history, live or not (the
        // daemon-restart shape), and a closed sibling's record is fair game.
        QVERIFY(store.take(QStringLiteral("app|first"), QStringLiteral("app")).has_value());
        QVERIFY(store.record(makePlacement(QStringLiteral("app|gone"), QStringLiteral("app"),
                                           WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                           QStringLiteral("DP-1"), QRect(0, 0, 10, 10))));
        QVERIFY(store.take(QStringLiteral("app|second"), QStringLiteral("app")).has_value());
        QCOMPARE(store.size(), 0);
    }

    // The sibling a fresh window inherits from is the EARLIEST-recorded live
    // one that passes the caller's accept, never the asking window's own
    // instance, and there is no sibling at all without a probe or without a
    // live window.
    void testPeekLiveSibling_earliestLiveOtherInstancePassingAccept()
    {
        WindowPlacementStore store;
        QSet<QString> liveInstances;
        // Without a probe liveness cannot be established, so there is no sibling.
        QVERIFY(store.record(snappedApp(QStringLiteral("app|a"))));
        QVERIFY(!store.peekLiveSibling(QStringLiteral("app|new"), QStringLiteral("app")).has_value());

        store.setLiveInstanceProbe(liveInstanceProbe(liveInstances));
        QVERIFY(store.record(snappedApp(QStringLiteral("app|b"), QRect(50, 50, 20, 20))));
        // Nothing live: no sibling. A closed sibling's record is reopen memory,
        // not a window to inherit from.
        QVERIFY(!store.peekLiveSibling(QStringLiteral("app|new"), QStringLiteral("app")).has_value());
        // An empty asker has no instance to be a sibling OF.
        liveInstances.insert(QStringLiteral("a"));
        liveInstances.insert(QStringLiteral("b"));
        QVERIFY(!store.peekLiveSibling(QString(), QStringLiteral("app")).has_value());

        // Both live: the earliest-recorded wins (a before b), whatever their
        // sequence stamps say. Re-recording b with a real change restamps it
        // newest and must not change the answer.
        auto bAgain = snappedApp(QStringLiteral("app|b"));
        bAgain.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(60, 60, 20, 20));
        QVERIFY(store.record(bAgain));
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
    }

    // peek() gains the exclusion on request only: the snap engine's
    // cross-screen gate peeks to predict what take() will consume, and the two
    // must agree on a live sibling's record. Plain peek() keeps answering it
    // (float-back geometry reads legitimately consult a live sibling).
    void testPeek_excludeLiveSiblingsOnRequest()
    {
        WindowPlacementStore store;
        QSet<QString> liveInstances{QStringLiteral("first")};
        store.setLiveInstanceProbe(liveInstanceProbe(liveInstances));
        QVERIFY(store.record(snappedApp(QStringLiteral("app|first"))));

        QVERIFY(store.peek(QStringLiteral("app|second"), QStringLiteral("app")).has_value());
        QVERIFY(!store.peek(QStringLiteral("app|second"), QStringLiteral("app"), {}, /*excludeLiveSiblings=*/true)
                     .has_value());
        // The asker's own record is never excluded from itself.
        QVERIFY(store.peek(QStringLiteral("app|first"), QStringLiteral("app"), {}, /*excludeLiveSiblings=*/true)
                    .has_value());
    }

    // An open claim dies with its instance on BOTH close funnels. The prune
    // backstop (a window that died without a close signal) reaches only
    // markInstanceClosed, which used to leave the claim standing, so the
    // record the dead window claimed stayed unpairable for every later
    // same-app open.
    void testClaim_releasedByMarkInstanceClosedAndClear()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(makePlacement(QStringLiteral("app|old"), QStringLiteral("app"),
                                           WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                           QStringLiteral("DP-1"), QRect(0, 0, 10, 10))));
        // A fresh instance claims the closed sibling's record at open.
        QVERIFY(store.claimForOpen(QStringLiteral("app|claimer"), QStringLiteral("app")).has_value());
        // While the claim stands, another instance may not read that record.
        QVERIFY(!store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());

        // The claimer dies unobserved: the backstop funnel.
        store.markInstanceClosed(QStringLiteral("app|claimer"), /*graceEligible=*/false);
        QVERIFY2(store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value(),
                 "a dead claimer must not keep a record unpairable");

        // Same through clear(): the instance's own claim on a SIBLING's record
        // goes too, not only the claims naming the records clear() removes.
        QVERIFY(store.claimForOpen(QStringLiteral("app|claimer2"), QStringLiteral("app")).has_value());
        QVERIFY(!store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());
        store.clear(QStringLiteral("app|claimer2")); // removes nothing: it holds no record of its own
        QVERIFY(store.peek(QStringLiteral("app|other"), QStringLiteral("app")).has_value());
    }

    // record() rewrites the stored windowId STRING on every merge, and the
    // open claim is keyed on that string. A same-bucket rewrite with a
    // different prefix (the bucket keys on the registry's class, the id on
    // whatever the caller passed) used to leave the claim on the old string,
    // locking the instance out of its own record.
    void testClaim_rekeyedOnSameBucketIdRewrite()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(makePlacement(QStringLiteral("app|old"), QStringLiteral("app"),
                                           WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(),
                                           QStringLiteral("DP-1"), QRect(0, 0, 10, 10))));
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
};

QTEST_GUILESS_MAIN(TestWindowPlacementStoreSiblings)
#include "test_window_placement_store_siblings.moc"
