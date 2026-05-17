// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorScrollEngine/Column.h>

#include <QtTest>

using namespace PhosphorScrollEngine;

class TestColumn : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyColumn();
    void appendFocusesFirstTile();
    void insertShiftsActive();
    void removeWindowFixesActive();
    void moveTileFollowsFocus();
    void widthIntent();
    void jsonRoundTrip();
};

void TestColumn::emptyColumn()
{
    Column column;
    QVERIFY(column.isEmpty());
    QCOMPARE(column.tileCount(), 0);
    QCOMPARE(column.activeTileIndex(), -1);
    QVERIFY(column.activeTile() == nullptr);
    QVERIFY(!column.containsWindow(QStringLiteral("w1")));
}

void TestColumn::appendFocusesFirstTile()
{
    Column column;
    column.appendTile(Tile{QStringLiteral("a"), WindowHeight::automatic()});
    QCOMPARE(column.tileCount(), 1);
    QCOMPARE(column.activeTileIndex(), 0); // first append focuses

    column.appendTile(Tile{QStringLiteral("b"), WindowHeight::automatic()});
    QCOMPARE(column.activeTileIndex(), 0); // focus stays on the first tile
    QVERIFY(column.activeTile() != nullptr);
    QCOMPARE(column.activeTile()->windowId, QStringLiteral("a"));
    QCOMPARE(column.windowIds(), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

void TestColumn::insertShiftsActive()
{
    Column column;
    column.appendTile(Tile{QStringLiteral("a"), {}});
    column.appendTile(Tile{QStringLiteral("b"), {}});
    column.setActiveTileIndex(1); // active = b

    column.insertTile(0, Tile{QStringLiteral("x"), {}});
    QCOMPARE(column.windowIds(), (QStringList{QStringLiteral("x"), QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(column.activeTileIndex(), 2); // b shifted right, focus follows
}

void TestColumn::removeWindowFixesActive()
{
    Column column;
    column.appendTile(Tile{QStringLiteral("a"), {}});
    column.appendTile(Tile{QStringLiteral("b"), {}});
    column.appendTile(Tile{QStringLiteral("c"), {}});
    column.setActiveTileIndex(2); // active = c

    QVERIFY(column.removeWindow(QStringLiteral("a")));
    QCOMPARE(column.tileCount(), 2);
    QCOMPARE(column.activeTileIndex(), 1); // c shifted down to index 1
    QCOMPARE(column.activeTile()->windowId, QStringLiteral("c"));
    QVERIFY(!column.removeWindow(QStringLiteral("missing")));
}

void TestColumn::moveTileFollowsFocus()
{
    Column column;
    column.appendTile(Tile{QStringLiteral("a"), {}});
    column.appendTile(Tile{QStringLiteral("b"), {}});
    column.appendTile(Tile{QStringLiteral("c"), {}}); // active = a (index 0)

    QVERIFY(column.moveTile(0, 2));
    QCOMPARE(column.windowIds(), (QStringList{QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("a")}));
    QCOMPARE(column.activeTileIndex(), 2); // focus tracks the moved tile
    QVERIFY(!column.moveTile(1, 1));
    QVERIFY(!column.moveTile(5, 0));
}

void TestColumn::widthIntent()
{
    Column column;
    QCOMPARE(column.width().kind, ColumnWidth::Kind::Proportion);
    QCOMPARE(column.presetWidthIndex(), -1);

    column.setWidth(ColumnWidth::fixed(640));
    QCOMPARE(column.width().kind, ColumnWidth::Kind::Fixed);
    QCOMPARE(column.width().value, 640.0);

    column.setPresetWidthIndex(2);
    QCOMPARE(column.presetWidthIndex(), 2);
}

void TestColumn::jsonRoundTrip()
{
    Column column;
    column.appendTile(Tile{QStringLiteral("a"), WindowHeight::fixed(300)});
    column.appendTile(Tile{QStringLiteral("b"), WindowHeight::preset(1)});
    column.setActiveTileIndex(1);
    column.setWidth(ColumnWidth::proportion(0.33));
    column.setPresetWidthIndex(0);

    const Column restored = Column::fromJson(column.toJson());
    QCOMPARE(restored.windowIds(), column.windowIds());
    QCOMPARE(restored.activeTileIndex(), 1);
    QVERIFY(restored.width() == ColumnWidth::proportion(0.33));
    QCOMPARE(restored.presetWidthIndex(), 0);
    QVERIFY(restored.tiles().at(0).height == WindowHeight::fixed(300));
    QVERIFY(restored.tiles().at(1).height == WindowHeight::preset(1));
}

QTEST_GUILESS_MAIN(TestColumn)
#include "test_column.moc"
