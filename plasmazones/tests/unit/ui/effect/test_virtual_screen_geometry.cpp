// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_virtual_screen_geometry.cpp
 * @brief The absolute rect of a virtual screen
 *        (plasmazoneseffect/virtualscreengeometry.h): edges rounded and sizes
 *        derived from them, so abutting virtual screens share their edge, and
 *        the same region re-derived on an output that moved follows it (F806).
 */

#include "plasmazoneseffect/virtualscreengeometry.h"

#include <QTest>

using PlasmaZones::absoluteVirtualScreenRect;

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
};

QTEST_MAIN(TestVirtualScreenGeometry)
#include "test_virtual_screen_geometry.moc"
