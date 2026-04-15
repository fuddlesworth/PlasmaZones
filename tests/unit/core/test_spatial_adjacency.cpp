// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QList>
#include <QRectF>

#include "core/spatialadjacency.h"
#include "core/utils.h"

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
    QCOMPARE(SpatialAdjacency::findAdjacentRect(current, {}, Utils::Direction::Right), -1);
}

void TestSpatialAdjacency::twoSplitHorizontal()
{
    const QRectF left(0, 0, 0.5, 1.0);
    const QRectF right(0.5, 0, 0.5, 1.0);
    const QList<QRectF> rects{left, right};

    QCOMPARE(SpatialAdjacency::findAdjacentRect(left, rects, Utils::Direction::Right), 1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(right, rects, Utils::Direction::Left), 0);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(left, rects, Utils::Direction::Left), -1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(right, rects, Utils::Direction::Right), -1);
}

void TestSpatialAdjacency::twoSplitVertical()
{
    const QRectF top(0, 0, 1.0, 0.5);
    const QRectF bottom(0, 0.5, 1.0, 0.5);
    const QList<QRectF> rects{top, bottom};

    QCOMPARE(SpatialAdjacency::findAdjacentRect(top, rects, Utils::Direction::Down), 1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(bottom, rects, Utils::Direction::Up), 0);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(top, rects, Utils::Direction::Up), -1);
}

void TestSpatialAdjacency::gridPrefersSameRow()
{
    // 2x2 grid: from top-left, going right should land on top-right,
    // not bottom-right (same-row wins via 2x perpendicular weighting).
    const QRectF topLeft(0, 0, 0.5, 0.5);
    const QRectF topRight(0.5, 0, 0.5, 0.5);
    const QRectF bottomLeft(0, 0.5, 0.5, 0.5);
    const QRectF bottomRight(0.5, 0.5, 0.5, 0.5);
    const QList<QRectF> rects{topLeft, topRight, bottomLeft, bottomRight};

    QCOMPARE(SpatialAdjacency::findAdjacentRect(topLeft, rects, Utils::Direction::Right), 1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(topRight, rects, Utils::Direction::Left), 0);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(bottomLeft, rects, Utils::Direction::Right), 3);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(bottomRight, rects, Utils::Direction::Left), 2);
}

void TestSpatialAdjacency::gridPrefersSameColumn()
{
    const QRectF topLeft(0, 0, 0.5, 0.5);
    const QRectF topRight(0.5, 0, 0.5, 0.5);
    const QRectF bottomLeft(0, 0.5, 0.5, 0.5);
    const QRectF bottomRight(0.5, 0.5, 0.5, 0.5);
    const QList<QRectF> rects{topLeft, topRight, bottomLeft, bottomRight};

    QCOMPARE(SpatialAdjacency::findAdjacentRect(topLeft, rects, Utils::Direction::Down), 2);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(topRight, rects, Utils::Direction::Down), 3);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(bottomLeft, rects, Utils::Direction::Up), 0);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(bottomRight, rects, Utils::Direction::Up), 1);
}

void TestSpatialAdjacency::skipsCurrentByCentre()
{
    // The helper skips any candidate whose centre equals `current`, so
    // passing the current rect itself in the list is a no-op.
    const QRectF left(0, 0, 0.5, 1.0);
    const QRectF right(0.5, 0, 0.5, 1.0);
    const QList<QRectF> rects{left, right};

    // `current` equals `left` — from left, the only valid neighbour is right.
    QCOMPARE(SpatialAdjacency::findAdjacentRect(left, rects, Utils::Direction::Right), 1);
}

void TestSpatialAdjacency::noMatchInDirection()
{
    const QRectF only(0, 0, 1.0, 1.0);
    const QList<QRectF> rects{only};
    QCOMPARE(SpatialAdjacency::findAdjacentRect(only, rects, Utils::Direction::Left), -1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(only, rects, Utils::Direction::Right), -1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(only, rects, Utils::Direction::Up), -1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(only, rects, Utils::Direction::Down), -1);
}

void TestSpatialAdjacency::threeColumnChain()
{
    // Three equal columns — from the middle, left/right pick the adjacent
    // ones, not jump past to the far column.
    const QRectF col0(0.0, 0, 1.0 / 3.0, 1.0);
    const QRectF col1(1.0 / 3.0, 0, 1.0 / 3.0, 1.0);
    const QRectF col2(2.0 / 3.0, 0, 1.0 / 3.0, 1.0);
    const QList<QRectF> rects{col0, col1, col2};

    QCOMPARE(SpatialAdjacency::findAdjacentRect(col1, rects, Utils::Direction::Left), 0);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(col1, rects, Utils::Direction::Right), 2);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(col0, rects, Utils::Direction::Right), 1);
    QCOMPARE(SpatialAdjacency::findAdjacentRect(col2, rects, Utils::Direction::Left), 1);
}

void TestSpatialAdjacency::asymmetricGridFirstSeenWins()
{
    // Asymmetric layout: a tall left column and two stacked right cells.
    // From Left, going Right, both right cells are equidistant on the
    // weighted distance metric (same dx, perpendicular offsets symmetric
    // around the left's center). The helper does not promise a specific
    // winner — it walks the candidate list in order and keeps strictly-
    // smaller results, so first-seen wins on ties. Pin that behavior so
    // the implementation can't drift to a non-deterministic sort.
    const QRectF left(0.0, 0.0, 0.5, 1.0); // center (0.25, 0.5)
    const QRectF topRight(0.5, 0.0, 0.5, 0.5); // center (0.75, 0.25)
    const QRectF bottomRight(0.5, 0.5, 0.5, 0.5); // center (0.75, 0.75)
    const QList<QRectF> rects{left, topRight, bottomRight};

    // dy from left to topRight = |0.25 - 0.5| = 0.25, weighted 2x = 0.5
    // dy from left to bottomRight = |0.75 - 0.5| = 0.25, weighted 2x = 0.5
    // Both have dx = 0.5, so weighted distance is equal — first-seen (topRight, idx 1) wins.
    QCOMPARE(SpatialAdjacency::findAdjacentRect(left, rects, Utils::Direction::Right), 1);

    // Reverse order in the input — now bottomRight is seen first.
    const QList<QRectF> reversed{left, bottomRight, topRight};
    QCOMPARE(SpatialAdjacency::findAdjacentRect(left, reversed, Utils::Direction::Right), 1);
}

QTEST_MAIN(TestSpatialAdjacency)
#include "test_spatial_adjacency.moc"
