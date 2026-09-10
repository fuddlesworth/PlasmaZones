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
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void invalidForUnmanagedScreen();
    void emptyStripIsValidWithZeroColumns();
    void columnsFollowStripOrder();
    void tabbedColumnMarksTabs();
    void minimizedTileKeptWithNullRect();
    void fullyMinimizedColumnStillEmitted();
    void excludeRemovesTileAndRenumbers();
    void excludeDropsEmptiedColumn();
    void excludeActiveSoloColumnRepointsActive();
    void excludeActiveTilePromotesSurvivor();
    void excludeActiveTilePromotesByPosition();
    void excludeActiveTabPromotesVisibleTab();
    void excludeActivePromotionSkipsMinimizedSurvivors();
    void excludeSoloWindowLeavesNoActiveColumn();
    void overWideColumnClampsFractionToOne();
    void invalidWorkAreaAnswersInvalid();
    void gapsShareTheColumnCrossExtent();
    void livePreviewOmitsDraggedWindow();
    // The key-taking overload (the workspace overview's read) and the two
    // IOverviewModelSource views over it.
    void keyOverloadMatchesCurrentContextAndFillsAbsRects();
    void keyOverloadAnswersANonCurrentContext();
    void keyOverloadPlacesAParkedColumnOutsideTheWorkArea();
    void keyOverloadMutatesNeitherActiveColumnNorView();
    void neverCreatedKeyAnswersInvalidAndNullopt();
    void overviewWindowsListsTilesOnceWithIndicesAndFloats();
    void overviewStripCarriesTheSnapshotRects();

private:
    static PhosphorEngine::PlacementStateKey keyFor(const QString& screenId, int desktop)
    {
        return PhosphorEngine::PlacementStateKey{screenId, desktop, QString()};
    }

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

    /// Insert @p windowId into column @p column at tile index @p tileIdx.
    /// Returns false on failure — callers wrap the call in QVERIFY, because a
    /// QVERIFY inside this helper would only return from the HELPER and let
    /// the slot run on against an un-stacked strip, cascading confusing
    /// secondary failures.
    [[nodiscard]] static bool stackUnder(ScrollState* state, int column, int tileIdx, const QString& windowId)
    {
        return state->strip().takeWindow(windowId, engineParams())
            && state->strip().insertWindowIntoColumnAt(column, tileIdx, windowId, engineParams());
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
    QVERIFY(snap.columns.at(0).tiles.at(0).relRect.height() > 0.9);
    // Every resolved column carries its real work-area share for the
    // preview scaling. The engine default column width is proportion 0.5,
    // so a freshly opened solo column must report exactly half — pinning
    // the value (not just a range) catches a fraction that silently
    // degrades to full width.
    for (const ScrollStripSnapshotColumn& column : snap.columns) {
        QCOMPARE(column.widthFraction, 0.5);
    }
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
    QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));
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
    QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));
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

void TestScrollEngineSnapshot::fullyMinimizedColumnStillEmitted()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // Minimize b's SOLE tile: relayout emits no entry for the column, but
    // the snapshot must keep it (with its tile, rect-less) — dropping it
    // would renumber c against the model the target indices name.
    QVERIFY(state->strip().setWindowMinimized(QStringLiteral("b"), true, engineParams()));

    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 3);
    QCOMPARE(snap.columns.at(1).tiles.size(), 1);
    QVERIFY(snap.columns.at(1).tiles.at(0).minimized);
    QVERIFY(snap.columns.at(1).tiles.at(0).relRect.isNull());
    // A column that resolves no rect carries no width share either — the
    // renderer's full-width fallback covers it.
    QCOMPARE(snap.columns.at(1).widthFraction, 0.0);
    QCOMPARE(snap.columns.at(2).tiles.at(0).windowId, QStringLiteral("c"));
}

void TestScrollEngineSnapshot::excludeRemovesTileAndRenumbers()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));

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
    // c was the active column (last opened, model index 2); after b's column
    // drops, the active index renumbers with it.
    QCOMPARE(snap.activeColumnIndex, 1);
}

void TestScrollEngineSnapshot::excludeActiveSoloColumnRepointsActive()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    // c is the active column AND the excluded window (the common
    // drag-a-solo-window case). The real detach re-points the active index
    // to qMin(colIdx, size - 1) — the new last column — and the emulation
    // must agree, or the popup shows no active-column highlight until the
    // trigger is held and the real detach runs.
    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("c"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 2);
    QCOMPARE(snap.activeColumnIndex, 1);
}

void TestScrollEngineSnapshot::excludeActiveTilePromotesSurvivor()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));

    // c is column 1's ACTIVE tile after the re-insert. Excluding it leaves b
    // with no activeTab from the model comparison (b's model index is 0, the
    // active index was 1); the snapshot promotes the survivor the way the
    // real detach re-points activeTileIdx.
    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("c"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 2);
    QCOMPARE(snap.columns.at(1).tiles.size(), 1);
    QCOMPARE(snap.columns.at(1).tiles.at(0).windowId, QStringLiteral("b"));
    QVERIFY(snap.columns.at(1).tiles.at(0).activeTab);
    QVERIFY(!snap.columns.at(1).tiles.at(0).hidden);
}

void TestScrollEngineSnapshot::excludeActiveTilePromotesByPosition()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"),
                {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // Column 1 becomes [b, c, d] with c ACTIVE (the last insert wins focus).
    QVERIFY(stackUnder(state, 1, 1, QStringLiteral("d")));
    QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));

    // Excluding the MIDDLE active tile must promote the NEXT tile DOWN (d),
    // the way removeWindowInternal keeps activeTileIdx's numeric value — a
    // first-survivor scan would wrongly promote b here.
    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("c"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.at(1).tiles.size(), 2);
    QCOMPARE(snap.columns.at(1).tiles.at(0).windowId, QStringLiteral("b"));
    QCOMPARE(snap.columns.at(1).tiles.at(1).windowId, QStringLiteral("d"));
    QVERIFY(!snap.columns.at(1).tiles.at(0).activeTab);
    QVERIFY(snap.columns.at(1).tiles.at(1).activeTab);
}

void TestScrollEngineSnapshot::excludeActiveTabPromotesVisibleTab()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));
    QVERIFY(state->strip().toggleActiveColumnTabbed());

    // Excluding a TABBED column's active tab: the survivor is promoted to
    // the visible tab (activeTab set, hidden cleared) so the tab strip does
    // not render a column with no highlighted segment, and its relRect is
    // BACK-FILLED from the resolve (the rect walk ran while it was still a
    // hidden tab) so no tile is handed to renderers visible but rect-less.
    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("c"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 2);
    QVERIFY(snap.columns.at(1).tabbed);
    QCOMPARE(snap.columns.at(1).tiles.size(), 1);
    QCOMPARE(snap.columns.at(1).tiles.at(0).windowId, QStringLiteral("b"));
    QVERIFY(snap.columns.at(1).tiles.at(0).activeTab);
    QVERIFY(!snap.columns.at(1).tiles.at(0).hidden);
    QVERIFY(!snap.columns.at(1).tiles.at(0).relRect.isNull());
}

void TestScrollEngineSnapshot::excludeActivePromotionSkipsMinimizedSurvivors()
{
    // The minimized skip in the promotion scan — a DELIBERATE divergence from
    // the real detach, which does no such skip: a preview highlighting an
    // invisible tile would be a lie. Three legs, one per scan outcome.
    const auto params = ScrollTestUtils::engineParams();

    // Forward skip: [b, c(active), d(min), e] excluding c — the inheriting
    // slot is d, which is minimized, so the FORWARD scan moves on to e. A
    // scan that promoted the inheriting slot regardless highlights d.
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
        openWindows(
            engine, QStringLiteral("S1"),
            {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")});
        ScrollState* state = stateFor(engine, QStringLiteral("S1"));
        QVERIFY(state);
        QVERIFY(stackUnder(state, 1, 1, QStringLiteral("e")));
        QVERIFY(stackUnder(state, 1, 1, QStringLiteral("d")));
        QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));
        QVERIFY(state->strip().setWindowMinimized(QStringLiteral("d"), true, params));

        const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("c"));
        QVERIFY(snap.valid);
        QCOMPARE(snap.columns.at(1).tiles.size(), 3);
        for (const auto& tile : snap.columns.at(1).tiles) {
            QCOMPARE(tile.activeTab, tile.windowId == QStringLiteral("e"));
        }
    }

    // Backward scan: [b, c(active), d(min)] excluding c — nothing visible at
    // or after the inheriting slot, so the promotion walks back to b.
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
        openWindows(engine, QStringLiteral("S1"),
                    {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});
        ScrollState* state = stateFor(engine, QStringLiteral("S1"));
        QVERIFY(state);
        QVERIFY(stackUnder(state, 1, 1, QStringLiteral("d")));
        QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));
        QVERIFY(state->strip().setWindowMinimized(QStringLiteral("d"), true, params));

        const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("c"));
        QVERIFY(snap.valid);
        QCOMPARE(snap.columns.at(1).tiles.size(), 2);
        for (const auto& tile : snap.columns.at(1).tiles) {
            QCOMPARE(tile.activeTab, tile.windowId == QStringLiteral("b"));
        }
    }

    // Every survivor minimized (embedder-only state): the flag stays unset
    // rather than highlighting an invisible tile.
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
        openWindows(engine, QStringLiteral("S1"),
                    {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});
        ScrollState* state = stateFor(engine, QStringLiteral("S1"));
        QVERIFY(state);
        QVERIFY(stackUnder(state, 1, 1, QStringLiteral("d")));
        QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));
        QVERIFY(state->strip().setWindowMinimized(QStringLiteral("b"), true, params));
        QVERIFY(state->strip().setWindowMinimized(QStringLiteral("d"), true, params));

        const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("c"));
        QVERIFY(snap.valid);
        QCOMPARE(snap.columns.at(1).tiles.size(), 2);
        for (const auto& tile : snap.columns.at(1).tiles) {
            QVERIFY2(!tile.activeTab, "with every survivor minimized, nothing may be highlighted");
        }
    }
}

void TestScrollEngineSnapshot::excludeSoloWindowLeavesNoActiveColumn()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a")});

    // Excluding the ONLY window empties the snapshot: the dropped-active
    // fixup must leave activeColumnIndex at -1 rather than inventing one.
    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"), QStringLiteral("a"));
    QVERIFY(snap.valid);
    QVERIFY(snap.columns.isEmpty());
    QCOMPARE(snap.activeColumnIndex, -1);
}

void TestScrollEngineSnapshot::overWideColumnClampsFractionToOne()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a")});
    // Force the active column wider than the 1200px work area.
    engine->setColumnWidth(ColumnWidth::makeFixed(2000), QStringLiteral("S1"));

    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 1);
    // The share is clamped to EXACTLY 1: a 2000px intent on a 1200px work
    // area fills the whole viewport, so the preview must show the full
    // visible share, matching the committed clip. The old range assertion
    // (> 0, <= 1) also passed for a fraction that silently degraded to 0.5.
    QCOMPARE(snap.columns.at(0).widthFraction, 1.0);
}

void TestScrollEngineSnapshot::invalidWorkAreaAnswersInvalid()
{
    QObject owner;
    // A geometry provider answering an invalid rect: the snapshot must
    // report invalid (distinct from a valid, empty strip).
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")}, [](const QString&) {
        return QRect();
    });
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a")});
    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"));
    QVERIFY(!snap.valid);
}

void TestScrollEngineSnapshot::gapsShareTheColumnCrossExtent()
{
    // The zero-gap fixture cannot observe a gap-dependent defect (its own
    // header says so, and that blind spot hid a real drop-indicator bug), so
    // pin the rel math under a NON-ZERO gap: two stacked tiles must NOT sum
    // to the column's full CROSS extent — the gap between them takes its
    // share. relRect is ROLE-normalized (x/width along the strip, y/height
    // across it), so the .height() reads below mean the cross fraction on
    // either axis.
    QObject owner;
    ScrollEngine* engine = ScrollTestUtils::makeGappedProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->strip().takeWindow(QStringLiteral("c"), ScrollTestUtils::gappedEngineParams()));
    QVERIFY(state->strip().insertWindowIntoColumnAt(1, 1, QStringLiteral("c"), ScrollTestUtils::gappedEngineParams()));

    const ScrollStripSnapshot snap = engine->stripSnapshot(QStringLiteral("S1"));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 2);
    const ScrollStripSnapshotColumn& stacked = snap.columns.at(1);
    QCOMPARE(stacked.tiles.size(), 2);
    const qreal leadCross = stacked.tiles.at(0).relRect.height();
    const qreal trailCross = stacked.tiles.at(1).relRect.height();
    QVERIFY(leadCross > 0.0);
    QVERIFY(trailCross > 0.0);
    QVERIFY2(leadCross + trailCross < 1.0, "the inner gap must take its share of the column's cross extent");
    // And the second tile starts past the first plus the gap ACROSS the
    // column, not flush against it.
    QVERIFY(stacked.tiles.at(1).relRect.y() > leadCross);
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
    // The size compare alone cannot separate a correct implementation from a
    // double-applying one (b is already detached either way): pin the ids
    // and the active index against the no-exclude snapshot too.
    QCOMPARE(snapWithExclude.columns.size(), 2);
    QCOMPARE(snapWithExclude.columns.at(0).tiles.at(0).windowId, QStringLiteral("a"));
    QCOMPARE(snapWithExclude.columns.at(1).tiles.at(0).windowId, QStringLiteral("c"));
    QCOMPARE(snapWithExclude.activeColumnIndex, snap.activeColumnIndex);
    engine->cancelDragInsertPreview();
}

void TestScrollEngineSnapshot::keyOverloadMatchesCurrentContextAndFillsAbsRects()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // A stacked column too, so the cross-axis rects are exercised.
    QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));

    const ScrollStripSnapshot byScreen = engine->stripSnapshot(QStringLiteral("S1"));
    const ScrollStripSnapshot byKey = engine->stripSnapshot(keyFor(QStringLiteral("S1"), 1));
    QVERIFY(byKey.valid);
    QCOMPARE(byKey.activeColumnIndex, byScreen.activeColumnIndex);
    QCOMPARE(byKey.columns.size(), byScreen.columns.size());
    const QRect workArea = engineParams().workArea;
    for (int ci = 0; ci < byKey.columns.size(); ++ci) {
        const ScrollStripSnapshotColumn& kc = byKey.columns.at(ci);
        const ScrollStripSnapshotColumn& sc = byScreen.columns.at(ci);
        QCOMPARE(kc.tabbed, sc.tabbed);
        QCOMPARE(kc.widthFraction, sc.widthFraction);
        QCOMPARE(kc.tiles.size(), sc.tiles.size());
        // The screenId overload leaves the absolute rects null; the key
        // overload fills every one and they sit inside the work area for
        // an unscrolled strip.
        QVERIFY(sc.absRect.isNull());
        QVERIFY(!kc.absRect.isNull());
        QVERIFY(workArea.contains(kc.absRect));
        for (int ti = 0; ti < kc.tiles.size(); ++ti) {
            const ScrollStripSnapshotTile& kt = kc.tiles.at(ti);
            const ScrollStripSnapshotTile& st = sc.tiles.at(ti);
            QCOMPARE(kt.windowId, st.windowId);
            QCOMPARE(kt.activeTab, st.activeTab);
            QCOMPARE(kt.relRect, st.relRect);
            QVERIFY(st.absRect.isNull());
            QVERIFY(!kt.absRect.isNull());
            QVERIFY(kc.absRect.contains(kt.absRect));
        }
    }
    // The two stacked tiles split the column's cross extent, in order.
    const ScrollStripSnapshotColumn& stacked = byKey.columns.at(1);
    QCOMPARE(stacked.tiles.size(), 2);
    QVERIFY(ScrollTestUtils::Ax::crossPos(stacked.tiles.at(1).absRect)
            > ScrollTestUtils::Ax::crossPos(stacked.tiles.at(0).absRect));
    QCOMPARE(byKey.viewX, state->strip().relayout(engineParams()).viewOffset);
    QCOMPARE(byScreen.viewX, 0);
}

void TestScrollEngineSnapshot::keyOverloadAnswersANonCurrentContext()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    const ScrollStripSnapshot whileCurrent = engine->stripSnapshot(keyFor(QStringLiteral("S1"), 2));
    QVERIFY(whileCurrent.valid);
    QCOMPARE(whileCurrent.columns.size(), 2);

    // Desktop 1 is current again: the screenId overload now describes an
    // empty desktop 1, and only the key overload can still see desktop 2.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    QVERIFY(engine->stripSnapshot(QStringLiteral("S1")).columns.isEmpty());
    const ScrollStripSnapshot hidden = engine->stripSnapshot(keyFor(QStringLiteral("S1"), 2));
    QVERIFY(hidden.valid);
    QCOMPARE(hidden.columns.size(), 2);
    QCOMPARE(hidden.columns.at(0).tiles.at(0).windowId, QStringLiteral("a"));
    QCOMPARE(hidden.columns.at(1).tiles.at(0).windowId, QStringLiteral("b"));
    QCOMPARE(hidden.activeColumnIndex, whileCurrent.activeColumnIndex);
    QCOMPARE(hidden.viewX, whileCurrent.viewX);
    for (int ci = 0; ci < hidden.columns.size(); ++ci) {
        QCOMPARE(hidden.columns.at(ci).absRect, whileCurrent.columns.at(ci).absRect);
        QCOMPARE(hidden.columns.at(ci).tiles.at(0).absRect, whileCurrent.columns.at(ci).tiles.at(0).absRect);
    }

    // And the same answer once the context is current again.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    const ScrollStripSnapshot again = engine->stripSnapshot(keyFor(QStringLiteral("S1"), 2));
    QCOMPARE(again.columns.size(), hidden.columns.size());
    QCOMPARE(again.viewX, hidden.viewX);
    for (int ci = 0; ci < again.columns.size(); ++ci) {
        QCOMPARE(again.columns.at(ci).absRect, hidden.columns.at(ci).absRect);
    }
}

void TestScrollEngineSnapshot::keyOverloadPlacesAParkedColumnOutsideTheWorkArea()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    // Three half-width columns overflow the viewport by one column.
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // Pan to the strip's far end. The result is not asserted: the centering
    // policy may already have the view there after c opened, in which case
    // the pan is refused and the first column is already parked.
    state->strip().scrollViewBy(ScrollTestUtils::kMainExtent, engineParams());

    const ScrollStripSnapshot snap = engine->stripSnapshot(keyFor(QStringLiteral("S1"), 1));
    QVERIFY(snap.valid);
    QCOMPARE(snap.columns.size(), 3);
    const QRect workArea = engineParams().workArea;
    const QRect parked = snap.columns.at(0).absRect;
    QVERIFY(!parked.isNull());
    // Outside the work area BEFORE its main-axis start: that is where a
    // scrolled-past column lives in strip space, and the overview draws it
    // there rather than at the compositor's park position.
    QVERIFY(!parked.intersects(workArea));
    QVERIFY(ScrollTestUtils::Ax::mainPos(parked) < ScrollTestUtils::Ax::mainPos(workArea));
    QVERIFY(!snap.columns.at(0).tiles.at(0).absRect.intersects(workArea));
    // The last column is on screen.
    QVERIFY(workArea.contains(snap.columns.at(2).absRect));
    // And the strip-space position round-trips through viewX.
    QCOMPARE(ScrollTestUtils::Ax::mainPos(parked) + snap.viewX, ScrollTestUtils::Ax::mainPos(workArea));
}

void TestScrollEngineSnapshot::keyOverloadMutatesNeitherActiveColumnNorView()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // Focus the first column so the view is somewhere other than the end,
    // then detach it with a pan: a read that re-ran the centering policy
    // would move it back.
    engine->windowFocused(QStringLiteral("a"), QStringLiteral("S1"));
    state->strip().scrollViewBy(100, engineParams());
    const int activeBefore = state->strip().activeColumnIndex();
    const int viewBefore = state->strip().relayout(engineParams()).viewOffset;
    const QString focusBefore = state->strip().activeWindowId();

    const ScrollStripSnapshot snap = engine->stripSnapshot(keyFor(QStringLiteral("S1"), 1));
    QVERIFY(snap.valid);
    const auto windows = engine->overviewWindowsFor(keyFor(QStringLiteral("S1"), 1));
    const auto strip = engine->overviewStripFor(keyFor(QStringLiteral("S1"), 1));
    QVERIFY(windows.has_value());
    QVERIFY(strip.has_value());

    QCOMPARE(state->strip().activeColumnIndex(), activeBefore);
    QCOMPARE(state->strip().relayout(engineParams()).viewOffset, viewBefore);
    QCOMPARE(state->strip().activeWindowId(), focusBefore);
    QCOMPARE(snap.viewX, viewBefore);
}

void TestScrollEngineSnapshot::neverCreatedKeyAnswersInvalidAndNullopt()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a")});

    // A desktop this screen never visited, and a screen the engine does
    // not own. Neither read may create the state as a side effect, so the
    // second read of each answers the same.
    for (const PhosphorEngine::PlacementStateKey& key :
         {keyFor(QStringLiteral("S1"), 7), keyFor(QStringLiteral("S9"), 1)}) {
        for (int pass = 0; pass < 2; ++pass) {
            QVERIFY(!engine->stripSnapshot(key).valid);
            QVERIFY(!engine->overviewWindowsFor(key).has_value());
            QVERIFY(!engine->overviewStripFor(key).has_value());
        }
    }
    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{1}));
    // The real context is untouched by the misses.
    QVERIFY(engine->overviewWindowsFor(keyFor(QStringLiteral("S1"), 1)).has_value());
}

void TestScrollEngineSnapshot::overviewWindowsListsTilesOnceWithIndicesAndFloats()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"),
                {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("f")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));
    engine->setWindowFloat(QStringLiteral("f"), true, QStringLiteral("S1"));
    QVERIFY(state->isFloating(QStringLiteral("f")));

    const auto entries = engine->overviewWindowsFor(keyFor(QStringLiteral("S1"), 1));
    QVERIFY(entries.has_value());
    QCOMPARE(entries->size(), 4);
    // Strip model order: a (0,0), b (1,0), c (1,1), then the float.
    QCOMPARE(entries->at(0).windowId, QStringLiteral("a"));
    QCOMPARE(entries->at(0).column, 0);
    QCOMPARE(entries->at(0).tile, 0);
    QCOMPARE(entries->at(1).windowId, QStringLiteral("b"));
    QCOMPARE(entries->at(1).column, 1);
    QCOMPARE(entries->at(1).tile, 0);
    QCOMPARE(entries->at(2).windowId, QStringLiteral("c"));
    QCOMPARE(entries->at(2).column, 1);
    QCOMPARE(entries->at(2).tile, 1);
    const ScrollStripSnapshot snap = engine->stripSnapshot(keyFor(QStringLiteral("S1"), 1));
    for (int i = 0; i < 3; ++i) {
        const PhosphorEngine::OverviewWindowEntry& e = entries->at(i);
        QVERIFY(!e.floating);
        QVERIFY(!e.minimized);
        QVERIFY(!e.rect.isNull());
        QCOMPARE(e.rect, snap.columns.at(e.column).tiles.at(e.tile).absRect);
    }
    const PhosphorEngine::OverviewWindowEntry& floated = entries->at(3);
    QCOMPARE(floated.windowId, QStringLiteral("f"));
    QVERIFY(floated.floating);
    QCOMPARE(floated.column, -1);
    QCOMPARE(floated.tile, -1);
    // Every window exactly once.
    QSet<QString> seen;
    for (const PhosphorEngine::OverviewWindowEntry& e : *entries) {
        QVERIFY(!seen.contains(e.windowId));
        seen.insert(e.windowId);
    }
}

void TestScrollEngineSnapshot::overviewStripCarriesTheSnapshotRects()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(stackUnder(state, 1, 1, QStringLiteral("c")));
    QVERIFY(state->strip().toggleActiveColumnTabbed());
    state->strip().scrollViewBy(100, engineParams());

    const ScrollStripSnapshot snap = engine->stripSnapshot(keyFor(QStringLiteral("S1"), 1));
    const auto strip = engine->overviewStripFor(keyFor(QStringLiteral("S1"), 1));
    const auto windows = engine->overviewWindowsFor(keyFor(QStringLiteral("S1"), 1));
    QVERIFY(strip.has_value());
    QVERIFY(windows.has_value());
    QCOMPARE(strip->viewOffset, snap.viewX);
    QCOMPARE(strip->columns.size(), snap.columns.size());
    const QRect workArea = engineParams().workArea;
    for (int ci = 0; ci < strip->columns.size(); ++ci) {
        const PhosphorEngine::OverviewStripColumn& oc = strip->columns.at(ci);
        const ScrollStripSnapshotColumn& sc = snap.columns.at(ci);
        QCOMPARE(oc.rect, sc.absRect);
        QCOMPARE(oc.tabbed, sc.tabbed);
        QCOMPARE(oc.tiles.size(), sc.tiles.size());
        // The column's strip-space position is its rect's main position
        // plus the view offset, and its on-screen position is the rect's.
        QCOMPARE(ScrollTestUtils::Ax::mainPos(oc.rect) + strip->viewOffset - snap.viewX,
                 ScrollTestUtils::Ax::mainPos(sc.absRect));
        for (int ti = 0; ti < oc.tiles.size(); ++ti) {
            QCOMPARE(oc.tiles.at(ti).windowId, sc.tiles.at(ti).windowId);
            QCOMPARE(oc.tiles.at(ti).rect, sc.tiles.at(ti).absRect);
            // And the same rect the window entry reports for that slot.
            for (const PhosphorEngine::OverviewWindowEntry& e : *windows) {
                if (e.column == ci && e.tile == ti) {
                    QCOMPARE(e.rect, oc.tiles.at(ti).rect);
                }
            }
        }
    }
    // The tabbed column: activeTab names the visible tab, and its hidden
    // tab carries the shared rect rather than none.
    const PhosphorEngine::OverviewStripColumn& tabbed = strip->columns.at(1);
    QVERIFY(tabbed.tabbed);
    QCOMPARE(tabbed.tiles.size(), 2);
    QVERIFY(snap.columns.at(1).tiles.at(tabbed.activeTab).activeTab);
    QCOMPARE(tabbed.tiles.at(0).rect, tabbed.tiles.at(1).rect);
    QVERIFY(!tabbed.tiles.at(0).rect.isNull());
    QVERIFY(workArea.intersects(tabbed.rect));
}

QTEST_GUILESS_MAIN(TestScrollEngineSnapshot)
#include "test_scrollengine_snapshot.moc"
