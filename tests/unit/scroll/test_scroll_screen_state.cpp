// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorScrollEngine/ScrollScreenState.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace PhosphorScrollEngine;

class TestScrollScreenState : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyState();
    void openWindowsAsColumns();
    void addToActiveColumn();
    void removeDropsEmptyColumn();
    void consumeAndExpel();
    void minimizeKeepsSlot();
    void focusAndMoveColumns();
    void tileNavigation();
    void placementAndFloating();
    void jsonRoundTrip();
    void clearFloatingDropsWindow();
    void focusWindowById();
    void malformedJson();
    void duplicateWindowIdsDropped();
};

void TestScrollScreenState::emptyState()
{
    ScrollScreenState state(QStringLiteral("DP-1"));
    QCOMPARE(state.screenId(), QStringLiteral("DP-1"));
    QVERIFY(state.isEmpty());
    QCOMPARE(state.columnCount(), 0);
    QCOMPARE(state.activeColumnIndex(), -1);
    QVERIFY(state.focusedWindowId().isEmpty());
    QCOMPARE(state.windowCount(), 0);
    QVERIFY(state.activeColumn() == nullptr);
}

void TestScrollScreenState::openWindowsAsColumns()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    QCOMPARE(state.columnCount(), 1);
    QCOMPARE(state.activeColumnIndex(), 0);
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));

    state.addColumnForWindow(QStringLiteral("b"));
    QCOMPARE(state.columnCount(), 2);
    QCOMPARE(state.activeColumnIndex(), 1); // the new column is focused
    QCOMPARE(state.focusedWindowId(), QStringLiteral("b"));

    // A new column opens immediately to the right of the focused column.
    QVERIFY(state.focusColumn(-1)); // focus column "a" at index 0
    state.addColumnForWindow(QStringLiteral("c"));
    QCOMPARE(state.activeColumnIndex(), 1);
    QCOMPARE(state.columns().at(1).windowIds(), QStringList{QStringLiteral("c")});
    QCOMPARE(state.tiledWindowCount(), 3);

    state.addColumnForWindow(QStringLiteral("a")); // duplicate — ignored
    QCOMPARE(state.columnCount(), 3);
}

void TestScrollScreenState::addToActiveColumn()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addWindowToActiveColumn(QStringLiteral("b"));
    QCOMPARE(state.columnCount(), 1);
    QCOMPARE(state.activeColumn()->tileCount(), 2);
    QCOMPARE(state.focusedWindowId(), QStringLiteral("b")); // the new tile is focused
}

void TestScrollScreenState::removeDropsEmptyColumn()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c")); // focus = c (index 2)

    QVERIFY(state.removeWindow(QStringLiteral("a"))); // remove a non-focused column
    QCOMPARE(state.columnCount(), 2);
    QCOMPARE(state.focusedWindowId(), QStringLiteral("c")); // focus preserved

    QVERIFY(state.removeWindow(QStringLiteral("c"))); // remove the focused column
    QCOMPARE(state.columnCount(), 1);
    QCOMPARE(state.focusedWindowId(), QStringLiteral("b")); // focus clamps to survivor

    QVERIFY(!state.removeWindow(QStringLiteral("missing")));
}

void TestScrollScreenState::consumeAndExpel()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    QVERIFY(state.focusColumn(-1)); // focus column "a"

    QVERIFY(state.consumeIntoColumn()); // pull "b" into column "a"
    QCOMPARE(state.columnCount(), 1);
    QCOMPARE(state.activeColumn()->windowIds(), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    QVERIFY(!state.consumeIntoColumn()); // no next column

    QVERIFY(state.expelFromColumn()); // push focused tile "b" into its own column
    QCOMPARE(state.columnCount(), 2);
    QCOMPARE(state.activeColumnIndex(), 1);
    QCOMPARE(state.focusedWindowId(), QStringLiteral("b"));
}

void TestScrollScreenState::minimizeKeepsSlot()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c")); // [a][b][c]

    QVERIFY(state.focusWindow(QStringLiteral("b")));
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), true));
    QVERIFY(state.isWindowMinimized(QStringLiteral("b")));
    QCOMPARE(state.columnCount(), 3); // slot kept — the column is not dropped
    QCOMPARE(state.tiledWindowCount(), 3); // still tiled, only hidden
    // The focused window must not be a minimized one.
    QVERIFY(state.focusedWindowId() != QStringLiteral("b"));
    QVERIFY(!state.isWindowMinimized(state.focusedWindowId()));

    QVERIFY(!state.setWindowMinimized(QStringLiteral("b"), true)); // already minimized — no-op
    QVERIFY(!state.setWindowMinimized(QStringLiteral("missing"), true));

    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), false)); // restore
    QVERIFY(!state.isWindowMinimized(QStringLiteral("b")));

    // The minimized flag survives a JSON round-trip.
    QVERIFY(state.setWindowMinimized(QStringLiteral("c"), true));
    const ScrollScreenState restored = ScrollScreenState::fromJson(state.toJson());
    QVERIFY(restored.isWindowMinimized(QStringLiteral("c")));
    QVERIFY(!restored.isWindowMinimized(QStringLiteral("a")));
}

void TestScrollScreenState::focusAndMoveColumns()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c")); // [a, b, c], focus index 2

    QVERIFY(state.focusColumn(-2));
    QCOMPARE(state.activeColumnIndex(), 0);
    QVERIFY(!state.focusColumn(-1)); // clamped — no movement

    QVERIFY(state.moveColumn(1)); // move column "a" right one slot
    QCOMPARE(state.columns().at(0).windowIds(), QStringList{QStringLiteral("b")});
    QCOMPARE(state.columns().at(1).windowIds(), QStringList{QStringLiteral("a")});
    QCOMPARE(state.activeColumnIndex(), 1); // focus follows the moved column
}

void TestScrollScreenState::tileNavigation()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addWindowToActiveColumn(QStringLiteral("b"));
    state.addWindowToActiveColumn(QStringLiteral("c")); // column [a, b, c], focus tile c

    QVERIFY(state.focusTile(-2));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));

    QVERIFY(state.moveTile(1));
    QCOMPARE(state.activeColumn()->windowIds(),
             (QStringList{QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")}));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a")); // focus follows the moved tile
}

void TestScrollScreenState::placementAndFloating()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addWindowToActiveColumn(QStringLiteral("b")); // column 0 = [a, b]
    state.addColumnForWindow(QStringLiteral("c")); // column 1 = [c]

    QCOMPARE(state.placementIdForWindow(QStringLiteral("a")), QStringLiteral("0:0"));
    QCOMPARE(state.placementIdForWindow(QStringLiteral("b")), QStringLiteral("0:1"));
    QCOMPARE(state.placementIdForWindow(QStringLiteral("c")), QStringLiteral("1:0"));
    QVERIFY(state.placementIdForWindow(QStringLiteral("missing")).isEmpty());

    QVERIFY(!state.isFloating(QStringLiteral("b")));
    state.markFloating(QStringLiteral("b")); // leaves the strip, enters the floating set
    QVERIFY(state.isFloating(QStringLiteral("b")));
    QVERIFY(state.containsWindow(QStringLiteral("b")));
    QCOMPARE(state.tiledWindowCount(), 2); // a, c
    QCOMPARE(state.windowCount(), 3); // a, c + floating b
    QVERIFY(state.floatingWindows().contains(QStringLiteral("b")));
    QVERIFY(state.placementIdForWindow(QStringLiteral("b")).isEmpty());
}

void TestScrollScreenState::jsonRoundTrip()
{
    ScrollScreenState state(QStringLiteral("HDMI-1"));
    state.addColumnForWindow(QStringLiteral("a"));
    state.addWindowToActiveColumn(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c"));
    state.setViewOffset(-128.0);
    state.markFloating(QStringLiteral("f1"));

    const ScrollScreenState restored = ScrollScreenState::fromJson(state.toJson());
    QCOMPARE(restored.screenId(), QStringLiteral("HDMI-1"));
    QCOMPARE(restored.columnCount(), state.columnCount());
    QCOMPARE(restored.activeColumnIndex(), state.activeColumnIndex());
    QCOMPARE(restored.viewOffset(), -128.0);
    QCOMPARE(restored.focusedWindowId(), state.focusedWindowId());
    QVERIFY(restored.isFloating(QStringLiteral("f1")));
    QCOMPARE(restored.placementIdForWindow(QStringLiteral("b")), QStringLiteral("0:1"));
}

void TestScrollScreenState::clearFloatingDropsWindow()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.markFloating(QStringLiteral("b"));
    QVERIFY(state.isFloating(QStringLiteral("b")));
    QCOMPARE(state.windowCount(), 2);

    state.clearFloating(QStringLiteral("b"));
    QVERIFY(!state.isFloating(QStringLiteral("b")));
    QCOMPARE(state.windowCount(), 1); // dropped from the floating set, not re-tiled
}

void TestScrollScreenState::focusWindowById()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c")); // focus = c

    QVERIFY(state.focusWindow(QStringLiteral("a")));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));
    QVERIFY(!state.focusWindow(QStringLiteral("missing")));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a")); // unchanged on a miss
}

void TestScrollScreenState::malformedJson()
{
    // Empty and wrong-typed JSON degrade to an empty strip rather than crash.
    const ScrollScreenState empty = ScrollScreenState::fromJson(QJsonObject{});
    QVERIFY(empty.isEmpty());
    QCOMPARE(empty.activeColumnIndex(), -1);

    QJsonObject wrongType;
    wrongType.insert(QLatin1String("columns"), 42);
    wrongType.insert(QLatin1String("activeColumnIndex"), 99);
    const ScrollScreenState bad = ScrollScreenState::fromJson(wrongType);
    QVERIFY(bad.isEmpty());
    QCOMPARE(bad.activeColumnIndex(), -1); // clamped despite the out-of-range index
}

void TestScrollScreenState::duplicateWindowIdsDropped()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));

    // Hand-craft JSON with a duplicate column carrying id "a".
    QJsonObject json = state.toJson();
    QJsonArray columns = json.value(QLatin1String("columns")).toArray();
    columns.append(columns.at(0));
    json.insert(QLatin1String("columns"), columns);

    const ScrollScreenState restored = ScrollScreenState::fromJson(json);
    QCOMPARE(restored.columnCount(), 2); // the duplicate column is dropped (became empty)
    QCOMPARE(restored.tiledWindowCount(), 2); // "a" survives exactly once
}

QTEST_GUILESS_MAIN(TestScrollScreenState)
#include "test_scroll_screen_state.moc"
