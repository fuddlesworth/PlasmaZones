// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include <PhosphorEngineApi/IPlacementEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "../helpers/AutotileTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;

/**
 * @brief Tests for the IPlacementEngine cross-engine handoff contract on
 *        AutotileEngine: handoffReceive / handoffRelease / screenForTrackedWindow.
 *
 * The handoff API replaces the opportunistic adopt branch in toggleWindowFloat
 * (PR #366) with an explicit two-step transaction the daemon orchestrates.
 * These tests pin the engine-local invariants the daemon depends on:
 *  - receive populates per-state tracking + m_windowToStateKey
 *  - release drops tracking without mutating geometry
 *  - cross-screen receive on the same engine releases prior tracking first
 *  - screenForTrackedWindow follows ownership transfers
 */
class TestAutotileHandoff : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;
    static constexpr auto Screen1 = "eDP-1";
    static constexpr auto Screen2 = "HDMI-1";

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
    }

    // =========================================================================
    // handoffReceive — adoption + initial state
    // =========================================================================

    void testHandoffReceive_addsWindowAsFloating()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});

        const QString windowId = QStringLiteral("win-floating");
        PhosphorEngineApi::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = screen;
        ctx.fromEngineId = QStringLiteral("snap");
        ctx.wasFloating = true;
        engine.handoffReceive(ctx);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(windowId));
        QVERIFY(state->isFloating(windowId));
        QCOMPARE(engine.screenForTrackedWindow(windowId), screen);
    }

    void testHandoffReceive_addsWindowAsTiled()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});

        const QString windowId = QStringLiteral("win-tiled");
        PhosphorEngineApi::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = screen;
        ctx.fromEngineId = QStringLiteral("autotile");
        ctx.wasFloating = false;
        engine.handoffReceive(ctx);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(windowId));
        QVERIFY(!state->isFloating(windowId));
        QCOMPARE(engine.screenForTrackedWindow(windowId), screen);
    }

    void testHandoffReceive_rejectsNonAutotileScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});

        PhosphorEngineApi::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = QStringLiteral("win-x");
        ctx.toScreenId = QLatin1String(Screen2); // not an autotile screen
        ctx.wasFloating = true;
        engine.handoffReceive(ctx);

        QCOMPARE(engine.screenForTrackedWindow(QStringLiteral("win-x")), QString());
    }

    void testHandoffReceive_emptyArgsIsNoop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});

        PhosphorEngineApi::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = QString();
        ctx.toScreenId = QLatin1String(Screen1);
        engine.handoffReceive(ctx);

        ctx.windowId = QStringLiteral("win-x");
        ctx.toScreenId = QString();
        engine.handoffReceive(ctx);

        QCOMPARE(engine.screenForTrackedWindow(QStringLiteral("win-x")), QString());
    }

    // =========================================================================
    // handoffReceive — already-tracked / cross-screen edge cases
    // =========================================================================

    void testHandoffReceive_alreadyTrackedSameScreenIsNoop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});

        const QString windowId = QStringLiteral("win-existing");
        engine.windowOpened(windowId, screen);
        QCoreApplication::processEvents();

        // Idempotent receive: floating disposition shouldn't overwrite the
        // existing tiled state.
        PhosphorEngineApi::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = screen;
        ctx.wasFloating = true;
        engine.handoffReceive(ctx);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(windowId));
        QVERIFY(!state->isFloating(windowId));
    }

    void testHandoffReceive_crossScreenReleasesPreviousState()
    {
        // The PR review identified an orphaned-state bug: receiving a
        // window that's already tracked on a DIFFERENT autotile screen
        // would overwrite m_windowToStateKey to point at the new screen
        // while leaving the old screen's PhosphorTiles::TilingState holding the
        // window. Fix: release the previous state first.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString s1 = QLatin1String(Screen1);
        const QString s2 = QLatin1String(Screen2);
        engine.setAutotileScreens({s1, s2});

        const QString windowId = QStringLiteral("win-cross");
        engine.windowOpened(windowId, s1);
        QCoreApplication::processEvents();

        // Hand off to the OTHER autotile screen.
        PhosphorEngineApi::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = s2;
        ctx.wasFloating = false;
        engine.handoffReceive(ctx);
        QCoreApplication::processEvents();

        // s2 has it.
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(s2);
        QVERIFY(state2);
        QVERIFY(state2->containsWindow(windowId));
        // s1 must NOT have it — that's the orphan we're guarding against.
        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(s1);
        QVERIFY(state1);
        QVERIFY(!state1->containsWindow(windowId));
        QCOMPARE(engine.screenForTrackedWindow(windowId), s2);
    }

    // =========================================================================
    // handoffRelease — drops tracking without mutating geometry
    // =========================================================================

    void testHandoffRelease_clearsTracking()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QLatin1String(Screen1);
        engine.setAutotileScreens({screen});

        const QString windowId = QStringLiteral("win-rel");
        engine.windowOpened(windowId, screen);
        QCoreApplication::processEvents();
        QCOMPARE(engine.screenForTrackedWindow(windowId), screen);

        engine.handoffRelease(windowId);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(!state->containsWindow(windowId));
        QCOMPARE(engine.screenForTrackedWindow(windowId), QString());
        QVERIFY(!engine.isWindowTracked(windowId));
    }

    void testHandoffRelease_untrackedWindowIsNoop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});

        engine.handoffRelease(QStringLiteral("never-tracked"));
        engine.handoffRelease(QString());
        // Did not crash; nothing tracked.
        QVERIFY(!engine.isWindowTracked(QStringLiteral("never-tracked")));
    }

    // =========================================================================
    // engineId — stable identity used in HandoffContext.fromEngineId
    // =========================================================================

    void testEngineId_returnsAutotile()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QCOMPARE(engine.engineId(), QStringLiteral("autotile"));
    }

    // =========================================================================
    // screenForTrackedWindow — symmetry with snap-side
    // =========================================================================

    void testScreenForTrackedWindow_returnsEmptyForUnknown()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QLatin1String(Screen1)});
        QCOMPARE(engine.screenForTrackedWindow(QStringLiteral("never-seen")), QString());
    }
};

QTEST_MAIN(TestAutotileHandoff)
#include "test_autotile_handoff.moc"
