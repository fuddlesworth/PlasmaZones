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

    void tiledRecordIsNotConsumedAndDoesNotFloatTheReopen()
    {
        // Order restore was removed deliberately: a TILED record must not be
        // consumed on reopen (it stays as the exact-final evidence the window
        // closed tiled) and the reopened window takes a normal insert.
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);

        engine->windowOpened(QStringLiteral("one|a"), screen, 0, 0);
        engine->windowOpened(QStringLiteral("two|b"), screen, 0, 0);
        captureClose(engine, &tracker, QStringLiteral("two|b"));
        QVERIFY(tracker.placementStore().peekExact(QStringLiteral("two|b")).has_value());

        engine->windowOpened(QStringLiteral("two|b2"), screen, 0, 0);
        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        QVERIFY(!state->isFloating(QStringLiteral("two|b2"))); // tiled reopen stays tiled
        QVERIFY(engine->columnIndexForWindow(screen, QStringLiteral("two|b2")) >= 0);
        // The tiled record was not consumed by the reopen.
        QVERIFY(tracker.placementStore().peekExact(QStringLiteral("two|b")).has_value());
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

    // =========================================================================
    // Cross-screen session reclaim (claimCrossScreenReopen): KWin's session
    // restore opens windows on a nondeterministic output, so a window recorded
    // TILED on this engine's screen can arrive announced on some other screen.
    // The engine pulls it back into its recorded strip; a floating record, a
    // same-screen record, an already-tracked window, and a home no longer in
    // scrolling mode all refuse the claim.
    // =========================================================================

    void claimCrossScreenReopenPullsTiledRecordHome()
    {
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);
        engine->setScrollingModeResolver([&](const QString& rec, int, const QString&) {
            return rec == screen;
        });

        // Last session: the window closed tiled in Screen's strip.
        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        captureClose(engine, &tracker, QStringLiteral("term|t1"));
        QVERIFY(tracker.placementStore().peekExact(QStringLiteral("term|t1")).has_value());

        // This session: KWin drops the fresh-uuid window on another output.
        QVERIFY2(engine->claimCrossScreenReopen(QStringLiteral("term|t2"), QStringLiteral("OTHER"), 0, 0),
                 "a tiled record homed on a scrolling-mode screen must be reclaimed cross-screen");
        ScrollState* state = stateFor(engine, screen);
        QVERIFY(state);
        QVERIFY2(state->strip().containsWindow(QStringLiteral("term|t2")),
                 "the reclaimed window must re-enter the RECORDED screen's strip");
    }

    void claimCrossScreenReopenRefusalLadder()
    {
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);
        engine->setScrollingModeResolver([&](const QString& rec, int, const QString&) {
            return rec == screen;
        });

        // FLOATING record: float restore is screen-local, never a pull.
        engine->windowOpened(QStringLiteral("edit|e1"), screen, 0, 0);
        engine->setWindowFloat(QStringLiteral("edit|e1"), true, screen);
        captureClose(engine, &tracker, QStringLiteral("edit|e1"), QRect(30, 30, 400, 300));
        QVERIFY2(!engine->claimCrossScreenReopen(QStringLiteral("edit|e2"), QStringLiteral("OTHER"), 0, 0),
                 "a scroll-floating record must not claim cross-screen");

        // Same-screen arrival: the ordinary open path owns it, never the claim.
        engine->windowOpened(QStringLiteral("term|t1"), screen, 0, 0);
        captureClose(engine, &tracker, QStringLiteral("term|t1"));
        QVERIFY2(!engine->claimCrossScreenReopen(QStringLiteral("term|t2"), screen, 0, 0),
                 "an arrival on the recorded screen itself is not cross-screen");

        // Already-tracked window: an in-session move, never a session restore.
        engine->windowOpened(QStringLiteral("term|t2"), screen, 0, 0);
        QVERIFY2(!engine->claimCrossScreenReopen(QStringLiteral("term|t2"), QStringLiteral("OTHER"), 0, 0),
                 "a window this engine already tracks must never be re-claimed");

        // Home context no longer in scrolling mode: the resolver's verdict wins.
        engine->windowOpened(QStringLiteral("web|w1"), screen, 0, 0);
        captureClose(engine, &tracker, QStringLiteral("web|w1"));
        engine->setScrollingModeResolver([](const QString&, int, const QString&) {
            return false;
        });
        QVERIFY2(!engine->claimCrossScreenReopen(QStringLiteral("web|w2"), QStringLiteral("OTHER"), 0, 0),
                 "a home screen no longer in scrolling mode must refuse the claim");

        // No resolver wired at all (headless path): never claims.
        engine->setScrollingModeResolver({});
        QVERIFY(!engine->claimCrossScreenReopen(QStringLiteral("web|w2"), QStringLiteral("OTHER"), 0, 0));
    }

    void windowOpenedDefersToAutotileCrossScreenRestore()
    {
        // The scroll-side reciprocal of autotile's claim: a window recorded
        // TILED on an autotile-mode screen that KWin drops on this scrolling
        // screen must NOT be spliced into the strip — autotile's
        // claimCrossScreenReopen pulls it home instead.
        QObject owner;
        FakeStickyWindowTracking tracker;
        ScrollEngine* engine = makeEngine(&owner, &tracker);
        const QString screen = QLatin1String(Screen);
        engine->setAutotileModeResolver([](const QString& rec, int, const QString&) {
            return rec == QStringLiteral("AUTOTILE-1");
        });

        WindowPlacement rec;
        rec.windowId = QStringLiteral("ide|old");
        rec.appId = QStringLiteral("ide");
        rec.screenId = QStringLiteral("AUTOTILE-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = QString(WindowPlacement::stateTiled());
        slot.order = 0;
        rec.engines.insert(WindowPlacement::autotileEngineId(), slot);
        QVERIFY(tracker.placementStore().record(rec));

        engine->windowOpened(QStringLiteral("ide|new"), screen, 0, 0);
        ScrollState* state = stateFor(engine, screen);
        if (state) {
            QVERIFY2(!state->containsWindow(QStringLiteral("ide|new")),
                     "a cross-screen autotile restore must not be adopted into the strip");
        }
        // The record survives untouched for autotile's claim.
        QVERIFY(tracker.placementStore().peekExact(QStringLiteral("ide|old")).has_value());
    }
};

QTEST_MAIN(TestScrollReopenRestore)
#include "test_scroll_reopen_restore.moc"
