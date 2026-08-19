// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Drag-insert state machine on ScrollEngine — DETACH-ONCE semantics: begin
// detaches the dragged window from the strip (one settle), update only
// remembers the hit-tested target against the now-stable strip, commit
// applies the structure once at drop, cancel restores the captured slot.
// Covers all entry modes (same-screen tile, stacked tile, same-screen
// floating, cross-screen, fresh adoption), the point→target hit-test, the
// interactive-drag mark, the invalidation hooks, and the edge auto-scroll
// ramp that moves the view mid-drag while owning the drop target.
//
// Windows are registered through windowOpened() so the engine's reverse map
// is populated — that is what tells the entry modes apart.

#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::engineParams;
using ScrollTestUtils::makeGappedProviderEngine;
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
    void hitTestResolvesStackedTileSlots();
    void hitTestMapsThroughAMinimizedTile();
    void indicatorRectTracksTarget();
    void indicatorRectMatchesTheDropUnderAGap();
    void indicatorRectMatchesTheDropForANewColumn();
    void fullViewportOuterSlotIndicatorClampsToTheEdge();
    void scrolledStripEdgeSlotsMirror();
    void windowClosedDropsPreview();
    void screenSetChangeCancelsPreview();
    void interactiveDragMarkSuppressesEmitAndReconcile();
    void detachedResidueHealsInsteadOfLatching();
    void cancelRestoresADefensivelyDetachedWindow();
    void reentrantBeginRestoresPriorWindow();
    void neighbourCloseInvalidatesStaleTarget();
    void windowedFullscreenSurvivesCancelAndCommit();
    void windowedFullscreenSurvivesCrossScreenCommit();
    void edgeAutoScrollStaysInertWhenTheStripFits();
    void edgeAutoScrollHonoursTheStartDelay();
    void edgeAutoScrollKeepsTheTargetAtTheEdge();
    void edgeAutoScrollStampsItsBatchesImmediate();
    void edgeAutoScrollKeepsTheTabStripPayloadLive();
    void edgeAutoScrollAccumulatesSubPixelSpeed();
    void edgeAutoScrollClampsAtTheStripEnd();
    void edgeAutoScrollDisarmsOutsideTheBand();
    void edgeAutoScrollRepairsTheTargetOnLeavingTheBand();
    void edgeAutoScrollRightBandMirrorsTheLeft();
    void edgeAutoScrollCommitsTheSlotItPromised();
    void edgeAutoScrollClampsOneTickOfTravel();
    void edgeAutoScrollRefusesAForeignScreenOrNoPreview();
    void edgeAutoScrollKeepsOwnershipWhenPinned();

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

    /// Where the view sits, in strip pixels. The auto-scroll's whole job.
    static int viewX(ScrollEngine* engine, const QString& screenId)
    {
        return stateFor(engine, screenId)->strip().relayout(engineParams()).viewX;
    }

    /// The engine's own edge auto-scroll defaults, named rather than spelled
    /// as bare literals so a retune of IScrollSettings moves these with it
    /// instead of failing these tests obscurely.
    static constexpr int kTriggerWidth = PhosphorEngine::IScrollSettings::kDragScrollTriggerWidthDefault;
    static constexpr int kDelayMs = PhosphorEngine::IScrollSettings::kDragScrollDelayMsDefault;
    static constexpr int kMaxSpeed = PhosphorEngine::IScrollSettings::kDragScrollMaxSpeedDefault;
    /// Comfortably past kDelayMs, for the waits that must see it expire.
    static constexpr int kDelayWaitMs = kDelayMs + 20;

    /// One drag-scroll heartbeat at @p cursorX, carrying @p dtSeconds of
    /// elapsed time — the daemon's timer in miniature. @p screenId is
    /// parameterised so a test can drive a tick at a screen the preview does
    /// not belong to and watch the guard refuse it.
    static bool tick(ScrollEngine* engine, int cursorX, qreal dtSeconds = 0.016,
                     const QString& screenId = QStringLiteral("S1"))
    {
        return engine->dragAutoScrollTick(screenId, QPoint(cursorX, 300), dtSeconds);
    }

    /// Arm the band at @p cursorX and wait out the configured start delay,
    /// leaving the scroll owning the target. Works for EITHER band — the
    /// direction is whichever one @p cursorX falls in. Returns the view
    /// position at that point.
    static int armBand(ScrollEngine* engine, int cursorX)
    {
        // First tick only arms (it is the transition into the band), and
        // every tick inside the delay window declines to move.
        tick(engine, cursorX);
        QTest::qWait(kDelayWaitMs);
        tick(engine, cursorX);
        return viewX(engine, QStringLiteral("S1"));
    }
    /// A cursor at the leading band's outer edge, i.e. full ramp speed.
    static int leftBandX()
    {
        return ScrollTestUtils::defaultScreenRect().left();
    }
    /// A cursor at the trailing band's outermost contained pixel (QRect::right()
    /// is one inside the exclusive edge), i.e. also full ramp speed.
    static int rightBandX()
    {
        return ScrollTestUtils::defaultScreenRect().right();
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
    // The scan below is only meaningful against a NON-EMPTY batch: with no
    // emission at all the loop body never runs and the assertion passes
    // without proving anything. Its sibling at interactiveDragMark... guards
    // the same way and says so.
    QVERIFY(tiledSpy.count() >= 1);
    bool sawSurvivingWindow = false;
    for (const auto& emission : tiledSpy) {
        const QString payload = emission.first().toString();
        if (payload.contains(QStringLiteral("\"a\"")) || payload.contains(QStringLiteral("\"c\""))) {
            sawSurvivingWindow = true;
        }
        QVERIFY(!payload.contains(QStringLiteral("\"b\"")));
    }
    // ...and the batch really carried the surviving neighbours, so the absence
    // of "b" above is the detach rather than an empty or unrelated payload.
    QVERIFY(sawSurvivingWindow);
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
    // An INVALID target must not clobber the stored one (the guard that
    // keeps a stateless-screen wander mid-drag from erasing the aim).
    engine->updateDragInsertPreview(DragTarget{});
    // Commit proves the SECOND stored target was what survived: b joins
    // c's column (index 1 after the detach) at tile 0 — distinguishing
    // "stored the latest" from "stored nothing" and from "kept the first"
    // (which would have opened a new column at 0).
    engine->commitDragInsertPreview();
    QCOMPARE(state->strip().columnOfWindow(QStringLiteral("b")), state->strip().columnOfWindow(QStringLiteral("c")));
    const Column& joined = state->strip().columns().at(state->strip().columnOfWindow(QStringLiteral("b")));
    QCOMPARE(joined.indexOfWindow(QStringLiteral("b")), 0);
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
    QVERIFY(state->strip().takeWindow(QStringLiteral("b"), engineParams()));
    QVERIFY(state->strip().insertWindowIntoColumnAt(0, 1, QStringLiteral("b"), engineParams()));

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
    // The two screens the preview straddles, both reportable. The adaptor's
    // cancelDragInsertPreviewsForScreen needs the PRIOR one as well as the
    // target: cancel puts the window back on S1, so an output-removal or
    // desktop switch on S1 strands this preview just as surely as one on S2,
    // and with only the target reported it would survive with a priorKey
    // naming a context that no longer resolves.
    QCOMPARE(engine->dragInsertPreviewScreenId(), QStringLiteral("S2"));
    QCOMPARE(engine->dragInsertPreviewPriorScreenId(), QStringLiteral("S1"));

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
    // No prior state means no prior screen: there is nowhere for a cancel to
    // put this window back to, so no output going away can strand it.
    QVERIFY(engine->dragInsertPreviewPriorScreenId().isEmpty());
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
    // Cursor at b's centre resolves the above-midpoint arm: insert BEFORE b.
    QCOMPARE(join.secondary, 0);

    // b's left edge band: a new column before it — b is the first visible
    // column, so the aim is tagged as a leading-edge hint.
    const DragTarget before =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectB.left() + 4, rectB.center().y()));
    QCOMPARE(before.primary, 0);
    QVERIFY(before.newSlot);
    QVERIFY(before.leadingEdge);

    // b's right edge band: b is also the LAST visible column, so this is
    // the trailing append, not a before-b slot.
    const DragTarget after =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectB.right() - 4, rectB.center().y()));
    QCOMPARE(after.primary, 1);
    QVERIFY(after.newSlot);
    QVERIFY(!after.leadingEdge);

    // Right of the whole (short) strip: append.
    const DragTarget append =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectB.right() + 200, rectB.center().y()));
    QCOMPARE(append.primary, 1);
    QVERIFY(append.newSlot);
    QVERIFY(!append.leadingEdge);

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::hitTestResolvesStackedTileSlots()
{
    // The y-resolution loop with a REAL stack: secondary must be the MODEL
    // tile index of the hovered slot (above-midpoint inserts before the
    // tile, below-midpoint after), which the single-tile sibling above
    // cannot exercise.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // Stack c under b, then detach a: the settled strip is one column [b,c].
    QVERIFY(state->strip().takeWindow(QStringLiteral("c"), engineParams()));
    QVERIFY(state->strip().insertWindowIntoColumnAt(1, 1, QStringLiteral("c"), engineParams()));
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));

    const QRect rectB = tileRect(engine, QStringLiteral("S1"), QStringLiteral("b"));
    const QRect rectC = tileRect(engine, QStringLiteral("S1"), QStringLiteral("c"));
    QVERIFY(!rectB.isNull());
    QVERIFY(!rectC.isNull());
    const int columnMidX = rectB.center().x();

    // Upper half of the TOP tile: insert before b (model index 0).
    const DragTarget top = engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"),
                                                                  QPoint(columnMidX, rectB.top() + rectB.height() / 4));
    QVERIFY(!top.newSlot);
    QCOMPARE(top.secondary, 0);
    // Lower half of the BOTTOM tile: insert after c (model index 2).
    const DragTarget bottom = engine->computeDragInsertTargetAtPoint(
        QStringLiteral("S1"), QPoint(columnMidX, rectC.bottom() - rectC.height() / 4));
    QVERIFY(!bottom.newSlot);
    QCOMPARE(bottom.secondary, 2);
    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::hitTestMapsThroughAMinimizedTile()
{
    // The reason the y-loop maps through the hovered tile's windowId instead
    // of reusing its position among the RESOLVED tiles. Resolved tiles omit
    // minimized ones, so the two indices diverge by one per preceding
    // minimized tile, and a resolved-position shortcut would hand commit a
    // slot one place too high. Nothing else in the suite puts a hidden tile
    // in a column, so both the `continue` and the model mapping were free to
    // be replaced by the resolved position with every test still green.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"),
                {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    // One column [b, c, d], then minimize the MIDDLE tile. d is model index 2
    // but resolved index 1, which is the whole point.
    QVERIFY(state->strip().takeWindow(QStringLiteral("c"), engineParams()));
    QVERIFY(state->strip().insertWindowIntoColumnAt(1, 1, QStringLiteral("c"), engineParams()));
    QVERIFY(state->strip().takeWindow(QStringLiteral("d"), engineParams()));
    QVERIFY(state->strip().insertWindowIntoColumnAt(1, 2, QStringLiteral("d"), engineParams()));
    QVERIFY(state->strip().setWindowMinimized(QStringLiteral("c"), true, engineParams()));
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));

    // c is hidden, so it has no rect at all — the `continue` in the y-loop.
    QVERIFY(tileRect(engine, QStringLiteral("S1"), QStringLiteral("c")).isNull());
    const QRect rectD = tileRect(engine, QStringLiteral("S1"), QStringLiteral("d"));
    QVERIFY(!rectD.isNull());

    // Upper half of d: insert BEFORE d, which is model index 2. Reusing d's
    // resolved position would answer 1 and drop the window above the
    // minimized tile instead of below it.
    const DragTarget aboveD = engine->computeDragInsertTargetAtPoint(
        QStringLiteral("S1"), QPoint(rectD.center().x(), rectD.top() + rectD.height() / 4));
    QVERIFY(!aboveD.newSlot);
    QCOMPARE(aboveD.secondary, 2);
    // Lower half of d: after it, model index 3.
    const DragTarget belowD = engine->computeDragInsertTargetAtPoint(
        QStringLiteral("S1"), QPoint(rectD.center().x(), rectD.bottom() - rectD.height() / 4));
    QVERIFY(!belowD.newSlot);
    QCOMPARE(belowD.secondary, 3);
    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::indicatorRectTracksTarget()
{
    // Detach-once never opens a gap, so this rect is the ONLY drop feedback
    // the user gets. It must describe the space the window really takes:
    // a full-height column slot for a new column, and an (n+1)-th share of
    // the stack for a join.
    QObject owner;
    // BOTH screens active: the cross-screen assertion below must fail for the
    // screen-match guard, not because S2 merely has no state.
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});

    // No preview: nothing to paint.
    QVERIFY(engine->dragInsertIndicatorRect(QStringLiteral("S1")).isNull());

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));
    // Preview live but no target hit-tested yet.
    QVERIFY(engine->dragInsertIndicatorRect(QStringLiteral("S1")).isNull());

    const QRect rectB = tileRect(engine, QStringLiteral("S1"), QStringLiteral("b"));
    QVERIFY(!rectB.isNull());

    // New column BEFORE b, aimed from the leading edge: the promise sits
    // just OUTSIDE b (niri's leading hint placement) — covering b would
    // read as "replace this" while the drop actually shifts b aside. b sits
    // flush at the view's left edge here (the detach's dead space is on the
    // right), so the outside-left position crosses the screen edge and the
    // visibility clamp pins it half-in there. Full column height either
    // way.
    DragTarget newCol;
    newCol.primary = 0;
    newCol.newSlot = true;
    newCol.leadingEdge = true;
    engine->updateDragInsertPreview(newCol);
    const QRect openSlot = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(openSlot.isValid());
    // Outside-left placement, then the visibility clamp — deleting either
    // fails this: without the shift the promise sits at b's own x, without
    // the clamp it sits a full column past the edge.
    const QRect wa = ScrollTestUtils::defaultScreenRect();
    QCOMPARE(openSlot.x(), qMax(rectB.x() - openSlot.width(), wa.left() - openSlot.width() / 2));
    QCOMPARE(openSlot.height(), rectB.height());
    // Width pinned CONCRETELY, not just non-zero: both windows open at the
    // default column width, so the opening slot is exactly b's width. A
    // regression handing back the whole work area would satisfy `> 0`.
    QCOMPARE(openSlot.width(), rectB.width());

    // Another screen never borrows this screen's indicator. Asserted AFTER a
    // target exists: before one, the no-target early return fires first and
    // this would pass even with the screen guard deleted.
    QVERIFY(engine->dragInsertIndicatorRect(QStringLiteral("S2")).isNull());

    // Join b's column as a second tile: same width, half the height, and
    // the lower half for the below-b slot.
    DragTarget join;
    join.primary = 0;
    join.secondary = 1;
    engine->updateDragInsertPreview(join);
    const QRect joinSlot = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(joinSlot.isValid());
    QCOMPARE(joinSlot.width(), rectB.width());
    QCOMPARE(joinSlot.height(), rectB.height() / 2);
    QCOMPARE(joinSlot.y(), rectB.y() + rectB.height() / 2);
    // ...and the above-b slot is the upper half of the same column.
    join.secondary = 0;
    engine->updateDragInsertPreview(join);
    const QRect upper = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QCOMPARE(upper.y(), rectB.y());
    QCOMPARE(upper.height(), rectB.height() / 2);

    // A join with NO secondary takes the append-at-the-end default rather
    // than clamping to tile 0. With b alone in the column, appending puts the
    // window BELOW it, so this must land on the lower half — the same rect as
    // secondary=1 and the opposite of the secondary=0 upper half above.
    join.secondary = -1;
    engine->updateDragInsertPreview(join);
    const QRect appended = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QCOMPARE(appended, joinSlot);

    // A new column PAST the last one: the primary index is beyond the strip,
    // and the insert clamps to the end rather than refusing. The rect lands
    // to the right of b's column, which is the arm a cursor dragged off the
    // right edge of the strip reaches.
    DragTarget pastEnd;
    pastEnd.primary = 99;
    pastEnd.newSlot = true;
    engine->updateDragInsertPreview(pastEnd);
    const QRect tail = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(tail.isValid());
    QVERIFY2(tail.x() > rectB.x(), "a past-the-end new column must open to the RIGHT of the last one");
    QCOMPARE(tail.height(), rectB.height());

    // The indicator dies with the preview.
    engine->cancelDragInsertPreview();
    QVERIFY(engine->dragInsertIndicatorRect(QStringLiteral("S1")).isNull());
}

void TestScrollEngineDragInsert::indicatorRectMatchesTheDropUnderAGap()
{
    // The whole suite used to run at gap 0, where a layout that omits the gap
    // term entirely still produces the right numbers — so no test could see
    // the indicator's arithmetic disagreeing with the strip's. Under a real
    // inner gap the two diverge by up to slot*gap, which is what this pins.
    //
    // Stated as an EQUIVALENCE rather than a formula: whatever the indicator
    // promises, committing must deliver. That holds no matter how the layout's
    // height distribution changes later, which a hand-computed expectation
    // would not.
    QObject owner;
    ScrollEngine* engine = makeGappedProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));

    // Join the column holding b, below it — the arm where the gap term and the
    // per-tile height distribution both bite.
    //
    // Driven by HIT TEST, not a hand-built target, because that is the only
    // way production sets one and it is what makes the equivalence below
    // meaningful. The indicator resolves the slot in the LIVE view, so a
    // hand-built target naming an off-screen column would promise an
    // off-screen rect and then "fail" against a delivery the commit scrolled
    // into view — testing the view policy rather than the layout maths.
    const auto visible = engine->visibleTiles(QStringLiteral("S1"));
    QVERIFY(!visible.isEmpty());
    const QRect host = visible.first().rect;
    const DragTarget join = engine->computeDragInsertTargetAtPoint(
        QStringLiteral("S1"), QPoint(host.center().x(), host.bottom() - host.height() / 4));
    QVERIFY(!join.newSlot);
    engine->updateDragInsertPreview(join);

    const QRect promised = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(promised.isValid());

    engine->commitDragInsertPreview();
    const QRect delivered = tileRect(engine, QStringLiteral("S1"), QStringLiteral("a"));
    QVERIFY(!delivered.isNull());

    // SIZE, not position. The indicator resolves the slot in the LIVE view
    // while the commit re-anchors, so the two disagree on x by whatever the
    // drop scrolls — a view-policy difference, not a layout one. The size is
    // what the gap term and the per-tile height distribution govern, and it is
    // what this test was written to pin: a dropped gap term or a mis-shared
    // column height changes it immediately.
    QCOMPARE(promised.size(), delivered.size());
}

void TestScrollEngineDragInsert::indicatorRectMatchesTheDropForANewColumn()
{
    // The new-column arm of the same equivalence, under a gap. Catches the
    // column-width resolution (including the min-width floor) and the vertical
    // extent, which used to come from the FULL column rect rather than the
    // tile's — taller than the tiles beside it whenever the tab indicator
    // reserves a band inside the column.
    QObject owner;
    ScrollEngine* engine = makeGappedProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));

    DragTarget newCol;
    newCol.primary = 0;
    newCol.newSlot = true;
    engine->updateDragInsertPreview(newCol);

    const QRect promised = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(promised.isValid());

    engine->commitDragInsertPreview();
    const QRect delivered = tileRect(engine, QStringLiteral("S1"), QStringLiteral("a"));
    QVERIFY(!delivered.isNull());

    // SIZE, not position. The indicator resolves the slot in the LIVE view
    // while the commit re-anchors, so the two disagree on x by whatever the
    // drop scrolls — a view-policy difference, not a layout one. The size is
    // what the gap term and the per-tile height distribution govern, and it is
    // what this test was written to pin: a dropped gap term or a mis-shared
    // column height changes it immediately.
    QCOMPARE(promised.size(), delivered.size());
}

void TestScrollEngineDragInsert::fullViewportOuterSlotIndicatorClampsToTheEdge()
{
    // The niri-parity visibility clamp. A strip window's own drag can never
    // face a full viewport (detach-once frees its column's width), so the
    // fixture drags a FRESH window — the cross-screen / floating shape —
    // onto a strip whose two 600px columns exactly fill the 1200px view.
    // The after-the-last slot then resolves at x=1200, entirely off screen,
    // where the per-screen overlay would clip the indicator away; the clamp
    // pins it half-in at the right edge instead. Deleting the clamp fails
    // the first QCOMPARE with left()==1200.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    const QRect wa = ScrollTestUtils::defaultScreenRect();

    // Right-outer slot (after the last column): clamped to exactly half-in
    // at the right edge instead of resolving at x=1200.
    DragTarget rightOuter;
    rightOuter.primary = 2;
    rightOuter.newSlot = true;
    engine->updateDragInsertPreview(rightOuter);
    const QRect right = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(right.isValid());
    QCOMPARE(right.left(), wa.left() + wa.width() - right.width() / 2);
    QCOMPARE(right.intersected(wa).width(), right.width() / 2);

    // Before-the-first slot aimed from the leading edge: placed just
    // outside the first column, which on a full strip is past the LEFT
    // screen edge — clamped to half-in there, mirroring the right side
    // exactly.
    DragTarget leftOuter;
    leftOuter.primary = 0;
    leftOuter.newSlot = true;
    leftOuter.leadingEdge = true;
    engine->updateDragInsertPreview(leftOuter);
    const QRect left = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(left.isValid());
    QCOMPARE(left.left(), wa.left() - left.width() / 2);
    QCOMPARE(left.intersected(wa).width(), left.width() - left.width() / 2);

    // Control: a slot between the two visible columns is on screen and
    // untouched.
    DragTarget between;
    between.primary = 1;
    between.newSlot = true;
    engine->updateDragInsertPreview(between);
    const QRect mid = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(mid.isValid());
    QCOMPARE(mid.intersected(wa), mid);

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::scrolledStripEdgeSlotsMirror()
{
    // The common scrolled shape: three 600px columns, view on the last two
    // (b left, c right), column a off screen to the LEFT. Walking the view
    // must be mirror-symmetric: b's left band is the leading past-the-edge
    // hint, b's right band covers B at full size (new column at b's spot),
    // c's left band covers C at full size, and c's right band is the
    // trailing past-the-edge hint. Before the leadingEdge tag and the
    // right-band remap, b's left band painted a full rect over b while the
    // right edge showed the band, and b's right band lit the rect over c
    // from inside b — the imbalances this test exists to pin.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    // Focus-new-windows left c focused: the view shows b and c, a is parked.
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    const QRect wa = ScrollTestUtils::defaultScreenRect();
    const QRect rectB = tileRect(engine, QStringLiteral("S1"), QStringLiteral("b"));
    const QRect rectC = tileRect(engine, QStringLiteral("S1"), QStringLiteral("c"));
    QVERIFY(!rectB.isNull());
    QVERIFY(!rectC.isNull());

    // b's LEFT band: the leading slot, tagged — half-in at the left edge.
    const DragTarget beforeVisible =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectB.left() + 4, rectB.center().y()));
    QCOMPARE(beforeVisible.primary, 1);
    QVERIFY(beforeVisible.newSlot);
    QVERIFY(beforeVisible.leadingEdge);
    engine->updateDragInsertPreview(beforeVisible);
    const QRect left = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(left.isValid());
    QCOMPARE(left.left(), wa.left() - left.width() / 2);
    QCOMPARE(left.intersected(wa).width(), left.width() - left.width() / 2);

    // b's RIGHT band: the SAME leading slot aimed from inside — full rect
    // over b itself (the mirror of c's left band covering c).
    const DragTarget bInner =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectB.right() - 4, rectB.center().y()));
    QCOMPARE(bInner.primary, 1);
    QVERIFY(bInner.newSlot);
    QVERIFY(!bInner.leadingEdge);
    engine->updateDragInsertPreview(bInner);
    const QRect overB = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QCOMPARE(overB, rectB);

    // c's LEFT band: the between slot — full rect over c.
    const DragTarget cInner =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectC.left() + 4, rectC.center().y()));
    QCOMPARE(cInner.primary, 2);
    QVERIFY(cInner.newSlot);
    QVERIFY(!cInner.leadingEdge);
    engine->updateDragInsertPreview(cInner);
    const QRect overC = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QCOMPARE(overC, rectC);

    // c's RIGHT band: the trailing append — half-in at the right edge.
    const DragTarget afterLast =
        engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(rectC.right() - 4, rectC.center().y()));
    QCOMPARE(afterLast.primary, 3);
    QVERIFY(afterLast.newSlot);
    engine->updateDragInsertPreview(afterLast);
    const QRect right = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(right.isValid());
    QCOMPARE(right.left(), wa.left() + wa.width() - right.width() / 2);
    QCOMPARE(right.intersected(wa).width(), right.width() / 2);

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
    // teardown, not dangle into a released context — and the unwind must
    // put b BACK first, so the release hands it over like any other tile
    // instead of orphaning a detached window.
    QSignalSpy releasedSpy(engine, &PhosphorEngine::PlacementEngineBase::windowsReleased);
    engine->setActiveScreens({QStringLiteral("S2")});
    QVERIFY(!engine->hasDragInsertPreview());
    QCOMPARE(releasedSpy.count(), 1);
    const QStringList released = releasedSpy.first().first().toStringList();
    QVERIFY(released.contains(QStringLiteral("a")));
    QVERIFY(released.contains(QStringLiteral("b")));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("b")));
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
    // was the mid-drag teleport fight. Change b's column width FIRST so the
    // relayout genuinely moves rects and the batch is non-empty: an
    // identical relayout is silent for every window, and an empty spy would
    // pass the no-"a" scan without proving the mark did anything.
    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    state->strip().focusWindow(QStringLiteral("b"), engineParams());
    state->strip().setActiveColumnWidth(ColumnWidth::makeProportion(0.7));
    engine->retile(QStringLiteral("S1"));
    QVERIFY(tiledSpy.count() >= 1);
    bool sawEmissionWithB = false;
    for (const auto& emission : tiledSpy) {
        QVERIFY(!emission.first().toString().contains(QStringLiteral("\"a\"")));
        sawEmissionWithB = sawEmissionWithB || emission.first().toString().contains(QStringLiteral("\"b\""));
    }
    QVERIFY(sawEmissionWithB);

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
    state->strip().focusWindow(QStringLiteral("a"), engineParams());
    state->strip().setActiveColumnWidth(ColumnWidth::makeProportion(0.4));
    engine->retile(QStringLiteral("S1"));
    bool emittedA = false;
    for (const auto& emission : tiledSpy) {
        emittedA = emittedA || emission.first().toString().contains(QStringLiteral("\"a\""));
    }
    QVERIFY(emittedA);
}

void TestScrollEngineDragInsert::detachedResidueHealsInsteadOfLatching()
{
    // The unhealable-limbo class: a window tracked in the reverse map but in
    // neither the strip nor the floating set (e.g. a preview torn down
    // without restoration). Refusing it in begin AND in floatWindowInternal
    // made the state permanent — Alt+drag dead forever for that window.
    // Both paths must heal instead.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    // Manufacture the residue: strip loses b, the reverse map keeps it.
    QVERIFY(state->strip().takeWindow(QStringLiteral("b"), engineParams()));
    QVERIFY(engine->isWindowTracked(QStringLiteral("b")));
    QVERIFY(!engine->isWindowTiled(QStringLiteral("b")));

    // begin adopts-and-heals; a targeted commit re-homes the window.
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    DragTarget target;
    target.primary = 1;
    target.newSlot = true;
    engine->updateDragInsertPreview(target);
    engine->commitDragInsertPreview();
    QVERIFY(engine->isWindowTiled(QStringLiteral("b")));

    // And the float path heals the same residue (the drop's ApplyFloat leg).
    QVERIFY(state->strip().takeWindow(QStringLiteral("b"), engineParams()));
    engine->setWindowFloat(QStringLiteral("b"), true, QStringLiteral("S1"));
    QVERIFY(engine->isWindowFloatingInScroll(QStringLiteral("b")));
    // The healed float round-trips: unfloat opens a fresh column.
    engine->setWindowFloat(QStringLiteral("b"), false, QStringLiteral("S1"));
    QVERIFY(engine->isWindowTiled(QStringLiteral("b")));
}

void TestScrollEngineDragInsert::cancelRestoresADefensivelyDetachedWindow()
{
    // The MIRROR residue of the test above: in the strip, but with no
    // reverse-map entry behind it. begin's defensive block pulls such a
    // window out so commit cannot double-insert, and that take happens on
    // the hadPriorState=false path — where cancel used to return early
    // saying "fresh adoption never touched any state". It had. Escape then
    // left the window out of the strip AND untracked: gone from the engine
    // outright, with the user's window still on screen and nothing placing
    // it ever again.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    // Manufacture it: put a window straight into the strip model, so the
    // engine's reverse map never learns about it.
    QVERIFY(state->strip().insertWindow(QStringLiteral("ghost"), ColumnWidth{}, ColumnDisplay::Normal, engineParams()));
    QVERIFY(state->strip().containsWindow(QStringLiteral("ghost")));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("ghost")));

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("ghost"), QStringLiteral("S1")));
    // begin took it out, which is the behaviour being relied on.
    QVERIFY(!state->strip().containsWindow(QStringLiteral("ghost")));

    engine->cancelDragInsertPreview();
    QVERIFY2(state->strip().containsWindow(QStringLiteral("ghost")),
             "Escape must put a defensively detached window back, not drop it out of the engine");
    QVERIFY2(engine->isWindowTracked(QStringLiteral("ghost")),
             "and re-key it, so the next drag resolves its state instead of adopting it fresh again");
}

void TestScrollEngineDragInsert::reentrantBeginRestoresPriorWindow()
{
    // Begin-over-a-live-preview (a new interactive move starting before the
    // first drop landed): the implicit cancel must restore window #1's
    // captured slot before window #2 detaches — the second-invocation shape
    // the autotile twin also pins.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("c"), QStringLiteral("S1")));
    QCOMPARE(engine->dragInsertPreviewWindowId(), QStringLiteral("c"));
    // b is back at its captured slot; only c is detached (mid-preview).
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);
    QVERIFY(state->strip().containsWindow(QStringLiteral("b")));
    QVERIFY(!state->strip().containsWindow(QStringLiteral("c")));
    engine->cancelDragInsertPreview();
    // And the final cancel puts c back too — the full original order.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
}

void TestScrollEngineDragInsert::neighbourCloseInvalidatesStaleTarget()
{
    // A NEIGHBOUR closing mid-hold shifts the structure the remembered
    // indexes were aimed at, and a stationary cursor never re-aims. The
    // stale target must be discarded so commit takes the restore-slot
    // fallback instead of silently landing at a shifted index.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("a"), QStringLiteral("S1")));
    // Aim at c's column (index 1 of the detached strip b,c) as a join.
    DragTarget target;
    target.primary = 1;
    target.secondary = 0;
    engine->updateDragInsertPreview(target);

    engine->windowClosed(QStringLiteral("b"));
    QVERIFY(engine->hasDragInsertPreview());

    engine->commitDragInsertPreview();
    // Restore-slot fallback: a returns to its captured column 0, alone —
    // the stale join would have stacked it into c's column instead.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("a"), QStringLiteral("c")}));
    QVERIFY(state->strip().columnOfWindow(QStringLiteral("a")) != state->strip().columnOfWindow(QStringLiteral("c")));
}

void TestScrollEngineDragInsert::windowedFullscreenSurvivesCancelAndCommit()
{
    // The drag capture carries the flag through both exits: Escape (cancel)
    // restores it with the slot, and a commit re-seats it on the re-inserted
    // tile. Neither path may drop it silently — the client would stay
    // fullscreen-presented with the model saying otherwise.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    ScrollState* state = stateFor(engine, QStringLiteral("S1"));
    QVERIFY(state);

    engine->windowFocused(QStringLiteral("b"), QStringLiteral("S1"));
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));
    QVERIFY(state->strip().isWindowedFullscreen(QStringLiteral("b")));

    // Cancel path: begin detaches (flag leaves with the tile), cancel
    // restores tile AND flag. The neighbour is the negative control on both
    // exits: the flag must ride the dragged tile, never leak to a sibling.
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    QVERIFY(!state->strip().containsWindow(QStringLiteral("b")));
    engine->cancelDragInsertPreview();
    QVERIFY(state->strip().containsWindow(QStringLiteral("b")));
    QVERIFY(state->strip().isWindowedFullscreen(QStringLiteral("b")));
    QVERIFY(!state->strip().isWindowedFullscreen(QStringLiteral("a")));

    // Commit path: the re-inserted tile carries the flag too.
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S1")));
    DragTarget target;
    target.primary = 0;
    target.newSlot = true;
    engine->updateDragInsertPreview(target);
    engine->commitDragInsertPreview();
    QVERIFY(state->strip().containsWindow(QStringLiteral("b")));
    QVERIFY(state->strip().isWindowedFullscreen(QStringLiteral("b")));
    QVERIFY(!state->strip().isWindowedFullscreen(QStringLiteral("a")));
}

void TestScrollEngineDragInsert::windowedFullscreenSurvivesCrossScreenCommit()
{
    // The cross-screen twin of the test above: a windowed-fullscreen tile
    // dragged to ANOTHER scrolling screen keeps its flag on the receiving
    // strip, and the receiving strip's own tiles stay unflagged.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b")});
    openWindows(engine, QStringLiteral("S2"), {QStringLiteral("c")});
    ScrollState* s1 = stateFor(engine, QStringLiteral("S1"));
    ScrollState* s2 = stateFor(engine, QStringLiteral("S2"));
    QVERIFY(s1);
    QVERIFY(s2);

    engine->windowFocused(QStringLiteral("b"), QStringLiteral("S1"));
    engine->toggleWindowedFullscreen(QStringLiteral("S1"));
    QVERIFY(s1->strip().isWindowedFullscreen(QStringLiteral("b")));

    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b"), QStringLiteral("S2")));
    DragTarget target;
    target.primary = 0;
    target.newSlot = true;
    engine->updateDragInsertPreview(target);
    engine->commitDragInsertPreview();
    QVERIFY(!s1->strip().containsWindow(QStringLiteral("b")));
    QVERIFY(s2->strip().containsWindow(QStringLiteral("b")));
    QVERIFY(s2->strip().isWindowedFullscreen(QStringLiteral("b")));
    QVERIFY(!s2->strip().isWindowedFullscreen(QStringLiteral("c")));
}

// ── Edge auto-scroll (niri's dnd-edge-view-scroll) ──────────────────────────
//
// The feature shipped once and was reverted: it scrolled the view while
// still hit-testing, so columns sliding under a parked cursor re-answered on
// every boundary that passed and the indicator flipped between a full-height
// new column and a half-height join. keepsTheTargetAtTheEdge is the test that
// exists for that; the rest pin the ramp's edges.

void TestScrollEngineDragInsert::edgeAutoScrollStaysInertWhenTheStripFits()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("b|fresh"), QStringLiteral("S1")));

    // One 600 px column on a 1200 px screen: nothing is off screen, so the
    // bands have nothing to reveal and must not arm at all — otherwise the
    // target would sit locked to an edge slot with no motion to justify it.
    const int before = viewX(engine, QStringLiteral("S1"));
    tick(engine, 2);
    QTest::qWait(120);
    QVERIFY(!tick(engine, 2));
    QVERIFY(!engine->dragAutoScrollActive());
    QCOMPARE(viewX(engine, QStringLiteral("S1")), before);

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::edgeAutoScrollHonoursTheStartDelay()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));
    const int start = viewX(engine, QStringLiteral("S1"));
    QVERIFY(start > 0); // column a is parked off the left edge

    // Entering the band arms it and moves nothing, and neither does a tick
    // taken immediately after: a drag that merely passes an edge on its way
    // somewhere else must not drag the strip with it.
    QVERIFY(!tick(engine, 2));
    QVERIFY(!tick(engine, 2));
    QVERIFY(!engine->dragAutoScrollActive());
    QCOMPARE(viewX(engine, QStringLiteral("S1")), start);

    QTest::qWait(120);
    QVERIFY(tick(engine, 2));
    QVERIFY(engine->dragAutoScrollActive());
    // At the very edge the ramp is at full speed, so ~16 ms of the 1500 px/s
    // default is ~24 px — leftwards, revealing the parked column.
    QVERIFY(viewX(engine, QStringLiteral("S1")) < start);

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::edgeAutoScrollKeepsTheTargetAtTheEdge()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    const QRect wa = ScrollTestUtils::defaultScreenRect();
    int last = armBand(engine, 2);
    QVERIFY(engine->dragAutoScrollActive());

    // Walk the strip a long way with the cursor NEVER moving. The indicator
    // must keep the leading slot's shape the whole time — half of a
    // full-height new column hanging off the left edge — even as which
    // column is first visible changes underneath it. This is the reverted
    // implementation's exact failure: there, the rect's HEIGHT changed as
    // each boundary crossed the cursor.
    const QRect first = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(first.isValid());
    QCOMPARE(first.left(), wa.left() - first.width() / 2);
    bool sawMotion = false;
    for (int i = 0; i < 20; ++i) {
        tick(engine, 2);
        const int now = viewX(engine, QStringLiteral("S1"));
        sawMotion = sawMotion || now != last;
        last = now;
        const QRect rect = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
        QCOMPARE(rect.height(), first.height());
        QCOMPARE(rect.left(), wa.left() - rect.width() / 2);
    }
    QVERIFY(sawMotion);

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::edgeAutoScrollStampsItsBatchesImmediate()
{
    // A batch the auto-scroll heartbeat produces is user-driven continuous
    // motion: the effect must apply its view delta outright rather than
    // animate it (a leg retargeted every 16 ms never progresses on a
    // stateless curve). The engine says so per entry, beside viewDeltaX, and
    // ONLY while the scroll owns the target — an ordinary verb's batch after
    // the drag must not carry the flag, or every discrete scroll loses its
    // animation.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    armBand(engine, 2);
    QVERIFY(engine->dragAutoScrollActive());
    tiled.clear();
    tick(engine, 2);
    QVERIFY2(!tiled.isEmpty(), "an owned tick that moved the view must emit a batch");

    int carried = 0;
    const QJsonArray scrolled = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    for (const QJsonValue& v : scrolled) {
        const QJsonObject o = v.toObject();
        if (!o.contains(QLatin1String("viewDeltaX"))) {
            QVERIFY2(!o.contains(QLatin1String("viewImmediate")),
                     "viewImmediate is meaningless without a view delta to apply");
            continue;
        }
        QVERIFY2(o.value(QLatin1String("viewImmediate")).toBool(false),
                 "a heartbeat batch's carried entries must be marked immediate");
        ++carried;
    }
    QVERIFY2(carried > 0, "expected at least one carried entry in the scrolled batch");

    // Ownership released: an ordinary verb's view travel animates again.
    engine->cancelDragInsertPreview();
    tiled.clear();
    engine->focusColumnFirst(QStringLiteral("S1"));
    QVERIFY(!tiled.isEmpty());
    const QJsonArray verb = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
    for (const QJsonValue& v : verb) {
        QVERIFY2(!v.toObject().contains(QLatin1String("viewImmediate")),
                 "a discrete scroll's batch must not be marked immediate");
    }
}

void TestScrollEngineDragInsert::edgeAutoScrollKeepsTheTabStripPayloadLive()
{
    // The owned scroll's tab-strip payload keeps flowing per tick, exactly
    // like every ordinary scroll's: the pills re-derive from the moved
    // columns each apply. A freeze-and-slide scheme (skip the emit, let the
    // compositor translate stale content) was tried and reverted — the
    // frozen payload holds only the columns visible at freeze time, so a
    // sustained scroll empties the bar, and no compositor-side re-baseline
    // can tell a fresh-payload commit from an enrichment replay of the
    // stale one. This test pins the revert.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"),
                {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});
    // Tab the first column so the screen owns an indicator at all.
    engine->focusColumnFirst(QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));

    QSignalSpy strips(engine, &ScrollEngine::tabStripsChanged);
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));
    // The focus sits on the first column, so the view is pinned at the strip's
    // start — the RIGHT band is the one with travel left in it.
    armBand(engine, rightBandX());
    QVERIFY(engine->dragAutoScrollActive());
    strips.clear();

    const int before = viewX(engine, QStringLiteral("S1"));
    for (int i = 0; i < 10; ++i) {
        tick(engine, rightBandX());
    }
    QVERIFY2(qAbs(viewX(engine, QStringLiteral("S1")) - before) > 0, "precondition: the owned ticks moved the view");
    QVERIFY2(!strips.isEmpty(), "an owned scroll must keep re-pushing the tab-strip payload as the view moves");

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::edgeAutoScrollAccumulatesSubPixelSpeed()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    // One pixel inside the band's INNER edge: depth is 1/30, so a 16 ms tick
    // asks for well under a pixel. The view anchor is integer pixels, so
    // without the carried remainder the shallow end of the band would be
    // dead however long the user held there.
    const int shallow = ScrollTestUtils::defaultScreenRect().left() + kTriggerWidth - 1;
    const int start = armBand(engine, shallow);
    int moved = 0;
    for (int i = 0; i < 12 && moved == 0; ++i) {
        tick(engine, shallow);
        moved = start - viewX(engine, QStringLiteral("S1"));
    }
    QVERIFY(moved > 0);

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::edgeAutoScrollClampsAtTheStripEnd()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    armBand(engine, 0);
    // Long enough to run out of strip several times over. The view stops at
    // the first column's left edge and stays there; it never walks negative,
    // and the tick keeps answering rather than latching.
    for (int i = 0; i < 60; ++i) {
        tick(engine, 0, 0.05);
        QVERIFY(viewX(engine, QStringLiteral("S1")) >= 0);
    }
    QCOMPARE(viewX(engine, QStringLiteral("S1")), 0);
    QVERIFY(!tick(engine, 0, 0.05)); // pinned: nothing moves, nothing changes

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::edgeAutoScrollDisarmsOutsideTheBand()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    armBand(engine, 2);
    QVERIFY(engine->dragAutoScrollActive());

    // Cursor pulled back into the middle: the scroll stops owning the target
    // immediately, so the caller's hit-test takes over again on the same
    // tick rather than one delay later. The tick's return value is a REPAINT
    // hint, not "the view moved" — leaving the band repairs the stored target
    // back to the cursor, and that needs redrawing — so what is pinned here
    // is the ownership release and the view standing still, not the boolean.
    const int settled = viewX(engine, QStringLiteral("S1"));
    tick(engine, ScrollTestUtils::defaultScreenRect().center().x());
    QVERIFY(!engine->dragAutoScrollActive());
    QCOMPARE(viewX(engine, QStringLiteral("S1")), settled);

    // Re-entering serves a fresh delay rather than resuming instantly.
    QVERIFY(!tick(engine, 2));
    QVERIFY(!engine->dragAutoScrollActive());
    // ...and that fresh delay must actually EXPIRE. Without this the test
    // could not tell a working re-arm from one that can never take ownership
    // again, which is the more damaging of the two failures.
    QTest::qWait(kDelayWaitMs);
    QVERIFY(tick(engine, 2));
    QVERIFY(engine->dragAutoScrollActive());

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::edgeAutoScrollRepairsTheTargetOnLeavingTheBand()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    armBand(engine, leftBandX());
    QVERIFY(engine->dragAutoScrollActive());
    for (int i = 0; i < 10; ++i) {
        tick(engine, leftBandX());
    }
    const QRect edgeRect = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(edgeRect.isValid());

    // Leaving the band must REPAIR the stored target, not merely release the
    // latch. Only cursor MOTION re-hit-tests in the daemon, so a cursor that
    // steps out of the band and then parks would otherwise keep the edge slot
    // as both the painted indicator and the thing a release commits — a drop
    // at the far end of the strip instead of under the cursor. The tick
    // returns true precisely because the indicator has to be repainted.
    const int middle = ScrollTestUtils::defaultScreenRect().center().x();
    QVERIFY(tick(engine, middle));
    QVERIFY(!engine->dragAutoScrollActive());
    const QRect repaired = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(repaired.isValid());
    QVERIFY(repaired != edgeRect);
    // And the stored target is the one an ordinary hit-test answers at that
    // point, not merely some other valid target. dragInsertIndicatorRect
    // reads the STORED target, so this needs the hit-test run separately.
    const auto expected = engine->computeDragInsertTargetAtPoint(QStringLiteral("S1"), QPoint(middle, 300));
    QVERIFY(expected.isValid());
    engine->updateDragInsertPreview(expected);
    QCOMPARE(engine->dragInsertIndicatorRect(QStringLiteral("S1")), repaired);

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::edgeAutoScrollRightBandMirrorsTheLeft()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    const QRect wa = ScrollTestUtils::defaultScreenRect();
    // Opening a, b, c focuses the last column, so the view already sits at
    // the TRAILING end and the right band has nowhere to travel. Walk to the
    // leading end first, otherwise this test would pass without the trailing
    // arm ever moving anything.
    armBand(engine, leftBandX());
    for (int i = 0; i < 60; ++i) {
        tick(engine, leftBandX());
    }
    QCOMPARE(viewX(engine, QStringLiteral("S1")), 0);

    // The trailing band drives direction = +1, the positive-residue floor
    // branch and the OTHER arm of the target writer (a new column after the
    // last visible one, with leadingEdge left false). None of that is
    // exercised by the leading-band tests, so a sign error or an off-by-one
    // in the trailing slot index would ship green without this.
    int last = armBand(engine, rightBandX());
    QVERIFY(engine->dragAutoScrollActive());

    const QRect first = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
    QVERIFY(first.isValid());
    // Mirror of the leading band's half-in clamp, at the other edge. Against
    // the EXCLUSIVE right edge (left + width), not QRect::right(), which is
    // the last contained pixel and would be one short.
    const int waRightEdge = wa.left() + wa.width();
    QCOMPARE(first.left(), waRightEdge - first.width() / 2);
    bool sawMotion = false;
    for (int i = 0; i < 20; ++i) {
        tick(engine, rightBandX());
        const int now = viewX(engine, QStringLiteral("S1"));
        sawMotion = sawMotion || now != last;
        // The view travels FORWARD here, the opposite of the leading band.
        QVERIFY(now >= last);
        last = now;
        const QRect rect = engine->dragInsertIndicatorRect(QStringLiteral("S1"));
        QCOMPARE(rect.height(), first.height());
        QCOMPARE(rect.left(), waRightEdge - rect.width() / 2);
    }
    QVERIFY(sawMotion);

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::edgeAutoScrollCommitsTheSlotItPromised()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    // The whole point of the feature is that the DROP can land somewhere the
    // drag could not reach when it began. Watching viewX and the indicator
    // proves the view moved and the promise held; only a commit proves the
    // promise is what the strip actually acts on.
    armBand(engine, leftBandX());
    QVERIFY(engine->dragAutoScrollActive());
    for (int i = 0; i < 40; ++i) {
        tick(engine, leftBandX());
    }
    QCOMPARE(viewX(engine, QStringLiteral("S1")), 0); // pinned at the leading end

    engine->commitDragInsertPreview();
    // Leading slot: the dragged window becomes the first column, ahead of
    // everything that was there.
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")).first(), QStringLiteral("d|fresh"));
}

void TestScrollEngineDragInsert::edgeAutoScrollClampsOneTickOfTravel()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    // Walk to the leading end first so the trailing band has room to travel;
    // otherwise a clamp failure would be indistinguishable from a pinned view.
    armBand(engine, leftBandX());
    for (int i = 0; i < 60; ++i) {
        tick(engine, leftBandX());
    }
    QCOMPARE(viewX(engine, QStringLiteral("S1")), 0);

    // A stalled event loop can hand one tick whole seconds of elapsed time.
    // Unclamped, speed times that lurches the strip to an end in a single
    // frame, which reads as a teleport. Deleting the clamp leaves every other
    // slot in this file green, so it needs its own.
    const int start = armBand(engine, rightBandX());
    QVERIFY(engine->dragAutoScrollActive());
    tick(engine, rightBandX(), /*dtSeconds=*/5.0);
    const int travelled = viewX(engine, QStringLiteral("S1")) - start;
    QVERIFY(travelled > 0);
    // Mirrors drag_autoscroll.cpp's kMaxTickSeconds by hand: that constant is
    // in an anonymous namespace in the .cpp and is deliberately not exported,
    // so there is nothing to include. At full depth the per-tick ceiling is
    // maxSpeed * that, plus one pixel of slack for the carried remainder.
    constexpr qreal kMaxTickSeconds = 0.05;
    QVERIFY2(travelled <= int(kMaxSpeed * kMaxTickSeconds) + 1,
             qPrintable(QStringLiteral("one tick travelled %1 px").arg(travelled)));

    engine->cancelDragInsertPreview();
}

void TestScrollEngineDragInsert::edgeAutoScrollRefusesAForeignScreenOrNoPreview()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    armBand(engine, leftBandX());
    QVERIFY(engine->dragAutoScrollActive());
    const int settled = viewX(engine, QStringLiteral("S1"));

    // A tick naming a screen the preview does not belong to must refuse AND
    // give the target back: dragAutoScrollActive() carries no screen, so a
    // mismatched tick that kept ownership would block the hit-test on every
    // screen for the rest of the drag.
    QVERIFY(!tick(engine, leftBandX(), 0.016, QStringLiteral("S2")));
    QVERIFY(!engine->dragAutoScrollActive());
    QCOMPARE(viewX(engine, QStringLiteral("S1")), settled);

    // With no preview at all the tick is inert rather than crashing.
    engine->cancelDragInsertPreview();
    QVERIFY(!tick(engine, leftBandX()));
    QVERIFY(!engine->dragAutoScrollActive());
}

void TestScrollEngineDragInsert::edgeAutoScrollKeepsOwnershipWhenPinned()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    openWindows(engine, QStringLiteral("S1"), {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("d|fresh"), QStringLiteral("S1")));

    armBand(engine, leftBandX());
    for (int i = 0; i < 60; ++i) {
        tick(engine, leftBandX());
    }
    QCOMPARE(viewX(engine, QStringLiteral("S1")), 0);

    // Reaching an end pins the VIEW but must not end ownership. The cursor is
    // still asking to insert past that edge, and that edge slot is still the
    // promise; handing the target back here would resume per-column
    // hit-testing the instant the strip pinned, which is the churn the whole
    // mechanism exists to prevent. Without this assertion, adding a disarm on
    // the pinned branch leaves every slot in this file green.
    QVERIFY(engine->dragAutoScrollActive());
    QVERIFY(!tick(engine, leftBandX(), 0.05));
    QVERIFY(engine->dragAutoScrollActive());

    engine->cancelDragInsertPreview();
}

QTEST_GUILESS_MAIN(TestScrollEngineDragInsert)
#include "test_scrollengine_draginsert.moc"
