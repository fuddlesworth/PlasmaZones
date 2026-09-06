// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The TILE-focus family at the engine layer: cycleTab, focusTab, and the
// contract they share with focusWindowTop/Bottom through
// P_SCROLL_TILE_FOCUS_VERB — refusing while the float layer holds focus, and
// handing a panned view back to the centering policy on success.
//
// Its own file rather than more of test_scrollengine_smoke, whose sanctioned
// size exception says in as many words that a new concern takes a sibling.
// The strip-level primitives these verbs drive (focusAdjacentTile,
// focusTileAtEnd, focusTileByOrdinal) are pinned in test_scrollstrip_ops;
// what this file owns is what the ENGINE adds on top of them.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::makeProviderEngine;

class TestScrollEngineTabFocus : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cycleTabWrapsInsteadOfLeavingTheColumn();
    void focusTabAddressesTabsByOrdinal();
    void floatingATabRenumbersTheOrdinals();
    void tabVerbsRefuseWhileFloatingHoldsFocus();
    void tileFocusHandsAPannedViewBackToThePolicy();

private:
    /// The engine helpers below all resolve the state through this, so a
    /// missing screen fails at the QVERIFY rather than as a null deref deeper
    /// in a slot.
    static ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screenId));
    }
};

void TestScrollEngineTabFocus::cycleTabWrapsInsteadOfLeavingTheColumn()
{
    // The whole reason this verb exists beside the generic directional focus:
    // focusAdjacentTile refuses at the stack edge, and focusInDirection turns
    // that refusal into a CROSS-OUTPUT hop. A user cycling tabs must land back
    // on the first tab instead. Two screens, and S2 is POPULATED and asserted
    // on: an empty second screen cannot show a leak, because focus landing
    // nowhere and focus landing on an unoccupied output look identical.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    engine->windowOpened(QStringLiteral("other|z"), QStringLiteral("S2"), 0, 0);
    engine->windowFocused(QStringLiteral("other|z"), QStringLiteral("S2"));
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    // THREE tiles, so a middle position exists. With only two, "step forward
    // off tile 1" and "wrap forward off tile 2" both land on the same tile
    // and the adjacent arm is never separated from the wrap arm.
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));

    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    ScrollState* other = stateFor(engine, QStringLiteral("S2"));
    QVERIFY(other);
    const QString s2Before = other->strip().activeWindowId();
    QVERIFY2(!s2Before.isEmpty(), "precondition: S2 must hold a window for a leak to be visible");

    // Seed a known end so the wrap arm is the one under test rather than a
    // plain adjacent step that happens to succeed.
    engine->focusWindowTop(QStringLiteral("S1"));
    const QString first = state->strip().activeWindowId();
    QVERIFY(!first.isEmpty());
    QVERIFY(state->strip().activeColumn());
    QCOMPARE(state->strip().activeColumn()->tiles.size(), 3);

    // A successful verb must APPLY, not merely move activeTileIdx: the
    // relayout and the announce are what raise the tab and redraw the
    // indicator, so a cycleTab that skipped them would reach the user as a
    // chord that does visibly nothing while this test still passed.
    QSignalSpy placementSpy(engine, &ScrollEngine::placementChanged);
    QSignalSpy feedbackSpy(engine, &ScrollEngine::navigationFeedback);

    // Forward off the first tab is an ordinary step, onto the MIDDLE tab.
    engine->cycleTab(1, QStringLiteral("S1"));
    const QString second = state->strip().activeWindowId();
    QVERIFY2(second != first, "precondition: the column must have a second tab to step onto");
    QCOMPARE(placementSpy.count(), 1);
    QCOMPARE(feedbackSpy.count(), 1);
    QCOMPARE(feedbackSpy.takeFirst().at(0).toBool(), true);

    // Forward off the MIDDLE tab reaches the third rather than wrapping: the
    // adjacent arm short-circuits the wrap fallback.
    engine->cycleTab(1, QStringLiteral("S1"));
    const QString third = state->strip().activeWindowId();
    QVERIFY2(third != first && third != second, "a middle step must reach the third tab, not wrap to the first");

    // Forward off the LAST tab wraps to the first, and stays on this screen.
    engine->cycleTab(1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), first);
    QCOMPARE(other->strip().activeWindowId(), s2Before);

    // Backward off the first wraps the other way, to the LAST tab.
    engine->cycleTab(-1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), third);
    QCOMPARE(other->strip().activeWindowId(), s2Before);

    // Out-of-contract deltas are refused outright: a zero must not read as a
    // press, and must not short-circuit into the wrap fallback. A refusal
    // emits nothing at all, since the verb returns before the macro runs.
    placementSpy.clear();
    feedbackSpy.clear();
    engine->cycleTab(0, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), third);
    engine->cycleTab(7, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), third);
    QCOMPARE(placementSpy.count(), 0);
    QCOMPARE(feedbackSpy.count(), 0);
    QCOMPARE(other->strip().activeWindowId(), s2Before);
}

void TestScrollEngineTabFocus::focusTabAddressesTabsByOrdinal()
{
    // The verb behind all nine Focus Tab shortcuts. Its refusal arms matter
    // as much as its success one: an out-of-range ordinal that silently
    // landed somewhere else would reach the user as a chord that jumps to the
    // wrong tab, which is worse than one that does nothing.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));

    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->strip().activeColumn());
    QCOMPARE(state->strip().activeColumn()->tiles.size(), 3);
    engine->focusWindowTop(QStringLiteral("S1"));
    const QString first = state->strip().activeWindowId();
    QVERIFY(!first.isEmpty());

    // Ordinals are 1-based and count the column's tiles in order.
    engine->focusTab(3, QStringLiteral("S1"));
    const QString third = state->strip().activeWindowId();
    QVERIFY2(third != first, "ordinal 3 must not resolve to the first tab");
    engine->focusTab(2, QStringLiteral("S1"));
    const QString second = state->strip().activeWindowId();
    QVERIFY2(second != first && second != third, "ordinal 2 must name the middle tab");
    engine->focusTab(1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), first);

    // A successful ordinal applies and announces, like every other focus verb.
    QSignalSpy placementSpy(engine, &ScrollEngine::placementChanged);
    QSignalSpy feedbackSpy(engine, &ScrollEngine::navigationFeedback);
    engine->focusTab(2, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), second);
    QCOMPARE(placementSpy.count(), 1);
    QCOMPARE(feedbackSpy.count(), 1);
    const QList<QVariant> ok = feedbackSpy.takeFirst();
    QCOMPARE(ok.at(0).toBool(), true);
    // "tab", not an empty reason: the OSD's arrow default points right, so an
    // empty reason on the focus action would draw a glyph for a verb that has
    // no direction.
    QCOMPARE(ok.at(2).toString(), QStringLiteral("tab"));

    // Already there refuses rather than re-announcing a move that did not
    // happen.
    placementSpy.clear();
    feedbackSpy.clear();
    engine->focusTab(2, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), second);
    QCOMPARE(placementSpy.count(), 0);
    QCOMPARE(feedbackSpy.count(), 1);
    QCOMPARE(feedbackSpy.takeFirst().at(0).toBool(), false);

    // Past the tab count refuses and leaves focus alone. It still ANNOUNCES,
    // because the strip answered false rather than the verb rejecting the
    // ordinal outright.
    engine->focusTab(4, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), second);
    QCOMPARE(placementSpy.count(), 0);
    QCOMPARE(feedbackSpy.count(), 1);
    QCOMPARE(feedbackSpy.takeFirst().at(0).toBool(), false);

    // Non-positive ordinals are out of contract and refused BEFORE the macro,
    // so unlike the case above they announce nothing at all.
    feedbackSpy.clear();
    engine->focusTab(0, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), second);
    engine->focusTab(-3, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), second);
    QCOMPARE(placementSpy.count(), 0);
    QCOMPARE(feedbackSpy.count(), 0);
}

void TestScrollEngineTabFocus::floatingATabRenumbersTheOrdinals()
{
    // The PRODUCTION way a tab leaves a column. The strip's own `minimized`
    // flag has no daemon caller: the compositor reports a minimize as a float
    // toggle, which takes the window out of the column entirely. So this, not
    // the strip-level minimize, is what decides whether a user's numbered
    // chord still matches the pills they can see.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));

    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->strip().activeColumn());
    QCOMPARE(state->strip().activeColumn()->tiles.size(), 3);

    // Name the three in stack order before anything leaves.
    engine->focusTab(1, QStringLiteral("S1"));
    const QString one = state->strip().activeWindowId();
    engine->focusTab(2, QStringLiteral("S1"));
    const QString two = state->strip().activeWindowId();
    engine->focusTab(3, QStringLiteral("S1"));
    const QString three = state->strip().activeWindowId();
    QVERIFY(one != two && two != three && one != three);

    // Float the MIDDLE one out, and float it while it is the ACTIVE tile:
    // that is the shape a user produces, and it is the only one that makes
    // floatWindowInternal set floatingHasFocus, so the clear below is
    // load-bearing rather than dead.
    engine->focusTab(2, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), two);
    engine->setWindowFloat(two, true, QStringLiteral("S1"));
    QVERIFY(state->strip().activeColumn());
    QCOMPARE(state->strip().activeColumn()->tiles.size(), 2);
    QVERIFY2(state->floatingHasFocus(), "floating the active tile must hand focus to the float layer");

    // Ordinal 2 now names what used to be the third tab, which is exactly
    // what the indicator draws second. Ordinal 3 is out of range. Focus has
    // to come back to the strip first, or the tile-focus verbs refuse.
    state->setFloatingHasFocus(false);
    engine->focusTab(1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), one);
    engine->focusTab(2, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), three);
    engine->focusTab(3, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), three);
}

void TestScrollEngineTabFocus::tabVerbsRefuseWhileFloatingHoldsFocus()
{
    // Both verbs move focus WITHIN a column, so a floating window holding
    // focus owns no column for them to act on. Acting anyway would move the
    // strip's stale active tile and drag focus off the window the user is
    // looking at.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));

    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    engine->focusWindowTop(QStringLiteral("S1"));
    const QString parked = state->strip().activeWindowId();
    QVERIFY(!parked.isEmpty());

    state->setFloatingHasFocus(true);
    QSignalSpy placementSpy(engine, &ScrollEngine::placementChanged);
    QSignalSpy feedbackSpy(engine, &ScrollEngine::navigationFeedback);

    engine->cycleTab(1, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), parked);
    engine->focusTab(2, QStringLiteral("S1"));
    QCOMPARE(state->strip().activeWindowId(), parked);

    // Both refusals are audible, and neither relayouts.
    QCOMPARE(placementSpy.count(), 0);
    QCOMPARE(feedbackSpy.count(), 2);
    for (const QList<QVariant>& args : feedbackSpy) {
        QCOMPARE(args.at(0).toBool(), false);
        QCOMPARE(args.at(2).toString(), QStringLiteral("no_target"));
    }
}

void TestScrollEngineTabFocus::tileFocusHandsAPannedViewBackToThePolicy()
{
    // The reason P_SCROLL_TILE_FOCUS_VERB exists rather than P_SCROLL_VERB.
    // The strip's tile-focus ops never re-anchor, so without the latch clear
    // a user who pans the view away with the wheel and then presses a tab
    // chord activates a window that is parked off-screen: updateViewForFocus
    // returns early while the view is detached.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    // Wide columns, so the strip overflows the output and a pan has somewhere
    // to go. Without the overflow scrollViewByPercent clamps and never
    // detaches, and the whole slot would pass vacuously.
    for (const char* id : {"app|a", "app|c", "app|d"}) {
        engine->windowOpened(QString::fromLatin1(id), QStringLiteral("S1"), 0, 0);
        engine->windowFocused(QString::fromLatin1(id), QStringLiteral("S1"));
        engine->setColumnWidth(ColumnWidth::makeProportion(0.55), QStringLiteral("S1"));
    }
    // A second tile in the FIRST column, tabbed, so the tab verbs have a run
    // to walk while the strip still overflows.
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->focusColumnFirst(QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));

    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->strip().activeColumn());
    QVERIFY2(state->strip().activeColumn()->tiles.size() >= 2, "precondition: the focused column needs a tab run");
    QVERIFY(!state->strip().viewDetached());

    // The user pans. Positive, because the focused column is the first one and
    // the anchor is already at zero. The latch is what keeps the pan from
    // being undone by the next layout pass.
    engine->scrollViewByPercent(25, QStringLiteral("S1"));
    QVERIFY2(state->strip().viewDetached(), "precondition: the pan must detach the view");

    // A REFUSED press is not a view event, so the pan survives it. Ordinal 9
    // is past the tab count of this column.
    engine->focusTab(9, QStringLiteral("S1"));
    QVERIFY2(state->strip().viewDetached(), "a refused press must leave the pan alone");

    // A SUCCESSFUL step hands the view back, so the policy can bring the
    // newly focused tab into view.
    engine->cycleTab(1, QStringLiteral("S1"));
    QVERIFY2(!state->strip().viewDetached(), "a successful tile-focus step must re-attach the view");

    // The same contract on the ordinal verb and on the shipped stack-end
    // verbs, which share the macro.
    engine->scrollViewByPercent(25, QStringLiteral("S1"));
    QVERIFY2(state->strip().viewDetached(), "pan 2 must detach");
    // Ordinal 2, not 1: the wrap above left tab 1 active, and an already-there
    // press is a refusal, which by contract leaves the pan alone.
    engine->focusTab(2, QStringLiteral("S1"));
    QVERIFY2(!state->strip().viewDetached(), "focusTab must re-attach");

    engine->scrollViewByPercent(25, QStringLiteral("S1"));
    QVERIFY2(state->strip().viewDetached(), "pan 3 must detach");
    // Top, because the ordinal above left the BOTTOM tile active and an
    // already-there press would refuse. The two shipped stack-end verbs share
    // the macro, so covering one covers the pair's wiring.
    engine->focusWindowTop(QStringLiteral("S1"));
    QVERIFY2(!state->strip().viewDetached(), "focusWindowTop must re-attach");
}

// GUILESS (not APPLESS): a QCoreApplication provides the event dispatcher the
// engine's coalesced work and deleteLater need.
QTEST_GUILESS_MAIN(TestScrollEngineTabFocus)
#include "test_scrollengine_tabfocus.moc"
