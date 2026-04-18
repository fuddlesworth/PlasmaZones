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
#include "config/settingsconfigstore.h"
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

private:
    using Result = VirtualScreenSwapper::Result;

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

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.swapInDirection(leftId, Utils::Direction::Right), Result::Ok);

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

    void swap_noSiblingInDirection_returnsStructuredReason()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        // Left VS — "left" has no sibling in that direction.
        QCOMPARE(swapper.swapInDirection(VirtualScreenId::make(physId, 0), Utils::Direction::Left),
                 Result::NoSiblingInDirection);
        // Vertical direction on a horizontal split has no sibling either.
        QCOMPARE(swapper.swapInDirection(VirtualScreenId::make(physId, 0), Utils::Direction::Up),
                 Result::NoSiblingInDirection);

        const VirtualScreenConfig after = settings.virtualScreenConfig(physId);
        QCOMPARE(after, before); // unchanged
    }

    void swap_nonVirtualId_returnsNotVirtual()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.swapInDirection(physId, Utils::Direction::Right), Result::NotVirtual);
    }

    void swap_unknownPhysical_returnsNoSubdivision()
    {
        IsolatedConfigGuard guard;
        Settings settings; // no VS configured

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        // Looks like a virtual id, but its physical screen has no entry in
        // Settings — the helper falls through to the size-check failure.
        QCOMPARE(swapper.swapInDirection(QStringLiteral("ghost/vs:0"), Utils::Direction::Right), Result::NoSubdivision);
    }

    void swap_unknownVirtualInExistingConfig_returnsUnknownVirtualScreen()
    {
        // Physical monitor has a valid 2-VS split, but the caller passes a
        // virtual id whose physical prefix points at that monitor yet whose
        // index is not present in the config (stale id after reconfiguration).
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        // Index 7 is out-of-range; id format still parses as virtual.
        QCOMPARE(swapper.swapInDirection(VirtualScreenId::make(physId, 7), Utils::Direction::Right),
                 Result::UnknownVirtualScreen);
    }

    void swap_emptyDirection_returnsInvalidDirection()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.swapInDirection(VirtualScreenId::make(physId, 0), QString()), Result::InvalidDirection);
    }

    void swap_resultPassesValidation()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.swapInDirection(VirtualScreenId::make(physId, 0), Utils::Direction::Right), Result::Ok);

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

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.swapInDirection(VirtualScreenId::make(physA, 0), Utils::Direction::Right), Result::Ok);

        QCOMPARE(settings.virtualScreenConfig(physB), bBefore);
    }

    void reasonString_emptyForOk()
    {
        QVERIFY(VirtualScreenSwapper::reasonString(Result::Ok).isEmpty());
        QCOMPARE(VirtualScreenSwapper::reasonString(Result::NoSubdivision), QStringLiteral("no_subdivision"));
        QCOMPARE(VirtualScreenSwapper::reasonString(Result::UnknownVirtualScreen), QStringLiteral("unknown_vs"));
        QCOMPARE(VirtualScreenSwapper::reasonString(Result::NoSiblingInDirection), QStringLiteral("no_sibling"));
        QCOMPARE(VirtualScreenSwapper::reasonString(Result::NotVirtual), QStringLiteral("not_virtual"));
        QCOMPARE(VirtualScreenSwapper::reasonString(Result::InvalidDirection), QStringLiteral("invalid_direction"));
        QCOMPARE(VirtualScreenSwapper::reasonString(Result::SettingsRejected), QStringLiteral("settings_rejected"));
    }

    // ─── rotate ───────────────────────────────────────────────────────────

    void rotate_threeSplit_clockwise()
    {
        // 3-VS horizontal strip — the 1D fallback in computeCwRingOrder
        // sorts left→right (ascending x), so the ring is [Left, Centre,
        // Right]. CW rotate in that ring means each VS inherits the region
        // of its successor, i.e. content shifts one slot leftward and the
        // leftmost wraps around to the rightmost slot. This matches the
        // visual expectation that "clockwise on a horizontal strip" cycles
        // content from right to left along the strip's natural reading
        // order.
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);
        const QRectF r0 = before.screens[0].region; // Left
        const QRectF r1 = before.screens[1].region; // Center
        const QRectF r2 = before.screens[2].region; // Right

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.rotate(physId, /*clockwise=*/true), Result::Ok);

        const VirtualScreenConfig after = settings.virtualScreenConfig(physId);
        // Ring order [Left, Centre, Right]; CW rotate (each inherits
        // successor's region): Left ← Centre, Centre ← Right, Right ← Left.
        QCOMPARE(after.screens[0].region, r1); // Left slot gets old Centre
        QCOMPARE(after.screens[1].region, r2); // Centre slot gets old Right
        QCOMPARE(after.screens[2].region, r0); // Right slot gets old Left
    }

    void rotate_twoSplitHorizontal_equivalentToSwap()
    {
        // 2-VS horizontal strip — rotate must produce the same outcome as
        // swap (a 2-element ring rotation IS a swap). Pinning this so the
        // 1D fallback can't accidentally diverge from the swap path on the
        // simplest possible subdivision.
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.rotate(physId, /*clockwise=*/true), Result::Ok);

        const VirtualScreenConfig afterRotate = settings.virtualScreenConfig(physId);
        QCOMPARE(afterRotate.screens[0].region, before.screens[1].region);
        QCOMPARE(afterRotate.screens[1].region, before.screens[0].region);

        // CCW must round-trip back to the original.
        QCOMPARE(swapper.rotate(physId, /*clockwise=*/false), Result::Ok);
        QCOMPARE(settings.virtualScreenConfig(physId), before);
    }

    void rotate_twoSplitVertical_topToBottom()
    {
        // 2-VS vertical strip — the 1D fallback sorts top→bottom, so the
        // ring is [Top, Bottom]. CW rotate exchanges them.
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = physId;
        cfg.screens.append(makeDef(physId, 0, QStringLiteral("Top"), QRectF(0.0, 0.0, 1.0, 0.5)));
        cfg.screens.append(makeDef(physId, 1, QStringLiteral("Bottom"), QRectF(0.0, 0.5, 1.0, 0.5)));
        settings.setVirtualScreenConfig(physId, cfg);
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.rotate(physId, /*clockwise=*/true), Result::Ok);

        const VirtualScreenConfig after = settings.virtualScreenConfig(physId);
        QCOMPARE(after.screens[0].region, before.screens[1].region);
        QCOMPARE(after.screens[1].region, before.screens[0].region);
    }

    void rotate_threeSplit_cycleReturnsToStart()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(swapper.rotate(physId, true), Result::Ok);
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

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.rotate(physId, true), Result::Ok);
        QCOMPARE(swapper.rotate(physId, false), Result::Ok);
        QCOMPARE(settings.virtualScreenConfig(physId), before);
    }

    void rotate_twoByTwoGrid_clockwiseOrder()
    {
        // Centroid-based CW angle-from-centroid ring order on a 2×2 grid
        // starts from the top-right (smallest positive angle when measured
        // CW from "up") and walks TR → BR → BL → TL.
        // CW rotate then has each slot inherit its successor's region: the
        // slot that was TR now holds BR's old region, BR → BL's, BL → TL's,
        // TL → TR's.
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeTwoByTwoGrid(physId));
        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);
        const QRectF tl = before.screens[0].region; // TL
        const QRectF tr = before.screens[1].region; // TR
        const QRectF bl = before.screens[2].region; // BL
        const QRectF br = before.screens[3].region; // BR

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.rotate(physId, /*clockwise=*/true), Result::Ok);

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

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(swapper.rotate(physId, true), Result::Ok);
        }
        QCOMPARE(settings.virtualScreenConfig(physId), before);
    }

    void rotate_unconfiguredMonitor_returnsNoSubdivision()
    {
        IsolatedConfigGuard guard;
        Settings settings; // no VS configured

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.rotate(QStringLiteral("DP-1"), true), Result::NoSubdivision);
    }

    void rotate_rejectsVirtualId()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;
        settings.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.rotate(VirtualScreenId::make(physId, 0), true), Result::NotVirtual);
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

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.rotate(physA, true), Result::Ok);
        QCOMPARE(settings.virtualScreenConfig(physB), bBefore);
    }

    // Regression: a 2×2 grid whose row heights differ by slightly more than
    // VirtualScreenDef::Tolerance (1e-3) must take the centroid-angle path,
    // not fall through the 1D collinear fallback. Pins the boundary so a
    // future tweak to kCollinearEpsilon can't silently collapse 2D grids
    // into left→right strips.
    void rotate_nearCollinear2DGrid_takesCentroidPath()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("DP-1");
        Settings settings;

        // Row heights 0.4990 + 0.5010 → row centres at y=0.2495 and y=0.7505,
        // Δy = 0.501, far outside the 1e-3 collinearity epsilon.
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = physId;
        cfg.screens.append(makeDef(physId, 0, QStringLiteral("TL"), QRectF(0.0, 0.0, 0.5, 0.4990)));
        cfg.screens.append(makeDef(physId, 1, QStringLiteral("TR"), QRectF(0.5, 0.0, 0.5, 0.4990)));
        cfg.screens.append(makeDef(physId, 2, QStringLiteral("BL"), QRectF(0.0, 0.4990, 0.5, 0.5010)));
        cfg.screens.append(makeDef(physId, 3, QStringLiteral("BR"), QRectF(0.5, 0.4990, 0.5, 0.5010)));
        settings.setVirtualScreenConfig(physId, cfg);

        const VirtualScreenConfig before = settings.virtualScreenConfig(physId);
        const QRectF tl = before.screens[0].region;
        const QRectF tr = before.screens[1].region;
        const QRectF bl = before.screens[2].region;
        const QRectF br = before.screens[3].region;

        SettingsConfigStore swapStore(&settings);
        VirtualScreenSwapper swapper(&swapStore);
        QCOMPARE(swapper.rotate(physId, /*clockwise=*/true), Result::Ok);

        // If the 2D centroid path was taken, the ring order is
        // [TR, BR, BL, TL] and the post-rotate mapping is:
        //   TR ← BR, BR ← BL, BL ← TL, TL ← TR   (per test rotate_twoByTwoGrid_clockwiseOrder)
        // If the 1D fallback had fired instead, the order would be
        // left→right (or top→bottom) and the result would NOT match these.
        const VirtualScreenConfig after = settings.virtualScreenConfig(physId);
        QCOMPARE(after.screens[0].region, tr); // TL slot gets old TR
        QCOMPARE(after.screens[1].region, br); // TR slot gets old BR
        QCOMPARE(after.screens[2].region, tl); // BL slot gets old TL
        QCOMPARE(after.screens[3].region, bl); // BR slot gets old BL

        // And a full 4-rotation cycle returns to the start — pins the ring
        // length at 4, which fails loudly if the collinear fallback ever
        // sorts this config as a 1D strip.
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(swapper.rotate(physId, true), Result::Ok);
        }
        QCOMPARE(settings.virtualScreenConfig(physId), before);
    }
};

QTEST_MAIN(TestVirtualScreenSwapper)
#include "test_virtualscreen_swapper.moc"
