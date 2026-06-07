// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_screeninfo_variant.cpp
 * @brief Coverage for @c screenInfoListToVariantList — focuses on the
 *        screen-space position (x/y) keys added for the proportional
 *        multi-monitor map.
 *
 * The contract that distinguishes x/y from width/height: dimensions are
 * skipped when non-positive (a 0×0 tile must not render), but position is
 * always emitted because 0,0 is a legitimate origin and negative
 * coordinates are normal for outputs placed left of / above the primary.
 */

#include <PhosphorScreens/ScreenInfo.h>

#include <QTest>
#include <QVariantMap>

using namespace PhosphorScreens;

class TestScreenInfoVariant : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void positionAlwaysEmitted_includingOrigin()
    {
        // Primary at the coordinate origin: x==y==0 must still be present.
        // Were x/y subject to the >0 skip used for dimensions, a monitor
        // at the origin would lose its position and collapse the map.
        ScreenInfo s;
        s.name = QStringLiteral("DP-1");
        s.width = 3200;
        s.height = 1800;
        s.x = 0;
        s.y = 0;

        const QVariantList out = screenInfoListToVariantList({s});
        QCOMPARE(out.size(), 1);
        const QVariantMap m = out.first().toMap();
        QVERIFY2(m.contains(QStringLiteral("x")), "x key must always be present");
        QVERIFY2(m.contains(QStringLiteral("y")), "y key must always be present");
        QCOMPARE(m.value(QStringLiteral("x")).toInt(), 0);
        QCOMPARE(m.value(QStringLiteral("y")).toInt(), 0);
    }

    void positionRoundTrips_includingNegative()
    {
        // A second output placed to the LEFT of the primary has a negative
        // x; an output stacked ABOVE has a negative y. Both are routine and
        // must survive serialization verbatim.
        ScreenInfo left;
        left.name = QStringLiteral("DP-2");
        left.width = 2560;
        left.height = 1440;
        left.x = -2560;
        left.y = 360;

        const QVariantMap m = screenInfoListToVariantList({left}).first().toMap();
        QCOMPARE(m.value(QStringLiteral("x")).toInt(), -2560);
        QCOMPARE(m.value(QStringLiteral("y")).toInt(), 360);

        // An output stacked ABOVE the primary has a negative y with a
        // positive x — exercise the y path independently of the x path.
        ScreenInfo above;
        above.name = QStringLiteral("DP-3");
        above.width = 1920;
        above.height = 1080;
        above.x = 640;
        above.y = -1080;

        const QVariantMap a = screenInfoListToVariantList({above}).first().toMap();
        QCOMPARE(a.value(QStringLiteral("x")).toInt(), 640);
        QCOMPARE(a.value(QStringLiteral("y")).toInt(), -1080);
    }

    void dimensionsStillSkippedWhenNonPositive_butPositionRemains()
    {
        // Unknown geometry (0×0) drops width/height/resolution per the
        // existing contract — but x/y stay, so the map can at least know
        // where the tile belongs even if it can't size it.
        ScreenInfo s;
        s.name = QStringLiteral("HDMI-A-1");
        s.width = 0;
        s.height = 0;
        s.x = 100;
        s.y = 200;

        const QVariantMap m = screenInfoListToVariantList({s}).first().toMap();
        QVERIFY2(!m.contains(QStringLiteral("width")), "width must be skipped when non-positive");
        QVERIFY2(!m.contains(QStringLiteral("height")), "height must be skipped when non-positive");
        QVERIFY2(!m.contains(QStringLiteral("resolution")), "resolution needs both dimensions");
        QCOMPARE(m.value(QStringLiteral("x")).toInt(), 100);
        QCOMPARE(m.value(QStringLiteral("y")).toInt(), 200);
    }

    void multiMonitorArrangement_preservesPerScreenPositions()
    {
        // Two side-by-side outputs: the map relies on each tile carrying
        // its own x so DP-2 lands to the right of DP-1.
        ScreenInfo a;
        a.name = QStringLiteral("DP-1");
        a.width = 3200;
        a.height = 1800;
        a.x = 0;
        a.y = 0;
        ScreenInfo b;
        b.name = QStringLiteral("DP-2");
        b.width = 3200;
        b.height = 1800;
        b.x = 3200;
        b.y = 0;

        const QVariantList out = screenInfoListToVariantList({a, b});
        QCOMPARE(out.size(), 2);
        QCOMPARE(out.at(0).toMap().value(QStringLiteral("x")).toInt(), 0);
        QCOMPARE(out.at(1).toMap().value(QStringLiteral("x")).toInt(), 3200);
    }
};

QTEST_MAIN(TestScreenInfoVariant)
#include "test_screeninfo_variant.moc"
