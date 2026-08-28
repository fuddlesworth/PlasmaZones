// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// FILE-SIZE EXCEPTION (sanctioned): this file is around 2000 lines, past the
// 1150 hard ceiling.
//
// The case for it: the split-by-concern work the rule asks for has already
// been done. Eight siblings carry the rest of the suite (enumerated below),
// each owning a coherent concern, and what remains here is the core smoke
// path — tracking, ordering, float state, capture, context teardown, handoff.
// Splitting that residue again would divide one narrative across two files
// without giving either a concern of its own, and a reader following an
// engine regression would then have to know which half to open.
//
// Reviewed at the same time as the file's other exception-worthy neighbours;
// if a ninth concern emerges, it takes a sibling rather than growing this.

// Headless ScrollEngine smoke test: tracking, ordering, float state, capture,
// context teardown, and handoff semantics.
//
// The engine tolerates a missing ScreenManager and tracking service, so most
// fixtures here pass none. That is not the same as "no dependencies": the
// tests that need real rects (parking, the emit-on-change gate, the scheduled
// retile) wire the geometry-provider seam instead, and the strip geometry they
// assert on is the engine's own, not the strip model's.
//
// Eight siblings carry the rest of the suite, split off at this file's size
// ceiling: test_scrollengine_persistence.cpp owns the stash focus/anchor carry
// and the serialize/restore blob, test_scrollengine_zonenumbers.cpp owns the
// zone-number walk and the verbs that address it, test_scrollengine_perscreen
// owns the per-screen override resolution, test_scrollengine_draginsert owns
// the drag-insert state machine, test_scrollengine_boundary.cpp owns the
// screen-boundary contract (the straddler clamp, the park peek floor, and
// crop mode), test_scrollengine_verbs.cpp owns the niri-parity verb
// vocabulary (column focus polarity, tile-end focus, absolute width/height
// intents, the float moves and the layer switch),
// test_scrollengine_behaviour.cpp owns the per-screen BEHAVIOUR overrides,
// and test_scrollengine_snapshot.cpp owns stripSnapshot and its index
// contract.

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

namespace Ax = ScrollTestUtils::Ax;

using ScrollTestUtils::defaultScreenRect;
using ScrollTestUtils::makeProviderEngine;

class TestScrollEngineSmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

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
    void columnMaximizeFlagRidesEveryTileOfTheColumn();
    void minSizeOutgrowingWorkAreaFloatsTheWindow();
    void removedScreenReleasesWindows();
    void desktopSwitchAwayPreservesSiblingContextStrips();
    void seedAdoptionClampsViewToStripEnd();
    void parkingAvoidsNeighbourOutputs();
    void parkingReportsDepartureEdge();
    void viewDeltaCarriesOnScreenTilesOnly();
    void viewDeltaIsSuppressedAcrossAWorkAreaChange();
    void secondScrollMeasuresFromTheEmittedBaselineOnly();
    void aWidthChangeKeepsTheResizedColumnInTheBatch();
    void aDepartingColumnKeepsItsTabIndicator();
    void onlyAStripDepartedTileCarriesAVisualPosition();
    void aTabSwitchNamesTheTabItReplaces();
    void modeRoundTripRestoresStripStructure();
    void operationScreenFallbackIsDeterministic();
    void minSizeSeedsAndCarries();
    void minSizeSurvivesFloatRoundTrip();
    void handoffReleaseClearsFloatMarker();
    void applyPathEmitsOnChangeOnly();
    void orderedOpenFollowsAndConsumesSeed();
    void partiallyConsumedSeedGuardsReopens();
    void modeTransitionFocusSeedAnchorsArrival();
    void modeTransitionFocusSeedIsDrainedByItsBurst();
    void modeTransitionFocusSeedDropsWhenScreenLeaves();
    void modeTransitionFocusSeedDropsOnMidBurstContextSwitch();
    void modeTransitionFocusSeedSurvivesAStraddlingBurst();
    void perOutputDesktopSurvivesScrollingSetLeave();
    void orderedOpenForwardArrivalsKeepSeedOrder();
    void floatedOpenConsumesSeed();
    void tileFlaggedFloatingBySiblingEngineSyncsClear();
    void migrateOutAnnouncesDroppedFloat();
    void contextSwitchFlagRidesChangedScreenSets();

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

    /// The state for a screen, or nullptr. QVERIFY'd at every call site: a
    /// regression that drops the state segfaults the whole binary otherwise,
    /// taking the remaining tests' results with it.
    static ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screenId));
    }
};

void TestScrollEngineSmoke::screensSetLifecycle()
{
    QObject owner;
    auto* engine = new ScrollEngine(nullptr, nullptr, &owner);
    QVERIFY(!engine->isEnabled());
    // Capability contract the daemon's layout-selection gates rest on: the
    // strip consumes layouts as sizing TEMPLATES, never as placement (snap
    // and autotile answer Placement). One call, not two: layoutSupport is
    // virtual, so an added base-pointer cast would dispatch through the
    // identical vtable slot and prove nothing.
    using LayoutSupport = PhosphorEngine::IPlacementEngine::LayoutSupport;
    QCOMPARE(engine->layoutSupport(), LayoutSupport::Templates);
    QSignalSpy screensSpy(engine, &ScrollEngine::scrollingScreensChanged);

    QSignalSpy enabledSpy(engine, &ScrollEngine::enabledChanged);
    engine->setActiveScreens({QStringLiteral("S2"), QStringLiteral("S1")});
    QVERIFY(engine->isEnabled());
    QVERIFY(engine->isActiveOnScreen(QStringLiteral("S1")));
    QCOMPARE(screensSpy.count(), 1);
    // The wire payload is SORTED (consumers compare successive payloads).
    QCOMPARE(screensSpy.last().at(0).toStringList(), (QStringList{QStringLiteral("S1"), QStringLiteral("S2")}));
    QCOMPARE(enabledSpy.count(), 1);
    // A genuine shrink emits — asserted AT the shrink, not inferred from the
    // count two blocks later: with the emit-on-change gate inverted, the
    // identical-set assertion below would read this missing emission as the
    // no-op it is testing for and pass.
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(screensSpy.count(), 2);
    QCOMPARE(screensSpy.last().at(0).toStringList(), (QStringList{QStringLiteral("S1")}));

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

    // The emptying push itself emits — asserted rather than only sampled
    // afterwards, or a regression that stopped emitting on this edge would
    // pass on the equality below.
    engine->setActiveScreens({});
    QCOMPARE(screensSpy.count(), 4);
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

// Named for handoffRelease but covers the RELEASE-then-RECEIVE round trip:
// the release half asserts the window leaves tracking without disturbing its
// neighbour, and the receive half asserts it comes back tracked on the new
// screen. Kept as one slot because the two halves only mean anything
// together — a release that drops tracking is only correct if something can
// pick the window back up.
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

    // The state is re-acquired per call (a float round trip can rebuild it)
    // and reported as Normal when it is gone, so a regression that drops the
    // state fails the display assertion instead of dereferencing null and
    // taking the rest of the binary with it.
    auto columnDisplayOf = [engine](const QString& windowId) {
        ScrollState* state = stateFor(engine, QStringLiteral("S1"));
        if (!state) {
            return ColumnDisplay::Normal;
        }
        const int col = state->strip().columnOfWindow(windowId);
        return col >= 0 ? state->strip().columns().at(col).display : ColumnDisplay::Normal;
    };
    QVERIFY(stateFor(engine, QStringLiteral("S1")));
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

    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().columns().at(0).tiles.at(1).windowId, QStringLiteral("app|b"));

    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    state = stateFor(engine, QStringLiteral("S1")); // re-acquired: a float can rebuild the state
    QVERIFY(state);
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 1);
    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    // b re-enters ITS slot in a's column, not a fresh column.
    state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 2);
    QCOMPARE(state->strip().columns().at(0).tiles.at(1).windowId, QStringLiteral("app|b"));

    // Dead-anchor fallback: float b again, close the anchor sibling a —
    // unfloat must open a FRESH column, never splice into a stranger.
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    engine->windowClosed(QStringLiteral("app|a"));
    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    const int bCol = state->strip().columnOfWindow(QStringLiteral("app|b"));
    const int cCol = state->strip().columnOfWindow(QStringLiteral("app|c"));
    QVERIFY(bCol >= 0);
    QVERIFY(bCol != cCol);
    QCOMPARE(state->strip().columns().at(bCol).tiles.size(), 1);
}

void TestScrollEngineSmoke::columnMaximizeFlagRidesEveryTileOfTheColumn()
{
    // The columnMaximized wire flag: it must ride EVERY tile of a maximized
    // column (the wire is per window and the effect has no column identity to
    // hang a per-column flag on), must not leak onto a sibling column, and
    // must clear on the toggle back.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    // Two tiles in ONE column, so the "every tile" half of the claim has
    // something to bite on: a single-tile column would pass a per-column
    // emit just as well.
    // Focus the LEADING column first: consume pulls the NEXT column's window
    // into the active one, so consuming while the trailing column is already
    // active has nothing to take and leaves two single-tile columns.
    engine->focusColumnFirst(QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    {
        const auto* st = stateFor(engine, QStringLiteral("S1"));
        QVERIFY(st);
        QCOMPARE(st->strip().columnCount(), 1);
        QCOMPARE(st->strip().columns().at(0).tiles.size(), 2);
    }

    const auto flagsByWindow = [](const QSignalSpy& spy) {
        QHash<QString, bool> out;
        const QJsonArray batch = QJsonDocument::fromJson(spy.last().at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : batch) {
            const QJsonObject o = v.toObject();
            out.insert(o.value(QLatin1String("windowId")).toString(),
                       o.value(QLatin1String("columnMaximized")).toBool(false));
        }
        return out;
    };

    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->toggleMaximizeColumn(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QVERIFY(tiled.count() > 0);
    QHash<QString, bool> flags = flagsByWindow(tiled);
    QVERIFY2(flags.contains(QStringLiteral("app|a")), "the batch must carry the maximized column's first tile");
    QVERIFY2(flags.contains(QStringLiteral("app|b")), "the batch must carry the maximized column's second tile");
    QVERIFY2(flags.value(QStringLiteral("app|a")), "columnMaximized must ride the first tile");
    QVERIFY2(flags.value(QStringLiteral("app|b")), "columnMaximized must ride the SECOND tile too");

    // Toggle back: the flag is absent again, so the effect's Release arm has
    // something to fire on. Absence rather than an explicit false — the emit
    // is gated on the flag being set, the way windowedFullscreen is.
    tiled.clear();
    engine->toggleMaximizeColumn(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QVERIFY(tiled.count() > 0);
    flags = flagsByWindow(tiled);
    QVERIFY2(!flags.value(QStringLiteral("app|a"), false), "un-maximizing must drop the flag");
    QVERIFY2(!flags.value(QStringLiteral("app|b"), false), "un-maximizing must drop it on every tile");
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
    // A 900px minimum outgrows the default half-width column (600px here:
    // the proportion is gap-aware, and this fixture attaches no
    // IScrollSettings so the inner gap is 0 — the 595px figure belongs to
    // the strip-ops fixture, which sets a 10px gap), so the retile MUST move
    // the rect and emit.
    const QSize min900 = Ax::t(QSize(900, 0));
    engine->windowMinSizeUpdated(QStringLiteral("app|a"), min900.width(), min900.height());
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), min900);
    QCOMPARE(tiledSpy.count(), 0); // nothing applied yet: the retile is queued
    QCoreApplication::processEvents();
    QCOMPARE(tiledSpy.count(), 1);
    // Parsed and looked up BY WINDOW, the way every sibling batch assertion in
    // this file does. A substring match against the whole batch JSON matched
    // the value on ANY entry, so a retile that widened the wrong window's
    // column passed just as well.
    const QJsonArray batch = QJsonDocument::fromJson(tiledSpy.last().at(0).toString().toUtf8()).array();
    bool sawA = false;
    for (const QJsonValue& v : batch) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("windowId")).toString() != QLatin1String("app|a")) {
            continue;
        }
        sawA = true;
        QCOMPARE(Ax::entryMainLen(o), 900);
    }
    QVERIFY2(sawA, "the batch must carry the window whose minimum grew");
}

void TestScrollEngineSmoke::minSizeOutgrowingWorkAreaFloatsTheWindow()
{
    // windowMinSizeUpdated re-runs insertOpenedWindow's oversized verdict:
    // a client that pins its min size AFTER mapping (the Wine late-pin
    // pattern) must reach the same float the open path would have chosen,
    // instead of staying tiled at a size no column slot can honour. Driven
    // through the provider fixture so the 1200x800 work area is valid — the
    // verdict deliberately skips an invalid one.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 2);

    QSignalSpy floatSpy(engine, &ScrollEngine::windowFloatingChanged);
    // In-bounds growth keeps the window tiled; the column widens instead.
    // Minimums ALONG the strip, so they transpose with the fixture: 900
    // widens the column, 1300 outgrows the work area entirely.
    const QSize min900 = Ax::t(QSize(900, 0));
    engine->windowMinSizeUpdated(QStringLiteral("app|a"), min900.width(), min900.height());
    QVERIFY(!state->isFloating(QStringLiteral("app|a")));
    QCOMPARE(floatSpy.count(), 0);

    // Past the work-area width: the verdict floats it mid-session.
    const QSize min1300 = Ax::t(QSize(1300, 0));
    engine->windowMinSizeUpdated(QStringLiteral("app|a"), min1300.width(), min1300.height());
    state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->isFloating(QStringLiteral("app|a")));
    QVERIFY(!state->strip().containsWindow(QStringLiteral("app|a")));
    QCOMPARE(floatSpy.count(), 1);
    QCOMPARE(floatSpy.last().at(0).toString(), QStringLiteral("app|a"));
    QCOMPARE(floatSpy.last().at(1).toBool(), true);
    // Engine-decided float, so it carries the mode marker (the daemon's
    // mode-transition capture keys off it) and the restore entry holds the
    // clamp that forced the float.
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|a")));
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), min1300);

    // A repeat report is idempotent — already floating, nothing re-fires.
    engine->windowMinSizeUpdated(QStringLiteral("app|a"), min1300.width(), min1300.height());
    QCOMPARE(floatSpy.count(), 1);

    // Shrinking back below the work area does NOT auto-unfloat: the float
    // may have been rearranged, and the manual unfloat restores the slot.
    const QSize min400 = Ax::t(QSize(400, 0));
    engine->windowMinSizeUpdated(QStringLiteral("app|a"), min400.width(), min400.height());
    QVERIFY(state->isFloating(QStringLiteral("app|a")));
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
    // window then inserts on its LEAD side). The anchor is active-relative, so
    // without the insert-time clamp the view drifted past the strip's end:
    // the active column sat pinned at the LEAD edge with every other
    // column parked off-screen and dead space at the trail end (the "toggle
    // into scrolling shows one window" bug). Work area 1200 along the strip by
    // 800 across it, gap 0, three 600px half columns: the clamped view must
    // show the active column c at main 600 with its lead neighbour b visible
    // at main 0.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setInitialWindowOrder(QStringLiteral("S1"),
                                  {QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")});
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);

    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             QStringList({QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")}));
    QCOMPARE(Ax::mainPos(engine->lastManagedRect(QStringLiteral("app|c"))), 600);
    QCOMPARE(Ax::mainPos(engine->lastManagedRect(QStringLiteral("app|b"))), 0);
}

void TestScrollEngineSmoke::parkingAvoidsNeighbourOutputs()
{
    // ONE park rule for every topology: an off-viewport column parks just
    // BELOW the union of all outputs, x kept within its own screen's span.
    // Physical on both axes on purpose — no point below the union belongs to
    // any monitor — so there is no resolver consultation, no per-side
    // preference, and no boxed-in degraded case. The departure side travels
    // separately as scrollEdge (pinned by parkingReportsDepartureEdge), so the
    // park position carries no direction meaning. A resolver reporting a
    // trail-side neighbour is installed here to pin exactly that irrelevance:
    // its presence must not move the park.
    //
    // The two geometries are deliberately DIFFERENT: a 100px panel at each end
    // of the strip makes the work area run 100..1100 along the MAIN axis while
    // the screen still runs 0..1200, and the park bound is the SCREEN rect
    // (provider engines have no ScreenManager, so the union degrades to the
    // parked screen's own rect). Transposed with the fixture, the same way the
    // boundary suite builds its inset: an untransposed adjusted(100, 0, -100,
    // 0) insets the CROSS axis on the vertical arm, which leaves the main
    // extent equal to the screen's and the two geometries no longer tell the
    // work area apart from the screen at all.
    ScrollTestUtils::TrailNeighbourResolver resolver;

    const auto screenGeometry = [](const QString&) {
        return defaultScreenRect();
    };
    const auto panelInsetGeometry = [](const QString&) {
        return Ax::t(QRect(100, 0, 1000, 800));
    };

    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")}, screenGeometry, panelInsetGeometry);
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    // View shows [b, c]; focusing the first column scrolls to [a, b], so c
    // leaves by the TRAIL edge. It parks BELOW the screen regardless.
    const QRect beforePark = engine->lastManagedRect(QStringLiteral("app|c"));
    QVERIFY(beforePark.isValid());
    engine->focusColumnFirst(QStringLiteral("S1"));
    const QRect screen = defaultScreenRect();
    const QRect parked = engine->lastManagedRect(QStringLiteral("app|c"));
    // THE TWO ARMS DISCRIMINATE THROUGH DIFFERENT ASSERTIONS, because the
    // fixture's inset lands on a different physical edge in each. parkTop is
    // unionBottom + 1 + kParkMargin, and the union seeds from the screen rect.
    //
    // VERTICAL arm: screen 800x1200 (bottom 1199), work area inset to
    // y 100..1099. A work-area-derived parkTop would be 1116 and fail THIS
    // assertion. So the park-below-the-screen check is the discriminator here.
    //
    // HORIZONTAL arm: screen 1200x800 (bottom 799), work area inset on x only,
    // so its bottom is ALSO 799 and both derivations give the same parkTop.
    // This assertion cannot tell them apart on that arm; the far-edge pin
    // below does it instead.
    QVERIFY2(parked.top() > screen.bottom(),
             qPrintable(QStringLiteral("expected a park below the screen, got y=%1").arg(parked.y())));
    // parkRect clamps the PHYSICAL x only (it is deliberately physical on both
    // axes). This is a SHAPE pin and nothing more: qBound guarantees the lower
    // half on either arm, and the upper half holds whenever the rect is no
    // wider than the screen, which it never is. Neither arm's screen-vs-work-
    // area claim rests on it.
    QVERIFY2(parked.left() >= screen.left() && parked.right() <= screen.right(),
             qPrintable(QStringLiteral("parked rect must stay within the screen's horizontal span, got x=%1 w=%2")
                            .arg(parked.x())
                            .arg(parked.width())));

    // The far-edge PIN is the HORIZONTAL arm's discriminator, and gated to it
    // deliberately rather than by oversight. c departs along the main axis, so
    // on a horizontal strip it carries an x overhang that the clamp pulls back
    // to the SCREEN's right edge, where a work-area clamp would stop 100px
    // short. On a vertical strip c departs along y instead and its x is simply
    // the work area's own span, so there is no overhang, the clamp never
    // engages, and asserting the pin there would be true by construction and
    // prove nothing — which is why the vertical arm leans on the park-below
    // check above instead.
    if (!Ax::vertical()) {
        QCOMPARE(parked.right(), screen.right());
    }
    // parkRect MOVES; it never resizes. A park that trimmed the rect to fit
    // would satisfy the containment above.
    QCOMPARE(parked.size(), beforePark.size());
    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineSmoke::parkingReportsDepartureEdge()
{
    // The companion to parkingAvoidsNeighbourOutputs: wherever a column ends
    // up parked, the tile request must still name the edge it actually left
    // by, because that is what the effect animates from. These two properties
    // used to be one number (the park position), which is exactly why they
    // could not both be satisfied on an adjacent monitor pair. Under the
    // current contract the park position carries NO direction at all: it lands
    // below the union of every output no matter which end the column left by,
    // and a neighbour cannot move it. This test installs a neighbour and pins
    // both halves anyway — the park below the union AND the trail-edge report
    // that travels beside it.
    ScrollTestUtils::TrailNeighbourResolver resolver;

    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCrossSurfaceResolver(&resolver);
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->focusColumnFirst(QStringLiteral("S1"));
    QVERIFY(!tiled.isEmpty());

    // Read the LAST batch: it is the one the focus move produced.
    const QJsonArray batch = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    QJsonObject cEntry;
    for (const QJsonValue& v : batch) {
        if (v.toObject().value(QLatin1String("windowId")).toString() == QLatin1String("app|c")) {
            cEntry = v.toObject();
        }
    }
    QVERIFY2(!cEntry.isEmpty(), "expected app|c in the tile batch");
    QCOMPARE(cEntry.value(QLatin1String("scrollEdge")).toString(), Ax::edgeTrail());
    // Parked BELOW the union (position carries no direction) while reporting
    // the TRAIL edge as data. Asserting BOTH is the point: the pair is what
    // the old single-number design could not express.
    QVERIFY2(cEntry.value(QLatin1String("y")).toInt() > defaultScreenRect().bottom(),
             "expected the park itself to sit below the outputs");

    // app|a was parked off the LEAD end before this move (the view opened on
    // [b, c]) and has just scrolled in, so it reports the edge it arrives
    // from. Same rule as c, opposite direction.
    QJsonObject aEntry;
    for (const QJsonValue& v : batch) {
        if (v.toObject().value(QLatin1String("windowId")).toString() == QLatin1String("app|a")) {
            aEntry = v.toObject();
        }
    }
    QVERIFY2(!aEntry.isEmpty(), "expected app|a in the tile batch");
    QCOMPARE(aEntry.value(QLatin1String("scrollEdge")).toString(), Ax::edgeLead());

    // app|b has been on screen throughout — never parked, so nothing to
    // anchor and no edge. This is what stops the field being emitted blanket.
    QJsonObject bEntry;
    for (const QJsonValue& v : batch) {
        if (v.toObject().value(QLatin1String("windowId")).toString() == QLatin1String("app|b")) {
            bEntry = v.toObject();
        }
    }
    QVERIFY2(!bEntry.isEmpty(), "expected app|b in the tile batch");
    QVERIFY2(!bEntry.contains(QLatin1String("scrollEdge")),
             "a column that was never parked must not carry a scrollEdge");

    // No committed rect from this fixture crosses the TRAIL screen edge,
    // where S2 sits. Narrow by construction: this fixture's columns are
    // either fully on screen or fully off (they park), so the clamp branches
    // never run here and only the park bound is exercised — the genuine
    // straddler clamp contract (both edges, screen-not-work-area, peek
    // floor, crop mode) is pinned by test_scrollengine_boundary.cpp.
    for (int sig = 0; sig < tiled.count(); ++sig) {
        const QJsonArray b = QJsonDocument::fromJson(tiled.at(sig).at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : b) {
            const QJsonObject o = v.toObject();
            // Parked entries are excluded: parkRect is deliberately PHYSICAL
            // (it means "off every output"), so a parked column sits below
            // the canvas whichever way the strip runs and its main extent
            // there says nothing about the neighbour boundary.
            if (o.value(QLatin1String("y")).toInt() > defaultScreenRect().bottom()) {
                continue;
            }
            const int right = Ax::entryMainEnd(o);
            QVERIFY2(right <= Ax::mainEnd(defaultScreenRect()),
                     qPrintable(QStringLiteral("rect for %1 crosses the neighbour boundary (right=%2)")
                                    .arg(o.value(QLatin1String("windowId")).toString())
                                    .arg(right)));
        }
    }

    // The half that matters most and the one that was missed first time
    // round: scrolling BACK must report the edge the column is arriving from.
    // c went out by the TRAIL end, so when it returns it must come back in
    // from the trail end — even though it has been sitting parked BELOW the
    // canvas the whole time, which is exactly why the edge cannot be read off
    // its rect.
    tiled.clear();
    engine->focusColumnLast(QStringLiteral("S1"));
    QVERIFY(!tiled.isEmpty());
    const QJsonArray backBatch = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    QJsonObject cBack;
    for (const QJsonValue& v : backBatch) {
        if (v.toObject().value(QLatin1String("windowId")).toString() == QLatin1String("app|c")) {
            cBack = v.toObject();
        }
    }
    QVERIFY2(!cBack.isEmpty(), "expected app|c in the scroll-back batch");
    QVERIFY2(Ax::entryMainPos(cBack) >= Ax::mainPos(defaultScreenRect()),
             "app|c should be back on screen after scrolling to the last column");
    QCOMPARE(cBack.value(QLatin1String("scrollEdge")).toString(), Ax::edgeTrail());
    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineSmoke::viewDeltaCarriesOnScreenTilesOnly()
{
    // viewDeltaX is what lets the effect move the strip as ONE object: it
    // springs the delta once per output rather than starting an independent
    // per-window spring for every column and watching them desync.
    //
    // Three properties, and the third is the one the effect's residual rule
    // rests on:
    //   1. the first batch for a context carries none (nothing on screen to
    //      slide from, so the windows are placed outright);
    //   2. a PARKED tile carries none (its committed rect is below the
    //      outputs, where no translation can put it back on screen — it keeps
    //      the edge-anchored slide-out built from scrollEdge instead);
    //   3. for a tile on screen in BOTH batches of a pure view change, the
    //      delta equals its ENTIRE movement. That is the definition of "the
    //      view carries this window", and it is what makes the effect's
    //      residual come out at zero so it starts no second animation.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QVERIFY(!tiled.isEmpty());

    const QJsonArray firstBatch = QJsonDocument::fromJson(tiled.first().at(0).toString().toUtf8()).array();
    QVERIFY2(!firstBatch.isEmpty(), "expected the opening batch to carry app|a");
    for (const QJsonValue& v : firstBatch) {
        QVERIFY2(!v.toObject().contains(QLatin1String("viewDelta")),
                 "a context's first batch has no previous view to slide from");
    }

    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);

    // Rects as committed by the last STRUCTURAL batch, so the comparison
    // below spans a pure view change and nothing else.
    const auto rectsOf = [](const QSignalSpy& spy, int index) {
        QHash<QString, QRect> out;
        const QJsonArray b = QJsonDocument::fromJson(spy.at(index).at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : b) {
            const QJsonObject o = v.toObject();
            out.insert(o.value(QLatin1String("windowId")).toString(),
                       QRect(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt(),
                             o.value(QLatin1String("width")).toInt(), o.value(QLatin1String("height")).toInt()));
        }
        return out;
    };
    const QHash<QString, QRect> before = rectsOf(tiled, tiled.count() - 1);

    tiled.clear();
    engine->focusColumnFirst(QStringLiteral("S1"));
    QVERIFY2(!tiled.isEmpty(), "a focus move that shifts the view must emit a batch");
    const QJsonArray moved = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();

    int carried = 0;
    for (const QJsonValue& v : moved) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QLatin1String("windowId")).toString();
        const QRect now(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt(),
                        o.value(QLatin1String("width")).toInt(), o.value(QLatin1String("height")).toInt());
        const bool parked = now.top() > defaultScreenRect().bottom();
        if (parked) {
            QVERIFY2(!o.contains(QLatin1String("viewDelta")),
                     qPrintable(QStringLiteral("parked tile %1 must not claim to ride the view").arg(id)));
            continue;
        }
        const auto prev = before.constFind(id);
        if (prev == before.constEnd() || prev->top() > defaultScreenRect().bottom()) {
            continue; // arriving from a park: no on-screen predecessor to difference against
        }
        QVERIFY2(o.contains(QLatin1String("viewDelta")),
                 qPrintable(QStringLiteral("on-screen tile %1 should ride the view").arg(id)));
        // The whole of this window's movement, and nothing but the view's.
        // Sign: the field is the translation that puts the window BACK where
        // it was rendered, which is where the effect starts its spring before
        // ringing it out to zero — so it is the negation of the movement.
        //
        // Holds for a tile the layout left where the strip put it, which is
        // every tile in this fixture. A straddling tile the edge clamp pins
        // ALSO carries the field: its position leg and the view offset cancel
        // in the effect's residual branch while only its clamped width
        // animates, so this position-delta equality would not hold for it —
        // no such tile exists in this fixture.
        QCOMPARE(Ax::mainPos(*prev) - Ax::mainPos(now), o.value(QLatin1String("viewDelta")).toInt());
        QCOMPARE(Ax::crossPos(now), Ax::crossPos(*prev));
        QCOMPARE(now.size(), prev->size());
        ++carried;
    }
    QVERIFY2(carried > 0, "expected at least one column carried by the view across the focus move");
}

void TestScrollEngineSmoke::viewDeltaIsSuppressedAcrossAWorkAreaChange()
{
    // The baseline only means something against the work area it was measured
    // in. Column widths are fractions of that area, so a panel appearing or a
    // gap edit rescales every column's strip position and with it the view
    // coordinate — by an amount proportional to how deep the anchor sits, not
    // by a pixel or two. Subtracting across two bases would describe a slide
    // nobody made, and the effect springs the WHOLE strip to ring it out.
    // `available` BEFORE `owner`, load-bearing: locals destroy in reverse
    // declaration order, and the engine (owner's child, torn down in owner's
    // destructor) holds a provider capturing &available — declared the other
    // way round, the rect dies first and any teardown-path resolve reads a
    // dangling reference.
    QRect available = defaultScreenRect();
    QObject owner;
    ScrollEngine* engine = ScrollTestUtils::makeProviderEngine(
        &owner, {QStringLiteral("S1")},
        [](const QString&) {
            return defaultScreenRect();
        },
        [&available](const QString&) {
            return available;
        });
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);

    for (const char* id : {"app|a", "app|b", "app|c", "app|d"}) {
        engine->windowOpened(QString::fromLatin1(id), QStringLiteral("S1"), 0, 0);
    }
    // Scroll deep enough that the view coordinate is far from zero, which is
    // what makes a rescale of it large rather than negligible.
    engine->focusColumnLast(QStringLiteral("S1"));
    QVERIFY(!tiled.isEmpty());

    // A panel appears. Every subsequent rect is resolved in the new area.
    // The shrink is TRANSPOSED with the fixture — it must eat the MAIN
    // extent (the direction column strip positions rescale in), or the
    // vertical arm only shrinks the cross extent and the different-basis
    // premise this test exists for never holds there.
    available = ScrollTestUtils::Ax::vertical() ? defaultScreenRect().adjusted(0, 0, 0, -240)
                                                : defaultScreenRect().adjusted(0, 0, -240, 0);
    const int before = tiled.count();
    engine->focusColumnFirst(QStringLiteral("S1"));
    QVERIFY2(tiled.count() > before, "the work-area change plus focus move must emit");

    const QJsonArray batch = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    QVERIFY(!batch.isEmpty());
    for (const QJsonValue& v : batch) {
        QVERIFY2(!v.toObject().contains(QLatin1String("viewDelta")),
                 "a batch resolved in a different work area than its baseline must carry no view delta");
    }

    // And the NEXT batch, now sharing a basis with its baseline again, carries
    // one — so the suppression is scoped to the crossing rather than latching.
    // Asserted UNCONDITIONALLY (the file's own style, see the arm above): a
    // scroll back across a five-column strip genuinely moves rects, so a
    // missing emit here would itself be the latch this test exists to catch.
    const int afterChange = tiled.count();
    engine->focusColumnLast(QStringLiteral("S1"));
    QVERIFY2(tiled.count() > afterChange, "the settling scroll must emit");
    const QJsonArray settled = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    bool anyDelta = false;
    for (const QJsonValue& v : settled) {
        anyDelta = anyDelta || v.toObject().contains(QLatin1String("viewDelta"));
    }
    QVERIFY2(anyDelta, "once the basis matches again the view delta must come back");
}

void TestScrollEngineSmoke::secondScrollMeasuresFromTheEmittedBaselineOnly()
{
    // The baseline advances with the EMIT, not with the relayout. A batch the
    // emit-on-change gate suppresses leaves the compositor showing the
    // previous positions, so a baseline that moved anyway would make the NEXT
    // delta describe a slide that never happened — and the deltas of a chain
    // of scrolls would drift apart from the movement they claim to describe.
    //
    // Drives the REPEAT rather than one scroll, which is the whole point: a
    // single invocation cannot tell a baseline that advances on emit from one
    // that advances on relayout.
    // Three columns and SINGLE-column steps: two fit on the default output, so
    // a one-column scroll leaves a survivor on screen across both batches,
    // which is what the identity below needs. A first-to-last jump would
    // scroll the whole strip past and leave nothing to difference.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    for (const char* id : {"app|a", "app|b", "app|c"}) {
        engine->windowOpened(QString::fromLatin1(id), QStringLiteral("S1"), 0, 0);
    }

    const auto xOf = [](const QSignalSpy& spy, int index, const QString& id) {
        const QJsonArray b = QJsonDocument::fromJson(spy.at(index).at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : b) {
            const QJsonObject o = v.toObject();
            if (o.value(QLatin1String("windowId")).toString() == id) {
                return Ax::entryMainPos(o);
            }
        }
        return INT_MIN;
    };

    engine->focusColumnFirst(QStringLiteral("S1"));
    const int afterFirst = tiled.count() - 1;
    QVERIFY(afterFirst >= 0);

    // A REDUNDANT relayout between the two scrolls. It resolves every window
    // to the rect already applied, so the gate suppresses it — and the
    // baseline must not move for it.
    const int beforeRedundant = tiled.count();
    engine->focusColumnFirst(QStringLiteral("S1"));
    QCOMPARE(tiled.count(), beforeRedundant);

    // One column back the other way.
    engine->windowFocused(QStringLiteral("app|c"), QStringLiteral("S1"));
    QVERIFY2(tiled.count() > beforeRedundant, "the second scroll must emit");
    const int afterSecond = tiled.count() - 1;

    // The second delta describes movement between the two EMITTED batches,
    // which is the only thing the compositor ever saw. Had the suppressed
    // relayout advanced the baseline, the delta would describe a shorter slide
    // than the one actually rendered.
    //
    // Checked only against tiles that were ON SCREEN in the first batch. An
    // ARRIVING tile also carries the delta — deliberately, so translating its
    // final rect back lands it at its real pre-scroll STRIP position rather
    // than at a made-up point beside the screen edge — but its previous
    // COMMITTED main position was the park, which describes nothing. parkRect
    // moves only the top edge, so a parked tile is identified by its y sitting
    // past the screen's bottom — the same `.bottom()` predicate every other
    // park check in this file uses, physical on both axes because the park is.
    const QJsonArray second = QJsonDocument::fromJson(tiled.at(afterSecond).at(0).toString().toUtf8()).array();
    const QJsonArray first = QJsonDocument::fromJson(tiled.at(afterFirst).at(0).toString().toUtf8()).array();
    const auto wasOnScreenInFirst = [&first](const QString& id) {
        for (const QJsonValue& v : first) {
            const QJsonObject o = v.toObject();
            if (o.value(QLatin1String("windowId")).toString() == id) {
                return o.value(QLatin1String("y")).toInt() <= defaultScreenRect().bottom();
            }
        }
        return false;
    };

    int checked = 0;
    for (const QJsonValue& v : second) {
        const QJsonObject o = v.toObject();
        if (!o.contains(QLatin1String("viewDelta"))) {
            continue;
        }
        const QString id = o.value(QLatin1String("windowId")).toString();
        if (!wasOnScreenInFirst(id)) {
            continue;
        }
        QCOMPARE(o.value(QLatin1String("viewDelta")).toInt(), xOf(tiled, afterFirst, id) - Ax::entryMainPos(o));
        ++checked;
    }
    QVERIFY2(checked > 0, "expected at least one column on screen across both emitted batches");
}

void TestScrollEngineSmoke::aWidthChangeKeepsTheResizedColumnInTheBatch()
{
    // The residual rule: a scrolled column's origin is placed one delta behind
    // its target so the per-window leg comes out degenerate and no second
    // spring runs. A column whose WIDTH also changed in the same batch keeps a
    // real leg — its size has to interpolate. What this test PINS is the
    // batch half of that: the resized column is present and reports its new
    // width. Whether the same entry also carries viewDelta depends on
    // whether the view genuinely moved, which the clamp-suppression rule can
    // legitimately veto — so the delta is deliberately NOT asserted here.
    // The sibling smoke test deliberately asserts size EQUALITY across its
    // batches, so this case had no coverage anywhere.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    for (const char* id : {"app|a", "app|b", "app|c"}) {
        engine->windowOpened(QString::fromLatin1(id), QStringLiteral("S1"), 0, 0);
    }
    engine->focusColumnFirst(QStringLiteral("S1"));
    const QRect widthBefore = engine->lastManagedRect(QStringLiteral("app|a"));
    QVERIFY(widthBefore.isValid());

    // Widen the focused column AND move the view in the same pass: the width
    // change reflows the strip, which shifts every later column's strip
    // position and so moves the view anchor too.
    const int before = tiled.count();
    engine->adjustColumnWidth(20.0, QStringLiteral("S1"));
    QVERIFY2(tiled.count() > before, "a width change must emit");

    const QJsonArray batch = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    QVERIFY(!batch.isEmpty());
    // BOTH sides read by ROLE. Comparing the new MAIN extent against the old
    // rect's physical width made the inequality hold on the vertical arm no
    // matter what the engine did — 800, the cross extent, never changes — so
    // the assertion could not fail even for a no-op adjustColumnWidth.
    QVERIFY2(Ax::mainLen(engine->lastManagedRect(QStringLiteral("app|a"))) != Ax::mainLen(widthBefore),
             qPrintable(QStringLiteral("the width change must land in the managed rect (main extent still %1)")
                            .arg(Ax::mainLen(widthBefore))));

    // The resized column is in the batch and its geometry reflects the new
    // width — the delta field is orthogonal to that, and whether it appears
    // depends on whether the view actually moved, which the clamp-suppression
    // rule can legitimately veto. What must NOT happen is the batch losing the
    // resized column or reporting the old width.
    bool sawResized = false;
    for (const QJsonValue& v : batch) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("windowId")).toString() != QLatin1String("app|a")) {
            continue;
        }
        sawResized = true;
        QCOMPARE(Ax::entryMainLen(o), Ax::mainLen(engine->lastManagedRect(QStringLiteral("app|a"))));
        // Pinned, not just "differs and agrees": both sides above come out of
        // the same resolve, so a wrong-but-consistent width satisfies them.
        // 840 is the fixture arithmetic — a half-work-area column on the
        // 1200px main extent is 600px, and +20% of that extent adds 240.
        QCOMPARE(Ax::entryMainLen(o), 840);
    }
    QVERIFY2(sawResized, "the batch must carry the column whose width changed");
}

void TestScrollEngineSmoke::aDepartingColumnKeepsItsTabIndicator()
{
    // A column scrolling OUT of view keeps its indicator for the whole leg,
    // because the compositor slides the indicator surface by the same offset
    // it slides the columns — an indicator dropped the moment its column's
    // final rect left the work area would vanish while the column it labels is
    // still on screen travelling.
    //
    // This is the case the all-parked skip used to swallow: a departing
    // column has every tile parked by the same work-area test that makes it
    // invisible, so the skip fired before the visible-before term could keep
    // it. Without this test that regression is invisible.
    QObject owner;
    ScrollEngine* engine = ScrollTestUtils::makeGappedProviderEngine(&owner, {QStringLiteral("S1")});
    QSignalSpy strips(engine, &ScrollEngine::tabStripsChanged);
    for (const char* id : {"app|a", "app|b", "app|c", "app|d", "app|e", "app|f"}) {
        engine->windowOpened(QString::fromLatin1(id), QStringLiteral("S1"), 0, 0);
    }
    // Make the FIRST column tabbed so it owns an indicator, then scroll far
    // enough that it leaves the viewport.
    engine->focusColumnFirst(QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));
    QVERIFY2(!strips.isEmpty(), "a tabbed column must announce an indicator");

    const auto stripCount = [](const QSignalSpy& spy, int index) {
        return QJsonDocument::fromJson(spy.at(index).at(1).toString().toUtf8()).array().size();
    };
    QVERIFY2(stripCount(strips, strips.count() - 1) > 0, "precondition: the tabbed column has a strip");

    const int beforeScroll = strips.count();
    engine->focusColumnLast(QStringLiteral("S1"));
    QVERIFY2(strips.count() > beforeScroll, "the scroll must re-announce");
    // The departing column's indicator survives the batch that scrolls it out.
    // It resolves off screen and the per-screen surface clips it, which is why
    // carrying it costs nothing once at rest.
    QVERIFY2(stripCount(strips, strips.count() - 1) > 0,
             "a column scrolling out of view must keep its indicator for the leg");
}

void TestScrollEngineSmoke::onlyAStripDepartedTileCarriesAVisualPosition()
{
    // visualX/visualY are a PAINT hint for a column the strip carried off one
    // of its ENDS: its committed rect is the park, which no translation can put back
    // on screen, so the effect draws it at its real strip position instead.
    //
    // The gate is the departure EDGE, not the park itself, and that matters
    // because the other park kinds share the predicate but not the meaning. A
    // hidden tab of an ON-SCREEN tabbed column parks to keep it from stealing
    // input and shares the active tab's rect — emitting a visual position for
    // it drew every inactive tab stacked on the visible one, permanently, on a
    // strip that was not even moving. Nothing covered that before.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    // One tabbed column, entirely on screen: b is hidden behind a, so b parks
    // WITHOUT a departure edge. Focus a first — consume pulls the NEXT column
    // into the FOCUSED one, and focus sits on the last-opened window.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));
    QVERIFY(!tiled.isEmpty());

    const QJsonArray batch = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    QVERIFY(!batch.isEmpty());

    // The fixture must actually produce the case, or the assertions below are
    // vacuous: exactly one tile parked (the hidden tab) and it carries no
    // departure edge, because nothing scrolled it away.
    int hiddenTabs = 0;
    for (const QJsonValue& v : batch) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("y")).toInt() > defaultScreenRect().bottom()
            && !o.contains(QLatin1String("scrollEdge"))) {
            ++hiddenTabs;
        }
    }
    QVERIFY2(hiddenTabs > 0, "precondition: the tabbed column must park a hidden tab with no departure edge");

    for (const QJsonValue& v : batch) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QLatin1String("windowId")).toString();
        const bool hasEdge = o.contains(QLatin1String("scrollEdge"));
        const bool hasVisual = o.contains(QLatin1String("visualX"));
        // The invariant, stated directly: a visual position implies a
        // departure edge. The hidden tab has neither; nothing has only one.
        QVERIFY2(!hasVisual || hasEdge,
                 qPrintable(QStringLiteral("tile %1 carries a visual position with no departure edge").arg(id)));
        // And both halves of the pair travel together.
        QCOMPARE(hasVisual, o.contains(QLatin1String("visualY")));
    }

    // POSITIVE witness for the implication: nothing above carries the hint,
    // so deleting the visualX emission entirely would leave every assertion
    // green. Grow the strip and scroll back to the start so trailing columns
    // genuinely depart by the TRAIL edge — the case the hint exists for.
    for (const char* id : {"app|c", "app|d", "app|e"}) {
        engine->windowOpened(QString::fromLatin1(id), QStringLiteral("S1"), 0, 0);
    }
    const int beforeWitnessScroll = tiled.count();
    engine->focusColumnFirst(QStringLiteral("S1"));
    QVERIFY2(tiled.count() > beforeWitnessScroll, "the witness scroll must emit its own batch");
    const QJsonArray scrolled = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    bool sawCarriedPark = false;
    for (const QJsonValue& v : scrolled) {
        const QJsonObject o = v.toObject();
        if (o.contains(QLatin1String("visualX")) && o.contains(QLatin1String("scrollEdge"))) {
            sawCarriedPark = true;
            QVERIFY2(o.contains(QLatin1String("visualY")), "the paint hint must carry both axes");
        }
    }
    QVERIFY2(sawCarriedPark, "a strip-departed column must carry the visual-position paint hint");
}

void TestScrollEngineSmoke::aTabSwitchNamesTheTabItReplaces()
{
    // The pairing the compositor cannot derive. Activating a tab is two
    // commits sharing one rect — the shown tab parks, the picked one takes the
    // rect it vacated — and both are ordinary placements on the wire, so
    // without `tabFrom` the effect can only hard-cut between two different
    // windows in one rectangle.
    //
    // Both halves are asserted, because either alone would pass against a
    // field that always fired: the ARRIVING tab names the outgoing one, and
    // the outgoing entry carries nothing.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));

    // Tabbing is NOT a switch, and this is the assertion that says so. Every
    // tile the toggle hides was on screen a moment ago, but so was the one it
    // leaves showing, so no pairing exists — a rule keyed on "something got
    // hidden" alone would fire here and cross-fade a window against itself.
    const QJsonArray tabbedBatch = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    QVERIFY(!tabbedBatch.isEmpty());
    for (const QJsonValue& v : tabbedBatch) {
        QVERIFY2(!v.toObject().contains(QLatin1String("tabFrom")),
                 "tabbing a column swaps nothing, so no entry may name a replaced tab");
    }

    // Now the switch itself: focus the hidden tab, which shows it and parks
    // the one that was showing.
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    const QString shown = state->strip().activeWindowId();
    const QString hidden = shown == QLatin1String("app|a") ? QStringLiteral("app|b") : QStringLiteral("app|a");
    const int beforeSwitch = tiled.count();
    engine->windowFocused(hidden, QStringLiteral("S1"));
    QVERIFY2(tiled.count() > beforeSwitch, "a tab switch must emit its own batch");
    const QJsonArray batch = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();

    QJsonObject arriving;
    QJsonObject leaving;
    for (const QJsonValue& v : batch) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("y")).toInt() > defaultScreenRect().bottom()) {
            leaving = o;
        } else {
            arriving = o;
        }
    }
    QVERIFY2(!arriving.isEmpty() && !leaving.isEmpty(),
             "precondition: the switch must park one tab and show the other");
    QCOMPARE(arriving.value(QLatin1String("tabFrom")).toString(), leaving.value(QLatin1String("windowId")).toString());
    QVERIFY2(!leaving.contains(QLatin1String("tabFrom")),
             "the outgoing tab is the source of the cross-fade, not a subject of one");
    // Not a self-reference: the wire validator rejects that outright, and it is
    // what a naive "the other tile in this column" rule would produce on a
    // single-tab column.
    QVERIFY(arriving.value(QLatin1String("tabFrom")).toString()
            != arriving.value(QLatin1String("windowId")).toString());
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
    QCOMPARE(Ax::mainLen(engine->lastManagedRect(QStringLiteral("app|a"))), 720);

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
    QCOMPARE(Ax::mainLen(engine->lastManagedRect(QStringLiteral("app|a"))), 720);
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
    // WHILE floated the answer comes from the FloatRestore entry, so the
    // cross-engine handoff still gets a clamp for a floated window.
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), QSize(500, 400));
    // A min-size report landing mid-float writes through to the restore
    // entry; unfloat re-applies the UPDATED clamp, not the float-time one.
    engine->windowMinSizeUpdated(QStringLiteral("app|a"), 520, 410);
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), QSize(520, 410));
    engine->setWindowFloat(QStringLiteral("app|a"), false, QStringLiteral("S1"));
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), QSize(520, 410));
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

void TestScrollEngineSmoke::modeTransitionFocusSeedAnchorsArrival()
{
    // A mode flip seeds POSITION and FOCUS together. Position alone left the
    // strip pointed at whichever column the seed adopted first — insertWindowAt
    // takes no focus — so the window the user was actually on could not pull
    // the view onto itself, and a window opened just before the flip landed
    // parked off-screen.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setInitialWindowOrder(QStringLiteral("S1"),
                                  {QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")});
    engine->setInitialFocusedWindow(QStringLiteral("S1"), QStringLiteral("app|c"));

    // The re-announce arrives as a burst, which is what defers the apply (and
    // the focus restore with it) to the end.
    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    // Still on the first arrival until the burst closes: the restore is a
    // burst-end step precisely because the seeded window is usually not last.
    QCOMPARE(engine->managedFocusedWindow(QStringLiteral("S1")), QStringLiteral("app|a"));
    engine->endArrivalBurst();

    QCOMPARE(engine->managedFocusedWindow(QStringLiteral("S1")), QStringLiteral("app|c"));
    // Position is untouched by the focus restore.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")}));
}

void TestScrollEngineSmoke::modeTransitionFocusSeedIsDrainedByItsBurst()
{
    // Named for what it actually pins: consumption by the burst that FOLLOWS
    // the seed. That is the path that always worked. The two tests below cover
    // the paths where the seed is never consumed at all, which is where it used
    // to survive and re-anchor an unrelated later transition.
    //
    // A later burst on the same screen must leave focus where the user's own
    // arrivals put it, or every subsequent flip would rewind the view to a
    // transition the user has long since moved past.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setInitialWindowOrder(QStringLiteral("S1"), {QStringLiteral("app|a"), QStringLiteral("app|b")});
    engine->setInitialFocusedWindow(QStringLiteral("S1"), QStringLiteral("app|b"));

    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->endArrivalBurst();
    QCOMPARE(engine->managedFocusedWindow(QStringLiteral("S1")), QStringLiteral("app|b"));

    // Second burst, no fresh seed: the arrival keeps the focus it took.
    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->endArrivalBurst();
    QCOMPARE(engine->managedFocusedWindow(QStringLiteral("S1")), QStringLiteral("app|c"));
}

void TestScrollEngineSmoke::modeTransitionFocusSeedDropsWhenScreenLeaves()
{
    // The seed is scoped to the transition that captured it. Its only consumer
    // is the arrival burst, so a screen seeded for a flip whose re-announce
    // never produces one — an empty desktop, or arrivals filtered out — reaches
    // no drain at all. Without a drop at the lifecycle edge the entry survives
    // the screen leaving scrolling entirely, and the NEXT unrelated burst on
    // that screen anchors the view on a window from a flip the user has long
    // since moved past.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    // Seeded with a window that WILL be in the strip later. Naming an absent
    // window would not discriminate: the burst-end restore drops a seed whose
    // window the strip does not contain, so the defect would hide behind that
    // guard rather than being caught.
    engine->setInitialFocusedWindow(QStringLiteral("S1"), QStringLiteral("app|a"));

    // No burst: nothing consumes the seed. The screen then leaves scrolling.
    engine->setActiveScreens({QStringLiteral("S2")});
    engine->setActiveScreens({QStringLiteral("S1"), QStringLiteral("S2")});

    // A fresh, unrelated burst with no seed of its own. These arrivals take
    // focus the ordinary way, so the LAST one owns it — there is no order seed
    // here, which is what would otherwise route them through the no-focus
    // insert. A surviving seed would override that at burst end and pull focus
    // back to app|a, so the two outcomes are distinguishable.
    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->endArrivalBurst();
    QCOMPARE(engine->managedFocusedWindow(QStringLiteral("S1")), QStringLiteral("app|b"));
}

void TestScrollEngineSmoke::modeTransitionFocusSeedDropsOnMidBurstContextSwitch()
{
    // A context switch landing mid-burst makes the deferred apply's strip no
    // longer the one on screen, so the burst-end pass skips that key. The seed
    // has to die there anyway: the transition it belongs to is over the moment
    // the context moves, and nothing downstream of the skip revisits the entry
    // — the switch-back retile does not consume seeds. Left armed, it waits for
    // whatever burst comes next.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->setInitialFocusedWindow(QStringLiteral("S1"), QStringLiteral("app|b"));

    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    // The context moves out from under the burst before it closes.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    engine->endArrivalBurst();

    // Back on the original desktop, with its strip intact. A second burst
    // there must anchor on its own arrival: the skipped seed is gone, not
    // waiting.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->endArrivalBurst();
    QCOMPARE(engine->managedFocusedWindow(QStringLiteral("S1")), QStringLiteral("app|c"));
}

void TestScrollEngineSmoke::modeTransitionFocusSeedSurvivesAStraddlingBurst()
{
    // The seed is keyed by bare screen id; the burst's deferred applies are
    // keyed by the whole context. One screen can therefore appear TWICE in a
    // burst that straddles a context switch — once under the desktop the
    // arrivals started on, once under the one they finished on. Only the
    // matching key consumes the seed; the other takes the skip arm.
    //
    // The skip arm must not drop the seed out from under the key that is about
    // to use it. Keys are walked in sorted order, so the stale one runs FIRST
    // whenever its desktop sorts lower, which is exactly this arrangement.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);

    engine->beginArrivalBurst();
    // Arrival on desktop 1 — inserts a pending key under desktop 1.
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    // The context moves, and the seed belongs to the desktop the burst ENDS on.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    engine->setInitialFocusedWindow(QStringLiteral("S1"), QStringLiteral("app|b"));
    // Two more arrivals on desktop 2 — a second pending key for the same screen.
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->endArrivalBurst();

    // app|c arrived last and would own focus on its own; the seed names app|b,
    // so a consumed seed is distinguishable from a dropped one.
    QCOMPARE(engine->managedFocusedWindow(QStringLiteral("S1")), QStringLiteral("app|b"));
}

void TestScrollEngineSmoke::perOutputDesktopSurvivesScrollingSetLeave()
{
    // A screen leaving the scrolling set releases this engine's OWNERSHIP of it
    // but must keep its per-output desktop: that value is compositor truth the
    // engine cannot re-derive, and the global desktop it would otherwise fall
    // back to is written once at startup and never again under per-output
    // desktops. Dropping it merged every output onto one desktop on re-entry.
    //
    // The per-output value differs from the global ON PURPOSE. With the two
    // equal — which is what every other test in this file leaves them as — the
    // fallback resolves to the same key and no assertion here can tell a
    // preserved entry from a dropped one.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktop(1);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 3);

    // A strip with a stacked column, so the structure that comes back is
    // distinguishable from a default one-window-per-column rebuild.
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|b")), 0);

    // Leave and re-enter WITHOUT re-pushing the per-output desktop. A real
    // context switch re-pushes, which is why the existing round-trip tests pass
    // either way; a plain engine-set flip does not.
    engine->setActiveScreens({});
    engine->setActiveScreens({QStringLiteral("S1")});
    // Leaving releases the windows, so the flip back re-announces them — which
    // is what a real mode toggle does, and what lets the stash apply.
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);

    // The stash is keyed by context. With the per-output entry preserved the
    // re-entry resolves the same key and the stacked column returns; had it
    // been dropped, the key would fall back to the global desktop 1, the stash
    // would not be found, and app|b would come back in its own column.
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|a")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|b")), 0);
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

void TestScrollEngineSmoke::tileFlaggedFloatingBySiblingEngineSyncsClear()
{
    // The one-way float trap, seen live: a window can be a strip TILE here
    // while the SHARED float set still flags it, because a sibling engine
    // floated it (snap's no-zone-match default on its own screen, which is
    // correct there) and the window later joined this strip.
    //
    // unfloatWindowInternal used to bail at removeFloating() and return in
    // silence, so nothing ever cleared the shared flag: every unfloat route
    // refused, and a window the shared set calls floating is never adopted
    // into a strip again. The engine must announce its real view instead —
    // on the PASSIVE arm, because the window is already placed and must not
    // be moved.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QVERIFY(engine->isWindowTiled(QStringLiteral("app|a")));
    // The engine holds NO float for it — exactly the sibling-floated shape.
    QVERIFY(!engine->isWindowFloatingInScroll(QStringLiteral("app|a")));

    QSignalSpy syncSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
    QSignalSpy activeSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
    engine->setWindowFloat(QStringLiteral("app|a"), false, QStringLiteral("S1"));

    // Exactly one passive sync clearing the flag, and no active transition
    // (which would drag the tile back to a float-back geometry).
    QCOMPARE(syncSpy.count(), 1);
    QCOMPARE(syncSpy.last().at(0).toString(), QStringLiteral("app|a"));
    QCOMPARE(syncSpy.last().at(1).toBool(), false);
    QCOMPARE(syncSpy.last().at(2).toString(), QStringLiteral("S1"));
    QCOMPARE(activeSpy.count(), 0);
    // Still a tile, untouched.
    QVERIFY(engine->isWindowTiled(QStringLiteral("app|a")));

    // Boundary: a window this state has never seen is NOT the heal arm's
    // case — setWindowFloat's adopt route takes it, inserting it into the
    // strip and announcing that on the same passive arm. Pinned here so the
    // heal is never widened into a second adopt path.
    engine->setWindowFloat(QStringLiteral("app|ghost"), false, QStringLiteral("S1"));
    QCOMPARE(syncSpy.count(), 2);
    QVERIFY(engine->isWindowTiled(QStringLiteral("app|ghost")));
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

// GUILESS (not APPLESS): a QCoreApplication provides the event
// dispatcher the coalesced scheduleRetileForScreen and the prunes'
// deleteLater need — under APPLESS every processEvents() in this file was
// silently a no-op and the coalescing path had zero coverage.
QTEST_GUILESS_MAIN(TestScrollEngineSmoke)
#include "test_scrollengine_smoke.moc"
