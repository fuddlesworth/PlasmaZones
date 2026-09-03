// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/AlgorithmRegistry.h>

#include "helpers/AutotileTestHelpers.h"
#include "helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

/**
 * @brief Tests for the drag-insert preview state machine on AutotileEngine.
 *
 * Covers begin/update/commit/cancel across same-screen reorder, cross-screen
 * adoption, and fresh adoption paths, plus eviction undo on cancel.
 *
 * Uses windowOpened() + processEvents() to register windows through the proper
 * lifecycle (populating m_states), which is required for the engine
 * to detect same-screen vs cross-screen vs fresh adoption paths.
 */
class TestAutotileDragInsert : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

    static constexpr auto Screen1 = "eDP-1";
    static constexpr auto Screen2 = "HDMI-1";

    /// Helper: open windows through the engine lifecycle so m_states
    /// is properly populated (unlike direct PhosphorTiles::TilingState::addWindow).
    void openWindows(AutotileEngine& engine, const QString& screenId, const QStringList& windowIds)
    {
        for (const QString& id : windowIds) {
            engine.windowOpened(id, screenId);
        }
        QCoreApplication::processEvents();
    }

    /// Helper: add windows directly to PhosphorTiles::TilingState WITHOUT populating
    /// m_states. Use only for "fresh adoption" tests where the
    /// window is intentionally untracked by the engine.
    void addWindowsToState(AutotileEngine& engine, const QString& screenId, const QStringList& windowIds)
    {
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenId);
        // QVERIFY, not Q_ASSERT: the latter compiles out under NDEBUG, so a
        // release-mode run of this suite would dereference null here instead
        // of failing the slot.
        QVERIFY(state);
        for (const QString& id : windowIds) {
            state->addWindow(id);
        }
    }

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    // =========================================================================
    // Precondition tests
    // =========================================================================

    void testBegin_rejectsEmptyWindowId()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});
        QVERIFY(!engine.beginDragInsertPreview(QString(), QLatin1String(Screen1)));
        QVERIFY(!engine.hasDragInsertPreview());
    }

    void testBegin_rejectsEmptyScreenId()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});
        QVERIFY(!engine.beginDragInsertPreview(QStringLiteral("win1"), QString()));
        QVERIFY(!engine.hasDragInsertPreview());
    }

    void testBegin_rejectsNonAutotileScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});
        QVERIFY(!engine.beginDragInsertPreview(QStringLiteral("win1"), QLatin1String(Screen2)));
        QVERIFY(!engine.hasDragInsertPreview());
    }

    // =========================================================================
    // Same-screen reorder: begin → update → commit
    // =========================================================================

    void testSameScreen_beginSetsPreview()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        QVERIFY(engine.hasDragInsertPreview());
        QCOMPARE(engine.dragInsertPreviewWindowId(), QStringLiteral("A"));
        QCOMPARE(engine.dragInsertPreviewScreenId(), screen);
    }

    void testSameScreen_updateMovesWindow()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));

        // Move A from index 0 to index 2
        engine.updateDragInsertPreview(2);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        const QStringList tiled = state->tiledWindows();
        QCOMPARE(tiled.size(), 3);
        // A should now be at position 2
        QCOMPARE(tiled[2], QStringLiteral("A"));
    }

    void testSameScreen_commitClearsPreview()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.updateDragInsertPreview(2);
        engine.commitDragInsertPreview();

        QVERIFY(!engine.hasDragInsertPreview());
        // Order should persist after commit
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QCOMPARE(state->tiledWindows()[2], QStringLiteral("A"));
    }

    // =========================================================================
    // Same-screen reorder: cancel restores original order
    // =========================================================================

    void testSameScreen_cancelRestoresOrder()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        const QStringList originalOrder = engine.tilingStateForScreen(screen)->tiledWindows();

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.updateDragInsertPreview(2);
        engine.cancelDragInsertPreview();

        QVERIFY(!engine.hasDragInsertPreview());
        QCOMPARE(engine.tilingStateForScreen(screen)->tiledWindows(), originalOrder);
    }

    /// Helper for the two cancel-geometry tests below: true when any
    /// windowsTiled emission recorded by @p spy carries a GEOMETRY entry
    /// (an "x" key — float-only sync entries have none) for @p windowId.
    static bool spyHasGeometryEntryFor(const QSignalSpy& spy, const QString& windowId)
    {
        for (const QList<QVariant>& emission : spy) {
            const QJsonArray arr = QJsonDocument::fromJson(emission.first().toString().toUtf8()).array();
            for (const QJsonValue& v : arr) {
                const QJsonObject obj = v.toObject();
                if (obj.value(QLatin1String("windowId")).toString() == windowId && obj.contains(QLatin1String("x"))) {
                    return true;
                }
            }
        }
        return false;
    }

    // A mid-drag cancel (insert trigger released while the pointer still
    // holds the window) must not emit tile geometry for the dragged window —
    // doing so resized the window in the user's hand the moment they let go
    // of the trigger (discussion #1028 follow-up: float-drag + tap ALT).
    void testSameScreen_midDragCancelSkipsDraggedGeometry()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        // Force zones — unit tests have no real screen geometry, and
        // applyTiling reuses the last calculated zones when recalc bails.
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        state->setCalculatedZones({QRect(0, 0, 900, 1000), QRect(900, 0, 500, 1000), QRect(1400, 0, 500, 1000)});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.updateDragInsertPreview(2);

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.cancelDragInsertPreview(/*dragStillActive=*/true);

        QVERIFY(tiledSpy.count() >= 1);
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("B")),
                 "cancel retile emitted no neighbour geometry — the assertion below would be vacuous");
        QVERIFY2(!spyHasGeometryEntryFor(tiledSpy, QStringLiteral("A")),
                 "mid-drag cancel emitted tile geometry for the window still being dragged");
        // Any retile deferred past the cancel (the queued coalesced channel)
        // must not re-emit A's rect after the scope-bound filter clears.
        QCoreApplication::processEvents();
        QVERIFY2(!spyHasGeometryEntryFor(tiledSpy, QStringLiteral("A")),
                 "a deferred retile emitted the dragged window's geometry after the cancel returned");

        // The filter is scope-bound to the cancel: a LATER retile on the same
        // engine must emit A's geometry again. Deleting the qScopeGuard clear
        // would otherwise skip this window on every future retile forever,
        // with the suite green.
        tiledSpy.clear();
        state->setCalculatedZones(
            {QRect(0, 0, 700, 1000), QRect(700, 0, 500, 1000), QRect(1200, 0, 400, 1000), QRect(1600, 0, 300, 1000)});
        openWindows(engine, screen, {QStringLiteral("D")});
        engine.retile(screen);
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("A")),
                 "the cancel filter outlived the cancel — geometry emission for the window never resumed");
    }

    // Cross-screen twin: a mid-drag cancel of a cross-screen adoption retiles
    // BOTH screens (target, then the prior screen the window is restored to),
    // and the non-screen-scoped filter must suppress the dragged window's
    // geometry in each while neighbours still reflow.
    void testCrossScreen_midDragCancelSkipsDraggedGeometryOnBothScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString s1 = QLatin1String(Screen1);
        const QString s2 = QLatin1String(Screen2);
        engine.setAutotileScreens({s1, s2});
        openWindows(engine, s1, {QStringLiteral("A"), QStringLiteral("B")});
        openWindows(engine, s2, {QStringLiteral("X"), QStringLiteral("Y")});

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(s1);
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(s2);
        QVERIFY(state1);
        QVERIFY(state2);

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), s2));

        // Zones sized for the POST-CANCEL counts (cancel restores A to s1, so
        // s1 tiles A+B and s2 tiles X+Y again). Set after begin — begin's own
        // retiles bail harmlessly on the missing geometry, and applyTiling
        // refuses a zone vector that outnumbers the tiled list, so begin-time
        // counts must not constrain these.
        state1->setCalculatedZones({QRect(0, 0, 900, 1000), QRect(900, 0, 900, 1000)});
        state2->setCalculatedZones({QRect(0, 0, 600, 1000), QRect(600, 0, 600, 1000)});

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.cancelDragInsertPreview(/*dragStillActive=*/true);

        QVERIFY(tiledSpy.count() >= 1);
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("X")),
                 "target-screen cancel retile emitted no neighbour geometry — the negative below would be vacuous");
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("B")),
                 "prior-screen cancel retile emitted no neighbour geometry — the negative below would be vacuous");
        QVERIFY2(!spyHasGeometryEntryFor(tiledSpy, QStringLiteral("A")),
                 "cross-screen mid-drag cancel emitted tile geometry for the window still being dragged");
    }

    // Eviction twin: the mid-drag cancel's unfloat-the-victim retile must
    // still skip the dragged window while re-tiling the restored neighbour.
    void testEviction_midDragCancelSkipsDraggedGeometry()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 3;
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        state->setCalculatedZones({QRect(0, 0, 700, 1000), QRect(700, 0, 600, 1000), QRect(1300, 0, 600, 1000)});

        // Fresh adoption over the cap evicts C (last tiled neighbour).
        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("newcomer"), screen));
        QVERIFY(!engine.tilingStateForScreen(screen)->tiledWindows().contains(QStringLiteral("C")));

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.cancelDragInsertPreview(/*dragStillActive=*/true);

        QVERIFY(tiledSpy.count() >= 1);
        // The victim is back in the tiled list and gets geometry again.
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("C")),
                 "cancel retile emitted no geometry for the restored eviction victim");
        QVERIFY2(!spyHasGeometryEntryFor(tiledSpy, QStringLiteral("newcomer")),
                 "eviction-arm mid-drag cancel emitted tile geometry for the window still being dragged");
    }

    // Control: the drop-time cancel (default argument) keeps the snap-back —
    // the dragged window's tile geometry IS applied.
    void testSameScreen_dropTimeCancelAppliesDraggedGeometry()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        state->setCalculatedZones({QRect(0, 0, 900, 1000), QRect(900, 0, 500, 1000), QRect(1400, 0, 500, 1000)});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.updateDragInsertPreview(2);

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.cancelDragInsertPreview();

        QVERIFY(tiledSpy.count() >= 1);
        // Neighbour guard first, mirroring the mid-drag twin: an emission
        // with no geometry entries at all would satisfy neither assertion
        // meaningfully, and the helper answers false for a payload whose
        // shape changed, which the positive alone would misread as regression.
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("B")),
                 "drop-time cancel retile emitted no neighbour geometry — payload shape changed?");
        QVERIFY(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("A")));
    }

    // =========================================================================
    // Interactive-drag mark (setInteractiveDragWindow)
    // =========================================================================

    // While the daemon-set mark names a window, NO retile may emit its
    // geometry — this is what protects the window in the user's hand from
    // retiles the preview/cancel filters never see (a deferred geometry
    // retry, a neighbour opening or closing, discussion #1028).
    void testInteractiveDragMark_suppressesGeometryUntilCleared()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        state->setCalculatedZones({QRect(0, 0, 900, 1000), QRect(900, 0, 500, 1000), QRect(1400, 0, 500, 1000)});

        engine.setInteractiveDragWindow(QStringLiteral("A"));
        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.retile(screen);
        QVERIFY(tiledSpy.count() >= 1);
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("B")),
                 "marked retile emitted no neighbour geometry — the negative below would be vacuous");
        QVERIFY2(!spyHasGeometryEntryFor(tiledSpy, QStringLiteral("A")),
                 "a retile emitted geometry for the window under a compositor interactive move");

        // Clearing the mark restores normal emission (the drop finalizes
        // through its own paths; here we just prove the mark is not sticky).
        engine.setInteractiveDragWindow(QString());
        tiledSpy.clear();
        engine.retile(screen);
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("A")), "the interactive-drag mark outlived its clear");
    }

    // The stale-preview identity gate: a cancel(dragStillActive=true) whose
    // preview names a DIFFERENT window than the interactive-drag mark is
    // residue from a prior drag — nobody holds that window, so its snap-back
    // geometry must be emitted, not suppressed.
    void testInteractiveDragMark_stalePreviewCancelStillSnapsBack()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        state->setCalculatedZones({QRect(0, 0, 900, 1000), QRect(900, 0, 500, 1000), QRect(1400, 0, 500, 1000)});

        // A's preview is left over from a prior drag; the NEW drag holds C.
        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.updateDragInsertPreview(2);
        engine.setInteractiveDragWindow(QStringLiteral("C"));

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.cancelDragInsertPreview(/*dragStillActive=*/true);

        QVERIFY(tiledSpy.count() >= 1);
        QVERIFY2(spyHasGeometryEntryFor(tiledSpy, QStringLiteral("A")),
                 "stale preview's snap-back was suppressed — the window stays parked at the previewed rect");
        // The genuinely-held window stays protected throughout.
        QVERIFY2(!spyHasGeometryEntryFor(tiledSpy, QStringLiteral("C")),
                 "cancel retile emitted geometry for the window the NEW drag is holding");
        engine.setInteractiveDragWindow(QString());
    }

    // =========================================================================
    // Cross-screen adoption: begin → commit
    // =========================================================================

    void testCrossScreen_adoptionMovesBetweenScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString s1 = QLatin1String(Screen1);
        const QString s2 = QLatin1String(Screen2);
        engine.setAutotileScreens({s1, s2});
        openWindows(engine, s1, {QStringLiteral("A"), QStringLiteral("B")});
        openWindows(engine, s2, {QStringLiteral("X"), QStringLiteral("Y")});

        // Adopt A from screen1 onto screen2
        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), s2));
        QVERIFY(engine.hasDragInsertPreview());

        // A should be gone from screen1 and present on screen2
        QVERIFY(!engine.tilingStateForScreen(s1)->tiledWindows().contains(QStringLiteral("A")));
        QVERIFY(engine.tilingStateForScreen(s2)->tiledWindows().contains(QStringLiteral("A")));
    }

    void testCrossScreen_cancelRestoresOriginalScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString s1 = QLatin1String(Screen1);
        const QString s2 = QLatin1String(Screen2);
        engine.setAutotileScreens({s1, s2});
        openWindows(engine, s1, {QStringLiteral("A"), QStringLiteral("B")});
        openWindows(engine, s2, {QStringLiteral("X"), QStringLiteral("Y")});

        const QStringList s1Original = engine.tilingStateForScreen(s1)->tiledWindows();
        const QStringList s2Original = engine.tilingStateForScreen(s2)->tiledWindows();

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), s2));
        engine.cancelDragInsertPreview();

        QVERIFY(!engine.hasDragInsertPreview());
        // A should be back on screen1, screen2 unchanged
        QCOMPARE(engine.tilingStateForScreen(s1)->tiledWindows(), s1Original);
        QCOMPARE(engine.tilingStateForScreen(s2)->tiledWindows(), s2Original);
    }

    // =========================================================================
    // Fresh adoption: untracked window enters autotile stack
    // =========================================================================

    void testFreshAdoption_addsWindowToStack()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        addWindowsToState(engine, screen, {QStringLiteral("A"), QStringLiteral("B")});

        // "newcomer" is not tracked by the engine at all
        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("newcomer"), screen));
        QVERIFY(engine.tilingStateForScreen(screen)->tiledWindows().contains(QStringLiteral("newcomer")));
    }

    void testFreshAdoption_cancelRemovesWindow()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        addWindowsToState(engine, screen, {QStringLiteral("A"), QStringLiteral("B")});

        const QStringList originalOrder = engine.tilingStateForScreen(screen)->tiledWindows();

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("newcomer"), screen));
        engine.cancelDragInsertPreview();

        QVERIFY(!engine.hasDragInsertPreview());
        QCOMPARE(engine.tilingStateForScreen(screen)->tiledWindows(), originalOrder);
    }

    // =========================================================================
    // Idempotency: double cancel is safe
    // =========================================================================

    void testDoubleCancel_isIdempotent()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.cancelDragInsertPreview();
        // Second cancel should be a no-op, not crash
        engine.cancelDragInsertPreview();
        QVERIFY(!engine.hasDragInsertPreview());
    }

    void testDoubleCommit_isIdempotent()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.commitDragInsertPreview();
        engine.commitDragInsertPreview();
        QVERIFY(!engine.hasDragInsertPreview());
    }

    // =========================================================================
    // Begin replaces existing preview
    // =========================================================================

    void testBegin_cancelsExistingPreview()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.updateDragInsertPreview(2);

        // A has been moved to index 2 by the update above; the re-begin must
        // CANCEL that, putting A back at 0.
        const QStringList beforeReBegin = engine.tilingStateForScreen(screen)->tiledWindows();
        QCOMPARE(beforeReBegin.indexOf(QStringLiteral("A")), 2);

        // Starting a new preview for B should cancel A's preview first
        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("B"), screen));
        QCOMPARE(engine.dragInsertPreviewWindowId(), QStringLiteral("B"));
        // The id alone does not show a cancel happened — replacing the implicit
        // cancel with a plain overwrite of the preview would satisfy it while
        // leaving A stranded at 2. Assert A actually went home.
        QCOMPARE(engine.tilingStateForScreen(screen)->tiledWindows().indexOf(QStringLiteral("A")), 0);
    }

    // =========================================================================
    // Update with no-change index is a no-op
    // =========================================================================

    void testUpdate_sameIndexNoOp()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        // A starts at index 0, updating to 0 should be a no-op
        const QStringList before = engine.tilingStateForScreen(screen)->tiledWindows();
        engine.updateDragInsertPreview(0);
        QCOMPARE(engine.tilingStateForScreen(screen)->tiledWindows(), before);
    }

    // =========================================================================
    // Update clamps out-of-range indices
    // =========================================================================

    void testUpdate_clampsNegativeIndex()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("C"), screen));
        // Negative should clamp to 0
        engine.updateDragInsertPreview(-5);
        QCOMPARE(engine.tilingStateForScreen(screen)->tiledWindows()[0], QStringLiteral("C"));
    }

    void testUpdate_clampsOverflowIndex()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        // Index 100 should clamp to last (2)
        engine.updateDragInsertPreview(100);
        QCOMPARE(engine.tilingStateForScreen(screen)->tiledWindows()[2], QStringLiteral("A"));
    }

    // =========================================================================
    // computeDragInsertIndexAtPoint with no state returns -1
    // =========================================================================

    void testComputeIndex_noStateReturnsMinus1()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QCOMPARE(engine.computeDragInsertIndexAtPoint(QStringLiteral("nonexistent"), QPoint(50, 50)), -1);
    }

    /// Every POSITIVE branch of the same function. Only the no-state arm above
    /// was covered, so the hit-test loop, the empty-zones shortcut, the
    /// cursor-over-own-zone identity and both fallbacks could each be
    /// rewritten with the suite still green.
    void testComputeIndex_hitTestAndFallbacks()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);

        // No zones calculated yet: the layout has nothing to hit-test against,
        // so the answer is the head of the list rather than -1. The two are
        // NOT interchangeable — -1 means "no state" and suppresses the drag,
        // 0 means "insert at the front".
        state->setCalculatedZones({});
        QCOMPARE(engine.computeDragInsertIndexAtPoint(screen, QPoint(50, 50)), 0);

        // Three side-by-side zones, one per window.
        const QVector<QRect> zones{QRect(0, 0, 100, 200), QRect(100, 0, 100, 200), QRect(200, 0, 100, 200)};
        state->setCalculatedZones(zones);
        QCOMPARE(engine.computeDragInsertIndexAtPoint(screen, QPoint(50, 100)), 0);
        QCOMPARE(engine.computeDragInsertIndexAtPoint(screen, QPoint(150, 100)), 1);
        QCOMPARE(engine.computeDragInsertIndexAtPoint(screen, QPoint(250, 100)), 2);

        // Outside every zone with no preview live: the last index, not -1 and
        // not 0. Snapping to an endpoint here is what the fallback avoids.
        QCOMPARE(engine.computeDragInsertIndexAtPoint(screen, QPoint(5000, 5000)), 2);

        // With a preview live, the same miss HOLDS the preview's last index
        // instead. Wandering off the layout mid-drag must not yank the
        // window to an endpoint.
        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        state->setCalculatedZones(zones);
        engine.updateDragInsertPreview(1);
        QCOMPARE(engine.computeDragInsertIndexAtPoint(screen, QPoint(5000, 5000)), 1);

        // Cursor over the DRAGGED window's own zone is a stable identity: it
        // answers that zone rather than skipping to a neighbour. Skipping
        // would re-match under the cursor on the next tick and oscillate.
        const int ownIndex = state->tiledWindows().indexOf(QStringLiteral("A"));
        QVERIFY(ownIndex >= 0);
        QVERIFY(ownIndex < zones.size());
        QCOMPARE(engine.computeDragInsertIndexAtPoint(screen, zones.at(ownIndex).center()), ownIndex);
        engine.cancelDragInsertPreview();
    }

    /// The maxWindows cap arm. When the layout produces fewer zones than
    /// there are tiled windows and the DRAGGED window fell past the cap, the
    /// stable-identity contract cannot hold, so the hit-test is skipped
    /// entirely and the preview holds its last index. Without the skip the
    /// cursor would match some other window's zone and shuffle the layout.
    void testComputeIndex_draggedPastTheZoneCapHolds()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.updateDragInsertPreview(2);
        // A only has a zone if the layout produced three. One zone caps the
        // layout at the head window, so the dragged one sits past it.
        state->setCalculatedZones({QRect(0, 0, 100, 200)});
        QCOMPARE(state->tiledWindows().indexOf(QStringLiteral("A")), 2);
        // The cursor is squarely inside zone 0. With the cap check gone the
        // loop returns 0 and the window jumps to the head of the layout.
        QCOMPARE(engine.computeDragInsertIndexAtPoint(screen, QPoint(50, 100)), 2);
        engine.cancelDragInsertPreview();
    }

    // =========================================================================
    // The IPlacementEngine STRUCT seam — the only forms production calls.
    // The int forms above stay engine-local; their clamping contract does
    // NOT cross this seam, so both differences need their own pins.
    // =========================================================================

    void testStructSeam_noStateYieldsInvalidTarget()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const PhosphorEngine::IPlacementEngine::DragInsertTarget target =
            engine.computeDragInsertTargetAtPoint(QStringLiteral("nonexistent"), QPoint(50, 50));
        QVERIFY(!target.isValid());
        QCOMPARE(target.secondary, -1);
        QVERIFY(!target.newSlot);
    }

    void testStructSeam_invalidTargetIsIgnoredNotClamped()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        PhosphorEngine::IPlacementEngine::DragInsertTarget target;
        target.primary = 2;
        engine.updateDragInsertPreview(target);
        const QStringList afterValid = engine.tilingStateForScreen(screen)->tiledWindows();
        QCOMPARE(afterValid[2], QStringLiteral("A"));
        // An invalid struct target is a silent no-op — the int form's
        // clamp-to-0 must NOT happen here, or a stateless-screen wander
        // mid-drag would teleport the preview to the front.
        engine.updateDragInsertPreview(PhosphorEngine::IPlacementEngine::DragInsertTarget{});
        QCOMPARE(engine.tilingStateForScreen(screen)->tiledWindows(), afterValid);
        engine.cancelDragInsertPreview();
    }

    // =========================================================================
    // Commit emits windowFloatingStateSynced for fresh adoption
    // =========================================================================

    void testCommit_freshAdoptionEmitsFloatSync()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        addWindowsToState(engine, screen, {QStringLiteral("A")});

        QSignalSpy spy(&engine, &AutotileEngine::windowFloatingStateSynced);

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("newcomer"), screen));
        engine.commitDragInsertPreview();

        QVERIFY(spy.count() >= 1);
        // Should emit for "newcomer" with floating=false
        bool found = false;
        for (const auto& call : spy) {
            if (call[0].toString() == QStringLiteral("newcomer") && call[1].toBool() == false) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    // =========================================================================
    // Eviction path: maxWindows cap forces a neighbour float on adoption
    // =========================================================================

    void testEviction_cancelRestoresFloatedNeighbour()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 3;
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        const QStringList originalTiled = engine.tilingStateForScreen(screen)->tiledWindows();
        QCOMPARE(originalTiled.size(), 3);

        // Fresh adoption pushes count to 4 > maxWindows=3 → last neighbour is floated.
        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("newcomer"), screen));
        QVERIFY(engine.hasDragInsertPreview());
        QCOMPARE(engine.tilingStateForScreen(screen)->tiledWindowCount(), 3);

        // Cancel must unfloat the evicted neighbour and remove the newcomer.
        engine.cancelDragInsertPreview();
        QVERIFY(!engine.hasDragInsertPreview());
        QCOMPARE(engine.tilingStateForScreen(screen)->tiledWindows(), originalTiled);
    }

    void testEviction_commitEmitsBatchFloated()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B")});

        QSignalSpy batchSpy(&engine, &AutotileEngine::windowsBatchFloated);

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("newcomer"), screen));
        engine.commitDragInsertPreview();

        // Evicted neighbour should be routed through the batch-float signal.
        // The COUNT alone does not show that: a batch naming the newcomer, or
        // any other window, satisfies it. Walk the payload for the evicted id,
        // the way testCommit_freshAdoptionEmitsFloatSync does.
        QVERIFY(batchSpy.count() >= 1);
        bool sawEvicted = false;
        for (const auto& emission : batchSpy) {
            if (emission.first().toStringList().contains(QStringLiteral("B"))) {
                sawEvicted = true;
            }
        }
        QVERIFY2(sawEvicted, "batch-float did not name the evicted neighbour");
    }

    // =========================================================================
    // Window-close bookkeeping
    // =========================================================================

    void testWindowClosed_draggedWindowClearsPreview()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        QVERIFY(engine.hasDragInsertPreview());

        // Dragged window closed mid-preview — preview must vanish, not crash.
        engine.windowClosed(QStringLiteral("A"));
        QVERIFY(!engine.hasDragInsertPreview());
    }

    void testWindowClosed_evictedNeighbourClearsEviction()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("newcomer"), screen));
        QVERIFY(engine.hasDragInsertPreview());

        // B really IS the evicted one — asserted, not assumed. If the eviction
        // policy ever picked first-instead-of-last this slot would quietly
        // degenerate into "close an unrelated tiled window mid-preview" and
        // keep passing, losing the coverage its name promises.
        QVERIFY(!engine.tilingStateForScreen(screen)->tiledWindows().contains(QStringLiteral("B")));

        // Close it while the preview is live.
        QSignalSpy batchSpy(&engine, &AutotileEngine::windowsBatchFloated);
        engine.windowClosed(QStringLiteral("B"));

        // Preview still live (newcomer is the dragged window, not B) and
        // commit/cancel must not crash on the vanished eviction id.
        QVERIFY(engine.hasDragInsertPreview());
        engine.commitDragInsertPreview(); // should be safe
        QVERIFY(!engine.hasDragInsertPreview());
        // The point of the guard this slot is named for: commit must NOT emit
        // a batch-float for the closed id. Without the assertion, deleting the
        // guard leaves the test green while commit routes a dead window id.
        for (const auto& emission : batchSpy) {
            QVERIFY2(!emission.first().toStringList().contains(QStringLiteral("B")),
                     "batch-float named a window that closed mid-preview");
        }
    }

    // =========================================================================
    // setAutotileScreens cancels preview when target screen is removed
    // =========================================================================

    void testSetAutotileScreens_cancelsPreviewOnTargetRemoval()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString s1 = QLatin1String(Screen1);
        const QString s2 = QLatin1String(Screen2);
        engine.setAutotileScreens({s1, s2});
        openWindows(engine, s2, {QStringLiteral("X"), QStringLiteral("Y")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("X"), s2));
        QVERIFY(engine.hasDragInsertPreview());

        // Remove s2 from autotile — preview must be cancelled before its
        // PhosphorTiles::TilingState gets torn down.
        engine.setAutotileScreens({s1});
        QVERIFY(!engine.hasDragInsertPreview());
    }
};

QTEST_MAIN(TestAutotileDragInsert)
#include "test_autotile_drag_insert.moc"
