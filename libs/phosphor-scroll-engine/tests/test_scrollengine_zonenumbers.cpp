// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The zone-number space and the verbs that address it: what a digit press
// resolves to (visibleTiles), what it actually MOVES (a stack-mate reorder
// inside the column, a whole-column travel across columns), what it refuses,
// and the cross-output crossings whose landing slots the swap contract
// promises. Split out of test_scrollengine_smoke.cpp, which is at its size
// ceiling; the fixtures are that file's makeProviderEngine shape.
//
// Geometry throughout: 1200x800 work area, no IScrollSettings (so the inner
// gap is 0) and a 600px default column, which makes exactly two columns fit.

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

namespace {

/// Two outputs side by side: S1's right neighbour is S2 and S2's left is S1.
struct SideBySideResolver : PhosphorEngine::ICrossSurfaceResolver
{
    QString neighborOutputInDirection(const QString& screenId, const QString& direction) const override
    {
        if (direction == QLatin1String("right") && screenId == QLatin1String("S1")) {
            return QStringLiteral("S2");
        }
        if (direction == QLatin1String("left") && screenId == QLatin1String("S2")) {
            return QStringLiteral("S1");
        }
        return QString();
    }
    int neighborDesktopInDirection(int, const QString&) const override
    {
        return 0;
    }
};

QString wid(const char* suffix)
{
    return QLatin1String("app|") + QLatin1String(suffix);
}

} // namespace

class TestScrollEngineZoneNumbers : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void crossColumnDigitMovesTheWholeColumn();
    void intraColumnDigitReordersStackMate();
    void outOfRangeDigitIsRejected();
    void digitNamingTheOperatedWindowIsANoOp();
    void hiddenTabCarriesNoNumber();
    void clippedEdgeTilesKeepTheirNumbers();
    void crossOutputMoveKeepsHeightAndAnnouncesOnDestination();
    void crossOutputSwapTradesSlotsWithStackedSource();

private:
    /// One screen with the geometry-provider seam wired, so the apply path
    /// resolves real rects (only then do lastManagedRect / visibleTiles have
    /// anything to say).
    static ScrollEngine* makeProviderEngine(QObject* parent)
    {
        auto* engine = new ScrollEngine(nullptr, nullptr, parent);
        const auto geometry = [](const QString&) {
            return QRect(0, 0, 1200, 800);
        };
        engine->setScreenGeometryProviders(geometry, geometry);
        engine->setActiveScreens({QStringLiteral("S1")});
        return engine;
    }

    /// S1 and S2 as adjacent 1200x800 outputs, both scrolling.
    static ScrollEngine* makeTwoScreenEngine(QObject* parent)
    {
        auto* engine = new ScrollEngine(nullptr, nullptr, parent);
        const auto geometry = [](const QString& screenId) {
            return screenId == QLatin1String("S2") ? QRect(1200, 0, 1200, 800) : QRect(0, 0, 1200, 800);
        };
        engine->setScreenGeometryProviders(geometry, geometry);
        engine->setActiveScreens({QStringLiteral("S1"), QStringLiteral("S2")});
        return engine;
    }

    static int numberOf(ScrollEngine* engine, const char* suffix)
    {
        return engine->visibleTileNumberForWindow(QStringLiteral("S1"), wid(suffix));
    }

    static PhosphorEngine::NavigationContext ctxFor(const char* suffix)
    {
        return PhosphorEngine::NavigationContext{wid(suffix), QStringLiteral("S1")};
    }
};

void TestScrollEngineZoneNumbers::crossColumnDigitMovesTheWholeColumn()
{
    // THE documented address-vs-action divergence (ScrollEngine.h,
    // VisibleTile): a digit naming a tile in ANOTHER column moves the whole
    // active COLUMN to that column's strip position, stack-mates included.
    // The window the user operated therefore does NOT end up wearing the
    // number that was pressed — asserted here so the semantics cannot drift
    // silently in either direction.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("x"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1")); // x joins a's column
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);

    // Two 600px columns fill the work area, so all three tiles are visible:
    // the stacked pair first (column-major), then b.
    QCOMPARE(numberOf(engine, "a"), 1);
    QCOMPARE(numberOf(engine, "x"), 2);
    QCOMPARE(numberOf(engine, "b"), 3);

    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    engine->moveFocusedToPosition(3, ctxFor("a"));

    // a's column travelled to b's slot, carrying x with it.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("b"), wid("a"), wid("x")}));
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("b")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("a")), 1);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("x")), 1);
    // And the operated window now reads 2, not the 3 that was pressed.
    QCOMPARE(numberOf(engine, "a"), 2);
    QCOMPARE(numberOf(engine, "b"), 1);
}

void TestScrollEngineZoneNumbers::intraColumnDigitReordersStackMate()
{
    // A digit naming a STACK-MATE reorders the operated window inside its
    // column, in both directions, and here the pressed number is exactly
    // what the window ends up wearing.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("x"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    engine->windowOpened(wid("y"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("a"), wid("x"), wid("y")}));
    QCOMPARE(engine->visibleTiles(QStringLiteral("S1")).size(), 3);

    // Downward: the top tile onto the bottom slot.
    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    engine->moveFocusedToPosition(3, ctxFor("a"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("x"), wid("y"), wid("a")}));
    QCOMPARE(numberOf(engine, "a"), 3);

    // Upward: and back to the top.
    engine->moveFocusedToPosition(1, ctxFor("a"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("a"), wid("x"), wid("y")}));
    QCOMPARE(numberOf(engine, "a"), 1);
}

void TestScrollEngineZoneNumbers::outOfRangeDigitIsRejected()
{
    // A digit the strip cannot honour is REFUSED, not clamped onto the
    // nearest tile (SnapEngine's convention): clamping moved a window the
    // user never named. 0 also pins the pre-subtraction bound — position - 1
    // on an unvalidated int is one adaptor away from the wire.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    const QStringList before = engine->managedWindowOrder(QStringLiteral("S1"));

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    for (const int position : {0, 3, -1}) {
        const int emitted = feedback.count();
        engine->moveFocusedToPosition(position, ctxFor("a"));
        QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), before);
        // Exactly one emission per rejection — a silent position would
        // otherwise re-read the previous iteration's feedback.
        QVERIFY2(feedback.count() == emitted + 1,
                 qPrintable(
                     QStringLiteral("position %1 emitted %2 feedbacks").arg(position).arg(feedback.count() - emitted)));
        QVERIFY2(!feedback.last().at(0).toBool(),
                 qPrintable(QStringLiteral("position %1 reported success").arg(position)));
        QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("snap"));
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("invalid_zone_number"));
    }
}

void TestScrollEngineZoneNumbers::digitNamingTheOperatedWindowIsANoOp()
{
    // The digit already naming the operated window is a no-op reported with
    // NavigationController's reason token, so the OSD shows the dedicated
    // "already in that position" copy instead of a generic failure.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    const QStringList before = engine->managedWindowOrder(QStringLiteral("S1"));

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->moveFocusedToPosition(1, ctxFor("a"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), before);
    QCOMPARE(feedback.count(), 1);
    QVERIFY(!feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("snap"));
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("already_at_position"));
}

void TestScrollEngineZoneNumbers::hiddenTabCarriesNoNumber()
{
    // A tabbed column shows one tile; its other tabs are hidden, drop out of
    // the walk, and report no number at all — so no digit can target them.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("x"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    engine->windowFocused(wid("a"), QStringLiteral("S1"));
    QCOMPARE(engine->visibleTiles(QStringLiteral("S1")).size(), 2);

    engine->toggleColumnTabbed(QStringLiteral("S1"));

    QCOMPARE(engine->visibleTiles(QStringLiteral("S1")).size(), 1);
    QCOMPARE(numberOf(engine, "a"), 1);
    QCOMPARE(numberOf(engine, "x"), -1);
    // The window is still MANAGED — it lost its number, not its tile.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")).size(), 2);
    QVERIFY(engine->isWindowTracked(wid("x")));
}

void TestScrollEngineZoneNumbers::clippedEdgeTilesKeepTheirNumbers()
{
    // "Clipped, not dropped": centering the middle of three 600px columns
    // pushes its neighbours halfway off both edges. They keep their numbers,
    // there is no minimum-visibility threshold, and the rects the walk
    // reports are the CLIPPED ones even though the applied geometry is not.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("c"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(wid("b"), QStringLiteral("S1"));
    engine->centerColumn(QStringLiteral("S1"));

    const QVector<ScrollEngine::VisibleTile> tiles = engine->visibleTiles(QStringLiteral("S1"));
    QCOMPARE(tiles.size(), 3);
    QCOMPARE(tiles.at(0).windowId, wid("a"));
    QCOMPARE(tiles.at(0).rect, QRect(0, 0, 300, 800));
    QCOMPARE(tiles.at(1).windowId, wid("b"));
    QCOMPARE(tiles.at(1).rect, QRect(300, 0, 600, 800));
    QCOMPARE(tiles.at(2).windowId, wid("c"));
    QCOMPARE(tiles.at(2).rect, QRect(900, 0, 300, 800));
    QCOMPARE(numberOf(engine, "a"), 1);
    QCOMPARE(numberOf(engine, "b"), 2);
    QCOMPARE(numberOf(engine, "c"), 3);

    // The APPLIED geometry keeps the full width; only the walk clips.
    QCOMPARE(engine->lastManagedRect(wid("a")), QRect(-300, 0, 600, 800));
}

void TestScrollEngineZoneNumbers::crossOutputMoveKeepsHeightAndAnnouncesOnDestination()
{
    // Moving off the strip's right edge migrates the window to the adjacent
    // scrolling output: it enters at the facing edge (column 0 for a "right"
    // crossing), keeps the user's height intent, and the success feedback
    // names the DESTINATION screen — the source no longer holds the window
    // the OSD is about.
    //
    // Resolver BEFORE owner: the engine (owned by `owner`) must be destroyed
    // before the resolver it borrows, and a failing QCOMPARE returns before
    // the trailing setCrossSurfaceResolver(nullptr) runs.
    SideBySideResolver resolver;
    QObject owner;
    ScrollEngine* engine = makeTwoScreenEngine(&owner);
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("p"), QStringLiteral("S2"), 0, 0);

    engine->windowFocused(wid("b"), QStringLiteral("S1"));
    engine->cycleWindowPresetHeight(1, QStringLiteral("S1"));
    const int height = engine->lastManagedRect(wid("b")).height();
    QVERIFY2(height > 0 && height < 800, qPrintable(QStringLiteral("expected a preset height, got %1").arg(height)));

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->moveFocusedInDirection(QStringLiteral("right"),
                                   PhosphorEngine::NavigationContext{wid("b"), QStringLiteral("S1")});

    QCOMPARE(engine->screenForTrackedWindow(wid("b")), QStringLiteral("S2"));
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S2"), wid("b")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S2"), wid("p")), 1);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("a")}));
    // The height intent crossed with it (insertWindowAt builds a default
    // Auto tile, so this only holds because the crossing re-applies it).
    QCOMPARE(engine->lastManagedRect(wid("b")).height(), height);

    QCOMPARE(feedback.count(), 1);
    QVERIFY(feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("move"));
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("screen:right"));
    QCOMPARE(feedback.last().at(3).toString(), wid("b"));
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S2"));

    engine->setCrossSurfaceResolver(nullptr);
}

void TestScrollEngineZoneNumbers::crossOutputSwapTradesSlotsWithStackedSource()
{
    // The swap TRADES slots. When the crossing window came out of a STACK,
    // its vacated slot is a tile inside a surviving column, so the partner
    // must re-enter that column — landing it in a fresh column beside the
    // stack is the shape this pins against. The partner's height intent
    // crosses too, and the feedback names the destination screen.
    //
    // Resolver before owner: see the move test above.
    SideBySideResolver resolver;
    QObject owner;
    ScrollEngine* engine = makeTwoScreenEngine(&owner);
    engine->setCrossSurfaceResolver(&resolver);
    engine->windowOpened(wid("a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(wid("b"), QStringLiteral("S1"), 0, 0);
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1")); // b joins a's column
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("b")), 0);
    engine->windowOpened(wid("p"), QStringLiteral("S2"), 0, 0);

    engine->windowFocused(wid("p"), QStringLiteral("S2"));
    engine->cycleWindowPresetHeight(1, QStringLiteral("S2"));
    const int partnerHeight = engine->lastManagedRect(wid("p")).height();
    QVERIFY2(partnerHeight > 0 && partnerHeight < 800,
             qPrintable(QStringLiteral("expected a preset height, got %1").arg(partnerHeight)));

    engine->windowFocused(wid("b"), QStringLiteral("S1"));
    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
    engine->swapFocusedInDirection(QStringLiteral("right"),
                                   PhosphorEngine::NavigationContext{wid("b"), QStringLiteral("S1")});

    QCOMPARE(engine->screenForTrackedWindow(wid("b")), QStringLiteral("S2"));
    QCOMPARE(engine->screenForTrackedWindow(wid("p")), QStringLiteral("S1"));
    // The trade: p took b's TILE slot beside a. Both windows in column 0 is
    // the whole assertion — a fresh column for p would push a to index 1.
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("a")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), wid("p")), 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), QStringList({wid("a"), wid("p")}));
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S2"), wid("b")), 0);
    // A Preset height resolves against the column height, which is the work
    // area height on both outputs, so the surviving intent reads identically.
    QCOMPARE(engine->lastManagedRect(wid("p")).height(), partnerHeight);

    QCOMPARE(feedback.count(), 1);
    QVERIFY(feedback.last().at(0).toBool());
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("swap"));
    QCOMPARE(feedback.last().at(5).toString(), QStringLiteral("S2"));

    engine->setCrossSurfaceResolver(nullptr);
}

// GUILESS, matching the sibling engine suites: the coalesced retile and the
// prunes' deleteLater need a real event dispatcher.
QTEST_GUILESS_MAIN(TestScrollEngineZoneNumbers)

#include "test_scrollengine_zonenumbers.moc"
