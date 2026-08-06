// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "helpers/AutotileFakes.h"
#include "helpers/WindowPlacementBuilders.h"

using namespace PhosphorScrollEngine;
using PhosphorEngine::WindowPlacement;
using PlasmaZones::TestHelpers::FakeStickyWindowTracking;
using PlasmaZones::TestHelpers::makePlacement;

/**
 * @brief Close/reopen restore through the unified placement store, on the
 *        SCROLL engine.
 *
 * A reopened window carries a fresh KWin uuid, so its record matches by the
 * appId FIFO — never uuid-exact. The engine's open-time restore resolves that
 * through WindowPlacementStore::takeForReopen (the shared reopen pattern
 * autotile's insert path uses). These tests pin the regression that motivated
 * the hoist: the scroll engine consulted only peekExact, so a window floated
 * in scrolling mode, closed, and reopened re-tiled into the strip instead of
 * staying floated.
 */
class TestScrollReopenRestore : public QObject
{
    Q_OBJECT

private:
    static constexpr auto Screen = "S1";

    /// A headless tracker-wired engine on the geometry-provider seam,
    /// active on Screen. The activation echo mirrors the lib suite's
    /// makeProviderEngine (which cannot be reused here: it hardwires a
    /// null tracker, and this suite exists to test the tracker seam).
    static ScrollEngine* makeEngine(QObject* parent, FakeStickyWindowTracking* tracker)
    {
        auto* engine = new ScrollEngine(tracker, nullptr, parent);
        const auto geometry = [](const QString&) {
            return QRect(0, 0, 1200, 800);
        };
        engine->setScreenGeometryProviders(geometry, geometry);
        QObject::connect(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested, engine,
                         [engine](const QString& windowId) {
                             engine->windowFocused(windowId, engine->screenForTrackedWindow(windowId));
                         });
        engine->setActiveScreens({QLatin1String(Screen)});
        engine->setCurrentDesktopForScreen(QLatin1String(Screen), 1);
        return engine;
    }

    static ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screenId));
    }

    /// The daemon's close capture, condensed: snapshot the engine's slot,
    /// graft the free-geometry rect the shared layer would carry, record.
    static void captureClose(ScrollEngine* engine, FakeStickyWindowTracking* tracker, const QString& windowId,
                             const QRect& freeGeo = QRect())
    {
        auto record = engine->capturePlacement(windowId);
        QVERIFY(record.has_value());
        if (freeGeo.isValid()) {
            record->freeGeometryByScreen.insert(QLatin1String(Screen), freeGeo);
        }
        QVERIFY(tracker->placementStore().record(*record));
        engine->windowClosed(windowId);
    }

private Q_SLOTS:

    void reopenWithFreshUuidStaysFloating()
    {
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("editor|e1"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        engine->setWindowFloat(QStringLiteral("term|t1"), true, screen);

        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        QVERIFY(state->isFloating(QStringLiteral("term|t1")));

        captureClose(engine, &tracker, QStringLiteral("term|t1"), QRect(40, 40, 500, 300));
        QVERIFY(!state->containsWindow(QStringLiteral("term|t1")));

        // Reproduce the production open sequence: the effect's pre-tile
        // geometry capture writes a geometry-only, ENGINE-SLOT-LESS record
        // under the fresh uuid BEFORE the engine's restore runs (the exact
        // stub whose veto was the original bug — the fake's recordFreeGeometry
        // is a no-op, so without this the gate never even fires here).
        {
            PhosphorEngine::WindowPlacement stub;
            stub.windowId = QStringLiteral("term|t2");
            stub.appId = QStringLiteral("term");
            stub.screenId = screen;
            stub.freeGeometryByScreen.insert(screen, QRect(0, 0, 800, 600));
            QVERIFY(tracker.placementStore().record(stub));
        }

        // Reopen: same app, FRESH uuid. The floating record must be consumed
        // via the appId FIFO and the window must arrive floating, not tiled.
        engine->windowOpened(QStringLiteral("term|t2"), screen, 0, 0);
        QVERIFY(state->isFloating(QStringLiteral("term|t2")));
        QVERIFY(!state->strip().containsWindow(QStringLiteral("term|t2")));
        // The consumed record was re-bound to the live uuid (takeForReopen
        // rule 2), so the float-back geometry survives for the next reopen.
        const auto rebound = tracker.placementStore().peekExact(QStringLiteral("term|t2"));
        QVERIFY(rebound.has_value());
        QCOMPARE(rebound->freeGeometryFor(screen), QRect(40, 40, 500, 300));
    }

    void ruleFloatedReopenRestoresRememberedPosition()
    {
        // A "Float this app" rule floats the arrival before the tiled/record
        // ladder runs. The engine-decided float must still consume the
        // window's FLOATING record and restore the remembered position, the
        // outcome autotile reaches through its record branch. Regression:
        // the rule exit used to skip the store entirely, so a rule-floated
        // reopen forgot its position and left the record stale in the FIFO.
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);
        engine->setFloatPredicate([](const QString& windowId, const QString&) {
            return windowId.startsWith(QLatin1String("term|"));
        });

        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        QVERIFY(state->isFloating(QStringLiteral("term|t1"))); // rule float
        captureClose(engine, &tracker, QStringLiteral("term|t1"), QRect(40, 40, 500, 300));

        QSignalSpy restoreSpy(engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        engine->windowOpened(QStringLiteral("term|t2"), screen, 0, 0);
        QVERIFY(state->isFloating(QStringLiteral("term|t2")));
        QCOMPARE(restoreSpy.count(), 1);
        QCOMPARE(restoreSpy.at(0).at(0).toString(), QStringLiteral("term|t2"));
        QCOMPARE(restoreSpy.at(0).at(1).toRect(), QRect(40, 40, 500, 300));
        // Consumed and re-bound, not left stale in the FIFO.
        QVERIFY(tracker.placementStore().peekExact(QStringLiteral("term|t2")).has_value());
        QCOMPARE(tracker.placementStore().size(), 1);
    }

    void reopenWithSameUuidStaysFloating()
    {
        // The daemon-restart shape (uuid stable) the old peekExact-only read
        // covered — pinned so the reopen fix cannot regress it.
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        engine->setWindowFloat(QStringLiteral("term|t1"), true, screen);
        captureClose(engine, &tracker, QStringLiteral("term|t1"), QRect(40, 40, 500, 300));

        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        QVERIFY(state->isFloating(QStringLiteral("term|t1")));
    }

    void reopenWithFreshUuidRestoresColumn()
    {
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("one|a"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("two|b"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("three|c"), screen, 0, 0);
        const int closedColumn = engine->columnIndexForWindow(screen, QStringLiteral("two|b"));
        QVERIFY(closedColumn >= 0);

        captureClose(engine, &tracker, QStringLiteral("two|b"));

        engine->windowOpened(QStringLiteral("two|b2"), screen, 0, 0);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("two|b2")), closedColumn);
    }

    void massCloseCapturesPreBurstColumnsAndLoginRestoresOrder()
    {
        // The logout regression: session teardown closes every window one at
        // a time, and each removal compacts the strip — so closes 2..N used
        // to capture already-shifted columns (a {0..3} strip persisted with
        // duplicate orders) and the login rebuilt a collapsed strip with
        // windows parked off-viewport. The close-burst ledger must make each
        // capture answer its PRE-burst column, whatever the close order, and
        // the reopen rank logic must rebuild the original order whatever the
        // announce order.
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("aa|a1"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("bb|b1"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("cc|c1"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("dd|d1"), screen, 0, 0);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("aa|a1")), 0);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("dd|d1")), 3);

        // Teardown sweep in a compaction-provoking order.
        captureClose(engine, &tracker, QStringLiteral("bb|b1"));
        captureClose(engine, &tracker, QStringLiteral("dd|d1"));
        captureClose(engine, &tracker, QStringLiteral("aa|a1"));
        captureClose(engine, &tracker, QStringLiteral("cc|c1"));

        // Every record carries its PRE-burst column, not the compacted one.
        const auto orderOf = [&tracker](const QString& windowId) {
            const auto rec = tracker.placementStore().peekExact(windowId);
            return rec ? rec->slotFor(WindowPlacement::scrollingEngineId()).order : -99;
        };
        QCOMPARE(orderOf(QStringLiteral("aa|a1")), 0);
        QCOMPARE(orderOf(QStringLiteral("bb|b1")), 1);
        QCOMPARE(orderOf(QStringLiteral("cc|c1")), 2);
        QCOMPARE(orderOf(QStringLiteral("dd|d1")), 3);

        // Login: fresh uuids in strictly DESCENDING saved order — the one
        // announce order a plain absolute-clamped insert gets wrong ([a,d,b,c]
        // instead of [a,b,c,d]), so this half genuinely pins the rank logic.
        engine->windowOpened(QStringLiteral("dd|d2"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("cc|c2"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("bb|b2"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("aa|a2"), screen, 0, 0);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("aa|a2")), 0);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("bb|b2")), 1);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("cc|c2")), 2);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("dd|d2")), 3);
    }

    void secondInstanceCannotStealLiveSiblingsReboundRecord()
    {
        // The live-instance probe at engine level: after t2 consumes t1's
        // floating record (re-bound to the live t2), a THIRD instance must
        // not steal it — t2 would be left recordless. Probe wired with
        // production's extractInstanceId keying.
        QObject owner;
        FakeStickyWindowTracking tracker;
        tracker.wireLiveInstanceProbe();
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        tracker.liveInstances.insert(QStringLiteral("t1"));
        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        engine->setWindowFloat(QStringLiteral("term|t1"), true, screen);
        captureClose(engine, &tracker, QStringLiteral("term|t1"), QRect(40, 40, 500, 300));
        tracker.liveInstances.remove(QStringLiteral("t1"));

        tracker.liveInstances.insert(QStringLiteral("t2"));
        engine->windowOpened(QStringLiteral("term|t2"), screen, 0, 0);
        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        QVERIFY(state->isFloating(QStringLiteral("term|t2"))); // consumed t1's record

        tracker.liveInstances.insert(QStringLiteral("t3"));
        engine->windowOpened(QStringLiteral("term|t3"), screen, 0, 0);
        QVERIFY(!state->isFloating(QStringLiteral("term|t3"))); // nothing to steal — tiles
        // t2's re-bound record survived.
        QVERIFY(tracker.placementStore().peekExact(QStringLiteral("term|t2")).has_value());
    }

    void stackedTileCloseDoesNotFeedTheBurstLedger()
    {
        // Closing one tile of a STACKED column removes no column, so it must
        // not append to the close-burst ledger — an appended entry would
        // over-correct every later capture of the burst by one.
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("aa|a1"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("bb|b1"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("cc|c1"), screen, 0, 0);
        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        // Stack b into a's column: focus a, then consume the next window.
        engine->windowFocused(QStringLiteral("aa|a1"), screen);
        engine->consumeWindowIntoColumn(screen);
        QCOMPARE(state->strip().columnOfWindow(QStringLiteral("bb|b1")), 0); // stacked with a
        QCOMPARE(state->strip().columnOfWindow(QStringLiteral("cc|c1")), 1);

        // The consume itself ended any burst; now a burst of two closes.
        // Closing b removes a TILE, not a column: no ledger entry, so c's
        // capture must answer its true column 1 — with the bug (stacked
        // closes appended), c would be corrected to 2.
        captureClose(engine, &tracker, QStringLiteral("bb|b1"));
        captureClose(engine, &tracker, QStringLiteral("cc|c1"));
        const auto rec = tracker.placementStore().peekExact(QStringLiteral("cc|c1"));
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(WindowPlacement::scrollingEngineId()).order, 1);
    }

    void reopenRanksStraddledArrivalBetweenRestoredNeighbours()
    {
        // Rank discrimination beyond the mass-close case: two record-restored
        // windows whose saved columns straddle a later arrival must bracket
        // it, whatever the announce order.
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        const auto seed = [&](const QString& windowId, const QString& appId, int order) {
            auto p = makePlacement(windowId, appId, WindowPlacement::stateTiled(), WindowPlacement::scrollingEngineId(),
                                   screen, QRect(), order);
            p.virtualDesktop = 1; // match the engine's current context
            QVERIFY(tracker.placementStore().record(p));
        };
        seed(QStringLiteral("aa|a1"), QStringLiteral("aa"), 0);
        seed(QStringLiteral("bb|b1"), QStringLiteral("bb"), 1);
        seed(QStringLiteral("cc|c1"), QStringLiteral("cc"), 2);

        // Announce the outer pair first, then the straddled middle.
        engine->windowOpened(QStringLiteral("cc|c2"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("aa|a2"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("bb|b2"), screen, 0, 0);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("aa|a2")), 0);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("bb|b2")), 1);
        QCOMPARE(engine->columnIndexForWindow(screen, QStringLiteral("cc|c2")), 2);
    }

    void geometrylessFloatingResidueNotConsumedByFreshSibling()
    {
        // A floating record with NO float-back rect is meaningful only for
        // the SAME instance; consumed by a FIFO sibling it would float a
        // fresh window at its spawn rect for no visible reason. Autotile's
        // accept rule, mirrored on scroll.
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        engine->setWindowFloat(QStringLiteral("term|t1"), true, screen);
        captureClose(engine, &tracker, QStringLiteral("term|t1")); // no free rect

        engine->windowOpened(QStringLiteral("term|t2"), screen, 0, 0);
        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        QVERIFY(!state->isFloating(QStringLiteral("term|t2")));
        QVERIFY(state->strip().containsWindow(QStringLiteral("term|t2")));
    }
};

QTEST_MAIN(TestScrollReopenRestore)
#include "test_scroll_reopen_restore.moc"
