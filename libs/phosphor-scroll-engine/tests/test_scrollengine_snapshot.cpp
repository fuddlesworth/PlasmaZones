// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// ScrollEngine::stripSnapshot — the column-aware read surface behind the
// daemon's strip-mode drag popup. The load-bearing property throughout is
// the INDEX CONTRACT (ScrollEngineTypes.h): snapshot column and tile
// positions must be valid DragInsertTarget indices against the strip a
// commit will see, whether the dragged window was already detached by a
// live preview or is emulated out via excludeWindowId.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::engineParams;
using ScrollTestUtils::makeProviderEngine;

class TestScrollEngineSnapshot : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void invalidForUnmanagedScreen();
    void emptyStripIsValidWithZeroColumns();
    void columnsFollowStripOrder();
    void tabbedColumnMarksTabs();
    void minimizedTileKeptWithNullRect();
    void excludeRemovesTileAndRenumbers();
    void excludeDropsEmptiedColumn();
    void livePreviewOmitsDraggedWindow();

private:
    static ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screenId));
    }

    static void openWindows(ScrollEngine* engine, const QString& screenId, const QStringList& ids)
    {
        for (const QString& id : ids) {
            engine->windowOpened(id, screenId, 0, 0);
        }
    }

    /// Stack @p windowId under column @p column as its last tile.
    static void stackUnder(ScrollState* state, int column, int tileIdx, const QString& windowId)
    {
        QVERIFY(state->strip().takeWindow(windowId, engineParams()));
        QVERIFY(state->strip().insertWindowIntoColumnAt(column, tileIdx, windowId, engineParams()));
    }
};

void TestScrollEngineSnapshot::invalidForUnmanagedScreen()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S9"));
    QVERIFY(!snap.valid);
    QVERIFY(snap.columns.isEmpty());
}

void TestScrollEngineSnapshot::emptyStripIsValidWithZeroColumns()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->strip().takeWindow(QStringLiteral("a"), engineParams()));

    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"));
    QVERIFY(snap.valid);
    QVERIFY(snap.columns.isEmpty());
    QCOMPARE(snap.activeColumnIndex, -1);
}

void TestScrollEngineSnapshot::columnsFollowStripOrder()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 3);
    QCOMPARE(snap.columns.at(0).tiles.size(), 1);
    QCOMPARE(snap.columns.at(0).tiles.at(0).windowId, QStringLiteral("a"));
    QCOMPARE(snap.columns.at(1).tiles.at(0).windowId, QStringLiteral("b"));
    QCOMPARE(snap.columns.at(2).tiles.at(0).windowId, QStringLiteral("c"));
    // Solo tiles are their column's active tile and fill their column.
    QVERIFY(snap.columns.at(0).tiles.at(0).activeTab);
    QVERIFY(!snap.columns.at(0).tiles.at(0).hidden);
    QVERIFY(snap.columns.at(0).relWidth > 0.0);
    QVERIFY(snap.columns.at(0).relHeight > 0.0);
    QVERIFY(snap.columns.at(0).tiles.at(0).relRect.height() > 0.9);
    // The last-opened window's column is active.
    QCOMPARE(snap.activeColumnIndex, 2);
}

void TestScrollEngineSnapshot::tabbedColumnMarksTabs()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    stackUnder(state, 1, 1, QStringLiteral("c"));
    // Column [b, c] is the active column after the re-insert; make it tabbed.
    QVERIFY(state->strip().toggleActiveColumnTabbed());

    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 2);
    const ScrollStripSnapshotColumn& tabbed = snap.columns.at(1);
    QVERIFY(tabbed.tabbed);
    QCOMPARE(tabbed.tiles.size(), 2);
    // Exactly one visible tab; the other is hidden with no rect.
    int activeCount = 0;
    for (const ScrollStripSnapshotTile& tile : tabbed.tiles) {
        if (tile.activeTab) {
            ++activeCount;
            QVERIFY(!tile.hidden);
            QVERIFY(!tile.relRect.isNull());
        } else {
            QVERIFY(tile.hidden);
            QVERIFY(tile.relRect.isNull());
        }
    }
    QCOMPARE(activeCount, 1);
}

void TestScrollEngineSnapshot::minimizedTileKeptWithNullRect()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    stackUnder(state, 1, 1, QStringLiteral("c"));
    QVERIFY(state->strip().setWindowMinimized(QStringLiteral("b"), true, engineParams()));

    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 2);
    // b keeps its MODEL slot (tile 0) — that is what keeps c's position a
    // valid DragInsertTarget.secondary — but resolves no rect.
    const ScrollStripSnapshotColumn& column = snap.columns.at(1);
    QCOMPARE(column.tiles.size(), 2);
    QCOMPARE(column.tiles.at(0).windowId, QStringLiteral("b"));
    QVERIFY(column.tiles.at(0).minimized);
    QVERIFY(column.tiles.at(0).relRect.isNull());
    QCOMPARE(column.tiles.at(1).windowId, QStringLiteral("c"));
    QVERIFY(!column.tiles.at(1).minimized);
    QVERIFY(!column.tiles.at(1).relRect.isNull());
}

void TestScrollEngineSnapshot::excludeRemovesTileAndRenumbers()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    stackUnder(state, 1, 1, QStringLiteral("c"));

    // Excluding the stack's TOP tile: the column survives and c moves up to
    // tile position 0 — the position it will hold once a real begin detaches
    // b from a two-tile column.
    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("b"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 2);
    QCOMPARE(snap.columns.at(1).tiles.size(), 1);
    QCOMPARE(snap.columns.at(1).tiles.at(0).windowId, QStringLiteral("c"));
}

void TestScrollEngineSnapshot::excludeDropsEmptiedColumn()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    // b's solo column empties and drops, so c renumbers from column 2 to
    // column 1 — the post-detach index a commit against the excluded strip
    // will actually consume.
    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("b"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 2);
    QCOMPARE(snap.columns.at(0).tiles.at(0).windowId, QStringLiteral("a"));
    QCOMPARE(snap.columns.at(1).tiles.at(0).windowId, QStringLiteral("c"));
}

void TestScrollEngineSnapshot::livePreviewOmitsDraggedWindow()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));

    // The preview detached b; the snapshot reads the preview's captured
    // state, so b is absent WITHOUT excludeWindowId and the exclusion arm
    // must not double-apply (passing the id anyway changes nothing).
    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 2);
    QCOMPARE(snap.columns.at(0).tiles.at(0).windowId, QStringLiteral("a"));
    QCOMPARE(snap.columns.at(1).tiles.at(0).windowId, QStringLiteral("c"));

    const ScrollStripSnapshot snapWithExclude = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("b"));
    QCOMPARE(snapWithExclude.columns.size(), 2);
    engine->cancelDragInsertPreview();
}

QTEST_GUILESS_MAIN(TestScrollEngineSnapshot)
#include "test_scrollengine_snapshot.moc"
