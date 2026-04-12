// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "autotile/AutotileEngine.h"
#include "autotile/AutotileConfig.h"
#include "autotile/TilingState.h"
#include "autotile/AlgorithmRegistry.h"

#include "../helpers/AutotileTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;

/**
 * @brief Tests for the drag-insert preview state machine on AutotileEngine.
 *
 * Covers begin/update/commit/cancel across same-screen reorder, cross-screen
 * adoption, and fresh adoption paths, plus eviction undo on cancel.
 *
 * Uses windowOpened() + processEvents() to register windows through the proper
 * lifecycle (populating m_windowToStateKey), which is required for the engine
 * to detect same-screen vs cross-screen vs fresh adoption paths.
 */
class TestAutotileDragInsert : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

    static constexpr auto Screen1 = "eDP-1";
    static constexpr auto Screen2 = "HDMI-1";

    /// Helper: open windows through the engine lifecycle so m_windowToStateKey
    /// is properly populated (unlike direct TilingState::addWindow).
    void openWindows(AutotileEngine& engine, const QString& screenId, const QStringList& windowIds)
    {
        for (const QString& id : windowIds) {
            engine.windowOpened(id, screenId);
        }
        QCoreApplication::processEvents();
    }

    /// Helper: add windows directly to TilingState WITHOUT populating
    /// m_windowToStateKey. Use only for "fresh adoption" tests where the
    /// window is intentionally untracked by the engine.
    void addWindowsToState(AutotileEngine& engine, const QString& screenId, const QStringList& windowIds)
    {
        TilingState* state = engine.stateForScreen(screenId);
        Q_ASSERT(state);
        for (const QString& id : windowIds) {
            state->addWindow(id);
        }
    }

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
    }

    // =========================================================================
    // Precondition tests
    // =========================================================================

    void testBegin_rejectsEmptyWindowId()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        engine.setAutotileScreens({QLatin1String(Screen1)});
        QVERIFY(!engine.beginDragInsertPreview(QString(), QLatin1String(Screen1)));
        QVERIFY(!engine.hasDragInsertPreview());
    }

    void testBegin_rejectsEmptyScreenId()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        engine.setAutotileScreens({QLatin1String(Screen1)});
        QVERIFY(!engine.beginDragInsertPreview(QStringLiteral("win1"), QString()));
        QVERIFY(!engine.hasDragInsertPreview());
    }

    void testBegin_rejectsNonAutotileScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        engine.setAutotileScreens({QLatin1String(Screen1)});
        QVERIFY(!engine.beginDragInsertPreview(QStringLiteral("win1"), QLatin1String(Screen2)));
        QVERIFY(!engine.hasDragInsertPreview());
    }

    // =========================================================================
    // Same-screen reorder: begin → update → commit
    // =========================================================================

    void testSameScreen_beginSetsPreview()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
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
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));

        // Move A from index 0 to index 2
        engine.updateDragInsertPreview(2);

        TilingState* state = engine.stateForScreen(screen);
        QVERIFY(state);
        const QStringList tiled = state->tiledWindows();
        QCOMPARE(tiled.size(), 3);
        // A should now be at position 2
        QCOMPARE(tiled[2], QStringLiteral("A"));
    }

    void testSameScreen_commitClearsPreview()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.updateDragInsertPreview(2);
        engine.commitDragInsertPreview();

        QVERIFY(!engine.hasDragInsertPreview());
        // Order should persist after commit
        TilingState* state = engine.stateForScreen(screen);
        QCOMPARE(state->tiledWindows()[2], QStringLiteral("A"));
    }

    // =========================================================================
    // Same-screen reorder: cancel restores original order
    // =========================================================================

    void testSameScreen_cancelRestoresOrder()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        const QStringList originalOrder = engine.stateForScreen(screen)->tiledWindows();

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.updateDragInsertPreview(2);
        engine.cancelDragInsertPreview();

        QVERIFY(!engine.hasDragInsertPreview());
        QCOMPARE(engine.stateForScreen(screen)->tiledWindows(), originalOrder);
    }

    // =========================================================================
    // Cross-screen adoption: begin → commit
    // =========================================================================

    void testCrossScreen_adoptionMovesBetweenScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString s1 = QLatin1String(Screen1);
        const QString s2 = QLatin1String(Screen2);
        engine.setAutotileScreens({s1, s2});
        openWindows(engine, s1, {QStringLiteral("A"), QStringLiteral("B")});
        openWindows(engine, s2, {QStringLiteral("X"), QStringLiteral("Y")});

        // Adopt A from screen1 onto screen2
        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), s2));
        QVERIFY(engine.hasDragInsertPreview());

        // A should be gone from screen1 and present on screen2
        QVERIFY(!engine.stateForScreen(s1)->tiledWindows().contains(QStringLiteral("A")));
        QVERIFY(engine.stateForScreen(s2)->tiledWindows().contains(QStringLiteral("A")));
    }

    void testCrossScreen_cancelRestoresOriginalScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString s1 = QLatin1String(Screen1);
        const QString s2 = QLatin1String(Screen2);
        engine.setAutotileScreens({s1, s2});
        openWindows(engine, s1, {QStringLiteral("A"), QStringLiteral("B")});
        openWindows(engine, s2, {QStringLiteral("X"), QStringLiteral("Y")});

        const QStringList s1Original = engine.stateForScreen(s1)->tiledWindows();
        const QStringList s2Original = engine.stateForScreen(s2)->tiledWindows();

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), s2));
        engine.cancelDragInsertPreview();

        QVERIFY(!engine.hasDragInsertPreview());
        // A should be back on screen1, screen2 unchanged
        QCOMPARE(engine.stateForScreen(s1)->tiledWindows(), s1Original);
        QCOMPARE(engine.stateForScreen(s2)->tiledWindows(), s2Original);
    }

    // =========================================================================
    // Fresh adoption: untracked window enters autotile stack
    // =========================================================================

    void testFreshAdoption_addsWindowToStack()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        addWindowsToState(engine, screen, {QStringLiteral("A"), QStringLiteral("B")});

        // "newcomer" is not tracked by the engine at all
        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("newcomer"), screen));
        QVERIFY(engine.stateForScreen(screen)->tiledWindows().contains(QStringLiteral("newcomer")));
    }

    void testFreshAdoption_cancelRemovesWindow()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        addWindowsToState(engine, screen, {QStringLiteral("A"), QStringLiteral("B")});

        const QStringList originalOrder = engine.stateForScreen(screen)->tiledWindows();

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("newcomer"), screen));
        engine.cancelDragInsertPreview();

        QVERIFY(!engine.hasDragInsertPreview());
        QCOMPARE(engine.stateForScreen(screen)->tiledWindows(), originalOrder);
    }

    // =========================================================================
    // Idempotency: double cancel is safe
    // =========================================================================

    void testDoubleCancel_isIdempotent()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
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
        AutotileEngine engine(nullptr, nullptr, nullptr);
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
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        engine.updateDragInsertPreview(2);

        // Starting a new preview for B should cancel A's preview first
        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("B"), screen));
        QCOMPARE(engine.dragInsertPreviewWindowId(), QStringLiteral("B"));
    }

    // =========================================================================
    // Update with no-change index is a no-op
    // =========================================================================

    void testUpdate_sameIndexNoOp()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        // A starts at index 0, updating to 0 should be a no-op
        const QStringList before = engine.stateForScreen(screen)->tiledWindows();
        engine.updateDragInsertPreview(0);
        QCOMPARE(engine.stateForScreen(screen)->tiledWindows(), before);
    }

    // =========================================================================
    // Update clamps out-of-range indices
    // =========================================================================

    void testUpdate_clampsNegativeIndex()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("C"), screen));
        // Negative should clamp to 0
        engine.updateDragInsertPreview(-5);
        QCOMPARE(engine.stateForScreen(screen)->tiledWindows()[0], QStringLiteral("C"));
    }

    void testUpdate_clampsOverflowIndex()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});
        openWindows(engine, screen, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});

        QVERIFY(engine.beginDragInsertPreview(QStringLiteral("A"), screen));
        // Index 100 should clamp to last (2)
        engine.updateDragInsertPreview(100);
        QCOMPARE(engine.stateForScreen(screen)->tiledWindows()[2], QStringLiteral("A"));
    }

    // =========================================================================
    // computeDragInsertIndexAtPoint with no state returns -1
    // =========================================================================

    void testComputeIndex_noStateReturnsMinus1()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        QCOMPARE(engine.computeDragInsertIndexAtPoint(QStringLiteral("nonexistent"), QPoint(50, 50)), -1);
    }

    // =========================================================================
    // Commit emits windowFloatingStateSynced for fresh adoption
    // =========================================================================

    void testCommit_freshAdoptionEmitsFloatSync()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
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
};

QTEST_MAIN(TestAutotileDragInsert)
#include "test_autotile_drag_insert.moc"
