// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// handoffReceive's join-a-column arm: HandoffContext::insertTileIndex names a
// slot INSIDE the column at insertIndex, which is how the workspace overview
// drops a window onto an existing tile of a scrolling workspace. The claims
// are structural (which column holds the arrival, and where in its stack),
// read straight off the state's strip, plus the two boundaries the arm has
// to respect: an insertIndex that names no live column falls back to the
// new-column path, and a receive into a context that is not on screen lays
// nothing out until that context becomes current.
//
// A sibling of test_scrollengine_smoke.cpp, which owns the release and the
// floating-arrival halves of the handoff and has no headroom left.

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"

#include <QSignalSpy>
#include <QStringList>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::makeProviderEngine;

namespace {
const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kA = QStringLiteral("app|a");
const QString kB = QStringLiteral("app|b");
const QString kC = QStringLiteral("app|c");
const QString kX = QStringLiteral("app|x");
} // namespace

class TestScrollEngineHandoff : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void tileIndexZeroLeadsTheNamedColumn();
    void tileIndexPastTheEndAppendsInsideTheColumn();
    void tileIndexIntoATabbedColumnAddsATab();
    void outOfRangeColumnFallsBackToANewColumn();
    void nonCurrentDesktopJoinsSilently();

private:
    static ScrollEngine* makeEngine(QObject* parent)
    {
        return makeProviderEngine(parent, {kS1, kS2});
    }

    static ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screenId));
    }

    /// Three single-tile columns a, b, c on S1's current context.
    static void openThree(ScrollEngine* engine)
    {
        engine->windowOpened(kA, kS1, 0, 0);
        engine->windowOpened(kB, kS1, 0, 0);
        engine->windowOpened(kC, kS1, 0, 0);
    }

    static PhosphorEngine::IPlacementEngine::HandoffContext joinCtx(int insertIndex, int insertTileIndex)
    {
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = kX;
        ctx.toScreenId = kS1;
        ctx.fromEngineId = QStringLiteral("snap");
        ctx.insertIndex = insertIndex;
        ctx.insertTileIndex = insertTileIndex;
        return ctx;
    }

    static QStringList tileOrder(const Column& column)
    {
        QStringList ids;
        for (const Tile& tile : column.tiles) {
            ids.append(tile.windowId);
        }
        return ids;
    }
};

void TestScrollEngineHandoff::tileIndexZeroLeadsTheNamedColumn()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    openThree(engine);

    engine->handoffReceive(joinCtx(1, 0));

    ScrollState* state = stateFor(engine, kS1);
    QVERIFY(state);
    const ScrollStrip& strip = state->strip();
    QCOMPARE(strip.columnCount(), 3);
    QCOMPARE(strip.columnOfWindow(kX), 1);
    QCOMPARE(tileOrder(strip.columns().at(1)), (QStringList{kX, kB}));
    QCOMPARE(tileOrder(strip.columns().at(0)), (QStringList{kA}));
    QCOMPARE(tileOrder(strip.columns().at(2)), (QStringList{kC}));
    QVERIFY(engine->isWindowTracked(kX));
    QCOMPARE(engine->screenForTrackedWindow(kX), kS1);
    QVERIFY(engine->isWindowTiled(kX));
}

void TestScrollEngineHandoff::tileIndexPastTheEndAppendsInsideTheColumn()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    openThree(engine);

    engine->handoffReceive(joinCtx(1, 99));

    ScrollState* state = stateFor(engine, kS1);
    QVERIFY(state);
    const ScrollStrip& strip = state->strip();
    QCOMPARE(strip.columnCount(), 3);
    QCOMPARE(tileOrder(strip.columns().at(1)), (QStringList{kB, kX}));
}

void TestScrollEngineHandoff::tileIndexIntoATabbedColumnAddsATab()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    openThree(engine);
    // Make b's column tabbed: the toggle acts on the active column.
    engine->windowFocused(kB, kS1);
    engine->toggleColumnTabbed(kS1);
    {
        ScrollState* state = stateFor(engine, kS1);
        QVERIFY(state);
        QCOMPARE(state->strip().columns().at(1).display, ColumnDisplay::Tabbed);
    }

    engine->handoffReceive(joinCtx(1, 0));

    ScrollState* state = stateFor(engine, kS1);
    QVERIFY(state);
    const ScrollStrip& strip = state->strip();
    QCOMPARE(strip.columnCount(), 3);
    const Column& column = strip.columns().at(1);
    QCOMPARE(column.display, ColumnDisplay::Tabbed);
    QCOMPARE(tileOrder(column), (QStringList{kX, kB}));
}

void TestScrollEngineHandoff::outOfRangeColumnFallsBackToANewColumn()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    openThree(engine);

    // insertIndex equals the column count, which names no live column: the
    // tile index is ignored and the arrival opens a column of its own at
    // that position, exactly as a receive with no tile index would.
    engine->handoffReceive(joinCtx(3, 0));

    ScrollState* state = stateFor(engine, kS1);
    QVERIFY(state);
    const ScrollStrip& strip = state->strip();
    QCOMPARE(strip.columnCount(), 4);
    QCOMPARE(strip.columnOfWindow(kX), 3);
    QCOMPARE(tileOrder(strip.columns().at(3)), (QStringList{kX}));
    QCOMPARE(tileOrder(strip.columns().at(1)), (QStringList{kB}));
}

void TestScrollEngineHandoff::nonCurrentDesktopJoinsSilently()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    // Desktop 2 holds the three-column strip; desktop 1 is current and empty.
    engine->setCurrentDesktopForScreen(kS1, 2);
    openThree(engine);
    engine->setCurrentDesktopForScreen(kS1, 1);
    QTest::qWait(50);
    QVERIFY(engine->stripSnapshot(kS1).columns.isEmpty());

    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    PhosphorEngine::IPlacementEngine::HandoffContext ctx = joinCtx(1, 0);
    ctx.toDesktop = 2;
    engine->handoffReceive(ctx);
    QTest::qWait(50);

    // Tracked on the target screen, but nothing laid out: the current
    // context is desktop 1, which the receive did not touch.
    QVERIFY(engine->isWindowTracked(kX));
    QCOMPARE(engine->screenForTrackedWindow(kX), kS1);
    QCOMPARE(tiledSpy.count(), 0);
    QVERIFY(engine->stripSnapshot(kS1).columns.isEmpty());

    // Once desktop 2 is current, the arrival sits where the drop put it.
    engine->setCurrentDesktopForScreen(kS1, 2);
    ScrollState* state = stateFor(engine, kS1);
    QVERIFY(state);
    const ScrollStrip& strip = state->strip();
    QCOMPARE(strip.columnCount(), 3);
    QCOMPARE(tileOrder(strip.columns().at(1)), (QStringList{kX, kB}));
}

QTEST_GUILESS_MAIN(TestScrollEngineHandoff)
#include "test_scrollengine_handoff.moc"
