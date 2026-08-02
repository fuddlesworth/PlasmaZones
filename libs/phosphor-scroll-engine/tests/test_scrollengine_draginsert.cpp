// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Drag-insert state machine on ScrollEngine — DETACH-ONCE semantics: begin
// detaches the dragged window from the strip (one settle), update only
// remembers the hit-tested target against the now-stable strip, commit
// applies the structure once at drop, cancel restores the captured slot.
// Covers all entry modes (same-screen tile, stacked tile, same-screen
// floating, cross-screen, fresh adoption), the point→target hit-test, edge
// auto-scroll, the interactive-drag mark, and the invalidation hooks.
//
// Windows are registered through windowOpened() so the engine's reverse map
// is populated — that is what tells the entry modes apart.

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
    void beginDetachesFromStrip();
    void updateStoresTargetWithoutRestructuring();
    void commitNewColumnAtTarget();
    void commitJoinColumnStacks();
    void joinRightColumnIsStableAcrossTicks();
    void commitWithoutTargetRestoresSlot();
    void cancelRestoresSoloColumnSlot();
    void cancelRestoresStackedTileSlot();
    void floatingBeginDetachesAndCancelRefloats();
    void floatingCommitEmitsSyncOnce();
    void crossScreenCancelReturnsHome();
    void crossScreenCommitAdopts();
    void freshAdoptionStaysUntrackedUntilCommit();
    void hitTestResolvesTargets();
    void nudgeDragScrollShiftsView();
    void windowClosedDropsPreview();
    void screenSetChangeCancelsPreview();
    void interactiveDragMarkSuppressesEmitAndReconcile();

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

void TestScrollEngineDragInsert::beginDetachesFromStrip()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    QVERIFY(engine->hasDragInsertPreview());
    QCOMPARE(engine->dragInsertPreviewWindowId(), QStringLiteral("b"));
    QCOMPARE(engine->dragInsertPreviewScreenId(), QStringLiteral("S1"));

    // The window left the strip model (neighbours closed up once); it stays
    // TRACKED so daemon routing keeps answering, and no geometry batch can
    // carry it — KWin's interactive move owns the frame.
    QVERIFY(!state->strip().containsWindow(QStringLiteral("b")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("b")));
    QVERIFY(!engine->isWindowTiled(QStringLiteral("b")));
    for (const auto& emission : tiledSpy) {
        QVERIFY(!emission.first().toString().contains(QStringLiteral("\"b\"")));
    }
    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::updateStoresTargetWithoutRestructuring()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    const QStringList detached{QStringLiteral("a"), QStringLiteral("c")};
    QCOMPARE(state->strip().windowsInOrder(), detached);

    // Updates never restructure — the strip is stable for the whole hold.
    DragTarget target;
    target.primary = 0;
    target.newSlot = true;
    engine->updateDragInsertPreview(target);
    QCOMPARE(state->strip().windowsInOrder(), detached);
    target.primary = 1;
    target.newSlot = false;
    target.secondary = 0;
    engine->updateDragInsertPreview(target);
    QCOMPARE(state->strip().windowsInOrder(), detached);
    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::commitNewColumnAtTarget()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    DragTarget target;
    target.primary = 0;
    target.newSlot = true;
    engine->updateDragInsertPreview(target);

    QSignalSpy syncSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    engine->commitDragInsertPreview();
    QVERIFY(!engine->hasDragInsertPreview());
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")}));
    // Same-screen tiled reorder: no float bookkeeping changed, no sync.
    QCOMPARE(syncSpy.count(), 0);
    // The commit relayout finally emits the dropped window's rect.
    QVERIFY(tiledSpy.count() >= 1);
    QVERIFY(tiledSpy.last().first().toString().contains(QStringLiteral("\"b\"")));
}

void TestScrollEngineDragInsert::commitJoinColumnStacks()
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
    engine->commitDragInsertPreview();

    QCOMPARE(state->strip().columnOfWindow(QStringLiteral("c")), state->strip().columnOfWindow(QStringLiteral("a")));
    QCOMPARE(state->strip().columnCount(), 2);
}

void TestScrollEngineDragInsert::joinRightColumnIsStableAcrossTicks()
{
    // The scenario that killed the live-restructure design twice: two
    // columns, drag the LEFT window onto the RIGHT column to stack. With
    // detach-once the strip settles at begin (b slides to column 0) and a
    // stationary cursor over b's settled rect resolves to the SAME join
    // target on every tick — nothing can slide out from under it.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));
    const QRect rectB = tileRect(engine, QStringLiteral("S1"), QStringLiteral("b"));
    QVERIFY(!rectB.isNull());
    const QPoint cursor = rectB.center();

    const DragTarget first = engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), cursor);
    QCOMPARE(first.primary, 0);
    QVERIFY(!first.newSlot);
    engine->updateDragInsertPreview(first);
    const DragTarget second = engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), cursor);
    QVERIFY(second == first);

    engine->commitDragInsertPreview();
    QCOMPARE(state->strip().columnOfWindow(QStringLiteral("a")), state->strip().columnOfWindow(QStringLiteral("b")));
    QCOMPARE(state->strip().columnCount(), 1);
}

void TestScrollEngineDragInsert::commitWithoutTargetRestoresSlot()
{
    // Zero-motion drop (eager reorder seed, released in place): no target
    // was ever resolved, so commit returns the window to its captured slot
    // instead of an arbitrary append.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    engine->commitDragInsertPreview();
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
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
    QVERIFY(!state->strip().containsWindow(QStringLiteral("b")));

    engine->cancelDragInsertPreview();
    // b returns to a's stack at its remembered tile slot, via the
    // surviving-sibling anchor.
    QCOMPARE(state->strip().columnOfWindow(QStringLiteral("b")), state->strip().columnOfWindow(QStringLiteral("a")));
    const Column& column = state->strip().columns().at(state->strip().columnOfWindow(QStringLiteral("b")));
    QCOMPARE(column.indexOfWindow(QStringLiteral("b")), 1);
}

void TestScrollEngineDragInsert::floatingBeginDetachesAndCancelRefloats()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    engine->setWindowFloat(QStringLiteral("b"), true, QStringLiteral("S1"));
    QVERIFY(!engine->isWindowTiled(QStringLiteral("b")));

    QSignalSpy floatSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    // Detached silently — no adoption, no float signal.
    QVERIFY(!engine->isWindowTiled(QStringLiteral("b")));
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
    // Detached from home; the target strip is untouched until commit; the
    // window routes to the target screen while the hold lasts.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("a")}));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S2")), (QStringList{QStringLiteral("d")}));
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("b")), QStringLiteral("S2"));

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

void TestScrollEngineDragInsert::freshAdoptionStaysUntrackedUntilCommit()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("ghost"), QStringLiteral("S1")));
    // Nothing was touched at begin, so a cancel has nothing to restore and
    // the window stays foreign until a commit actually adopts it.
    QVERIFY(!engine->isWindowTracked(QStringLiteral("ghost")));
    engine->cancelDragInsertPreview();
    QVERIFY(!engine->isWindowTracked(QStringLiteral("ghost")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("a")}));

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("ghost"), QStringLiteral("S1")));
    DragTarget target;
    target.primary = 1;
    target.newSlot = true;
    engine->updateDragInsertPreview(target);
    engine->commitDragInsertPreview();
    QVERIFY(engine->isWindowTracked(QStringLiteral("ghost")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("a"), QStringLiteral("ghost")}));
}

void TestScrollEngineDragInsert::hitTestResolvesTargets()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});

    // S2 has no state at all yet — invalid target.
    const DragTarget empty = engine->computeDragInsertTargetAtPoint(QStringLiteral("S2"), QPoint(100, 100));
    QVERIFY(!empty.isValid());

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));
    // a detached: b settled as the only column (index 0).
    const QRect rectB = tileRect(engine, QStringLiteral("S1"), QStringLiteral("b"));
    QVERIFY(!rectB.isNull());

    // Middle of b: join it as a tile.
    const DragTarget join = engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), rectB.center());
    QCOMPARE(join.primary, 0);
    QVERIFY(!join.newSlot);
    QVERIFY(join.secondary >= 0);

    // b's left edge band: a new column before it.
    const DragTarget before =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectB.left() + 4, rectB.center().y()));
    QCOMPARE(before.primary, 0);
    QVERIFY(before.newSlot);

    // b's right edge band: a new column after it.
    const DragTarget after =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectB.right() - 4, rectB.center().y()));
    QCOMPARE(after.primary, 1);
    QVERIFY(after.newSlot);

    // Right of the whole (short) strip: append.
    const DragTarget append =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectB.right() + 200, rectB.center().y()));
    QCOMPARE(append.primary, 1);
    QVERIFY(append.newSlot);

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

    // Detaching a leaves b,c,d — still wider than the 1200px viewport.
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));
    const int anchorBefore = state->strip().viewAnchor();
    // Center of the work area: outside both bands.
    QVERIFY(!engine->nudgeDragScroll(QStringLiteral("S1"), QPoint(600, 400)));
    QCOMPARE(state->strip().viewAnchor(), anchorBefore);
    // Detaching the leftmost column re-clamped the view off the strip's
    // right end, so the right band has room and slides the view a step...
    QVERIFY(engine->nudgeDragScroll(QStringLiteral("S1"), QPoint(1195, 400)));
    QVERIFY(state->strip().viewAnchor() != anchorBefore);
    // ...and the left band scrolls back the other way.
    QVERIFY(engine->nudgeDragScroll(QStringLiteral("S1"), QPoint(5, 400)));
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

void TestScrollEngineDragInsert::interactiveDragMarkSuppressesEmitAndReconcile()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    engine->setInteractiveDragWindow(QStringLiteral("a"));

    // Retiles while the mark is set never emit the marked window's rect —
    // KWin's interactive move owns the frame, and re-emitting the slot rect
    // was the mid-drag teleport fight.
    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    engine->retile(QStringLiteral("S1"));
    for (const auto& emission : tiledSpy) {
        QVERIFY(!emission.first().toString().contains(QStringLiteral("\"a\"")));
    }

    // A drag-frame ack for the marked window must not reconcile: without
    // the gate this pinned the column width intent to the transient drag
    // rect (Fixed pixels) and scheduled another fighting retile.
    const ColumnWidth widthBefore =
        state->strip().columns().at(state->strip().columnOfWindow(QStringLiteral("a"))).width;
    engine->onWindowResized(QStringLiteral("a"), QRect(0, 0, 595, 800), QRect(400, 300, 300, 200),
                            QStringLiteral("S1"));
    const ColumnWidth widthAfter =
        state->strip().columns().at(state->strip().columnOfWindow(QStringLiteral("a"))).width;
    QVERIFY(widthBefore == widthAfter);

    // Clearing the mark restores normal emission on the next retile. Change
    // a's column width so emit-on-change genuinely has a new rect to say
    // (the marked retile retained a's last-applied memory, so an identical
    // relayout would stay silent for every window).
    engine->setInteractiveDragWindow(QString());
    tiledSpy.clear();
    state->strip().focusWindow(QStringLiteral("a"), defaultParams());
    state->strip().setActiveColumnWidth(ColumnWidth::makeProportion(0.4));
    engine->retile(QStringLiteral("S1"));
    bool emittedA = false;
    for (const auto& emission : tiledSpy) {
        emittedA = emittedA || emission.first().toString().contains(QStringLiteral("\"a\""));
    }
    QVERIFY(emittedA);
}

QTEST_GUILESS_MAIN(TestScrollEngineDragInsert)
#include "test_scrollengine_draginsert.moc"
