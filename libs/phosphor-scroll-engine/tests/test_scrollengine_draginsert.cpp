// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Drag-insert preview state machine on ScrollEngine — the scrolling mirror
// of the daemon tree's test_autotile_drag_insert. Covers begin/update/
// commit/cancel across same-screen reorder (solo column and stacked tile),
// same-screen floating adoption, cross-screen adoption, and fresh adoption,
// plus the point→target hit-test, the applyLayout skip contract, edge
// auto-scroll, and the invalidation hooks (window close, screen-set change).
//
// Windows are registered through windowOpened() so the engine's reverse map
// is populated — that is what tells the same-screen / cross-screen / fresh
// adoption paths apart.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::defaultParams;
using ScrollTestUtils::makeProviderEngine;

using DragTarget = PhosphorEngine::IPlacementEngine::DragInsertTarget;

class TestScrollEngineDragInsert : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void beginRejectsInvalidInputs();
    void sameScreenBeginKeepsSlotAndSkipsGeometry();
    void updateNewColumnMovesAndCommitPersists();
    void updateJoinColumnStacks();
    void joinRightColumnStaysUnderCursor();
    void cancelRestoresSoloColumnSlot();
    void cancelRestoresStackedTileSlot();
    void floatingBeginAdoptsAndCancelRefloats();
    void floatingCommitEmitsSyncOnce();
    void crossScreenCancelReturnsHome();
    void crossScreenCommitAdopts();
    void freshAdoptionCancelRemovesTracking();
    void hitTestResolvesTargets();
    void nudgeDragScrollShiftsView();
    void windowClosedDropsPreview();
    void screenSetChangeCancelsPreview();

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

    /// The rect of @p windowId among visibleTiles, or a null rect.
    static QRect tileRect(ScrollEngine* engine, const QString& screenId, const QString& windowId)
    {
        const auto tiles = engine->visibleTiles(screenId);
        for (const auto& tile : tiles) {
            if (tile.windowId == windowId) {
                return tile.rect;
            }
        }
        return {};
    }
};

void TestScrollEngineDragInsert::beginRejectsInvalidInputs()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a")});

    QVERIFY(!engine->beginDragInsertPreview(QString(), QStringLiteral("S1")));
    QVERIFY(!engine->beginDragInsertPreview(QStringLiteral("a"), QString()));
    QVERIFY(!engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S9")));
    QVERIFY(!engine->hasDragInsertPreview());
}

void TestScrollEngineDragInsert::sameScreenBeginKeepsSlotAndSkipsGeometry()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    QVERIFY(engine->hasDragInsertPreview());
    QCOMPARE(engine->dragInsertPreviewWindowId(), QStringLiteral("b"));
    QCOMPARE(engine->dragInsertPreviewScreenId(), QStringLiteral("S1"));
    // No structural change at begin for a same-screen tiled reorder.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));

    // The applyLayout skip contract: while the preview is live, geometry
    // batches never carry the dragged window (KWin's interactive move owns
    // it). Force a batch by moving the window to a new slot.
    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    DragTarget target;
    target.primary = 0;
    target.newSlot = true;
    engine->updateDragInsertPreview(target);
    QVERIFY(tiledSpy.count() >= 1);
    for (const auto& emission : tiledSpy) {
        QVERIFY(!emission.first().toString().contains(QStringLiteral("\"b\"")));
    }
    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::updateNewColumnMovesAndCommitPersists()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    DragTarget target;
    target.primary = 0;
    target.newSlot = true;
    engine->updateDragInsertPreview(target);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")}));

    QSignalSpy syncSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    engine->commitDragInsertPreview();
    QVERIFY(!engine->hasDragInsertPreview());
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")}));
    // Same-screen tiled reorder: no float bookkeeping changed, no sync.
    QCOMPARE(syncSpy.count(), 0);
    // The commit relayout runs unfiltered — the dragged window's rect is
    // finally emitted.
    QVERIFY(tiledSpy.count() >= 1);
    QVERIFY(tiledSpy.last().first().toString().contains(QStringLiteral("\"b\"")));
}

void TestScrollEngineDragInsert::updateJoinColumnStacks()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("c"), QStringLiteral("S1")));
    DragTarget target;
    target.primary = 0;
    target.secondary = 0;
    engine->updateDragInsertPreview(target);
    QCOMPARE(state->strip().columnOfWindow(QStringLiteral("c")), state->strip().columnOfWindow(QStringLiteral("a")));

    engine->commitDragInsertPreview();
    QCOMPARE(state->strip().columnOfWindow(QStringLiteral("c")), state->strip().columnOfWindow(QStringLiteral("a")));
    QCOMPARE(state->strip().columnCount(), 2);
}

void TestScrollEngineDragInsert::joinRightColumnStaysUnderCursor()
{
    // Two columns, drag the LEFT window onto the RIGHT column. The join
    // removes the dragged window's own column, contracting the strip
    // leftward — without the join-column view pin, the merged column slides
    // out from under the stationary cursor, the next tick's hit-test reads
    // "right of the strip", and the window is expelled straight back out
    // (the right-column stack could never be formed).
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));
    const QRect rectB = tileRect(engine, QStringLiteral("S1"), QStringLiteral("b"));
    QVERIFY(!rectB.isNull());
    const QPoint cursor = rectB.center();

    const DragTarget join = engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), cursor);
    QCOMPARE(join.primary, 1);
    QVERIFY(!join.newSlot);
    engine->updateDragInsertPreview(join);
    QCOMPARE(state->strip().columnOfWindow(QStringLiteral("a")), state->strip().columnOfWindow(QStringLiteral("b")));

    // The STATIONARY cursor must still resolve inside the merged column —
    // never to a new column beyond the strip end.
    const DragTarget next = engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), cursor);
    QVERIFY(!next.newSlot);
    engine->updateDragInsertPreview(next);
    QCOMPARE(state->strip().columnOfWindow(QStringLiteral("a")), state->strip().columnOfWindow(QStringLiteral("b")));
    QCOMPARE(state->strip().columnCount(), 1);
    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::cancelRestoresSoloColumnSlot()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    DragTarget target;
    target.primary = 0;
    target.newSlot = true;
    engine->updateDragInsertPreview(target);

    engine->cancelDragInsertPreview();
    QVERIFY(!engine->hasDragInsertPreview());
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 3);
}

void TestScrollEngineDragInsert::cancelRestoresStackedTileSlot()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // Restructure b into a's column (tile 1) while the reverse map keeps
    // tracking it — the stacked-tile begin path.
    QVERIFY(state->strip().takeWindow(QStringLiteral("b"), defaultParams()));
    QVERIFY(state->strip().insertWindowIntoColumnAt(0, 1, QStringLiteral("b"), defaultParams()));

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    DragTarget target;
    target.primary = 2;
    target.newSlot = true;
    engine->updateDragInsertPreview(target);
    QVERIFY(state->strip().columnOfWindow(QStringLiteral("b")) != state->strip().columnOfWindow(QStringLiteral("a")));

    engine->cancelDragInsertPreview();
    // b returns to a's stack at its remembered tile slot, via the
    // surviving-sibling anchor.
    QCOMPARE(state->strip().columnOfWindow(QStringLiteral("b")), state->strip().columnOfWindow(QStringLiteral("a")));
    const Column& column = state->strip().columns().at(state->strip().columnOfWindow(QStringLiteral("b")));
    QCOMPARE(column.indexOfWindow(QStringLiteral("b")), 1);
}

void TestScrollEngineDragInsert::floatingBeginAdoptsAndCancelRefloats()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    engine->setWindowFloat(QStringLiteral("b"), true, QStringLiteral("S1"));
    QVERIFY(!engine->isWindowTiled(QStringLiteral("b")));

    QSignalSpy floatSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    // Silently adopted into its remembered slot; no float signal fired.
    QVERIFY(engine->isWindowTiled(QStringLiteral("b")));
    QCOMPARE(floatSpy.count(), 0);

    engine->cancelDragInsertPreview();
    QVERIFY(!engine->isWindowTiled(QStringLiteral("b")));
    QCOMPARE(floatSpy.count(), 0);
    // The FloatRestore entry went back intact: a later unfloat still
    // restores the remembered slot between a and c.
    engine->setWindowFloat(QStringLiteral("b"), false, QStringLiteral("S1"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
}

void TestScrollEngineDragInsert::floatingCommitEmitsSyncOnce()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    engine->setWindowFloat(QStringLiteral("b"), true, QStringLiteral("S1"));

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    QSignalSpy syncSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
    engine->commitDragInsertPreview();
    QCOMPARE(syncSpy.count(), 1);
    QCOMPARE(syncSpy.first().at(0).toString(), QStringLiteral("b"));
    QCOMPARE(syncSpy.first().at(1).toBool(), false);
    QVERIFY(engine->isWindowTiled(QStringLiteral("b")));
}

void TestScrollEngineDragInsert::crossScreenCancelReturnsHome()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    openWindows(engine, QStringLiteral("S2"), {QStringLiteral("d")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S2")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("a")}));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S2")), (QStringList{QStringLiteral("d"), QStringLiteral("b")}));

    engine->cancelDragInsertPreview();
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S2")), (QStringList{QStringLiteral("d")}));
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("b")), QStringLiteral("S1"));
}

void TestScrollEngineDragInsert::crossScreenCommitAdopts()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    openWindows(engine, QStringLiteral("S2"), {QStringLiteral("d")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S2")));
    QSignalSpy syncSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
    engine->commitDragInsertPreview();
    QCOMPARE(syncSpy.count(), 1);
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("b")), QStringLiteral("S2"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S2")), (QStringList{QStringLiteral("d"), QStringLiteral("b")}));
}

void TestScrollEngineDragInsert::freshAdoptionCancelRemovesTracking()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("ghost"), QStringLiteral("S1")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("ghost")));

    engine->cancelDragInsertPreview();
    QVERIFY(!engine->isWindowTracked(QStringLiteral("ghost")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("a")}));
}

void TestScrollEngineDragInsert::hitTestResolvesTargets()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});

    // Empty strip: everything is "open the first column".
    const DragTarget empty = engine->computeDragInsertTargetAtPoint(QStringLiteral("S2"), QPoint(100, 100));
    QVERIFY(!engine->hasDragInsertPreview());
    // S2 has no state at all yet — invalid target.
    QVERIFY(!empty.isValid());

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));

    // Over the dragged window's own solo column (center): the stable
    // identity — the current target verbatim, never an oscillating newSlot.
    const QRect rectA = tileRect(engine, QStringLiteral("S1"), QStringLiteral("a"));
    QVERIFY(!rectA.isNull());
    const DragTarget own = engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), rectA.center());
    QCOMPARE(own.primary, 0);
    QVERIFY(!own.newSlot);

    // Center of b's column (past the edge bands): join it as a tile.
    const QRect rectB = tileRect(engine, QStringLiteral("S1"), QStringLiteral("b"));
    QVERIFY(!rectB.isNull());
    const DragTarget join = engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), rectB.center());
    QCOMPARE(join.primary, 1);
    QVERIFY(!join.newSlot);
    QVERIFY(join.secondary >= 0);

    // b's left edge band: a new column between a and b.
    const DragTarget between =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectB.left() + 4, rectB.center().y()));
    QCOMPARE(between.primary, 1);
    QVERIFY(between.newSlot);

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::nudgeDragScrollShiftsView()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"),
                {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    // No preview: never scrolls.
    QVERIFY(!engine->nudgeDragScroll(QStringLiteral("S1"), QPoint(1195, 400)));

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));
    const int anchorBefore = state->strip().viewAnchor();
    // Center of the work area: outside both bands.
    QVERIFY(!engine->nudgeDragScroll(QStringLiteral("S1"), QPoint(600, 400)));
    QCOMPARE(state->strip().viewAnchor(), anchorBefore);
    // The view opens pinned at the strip's right end (the last-opened
    // window holds focus), so the right band is already at max and clamps
    // to a no-op...
    QVERIFY(!engine->nudgeDragScroll(QStringLiteral("S1"), QPoint(1195, 400)));
    // ...while the left band slides the view one step (the strip of four
    // half-width columns is wider than the 1200px viewport).
    QVERIFY(engine->nudgeDragScroll(QStringLiteral("S1"), QPoint(5, 400)));
    QVERIFY(state->strip().viewAnchor() != anchorBefore);
    // And once off the right end, the right band scrolls back.
    QVERIFY(engine->nudgeDragScroll(QStringLiteral("S1"), QPoint(1195, 400)));
    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::windowClosedDropsPreview()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    engine->windowClosed(QStringLiteral("b"));
    QVERIFY(!engine->hasDragInsertPreview());
    QVERIFY(!engine->isWindowTracked(QStringLiteral("b")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("a")}));
}

void TestScrollEngineDragInsert::screenSetChangeCancelsPreview()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    // S1 leaves the scrolling set: the preview must unwind BEFORE the state
    // teardown, not dangle into a released context.
    engine->setActiveScreens({QStringLiteral("S2")});
    QVERIFY(!engine->hasDragInsertPreview());
}

QTEST_GUILESS_MAIN(TestScrollEngineDragInsert)
#include "test_scrollengine_draginsert.moc"
