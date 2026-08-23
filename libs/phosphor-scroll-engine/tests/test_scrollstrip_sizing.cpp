// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The repeatable SIZE verbs' refusal contract: where adjustActiveColumnWidth
// and adjustActiveWindowHeight stop, and which states they decline outright.
// Placed in its own file rather than grown into test_scrollstrip_ops, which
// owns the per-operation surface over one shared strip fixture and already
// carries a file-size exception; every slot here builds its own strip against
// the shared screen constants.
//
// These verbs are held down on a key repeat, so a bool that says "changed"
// while nothing moved is the failure mode worth pinning: it costs a relayout
// and a success OSD per press, forever. Each slot therefore asserts the
// resulting GEOMETRY as well as the verdict, because a verdict-only
// assertion passes for a refusal that already mutated the model.

#include <PhosphorScrollEngine/ScrollStrip.h>

#include "scrollstriptestutils.h"

#include <QtTest>

using namespace PhosphorScrollEngine;

namespace {

using ScrollTestUtils::defaultParams;
using ScrollTestUtils::kHalf;
using ScrollTestUtils::rectOf;

namespace Ax = ScrollTestUtils::Ax;

} // namespace

class TestScrollStripSizing : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }
    // Declaration order IS definition order, and the run order: the list
    // doubles as the file's table of contents (the ops suite's rule).
    void heightAdjustFloorsAtTheClientMinimum();
    void heightAdjustRefusesOnATabbedColumn();
    void heightGrowLeavesTheColumnTilingItsBudget();
};

// The client half of the height floor, which the engine-minimum slots in the
// ops suite cannot reach: they run with minimum sizes off, where
// columnMinExtentPx answers 0 and tile->minCross drops out of the qMax. Here
// the client minimum is the LARGER of the two floors, so it is the one doing
// the work, and a shrink must stop on it rather than on the fraction.
void TestScrollStripSizing::heightAdjustFloorsAtTheClientMinimum()
{
    ScrollLayoutParams params = defaultParams();
    QVERIFY(params.respectMinimumSize); // the arm under test

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // Well clear of the engine's own floor (a twentieth of the cross extent),
    // so a shrink that stopped on the fraction instead would land far below
    // this and the QCOMPARE would catch it.
    const int clientMin = Ax::crossLen(ScrollTestUtils::defaultScreenRect()) / 4;
    QVERIFY(strip.setWindowMinimumSize(QStringLiteral("b"), clientMin, clientMin));
    // "b" is already the active tile: insertWindowIntoActiveColumn focuses
    // what it inserts, and focusWindow answers false for a window that is
    // already focused.
    QCOMPARE(strip.activeColumn()->tiles.at(strip.activeColumn()->activeTileIdx).windowId, QStringLiteral("b"));

    bool everRefused = false;
    for (int i = 0; i < 20; ++i) {
        if (!strip.adjustActiveWindowHeight(-25.0, params)) {
            everRefused = true;
            break;
        }
    }
    QVERIFY2(everRefused, "a repeated shrink must converge onto the floor and then decline");
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), clientMin);
    // Once seated on the floor the verb stays declined rather than alternating.
    QVERIFY(!strip.adjustActiveWindowHeight(-25.0, params));
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), clientMin);
    // Growing away from the floor still works, so the refusal is the floor
    // and not a wedged verb.
    QVERIFY(strip.adjustActiveWindowHeight(10.0, params));
    QVERIFY(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))) > clientMin);
}

// A tabbed column lays every visible tile out at the column's whole content
// rect and never reads Tile::height, so the measured extent is identical on
// every press. Without the refusal the verb wrote a Fixed intent nothing
// consumed and reported success forever.
void TestScrollStripSizing::heightAdjustRefusesOnATabbedColumn()
{
    ScrollLayoutParams params = defaultParams();

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.toggleActiveColumnTabbed());

    const int before = Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b")));
    QVERIFY(!strip.adjustActiveWindowHeight(10.0, params));
    QVERIFY(!strip.adjustActiveWindowHeight(-10.0, params));
    // Geometry, not just the verdict: a refusal that had already written the
    // intent would still return false on the way out.
    QCOMPARE(Ax::crossLen(rectOf(strip.relayout(params), QStringLiteral("b"))), before);
    // And no intent was buried for a later untab to spring.
    const Column* col = strip.activeColumn();
    QVERIFY(col);
    QCOMPARE(col->tiles.at(col->activeTileIdx).height.kind, WindowHeight::Auto);

    // The refusal is the tabbed state, not the fixture: untabbed, the same
    // press on the same strip is accepted.
    QVERIFY(strip.toggleActiveColumnTabbed());
    QVERIFY(strip.adjustActiveWindowHeight(10.0, params));
}

// The grow side's budget invariant. Deliberately NOT an exact ceiling
// pixel: what the verb's ceiling OUGHT to be for a multi-tile column is an
// open question (it caps at the work area's cross extent, while relayout can
// only ever hand out that extent less the gaps and the siblings' floors).
// What must hold under any answer is that the column still tiles its budget
// and no sibling is pushed off, so that is what this pins.
void TestScrollStripSizing::heightGrowLeavesTheColumnTilingItsBudget()
{
    ScrollLayoutParams params = defaultParams();
    const int crossExtent = Ax::crossLen(ScrollTestUtils::defaultScreenRect());

    ScrollStrip strip;
    QVERIFY(strip.insertWindow(QStringLiteral("a"), kHalf, ColumnDisplay::Normal, params));
    QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), kHalf, ColumnDisplay::Normal, params));
    // "b" is already active: the insert focuses what it adds.

    // Far past any plausible ceiling in one press.
    QVERIFY(strip.adjustActiveWindowHeight(500.0, params));

    const ResolvedStrip resolved = strip.relayout(params);
    const int grown = Ax::crossLen(rectOf(resolved, QStringLiteral("b")));
    const int sibling = Ax::crossLen(rectOf(resolved, QStringLiteral("a")));
    // The sibling keeps a positive extent: it was not pushed off the strip.
    QVERIFY2(sibling > 0, "growing one tile must not evict its sibling");
    // And the stack still exactly tiles the cross extent, gap included.
    QCOMPARE(grown + sibling + params.gap, crossExtent);
}

QTEST_APPLESS_MAIN(TestScrollStripSizing)
#include "test_scrollstrip_sizing.moc"
