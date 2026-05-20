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
    void dragReordersColumn();
    void focusAndMoveColumns();
    void tileNavigation();
    void placementAndFloating();
    void jsonRoundTrip();
    void clearFloatingDropsWindow();
    void focusWindowById();
    void malformedJson();
    void duplicateWindowIdsDropped();
    void consumeSkipsMinimizedSource();
    void moveColumnNextToNoOpReturnsFalse();
    void focusWindowAlreadyFocusedReturnsFalse();
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

    // Full-width toggle on an empty strip is a harmless no-op: no active column
    // to toggle, and the strip stays empty.
    state.toggleActiveColumnFullWidth();
    QCOMPARE(state.columnCount(), 0);
    QCOMPARE(state.activeColumnIndex(), -1);
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
    // Minimizing focused "b" hands focus to the first still-visible window in
    // strip order — deterministically "a".
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));

    QVERIFY(!state.setWindowMinimized(QStringLiteral("b"), true)); // already minimized — no-op
    QVERIFY(!state.setWindowMinimized(QStringLiteral("missing"), true));

    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), false)); // restore
    QVERIFY(!state.isWindowMinimized(QStringLiteral("b")));

    // The minimized flag survives a JSON round-trip.
    QVERIFY(state.setWindowMinimized(QStringLiteral("c"), true));
    const ScrollScreenState restored = ScrollScreenState::fromJson(state.toJson());
    QVERIFY(restored.isWindowMinimized(QStringLiteral("c")));
    QVERIFY(!restored.isWindowMinimized(QStringLiteral("a")));

    // Every tiled window minimized: setWindowMinimized still reports each
    // change, every slot is kept, and focus is left on its (now hidden)
    // window — there is nothing visible to hand off to — rather than cleared.
    // "c" is already minimized above; minimizing "a" then "b" completes the set.
    QVERIFY(state.setWindowMinimized(QStringLiteral("a"), true));
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), true));
    QCOMPARE(state.tiledWindowCount(), 3); // all still tiled, just hidden
    QCOMPARE(state.columnCount(), 3); // every slot kept
    QCOMPARE(state.focusedWindowId(), QStringLiteral("b")); // retained, not cleared
    QVERIFY(state.isWindowMinimized(state.focusedWindowId())); // ...even though hidden
}

void TestScrollScreenState::dragReordersColumn()
{
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c")); // [a][b][c]

    // Drop "a" after "c": column order becomes [b][c][a]; focus follows "a".
    QVERIFY(state.moveColumnNextTo(QStringLiteral("a"), QStringLiteral("c"), /*placeAfter=*/true));
    QCOMPARE(state.columns().at(0).windowIds().first(), QStringLiteral("b"));
    QCOMPARE(state.columns().at(1).windowIds().first(), QStringLiteral("c"));
    QCOMPARE(state.columns().at(2).windowIds().first(), QStringLiteral("a"));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));

    // Drop "a" before "b": back to [a][b][c].
    QVERIFY(state.moveColumnNextTo(QStringLiteral("a"), QStringLiteral("b"), /*placeAfter=*/false));
    QCOMPARE(state.columns().at(0).windowIds().first(), QStringLiteral("a"));
    QCOMPARE(state.columns().at(1).windowIds().first(), QStringLiteral("b"));
    QCOMPARE(state.columns().at(2).windowIds().first(), QStringLiteral("c"));

    // A drop onto the dragged window's own column, or against an unknown
    // window, returns false — and leaves the column order untouched.
    QVERIFY(!state.moveColumnNextTo(QStringLiteral("a"), QStringLiteral("a"), true));
    QVERIFY(!state.moveColumnNextTo(QStringLiteral("a"), QStringLiteral("missing"), true));
    QCOMPARE(state.columns().at(0).windowIds().first(), QStringLiteral("a"));
    QCOMPARE(state.columns().at(1).windowIds().first(), QStringLiteral("b"));
    QCOMPARE(state.columns().at(2).windowIds().first(), QStringLiteral("c"));

    // A drop that resolves to the column's own slot ("a" before "b" — where
    // "a" already sits) is a positional no-op: it still returns true and
    // focuses the window, but leaves the order untouched.
    QVERIFY(state.focusWindow(QStringLiteral("c")));
    QVERIFY(state.moveColumnNextTo(QStringLiteral("a"), QStringLiteral("b"), /*placeAfter=*/false));
    QCOMPARE(state.columns().at(0).windowIds().first(), QStringLiteral("a"));
    QCOMPARE(state.columns().at(1).windowIds().first(), QStringLiteral("b"));
    QCOMPARE(state.columns().at(2).windowIds().first(), QStringLiteral("c"));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));

    // A multi-tile column moves as a whole, carrying both tiles; focus lands
    // on the dragged window.
    ScrollScreenState multi;
    multi.addColumnForWindow(QStringLiteral("x"));
    multi.addWindowToActiveColumn(QStringLiteral("x2")); // column [x, x2]
    multi.addColumnForWindow(QStringLiteral("y")); // [x,x2][y]
    QVERIFY(multi.moveColumnNextTo(QStringLiteral("x"), QStringLiteral("y"), /*placeAfter=*/true));
    QCOMPARE(multi.columnCount(), 2);
    QCOMPARE(multi.columns().at(0).windowIds().first(), QStringLiteral("y"));
    QCOMPARE(multi.columns().at(1).windowIds(), QStringList({QStringLiteral("x"), QStringLiteral("x2")}));
    QCOMPARE(multi.focusedWindowId(), QStringLiteral("x"));
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
    state.setScrollX(-128.0);
    state.markFloating(QStringLiteral("f1"));
    state.toggleActiveColumnFullWidth(); // column "c" enters full-width

    ScrollScreenState restored = ScrollScreenState::fromJson(state.toJson());
    QCOMPARE(restored.screenId(), QStringLiteral("HDMI-1"));
    QCOMPARE(restored.columnCount(), state.columnCount());
    QCOMPARE(restored.activeColumnIndex(), state.activeColumnIndex());
    QCOMPARE(restored.scrollX(), -128.0);
    QCOMPARE(restored.focusedWindowId(), state.focusedWindowId());
    QVERIFY(restored.isFloating(QStringLiteral("f1")));
    QCOMPARE(restored.placementIdForWindow(QStringLiteral("b")), QStringLiteral("0:1"));
    QVERIFY(restored.activeColumn() && restored.activeColumn()->isFullWidth()); // survives the round-trip

    // The remembered restore-width survives too: toggling full-width off on the
    // restored state returns the column to its pre-toggle width (default 0.5).
    restored.toggleActiveColumnFullWidth();
    QVERIFY(restored.activeColumn() && !restored.activeColumn()->isFullWidth());
    QVERIFY(qFuzzyCompare(restored.activeColumn()->width().value, 0.5));
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

void TestScrollScreenState::consumeSkipsMinimizedSource()
{
    // consumeIntoColumn pulls the next column's active tile into the focused
    // column AND makes it the focused tile. A minimized source tile would
    // therefore land as the focused tile of the focused column — but a
    // minimized tile is excluded from the resolved layout, so the user would
    // see the focused column's previously visible tile vanish with no
    // feedback. The op must skip silently in that case.
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b")); // [a][b], focus=b
    QVERIFY(state.focusColumn(-1)); // focus column "a"

    // Minimize the active tile of the next column ("b"). Note: setWindowMinimized
    // on the focused window hands focus away — but here we minimize "b", which
    // is NOT focused, so column "a" stays focused.
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), true));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));

    // The consume must refuse: column count, structure, and focus all
    // unchanged.
    QVERIFY(!state.consumeIntoColumn());
    QCOMPARE(state.columnCount(), 2);
    QCOMPARE(state.columns().at(0).windowIds(), QStringList{QStringLiteral("a")});
    QCOMPARE(state.columns().at(1).windowIds(), QStringList{QStringLiteral("b")});
    QVERIFY(state.isWindowMinimized(QStringLiteral("b")));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));

    // Restoring the source tile lets the consume succeed normally.
    QVERIFY(state.setWindowMinimized(QStringLiteral("b"), false));
    QVERIFY(state.consumeIntoColumn());
    QCOMPARE(state.columnCount(), 1);
    QCOMPARE(state.activeColumn()->windowIds(), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

void TestScrollScreenState::moveColumnNextToNoOpReturnsFalse()
{
    // A drag drop that lands the dragged column in its own slot AND was
    // already the focused window is a complete no-op: column order unchanged,
    // focus unchanged. moveColumnNextTo must return false in that case so
    // ScrollEngine::windowDropped skips the placementChanged emit.
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addColumnForWindow(QStringLiteral("b"));
    state.addColumnForWindow(QStringLiteral("c")); // [a][b][c]
    QVERIFY(state.focusWindow(QStringLiteral("a"))); // focus a (column 0)

    // Drop "a" before "b": target == from (a stays at 0), focus already on a.
    QVERIFY(!state.moveColumnNextTo(QStringLiteral("a"), QStringLiteral("b"), /*placeAfter=*/false));
    QCOMPARE(state.columns().at(0).windowIds().first(), QStringLiteral("a"));
    QCOMPARE(state.columns().at(1).windowIds().first(), QStringLiteral("b"));
    QCOMPARE(state.columns().at(2).windowIds().first(), QStringLiteral("c"));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));

    // The companion case — same target == from, but focus had moved away — is
    // a real mutation (focus changes c → a) and must still return true. This
    // pins the asymmetry: only the COMPLETE no-op is suppressed.
    QVERIFY(state.focusWindow(QStringLiteral("c")));
    QVERIFY(state.moveColumnNextTo(QStringLiteral("a"), QStringLiteral("b"), /*placeAfter=*/false));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));
}

void TestScrollScreenState::focusWindowAlreadyFocusedReturnsFalse()
{
    // focusWindow on the already-focused {column, tile} must not mutate state
    // and must report no change so the engine skips a redundant emit on a
    // duplicate compositor-side focus event.
    ScrollScreenState state;
    state.addColumnForWindow(QStringLiteral("a"));
    state.addWindowToActiveColumn(QStringLiteral("a2")); // column 0 = [a, a2], focus a2
    state.addColumnForWindow(QStringLiteral("b")); // column 1 = [b], focus b

    // Already focused: returns false, no mutation.
    QVERIFY(!state.focusWindow(QStringLiteral("b")));
    QCOMPARE(state.focusedWindowId(), QStringLiteral("b"));
    QCOMPARE(state.activeColumnIndex(), 1);

    // Switching to a sibling tile within the same column counts as a real
    // change (column matches, tile differs).
    QVERIFY(state.focusWindow(QStringLiteral("a"))); // column 0 tile 0
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a"));
    QVERIFY(state.focusWindow(QStringLiteral("a2"))); // same column, different tile
    QCOMPARE(state.focusedWindowId(), QStringLiteral("a2"));
    QVERIFY(!state.focusWindow(QStringLiteral("a2"))); // already focused — no change
}

QTEST_GUILESS_MAIN(TestScrollScreenState)
#include "test_scroll_screen_state.moc"
