// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_geometry_utils_clamp.cpp
 * @brief Unit tests for PhosphorGeometry::clampZonesToScreen()
 *
 * Reproduces the bug fixed in PR #388: when an autotile zone is narrower than
 * the window's declared min size and sits on the right/bottom edge of a
 * virtual screen, KWin's compositor-side min-size enforcement grows the
 * window past the screen boundary; if an adjacent monitor is butted up to
 * that edge, the window's center crosses into it and the autotile engine
 * ejects the window from the layout.
 *
 * The clamp is a position-only shift — sizes are never changed (size changes
 * are owned by enforceMinSizes, which is unsafe for any algorithm
 * where producesOverlappingZones() returns true).
 */

#include <QRect>
#include <QSize>
#include <QTest>
#include <QVector>

#include <PhosphorGeometry/GeometryUtils.h>

class TestGeometryUtilsClamp : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── No-op cases ──────────────────────────────────────────────────────

    void test_emptyZones_noOp()
    {
        QVector<QRect> zones;
        const QVector<QSize> minSizes;
        const QRect screen(0, 0, 1920, 1080);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        QVERIFY(zones.isEmpty());
    }

    void test_invalidScreen_noOp()
    {
        QVector<QRect> zones = {QRect(2796, 40, 396, 1700)};
        const QVector<QRect> original = zones;
        const QVector<QSize> minSizes = {QSize(940, 500)};
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, QRect());
        QCOMPARE(zones, original);
    }

    void test_zoneFitsExactly_noShift()
    {
        // Zone right edge sits exactly on the screen's exclusive right edge.
        // No min-size constraint (or min ≤ zone). Must not shift.
        QVector<QRect> zones = {QRect(1000, 0, 920, 1080)};
        const QVector<QSize> minSizes = {QSize(800, 500)};
        const QRect screen(0, 0, 1920, 1080);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        QCOMPARE(zones[0], QRect(1000, 0, 920, 1080));
    }

    void test_emptyMinSizes_noShiftWhenZonesFit()
    {
        QVector<QRect> zones = {QRect(0, 0, 800, 600), QRect(800, 0, 1120, 600)};
        const QVector<QRect> original = zones;
        const QRect screen(0, 0, 1920, 600);
        // Empty minSizes vector — only the zones' own dimensions matter.
        PhosphorGeometry::clampZonesToScreen(zones, {}, screen);
        QCOMPARE(zones, original);
    }

    void test_zerosMinSize_noShiftWhenZonesFit()
    {
        QVector<QRect> zones = {QRect(0, 0, 800, 600), QRect(800, 0, 1120, 600)};
        const QVector<QRect> original = zones;
        const QVector<QSize> minSizes = {QSize(0, 0), QSize(0, 0)};
        const QRect screen(0, 0, 1920, 600);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        QCOMPARE(zones, original);
    }

    // ─── The PR #388 reproduction ──────────────────────────────────────────

    void test_pr388_repro_deckAlgoVesktopOnVsRightEdge()
    {
        // Setup from the PR description:
        //   Virtual screen LG:115107/vs:1 at (1600, 32, 1600, 1716) → right edge x=3200.
        //   Deck algorithm with 3 windows; vesktop assigned QRect(2796,40 396x1700).
        //   vesktop has min 940×500. KWin grows the window to 940 wide → right
        //   edge would be at 3736, crossing into adjacent monitor at x=3200+.
        //   Expectation: clamp shifts the zone left so 2796 + 940 ≤ 3200,
        //   i.e. zone.x() = 3200 - 940 = 2260.
        QVector<QRect> zones = {
            QRect(1608, 40, 1188, 1700),
            QRect(2796, 40, 396, 1700), // vesktop's zone — undersized
            QRect(2994, 40, 198, 1700), // overlapping (Deck stack)
        };
        const QVector<QSize> minSizes = {
            QSize(0, 0),
            QSize(940, 500),
            QSize(0, 0),
        };
        // Virtual screen geometry (NOT the physical monitor — what makes the
        // daemon-side fix VS-aware).
        const QRect screen(1600, 32, 1600, 1716);

        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);

        // Vesktop zone shifted left so effective right (zone.x + minSize.w) fits.
        QCOMPARE(zones[1].x(), 2260);
        QCOMPARE(zones[1].size(), QSize(396, 1700)); // size preserved
        // Adjacent zone (no min-size constraint, fits on screen) — untouched.
        QCOMPARE(zones[0], QRect(1608, 40, 1188, 1700));
        // Last zone fits within the VS without min-size pressure — untouched
        // (its right edge 2994+198 = 3192 ≤ 3200).
        QCOMPARE(zones[2], QRect(2994, 40, 198, 1700));
        // The actual contract being defended: KWin will grow vesktop to its
        // declared min size, and the resulting effective rect must stay
        // inside the VS (otherwise it crosses to an adjacent monitor and
        // gets ejected — the original bug). Lock in effective-rect bounds
        // rather than just the integer that came out today.
        const int effRight1 = zones[1].x() + minSizes[1].width();
        const int effBottom1 = zones[1].y() + minSizes[1].height();
        QVERIFY2(effRight1 <= screen.x() + screen.width(),
                 qPrintable(QStringLiteral("effective right %1 must be <= VS right %2")
                                .arg(effRight1)
                                .arg(screen.x() + screen.width())));
        QVERIFY2(effBottom1 <= screen.y() + screen.height(),
                 qPrintable(QStringLiteral("effective bottom %1 must be <= VS bottom %2")
                                .arg(effBottom1)
                                .arg(screen.y() + screen.height())));
        QVERIFY(zones[1].x() >= screen.x());
        QVERIFY(zones[1].y() >= screen.y());
    }

    // ─── Right/bottom overflow ────────────────────────────────────────────

    void test_rightEdge_minWidthShiftsZoneLeft()
    {
        QVector<QRect> zones = {QRect(1000, 0, 200, 600)};
        const QVector<QSize> minSizes = {QSize(500, 0)};
        const QRect screen(0, 0, 1200, 600);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        // Effective width 500 → zone.x = 1200 - 500 = 700.
        QCOMPARE(zones[0], QRect(700, 0, 200, 600));
    }

    void test_bottomEdge_minHeightShiftsZoneUp()
    {
        // Symmetric vertical case from the PR description's "vertical stacked
        // monitors" mention.
        QVector<QRect> zones = {QRect(0, 800, 1920, 200)};
        const QVector<QSize> minSizes = {QSize(0, 600)};
        const QRect screen(0, 0, 1920, 1080);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        // Effective height 600 → zone.y = 1080 - 600 = 480.
        QCOMPARE(zones[0], QRect(0, 480, 1920, 200));
    }

    void test_zoneSizeAlreadyExceeds_shiftsByZoneSize()
    {
        // Min size smaller than the zone itself — clamp uses the zone's own
        // size (max of the two).
        QVector<QRect> zones = {QRect(1500, 0, 600, 600)};
        const QVector<QSize> minSizes = {QSize(100, 100)};
        const QRect screen(0, 0, 1920, 600);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        // Effective width 600 → zone.x = 1920 - 600 = 1320.
        QCOMPARE(zones[0], QRect(1320, 0, 600, 600));
    }

    // ─── Unsatisfiable: window wider than the screen ──────────────────────

    void test_minWidthExceedsScreen_pinsToLeft()
    {
        // Window wider than the screen → no on-screen position works. Pin to
        // screenLeft and accept overflow on the right (better than negative x).
        QVector<QRect> zones = {QRect(100, 0, 200, 600)};
        const QVector<QSize> minSizes = {QSize(2000, 0)};
        const QRect screen(0, 0, 1200, 600);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        QCOMPARE(zones[0].x(), 0);
        QCOMPARE(zones[0].size(), QSize(200, 600));
    }

    void test_minHeightExceedsScreen_pinsToTop()
    {
        QVector<QRect> zones = {QRect(0, 100, 1920, 200)};
        const QVector<QSize> minSizes = {QSize(0, 2000)};
        const QRect screen(0, 0, 1920, 1080);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        QCOMPARE(zones[0].y(), 0);
        QCOMPARE(zones[0].size(), QSize(1920, 200));
    }

    // ─── Symmetric left/top underflow ─────────────────────────────────────

    void test_leftUnderflow_zoneOriginBeforeScreenLeft_snapsRight()
    {
        // Algorithm bug or odd coordinates: zone.x < screen.x. Symmetric to
        // right-edge case.
        QVector<QRect> zones = {QRect(50, 100, 400, 400)};
        const QRect screen(100, 100, 1000, 1000);
        PhosphorGeometry::clampZonesToScreen(zones, {QSize()}, screen);
        QCOMPARE(zones[0], QRect(100, 100, 400, 400));
    }

    void test_topUnderflow_zoneOriginBeforeScreenTop_snapsDown()
    {
        QVector<QRect> zones = {QRect(100, 50, 400, 400)};
        const QRect screen(100, 100, 1000, 1000);
        PhosphorGeometry::clampZonesToScreen(zones, {QSize()}, screen);
        QCOMPARE(zones[0], QRect(100, 100, 400, 400));
    }

    // ─── Negative-origin screens (multi-monitor with non-zero origin) ─────

    void test_negativeOriginScreen_clampsCorrectly()
    {
        // Multi-monitor configurations can place a screen at negative
        // coordinates. The clamp must use the screen's actual origin, not 0.
        QVector<QRect> zones = {QRect(-200, -100, 400, 600)};
        const QVector<QSize> minSizes = {QSize(800, 0)};
        const QRect screen(-1920, -1080, 1920, 1080);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        // screenRight = -1920 + 1920 = 0. effW = 800. zone.x + 800 = 600 > 0
        // → shift to qMax(-1920, 0 - 800) = -800.
        QCOMPARE(zones[0].x(), -800);
        QCOMPARE(zones[0].size(), QSize(400, 600));
    }

    // ─── minSizes vector shorter than zones (defensive) ───────────────────

    void test_minSizesShorterThanZones_treatsMissingAsZero()
    {
        // Defensive: if the vectors are out of sync (e.g. caller mistake),
        // the missing entries are treated as zero min size — i.e. only the
        // zone's own size constrains it. Must not read past minSizes.size()
        // and must not skip the over-the-end zones entirely.
        QVector<QRect> zones = {
            QRect(0, 0, 100, 100),
            QRect(1620, 0, 300, 100), // fits on a 1920 screen as-is
        };
        const QVector<QSize> minSizes = {QSize(100, 100)}; // only one entry
        const QRect screen(0, 0, 1920, 100);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        // First zone: covered by minSizes[0], fits — no shift.
        QCOMPARE(zones[0], QRect(0, 0, 100, 100));
        // Second zone: minSizes[1] missing → treated as (0,0); zone fits on
        // its own (1620 + 300 = 1920) → no shift, no crash.
        QCOMPARE(zones[1], QRect(1620, 0, 300, 100));
    }

    // ─── Sizes never change ───────────────────────────────────────────────

    void test_sizesPreservedAcrossAllShifts()
    {
        // Belt-and-suspenders: clamp must never change zone width/height,
        // only x/y. This is the contract that lets it run safely on any
        // algorithm where producesOverlappingZones() returns true (Deck,
        // Stair, Cascade, Monocle, Paper, Spread, horizontal-deck, and
        // any future opt-in).
        QVector<QRect> zones = {
            QRect(2000, 0, 100, 600), // overflows right
            QRect(0, 800, 800, 200), // overflows bottom
            QRect(-50, 100, 400, 400), // underflows left
        };
        const QVector<QSize> originalSizes = {zones[0].size(), zones[1].size(), zones[2].size()};
        const QVector<QSize> minSizes = {QSize(500, 0), QSize(0, 600), QSize()};
        const QRect screen(0, 0, 1920, 1080);
        PhosphorGeometry::clampZonesToScreen(zones, minSizes, screen);
        for (int i = 0; i < zones.size(); ++i) {
            QCOMPARE(zones[i].size(), originalSizes[i]);
        }
    }

    // ─── enforceMinSizes shares the tolerant-vector contract ────────
    // PR #388 aligned enforceMinSizes' behavior on mismatched vector
    // sizes with clampZonesToScreen's: both tolerate minSizes shorter than
    // zones (treating missing entries as no minimum). These tests lock in
    // that shared contract so the two functions don't drift apart again.

    void test_enforce_minSizesShorterThanZones_stillEnforcesPrefix()
    {
        // minSizes covers only the first zone; the second has no minimum and
        // must be left untouched. Pre-PR #388, the size-mismatch early-return
        // would have left BOTH zones untouched — a latent guard that PR #388
        // tightened up to share the tolerant-vector contract with
        // clampZonesToScreen. (Not the production bug per se: the in-tree
        // call site always passes equal-length vectors, so the early return
        // never fired in production. This test locks in the new contract so
        // the two functions don't drift.)
        //
        // Sizing: greedy stealer caps each steal at neighbor.width() / 3, so
        // pick a deficit ≤ donor/3 (here 100 ≤ 300) to verify enforcement
        // happens in a single pass.
        QVector<QRect> zones = {
            QRect(0, 0, 300, 900), // below 400 min — should grow by 100
            QRect(300, 0, 300, 900), // donor neighbor (300/3 = 100 max steal)
        };
        // Qt's QSize::isEmpty() returns true if either dim ≤ 0, so a
        // single-axis constraint must use a 1px sentinel on the unused axis.
        // Matches the convention in tests/unit/core/test_geometry_utils_minsizes.cpp.
        const QVector<QSize> minSizes = {QSize(400, 1)}; // only one entry
        PhosphorGeometry::enforceMinSizes(zones, minSizes, /*gapThreshold=*/5);
        // First zone grew toward its 400 min by stealing from the donor.
        QVERIFY2(zones[0].width() >= 400,
                 qPrintable(QStringLiteral("zone[0].width() %1 should be >= 400").arg(zones[0].width())));
        // Total width preserved (stealing is zero-sum).
        QCOMPARE(zones[0].width() + zones[1].width(), 600);
    }

    void test_enforce_minSizesLongerThanZones_extraEntriesIgnored()
    {
        // Extra trailing minSizes past zones.size() must not crash and must
        // not affect the existing zones' enforcement.
        QVector<QRect> zones = {
            QRect(0, 0, 200, 900),
            QRect(200, 0, 400, 900),
        };
        const QVector<QRect> originalZones = zones;
        const QVector<QSize> minSizes = {
            QSize(0, 0),
            QSize(0, 0), //
            QSize(99999, 99999), // extra entry past zones.size() — must be ignored
        };
        PhosphorGeometry::enforceMinSizes(zones, minSizes, /*gapThreshold=*/5);
        // No constraint active on either real zone → nothing should change.
        QCOMPARE(zones, originalZones);
    }

    void test_enforce_emptyMinSizes_noOp()
    {
        // Documented contract: empty minSizes is a no-op (nothing to enforce).
        // Distinct from clampZonesToScreen, which would still clamp by zone size.
        QVector<QRect> zones = {QRect(0, 0, 100, 100), QRect(2000, 0, 100, 100)};
        const QVector<QRect> originalZones = zones;
        PhosphorGeometry::enforceMinSizes(zones, {}, /*gapThreshold=*/5);
        QCOMPARE(zones, originalZones);
    }

    void test_enforce_emptyZones_noOp()
    {
        QVector<QRect> zones;
        const QVector<QSize> minSizes = {QSize(400, 400)};
        PhosphorGeometry::enforceMinSizes(zones, minSizes, /*gapThreshold=*/5);
        QVERIFY(zones.isEmpty());
    }
};

QTEST_MAIN(TestGeometryUtilsClamp)
#include "test_geometry_utils_clamp.moc"
