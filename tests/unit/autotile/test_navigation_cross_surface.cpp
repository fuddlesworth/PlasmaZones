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

#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "../helpers/AutotileTestHelpers.h"

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

} // namespace

class TestNavigationCrossSurface : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void focusRight_fromLeftColumn_picksWindowToTheRight();
    void focus_fromTopRight_resolvesEachDirectionGeometrically();
    void swap_exchangesWithSpatialNeighbour();
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

QTEST_MAIN(TestNavigationCrossSurface)
#include "test_navigation_cross_surface.moc"
