// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorGeometry/DirectionalNeighbor.h>

#include <QList>
#include <QRectF>
#include <QTest>

using PhosphorGeometry::Direction;
using PhosphorGeometry::directionalNeighbor;
using PhosphorGeometry::directionFromString;

class TestDirectionalNeighbor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void directionFromString_parsesCardinals();
    void directionFromString_rejectsUnknown();

    void grid2x2_picksOrthogonalNeighbour();
    void grid2x2_noCandidateInDirection_returnsMinusOne();

    void binarySplit_rightFromTopLeft_picksTopRightNotBelow();
    void overlapPreference_beatsNearerDiagonal();
    void overlappingTier_nearestGapWins();
    void tie_isDeterministicByOrder();
    void requireOverlap_diagonalOnly_returnsMinusOne();
    void requireOverlap_nearestGapAmongOverlapping_wins();
    void requireOverlap_tie_isDeterministicByOrder();
    void emptyCandidates_returnsMinusOne();

    void desktopGrid_2x2_steps();
    void desktopGrid_singleRow_horizontalOnly();
    void desktopGrid_partialLastRow_missingCellIsNoNeighbour();
    void desktopGrid_outOfRange_returnsZero();
};

void TestDirectionalNeighbor::directionFromString_parsesCardinals()
{
    QCOMPARE(directionFromString(QStringLiteral("left")).value(), Direction::Left);
    QCOMPARE(directionFromString(QStringLiteral("right")).value(), Direction::Right);
    QCOMPARE(directionFromString(QStringLiteral("up")).value(), Direction::Up);
    QCOMPARE(directionFromString(QStringLiteral("down")).value(), Direction::Down);
}

void TestDirectionalNeighbor::directionFromString_rejectsUnknown()
{
    QVERIFY(!directionFromString(QStringLiteral("diagonal")).has_value());
    QVERIFY(!directionFromString(QStringLiteral("Right")).has_value()); // case-sensitive
    QVERIFY(!directionFromString(QStringLiteral("")).has_value());
}

void TestDirectionalNeighbor::grid2x2_picksOrthogonalNeighbour()
{
    // 1920x1080 split into four quadrants. Index order: TL, TR, BL, BR.
    const QList<QRectF> grid{
        QRectF(0, 0, 960, 540), // 0 TL
        QRectF(960, 0, 960, 540), // 1 TR
        QRectF(0, 540, 960, 540), // 2 BL
        QRectF(960, 540, 960, 540) // 3 BR
    };

    // From TL.
    QCOMPARE(directionalNeighbor(grid[0], grid, Direction::Right), 1); // TR
    QCOMPARE(directionalNeighbor(grid[0], grid, Direction::Down), 2); // BL
    // From BR.
    QCOMPARE(directionalNeighbor(grid[3], grid, Direction::Left), 2); // BL
    QCOMPARE(directionalNeighbor(grid[3], grid, Direction::Up), 1); // TR
}

void TestDirectionalNeighbor::grid2x2_noCandidateInDirection_returnsMinusOne()
{
    const QList<QRectF> grid{QRectF(0, 0, 960, 540), QRectF(960, 0, 960, 540), QRectF(0, 540, 960, 540),
                             QRectF(960, 540, 960, 540)};
    // Nothing left of / above the top-left quadrant.
    QCOMPARE(directionalNeighbor(grid[0], grid, Direction::Left), -1);
    QCOMPARE(directionalNeighbor(grid[0], grid, Direction::Up), -1);
}

void TestDirectionalNeighbor::binarySplit_rightFromTopLeft_picksTopRightNotBelow()
{
    // The reported bug: focus a window and press "right"; it must land on the
    // window to the RIGHT, never the one below. Layout: top-left, with the
    // right column split into top-right and bottom-right.
    const QRectF topLeft(0, 0, 960, 540);
    const QRectF topRight(960, 0, 960, 540);
    const QRectF bottomRight(960, 540, 960, 540);

    // Candidate order deliberately puts the BELOW-ish window first to prove the
    // result is geometric, not index-based.
    const QList<QRectF> candidates{bottomRight, topRight};
    QCOMPARE(directionalNeighbor(topLeft, candidates, Direction::Right), 1); // topRight
}

void TestDirectionalNeighbor::overlapPreference_beatsNearerDiagonal()
{
    // A genuinely side-by-side (perpendicular-overlapping) candidate must win
    // over a diagonal candidate that is nearer by centre distance. This is the
    // behaviour a pure centre-distance heuristic gets wrong.
    const QRectF focus(0, 0, 100, 100); // centre (50,50)
    const QRectF overlappingFar(1000, 0, 100, 100); // side-by-side, far gap
    const QRectF diagonalNear(110, 150, 100, 100); // closer centre, no y-overlap

    const QList<QRectF> candidates{overlappingFar, diagonalNear};
    QCOMPARE(directionalNeighbor(focus, candidates, Direction::Right), 0); // overlappingFar
}

void TestDirectionalNeighbor::overlappingTier_nearestGapWins()
{
    const QRectF focus(0, 0, 100, 100);
    const QRectF near(110, 0, 100, 100); // gap 10
    const QRectF far(400, 0, 100, 100); // gap 300
    const QList<QRectF> candidates{far, near};
    QCOMPARE(directionalNeighbor(focus, candidates, Direction::Right), 1); // near
}

void TestDirectionalNeighbor::tie_isDeterministicByOrder()
{
    // Two candidates with identical geometry-derived ranking: the first in the
    // list wins, so the result never depends on iteration accidents.
    const QRectF focus(0, 0, 100, 1000); // tall; both candidates overlap equally
    const QRectF upperRight(110, 0, 100, 100);
    const QRectF lowerRight(110, 900, 100, 100);
    // Both: gap 10, full membership in the in-direction half-plane, equal
    // perpendicular centre distance (450) from focus centre y=500.
    const QList<QRectF> a{upperRight, lowerRight};
    const QList<QRectF> b{lowerRight, upperRight};
    QCOMPARE(directionalNeighbor(focus, a, Direction::Right), 0);
    QCOMPARE(directionalNeighbor(focus, b, Direction::Right), 0);
}

void TestDirectionalNeighbor::requireOverlap_diagonalOnly_returnsMinusOne()
{
    // Real-hardware tatami/pinwheel bug: pressing "right" from the bottom-right
    // tile swapped it UP with the top-right tile. The bottom-right tile is wider,
    // so its centre sits LEFT of the top-right tile's centre — putting top-right
    // in the "right" half-plane even though the two share NO vertical overlap.
    // Geometry mirrors the logged layout (115107, 4-window tatami).
    const QRectF bottomRight(1445, 894, 1747, 846); // focus, centre x≈2318
    const QRectF topRight(1762, 40, 1430, 846); // diagonal up-right, centre x≈2477
    const QRectF topLeft(8, 40, 1746, 846);
    const QRectF bottomLeft(8, 894, 1429, 846);
    const QList<QRectF> candidates{topLeft, topRight, bottomLeft};

    // Default (fallback) behaviour still resolves the diagonal (index 1).
    QCOMPARE(directionalNeighbor(bottomRight, candidates, Direction::Right), 1);
    // Window navigation rejects the diagonal → boundary (cross to next output).
    QCOMPARE(directionalNeighbor(bottomRight, candidates, Direction::Right, /*requireOverlap=*/true), -1);
    // ...but a genuine perpendicular-overlapping neighbour still resolves:
    // "left" from bottom-right finds bottom-left (shared y-band), overlap or not.
    QCOMPARE(directionalNeighbor(bottomRight, candidates, Direction::Left, /*requireOverlap=*/true), 2);
}

void TestDirectionalNeighbor::requireOverlap_nearestGapAmongOverlapping_wins()
{
    // The production window-nav caller always passes requireOverlap=true, so the
    // "two perpendicular-overlapping candidates at different gaps → nearest wins"
    // path is the hot path. Verify it ranks by gap under the flag, not just in
    // the default mode.
    const QRectF focus(0, 0, 100, 100);
    const QRectF near(110, 0, 100, 100); // gap 10, full y-overlap
    const QRectF far(400, 0, 100, 100); // gap 300, full y-overlap
    const QList<QRectF> candidates{far, near};
    QCOMPARE(directionalNeighbor(focus, candidates, Direction::Right, /*requireOverlap=*/true), 1); // near
}

void TestDirectionalNeighbor::requireOverlap_tie_isDeterministicByOrder()
{
    // Determinism under the production flag: two equally-ranked overlapping
    // candidates → first in the list wins, independent of iteration order.
    const QRectF focus(0, 0, 100, 1000);
    const QRectF upperRight(110, 0, 100, 100); // gap 10, perp distance 450
    const QRectF lowerRight(110, 900, 100, 100); // gap 10, perp distance 450
    const QList<QRectF> a{upperRight, lowerRight};
    const QList<QRectF> b{lowerRight, upperRight};
    QCOMPARE(directionalNeighbor(focus, a, Direction::Right, /*requireOverlap=*/true), 0);
    QCOMPARE(directionalNeighbor(focus, b, Direction::Right, /*requireOverlap=*/true), 0);
}

void TestDirectionalNeighbor::emptyCandidates_returnsMinusOne()
{
    QCOMPARE(directionalNeighbor(QRectF(0, 0, 10, 10), {}, Direction::Right), -1);
}

void TestDirectionalNeighbor::desktopGrid_2x2_steps()
{
    using PhosphorGeometry::neighborDesktopInDirection;
    // 4 desktops, 2 rows -> 2 columns:  [1 2] / [3 4]
    QCOMPARE(neighborDesktopInDirection(1, 4, 2, Direction::Right), 2);
    QCOMPARE(neighborDesktopInDirection(1, 4, 2, Direction::Down), 3);
    QCOMPARE(neighborDesktopInDirection(1, 4, 2, Direction::Left), 0); // edge
    QCOMPARE(neighborDesktopInDirection(1, 4, 2, Direction::Up), 0); // edge
    QCOMPARE(neighborDesktopInDirection(4, 4, 2, Direction::Left), 3);
    QCOMPARE(neighborDesktopInDirection(4, 4, 2, Direction::Up), 2);
    QCOMPARE(neighborDesktopInDirection(2, 4, 2, Direction::Right), 0); // column edge, no row wrap
}

void TestDirectionalNeighbor::desktopGrid_singleRow_horizontalOnly()
{
    using PhosphorGeometry::neighborDesktopInDirection;
    // 4 desktops, 1 row: [1 2 3 4]
    QCOMPARE(neighborDesktopInDirection(2, 4, 1, Direction::Right), 3);
    QCOMPARE(neighborDesktopInDirection(2, 4, 1, Direction::Left), 1);
    QCOMPARE(neighborDesktopInDirection(2, 4, 1, Direction::Up), 0);
    QCOMPARE(neighborDesktopInDirection(2, 4, 1, Direction::Down), 0);
    QCOMPARE(neighborDesktopInDirection(4, 4, 1, Direction::Right), 0); // edge
}

void TestDirectionalNeighbor::desktopGrid_partialLastRow_missingCellIsNoNeighbour()
{
    using PhosphorGeometry::neighborDesktopInDirection;
    // 3 desktops, 2 rows -> 2 columns: [1 2] / [3 _]
    QCOMPARE(neighborDesktopInDirection(1, 3, 2, Direction::Down), 3);
    QCOMPARE(neighborDesktopInDirection(2, 3, 2, Direction::Down), 0); // cell (1,1) is empty
    QCOMPARE(neighborDesktopInDirection(3, 3, 2, Direction::Right), 0); // would be the empty cell
}

void TestDirectionalNeighbor::desktopGrid_outOfRange_returnsZero()
{
    using PhosphorGeometry::neighborDesktopInDirection;
    QCOMPARE(neighborDesktopInDirection(0, 4, 2, Direction::Right), 0);
    QCOMPARE(neighborDesktopInDirection(5, 4, 2, Direction::Right), 0);
    QCOMPARE(neighborDesktopInDirection(1, 0, 1, Direction::Right), 0);
    QCOMPARE(neighborDesktopInDirection(1, 1, 1, Direction::Right), 0); // only desktop
}

QTEST_GUILESS_MAIN(TestDirectionalNeighbor)
#include "test_directional_neighbor.moc"
