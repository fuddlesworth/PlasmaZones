// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_screeninfo_variant.cpp
 * @brief Coverage for @c screenInfoListToVariantList: the screen-space
 *        position (x/y) keys added for the proportional multi-monitor map,
 *        the always-present @c isVirtualScreen flag, and the precomputed
 *        @c displayLabel (physical, virtual-fallback, and monitor-name forms).
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

    void isVirtualScreenFlag_alwaysPresentEvenForPhysical()
    {
        // The flag is unconditionally emitted so QML can test `map.isVirtualScreen`
        // directly instead of guarding against an absent (undefined) key on
        // physical screens.
        ScreenInfo s;
        s.name = QStringLiteral("DP-1");
        s.isVirtualScreen = false;

        const QVariantMap m = screenInfoListToVariantList({s}).first().toMap();
        QVERIFY2(m.contains(QStringLiteral("isVirtualScreen")), "isVirtualScreen key must always be present");
        QCOMPARE(m.value(QStringLiteral("isVirtualScreen")).toBool(), false);
    }

    void displayLabel_physicalScreen_vendorModelResolution()
    {
        // Physical output: the label leads with vendor + model and appends the
        // resolution. This is the single source of truth QML selectors render.
        ScreenInfo s;
        s.name = QStringLiteral("DP-1");
        s.manufacturer = QStringLiteral("Dell");
        s.model = QStringLiteral("U2720Q");
        s.width = 3840;
        s.height = 2160;

        const QVariantMap m = screenInfoListToVariantList({s}).first().toMap();
        QCOMPARE(m.value(QStringLiteral("displayLabel")).toString(), QStringLiteral("Dell U2720Q (3840×2160)"));
    }

    void displayLabel_physicalScreen_fallsBackToNameWhenNoVendor()
    {
        // No vendor/model and no geometry: the label degrades to the raw name.
        ScreenInfo s;
        s.name = QStringLiteral("HDMI-A-1");

        const QVariantMap m = screenInfoListToVariantList({s}).first().toMap();
        QCOMPARE(m.value(QStringLiteral("displayLabel")).toString(), QStringLiteral("HDMI-A-1"));
    }

    void displayLabel_virtualScreen_vsFallbackAndIndexOffset()
    {
        // Virtual screen with no friendly name: the label uses the "VS%1"
        // form with the 1-based index (virtualIndex 0 → "VS1"). With no vendor
        // parts and no connector, the monitor suffix is dropped entirely.
        ScreenInfo s;
        s.name = QStringLiteral("DP-1/vs:0");
        s.isVirtualScreen = true;
        s.virtualIndex = 0;

        const QVariantMap m = screenInfoListToVariantList({s}).first().toMap();
        QCOMPARE(m.value(QStringLiteral("displayLabel")).toString(), QStringLiteral("VS1"));
    }

    void displayLabel_virtualScreen_appendsMonitorNameWhenKnown()
    {
        // A virtual screen that knows its physical parent's vendor/model joins
        // the "VS%1" prefix to the monitor name with an em-dash separator.
        ScreenInfo s;
        s.name = QStringLiteral("DP-1/vs:1");
        s.isVirtualScreen = true;
        s.virtualIndex = 1;
        s.manufacturer = QStringLiteral("LG");
        s.model = QStringLiteral("27GP950");

        const QVariantMap m = screenInfoListToVariantList({s}).first().toMap();
        QCOMPARE(m.value(QStringLiteral("displayLabel")).toString(), QStringLiteral("VS2 — LG 27GP950"));
    }

    void displayLabel_virtualScreen_prefersVirtualDisplayName()
    {
        // A friendly virtualDisplayName takes precedence over the "VS%1"
        // fallback. With no vendor/model/connector, the label is just the name.
        ScreenInfo s;
        s.name = QStringLiteral("DP-1/vs:0");
        s.isVirtualScreen = true;
        s.virtualIndex = 0;
        s.virtualDisplayName = QStringLiteral("Workspace 2");

        const QVariantMap m = screenInfoListToVariantList({s}).first().toMap();
        QCOMPARE(m.value(QStringLiteral("displayLabel")).toString(), QStringLiteral("Workspace 2"));
        // The friendly name is also emitted as its own key when set.
        QCOMPARE(m.value(QStringLiteral("virtualDisplayName")).toString(), QStringLiteral("Workspace 2"));
    }

    void displayLabel_virtualScreen_connectorNameAsMonitorSuffixWhenNoVendor()
    {
        // No vendor/model but a known connector: the monitor suffix falls back
        // to the connector name rather than being dropped.
        ScreenInfo s;
        s.name = QStringLiteral("DP-3/vs:0");
        s.isVirtualScreen = true;
        s.virtualIndex = 0;
        s.connectorName = QStringLiteral("DP-3");

        const QVariantMap m = screenInfoListToVariantList({s}).first().toMap();
        QCOMPARE(m.value(QStringLiteral("displayLabel")).toString(), QStringLiteral("VS1 — DP-3"));
    }

    void displayLabel_virtualScreen_negativeIndexDegradesToVS0()
    {
        // Contract violation (isVirtualScreen with virtualIndex < 0): the
        // serializer warns and the label degrades to "VS0" via virtualIndex+1.
        // Assert the degraded label rather than crashing or emitting garbage.
        ScreenInfo s;
        s.name = QStringLiteral("DP-9/vs:0");
        s.isVirtualScreen = true;
        s.virtualIndex = -1;

        const QVariantMap m = screenInfoListToVariantList({s}).first().toMap();
        QCOMPARE(m.value(QStringLiteral("displayLabel")).toString(), QStringLiteral("VS0"));
    }

    void connectorName_emittedOnPhysicalScreen()
    {
        // connectorName is emitted whenever it is set, independent of the
        // virtual flag — QML reads it as the connector-first tile label.
        ScreenInfo s;
        s.name = QStringLiteral("DP-5");
        s.connectorName = QStringLiteral("DP-5");

        const QVariantMap m = screenInfoListToVariantList({s}).first().toMap();
        QVERIFY2(m.contains(QStringLiteral("connectorName")), "connectorName must be present when set");
        QCOMPARE(m.value(QStringLiteral("connectorName")).toString(), QStringLiteral("DP-5"));
        // When the monitor name already equals the connector (and there is no
        // vendor/model), the displayLabel must NOT append a redundant
        // " · DP-5" suffix — it stays the bare connector name.
        QCOMPARE(m.value(QStringLiteral("displayLabel")).toString(), QStringLiteral("DP-5"));
    }
};

QTEST_MAIN(TestScreenInfoVariant)
#include "test_screeninfo_variant.moc"
