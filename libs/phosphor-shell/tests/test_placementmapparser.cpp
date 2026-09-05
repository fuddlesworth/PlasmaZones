// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// PlacementMapParser: the bus-free half of the bar's placement map. Every
// case feeds the parser the JSON / struct shapes the daemon actually
// serialises and checks the cells the map would draw.

#include <PhosphorShell/PlacementMap.h>
#include <PhosphorShell/PlacementMapParser.h>

#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorShell;
using namespace PhosphorShell::PlacementMapParser;

class TestPlacementMapParser : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void snappingLayoutParsesRelativeZones()
    {
        const QString json = QStringLiteral(R"({"zones":[
            {"id":"a","zoneNumber":1,"name":"Left","relativeGeometry":{"x":0,"y":0,"width":0.5,"height":1}},
            {"id":"b","zoneNumber":2,"relativeGeometry":{"x":0.5,"y":0,"width":0.5,"height":0.5}},
            {"id":"c","zoneNumber":3,"relativeGeometry":{"x":0.5,"y":0.5,"width":0.5,"height":0.5}}
        ]})");
        const auto cells = parseSnappingLayout(json, QSize(1920, 1052));
        QCOMPARE(cells.size(), 3);
        QCOMPARE(cells[0].id, QStringLiteral("a"));
        QCOMPARE(cells[0].zoneNumber, 1);
        QCOMPARE(cells[0].label, QStringLiteral("Left"));
        QCOMPARE(cells[0].rect, QRectF(0, 0, 0.5, 1));
        // Hue is the rail axis: the cell's horizontal centre.
        QCOMPARE(cells[0].t, 0.25);
        QCOMPARE(cells[1].t, 0.75);
        QVERIFY(!cells[0].occupied);
    }

    void snappingLayoutScalesFixedZonesAndDropsUnscalable()
    {
        const QString json = QStringLiteral(R"({"zones":[
            {"id":"f","zoneNumber":1,"geometryMode":1,"fixedGeometry":{"x":0,"y":0,"width":960,"height":526},
             "relativeGeometry":{"x":0,"y":0,"width":0.1,"height":0.1}}
        ]})");
        const auto scaled = parseSnappingLayout(json, QSize(1920, 1052));
        QCOMPARE(scaled.size(), 1);
        QCOMPARE(scaled[0].rect, QRectF(0, 0, 0.5, 0.5));
        // No work area, no scale: the fixed zone cannot be placed.
        QCOMPARE(parseSnappingLayout(json, QSize()).size(), 0);
    }

    void snappingLayoutRejectsGarbage()
    {
        QCOMPARE(parseSnappingLayout(QStringLiteral("not json"), QSize(10, 10)).size(), 0);
        QCOMPARE(parseSnappingLayout(
                     QStringLiteral(R"({"zones":[{"id":"","relativeGeometry":{"x":0,"y":0,"width":1,"height":1}}]})"),
                     QSize(10, 10))
                     .size(),
                 0);
        QCOMPARE(parseSnappingLayout(
                     QStringLiteral(R"({"zones":[{"id":"z","relativeGeometry":{"x":0,"y":0,"width":0,"height":1}}]})"),
                     QSize(10, 10))
                     .size(),
                 0);
    }

    void tileBatchNormalisesAgainstTheWorkAreaAndFiltersScreens()
    {
        const QRect work(0, 28, 1000, 500);
        QList<TileRect> tiles;
        tiles.append(TileRect{QStringLiteral("w1"), QStringLiteral("S"), QRect(0, 28, 500, 500), false, false});
        tiles.append(TileRect{QStringLiteral("w2"), QStringLiteral("S"), QRect(500, 28, 500, 250), false, false});
        tiles.append(TileRect{QStringLiteral("other"), QStringLiteral("T"), QRect(0, 0, 10, 10), false, false});
        tiles.append(TileRect{QStringLiteral("float"), QStringLiteral("S"), QRect(0, 0, 10, 10), true, false});
        const auto cells = parseTileBatch(tiles, QStringLiteral("S"), work);
        QCOMPARE(cells.size(), 2);
        QCOMPARE(cells[0].id, QStringLiteral("w1"));
        QCOMPARE(cells[0].rect, QRectF(0, 0, 0.5, 1));
        QVERIFY(cells[0].occupied);
        QCOMPARE(cells[1].rect, QRectF(0.5, 0, 0.5, 0.5));
        QCOMPARE(cells[1].t, 0.75);
    }

    void tileBatchCollapsesMonocleIntoOneStackedCell()
    {
        const QRect work(0, 0, 100, 100);
        QList<TileRect> tiles;
        tiles.append(TileRect{QStringLiteral("a"), QStringLiteral("S"), QRect(0, 0, 100, 100), false, true});
        tiles.append(TileRect{QStringLiteral("b"), QStringLiteral("S"), QRect(0, 0, 100, 100), false, true});
        tiles.append(TileRect{QStringLiteral("c"), QStringLiteral("S"), QRect(0, 0, 100, 100), false, true});
        const auto cells = parseTileBatch(tiles, QStringLiteral("S"), work);
        QCOMPARE(cells.size(), 1);
        QCOMPARE(cells[0].stack, 3);
    }

    void visibleStripParsesColumnsAndFallsBackToAFullLens()
    {
        const QString json = QStringLiteral(R"([
            {"x":0,"y":0,"width":0.4,"height":1,"zoneNumber":1},
            {"x":0.4,"y":0,"width":0.6,"height":1,"zoneNumber":2,"tabCount":3}
        ])");
        const StripParse parse = parseVisibleStrip(json);
        QCOMPARE(parse.cells.size(), 2);
        QCOMPARE(parse.cells[0].id, QStringLiteral("strip:1"));
        QCOMPARE(parse.cells[1].stack, 3);
        QVERIFY(parse.cells[1].occupied);
        // Phase 1: the lens is the whole visible cut.
        QCOMPARE(parse.lens, QRectF(0, 0, 1, 1));
        QCOMPARE(parse.overflowLeft, 0);
        QCOMPARE(lensToVariant(parse.lens).value(QStringLiteral("w")).toDouble(), 1.0);
        QVERIFY(lensToVariant(QRectF()).isEmpty());
    }

    // Ten 100 px columns on a 1000 px strip, a 300 px viewport panned to
    // 300: columns 3, 4 and 5 are on screen, so 3 sit before the lens and 4
    // after it, and the lens covers 30 % of the strip starting at 30 %.
    static QString tenColumnStrip(int viewOffsetPx, int activeColumn)
    {
        QString columns;
        for (int i = 0; i < 10; ++i) {
            columns +=
                QStringLiteral(R"(%1{"index":%2,"stripPosPx":%3,"extentPx":100,"display":0,"activeTile":0,)"
                               R"("maximized":false,"tiles":[{"windowId":"w%2","crossPx":500,"minimized":false}]})")
                    .arg(i == 0 ? QString() : QStringLiteral(","))
                    .arg(i)
                    .arg(i * 100);
        }
        return QStringLiteral(R"({"axis":0,"viewOffsetPx":%1,"viewportPx":300,"stripExtentPx":1000,)"
                              R"("activeColumn":%2,"columns":[%3]})")
            .arg(viewOffsetPx)
            .arg(activeColumn)
            .arg(columns);
    }

    void stripModelGivesLensOverflowAndStructureAxis()
    {
        const StripParse parse = parseStripModel(tenColumnStrip(300, 4));
        QCOMPARE(parse.viewOffsetPx, 300);
        QCOMPARE(parse.viewportPx, 300);
        QCOMPARE(parse.stripExtentPx, 1000);
        QCOMPARE(parse.overflowLeft, 3);
        QCOMPARE(parse.overflowRight, 4);
        QCOMPARE(parse.lens, QRectF(0.3, 0, 0.3, 1));
        QCOMPARE(parse.cells.size(), 3);
        // Cells are keyed by the column's first tile and placed relative to
        // the viewport: column 3 starts at the lens's left edge.
        QCOMPARE(parse.cells[0].id, QStringLiteral("w3"));
        QCOMPARE(parse.cells[0].columnIndex, 3);
        QCOMPARE(parse.cells[0].rect.x(), 0.0);
        QCOMPARE(parse.cells[0].rect.width(), 1.0 / 3.0);
        QCOMPARE(parse.cells[2].id, QStringLiteral("w5"));
        QCOMPARE(parse.cells[2].rect.x(), 2.0 / 3.0);
        // Hue is the STRIP position, not the visible cut: column 3 starts
        // 30 % along the strip.
        QCOMPARE(parse.cells[0].stripT, 0.3);
        QCOMPARE(parse.cells[0].t, 0.3);
        QCOMPARE(parse.cells[2].stripT, 0.5);
        // activeColumn marks the focused cell; every column is occupied.
        QVERIFY(!parse.cells[0].focused);
        QVERIFY(parse.cells[1].focused);
        QVERIFY(parse.cells[1].occupied);
        QCOMPARE(parse.cells[1].stack, 1);
    }

    void stripModelCutsStraddlersAndFitsWholeStrip()
    {
        // Panned to 250: column 2 (200..300) straddles the left edge and is
        // cut to the lens; column 5 (500..600) straddles the right edge.
        const StripParse straddle = parseStripModel(tenColumnStrip(250, 0));
        QCOMPARE(straddle.overflowLeft, 2);
        QCOMPARE(straddle.overflowRight, 4);
        QCOMPARE(straddle.cells.size(), 4);
        QCOMPARE(straddle.cells[0].id, QStringLiteral("w2"));
        QCOMPARE(straddle.cells[0].rect, QRectF(0, 0, 1.0 / 6.0, 1));
        QCOMPARE(straddle.cells[3].id, QStringLiteral("w5"));
        QCOMPARE(straddle.cells[3].rect.x(), 5.0 / 6.0);
        QCOMPARE(straddle.cells[3].rect.right(), 1.0);

        // A strip shorter than the viewport: the lens is the whole band and
        // nothing overflows.
        const QString fits = QStringLiteral(
            R"({"axis":0,"viewOffsetPx":0,"viewportPx":1000,"stripExtentPx":400,"activeColumn":0,"columns":[)"
            R"({"index":0,"stripPosPx":0,"extentPx":200,"display":1,"activeTile":1,"maximized":false,)"
            R"("tiles":[{"windowId":"a","crossPx":1,"minimized":false},{"windowId":"b","crossPx":1,"minimized":false},)"
            R"({"windowId":"m","crossPx":0,"minimized":true}]},)"
            R"({"index":1,"stripPosPx":200,"extentPx":200,"display":0,"activeTile":0,"maximized":false,)"
            R"("tiles":[{"windowId":"c","crossPx":1,"minimized":false}]},)"
            R"({"index":2,"stripPosPx":400,"extentPx":0,"display":0,"activeTile":0,"maximized":false,)"
            R"("tiles":[{"windowId":"gone","crossPx":0,"minimized":true}]}]})");
        const StripParse whole = parseStripModel(fits);
        QCOMPARE(whole.lens, QRectF(0, 0, 1, 1));
        QCOMPARE(whole.overflowLeft, 0);
        QCOMPARE(whole.overflowRight, 0);
        // A fully minimized column takes no strip position and no cell.
        QCOMPARE(whole.cells.size(), 2);
        // A tabbed column is one cell keyed by its first tile, stacked by
        // its SHOWN tiles: the minimized tab is not drawn.
        QCOMPARE(whole.cells[0].id, QStringLiteral("a"));
        QCOMPARE(whole.cells[0].stack, 2);
        QCOMPARE(whole.cells[0].rect, QRectF(0, 0, 0.2, 1));
        QCOMPARE(whole.cells[1].stripT, 0.5);
    }

    void stripModelVerticalAxisAndGarbage()
    {
        const QString vertical = QStringLiteral(
            R"({"axis":1,"viewOffsetPx":0,"viewportPx":100,"stripExtentPx":200,"activeColumn":0,"columns":[)"
            R"({"index":0,"stripPosPx":0,"extentPx":50,"display":0,"activeTile":0,"maximized":false,)"
            R"("tiles":[{"windowId":"v","crossPx":1,"minimized":false}]}]})");
        const StripParse parse = parseStripModel(vertical);
        QCOMPARE(parse.cells.size(), 1);
        QCOMPARE(parse.cells[0].rect, QRectF(0, 0, 1, 0.5));
        // The daemon's answer for a screen that is not scrolling.
        QVERIFY(parseStripModel(QStringLiteral("{}")).cells.isEmpty());
        QVERIFY(parseStripModel(QStringLiteral("{}")).lens.isNull());
        QVERIFY(parseStripModel(QStringLiteral("junk")).cells.isEmpty());
        // No viewport: nothing to place against.
        QVERIFY(parseStripModel(QStringLiteral(R"({"viewportPx":0,"stripExtentPx":10,"columns":[]})")).cells.isEmpty());
    }

    void currentTilesJsonParsesLikeABatch()
    {
        const QString json = QStringLiteral(R"([
            {"windowId":"w1","x":0,"y":28,"width":500,"height":500,"screenId":"S","monocle":false,"floating":false,"zoneId":""},
            {"windowId":"w2","x":500,"y":28,"width":500,"height":250,"screenId":"S","monocle":false,"floating":false,"zoneId":""},
            {"windowId":"f","x":0,"y":0,"width":10,"height":10,"screenId":"S","monocle":false,"floating":true,"zoneId":""},
            {"windowId":"","x":0,"y":0,"width":10,"height":10,"screenId":"S","monocle":false,"floating":false,"zoneId":""}
        ])");
        const auto tiles = tileRectsFromJson(json);
        QCOMPARE(tiles.size(), 3);
        QCOMPARE(tiles[0].rect, QRect(0, 28, 500, 500));
        QVERIFY(tiles[2].floating);
        const auto cells = parseCurrentTiles(json, QStringLiteral("S"), QRect(0, 28, 1000, 500));
        QCOMPARE(cells.size(), 2);
        QCOMPARE(cells[0].rect, QRectF(0, 0, 0.5, 1));
        QCOMPARE(cells[1].rect, QRectF(0.5, 0, 0.5, 0.5));
        QVERIFY(tileRectsFromJson(QStringLiteral("[]")).isEmpty());
        QVERIFY(tileRectsFromJson(QStringLiteral("nope")).isEmpty());
    }

    void occupancyMarksZonesAndFocus()
    {
        QList<Cell> cells;
        for (const char* id : {"a", "b", "c"}) {
            Cell c;
            c.id = QLatin1String(id);
            cells.append(c);
        }
        QHash<QString, QStringList> occupancy;
        occupancy.insert(QStringLiteral("w1"), {QStringLiteral("a")});
        // A multi-zone span fills every zone it lists.
        occupancy.insert(QStringLiteral("w2"), {QStringLiteral("b"), QStringLiteral("c")});
        applyOccupancy(cells, occupancy, QStringLiteral("w2"));
        QVERIFY(cells[0].occupied && !cells[0].focused);
        QVERIFY(cells[1].occupied && cells[1].focused);
        QVERIFY(cells[2].occupied && cells[2].focused);

        applyFocusByWindowId(cells, QStringLiteral("a"));
        QVERIFY(cells[0].focused && !cells[1].focused);
    }

    void variantListCarriesTheDocumentedKeys()
    {
        Cell c;
        c.id = QStringLiteral("z");
        c.rect = QRectF(0.1, 0.2, 0.3, 0.4);
        c.t = 0.25;
        c.occupied = true;
        const QVariantList list = toVariantList({c});
        QCOMPARE(list.size(), 1);
        const QVariantMap m = list[0].toMap();
        for (const char* key : {"id", "x", "y", "w", "h", "t", "occupied", "focused", "label", "zoneNumber", "stack",
                                "stripT", "columnIndex"}) {
            QVERIFY2(m.contains(QLatin1String(key)), key);
        }
        QCOMPARE(m.value(QStringLiteral("w")).toDouble(), 0.3);
        QCOMPARE(m.value(QStringLiteral("columnIndex")).toInt(), -1);
    }

    // The screen-pixel helpers a surface uses to sit on a cell (A3 §3–§4):
    // no daemon, so no work area, so no rect and no focused cell.
    void cellRectAndFocusedCellIdAreEmptyWithoutADaemon()
    {
        PlacementMap map;
        PlacementMapScreen* s = map.forScreen(QStringLiteral("DP-1"));
        QVERIFY(s->cellRect(QStringLiteral("anything")).isNull());
        QVERIFY(s->focusedCellId().isEmpty());
        QVERIFY(s->workArea().isNull());
        QCOMPARE(s->stripExtentPx(), 0);
    }

    void modeForScreenReadsTheStatesArray()
    {
        const QString json = QStringLiteral(R"([{"screenId":"A","mode":2},{"screenId":"B","mode":0}])");
        QCOMPARE(modeForScreen(json, QStringLiteral("A")), 2);
        QCOMPARE(modeForScreen(json, QStringLiteral("B")), 0);
        QCOMPARE(modeForScreen(json, QStringLiteral("C")), -1);
        QCOMPARE(modeForScreen(QStringLiteral("[{\"screenId\":\"A\",\"mode\":9}]"), QStringLiteral("A")), -1);
        QCOMPARE(modeForScreen(QStringLiteral("junk"), QStringLiteral("A")), -1);
    }

    // The ownership trap: a Q_INVOKABLE returning a parentless QObject is
    // collected by the QML engine. forScreen() must hand back the same,
    // parented, C++-owned object every time.
    void forScreenVendsOneParentedScreenPerName()
    {
        PlacementMap map;
        PlacementMapScreen* a = map.forScreen(QStringLiteral("DP-1"));
        QVERIFY(a);
        QCOMPARE(a->parent(), &map);
        QCOMPARE(QQmlEngine::objectOwnership(a), QQmlEngine::CppOwnership);
        QCOMPARE(map.forScreen(QStringLiteral("DP-1")), a);
        QVERIFY(map.forScreen(QStringLiteral("DP-2")) != a);
        QCOMPARE(a->screenName(), QStringLiteral("DP-1"));
        // No daemon on this bus: the screen reads as none, with no cells.
        QCOMPARE(a->mode(), -1);
        QVERIFY(a->cells().isEmpty());
    }
};

QTEST_MAIN(TestPlacementMapParser)
#include "test_placementmapparser.moc"
