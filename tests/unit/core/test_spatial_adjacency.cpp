// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QList>
#include <QRectF>

#include "core/types/spatialadjacency.h"
#include "core/utils/utils.h"

#include <PhosphorScreens/Swapper.h>

using namespace PlasmaZones;

class TestSpatialAdjacency : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyCandidates();
    void twoSplitHorizontal();
    void twoSplitVertical();
    void gridPrefersSameRow();
    void gridPrefersSameColumn();
    void skipsCurrentByCentre();
    void noMatchInDirection();
    void threeColumnChain();
    void asymmetricGridFirstSeenWins();
};

void TestSpatialAdjacency::emptyCandidates()
{
    const QRectF current(0, 0, 100, 100);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(current, {}, PhosphorScreens::Direction::Right), -1);
}

void TestSpatialAdjacency::twoSplitHorizontal()
{
    const QRectF left(0, 0, 0.5, 1.0);
    const QRectF right(0.5, 0, 0.5, 1.0);
    const QList<QRectF> rects{left, right};

    QCOMPARE(SpatialAdjacency::findAdjacentRect(left, rects, PhosphorScreens::Direction::Right), 1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(right, rects, PhosphorScreens::Direction::Left), 0);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(left, rects, PhosphorScreens::Direction::Left), -1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(right, rects, PhosphorScreens::Direction::Right), -1);
}

void TestSpatialAdjacency::twoSplitVertical()
{
    const QRectF top(0, 0, 1.0, 0.5);
    const QRectF bottom(0, 0.5, 1.0, 0.5);
    const QList<QRectF> rects{top, bottom};

    QCOMPARE(SpatialAdjacency::findAdjacentRect(top, rects, PhosphorScreens::Direction::Down), 1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(bottom, rects, PhosphorScreens::Direction::Up), 0);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(top, rects, PhosphorScreens::Direction::Up), -1);
}

void TestSpatialAdjacency::gridPrefersSameRow()
{
    // 2x2 grid: from top-left, going right should land on top-right, not
    // bottom-right. top-right overlaps the source's vertical span so it wins by
    // the overlap preference; the diagonal bottom-right does not overlap.
    const QRectF topLeft(0, 0, 0.5, 0.5);
    const QRectF topRight(0.5, 0, 0.5, 0.5);
    const QRectF bottomLeft(0, 0.5, 0.5, 0.5);
    const QRectF bottomRight(0.5, 0.5, 0.5, 0.5);
    const QList<QRectF> rects{topLeft, topRight, bottomLeft, bottomRight};

    QCOMPARE(SpatialAdjacency::findAdjacentRect(topLeft, rects, PhosphorScreens::Direction::Right), 1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(topRight, rects, PhosphorScreens::Direction::Left), 0);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(bottomLeft, rects, PhosphorScreens::Direction::Right), 3);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(bottomRight, rects, PhosphorScreens::Direction::Left), 2);
}

void TestSpatialAdjacency::gridPrefersSameColumn()
{
    const QRectF topLeft(0, 0, 0.5, 0.5);
    const QRectF topRight(0.5, 0, 0.5, 0.5);
    const QRectF bottomLeft(0, 0.5, 0.5, 0.5);
    const QRectF bottomRight(0.5, 0.5, 0.5, 0.5);
    const QList<QRectF> rects{topLeft, topRight, bottomLeft, bottomRight};

    QCOMPARE(SpatialAdjacency::findAdjacentRect(topLeft, rects, PhosphorScreens::Direction::Down), 2);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(topRight, rects, PhosphorScreens::Direction::Down), 3);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(bottomLeft, rects, PhosphorScreens::Direction::Up), 0);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(bottomRight, rects, PhosphorScreens::Direction::Up), 1);
}

void TestSpatialAdjacency::skipsCurrentByCentre()
{
    // Any candidate sharing `current`'s centre — the current rect itself OR a
    // duplicate stacked exactly on it — must never be selected. The explicit
    // centre-equality skip excludes it (and the in-direction filter would too,
    // its centre not being past `current`'s, were that skip ever removed). Put a
    // co-centred duplicate BETWEEN `current` and the genuine neighbour so a
    // regression that admitted equal-centre candidates would wrongly return the
    // duplicate's index.
    const QRectF left(0, 0, 0.5, 1.0);
    const QRectF leftDuplicate(0, 0, 0.5, 1.0); // identical centre to `left`
    const QRectF right(0.5, 0, 0.5, 1.0);
    const QList<QRectF> rects{left, leftDuplicate, right};

    // `current` equals `left`; both co-centred entries (index 0 and 1) are
    // skipped, so the only valid right-hand neighbour is `right` at index 2.
    QCOMPARE(SpatialAdjacency::findAdjacentRect(left, rects, PhosphorScreens::Direction::Right), 2);
}

void TestSpatialAdjacency::noMatchInDirection()
{
    const QRectF only(0, 0, 1.0, 1.0);
    const QList<QRectF> rects{only};
    QCOMPARE(SpatialAdjacency::findAdjacentRect(only, rects, PhosphorScreens::Direction::Left), -1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(only, rects, PhosphorScreens::Direction::Right), -1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(only, rects, PhosphorScreens::Direction::Up), -1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(only, rects, PhosphorScreens::Direction::Down), -1);
}

void TestSpatialAdjacency::threeColumnChain()
{
    // Three equal columns — from the middle, left/right pick the adjacent
    // ones, not jump past to the far column.
    const QRectF col0(0.0, 0, 1.0 / 3.0, 1.0);
    const QRectF col1(1.0 / 3.0, 0, 1.0 / 3.0, 1.0);
    const QRectF col2(2.0 / 3.0, 0, 1.0 / 3.0, 1.0);
    const QList<QRectF> rects{col0, col1, col2};

    QCOMPARE(SpatialAdjacency::findAdjacentRect(col1, rects, PhosphorScreens::Direction::Left), 0);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(col1, rects, PhosphorScreens::Direction::Right), 2);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(col0, rects, PhosphorScreens::Direction::Right), 1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(col2, rects, PhosphorScreens::Direction::Left), 1);
}

void TestSpatialAdjacency::asymmetricGridFirstSeenWins()
{
    // Asymmetric layout: a tall left column and two stacked right cells.
    // From Left, going Right, both right cells are a genuine tie: equal edge
    // gap (same dx) and equal perpendicular centre distance (their centres sit
    // symmetrically above/below the left column's centre). The helper resolves
    // exact ties to the lowest candidate index (first-seen), keeping only
    // strictly-smaller results. Pin that behavior so the implementation can't
    // drift to a non-deterministic sort.
    const QRectF left(0.0, 0.0, 0.5, 1.0); // center (0.25, 0.5)
    const QRectF topRight(0.5, 0.0, 0.5, 0.5); // center (0.75, 0.25)
    const QRectF bottomRight(0.5, 0.5, 0.5, 0.5); // center (0.75, 0.75)
    const QList<QRectF> rects{left, topRight, bottomRight};

    // perp centre distance left→topRight    = |0.25 - 0.5| = 0.25
    // perp centre distance left→bottomRight = |0.75 - 0.5| = 0.25
    // Equal gap (dx = 0.5) and equal perp distance — a true tie, so first-seen (topRight, idx 1) wins.
    QCOMPARE(SpatialAdjacency::findAdjacentRect(left, rects, PhosphorScreens::Direction::Right), 1);

    // Reverse order in the input — now bottomRight is seen first.
    const QList<QRectF> reversed{left, bottomRight, topRight};
    QCOMPARE(SpatialAdjacency::findAdjacentRect(left, reversed, PhosphorScreens::Direction::Right), 1);
}

QTEST_MAIN(TestSpatialAdjacency)
#include "test_spatial_adjacency.moc"
