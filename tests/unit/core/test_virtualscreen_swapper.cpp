// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_virtualscreen_swapper.cpp
 * @brief Integration tests for VirtualScreenSwapper against a real Settings.
 *
 * Tests cover:
 * - swapInDirection exchanges geometry between adjacent VSs
 * - swapInDirection preserves VS IDs, physical ID, display names, indices
 * - swapInDirection is scoped to the current physical monitor
 * - swapInDirection no-ops when no sibling lies in the requested direction
 * - swapInDirection rejects non-virtual ids and empty configs
 * - rotate(cw/ccw) cycles regions in spatial CW order
 * - rotate returns to the original state after N full rotations
 * - rotate no-ops on a single-VS (or unconfigured) physical monitor
 * - rotate on a 2×2 grid follows CW angle-from-centroid ring order
 */

#include <QTest>
#include <QSignalSpy>

#include "core/utils.h"
#include "core/virtualscreen.h"
#include "core/virtualscreenswapper.h"
#include "config/settings.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/VirtualScreenTestHelpers.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using PlasmaZones::TestHelpers::makeDef;
using PlasmaZones::TestHelpers::makeSplitConfig;
using PlasmaZones::TestHelpers::makeThreeWayConfig;

class TestVirtualScreenSwapper : public QObject
{
    Q_OBJECT

private:
    /// Build a 2×2 grid of four VSs on a single physical monitor.
    static VirtualScreenConfig makeTwoByTwoGrid(const QString& physId)
    {
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = physId;
        cfg.screens.append(makeDef(physId, 0, QStringLiteral("TL"), QRectF(0.0, 0.0, 0.5, 0.5)));
        cfg.screens.append(makeDef(physId, 1, QStringLiteral("TR"), QRectF(0.5, 0.0, 0.5, 0.5)));
        cfg.screens.append(makeDef(physId, 2, QStringLiteral("BL"), QRectF(0.0, 0.5, 0.5, 0.5)));
        cfg.screens.append(makeDef(physId, 3, QStringLiteral("BR"), QRectF(0.5, 0.5, 0.5, 0.5)));
        return cfg;
    }

private Q_SLOTS:

    // ─── swapInDirection ───────────────────────────────────────────────────

    void swap_twoSplit_exchangesGeometry()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        const QString leftId = VirtualScreenId::make(physId, 0);
        const QString rightId = VirtualScreenId::make(physId, 1);

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(swapper.swapInDirection(leftId, Utils::Direction::Right));

        const VirtualScreenConfig after = settings.virtualScreenConfig(physId);
        QCOMPARE(after.screens.size(), 2);
        // Left VS now holds the original right region and vice versa.
        QVERIFY(qFuzzyCompare(after.screens[0].region.x(), 0.5));
        QVERIFY(qFuzzyCompare(after.screens[1].region.x() + 1.0, 1.0)); // x=0.0 comparison
        // Everything else preserved.
        QCOMPARE(after.screens[0].id, leftId);
        QCOMPARE(after.screens[1].id, rightId);
        QCOMPARE(after.screens[0].displayName, QStringLiteral("Left"));
        QCOMPARE(after.screens[1].displayName, QStringLiteral("Right"));
        QCOMPARE(after.screens[0].index, 0);
        QCOMPARE(after.screens[1].index, 1);
    }

    void swap_noSiblingInDirection_returnsFalse()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);

        VirtualScreenSwapper swapper(&settings);
        // Left VS — "left" has no sibling in that direction.
        QVERIFY(!swapper.swapInDirection(VirtualScreenId::make(physId, 0), Utils::Direction::Left));
        // Vertical direction on a horizontal split has no sibling either.
        QVERIFY(!swapper.swapInDirection(VirtualScreenId::make(physId, 0), Utils::Direction::Up));

        const VirtualScreenConfig after = settings.virtualScreenConfig(physId);
        QCOMPARE(after, before); // unchanged
    }

    void swap_nonVirtualId_returnsFalse()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(!swapper.swapInDirection(physId, Utils::Direction::Right));
    }

    void swap_unknownPhysical_returnsFalse()
    {
        IsolatedConfigGuard guard;
        Settings settings; // no VS configured

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(!swapper.swapInDirection(QStringLiteral("ghost/vs:0"), Utils::Direction::Right));
    }

    void swap_resultPassesValidation()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(swapper.swapInDirection(VirtualScreenId::make(physId, 0), Utils::Direction::Right));

        QString err;
        QVERIFY2(VirtualScreenConfig::isValid(settings.virtualScreenConfig(physId), physId, 8, &err), qPrintable(err));
    }

    void swap_scopedToPhysicalMonitor()
    {
        // Two physical monitors, each with two VSs. Swap on monitor A must
        // not touch monitor B.
        IsolatedConfigGuard guard;
        const QString physA = QStringLiteral("DP-1");
        const QString physB = QStringLiteral("DP-2");
        Settings settings;
        settings.setVirtualScreenConfig(physA, makeSplitConfig(physA));
        settings.setVirtualScreenConfig(physB, makeSplitConfig(physB));
        const VirtualScreenConfig bBefore = settings.virtualScreenConfig(physB);

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(swapper.swapInDirection(VirtualScreenId::make(physA, 0), Utils::Direction::Right));

        QCOMPARE(settings.virtualScreenConfig(physB), bBefore);
    }

    // ─── rotate ───────────────────────────────────────────────────────────

    void rotate_threeSplit_clockwise()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);
        const QRectF r0 = before.screens[0].region; // Left
        const QRectF r1 = before.screens[1].region; // Center
        const QRectF r2 = before.screens[2].region; // Right

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(swapper.rotate(physId, /*clockwise=*/true));

        // Three horizontal VSs all at the same y as the centroid. The
        // centroid-based CW angle order (atan2(dx, -dy) with -dy = -0) wraps
        // the centre VS to angle π, giving the ring [Right, Centre, Left]
        // starting from the smallest angle. A CW rotate in that ring:
        //   Right  takes Centre's old region
        //   Centre takes Left's   old region
        //   Left   takes Right's  old region
        // i.e. every VS shifts one slot leftward with the leftmost wrapping
        // around to the right. On a 1D strip this is self-consistent and
        // cycles back to the original after N rotations; the visual direction
        // is less meaningful without a true 2D grid.
        const VirtualScreenConfig after = settings.virtualScreenConfig(physId);
        QCOMPARE(after.screens[0].region, r2); // Left slot gets old Right
        QCOMPARE(after.screens[1].region, r0); // Centre slot gets old Left
        QCOMPARE(after.screens[2].region, r1); // Right slot gets old Centre
    }

    void rotate_threeSplit_cycleReturnsToStart()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);

        VirtualScreenSwapper swapper(&settings);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(swapper.rotate(physId, true));
        }
        QCOMPARE(settings.virtualScreenConfig(physId), before);
    }

    void rotate_clockwiseThenCCW_isNoOp()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(swapper.rotate(physId, true));
        QVERIFY(swapper.rotate(physId, false));
        QCOMPARE(settings.virtualScreenConfig(physId), before);
    }

    void rotate_twoByTwoGrid_clockwiseOrder()
    {
        // Centroid-based CW angle-from-centroid ring order on a 2×2 grid
        // starts from the top-right (smallest positive angle when measured
        // CW from "up") and walks TR → BR → BL → TL.
        // CW rotate then moves each VS forward in that ring: the slot that
        // was TR now holds BR's old region, BR → BL's, BL → TL's, TL → TR's.
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeTwoByTwoGrid(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);
        const QRectF tl = before.screens[0].region; // TL
        const QRectF tr = before.screens[1].region; // TR
        const QRectF bl = before.screens[2].region; // BL
        const QRectF br = before.screens[3].region; // BR

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(swapper.rotate(physId, /*clockwise=*/true));

        const VirtualScreenConfig after = settings.virtualScreenConfig(physId);
        // def[0] = TL slot; def[1] = TR slot; def[2] = BL slot; def[3] = BR slot.
        // In CW ring [TR, BR, BL, TL], CW rotate gives:
        //   TR ← BR,  BR ← BL,  BL ← TL,  TL ← TR
        QCOMPARE(after.screens[0].region, tr); // TL slot gets old TR
        QCOMPARE(after.screens[1].region, br); // TR slot gets old BR
        QCOMPARE(after.screens[2].region, tl); // BL slot gets old TL
        QCOMPARE(after.screens[3].region, bl); // BR slot gets old BL
    }

    void rotate_twoByTwoGrid_fullCycle()
    {
        // Four rotations on a 4-element ring should return to the start.
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeTwoByTwoGrid(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);

        VirtualScreenSwapper swapper(&settings);
        for (int i = 0; i < 4; ++i) {
            QVERIFY(swapper.rotate(physId, true));
        }
        QCOMPARE(settings.virtualScreenConfig(physId), before);
    }

    void rotate_unconfiguredMonitor_returnsFalse()
    {
        IsolatedConfigGuard guard;
        Settings settings; // no VS configured

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(!swapper.rotate(QStringLiteral("DP-1"), true));
    }

    void rotate_rejectsVirtualId()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(!swapper.rotate(VirtualScreenId::make(physId, 0), true));
    }

    void rotate_scopedToPhysicalMonitor()
    {
        // Rotate on monitor A must not touch monitor B.
        IsolatedConfigGuard guard;
        const QString physA = QStringLiteral("DP-1");
        const QString physB = QStringLiteral("DP-2");
        Settings settings;
        settings.setVirtualScreenConfig(physA, makeThreeWayConfig(physA));
        settings.setVirtualScreenConfig(physB, makeThreeWayConfig(physB));
        const VirtualScreenConfig bBefore = settings.virtualScreenConfig(physB);

        VirtualScreenSwapper swapper(&settings);
        QVERIFY(swapper.rotate(physA, true));
        QCOMPARE(settings.virtualScreenConfig(physB), bBefore);
    }
};

QTEST_MAIN(TestVirtualScreenSwapper)
#include "test_virtualscreen_swapper.moc"
