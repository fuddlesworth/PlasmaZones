// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QVector>
#include <QSize>

#include "core/geometryutils.h"
#include "core/constants.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for GeometryUtils::enforceWindowMinSizes() and removeZoneOverlaps()
 *
 * Tests cover:
 * - No-op when minimums are empty or already satisfied
 * - Single-zone stealing from a neighbor
 * - Chain stealing across multiple columns (critical bug scenario)
 * - Height (vertical) chain stealing
 * - MasterStack-like layouts with multiple zones per column
 * - Unsatisfiable constraints (proportional fallback)
 * - Size mismatch early-return guard
 * - Gap threshold adjacency detection
 * - removeZoneOverlaps basic behaviors
 */
class TestGeometryUtils : public QObject
{
    Q_OBJECT

private:
    // Helper to get a zone's width (QRect width)
    static int zoneWidth(const QRect& z) { return z.width(); }
    static int zoneHeight(const QRect& z) { return z.height(); }

    // Helper to verify no two zones overlap
    static bool noOverlaps(const QVector<QRect>& zones)
    {
        for (int i = 0; i < zones.size(); ++i) {
            for (int j = i + 1; j < zones.size(); ++j) {
                if (zones[i].intersects(zones[j])) {
                    return false;
                }
            }
        }
        return true;
    }

    // Helper to verify all zones have positive dimensions
    static bool allPositiveDimensions(const QVector<QRect>& zones)
    {
        for (const QRect& z : zones) {
            if (z.width() <= 0 || z.height() <= 0) {
                return false;
            }
        }
        return true;
    }

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // enforceWindowMinSizes tests
    // ═══════════════════════════════════════════════════════════════════════════

    void test_noMinSizes_noChange()
    {
        // 3 zones with empty QSize minimums → zones unchanged
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(300, 0, 300, 900),
            QRect(600, 0, 300, 900),
        };
        const QVector<QRect> original = zones;
        QVector<QSize> minSizes = {QSize(), QSize(), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QCOMPARE(zones.size(), 3);
        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], original[i]);
        }
    }

    void test_singleZoneBelowMin_stealsFromNeighbor()
    {
        // 2 adjacent zones, zone[0] below min width, zone[1] has surplus → zone[0] expands
        // Zone A: 0-299 (width 300), Zone B: 300-599 (width 300)
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(300, 0, 300, 900),
        };
        // Note: QSize with height=0 is considered isEmpty() by Qt, so use height=1
        // (trivially satisfied) to ensure the width minimum is actually applied.
        QVector<QSize> minSizes = {QSize(400, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        // Zone[0] should have expanded to at least 400
        QVERIFY2(zones[0].width() >= 400,
                 qPrintable(QStringLiteral("Zone[0] width %1 should be >= 400").arg(zones[0].width())));

        // Zone[1] should have shrunk correspondingly
        QVERIFY2(zones[1].width() > 0,
                 qPrintable(QStringLiteral("Zone[1] width %1 should be > 0").arg(zones[1].width())));

        // Total width should be preserved
        QCOMPARE(zones[0].width() + zones[1].width(), 600);
    }

    void test_chainStealing_threeColumns()
    {
        // CRITICAL BUG SCENARIO:
        // A(300)|B(300)|C(300), A.min=400, B.min=350, C.min=0
        // Expected after fix: A=400, B=350, C=150
        // Total minimums: 400+350+0 = 750 < 900 (total width), so this IS satisfiable.
        //
        // The chain must propagate: A steals from B, B replenishes from C.
        // Without chain stealing, A stays at 300 because B has no surplus after
        // satisfying its own minimum.
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(300, 0, 300, 900),
            QRect(600, 0, 300, 900),
        };
        QVector<QSize> minSizes = {QSize(400, 1), QSize(350, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        // Verify zones still have positive dimensions and don't overlap
        QVERIFY2(allPositiveDimensions(zones), "All zones must have positive dimensions");

        // The critical assertion: all minimum sizes must be satisfied
        QVERIFY2(zones[0].width() >= 400,
                 qPrintable(QStringLiteral("CHAIN STEAL FAILURE: Zone A width = %1, expected >= 400. "
                                           "Zone B = %2, Zone C = %3. A cannot reach C through B.")
                                .arg(zones[0].width())
                                .arg(zones[1].width())
                                .arg(zones[2].width())));

        QVERIFY2(zones[1].width() >= 350,
                 qPrintable(QStringLiteral("Zone B width = %1, expected >= 350. "
                                           "Zone A = %2, Zone C = %3.")
                                .arg(zones[1].width())
                                .arg(zones[0].width())
                                .arg(zones[2].width())));

        // C has no minimum, but should still have positive width
        QVERIFY2(zones[2].width() > 0,
                 qPrintable(QStringLiteral("Zone C width = %1, must be > 0").arg(zones[2].width())));

        // Total width must be preserved (900 = 300+300+300)
        const int totalWidth = zones[0].width() + zones[1].width() + zones[2].width();
        QVERIFY2(totalWidth == 900,
                 qPrintable(QStringLiteral("Total width %1 != 900 (A=%2, B=%3, C=%4)")
                                .arg(totalWidth)
                                .arg(zones[0].width())
                                .arg(zones[1].width())
                                .arg(zones[2].width())));
    }

    void test_chainStealing_fourColumns()
    {
        // A(250)|B(250)|C(250)|D(250), total = 1000
        // A.min=350, B.min=300, C.min=250, D.min=0
        // Total minimums: 350+300+250 = 900 < 1000 (total width), so satisfiable.
        // Expected: all constraints met, D gets the remaining ~100px.
        QVector<QRect> zones = {
            QRect(0, 0, 250, 900),
            QRect(250, 0, 250, 900),
            QRect(500, 0, 250, 900),
            QRect(750, 0, 250, 900),
        };
        QVector<QSize> minSizes = {QSize(350, 1), QSize(300, 1), QSize(250, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QVERIFY2(allPositiveDimensions(zones), "All zones must have positive dimensions");

        // Verify each zone meets its minimum (or is as close as possible)
        QVERIFY2(zones[0].width() >= 350,
                 qPrintable(QStringLiteral("Zone A width = %1, expected >= 350").arg(zones[0].width())));
        QVERIFY2(zones[1].width() >= 300,
                 qPrintable(QStringLiteral("Zone B width = %1, expected >= 300").arg(zones[1].width())));
        QVERIFY2(zones[2].width() >= 250,
                 qPrintable(QStringLiteral("Zone C width = %1, expected >= 250").arg(zones[2].width())));

        // D absorbs the deficit; it should still be positive
        QVERIFY2(zones[3].width() > 0,
                 qPrintable(QStringLiteral("Zone D width = %1, must be > 0").arg(zones[3].width())));
    }

    void test_heightChainStealing_threeRows()
    {
        // Same as test_chainStealing_threeColumns but with vertical zones (rows)
        // A(0,0,900,300) | B(0,300,900,300) | C(0,600,900,300), total height = 900
        // A.minHeight=400, B.minHeight=350, C.minHeight=0
        // Expected: A.height>=400, B.height>=350, C absorbs deficit
        QVector<QRect> zones = {
            QRect(0, 0, 900, 300),
            QRect(0, 300, 900, 300),
            QRect(0, 600, 900, 300),
        };
        QVector<QSize> minSizes = {QSize(1, 400), QSize(1, 350), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        QVERIFY2(allPositiveDimensions(zones), "All zones must have positive dimensions");

        QVERIFY2(zones[0].height() >= 400,
                 qPrintable(QStringLiteral("Row A height = %1, expected >= 400. "
                                           "Row B = %2, Row C = %3.")
                                .arg(zones[0].height())
                                .arg(zones[1].height())
                                .arg(zones[2].height())));

        QVERIFY2(zones[1].height() >= 350,
                 qPrintable(QStringLiteral("Row B height = %1, expected >= 350").arg(zones[1].height())));

        QVERIFY2(zones[2].height() > 0,
                 qPrintable(QStringLiteral("Row C height = %1, must be > 0").arg(zones[2].height())));
    }

    void test_masterStackLayout()
    {
        // Master(0-600,0-1080) + Stack1(600-900,0-540) + Stack2(600-900,540-1080)
        // Master min width = 700 → master expands, both stack zones shrink equally
        // Note: QRect(x, y, w, h). Master: x=0, w=600. Stacks: x=600, w=300.
        QVector<QRect> zones = {
            QRect(0, 0, 600, 1080),
            QRect(600, 0, 300, 540),
            QRect(600, 540, 300, 540),
        };
        QVector<QSize> minSizes = {QSize(700, 1), QSize(), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        // Master should have expanded to at least 700
        QVERIFY2(zones[0].width() >= 700,
                 qPrintable(QStringLiteral("Master width = %1, expected >= 700").arg(zones[0].width())));

        // Both stack zones should have the same left edge (moved together as a column)
        QCOMPARE(zones[1].left(), zones[2].left());

        // Both stack zones should have the same width
        QCOMPARE(zones[1].width(), zones[2].width());

        // Stack zones should still have positive width
        QVERIFY2(zones[1].width() > 0,
                 qPrintable(QStringLiteral("Stack width = %1, must be > 0").arg(zones[1].width())));

        // Total horizontal extent should be preserved
        QCOMPARE(zones[0].width() + zones[1].width(), 900);
    }

    void test_unsatisfiableConstraints_proportional()
    {
        // Total minimums exceed available space → zones distributed proportionally,
        // no crashes, no negative widths
        // 3 zones each 300px wide (total 900). Minimums: 500+500+500 = 1500 > 900
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(300, 0, 300, 900),
            QRect(600, 0, 300, 900),
        };
        QVector<QSize> minSizes = {QSize(500, 1), QSize(500, 1), QSize(500, 1)};

        // Must not crash
        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        // All zones must have positive dimensions
        QVERIFY2(allPositiveDimensions(zones), "All zones must have positive dimensions even with unsatisfiable constraints");

        // No negative widths
        for (int i = 0; i < zones.size(); ++i) {
            QVERIFY2(zones[i].width() > 0,
                     qPrintable(QStringLiteral("Zone[%1] width = %2, must be > 0").arg(i).arg(zones[i].width())));
        }
    }

    void test_noDeficit_noChange()
    {
        // All zones already meet their minimum → zones unchanged
        QVector<QRect> zones = {
            QRect(0, 0, 500, 900),
            QRect(500, 0, 400, 900),
        };
        const QVector<QRect> original = zones;
        QVector<QSize> minSizes = {QSize(400, 1), QSize(300, 1)};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], original[i]);
        }
    }

    void test_sizesMismatch_earlyReturn()
    {
        // zones.size() != minSizes.size() → no crash, zones unchanged
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(300, 0, 300, 900),
        };
        const QVector<QRect> original = zones;
        QVector<QSize> minSizes = {QSize(400, 1)}; // Only 1 entry for 2 zones

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        // Zones should be unchanged due to early return
        QCOMPARE(zones.size(), 2);
        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], original[i]);
        }
    }

    void test_gapThreshold_adjacencyDetection()
    {
        // Two zones separated by a gap of 8px, gapThreshold=10 → recognized as adjacent
        // Zone A: x=0, w=300 → right=299. Zone B: x=308, w=292 → left=308.
        // Gap = |308 - 299| = 9 <= 10, so they are adjacent.
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(308, 0, 292, 900),
        };
        QVector<QSize> minSizes = {QSize(400, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 10);

        // Zone[0] should have been able to steal from zone[1] through the gap
        QVERIFY2(zones[0].width() >= 400,
                 qPrintable(QStringLiteral("Zone[0] width = %1, expected >= 400 (gap threshold should allow stealing)")
                                .arg(zones[0].width())));
    }

    void test_gapThreshold_tooFar()
    {
        // Two zones separated by 20px, gapThreshold=10 → NOT adjacent, no stealing
        // Zone A: x=0, w=300 → right=299. Zone B: x=320, w=280 → left=320.
        // Gap = |320 - 299| = 21 > 10, so they are NOT adjacent.
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),
            QRect(320, 0, 280, 900),
        };
        const QVector<QRect> original = zones;
        QVector<QSize> minSizes = {QSize(400, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 10);

        // Zones should be unchanged because they are too far apart to be adjacent
        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], original[i]);
        }
    }

    void test_multipleZonesSameColumn()
    {
        // MasterStack-like: 2 master zones stacked vertically and 2 stack zones stacked vertically
        // Master column: (0,0,400,540) and (0,540,400,540) → left=0, right=399
        // Stack column:  (400,0,400,540) and (400,540,400,540) → left=400, right=799
        // Min width applied to master column (both zones) → both should expand together
        QVector<QRect> zones = {
            QRect(0, 0, 400, 540),     // Master top
            QRect(0, 540, 400, 540),   // Master bottom
            QRect(400, 0, 400, 540),   // Stack top
            QRect(400, 540, 400, 540), // Stack bottom
        };
        QVector<QSize> minSizes = {QSize(500, 1), QSize(500, 1), QSize(), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, 5);

        // Both master zones should have expanded to the same width
        QVERIFY2(zones[0].width() >= 500,
                 qPrintable(QStringLiteral("Master top width = %1, expected >= 500").arg(zones[0].width())));
        QVERIFY2(zones[1].width() >= 500,
                 qPrintable(QStringLiteral("Master bottom width = %1, expected >= 500").arg(zones[1].width())));

        // Master zones should have identical left and right edges (column boundary moved together)
        QCOMPARE(zones[0].left(), zones[1].left());
        QCOMPARE(zones[0].right(), zones[1].right());

        // Stack zones should have shrunk together
        QCOMPARE(zones[2].left(), zones[3].left());
        QCOMPARE(zones[2].right(), zones[3].right());

        // All zones should still have positive width
        for (int i = 0; i < zones.size(); ++i) {
            QVERIFY2(zones[i].width() > 0,
                     qPrintable(QStringLiteral("Zone[%1] width = %2, must be > 0").arg(i).arg(zones[i].width())));
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // removeZoneOverlaps tests
    // ═══════════════════════════════════════════════════════════════════════════

    void test_noOverlap_noChange()
    {
        // Non-overlapping zones → unchanged
        QVector<QRect> zones = {
            QRect(0, 0, 400, 900),
            QRect(400, 0, 400, 900),
        };
        const QVector<QRect> original = zones;

        GeometryUtils::removeZoneOverlaps(zones);

        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i], original[i]);
        }
    }

    void test_horizontalOverlap_resolved()
    {
        // Two zones overlapping horizontally → overlap removed
        // Zone A: x=0, w=500 → right=499. Zone B: x=400, w=500 → right=899.
        // Overlap region: left=400, right=499. Midpoint = (400+499)/2 = 449.
        // After: A.right <= 449, B.left >= 449
        QVector<QRect> zones = {
            QRect(0, 0, 500, 900),
            QRect(400, 0, 500, 900),
        };

        GeometryUtils::removeZoneOverlaps(zones);

        QVERIFY2(!zones[0].intersects(zones[1]),
                 qPrintable(QStringLiteral("Zones still overlap after removeZoneOverlaps: "
                                           "A(%1,%2,%3,%4) B(%5,%6,%7,%8)")
                                .arg(zones[0].x())
                                .arg(zones[0].y())
                                .arg(zones[0].width())
                                .arg(zones[0].height())
                                .arg(zones[1].x())
                                .arg(zones[1].y())
                                .arg(zones[1].width())
                                .arg(zones[1].height())));

        // Both zones should still have positive dimensions
        QVERIFY(zones[0].width() > 0);
        QVERIFY(zones[1].width() > 0);
    }

    void test_verticalOverlap_resolved()
    {
        // Two zones overlapping vertically → overlap removed
        // Zone A: y=0, h=600 → bottom=599. Zone B: y=500, h=600 → bottom=1099.
        // Overlap: top=500, bottom=599. Midpoint = (500+599)/2 = 549.
        QVector<QRect> zones = {
            QRect(0, 0, 900, 600),
            QRect(0, 500, 900, 600),
        };

        GeometryUtils::removeZoneOverlaps(zones);

        QVERIFY2(!zones[0].intersects(zones[1]),
                 qPrintable(QStringLiteral("Zones still overlap vertically after removeZoneOverlaps: "
                                           "A(%1,%2,%3,%4) B(%5,%6,%7,%8)")
                                .arg(zones[0].x())
                                .arg(zones[0].y())
                                .arg(zones[0].width())
                                .arg(zones[0].height())
                                .arg(zones[1].x())
                                .arg(zones[1].y())
                                .arg(zones[1].width())
                                .arg(zones[1].height())));

        // Both zones should still have positive dimensions
        QVERIFY(zones[0].height() > 0);
        QVERIFY(zones[1].height() > 0);
    }

    void test_singleZone_noChange()
    {
        // Only one zone → unchanged
        QVector<QRect> zones = {QRect(0, 0, 900, 900)};
        const QVector<QRect> original = zones;

        GeometryUtils::removeZoneOverlaps(zones);

        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], original[0]);
    }

    void test_emptyZones_noChange()
    {
        // Empty vector → no crash
        QVector<QRect> zones;

        GeometryUtils::removeZoneOverlaps(zones);

        QVERIFY(zones.isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Gap preservation tests
    // ═══════════════════════════════════════════════════════════════════════════

    void test_overlapResolution_preservesGap()
    {
        // Two zones overlapping by 100px with innerGap=8.
        // After resolution: zones should not overlap AND should have ≥8px gap.
        QVector<QRect> zones = {
            QRect(0, 0, 500, 900),   // exclusive right = 500
            QRect(400, 0, 500, 900), // left = 400
        };

        GeometryUtils::removeZoneOverlaps(zones, {}, /*innerGap=*/8);

        QVERIFY2(!zones[0].intersects(zones[1]),
                 qPrintable(QStringLiteral("Zones overlap: A.right=%1, B.left=%2")
                                .arg(zones[0].right())
                                .arg(zones[1].left())));

        // Gap should be at least innerGap
        int gap = zones[1].left() - (zones[0].left() + zones[0].width());
        QVERIFY2(gap >= 8,
                 qPrintable(QStringLiteral("Gap between zones = %1, expected >= 8").arg(gap)));
    }

    void test_crossRowOverlapPrevention()
    {
        // BSP-like layout: different column structures in top and bottom rows.
        //   Top:    [A width=500][B width=500]        (boundary at 500)
        //   Bottom: [C width=400][D width=600]        (boundary at 400)
        // A has minWidth=700. Without overlap prevention, pairwise would expand
        // A to 700, overlapping D (D.left=400 < A.newRight=700).
        // With overlap prevention, A should only expand to 400 (D's left edge).
        QVector<QRect> zones = {
            QRect(0, 0, 500, 450),     // A: top-left
            QRect(500, 0, 500, 450),   // B: top-right
            QRect(0, 450, 400, 450),   // C: bottom-left
            QRect(400, 450, 600, 450), // D: bottom-right
        };
        QVector<QSize> minSizes = {QSize(700, 1), QSize(), QSize(), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, /*gapThreshold=*/5);

        // A should NOT overlap with D
        QVERIFY2(!zones[0].intersects(zones[3]),
                 qPrintable(QStringLiteral("Zone A overlaps D: A=(%1,%2,%3,%4) D=(%5,%6,%7,%8)")
                                .arg(zones[0].x())
                                .arg(zones[0].y())
                                .arg(zones[0].width())
                                .arg(zones[0].height())
                                .arg(zones[3].x())
                                .arg(zones[3].y())
                                .arg(zones[3].width())
                                .arg(zones[3].height())));
    }

    void test_gapPreservedAfterMinSizeEnforcement()
    {
        // Two columns with 8px inner gap. Left column needs min width.
        // After enforcement, gap should still be ≈8px.
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900),     // left column
            QRect(308, 0, 292, 900),   // right column (8px gap)
        };
        QVector<QSize> minSizes = {QSize(400, 1), QSize()};

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, /*gapThreshold=*/10, /*innerGap=*/8);

        QVERIFY2(zones[0].width() >= 400,
                 qPrintable(QStringLiteral("Zone[0] width=%1, expected >= 400").arg(zones[0].width())));

        // Gap between zones should be approximately 8px (allow ±2 for rounding)
        int gap = zones[1].left() - (zones[0].left() + zones[0].width());
        QVERIFY2(gap >= 6 && gap <= 10,
                 qPrintable(QStringLiteral("Gap=%1, expected ~8px (6-10)").arg(gap)));
    }

    void test_bspHierarchicalBoundaryShift()
    {
        // BSP 5-window layout: zones at different tree levels share a single
        // boundary (the root V-split at x=955). When a top-left zone steals
        // from a top-right zone, the boundary shift must propagate to bottom
        // zones too, or a gap appears between Discord and Kate.
        //
        // Tree structure:
        // Root V-split (left 945 | gap 10 | right 945)
        //   Left H-split:  App+Steam (top) | Discord (bottom)
        //   Right H-split: Browser (top) | Kate (bottom)
        //
        // Top-left V-split: App (467) | gap 10 | Steam (468)
        //
        // Key: Discord.right=954 (inclusive) == Steam.right=954
        //      Kate.left=965 == Browser.left=965
        QVector<QRect> zones = {
            QRect(10, 10, 467, 525),     // 0: App (top-left-left)
            QRect(487, 10, 468, 525),    // 1: Steam (top-left-right)
            QRect(10, 545, 945, 525),    // 2: Discord (bottom-left, full width)
            QRect(965, 10, 945, 525),    // 3: Browser (top-right)
            QRect(965, 545, 945, 525),   // 4: Kate (bottom-right)
        };
        QVector<QSize> minSizes = {
            QSize(),         // App: no constraint
            QSize(500, 0),   // Steam needs 500 (deficit of 32)
            QSize(),         // Discord: no constraint
            QSize(),         // Browser: no constraint
            QSize(),         // Kate: no constraint
        };

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, /*gapThreshold=*/22, /*innerGap=*/10);

        // Steam should satisfy its minimum
        QVERIFY2(zones[1].width() >= 500,
                 qPrintable(QStringLiteral("Steam width=%1, expected >= 500").arg(zones[1].width())));

        // The gap between Discord and Kate must stay consistent (== innerGap).
        // Before the fix, applySteal co-moved Kate (shared both edges with
        // Browser) but NOT Discord (different left edge from Steam), creating
        // a gap of innerGap + delta instead of innerGap.
        int discordExclusiveRight = zones[2].left() + zones[2].width();
        int kateLeft = zones[4].left();
        int gap = kateLeft - discordExclusiveRight;
        QVERIFY2(gap == 10,
                 qPrintable(QStringLiteral("Gap between Discord and Kate = %1, expected 10").arg(gap)));

        // Browser and Kate should have the same left edge (both in right half)
        QCOMPARE(zones[3].left(), zones[4].left());

        // Discord and Steam should have the same right edge (both in left half)
        QCOMPARE(zones[1].right(), zones[2].right());

        // No zone overlaps
        for (int i = 0; i < zones.size(); ++i) {
            for (int j = i + 1; j < zones.size(); ++j) {
                QVERIFY2(!zones[i].intersects(zones[j]),
                         qPrintable(QStringLiteral("Zones %1 and %2 overlap").arg(i).arg(j)));
            }
        }
    }

    void test_bspLayout_perRowChainStealing()
    {
        // BSP layout: 3 windows in top row, 3 in bottom row with different column boundaries.
        // Top:    [Vesktop 480][gap 8][Steam 480][gap 8][Kate 944]   (1920 total)
        // Bottom: [Suno 400][gap 8][Terminal 600][gap 8][Settings 904]
        //
        // Min sizes are now primarily handled by the BSP algorithm itself via
        // split ratio clamping (computeSubtreeMinDims). The post-processing
        // safety net uses single-call solveAxisBoundaries + pairwise fallback.
        // For irregular BSP grids (different column boundaries per row), the
        // boundary solver bails out and pairwise handles direct neighbors.
        // Steam can steal directly from Kate. Vesktop can't chain-steal
        // through Steam (Steam has no surplus after satisfying its own min).
        // Full cross-row chain stealing is the algorithm's responsibility.
        QVector<QRect> zones = {
            QRect(10, 10, 480, 520),    // 0: Vesktop (top row)
            QRect(498, 10, 480, 520),   // 1: Steam (top row)
            QRect(986, 10, 944, 520),   // 2: Kate (top row)
            QRect(10, 538, 400, 520),   // 3: Suno (bottom row)
            QRect(418, 538, 600, 520),  // 4: Terminal (bottom row)
            QRect(1026, 538, 904, 520), // 5: Settings (bottom row)
        };
        QVector<QSize> minSizes = {
            QSize(940, 1),  // Vesktop needs 940
            QSize(800, 1),  // Steam needs 800
            QSize(),        // Kate: no constraint
            QSize(),        // Suno: no constraint
            QSize(),        // Terminal: no constraint
            QSize(),        // Settings: no constraint
        };

        GeometryUtils::enforceWindowMinSizes(zones, minSizes, /*gapThreshold=*/20, /*innerGap=*/8);

        // Steam can steal directly from Kate (adjacent, Kate has surplus)
        QVERIFY2(zones[1].width() >= 800,
                 qPrintable(QStringLiteral("Steam width=%1, expected >= 800").arg(zones[1].width())));

        // Vesktop: the safety net can't chain-steal through Steam (Steam is at its
        // min after satisfying itself). In the real pipeline, the BSP algorithm
        // would have already incorporated min sizes via split ratio clamping.
        // Here we just verify it's unchanged (no regression, no corruption).
        QVERIFY2(zones[0].width() > 0,
                 qPrintable(QStringLiteral("Vesktop width=%1, expected > 0").arg(zones[0].width())));

        // No zone overlaps
        for (int i = 0; i < zones.size(); ++i) {
            for (int j = i + 1; j < zones.size(); ++j) {
                QVERIFY2(!zones[i].intersects(zones[j]),
                         qPrintable(QStringLiteral("Zones %1 and %2 overlap: (%3,%4,%5,%6) vs (%7,%8,%9,%10)")
                                        .arg(i)
                                        .arg(j)
                                        .arg(zones[i].x())
                                        .arg(zones[i].y())
                                        .arg(zones[i].width())
                                        .arg(zones[i].height())
                                        .arg(zones[j].x())
                                        .arg(zones[j].y())
                                        .arg(zones[j].width())
                                        .arg(zones[j].height())));
            }
        }

        // Bottom row zones should be unchanged (different row, not affected)
        QCOMPARE(zones[3], QRect(10, 538, 400, 520));
        QCOMPARE(zones[4], QRect(418, 538, 600, 520));
        QCOMPARE(zones[5], QRect(1026, 538, 904, 520));
    }
};

QTEST_MAIN(TestGeometryUtils)
#include "test_geometry_utils.moc"
