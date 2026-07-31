// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Headless ScrollEngine smoke test: tracking, ordering, float state,
// capture, and handoff semantics with null daemon dependencies (the engine
// tolerates a missing screen manager / tracking service; geometry emission
// is covered by the strip-model tests, not here).

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

class TestScrollEngineSmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void screensSetLifecycle();
    void openTrackAndOrder();
    void floatPullsOutAndRestoresSlot();
    void capturePlacementReportsSlot();
    void handoffReleaseIsTrackingOnly();
    void handoffReceiveAdoptsFloatingWindow();
    void lastManagedRectSurvivesClose();
    void pruneStaleWindowsReclaimsRectsAndSeeds();
    void contextKeysSeparateDesktops();
    void floatRestoresDisplayIntent();
    void pruneDropsWindowBookkeeping();
    void pruneRemovedScreenAndActivitiesSweep();
    void stackedTileFloatRoundTripRestoresSlot();
    void scheduledRetileRunsUnderEventLoop();
    void removedScreenReleasesWindows();
    void desktopSwitchAwayPreservesSiblingContextStrips();
    void seedAdoptionClampsViewToStripEnd();
    void parkingAvoidsAdjacentOutputs();
    void modeRoundTripRestoresStripStructure();
    void operationScreenFallbackIsDeterministic();
    void minSizeSeedsAndCarries();
    void minSizeSurvivesFloatRoundTrip();
    void handoffReleaseClearsFloatMarker();
    void applyPathEmitsOnChangeOnly();
    void orderedOpenFollowsAndConsumesSeed();
    void partiallyConsumedSeedGuardsReopens();
    void orderedOpenForwardArrivalsKeepSeedOrder();
    void floatedOpenConsumesSeed();
    void migrateOutAnnouncesDroppedFloat();
    void contextSwitchFlagRidesChangedScreenSets();
    void zoneNumbersAreViewportRelativeVisibleSlots();
    void centeredColumnKeepsNeighboursFullRects();
    // Strip persistence (stash focus/anchor, serialize/restore) is covered
    // by test_scrollengine_persistence.cpp — this file is at the size split
    // point.

private:
    // NOTE: windowOpened's cross-screen snap-restore defer gate
    // (setSnappingModeResolver + placementStore().peek) is deliberately
    // untested here: makeEngine passes a null IWindowTrackingService, and
    // faking the full tracking service just for the peek would drag half
    // of phosphor-placement into this smoke suite. The gate's daemon-side
    // wiring mirrors AutotileEngine's, whose twin is covered at the
    // integration layer.
    static ScrollEngine* makeEngine(QObject* parent)
    {
        auto* engine = new ScrollEngine(nullptr, nullptr, parent);
        engine->setActiveScreens({QStringLiteral("S1"), QStringLiteral("S2")});
        return engine;
    }

    /// makeEngine's twin with the geometry-provider seam wired, for the tests
    /// that need the apply path to resolve real rects (only then does
    /// m_lastAppliedRect populate, and only then does windowsTiled fire).
    static ScrollEngine* makeProviderEngine(QObject* parent, const QSet<QString>& screens)
    {
        auto* engine = new ScrollEngine(nullptr, nullptr, parent);
        const auto geometry = [](const QString&) {
            return QRect(0, 0, 1200, 800);
        };
        engine->setScreenGeometryProviders(geometry, geometry);
        engine->setActiveScreens(screens);
        return engine;
    }
};

void TestScrollEngineSmoke::screensSetLifecycle()
{
    QObject owner;
    auto* engine = new ScrollEngine(nullptr, nullptr, &owner);
    QVERIFY(!engine->isEnabled());
    QSignalSpy screensSpy(engine, &ScrollEngine::scrollingScreensChanged);

    QSignalSpy enabledSpy(engine, &ScrollEngine::enabledChanged);
    engine->setActiveScreens({QStringLiteral("S2"), QStringLiteral("S1")});
    QVERIFY(engine->isEnabled());
    QVERIFY(engine->isActiveOnScreen(QStringLiteral("S1")));
    QCOMPARE(screensSpy.count(), 1);
    // The wire payload is SORTED (consumers compare successive payloads).
    QCOMPARE(screensSpy.last().at(0).toStringList(), (QStringList{QStringLiteral("S1"), QStringLiteral("S2")}));
    QCOMPARE(enabledSpy.count(), 1);
    engine->setActiveScreens({QStringLiteral("S1")}); // genuine shrink → emit 2

    // Identical non-empty set WITHOUT a preceding desktop/activity switch:
    // NO re-emit. A no-op re-push (updateEngineScreens re-derive) must not
    // claim isDesktopSwitch — TilingAdaptor OR-coalesces the flag across
    // engines, and a false true makes the effect skip the OTHER engine's
    // geometry/border restore in the same pass.
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(screensSpy.count(), 2);

    // After a REAL desktop switch the identical set re-emits once with
    // isDesktopSwitch=true (the effect catch-scan wakeup contract), and
    // the flag is CONSUMED: a further identical push stays silent.
    engine->setCurrentDesktop(1);
    engine->setCurrentDesktop(2); // armSwitch needs an established context first
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(screensSpy.count(), 3);
    QCOMPARE(screensSpy.last().at(1).toBool(), true);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(screensSpy.count(), 3);

    engine->setActiveScreens({});
    QVERIFY(!engine->isEnabled());
    // Empty-identical: a second empty set has nothing to catch-scan for,
    // so no re-emit.
    const int afterEmpty = screensSpy.count();
    engine->setActiveScreens({});
    QCOMPARE(screensSpy.count(), afterEmpty);
}

void TestScrollEngineSmoke::openTrackAndOrder()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);

    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S2"), 0, 0);

    QVERIFY(engine->isWindowTracked(QStringLiteral("app|a")));
    QVERIFY(engine->isWindowTiled(QStringLiteral("app|b")));
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("app|b")), QStringLiteral("S1"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|a"), QStringLiteral("app|b")}));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S2")), (QStringList{QStringLiteral("app|c")}));

    // Opens on a non-scrolling screen are ignored.
    engine->windowOpened(QStringLiteral("app|d"), QStringLiteral("OTHER"), 0, 0);
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|d")));

    engine->windowClosed(QStringLiteral("app|a"));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|a")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("app|b")}));
}

void TestScrollEngineSmoke::floatPullsOutAndRestoresSlot()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);

    QSignalSpy floatSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    QVERIFY(engine->isWindowFloatingInScroll(QStringLiteral("app|b")));
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|b")));
    QVERIFY(!engine->isWindowTiled(QStringLiteral("app|b")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|b")));
    QCOMPARE(floatSpy.count(), 1);
    QCOMPARE(engine->allFloatingWindows(), (QStringList{QStringLiteral("app|b")}));

    // Unfloat restores the remembered column slot: b returns between a and c.
    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    QVERIFY(!engine->isWindowFloatingInScroll(QStringLiteral("app|b")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")}));
}

void TestScrollEngineSmoke::capturePlacementReportsSlot()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);

    const auto placement = engine->capturePlacement(QStringLiteral("app|b"));
    QVERIFY(placement.has_value());
    QCOMPARE(placement->screenId, QStringLiteral("S1"));
    const PhosphorEngine::EngineSlot slot = placement->slotFor(PhosphorEngine::WindowPlacement::scrollingEngineId());
    QCOMPARE(slot.state, QString(PhosphorEngine::WindowPlacement::stateTiled()));
    QCOMPARE(slot.order, 1);

    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    const auto floated = engine->capturePlacement(QStringLiteral("app|b"));
    QVERIFY(floated.has_value());
    QCOMPARE(floated->slotFor(PhosphorEngine::WindowPlacement::scrollingEngineId()).state,
             QString(PhosphorEngine::WindowPlacement::stateFloating()));

    QVERIFY(!engine->capturePlacement(QStringLiteral("app|nope")).has_value());

    // Stacked column: slot.order is the COLUMN index (a restore feeds it to
    // insertWindowAt as a column position), NOT the window index — the two
    // diverge once a column holds more than one tile. Unfloat b (the float
    // arm above left it floating), then consume it into a's column: both
    // windows now report column 0.
    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    const auto stackedA = engine->capturePlacement(QStringLiteral("app|a"));
    const auto stackedB = engine->capturePlacement(QStringLiteral("app|b"));
    QVERIFY(stackedA.has_value() && stackedB.has_value());
    QCOMPARE(stackedA->slotFor(PhosphorEngine::WindowPlacement::scrollingEngineId()).order, 0);
    QCOMPARE(stackedB->slotFor(PhosphorEngine::WindowPlacement::scrollingEngineId()).order, 0);
}

void TestScrollEngineSmoke::handoffReleaseIsTrackingOnly()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);

    engine->handoffRelease(QStringLiteral("app|a"));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|a")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("app|b")}));

    // Receive adopts onto the target screen's strip and focuses it.
    PhosphorEngine::IPlacementEngine::HandoffContext ctx;
    ctx.windowId = QStringLiteral("app|a");
    ctx.toScreenId = QStringLiteral("S2");
    ctx.fromEngineId = QStringLiteral("snap");
    engine->handoffReceive(ctx);
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|a")));
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("app|a")), QStringLiteral("S2"));
}

void TestScrollEngineSmoke::handoffReceiveAdoptsFloatingWindow()
{
    // The wasFloating arm of handoffReceive: the arrival never reaches the
    // strip, and the float it carried becomes THIS engine's own — marked, so
    // the next mode transition does not capture it into the snap slot with
    // the arrival frame. The announcement is the effect's only input here.
    //
    // It rides windowFloatingStateSynced, not windowFloatingChanged: an arrival
    // is not a user float action, so the daemon must not restore the stored free
    // geometry over the drop position or raise a float OSD. Autotile's
    // handoffReceive announces on the same passive signal.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    QSignalSpy floatSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
    QSignalSpy activeFloatSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);

    PhosphorEngine::IPlacementEngine::HandoffContext ctx;
    ctx.windowId = QStringLiteral("app|h");
    ctx.toScreenId = QStringLiteral("S2");
    ctx.fromEngineId = QStringLiteral("snap");
    ctx.wasFloating = true;
    engine->handoffReceive(ctx);

    QVERIFY(engine->isWindowTracked(QStringLiteral("app|h")));
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("app|h")), QStringLiteral("S2"));
    QVERIFY(engine->isWindowFloatingInScroll(QStringLiteral("app|h")));
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|h")));
    QVERIFY(!engine->isWindowTiled(QStringLiteral("app|h")));
    QVERIFY(engine->managedWindowOrder(QStringLiteral("S2")).isEmpty());
    QCOMPARE(floatSpy.count(), 1);
    QCOMPARE(floatSpy.last().at(0).toString(), QStringLiteral("app|h"));
    QCOMPARE(floatSpy.last().at(1).toBool(), true);
    QCOMPARE(floatSpy.last().at(2).toString(), QStringLiteral("S2"));
    // And NOT on the active signal, which would teleport the arrival to its
    // stored free geometry.
    QCOMPARE(activeFloatSpy.count(), 0);
}

void TestScrollEngineSmoke::lastManagedRectSurvivesClose()
{
    // The float-back poison guard's memory: the daemon's close capture reads
    // lastManagedRect DURING windowClosed (the engines hear the close before
    // WindowTracking does), so dropping the entry there would hand the
    // column rect back as the reopen float-back geometry.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    const QRect tiled = engine->lastManagedRect(QStringLiteral("app|a"));
    QVERIFY(!tiled.isEmpty()); // the strip rect the apply path actually issued
    engine->windowClosed(QStringLiteral("app|a"));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|a")));
    QCOMPARE(engine->lastManagedRect(QStringLiteral("app|a")), tiled);
}

void TestScrollEngineSmoke::pruneStaleWindowsReclaimsRectsAndSeeds()
{
    // Aliveness sweep: because the rect memory is retained through close and
    // handoffRelease, this prune is its SOLE reclaimer, and it is keyed on
    // aliveness alone — the window below was untracked long before the
    // prune, so the tracking sweep (whose count the return value reports)
    // cannot be what dropped it.
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
        engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
        QCoreApplication::processEvents();
        engine->windowClosed(QStringLiteral("app|a"));
        QVERIFY(!engine->lastManagedRect(QStringLiteral("app|a")).isEmpty());

        QCOMPARE(engine->pruneStaleWindows({}), 0); // nothing tracked was reclaimed
        QVERIFY(engine->lastManagedRect(QStringLiteral("app|a")).isEmpty());
    }

    // Seed sweep: the dead ids leave the captured order, and the consumed
    // set is narrowed with them so the all-consumed drop condition stays
    // exact. Skipping the narrowing makes the (now shorter) list look fully
    // consumed and drops an entry that a still-pending id needs — a would
    // then open next to focus instead of at its captured column.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setInitialWindowOrder(QStringLiteral("S1"),
                                  {QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")});
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);

    // b dies; a has still never arrived, so the entry must survive as {a,c}
    // with only c marked consumed.
    QCOMPARE(engine->pruneStaleWindows({QStringLiteral("app|c")}), 1);
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|b")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("app|c")}));

    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|a"), QStringLiteral("app|c")}));
}

void TestScrollEngineSmoke::contextKeysSeparateDesktops()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setCurrentDesktop(1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);

    // Desktop 2 has its own strip: the desktop-1 window keeps its state but
    // the current-context order for S1 is empty.
    engine->setCurrentDesktop(2);
    QVERIFY(engine->managedWindowOrder(QStringLiteral("S1")).isEmpty());
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("app|b")}));

    engine->setCurrentDesktop(1);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("app|a")}));
    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{1, 2}));

    engine->pruneStatesForDesktop(2);
    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{1}));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|b")));
}

void TestScrollEngineSmoke::floatRestoresDisplayIntent()
{
    // FloatRestore regression (pass-2 fix): the intent a floated column
    // held must survive the round trip — replacing restore.display with
    // the engine default in unfloatWindowInternal fails this.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));

    auto columnDisplayOf = [engine](const QString& windowId) {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S1")));
        const int col = state->strip().columnOfWindow(windowId);
        return col >= 0 ? state->strip().columns().at(col).display : ColumnDisplay::Normal;
    };
    QCOMPARE(columnDisplayOf(QStringLiteral("app|b")), ColumnDisplay::Tabbed);

    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    QCOMPARE(columnDisplayOf(QStringLiteral("app|b")), ColumnDisplay::Tabbed);
}

void TestScrollEngineSmoke::pruneDropsWindowBookkeeping()
{
    // dropWindowBookkeeping regression: pruning a desktop's state must
    // sweep the per-window side maps — the float marker is the observable
    // one headless (m_lastAppliedRect never populates without a screen
    // manager).
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setCurrentDesktop(2);
    engine->windowOpened(QStringLiteral("app|d2"), QStringLiteral("S1"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|d2"), true, QStringLiteral("S1"));
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|d2")));

    engine->pruneStatesForDesktop(2);
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|d2")));
    QVERIFY(!engine->isModeSpecificFloated(QStringLiteral("app|d2")));
}

void TestScrollEngineSmoke::pruneRemovedScreenAndActivitiesSweep()
{
    // The other two prune entry points, same bookkeeping contract as the
    // desktop prune above. Removed-screen: the physical id AND its virtual
    // sub-screens ("S1/vs:0", the PhosphorIdentity separator) are swept,
    // but an id that merely shares the prefix ("S10") is not. Activities:
    // only states whose key names an activity absent from the valid list
    // go; activity-less states stay.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    // The default fixture only activates S1/S2; the sub-screen and the
    // prefix-sharing sibling must be MANAGED or the sweep asserts below
    // pass vacuously.
    engine->setActiveScreens(
        {QStringLiteral("S1"), QStringLiteral("S1/vs:0"), QStringLiteral("S10"), QStringLiteral("S2")});
    engine->windowOpened(QStringLiteral("app|p"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|v"), QStringLiteral("S1/vs:0"), 0, 0);
    engine->windowOpened(QStringLiteral("app|s10"), QStringLiteral("S10"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|p"), true, QStringLiteral("S1"));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|v")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|s10")));

    // The prune hands the windows back the same way the screens-set sweep
    // does — they are alive, only their output is gone — so the daemon's
    // restore consumers hear one windowsReleased naming both the physical
    // screen and the swept sub-screen.
    QSignalSpy releasedSpy(engine, &ScrollEngine::windowsReleased);
    engine->pruneStatesForRemovedScreen(QStringLiteral("S1"));
    QCOMPARE(releasedSpy.count(), 1);
    const QStringList released = releasedSpy.first().at(0).toStringList();
    QCOMPARE(released.size(), 2);
    QVERIFY(released.contains(QStringLiteral("app|p")));
    QVERIFY(released.contains(QStringLiteral("app|v")));
    const auto releasedScreens = releasedSpy.first().at(1).value<QSet<QString>>();
    QCOMPARE(releasedScreens, (QSet<QString>{QStringLiteral("S1"), QStringLiteral("S1/vs:0")}));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|p")));
    QVERIFY(!engine->isModeSpecificFloated(QStringLiteral("app|p")));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|v")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|s10")));

    engine->setCurrentActivity(QStringLiteral("actB"));
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S2"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S2"));
    engine->pruneStatesForActivities({QStringLiteral("actA")});
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|b")));
    QVERIFY(!engine->isModeSpecificFloated(QStringLiteral("app|b")));
    // The activity-less S10 state predates the activity context and stays.
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|s10")));
}

void TestScrollEngineSmoke::stackedTileFloatRoundTripRestoresSlot()
{
    // The stacked arm of the FloatRestore round trip: tileIndex +
    // stackAnchor capture, and the anchor-resolved re-entry through
    // insertWindowIntoColumnAt (every other float test uses a lone
    // column, which takes the insertWindowAt branch instead).
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1")); // b joins a's column

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S1")));
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().columns().at(0).tiles.at(1).windowId, QStringLiteral("app|b"));

    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 1);
    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    // b re-enters ITS slot in a's column, not a fresh column.
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 2);
    QCOMPARE(state->strip().columns().at(0).tiles.at(1).windowId, QStringLiteral("app|b"));

    // Dead-anchor fallback: float b again, close the anchor sibling a —
    // unfloat must open a FRESH column, never splice into a stranger.
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    engine->windowClosed(QStringLiteral("app|a"));
    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    const int bCol = state->strip().columnOfWindow(QStringLiteral("app|b"));
    const int cCol = state->strip().columnOfWindow(QStringLiteral("app|c"));
    QVERIFY(bCol >= 0);
    QVERIFY(bCol != cCol);
    QCOMPARE(state->strip().columns().at(bCol).tiles.size(), 1);
}

void TestScrollEngineSmoke::scheduledRetileRunsUnderEventLoop()
{
    // Pins that a scheduled retile is genuinely DEFERRED to the event loop
    // and then runs under the GUILESS main (uncoverable under the old
    // APPLESS main, where processEvents was a no-op). Driven through
    // windowMinSizeUpdated, the real asynchronous trigger: like the other
    // schedule-only callers (applyPerScreenConfig, clearPerScreenConfig) its
    // whole effect IS the scheduled retile, so a mutation to a direct
    // applyLayout call fails the "nothing yet" arm below. It is the cheapest
    // of them to drive headless — the others need a per-screen override map.
    // Coalescing itself is NOT observable — a second, un-coalesced
    // run would resolve the same rects and the emit-on-change gate would
    // swallow it — so this test deliberately does not claim it.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents(); // drain the open's own apply

    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    // A 900px minimum outgrows the default half-width column (595px on this
    // work area — the proportion is gap-aware), so the retile MUST move the
    // rect and emit.
    engine->windowMinSizeUpdated(QStringLiteral("app|a"), 900, 0);
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), QSize(900, 0));
    QCOMPARE(tiledSpy.count(), 0); // nothing applied yet: the retile is queued
    QCoreApplication::processEvents();
    QCOMPARE(tiledSpy.count(), 1);
    QVERIFY(tiledSpy.last().at(0).toString().contains(QStringLiteral("\"width\":900")));
}

void TestScrollEngineSmoke::removedScreenReleasesWindows()
{
    // The daemon-facing hand-back contract: shrinking the screen set
    // releases the leaving screen's windows in one windowsReleased with
    // the screen named, and drops this engine's tracking for them.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S2"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S2"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S2"));

    QSignalSpy releasedSpy(engine, &ScrollEngine::windowsReleased);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(releasedSpy.count(), 1);
    const QStringList released = releasedSpy.first().at(0).toStringList();
    QVERIFY(released.contains(QStringLiteral("app|a")));
    QVERIFY(released.contains(QStringLiteral("app|b")));
    QVERIFY(releasedSpy.first().at(1).value<QSet<QString>>().contains(QStringLiteral("S2")));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|a")));
    // The mode-specific float marker must SURVIVE the release. The daemon's
    // windowsReleased handler is its consumer: it reads isModeSpecificFloated
    // to decide whether the window still needs its snap float cleared and its
    // snap slot restored, and clears the marker itself per window. Clearing it
    // here reported every scroll-floated window as not-floated, so the window
    // stayed floated at its scroll-float geometry on the return to snapping.
    // AutotileEngine documents the same contract in
    // releaseScreenStateForTeardown.
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|b")));
    engine->clearModeSpecificFloatMarker(QStringLiteral("app|b"));
    QVERIFY(!engine->isModeSpecificFloated(QStringLiteral("app|b")));
}

void TestScrollEngineSmoke::desktopSwitchAwayPreservesSiblingContextStrips()
{
    // Per-context modes make a screen LEAVE the scrolling set on every
    // switch to a non-scrolling desktop. The daemon pushes the new desktop
    // BEFORE re-deriving the sets, so the removal must prune only the
    // (new, stateless) current context — tearing down every context here
    // destroyed the other desktop's strip structure (consumed stacks) and
    // released its windows, and the switch back rebuilt a fresh
    // one-window-per-column strip.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    // Stack b into a's column.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|b")), 0);

    // Desktop 2 is snapping-mode: context first, then the screen leaves.
    QSignalSpy releasedSpy(engine, &ScrollEngine::windowsReleased);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    engine->setActiveScreens({});
    QCOMPARE(releasedSpy.count(), 0); // a plain switch releases nothing
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|a")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|b")));

    // Switch back: the stacked column resumes as the user left it.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|a")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|b")), 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             QStringList({QStringLiteral("app|a"), QStringLiteral("app|b")}));
}

void TestScrollEngineSmoke::seedAdoptionClampsViewToStripEnd()
{
    // Mode-transition seed, arrivals in REVERSE seed order (the focused
    // window announces first and becomes the active column; every earlier
    // window then inserts to its LEFT). The anchor is active-relative, so
    // without the insert-time clamp the view drifted past the strip's end:
    // the active column sat pinned at the LEFT edge with every other
    // column parked off-screen and dead space on the right (the "toggle
    // into scrolling shows one window" bug). Work area 1200x800, gap 0,
    // three 600px half columns: the clamped view must show the active
    // column c at x=600 with its left neighbour b visible at x=0.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setInitialWindowOrder(QStringLiteral("S1"),
                                  {QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")});
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);

    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             QStringList({QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")}));
    QCOMPARE(engine->lastManagedRect(QStringLiteral("app|c")).x(), 600);
    QCOMPARE(engine->lastManagedRect(QStringLiteral("app|b")).x(), 0);
}

void TestScrollEngineSmoke::parkingAvoidsAdjacentOutputs()
{
    // "Just outside the right edge" is only off-screen when nothing sits
    // there: with a monitor to the right, a right-parked column lands
    // visibly ON that monitor and KWin reassigns the window's output (the
    // dolphin-on-the-second-monitor bug). With a right neighbour, the park
    // must fall to the free LEFT side instead.
    struct RightNeighbourResolver : PhosphorEngine::ICrossSurfaceResolver
    {
        QString neighborOutputInDirection(const QString&, const QString& direction) const override
        {
            return direction == QLatin1String("right") ? QStringLiteral("S2") : QString();
        }
        int neighborDesktopInDirection(int, const QString&) const override
        {
            return 0;
        }
    } resolver;

    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    // View shows [b, c]; focusing the first column scrolls to [a, b] and c
    // would naturally park off the RIGHT edge — occupied, so it parks left.
    engine->focusColumnFirst(QStringLiteral("S1"));
    const QRect parked = engine->lastManagedRect(QStringLiteral("app|c"));
    QVERIFY2(parked.right() < 0, qPrintable(QStringLiteral("expected left park, got x=%1").arg(parked.x())));
    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineSmoke::modeRoundTripRestoresStripStructure()
{
    // Cycling the CURRENT context away from Scrolling and back must rebuild
    // the strip the user left — consumed stacks, adjusted widths — not one
    // default-width column per window. The reassignment prune stashes the
    // structure; re-adoption restores it even with scrambled arrivals.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1")); // b joins a's stack
    engine->adjustColumnWidth(10.0, QStringLiteral("S1")); // 600px + 10% of 1200 = 720px
    QCOMPARE(engine->lastManagedRect(QStringLiteral("app|a")).width(), 720);

    // Mode reassignment of the SAME context (no desktop change): teardown.
    engine->setActiveScreens({});
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|a")));

    // Cycle back; arrivals scrambled relative to the stashed order.
    engine->setActiveScreens({QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);

    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|a")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|b")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|c")), 1);
    QCOMPARE(engine->lastManagedRect(QStringLiteral("app|a")).width(), 720);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             QStringList({QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")}));
}

void TestScrollEngineSmoke::operationScreenFallbackIsDeterministic()
{
    // With no active screen, a screen-less operation must land on the
    // lexicographic minimum of the active set — QSet iteration order is
    // unspecified, so reverting the deterministic pick is invisible to a
    // casual repro but not to this assertion.
    QObject owner;
    auto* engine = new ScrollEngine(nullptr, nullptr, &owner);
    engine->setActiveScreens({QStringLiteral("S2"), QStringLiteral("S1")});
    engine->setWindowFloat(QStringLiteral("app|f"), false, QString());
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("app|f")), QStringLiteral("S1"));
}

void TestScrollEngineSmoke::minSizeSeedsAndCarries()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);

    // windowOpened's min args land in the strip and surface through the
    // IPlacementEngine::windowMinimumSize override.
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 500, 400);
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), QSize(500, 400));
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|nope")), QSize());

    // handoffReceive seeds ctx.minSize so the first relayout clamps
    // without a refuse/re-discover round trip.
    PhosphorEngine::IPlacementEngine::HandoffContext ctx;
    ctx.windowId = QStringLiteral("app|x");
    ctx.toScreenId = QStringLiteral("S2");
    ctx.fromEngineId = QStringLiteral("autotile");
    ctx.minSize = QSize(640, 360);
    engine->handoffReceive(ctx);
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|x")), QSize(640, 360));

    // Partial min (height only) still seeds — the guard is an OR, and a
    // client can legitimately constrain a single axis.
    PhosphorEngine::IPlacementEngine::HandoffContext partial;
    partial.windowId = QStringLiteral("app|y");
    partial.toScreenId = QStringLiteral("S2");
    partial.fromEngineId = QStringLiteral("autotile");
    partial.minSize = QSize(0, 360);
    engine->handoffReceive(partial);
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|y")), QSize(0, 360));
}

void TestScrollEngineSmoke::minSizeSurvivesFloatRoundTrip()
{
    // I25 regression: FloatRestore carries the min size, so a float→unfloat
    // round trip keeps the relayout clamps without waiting for the
    // compositor to re-report.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 500, 400);
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), QSize(500, 400));
    engine->setWindowFloat(QStringLiteral("app|a"), true, QStringLiteral("S1"));
    engine->setWindowFloat(QStringLiteral("app|a"), false, QStringLiteral("S1"));
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), QSize(500, 400));
}

void TestScrollEngineSmoke::handoffReleaseClearsFloatMarker()
{
    // The mode-transition float marker must not outlive this engine's
    // tracking (the daemon's release handler reads it).
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|f"), QStringLiteral("S1"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|f"), true, QStringLiteral("S1"));
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|f")));
    engine->handoffRelease(QStringLiteral("app|f"));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|f")));
    QVERIFY(!engine->isModeSpecificFloated(QStringLiteral("app|f")));
}

void TestScrollEngineSmoke::applyPathEmitsOnChangeOnly()
{
    // The apply path, driven headless through the geometry-provider seam:
    // windowsTiled fires when rects genuinely move and stays silent on a
    // no-op re-apply; the tab-strip payload is change-gated with the empty
    // latch broadcasting exactly one "[]".
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});

    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    QSignalSpy stripSpy(engine, &ScrollEngine::tabStripsChanged);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QVERIFY(tiledSpy.count() >= 1);
    const int afterOpen = tiledSpy.count();

    // Same-state re-apply: nothing moved, nothing emitted.
    engine->retile(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QCOMPARE(tiledSpy.count(), afterOpen);

    // Tabbed column: the strip payload appears once and does not re-emit
    // byte-identically on a plain retile.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QVERIFY(stripSpy.count() >= 1);
    const int afterTab = stripSpy.count();
    engine->retile(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QCOMPARE(stripSpy.count(), afterTab);

    // Closing the last window latches ONE empty broadcast.
    engine->windowClosed(QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    QVERIFY(stripSpy.count() > afterTab);
    QCOMPARE(stripSpy.last().at(1).toString(), QStringLiteral("[]"));
    // "Exactly one": a further retile on the empty screen must not
    // re-broadcast the latched empty payload.
    const int afterEmpty = stripSpy.count();
    engine->retile(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QCOMPARE(stripSpy.count(), afterEmpty);
}

void TestScrollEngineSmoke::orderedOpenFollowsAndConsumesSeed()
{
    // The mode-transition seed path: with a captured order in hand, arrivals
    // land at a seeded strip position instead of next-to-focus, and each id
    // is consumed so a later open of the SAME id is placed normally.
    //
    // Reverse arrivals pin that the SEED BRANCH is taken at all (a
    // next-to-focus append would give c, b, a); the forward-arrival test
    // below pins the earlier-id column accounting.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setInitialWindowOrder(QStringLiteral("S1"),
                                  {QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")});

    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")}));

    // Seed FULLY consumed, so the screen's entry was dropped outright: the
    // re-open takes the ordinary next-to-focus path. Focus has sat on c
    // since it arrived into the empty strip (insertWindowAt does not
    // refocus, it only shifts the active index), so c is where the re-opened
    // a lands beside.
    engine->windowClosed(QStringLiteral("app|a"));
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|b"), QStringLiteral("app|c"), QStringLiteral("app|a")}));
}

void TestScrollEngineSmoke::partiallyConsumedSeedGuardsReopens()
{
    // The consumed-id guard, on the only screen state where it can fire: a
    // PARTIALLY consumed seed, whose entry is still installed because the
    // remaining ids have not arrived. (The full-consume case drops the entry
    // first, so the guard never runs there.) A re-open of an already-consumed
    // id must take the ordinary next-to-focus path, not the seeded position
    // its stale list entry still names.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setInitialWindowOrder(QStringLiteral("S1"),
                                  {QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")});

    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    // c never arrives, so the entry survives with a and b marked consumed.
    engine->windowClosed(QStringLiteral("app|a"));
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    // Next to the focused column (b), NOT back at the seeded column 0.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|b"), QStringLiteral("app|a")}));

    // Re-seeding the same screen RESETS the consumed set, so the very same
    // re-open is seed-positioned again — the guard keys on the current seed
    // generation, not on the id having ever been placed.
    engine->setInitialWindowOrder(QStringLiteral("S1"),
                                  {QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")});
    engine->windowClosed(QStringLiteral("app|a"));
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|a"), QStringLiteral("app|b")}));
}

void TestScrollEngineSmoke::orderedOpenForwardArrivalsKeepSeedOrder()
{
    // Forward arrivals are the accounting-sensitive sequence: each arrival
    // must COUNT its already-arrived seeded neighbours to land after them.
    // A consume that shrank the seed list would zero every index and build
    // the strip reversed (c, b, a) — the regression this pins.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setInitialWindowOrder(QStringLiteral("S1"),
                                  {QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")});

    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")}));

    // Out-of-order tail: a fresh seed {d,e,f} arriving d, f, e around the
    // existing strip. The middle arrival is what discriminates — e must
    // count d (present) but not f (already present and LATER in the seed),
    // landing between them. Two ids would not: any placement of the second
    // arrival that respects the first is a pass.
    engine->setInitialWindowOrder(QStringLiteral("S1"),
                                  {QStringLiteral("app|d"), QStringLiteral("app|e"), QStringLiteral("app|f")});
    engine->windowOpened(QStringLiteral("app|d"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|f"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|e"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|d"), QStringLiteral("app|e"), QStringLiteral("app|f"),
                          QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")}));
}

void TestScrollEngineSmoke::floatedOpenConsumesSeed()
{
    // consumePendingInitialOrder on the FLOATED-arrival path: a rule-floated
    // window never reaches the strip, so only the explicit consume call
    // clears its seed entry. Leaving it behind would re-position the window
    // whenever it later opens tiled — which is exactly what the re-open at
    // the end would expose (a surviving seed puts it at column 0).
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setFloatPredicate([](const QString& windowId, const QString&) {
        return windowId == QStringLiteral("app|f");
    });
    engine->setInitialWindowOrder(QStringLiteral("S1"), {QStringLiteral("app|f")});

    engine->windowOpened(QStringLiteral("app|t"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|f"), QStringLiteral("S1"), 0, 0);
    QVERIFY(engine->isWindowFloatingInScroll(QStringLiteral("app|f")));
    // The rule float is this engine's own decision, so it carries the
    // mode marker: without it the daemon captures the scroll float into the
    // snap slot at the next mode transition.
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|f")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("app|t")}));

    engine->windowClosed(QStringLiteral("app|f"));
    engine->setFloatPredicate({});
    engine->windowOpened(QStringLiteral("app|f"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|t"), QStringLiteral("app|f")}));
}

void TestScrollEngineSmoke::migrateOutAnnouncesDroppedFloat()
{
    // Both migrate-out paths drop the float bit, and both must SAY so: a
    // silent drop leaves signal-driven subscribers (the effect's FloatingCache)
    // believing the window still floats while it is tiled on the new screen,
    // which they later resolve as a float-back.
    //
    // The announcement rides windowFloatingStateSynced. A migration is the
    // engine's own bookkeeping, not a user float action, so the daemon must
    // mirror the state without restoring geometry or raising an OSD; only the
    // explicit setWindowFloat below is an active transition.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    QSignalSpy floatSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
    QSignalSpy activeFloatSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);

    // windowOpened's context migration.
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|a"), true, QStringLiteral("S1"));
    QCOMPARE(activeFloatSpy.count(), 1); // the user float is the ACTIVE arm
    QCOMPARE(floatSpy.count(), 0);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S2"), 0, 0);
    QCOMPARE(floatSpy.count(), 1);
    QCOMPARE(activeFloatSpy.count(), 1); // the migration must not reach it
    QCOMPARE(floatSpy.last().at(0).toString(), QStringLiteral("app|a"));
    QCOMPARE(floatSpy.last().at(1).toBool(), false);
    QCOMPARE(floatSpy.last().at(2).toString(), QStringLiteral("S1")); // the screen it LEFT
    QVERIFY(!engine->isWindowFloatingInScroll(QStringLiteral("app|a")));
    QVERIFY(engine->isWindowTiled(QStringLiteral("app|a")));
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("app|a")), QStringLiteral("S2"));

    // handoffReceive's defence-in-depth migration of a window still held by
    // another scroll context.
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    const int beforeHandoff = floatSpy.count();
    PhosphorEngine::IPlacementEngine::HandoffContext ctx;
    ctx.windowId = QStringLiteral("app|b");
    ctx.toScreenId = QStringLiteral("S2");
    ctx.fromEngineId = QStringLiteral("snap");
    engine->handoffReceive(ctx);
    QCOMPARE(floatSpy.count(), beforeHandoff + 1);
    QCOMPARE(floatSpy.last().at(0).toString(), QStringLiteral("app|b"));
    QCOMPARE(floatSpy.last().at(1).toBool(), false);
    QCOMPARE(floatSpy.last().at(2).toString(), QStringLiteral("S1"));
    QVERIFY(engine->isWindowTiled(QStringLiteral("app|b")));
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("app|b")), QStringLiteral("S2"));
}

void TestScrollEngineSmoke::contextSwitchFlagRidesChangedScreenSets()
{
    // The CHANGED-set arm of the same contract screensSetLifecycle pins for
    // the identical-set arm: a desktop/activity switch whose per-desktop
    // assignments also change the screen set must still report
    // isDesktopSwitch=true (the effect skips its destructive geometry/border
    // restore for the departing screens on a switch), and the flag is
    // consumed there too.
    QObject owner;
    auto* engine = new ScrollEngine(nullptr, nullptr, &owner);
    QSignalSpy screensSpy(engine, &ScrollEngine::scrollingScreensChanged);

    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(screensSpy.count(), 1);
    QCOMPARE(screensSpy.last().at(1).toBool(), false); // no switch armed

    engine->setCurrentDesktop(1);
    engine->setCurrentDesktop(2); // armSwitch needs an established context first
    engine->setActiveScreens({QStringLiteral("S1"), QStringLiteral("S2")});
    QCOMPARE(screensSpy.count(), 2);
    QCOMPARE(screensSpy.last().at(1).toBool(), true);

    // Consumed: the next changed set is a plain screen-assignment change.
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(screensSpy.count(), 3);
    QCOMPARE(screensSpy.last().at(1).toBool(), false);

    // An ACTIVITY switch arms the same flag (first non-empty push only
    // establishes the context, so it takes two), and here the set is
    // identical — the re-emit arm carries it and then goes quiet.
    engine->setCurrentActivity(QStringLiteral("actA"));
    engine->setCurrentActivity(QStringLiteral("actB"));
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(screensSpy.count(), 4);
    QCOMPARE(screensSpy.last().at(1).toBool(), true);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(screensSpy.count(), 4);
}

void TestScrollEngineSmoke::zoneNumbersAreViewportRelativeVisibleSlots()
{
    // Zone numbers are VIEWPORT-relative visible TILE slots, not strip
    // indices: tiles are numbered sequentially in strip order (columns left
    // to right, tiles top to bottom), the leftmost on-screen tile is 1
    // regardless of how many columns are parked off-screen to its left, and
    // a parked column's tiles have no number at all. visibleTiles is the
    // single source: the preview rect walk, the per-window query and the
    // Snap-to-Zone digit target all derive from it, so every visible window
    // carries its own distinct number and they can never disagree.
    //
    // Work area is 1200 wide and the default column is 600px (the proportion is
    // gap-aware and no IScrollSettings is attached, so innerGap is 0), so
    // exactly two columns fit and a third must be off-screen.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")).size(), 3);

    // Exactly one of the three is parked, and the two on screen carry 1 and 2.
    const int na = engine->visibleTileNumberForWindow(QStringLiteral("S1"), QStringLiteral("app|a"));
    const int nb = engine->visibleTileNumberForWindow(QStringLiteral("S1"), QStringLiteral("app|b"));
    const int nc = engine->visibleTileNumberForWindow(QStringLiteral("S1"), QStringLiteral("app|c"));
    QList<int> numbers{na, nb, nc};
    QCOMPARE(numbers.count(-1), 1); // one parked
    std::sort(numbers.begin(), numbers.end());
    QCOMPARE(numbers, (QList<int>{-1, 1, 2})); // and the visible pair is 1-based

    // The rect walk agrees with the per-window query: one rect per visible
    // tile, in the same left-to-right zone-number order.
    const QVector<QRect> rects = engine->visibleTileRects(QStringLiteral("S1"));
    QCOMPARE(rects.size(), 2);

    // An unknown window has no slot, and neither does a window on a screen the
    // engine does not manage.
    QCOMPARE(engine->visibleTileNumberForWindow(QStringLiteral("S1"), QStringLiteral("app|nope")), -1);
    QCOMPARE(engine->visibleTileNumberForWindow(QStringLiteral("S9"), QStringLiteral("app|a")), -1);

    // Stacking two windows into ONE column: every visible tile still gets
    // its own distinct number, in strip order. Three visible tiles across
    // two columns number 1..3 — the stacked pair does NOT collapse onto a
    // shared column ordinal (per-column numbering was the old model; it
    // rendered duplicate labels in every preview).
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    const QVector<ScrollEngine::VisibleTile> stacked = engine->visibleTiles(QStringLiteral("S1"));
    QCOMPARE(stacked.size(), 3);
    // Tile numbers are the list order, so pin the per-window query to it:
    // each visible window's number is its 1-based index in the walk.
    for (int i = 0; i < stacked.size(); ++i) {
        QCOMPARE(engine->visibleTileNumberForWindow(QStringLiteral("S1"), stacked.at(i).windowId), i + 1);
    }
    // The stacked pair shares a column, and the walk is column-major: the
    // single column's tile comes first, then the pair back to back.
    QVERIFY(stacked.at(0).columnIndex != stacked.at(1).columnIndex);
    QCOMPARE(stacked.at(1).columnIndex, stacked.at(2).columnIndex);

    // The normalized twin walks the same tiles, and every rect is inside
    // the unit square.
    const QVector<QRectF> relative = engine->visibleTileRectsRelative(QStringLiteral("S1"));
    QCOMPARE(relative.size(), stacked.size());
    for (const QRectF& r : relative) {
        QVERIFY(r.left() >= 0.0 && r.top() >= 0.0);
        QVERIFY(r.right() <= 1.0 + 1e-9 && r.bottom() <= 1.0 + 1e-9);
    }
}

void TestScrollEngineSmoke::centeredColumnKeepsNeighboursFullRects()
{
    // Centering the active column pushes its neighbours PARTLY off both edges
    // of the viewport. The engine must commit their TRUE rects, overhang
    // included: clamping the geometry to the work area was tried and rejected
    // (it resized the windows). The overhang is prevented from rendering on
    // the neighbouring output by the compositor effect, which skips strip
    // windows in foreign outputs' paint passes (paint_pipeline.cpp) — the
    // window keeps its full size and its drawing stops at the monitor edge.
    //
    // Work area is 1200 wide with 600px columns: centering the middle one
    // puts it at 300..900, leaving the outer two at -300..300 and 900..1500.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);

    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    engine->centerColumn(QStringLiteral("S1"));

    const QRect left = engine->lastManagedRect(QStringLiteral("app|a"));
    const QRect middle = engine->lastManagedRect(QStringLiteral("app|b"));
    const QRect right = engine->lastManagedRect(QStringLiteral("app|c"));

    // The centred column is fully on screen.
    QCOMPARE(middle, QRect(300, 0, 600, 800));

    // The neighbours keep their FULL 600px width, straddling the edges. A
    // clamped left of (0, 0, 300, 800) is exactly the rejected resize.
    QCOMPARE(left, QRect(-300, 0, 600, 800));
    QCOMPARE(right, QRect(900, 0, 600, 800));
}

// GUILESS (not APPLESS): a QCoreApplication provides the event
// dispatcher the coalesced scheduleRetileForScreen and the prunes'
// deleteLater need — under APPLESS every processEvents() in this file was
// silently a no-op and the coalescing path had zero coverage.
QTEST_GUILESS_MAIN(TestScrollEngineSmoke)
#include "test_scrollengine_smoke.moc"
