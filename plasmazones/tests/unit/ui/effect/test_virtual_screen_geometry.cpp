// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_virtual_screen_geometry.cpp
 * @brief The absolute rect of a virtual screen
 *        (plasmazoneseffect/virtualscreengeometry.h): edges rounded and sizes
 *        derived from them, so abutting virtual screens share their edge, and
 *        the same region re-derived on an output that moved follows it (F806).
 *        Also the virtual screen's device-pixel size, the share the
 *        screen-shape rule fields read, and parity with the daemon's
 *        VirtualScreenDef::absoluteGeometry, clamps included.
 */

#include "plasmazoneseffect/virtualscreengeometry.h"

#include <PhosphorScreens/VirtualScreen.h>

#include <QTest>

using PlasmaZones::absoluteVirtualScreenRect;
using PlasmaZones::virtualScreenPixelSize;

class TestVirtualScreenGeometry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // An odd width splits at a half pixel. Rounding each size on its own made
    // the right half end one pixel past the output.
    void halvesAbutWithoutAGap()
    {
        const QRect output(0, 0, 1921, 1080);
        const QRect left = absoluteVirtualScreenRect(QRectF(0.0, 0.0, 0.5, 1.0), output);
        const QRect right = absoluteVirtualScreenRect(QRectF(0.5, 0.0, 0.5, 1.0), output);
        QCOMPARE(left.x() + left.width(), right.x());
        QCOMPARE(right.x() + right.width(), output.x() + output.width());
        QCOMPARE(left.width() + right.width(), output.width());
    }

    // An arrangement change moves the output and keeps the region: the rect
    // moves with it, same size.
    void outputMoveRederivesAbsolute()
    {
        const QRectF region(0.5, 0.0, 0.5, 1.0);
        const QRect before = absoluteVirtualScreenRect(region, QRect(0, 0, 1920, 1080));
        const QRect after = absoluteVirtualScreenRect(region, QRect(2560, 0, 1920, 1080));
        QCOMPARE(before, QRect(960, 0, 960, 1080));
        QCOMPARE(after, before.translated(2560, 0));
    }

    // Thirds of 1000 px round to 333 / 334 / 333 and tile the output exactly.
    void fractionalEdgesRound()
    {
        const QRect output(100, 50, 1000, 600);
        const qreal third = 1.0 / 3.0;
        const QRect a = absoluteVirtualScreenRect(QRectF(0.0, 0.0, third, 1.0), output);
        const QRect b = absoluteVirtualScreenRect(QRectF(third, 0.0, third, 1.0), output);
        const QRect c = absoluteVirtualScreenRect(QRectF(2.0 * third, 0.0, third, 1.0), output);
        QCOMPARE(a, QRect(100, 50, 333, 600));
        QCOMPARE(b, QRect(433, 50, 334, 600));
        QCOMPARE(c, QRect(767, 50, 333, 600));
    }

    // Half of a 32:9 super-ultrawide is a 16:9 screen, and that half is what a
    // window on it matches against, not the whole output.
    void pixelSizeIsTheRegionsShare()
    {
        QCOMPARE(virtualScreenPixelSize(QRectF(0.0, 0.0, 0.5, 1.0), QSize(5120, 1440)), QSize(2560, 1440));
        QCOMPARE(virtualScreenPixelSize(QRectF(0.5, 0.0, 0.5, 1.0), QSize(5120, 1440)), QSize(2560, 1440));
        QCOMPARE(virtualScreenPixelSize(QRectF(0.0, 0.0, 1.0, 1.0), QSize(5120, 1440)), QSize(5120, 1440));
    }

    // An odd pixel width splits at a half pixel. The shares take the rounded
    // edges, so the two halves still add up to the output.
    void pixelSharesTileTheOutput()
    {
        const QSize output(1921, 1080);
        const QSize left = virtualScreenPixelSize(QRectF(0.0, 0.0, 0.5, 1.0), output);
        const QSize right = virtualScreenPixelSize(QRectF(0.5, 0.0, 0.5, 1.0), output);
        QCOMPARE(left, QSize(961, 1080));
        QCOMPARE(right, QSize(960, 1080));
        QCOMPARE(left.width() + right.width(), output.width());
    }

    // The share is taken over the pixel rect, not the logical rect scaled
    // back up. At 150% a 2560x1440 mode is a 1707x960 logical output (KWin
    // rounds the logical size), and half of that scaled by 1.5 would read 1281.
    void pixelShareIsExactUnderFractionalScale()
    {
        QCOMPARE(virtualScreenPixelSize(QRectF(0.0, 0.0, 0.5, 1.0), QSize(2560, 1440)), QSize(1280, 1440));
    }

    // The daemon admits a region whose far edge overshoots the output by its
    // tolerance. The far edge is pulled back onto the output, so the share is
    // the output's half and not two or three pixels wider.
    void overshootingRegionIsClampedToTheOutput()
    {
        const QRectF overshoot(0.5, 0.0, 0.5005, 1.0);
        QCOMPARE(virtualScreenPixelSize(overshoot, QSize(3840, 2160)), QSize(1920, 2160));
        QCOMPARE(virtualScreenPixelSize(overshoot, QSize(5120, 1440)), QSize(2560, 1440));
        QCOMPARE(absoluteVirtualScreenRect(overshoot, QRect(100, 0, 3840, 2160)), QRect(2020, 0, 1920, 2160));
    }

    // A region starting a tolerance before the output's origin starts at the
    // origin.
    void undershootingOriginIsClampedToTheOutput()
    {
        const QRectF undershoot(-0.0005, 0.0, 0.5005, 1.0);
        QCOMPARE(virtualScreenPixelSize(undershoot, QSize(3840, 2160)), QSize(1920, 2160));
        QCOMPARE(absoluteVirtualScreenRect(undershoot, QRect(100, 50, 3840, 2160)), QRect(100, 50, 1920, 2160));
    }

    // A sliver that rounds to nothing keeps one pixel, as the daemon's does,
    // so the effect neither drops a screen the daemon keeps nor reads it as
    // empty.
    void sliverKeepsOnePixel()
    {
        QCOMPARE(virtualScreenPixelSize(QRectF(0.0, 0.0, 0.0001, 1.0), QSize(1920, 1080)), QSize(1, 1080));
    }

    // Every region the daemon's VirtualScreenDef::absoluteGeometry cuts, cut
    // here: the shape fields and the screen a point lands on agree across the
    // two processes, overshoots and slivers included.
    void matchesTheDaemonsCut_data()
    {
        QTest::addColumn<QRectF>("region");
        QTest::addColumn<QRect>("physical");
        const QList<QRect> outputs = {QRect(0, 0, 3840, 2160), QRect(0, 0, 5120, 1440), QRect(0, 0, 1921, 1080),
                                      QRect(2560, 300, 1707, 960), QRect(0, 0, 1440, 2560)};
        const QList<QRectF> regions = {
            QRectF(0.0, 0.0, 0.5, 1.0),        QRectF(0.5, 0.0, 0.5, 1.0),       QRectF(0.5, 0.0, 0.5005, 1.0),
            QRectF(-0.0005, 0.0, 0.5005, 1.0), QRectF(0.0, 0.5, 1.0, 0.5009),    QRectF(1.0 / 3.0, 0.0, 1.0 / 3.0, 1.0),
            QRectF(0.0, 0.0, 0.0001, 1.0),     QRectF(0.9995, 0.0, 0.0010, 1.0), QRectF(0.25, -0.0009, 0.5, 1.0009)};
        for (const QRect& physical : outputs) {
            for (const QRectF& region : regions) {
                QTest::addRow("%dx%d+%d+%d (%g,%g %gx%g)", physical.width(), physical.height(), physical.x(),
                              physical.y(), region.x(), region.y(), region.width(), region.height())
                    << region << physical;
            }
        }
    }

    void matchesTheDaemonsCut()
    {
        QFETCH(QRectF, region);
        QFETCH(QRect, physical);
        PhosphorScreens::VirtualScreenDef def;
        def.region = region;
        const QRect daemon = def.absoluteGeometry(physical);
        QCOMPARE(absoluteVirtualScreenRect(region, physical), daemon);
        const QRect daemonPixels = def.absoluteGeometry(QRect(QPoint(), physical.size()));
        QCOMPARE(virtualScreenPixelSize(region, physical.size()), daemonPixels.size());
    }

    // A rotated output reports its pixel size already rotated, and a split
    // across its height shares that height.
    void pixelShareOfARotatedOutput()
    {
        const QSize portrait(1440, 2560);
        QCOMPARE(virtualScreenPixelSize(QRectF(0.0, 0.0, 1.0, 0.5), portrait), QSize(1440, 1280));
        QCOMPARE(virtualScreenPixelSize(QRectF(0.0, 0.5, 1.0, 0.5), portrait), QSize(1440, 1280));
    }

    // No physical size means no share: the shape stays unknown rather than
    // reading as some size.
    void emptyOutputHasNoShare()
    {
        QVERIFY(virtualScreenPixelSize(QRectF(0.0, 0.0, 0.5, 1.0), QSize(0, 0)).isEmpty());
    }
};

QTEST_MAIN(TestVirtualScreenGeometry)
#include "test_virtual_screen_geometry.moc"
