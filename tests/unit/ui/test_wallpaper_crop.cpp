// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorShaders/ShaderRegistry.h>

#include <QRect>
#include <QSize>
#include <QtTest/QtTest>

using PhosphorShaders::ShaderRegistry;

// Unit tests for ShaderRegistry::computeWallpaperCropRect.
//
// This is the pure geometry of the per-VS wallpaper crop introduced in
// PR #333 — it mirrors the "cover" placement used by shaders/wallpaper.glsl
// ::wallpaperUv so a virtual screen that is a sub-rect of its physical screen
// samples its correct slice instead of the center-cropped whole wallpaper.
//
// The crop math is duplicated between GLSL and C++; this test pins the C++
// half against hand-worked expected values to catch silent drift.
class WallpaperCropTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ── Degenerate inputs ─────────────────────────────────────────────────

    void degenerateWallpaperSize()
    {
        // Empty wallpaper size → invalid crop (caller falls back to full image).
        const QRect phys(0, 0, 1920, 1080);
        const QRect sub(0, 0, 960, 1080);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(QSize(), phys, sub), QRect());
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(QSize(0, 0), phys, sub), QRect());
    }

    void invalidRects()
    {
        const QSize wp(3840, 2160);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, QRect(), QRect(0, 0, 960, 1080)), QRect());
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, QRect(0, 0, 1920, 1080), QRect()), QRect());
    }

    void subEqualsPhys()
    {
        // subGeom == physGeom → caller should use full wallpaper, not a crop.
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 1920, 1080);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, phys, phys), QRect());
    }

    void subOutsidePhys()
    {
        // Sub fully outside phys → intersection invalid → full image used.
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 1920, 1080);
        const QRect sub(5000, 5000, 100, 100);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, phys, sub), QRect());
    }

    void subClampedCoversPhys()
    {
        // Sub fully contains phys (clamped intersection == phys) → no crop.
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 1920, 1080);
        const QRect sub(-100, -100, 2200, 1300);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, phys, sub), QRect());
    }

    // ── Same-aspect wallpaper and screen (no cover letterbox) ─────────────

    void sameAspect_leftHalf()
    {
        // 3840x2160 (16:9) wallpaper, 1920x1080 (16:9) screen, left-half VS.
        // Cover rect is the whole wallpaper. Left half maps to [0..1920, 0..2160].
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 1920, 1080);
        const QRect sub(0, 0, 960, 1080);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, phys, sub), QRect(0, 0, 1920, 2160));
    }

    void sameAspect_rightHalf()
    {
        // Right-half VS on a 16:9 screen + 16:9 wallpaper.
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 1920, 1080);
        const QRect sub(960, 0, 960, 1080);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, phys, sub), QRect(1920, 0, 1920, 2160));
    }

    // ── Wide wallpaper, narrow screen (horizontal letterbox of wallpaper) ─

    void wallpaperWiderThanScreen_rightVS()
    {
        // 3840x2160 (16:9, 1.777) wallpaper, 5120x1440 (ultrawide 3.555) screen.
        // wpAspect < physAspect → vertical-strip branch.
        // coverW = wpW = 3840, coverH = wpW / physAspect = 3840 / (5120/1440)
        //        = 3840 * 1440 / 5120 = 1080.
        // coverY = (2160 - 1080)/2 = 540.
        // Right VS: fracX=0.5 → left = 1920, right = 3840. width = 1920.
        //           fracY=0, fracB=1 → top = 540, bottom = 1620. height = 1080.
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 5120, 1440);
        const QRect subRight(2560, 0, 2560, 1440);
        const QRect expected(1920, 540, 1920, 1080);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, phys, subRight), expected);
    }

    void wallpaperWiderThanScreen_leftVS()
    {
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 5120, 1440);
        const QRect subLeft(0, 0, 2560, 1440);
        const QRect expected(0, 540, 1920, 1080);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, phys, subLeft), expected);
    }

    // ── Tall wallpaper, wide screen (vertical letterbox of wallpaper) ─────

    void wallpaperTallerThanScreen_rightVS()
    {
        // 2000x2000 (1:1, square) wallpaper, 4000x1000 (4:1) physical screen.
        // wpAspect = 1.0 < physAspect = 4.0 → coverW = 2000, coverH = 2000/4 = 500.
        // coverY = (2000 - 500)/2 = 750.
        // Right VS at [2000, 0, 2000, 1000]: fracX=0.5, fracR=1.0
        //   → left=1000, right=2000, width=1000.
        //   fracY=0, fracB=1 → top=750, bottom=1250, height=500.
        const QSize wp(2000, 2000);
        const QRect phys(0, 0, 4000, 1000);
        const QRect subRight(2000, 0, 2000, 1000);
        const QRect expected(1000, 750, 1000, 500);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, phys, subRight), expected);
    }

    // ── Non-(0,0) physical origin (multi-monitor compositor coords) ───────

    void physOffsetFromOrigin()
    {
        // Second monitor starts at (1920, 0). VS is its right half.
        // physGeom = (1920, 0, 1920, 1080), subGeom = (2880, 0, 960, 1080).
        // Same aspect (16:9 wallpaper): cover rect is full wallpaper.
        // fracX = (2880-1920)/1920 = 0.5 → left = 1920. right = 3840. width=1920.
        const QSize wp(3840, 2160);
        const QRect phys(1920, 0, 1920, 1080);
        const QRect sub(2880, 0, 960, 1080);
        QCOMPARE(ShaderRegistry::computeWallpaperCropRect(wp, phys, sub), QRect(1920, 0, 1920, 2160));
    }

    // ── Tiling/seam guarantee: adjacent VSes must abut exactly ────────────

    void adjacentVSesTileSeamlessly_evenSplit()
    {
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 1920, 1080);
        const QRect left(0, 0, 960, 1080);
        const QRect right(960, 0, 960, 1080);
        const QRect cL = ShaderRegistry::computeWallpaperCropRect(wp, phys, left);
        const QRect cR = ShaderRegistry::computeWallpaperCropRect(wp, phys, right);
        QVERIFY(cL.isValid());
        QVERIFY(cR.isValid());
        // Left crop's right edge must equal right crop's left edge — no gap,
        // no overlap — so the wallpaper tiles across the boundary.
        QCOMPARE(cL.x() + cL.width(), cR.x());
        // And together they span the full cover rect (here: whole wallpaper).
        QCOMPARE(cL.x(), 0);
        QCOMPARE(cR.x() + cR.width(), wp.width());
    }

    void adjacentVSesTileSeamlessly_oddSplit()
    {
        // Asymmetric split that exercises rounding: 1920 split at x=641.
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 1920, 1080);
        const QRect left(0, 0, 641, 1080);
        const QRect right(641, 0, 1279, 1080);
        const QRect cL = ShaderRegistry::computeWallpaperCropRect(wp, phys, left);
        const QRect cR = ShaderRegistry::computeWallpaperCropRect(wp, phys, right);
        QVERIFY(cL.isValid());
        QVERIFY(cR.isValid());
        QCOMPARE(cL.x() + cL.width(), cR.x());
    }

    void adjacentVSesTileSeamlessly_threeWaySplit()
    {
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 1920, 1080);
        const QRect a(0, 0, 640, 1080);
        const QRect b(640, 0, 640, 1080);
        const QRect c(1280, 0, 640, 1080);
        const QRect cA = ShaderRegistry::computeWallpaperCropRect(wp, phys, a);
        const QRect cB = ShaderRegistry::computeWallpaperCropRect(wp, phys, b);
        const QRect cC = ShaderRegistry::computeWallpaperCropRect(wp, phys, c);
        QVERIFY(cA.isValid() && cB.isValid() && cC.isValid());
        QCOMPARE(cA.x() + cA.width(), cB.x());
        QCOMPARE(cB.x() + cB.width(), cC.x());
    }

    // ── Cropped aspect matches VS aspect (invariant the shader relies on) ─

    void croppedAspectMatchesSubAspect()
    {
        // The in-shader aspect correction becomes a no-op only when the
        // cropped image's aspect equals the sub rect's aspect. Verify across
        // a spread of configurations (± a couple of pixels for rounding).
        struct Case
        {
            QSize wp;
            QRect phys;
            QRect sub;
        };
        const Case cases[] = {
            {{3840, 2160}, {0, 0, 5120, 1440}, {0, 0, 2560, 1440}},
            {{3840, 2160}, {0, 0, 5120, 1440}, {2560, 0, 2560, 1440}},
            {{2000, 2000}, {0, 0, 4000, 1000}, {0, 0, 1000, 1000}},
            {{3840, 2160}, {0, 0, 1920, 1080}, {100, 100, 800, 600}},
        };
        for (const Case& c : cases) {
            const QRect crop = ShaderRegistry::computeWallpaperCropRect(c.wp, c.phys, c.sub);
            QVERIFY2(crop.isValid(),
                     qPrintable(QStringLiteral("invalid crop for sub=%1").arg(QDebug::toString(c.sub))));
            const double cropAspect = static_cast<double>(crop.width()) / crop.height();
            const double subAspect = static_cast<double>(c.sub.width()) / c.sub.height();
            // Allow small slack for integer rounding on the crop rect.
            QVERIFY2(std::abs(cropAspect - subAspect) < 0.01,
                     qPrintable(QStringLiteral("aspect mismatch: crop=%1 sub=%2").arg(cropAspect).arg(subAspect)));
        }
    }

    // ── Crop stays inside the wallpaper (no out-of-bounds sampling) ───────

    void cropStaysInsideWallpaper()
    {
        const QSize wp(3840, 2160);
        const QRect phys(0, 0, 5120, 1440);
        // Sweep sub positions covering every corner and the middle.
        const QVector<QRect> subs = {
            QRect(0, 0, 2560, 1440), QRect(2560, 0, 2560, 1440), QRect(1000, 0, 2000, 1440),
            QRect(0, 0, 500, 1440),  QRect(4620, 0, 500, 1440),
        };
        const QRect wpBounds(QPoint(0, 0), wp);
        for (const QRect& sub : subs) {
            const QRect crop = ShaderRegistry::computeWallpaperCropRect(wp, phys, sub);
            QVERIFY(crop.isValid());
            QVERIFY(wpBounds.contains(crop));
        }
    }
};

QTEST_MAIN(WallpaperCropTest)
#include "test_wallpaper_crop.moc"
