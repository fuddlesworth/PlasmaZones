// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Integration tests for geometry-driven directional window navigation.
//
// Directional selection reads TilingState::calculatedZones() (index-aligned
// with tiledWindows()). Rather than drive the full screen-geometry + retile
// machinery, these inject a known layout via setCalculatedZones() so the
// geometry under test is exact and deterministic — the engine must then pick
// neighbours spatially, not by list index (the reported bug, where "right" and
// "down" were the same index+1 operation).

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/VirtualScreen.h>

#include "FakeScreenProvider.h"
#include "core/crosssurfaceresolver.h"

#include "../helpers/AutotileTestHelpers.h"
#include "../helpers/VirtualScreenTestHelpers.h"

using PhosphorEngine::NavigationContext;
using PhosphorTileEngine::AutotileEngine;

namespace {

const QString kScreen = QStringLiteral("eDP-1");

// A binary split of a 1920x1080 screen, index-aligned with [win1, win2, win3]:
//   win1 — left half (full height)
//   win2 — top-right quadrant
//   win3 — bottom-right quadrant
// Chosen because it makes direction unambiguous from win2: left=win1,
// down=win3, while up and right are layout edges.
void installBinarySplitLayout(AutotileEngine* engine)
{
    PhosphorTiles::TilingState* state = engine->tilingStateForScreen(kScreen);
    Q_ASSERT(state);
    // setCalculatedZones must run AFTER the post-open retile (which fails under
    // the null ScreenManager and would otherwise clear these) — callers invoke
    // this once the engine is fully constructed.
    state->setCalculatedZones({
        QRect(0, 0, 960, 1080), // win1 left
        QRect(960, 0, 960, 540), // win2 top-right
        QRect(960, 540, 960, 540) // win3 bottom-right
    });
}

NavigationContext ctx(const QString& windowId)
{
    return NavigationContext{windowId, kScreen};
}

/// Test resolver: no neighbour outputs, and a simple linear desktop grid
/// (right = next desktop, left = previous), so cross-desktop navigation can be
/// driven headlessly without a live KWin VirtualDesktopManager.
class FakeCrossSurfaceResolver : public PhosphorEngine::ICrossSurfaceResolver
{
public:
    QString neighborOutputInDirection(const QString&, const QString&) const override
    {
        return QString();
    }
    int neighborDesktopInDirection(int currentDesktop, const QString& direction) const override
    {
        if (direction == QLatin1String("right")) {
            return currentDesktop + 1;
        }
        if (direction == QLatin1String("left")) {
            return currentDesktop > 1 ? currentDesktop - 1 : 0;
        }
        return 0;
    }
};

} // namespace

class TestNavigationCrossSurface : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void focusRight_fromLeftColumn_picksWindowToTheRight();
    void focus_fromTopRight_resolvesEachDirectionGeometrically();
    void swap_exchangesWithSpatialNeighbour();

    void entryWindowForCrossing_picksEdgeTileFacingSource();
    void crossOutput_swapTowardNonAutotileOutput_emitsCrossModeSwap();
    void crossOutput_focusRight_entersLeftEdgeOfNeighbourOutput();
    void crossOutput_moveRight_emitsExpectedMoveOnceBeforeReflowsAndActivate();
    void crossOutput_moveTowardNonAutotileOutput_doesNotStrandWindow();
    void crossOutput_moveTowardFullAutotileOutput_doesNotStrandWindow();
    void inSurfaceMove_doesNotEmitExpectedMove();
    void crossVirtualScreen_focusRight_crossesToSiblingVirtualScreen();

    void crossDesktop_focusRight_activatesEntryWindowOnNextDesktop();
    void crossDesktop_moveRight_relocatesToNextDesktopAndRequestsKWinMove();
    void crossDesktop_swapRight_doesNotRelocate();
    void crossDesktop_focusLeft_atGridEdge_doesNotActivate();
    void crossDesktop_moveLeft_atGridEdge_doesNotRequestKWinMove();
    void stickyPinnedScreen_explicitFocusResolvesPinnedState();
};

void TestNavigationCrossSurface::focusRight_fromLeftColumn_picksWindowToTheRight()
{
    std::unique_ptr<AutotileEngine> engine(PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 3));
    installBinarySplitLayout(engine.get());
    const QStringList wins = engine->tilingStateForScreen(kScreen)->tiledWindows();

    QSignalSpy activateSpy(engine.get(), &AutotileEngine::activateWindowRequested);
    engine->focusInDirection(QStringLiteral("right"), ctx(wins.at(0))); // win1 (left column)

    QCOMPARE(activateSpy.count(), 1);
    // The target must be one of the right-column windows (win2 or win3), never
    // win1 itself — and specifically the geometric right neighbour.
    const QString target = activateSpy.takeFirst().at(0).toString();
    QVERIFY2(target == wins.at(1) || target == wins.at(2), "right from the left column must land in the right column");
}

void TestNavigationCrossSurface::focus_fromTopRight_resolvesEachDirectionGeometrically()
{
    std::unique_ptr<AutotileEngine> engine(PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 3));
    installBinarySplitLayout(engine.get());
    const QStringList wins = engine->tilingStateForScreen(kScreen)->tiledWindows();
    const QString win1 = wins.at(0);
    const QString win2 = wins.at(1); // top-right
    const QString win3 = wins.at(2); // bottom-right

    auto focusTarget = [&](const QString& dir) -> QString {
        QSignalSpy spy(engine.get(), &AutotileEngine::activateWindowRequested);
        engine->focusInDirection(dir, ctx(win2));
        return spy.isEmpty() ? QString() : spy.takeFirst().at(0).toString();
    };

    // From the top-right window: left=win1, down=win3, and up/right are edges.
    // Under the old index logic these would be win1, win3, win1, win3 — the
    // up/right boundaries are what the geometric selection gets right.
    QCOMPARE(focusTarget(QStringLiteral("left")), win1);
    QCOMPARE(focusTarget(QStringLiteral("down")), win3);
    QCOMPARE(focusTarget(QStringLiteral("up")), QString()); // boundary
    QCOMPARE(focusTarget(QStringLiteral("right")), QString()); // boundary
}

void TestNavigationCrossSurface::swap_exchangesWithSpatialNeighbour()
{
    std::unique_ptr<AutotileEngine> engine(PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 3));
    installBinarySplitLayout(engine.get());
    PhosphorTiles::TilingState* state = engine->tilingStateForScreen(kScreen);
    const QStringList before = state->tiledWindows();
    const QString win2 = before.at(1);
    const QString win3 = before.at(2);

    // Swap the top-right window "down": it must exchange with the bottom-right
    // window (its geometric down neighbour), not whatever sits at index+1.
    engine->swapFocusedInDirection(QStringLiteral("down"), ctx(win2));

    const QStringList after = state->tiledWindows();
    QCOMPARE(after.indexOf(win2), before.indexOf(win3));
    QCOMPARE(after.indexOf(win3), before.indexOf(win2));
}

// ── Cross-output: two side-by-side 1920x1080 outputs, real ScreenManager ──
//
// Cross-output resolution needs real output geometry, so these build the
// engine over a FakeScreenProvider-backed ScreenManager and inject the daemon
// CrossSurfaceResolver. Per-window zones are still injected (global coords) so
// the layout is exact; the screen geometry the resolver reads is the fake
// provider's.
namespace {

struct TwoOutputFixture
{
    PhosphorScreens::FakeScreenProvider provider;
    std::unique_ptr<PhosphorScreens::ScreenManager> manager;
    std::unique_ptr<AutotileEngine> engine;
    std::unique_ptr<PlasmaZones::CrossSurfaceResolver> resolver;

    TwoOutputFixture()
    {
        provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
        provider.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080));
        manager = std::make_unique<PhosphorScreens::ScreenManager>(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager->start();
        engine =
            std::make_unique<AutotileEngine>(nullptr, nullptr, manager.get(), PlasmaZones::TestHelpers::testRegistry());
        resolver = std::make_unique<PlasmaZones::CrossSurfaceResolver>(manager.get(), nullptr);
        engine->setCrossSurfaceResolver(resolver.get());
        engine->setAutotileScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        engine->windowOpened(QStringLiteral("a1"), QStringLiteral("DP-1"));
        engine->windowOpened(QStringLiteral("a2"), QStringLiteral("DP-1"));
        engine->windowOpened(QStringLiteral("b1"), QStringLiteral("DP-2"));
        engine->windowOpened(QStringLiteral("b2"), QStringLiteral("DP-2"));
        QCoreApplication::processEvents();
        // Left/right split per output, in GLOBAL coordinates.
        engine->tilingStateForScreen(QStringLiteral("DP-1"))
            ->setCalculatedZones({QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)});
        engine->tilingStateForScreen(QStringLiteral("DP-2"))
            ->setCalculatedZones({QRect(1920, 0, 960, 1080), QRect(2880, 0, 960, 1080)});
    }
};

} // namespace

void TestNavigationCrossSurface::crossOutput_focusRight_entersLeftEdgeOfNeighbourOutput()
{
    TwoOutputFixture fx;
    // a2 is the rightmost window on DP-1 — nothing to its right on this output,
    // so "right" must cross into DP-2 and land on its left-edge window (b1).
    QSignalSpy activateSpy(fx.engine.get(), &AutotileEngine::activateWindowRequested);
    fx.engine->focusInDirection(QStringLiteral("right"),
                                NavigationContext{QStringLiteral("a2"), QStringLiteral("DP-1")});
    QCOMPARE(activateSpy.count(), 1);
    QCOMPARE(activateSpy.takeFirst().at(0).toString(), QStringLiteral("b1"));
}

void TestNavigationCrossSurface::crossOutput_moveRight_emitsExpectedMoveOnceBeforeReflowsAndActivate()
{
    TwoOutputFixture fx;
    // a2 is the rightmost window on DP-1 — moving it "right" crosses into DP-2.
    // The daemon migrates its own tiling state and reflows BOTH outputs, so it
    // must warn the compositor that the window's imminent outputChanged is
    // daemon-owned (windowOutputMoveExpected) — otherwise the effect re-issues
    // windowClosed/windowOpened and strands the source monitor's reflow.
    QSignalSpy expectedSpy(fx.engine.get(), &AutotileEngine::windowOutputMoveExpected);
    QSignalSpy activateSpy(fx.engine.get(), &AutotileEngine::activateWindowRequested);
    QSignalSpy placementSpy(fx.engine.get(), &AutotileEngine::placementChanged);

    // Cross-signal emit order: QSignalSpy only timestamps within a single signal,
    // so record all three into one ordered list to actually verify the contract
    // (the expected-move marker must precede every reflow/activate so the
    // compositor holds it before any tile-apply-driven outputChanged echo).
    QStringList emitOrder;
    QObject::connect(fx.engine.get(), &AutotileEngine::windowOutputMoveExpected, fx.engine.get(), [&emitOrder]() {
        emitOrder.append(QStringLiteral("expected"));
    });
    QObject::connect(fx.engine.get(), &AutotileEngine::placementChanged, fx.engine.get(), [&emitOrder]() {
        emitOrder.append(QStringLiteral("placement"));
    });
    QObject::connect(fx.engine.get(), &AutotileEngine::activateWindowRequested, fx.engine.get(), [&emitOrder]() {
        emitOrder.append(QStringLiteral("activate"));
    });

    fx.engine->moveFocusedInDirection(QStringLiteral("right"),
                                      NavigationContext{QStringLiteral("a2"), QStringLiteral("DP-1")});

    // Emitted exactly once, naming the moved window and its destination output.
    QCOMPARE(expectedSpy.count(), 1);
    const QList<QVariant> expectedArgs = expectedSpy.takeFirst();
    QCOMPARE(expectedArgs.at(0).toString(), QStringLiteral("a2"));
    QCOMPARE(expectedArgs.at(1).toString(), QStringLiteral("DP-2"));

    // The window crossed to DP-2: it is still engine-tracked, the destination
    // state now owns it, and the source no longer does. (Under this test's null
    // algorithm the destination may overflow-float rather than tile a2, so use
    // containsWindow — which spans tiled AND floating membership — rather than
    // DP-2's tiled set; the real-algorithm end-to-end reflow is covered by
    // test_navigation_retile.)
    QVERIFY(fx.engine->isWindowTracked(QStringLiteral("a2")));
    QVERIFY(fx.engine->tilingStateForScreen(QStringLiteral("DP-2"))->containsWindow(QStringLiteral("a2")));
    QVERIFY(!fx.engine->tilingStateForScreen(QStringLiteral("DP-1"))->containsWindow(QStringLiteral("a2")));

    // Both outputs were reflowed and the moved window was activated. The
    // expected-move marker must come FIRST in the global emit order, ahead of
    // every reflow (placementChanged) and the activate.
    QVERIFY(!emitOrder.isEmpty());
    QCOMPARE(emitOrder.first(), QStringLiteral("expected"));
    QVERIFY(emitOrder.indexOf(QStringLiteral("expected"))
            < emitOrder.indexOf(QStringLiteral("activate"))); // marker precedes activation
    QVERIFY(placementSpy.count() >= 1);
    // The moved window is activated. (Under this null-algorithm harness overflow
    // handling on the destination can emit an extra activate, so assert that a2
    // is among the activations rather than pinning an exact count.)
    QVERIFY(activateSpy.count() >= 1);
    bool activatedA2 = false;
    for (const QList<QVariant>& call : activateSpy) {
        if (call.at(0).toString() == QLatin1String("a2")) {
            activatedA2 = true;
            break;
        }
    }
    QVERIFY2(activatedA2, "the moved window must be activated on its new output");
}

void TestNavigationCrossSurface::entryWindowForCrossing_picksEdgeTileFacingSource()
{
    // Binary split: win1 left (x=0), win2 top-right (x=960,y=0), win3 bottom-right
    // (x=960,y=540). The entry window for a crossing arriving in `direction` is the
    // tile at the edge facing back toward the source — i.e. the extreme tile on
    // the OPPOSITE edge from the travel direction.
    std::unique_ptr<AutotileEngine> engine(PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 3));
    installBinarySplitLayout(engine.get());
    const QStringList wins = engine->tilingStateForScreen(kScreen)->tiledWindows();

    // Crossing right enters the target's LEFT edge → the leftmost tile (win1).
    QCOMPARE(engine->entryWindowForCrossing(kScreen, QStringLiteral("right")), wins.at(0));
    // Crossing left enters the RIGHT edge → a right-column tile (win2/win3).
    const QString leftEntry = engine->entryWindowForCrossing(kScreen, QStringLiteral("left"));
    QVERIFY2(leftEntry == wins.at(1) || leftEntry == wins.at(2), "left crossing enters a right-column tile");
    // Crossing down enters the TOP edge; up enters the BOTTOM edge.
    const QString downEntry = engine->entryWindowForCrossing(kScreen, QStringLiteral("down"));
    QVERIFY2(downEntry == wins.at(0) || downEntry == wins.at(1), "down crossing enters a top-row tile");
    QCOMPARE(engine->entryWindowForCrossing(kScreen, QStringLiteral("up")), wins.at(2)); // bottom-right
}

void TestNavigationCrossSurface::crossOutput_swapTowardNonAutotileOutput_emitsCrossModeSwap()
{
    // DP-2 is a SNAP (non-autotile) neighbour. An autotile SWAP toward it is a
    // cross-MODE swap: crossOutputMove must emit crossModeSwapRequested (the
    // daemon does the two-way exchange) — NOT crossModeMoveRequested.
    PhosphorScreens::FakeScreenProvider provider;
    provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
    provider.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080));
    auto manager = std::make_unique<PhosphorScreens::ScreenManager>(
        PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
    manager->start();
    auto engine =
        std::make_unique<AutotileEngine>(nullptr, nullptr, manager.get(), PlasmaZones::TestHelpers::testRegistry());
    auto resolver = std::make_unique<PlasmaZones::CrossSurfaceResolver>(manager.get(), nullptr);
    engine->setCrossSurfaceResolver(resolver.get());
    engine->setAutotileScreens({QStringLiteral("DP-1")}); // DP-2 deliberately NOT autotile
    engine->windowOpened(QStringLiteral("a1"), QStringLiteral("DP-1"));
    engine->windowOpened(QStringLiteral("a2"), QStringLiteral("DP-1"));
    QCoreApplication::processEvents();
    engine->tilingStateForScreen(QStringLiteral("DP-1"))
        ->setCalculatedZones({QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)});

    QSignalSpy swapSpy(engine.get(), &AutotileEngine::crossModeSwapRequested);
    QSignalSpy moveSpy(engine.get(), &AutotileEngine::crossModeMoveRequested);
    // a2 is the right tile — swapping it "right" reaches the DP-2 snap boundary.
    engine->swapFocusedInDirection(QStringLiteral("right"),
                                   NavigationContext{QStringLiteral("a2"), QStringLiteral("DP-1")});

    QCOMPARE(moveSpy.count(), 0); // a swap, not a move
    QCOMPARE(swapSpy.count(), 1);
    const QList<QVariant> args = swapSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("a2"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("DP-2"));
    QCOMPARE(args.at(2).toInt(), 0); // monitor crossing, current desktop
    QCOMPARE(args.at(3).toString(), QStringLiteral("right"));
}

void TestNavigationCrossSurface::crossOutput_moveTowardNonAutotileOutput_doesNotStrandWindow()
{
    // Two side-by-side outputs, but only DP-1 is autotile-managed. The
    // CrossSurfaceResolver still returns DP-2 as the geometric right-neighbour
    // (it has no autotile knowledge), so moving a2 "right" reaches crossOutputMove
    // with a NON-autotile destination. migrateWindowBetweenKeys would remove a2
    // from DP-1 but never re-add it on a non-autotile screen — stranding it
    // (tracked nowhere). crossOutputMove must REFUSE before mutating any state:
    // no windowOutputMoveExpected marker, and a2 stays tiled and tracked on DP-1.
    PhosphorScreens::FakeScreenProvider provider;
    provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
    provider.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080));
    auto manager = std::make_unique<PhosphorScreens::ScreenManager>(
        PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
    manager->start();
    auto engine =
        std::make_unique<AutotileEngine>(nullptr, nullptr, manager.get(), PlasmaZones::TestHelpers::testRegistry());
    auto resolver = std::make_unique<PlasmaZones::CrossSurfaceResolver>(manager.get(), nullptr);
    engine->setCrossSurfaceResolver(resolver.get());
    engine->setAutotileScreens({QStringLiteral("DP-1")}); // DP-2 deliberately NOT autotile
    engine->windowOpened(QStringLiteral("a1"), QStringLiteral("DP-1"));
    engine->windowOpened(QStringLiteral("a2"), QStringLiteral("DP-1"));
    QCoreApplication::processEvents();
    engine->tilingStateForScreen(QStringLiteral("DP-1"))
        ->setCalculatedZones({QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)});

    QSignalSpy expectedSpy(engine.get(), &AutotileEngine::windowOutputMoveExpected);
    engine->moveFocusedInDirection(QStringLiteral("right"),
                                   NavigationContext{QStringLiteral("a2"), QStringLiteral("DP-1")});

    QCOMPARE(expectedSpy.count(), 0);
    QVERIFY(engine->isWindowTracked(QStringLiteral("a2")));
    QVERIFY(engine->tilingStateForScreen(QStringLiteral("DP-1"))->containsWindow(QStringLiteral("a2")));
}

void TestNavigationCrossSurface::crossOutput_moveTowardFullAutotileOutput_doesNotStrandWindow()
{
    TwoOutputFixture fx;
    // DP-2 is autotile but already holds maxWindows tiled windows (b1, b2).
    // Moving a2 "right" toward it reaches crossOutputMove with a FULL autotile
    // destination: onWindowAdded rejects the insert (count >= max) AFTER a2 was
    // already removed from DP-1 and re-keyed — stranding it. The capacity guard
    // must refuse before any state mutation or marker emit: a2 stays tiled and
    // tracked on DP-1.
    fx.engine->config()->maxWindows = 2;

    QSignalSpy expectedSpy(fx.engine.get(), &AutotileEngine::windowOutputMoveExpected);
    fx.engine->moveFocusedInDirection(QStringLiteral("right"),
                                      NavigationContext{QStringLiteral("a2"), QStringLiteral("DP-1")});

    QCOMPARE(expectedSpy.count(), 0);
    QVERIFY(fx.engine->isWindowTracked(QStringLiteral("a2")));
    QVERIFY(fx.engine->tilingStateForScreen(QStringLiteral("DP-1"))->containsWindow(QStringLiteral("a2")));
}

void TestNavigationCrossSurface::inSurfaceMove_doesNotEmitExpectedMove()
{
    TwoOutputFixture fx;
    // a1 is the left window on DP-1 — moving it "right" swaps with a2 IN PLACE
    // on the same output. No physical output change occurs, so no
    // windowOutputMoveExpected marker may be emitted (a spurious marker would
    // suppress a genuine later user-drag transfer of that window).
    QSignalSpy expectedSpy(fx.engine.get(), &AutotileEngine::windowOutputMoveExpected);

    fx.engine->moveFocusedInDirection(QStringLiteral("right"),
                                      NavigationContext{QStringLiteral("a1"), QStringLiteral("DP-1")});

    QCOMPARE(expectedSpy.count(), 0);
    // Both windows remain on DP-1 (an in-surface swap, not a cross-output move).
    const QStringList dp1 = fx.engine->tilingStateForScreen(QStringLiteral("DP-1"))->tiledWindows();
    QVERIFY(dp1.contains(QStringLiteral("a1")));
    QVERIFY(dp1.contains(QStringLiteral("a2")));
}

void TestNavigationCrossSurface::crossVirtualScreen_focusRight_crossesToSiblingVirtualScreen()
{
    // Virtual screens are standardised first-class outputs: one physical
    // monitor split into two virtual screens. Cross-output navigation must
    // treat the sibling virtual screen as the neighbour output — the resolver
    // iterates effectiveScreenIds() (which yields the vs IDs) and reads their
    // virtual geometry, exactly like physical outputs.
    const QString physId = QStringLiteral("DP-1");
    PhosphorScreens::FakeScreenProvider provider;
    provider.addScreen(physId, QRect(0, 0, 1920, 1080));
    PhosphorScreens::ScreenManager manager(
        PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
    manager.start();
    manager.setVirtualScreenConfig(physId, PlasmaZones::TestHelpers::makeSplitConfig(physId));

    const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0); // left half
    const QString vs1 = PhosphorIdentity::VirtualScreenId::make(physId, 1); // right half
    // Precondition: the manager presents both virtual screens as effective
    // outputs with valid (left/right half) geometry.
    QVERIFY(manager.effectiveScreenIds().contains(vs0));
    QVERIFY(manager.effectiveScreenIds().contains(vs1));
    QVERIFY(manager.screenGeometry(vs0).isValid());

    AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
    PlasmaZones::CrossSurfaceResolver resolver(&manager, nullptr);
    engine.setCrossSurfaceResolver(&resolver);
    engine.setAutotileScreens({vs0, vs1});
    engine.windowOpened(QStringLiteral("a"), vs0);
    engine.windowOpened(QStringLiteral("b"), vs1);
    QCoreApplication::processEvents();
    // Each virtual screen has one full-half window (global coordinates).
    engine.tilingStateForScreen(vs0)->setCalculatedZones({QRect(0, 0, 960, 1080)});
    engine.tilingStateForScreen(vs1)->setCalculatedZones({QRect(960, 0, 960, 1080)});

    // Focus right from the only window on the LEFT virtual screen → cross to the
    // RIGHT virtual screen's window.
    QSignalSpy activateSpy(&engine, &AutotileEngine::activateWindowRequested);
    engine.focusInDirection(QStringLiteral("right"), NavigationContext{QStringLiteral("a"), vs0});
    QCOMPARE(activateSpy.count(), 1);
    QCOMPARE(activateSpy.takeFirst().at(0).toString(), QStringLiteral("b"));
}

void TestNavigationCrossSurface::crossDesktop_focusRight_activatesEntryWindowOnNextDesktop()
{
    std::unique_ptr<AutotileEngine> engine(PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 2));
    FakeCrossSurfaceResolver resolver;
    engine->setCrossSurfaceResolver(&resolver);

    // Seed a window on desktop 2, then return to desktop 1.
    engine->setCurrentDesktop(2);
    engine->windowOpened(QStringLiteral("d2win"), kScreen);
    QCoreApplication::processEvents();
    engine->setCurrentDesktop(1);

    PhosphorTiles::TilingState* d1 = engine->tilingStateForScreen(kScreen);
    d1->setCalculatedZones({QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)});
    const QStringList wins = d1->tiledWindows();

    // Focus right from the rightmost window on desktop 1: no in-surface
    // neighbour, no neighbour output → cross to desktop 2's entry window.
    QSignalSpy activateSpy(engine.get(), &AutotileEngine::activateWindowRequested);
    engine->focusInDirection(QStringLiteral("right"), ctx(wins.at(1)));
    QCOMPARE(activateSpy.count(), 1);
    QCOMPARE(activateSpy.takeFirst().at(0).toString(), QStringLiteral("d2win"));
}

void TestNavigationCrossSurface::crossDesktop_moveRight_relocatesToNextDesktopAndRequestsKWinMove()
{
    std::unique_ptr<AutotileEngine> engine(PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 2));
    FakeCrossSurfaceResolver resolver;
    engine->setCrossSurfaceResolver(&resolver);

    PhosphorTiles::TilingState* d1 = engine->tilingStateForScreen(kScreen);
    d1->setCalculatedZones({QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)});
    const QStringList wins = d1->tiledWindows();
    const QString rightmost = wins.at(1);

    QSignalSpy moveSpy(engine.get(), &AutotileEngine::windowDesktopMoveRequested);
    // Move right off the edge of desktop 1 → ask the compositor to relocate the
    // real window to desktop 2. The engine does NOT mutate tiling state here: a
    // desktop move is owned by the compositor, exactly like a native KWin
    // desktop move. The reactive windowClosed (effect: "moved off current
    // desktop") reflows the source, and windowOpened tiles it on the target when
    // that desktop becomes visible. Proactively shuffling state here is what
    // corrupted m_windowToStateKey and left windows tracked nowhere (stuck
    // decoration / no reflow). End-to-end reflow is covered by
    // test_navigation_retile with a real algorithm.
    //
    // MOVE owns cross-desktop relocation (swap does not cross desktops).
    engine->moveFocusedInDirection(QStringLiteral("right"), ctx(rightmost));

    // The compositor was asked to move the real window to desktop 2.
    QCOMPARE(moveSpy.count(), 1);
    const QList<QVariant> args = moveSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), rightmost);
    QCOMPARE(args.at(1).toInt(), 2);
}

void TestNavigationCrossSurface::crossDesktop_swapRight_doesNotRelocate()
{
    // Swap is NOT extended across virtual desktops — only MOVE relocates to an
    // adjacent desktop. Swapping the edge window toward the next desktop is a
    // no-op: no windowDesktopMoveRequested.
    std::unique_ptr<AutotileEngine> engine(PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 2));
    FakeCrossSurfaceResolver resolver;
    engine->setCrossSurfaceResolver(&resolver);
    PhosphorTiles::TilingState* d1 = engine->tilingStateForScreen(kScreen);
    d1->setCalculatedZones({QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)});
    const QString rightmost = d1->tiledWindows().at(1);

    QSignalSpy moveSpy(engine.get(), &AutotileEngine::windowDesktopMoveRequested);
    engine->swapFocusedInDirection(QStringLiteral("right"), ctx(rightmost));

    QCOMPARE(moveSpy.count(), 0); // swap stops at the desktop boundary
}

void TestNavigationCrossSurface::crossDesktop_focusLeft_atGridEdge_doesNotActivate()
{
    // Desktop 1 is the left edge of the grid: FakeCrossSurfaceResolver returns 0
    // ("no neighbour") for "left" there. Focusing left from the leftmost window
    // has no in-surface neighbour, no neighbour output, and no neighbour desktop
    // — nothing may be activated.
    std::unique_ptr<AutotileEngine> engine(PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 2));
    FakeCrossSurfaceResolver resolver;
    engine->setCrossSurfaceResolver(&resolver);
    engine->setCurrentDesktop(1);

    PhosphorTiles::TilingState* d1 = engine->tilingStateForScreen(kScreen);
    d1->setCalculatedZones({QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)});
    const QStringList wins = d1->tiledWindows();

    QSignalSpy activateSpy(engine.get(), &AutotileEngine::activateWindowRequested);
    engine->focusInDirection(QStringLiteral("left"), ctx(wins.at(0))); // leftmost window
    QCOMPARE(activateSpy.count(), 0);
}

void TestNavigationCrossSurface::crossDesktop_moveLeft_atGridEdge_doesNotRequestKWinMove()
{
    // Mirror of the focus case for move/swap: at the grid's left edge there is no
    // neighbour output and no neighbour desktop, so the window must be left
    // untouched — neither a KWin desktop move nor an output-move marker fires.
    std::unique_ptr<AutotileEngine> engine(PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 2));
    FakeCrossSurfaceResolver resolver;
    engine->setCrossSurfaceResolver(&resolver);
    engine->setCurrentDesktop(1);

    PhosphorTiles::TilingState* d1 = engine->tilingStateForScreen(kScreen);
    d1->setCalculatedZones({QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)});
    const QStringList wins = d1->tiledWindows();
    const QString leftmost = wins.at(0);

    QSignalSpy moveSpy(engine.get(), &AutotileEngine::windowDesktopMoveRequested);
    QSignalSpy expectedSpy(engine.get(), &AutotileEngine::windowOutputMoveExpected);
    engine->swapFocusedInDirection(QStringLiteral("left"), ctx(leftmost));
    QCOMPARE(moveSpy.count(), 0);
    QCOMPARE(expectedSpy.count(), 0);
    QVERIFY(engine->tilingStateForScreen(kScreen)->containsWindow(leftmost));
}

void TestNavigationCrossSurface::stickyPinnedScreen_explicitFocusResolvesPinnedState()
{
    // A screen sticky-pinned (virtualdesktopsonlyonprimary) to a non-current
    // desktop keeps its TilingState on the pinned desktop. An explicit-window
    // focus request for a window on that pinned screen must resolve the pinned
    // state — even when m_activeScreen (and its focused window) points at a
    // DIFFERENT screen on the current desktop, which would otherwise hijack the
    // resolution. Regression guard for the desktop-filter scan in
    // tiledWindowsForFocusedScreen being sticky-pin aware.
    TwoOutputFixture fx;
    // Pin DP-1 (a1, a2 both "sticky"/on-all-desktops); DP-2 stays unpinned.
    fx.engine->updateStickyScreenPins([](const QString& w) {
        return w == QLatin1String("a1") || w == QLatin1String("a2");
    });

    // Switch desktops: DP-1 stays pinned to desktop 1; DP-2 follows to desktop 2.
    fx.engine->setCurrentDesktop(2);

    // Give DP-2 a focused window on desktop 2 so the active-screen path resolves
    // to DP-2 — the decoy that the OLD bare `!= m_currentDesktop` scan would
    // wrongly fall through to instead of finding the pinned DP-1 state.
    fx.engine->windowOpened(QStringLiteral("c1"), QStringLiteral("DP-2"));
    QCoreApplication::processEvents();
    fx.engine->windowFocused(QStringLiteral("c1"), QStringLiteral("DP-2")); // m_activeScreen = DP-2

    // Re-assert DP-1's pinned-state geometry right before the focus (the desktop
    // switch / window-open may have recomputed zones).
    fx.engine->tilingStateForScreen(QStringLiteral("DP-1"))
        ->setCalculatedZones({QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)});

    // Focus right from a1 (left window on the pinned DP-1): the explicit-window
    // resolution must find DP-1's pinned state and land on a2 — NOT operate on
    // DP-2 where m_activeScreen points.
    QSignalSpy activateSpy(fx.engine.get(), &AutotileEngine::activateWindowRequested);
    fx.engine->focusInDirection(QStringLiteral("right"),
                                NavigationContext{QStringLiteral("a1"), QStringLiteral("DP-1")});
    QCOMPARE(activateSpy.count(), 1);
    QCOMPARE(activateSpy.takeFirst().at(0).toString(), QStringLiteral("a2"));
}

QTEST_MAIN(TestNavigationCrossSurface)
#include "test_navigation_cross_surface.moc"
