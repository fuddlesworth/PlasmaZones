// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// PlasmaZones::StripZones::zoneMapsForTiles — the in-process half of the strip-preview
// payload, which the daemon's own OSD strip card renders without crossing the
// bus. Its wire twin (ScrollingAdaptor::visibleStripJson) has its own suite;
// this one exists because the two have to stay identically shaped and only
// one of them was pinned. A divergence here draws a tabbed column as a plain
// window on the OSD while the settings thumbnail draws it correctly, or the
// reverse, with nothing failing.
//
// Pins the tab keys specifically: present as a set for a tile whose column
// draws an indicator, absent as a set for one that does not (checked by key
// presence, not by value — a value compare against a coercion default cannot
// tell an absent key from a zero), and each carrying the engine's own value.

#include "daemon/daemon/stripzones.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScrollEngine/ScrollEngineTypes.h>

#include <QtTest>

using namespace PhosphorScrollEngine;
namespace Keys = PhosphorProtocol::Service::StripPreviewKey;

class TestStripZonesTabKeys : public QObject
{
    Q_OBJECT

private:
    /// One screen-filling tile, with the tab fields left to the caller.
    static ScrollEngine::VisibleTile tile(int zoneNumber, const QRect& rect)
    {
        ScrollEngine::VisibleTile t;
        t.windowId = QStringLiteral("app|a");
        t.columnIndex = 0;
        t.zoneNumber = zoneNumber;
        t.rect = rect;
        return t;
    }

    static QVariantMap zoneAt(const QVariantList& zones, int index)
    {
        return zones.at(index).toMap();
    }

private Q_SLOTS:
    /// The four wire values of TabIndicatorPosition, pinned against the enum.
    ///
    /// This numbering is spelled out in places that never meet: the enum, the
    /// D-Bus XML and StripPreviewKey's doc, and ZonePreview's own copy in QML.
    /// Renumbering the enum ALONE already fails the build, on the static_asserts
    /// in settingsschema_scrolling.cpp that tie it to the ConfigDefaults
    /// accessors. What those cannot see is a COORDINATED renumbering of the enum
    /// and ConfigDefaults together, which they would follow silently while every
    /// preview's indicator moved to the wrong edge. That is the case this covers.
    void wirePositionValuesMatchTheEnum()
    {
        QCOMPARE(static_cast<int>(TabIndicatorPosition::Left), 0);
        QCOMPARE(static_cast<int>(TabIndicatorPosition::Right), 1);
        QCOMPARE(static_cast<int>(TabIndicatorPosition::Top), 2);
        QCOMPARE(static_cast<int>(TabIndicatorPosition::Bottom), 3);
    }

    /// A tabbed column's tile carries all four keys, each with the engine's
    /// value rather than a default that happens to look right.
    void tabbedTileCarriesEveryTabKey()
    {
        ScrollEngine::VisibleTile t = tile(1, QRect(0, 0, 400, 800));
        t.tabCount = 3;
        // Not 0: the first tab is also the absent-key coercion value, so a
        // dropped copy would pass as a correct one.
        t.activeTabIndex = 2;
        // Not Left: Left is 0, which is both the enum's first value and the
        // coercion default, so it cannot discriminate either.
        t.tabIndicatorPosition = TabIndicatorPosition::Bottom;
        t.tabLengthProportion = 0.375;

        const QVariantList zones =
            PlasmaZones::StripZones::zoneMapsForTiles(QStringLiteral("DP-1"), {t}, QRect(0, 0, 800, 800));
        QCOMPARE(zones.size(), 1);
        const QVariantMap zone = zoneAt(zones, 0);

        QVERIFY(zone.contains(Keys::TabCount));
        QVERIFY(zone.contains(Keys::ActiveTab));
        QVERIFY(zone.contains(Keys::TabPosition));
        QVERIFY(zone.contains(Keys::TabLength));

        QCOMPARE(zone.value(Keys::TabCount).toInt(), 3);
        QCOMPARE(zone.value(Keys::ActiveTab).toInt(), 2);
        QCOMPARE(zone.value(Keys::TabPosition).toInt(), static_cast<int>(TabIndicatorPosition::Bottom));
        QCOMPARE(zone.value(Keys::TabLength).toDouble(), 0.375);
    }

    /// The producer clamps an out-of-range index into the pill row rather than
    /// forwarding it. The engine cannot currently produce one — an index only
    /// goes out of range when the column draws no indicator, which zeroes the
    /// count and writes no keys at all — so this pins defensive code that
    /// nothing else would notice the loss of.
    void outOfRangeActiveTabIsClampedIntoThePillRow()
    {
        ScrollEngine::VisibleTile t = tile(1, QRect(0, 0, 400, 800));
        t.tabCount = 3;
        t.activeTabIndex = 9;

        const QVariantList zones =
            PlasmaZones::StripZones::zoneMapsForTiles(QStringLiteral("DP-1"), {t}, QRect(0, 0, 800, 800));
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zoneAt(zones, 0).value(Keys::ActiveTab).toInt(), 2);
    }

    /// A column drawing no indicator carries no tab key AT ALL. Checked by
    /// presence: the renderer's gate is "absent or zero", so writing a zeroed
    /// set would render identically today and quietly break the contract that
    /// an absent key and a daemon predating this payload mean the same thing.
    void untabbedTileCarriesNoTabKeyAtAll()
    {
        const ScrollEngine::VisibleTile t = tile(1, QRect(0, 0, 400, 800));
        QCOMPARE(t.tabCount, 0);

        const QVariantList zones =
            PlasmaZones::StripZones::zoneMapsForTiles(QStringLiteral("DP-1"), {t}, QRect(0, 0, 800, 800));
        QCOMPARE(zones.size(), 1);
        const QVariantMap zone = zoneAt(zones, 0);

        QVERIFY(!zone.contains(Keys::TabCount));
        QVERIFY(!zone.contains(Keys::ActiveTab));
        QVERIFY(!zone.contains(Keys::TabPosition));
        QVERIFY(!zone.contains(Keys::TabLength));
        // The tile itself is still there — this is not passing because the
        // whole payload came back empty.
        QVERIFY(zone.contains(QLatin1String("relativeGeometry")));
        QCOMPARE(zone.value(QLatin1String("zoneNumber")).toInt(), 1);
    }

    /// The synthetic id is keyed on the tile's OWN zone number, which is what
    /// keeps this id space and the settings app's line up entry for entry
    /// when the walk's numbering is not dense.
    void zoneIdFollowsTheTilesNumberRatherThanItsPosition()
    {
        const QVariantList zones = PlasmaZones::StripZones::zoneMapsForTiles(
            QStringLiteral("DP-1"), {tile(4, QRect(0, 0, 400, 800)), tile(7, QRect(400, 0, 400, 800))},
            QRect(0, 0, 800, 800));
        QCOMPARE(zones.size(), 2);
        QCOMPARE(zoneAt(zones, 0).value(QLatin1String("id")).toString(), QStringLiteral("strip:DP-1:4"));
        QCOMPARE(zoneAt(zones, 1).value(QLatin1String("id")).toString(), QStringLiteral("strip:DP-1:7"));
    }

    /// The relative geometry is measured against the screen's own origin and
    /// span. Asserted on a NON-origin, NON-square screen on purpose: at (0,0)
    /// the origin subtraction is a no-op, and on a square screen the two spans
    /// divide identically, so a fixture with either property would let a
    /// dropped origin or a transposed axis pass.
    void relativeGeometryIsMeasuredAgainstTheScreenRect()
    {
        const QRect screen(1920, 0, 1200, 800);
        const QVariantList zones = PlasmaZones::StripZones::zoneMapsForTiles(
            QStringLiteral("DP-2"), {tile(1, QRect(2220, 0, 600, 800))}, screen);
        QCOMPARE(zones.size(), 1);
        const QVariantMap relGeo = zoneAt(zones, 0).value(QLatin1String("relativeGeometry")).toMap();

        // 2220 is 300px into a 1200px-wide screen starting at 1920.
        QCOMPARE(relGeo.value(QLatin1String("x")).toDouble(), 0.25);
        QCOMPARE(relGeo.value(QLatin1String("y")).toDouble(), 0.0);
        QCOMPARE(relGeo.value(QLatin1String("width")).toDouble(), 0.5);
        QCOMPARE(relGeo.value(QLatin1String("height")).toDouble(), 1.0);
    }

    /// A degenerate screen rect yields nothing rather than dividing by zero.
    void emptyScreenGeometryYieldsNoZones()
    {
        const ScrollEngine::VisibleTile t = tile(1, QRect(0, 0, 400, 800));
        QVERIFY(PlasmaZones::StripZones::zoneMapsForTiles(QStringLiteral("DP-1"), {t}, QRect(0, 0, 0, 800)).isEmpty());
        QVERIFY(PlasmaZones::StripZones::zoneMapsForTiles(QStringLiteral("DP-1"), {t}, QRect()).isEmpty());
    }
};

QTEST_MAIN(TestStripZonesTabKeys)
#include "test_stripzones_tabkeys.moc"
