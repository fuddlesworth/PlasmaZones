// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "../helpers/AutotileTestHelpers.h"
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "core/constants.h"

#include "../helpers/ScriptedAlgoTestSetup.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

/**
 * @brief Core AutotileEngine tests: construction, enable/disable, algorithm
 *        selection, state management, config access, and config round-trip.
 */
class TestAutotileEngineCore : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    // =========================================================================
    // Constructor tests
    // =========================================================================

    void testConstruction_defaultValues()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        QVERIFY(!engine.isEnabled());
        QCOMPARE(engine.algorithm(), PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId());
        QVERIFY(engine.config() != nullptr);
    }

    // =========================================================================
    // Enable/disable tests
    // =========================================================================

    void testEnabled_initiallyFalse()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QVERIFY(!engine.isEnabled());
    }

    void testEnabled_setTrue()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &AutotileEngine::enabledChanged);

        QSet<QString> screens{QStringLiteral("HDMI-1")};
        engine.setAutotileScreens(screens);

        QVERIFY(engine.isEnabled());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toBool(), true);
    }

    void testEnabled_noChangeNoSignal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &AutotileEngine::enabledChanged);

        engine.setAutotileScreens({});

        QVERIFY(!engine.isEnabled());
        QCOMPARE(spy.count(), 0);
    }

    void testEnabled_toggleBackAndForth()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &AutotileEngine::enabledChanged);

        QSet<QString> screens{QStringLiteral("HDMI-1")};
        engine.setAutotileScreens(screens);
        engine.setAutotileScreens({});
        engine.setAutotileScreens(screens);

        QVERIFY(engine.isEnabled());
        QCOMPARE(spy.count(), 3);
    }

    // =========================================================================
    // autotileScreensChanged emission on identical-set desktop switches
    // (discussion #219)
    // =========================================================================

    void testScreensChanged_desktopSwitchSameSet_emitsDesktopSwitchSignal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &AutotileEngine::autotileScreensChanged);

        const QSet<QString> screens{QStringLiteral("HDMI-1")};
        // Daemon startup push — establishes the desktop context, never a switch.
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens(screens);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toBool(), false);

        // Desktop switch where the new desktop resolves to the SAME set: the
        // engine must re-emit flagged as a desktop switch so the effect's
        // catch-scan runs for windows moved here while the user was away.
        engine.setCurrentDesktop(2);
        engine.setAutotileScreens(screens);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toStringList(), QStringList{QStringLiteral("HDMI-1")});
        QCOMPARE(spy.at(1).at(1).toBool(), true);
    }

    void testScreensChanged_initialDesktopPushIsNotASwitch()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy screensSpy(&engine, &AutotileEngine::autotileScreensChanged);
        QSignalSpy enabledSpy(&engine, &AutotileEngine::enabledChanged);

        // Daemon startup while the user sits on desktop 5: the very first
        // context push must NOT read as a desktop switch — login with
        // autotile enabled needs the genuine enabledChanged +
        // isDesktopSwitch=false sequence so the effect initializes window
        // tracking instead of treating it as a desktop return.
        engine.setCurrentDesktop(5);
        engine.setAutotileScreens({QStringLiteral("HDMI-1")});
        QCOMPARE(enabledSpy.count(), 1);
        QCOMPARE(enabledSpy.at(0).at(0).toBool(), true);
        QCOMPARE(screensSpy.count(), 1);
        QCOMPARE(screensSpy.at(0).at(1).toBool(), false);
    }

    void testScreensChanged_sameSetNoDesktopSwitch_noSignal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &AutotileEngine::autotileScreensChanged);

        const QSet<QString> screens{QStringLiteral("HDMI-1")};
        engine.setAutotileScreens(screens);
        // Same-set recompute outside a desktop/activity switch (settings
        // change, layout reassignment) must stay silent.
        engine.setAutotileScreens(screens);

        QCOMPARE(spy.count(), 1);
    }

    void testScreensChanged_desktopSwitchSameSet_flagConsumedForNextToggle()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &AutotileEngine::autotileScreensChanged);

        const QSet<QString> screens{QStringLiteral("HDMI-1")};
        engine.setCurrentDesktop(1); // startup push — establishes context
        engine.setAutotileScreens(screens);
        engine.setCurrentDesktop(2);
        engine.setAutotileScreens(screens);
        QCOMPARE(spy.count(), 2);

        // The identical-set early return consumed the desktop-switch flag, so
        // a later genuine toggle OFF must report isDesktopSwitch=false — the
        // effect relies on that to run its geometry/border restore.
        engine.setAutotileScreens({});
        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(2).at(0).toStringList(), QStringList());
        QCOMPARE(spy.at(2).at(1).toBool(), false);
    }

    void testScreensChanged_desktopSwitchEmptySet_noSignal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &AutotileEngine::autotileScreensChanged);

        // Both desktops resolve to an empty set: no screen autotiles anywhere,
        // so there is nothing for the effect's catch-scan to do — no wakeup.
        engine.setCurrentDesktop(1); // startup push — establishes context
        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({});
        QCOMPARE(spy.count(), 0);

        // The flag was still consumed: a following enable is a genuine toggle.
        engine.setAutotileScreens({QStringLiteral("HDMI-1")});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toBool(), false);
    }

    void testScreensChanged_activitySwitchSameSet_emitsDesktopSwitchSignal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &AutotileEngine::autotileScreensChanged);

        const QSet<QString> screens{QStringLiteral("HDMI-1")};
        // First non-empty activity push is initialization, NOT a switch —
        // same established-context arming as the desktop side.
        engine.setCurrentActivity(QStringLiteral("activity-a"));
        engine.setAutotileScreens(screens);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toBool(), false);

        // A genuine activity→activity switch with an identical set must arm
        // the flag and re-emit, same wire contract as the desktop case.
        engine.setCurrentActivity(QStringLiteral("activity-b"));
        engine.setAutotileScreens(screens);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toStringList(), QStringList{QStringLiteral("HDMI-1")});
        QCOMPARE(spy.at(1).at(1).toBool(), true);
    }

    void testWindowFocused_contextOnlyDelta_defersAndRevalidates()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("HDMI-1");
        const QString win = QStringLiteral("w|1");
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen});
        engine.windowOpened(win, screen);
        QCoreApplication::processEvents();
        QVERIFY(engine.tilingStateForScreen(screen)->containsWindow(win));

        // Focus event against a STALE engine context (alt-tab race: the
        // focus D-Bus call outran the daemon's desktop push). The
        // context-only key delta must DEFER — and when the push "arrives"
        // before the queued re-check runs, nothing migrates: the window
        // stays in its rightful desktop-1 state.
        engine.setCurrentDesktop(2);
        engine.windowFocused(win, screen);
        engine.setCurrentDesktop(1);
        QCoreApplication::processEvents();
        QVERIFY(engine.tilingStateForScreen(screen)->containsWindow(win));

        // A PERSISTING mismatch (the window genuinely lives in the new
        // context now): the deferred re-check migrates it into the current
        // desktop's state and out of the old one.
        engine.setCurrentDesktop(2);
        engine.windowFocused(win, screen);
        QCoreApplication::processEvents();
        QVERIFY(engine.tilingStateForScreen(screen)->containsWindow(win)); // desktop-2 state
        engine.setCurrentDesktop(1);
        QVERIFY(!engine.tilingStateForScreen(screen)->containsWindow(win)); // gone from desktop-1
    }

    void testWindowFocused_activityOnlyDelta_defersAndRevalidates()
    {
        // Activity flavor of the context-only deferral: the key delta is in
        // the activity dimension instead of the desktop one. Same contract —
        // a push arriving before the queued re-check means no migration; a
        // persisting mismatch migrates into the current activity's state.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("HDMI-1");
        const QString win = QStringLiteral("w|1");
        const QString actA = QStringLiteral("activity-a");
        const QString actB = QStringLiteral("activity-b");
        engine.setCurrentActivity(actA);
        engine.setAutotileScreens({screen});
        engine.windowOpened(win, screen);
        QCoreApplication::processEvents();
        QVERIFY(engine.tilingStateForScreen(screen)->containsWindow(win));

        // Focus outran the activity push, push arrives before the re-check:
        // the window stays in activity-a's state.
        engine.setCurrentActivity(actB);
        engine.windowFocused(win, screen);
        engine.setCurrentActivity(actA);
        QCoreApplication::processEvents();
        QVERIFY(engine.tilingStateForScreen(screen)->containsWindow(win));

        // Persisting mismatch: the deferred re-check migrates the window
        // into activity-b's state and out of activity-a's.
        engine.setCurrentActivity(actB);
        engine.windowFocused(win, screen);
        QCoreApplication::processEvents();
        QVERIFY(engine.tilingStateForScreen(screen)->containsWindow(win)); // activity-b state
        engine.setCurrentActivity(actA);
        QVERIFY(!engine.tilingStateForScreen(screen)->containsWindow(win)); // gone from activity-a
    }

    // =========================================================================
    // Algorithm selection tests
    // =========================================================================

    void testAlgorithm_defaultIsBsp()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QCOMPARE(engine.algorithm(), QLatin1String("bsp"));
    }

    void testAlgorithm_setValid()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::algorithmChanged);

        engine.setAlgorithm(QLatin1String("columns"));

        QCOMPARE(engine.algorithm(), QLatin1String("columns"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QLatin1String("columns"));
    }

    void testAlgorithm_setInvalidFallsBackToDefault()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        engine.setAlgorithm(QLatin1String("master-stack"));
        QCOMPARE(engine.algorithm(), QLatin1String("master-stack"));

        QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::algorithmChanged);
        engine.setAlgorithm(QStringLiteral("nonexistent-algorithm"));

        QCOMPARE(engine.algorithm(), PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId());
        QCOMPARE(spy.count(), 1);
    }

    void testAlgorithm_sameValueNoSignal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::algorithmChanged);

        engine.setAlgorithm(engine.algorithm());

        QCOMPARE(spy.count(), 0);
    }

    void testAlgorithm_currentAlgorithmNotNull()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QVERIFY(engine.currentAlgorithm() != nullptr);
    }

    // =========================================================================
    // State management tests
    // =========================================================================

    void testStateForScreen_createNew()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("TestScreen"));

        QVERIFY(state != nullptr);
        QCOMPARE(state->screenId(), QStringLiteral("TestScreen"));
    }

    void testStateForScreen_returnsSameInstance()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(QStringLiteral("TestScreen"));
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(QStringLiteral("TestScreen"));

        QCOMPARE(state1, state2);
    }

    void testStateForScreen_differentScreensDifferentStates()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(QStringLiteral("Screen1"));
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(QStringLiteral("Screen2"));

        QVERIFY(state1 != state2);
        QCOMPARE(state1->screenId(), QStringLiteral("Screen1"));
        QCOMPARE(state2->screenId(), QStringLiteral("Screen2"));
    }

    void testStateForScreen_inheritsConfigDefaults()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        AutotileConfig* config = engine.config();
        config->masterCount = 2;
        config->splitRatio = 0.7;

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("TestScreen"));

        QCOMPARE(state->splitRatio(), 0.7);

        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));
        state->setMasterCount(2);
        QCOMPARE(state->masterCount(), 2);
    }

    // =========================================================================
    // Config access tests
    // =========================================================================

    void testConfig_notNull()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QVERIFY(engine.config() != nullptr);
    }

    void testConfig_modifiable()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        AutotileConfig* config = engine.config();

        config->innerGap = 20;
        config->outerGap = 15;

        QCOMPARE(engine.config()->innerGap, 20);
        QCOMPARE(engine.config()->outerGap, 15);
    }

    // =========================================================================
    // Config round-trip tests
    // =========================================================================

    void testConfigRoundTrip()
    {
        AutotileConfig original;
        original.innerGap = 5;
        original.outerGap = 10;
        original.splitRatio = 0.65;
        original.masterCount = 2;
        original.algorithmId = QStringLiteral("bsp");
        original.smartGaps = false;
        original.focusNewWindows = false;
        original.focusFollowsMouse = true;
        original.respectMinimumSize = false;
        original.insertPosition = AutotileConfig::InsertPosition::AfterFocused;
        original.overflowBehavior = PhosphorTiles::AutotileOverflowBehavior::Unlimited;

        QJsonObject json = original.toJson();
        AutotileConfig restored = AutotileConfig::fromJson(json);

        QCOMPARE(restored.innerGap, original.innerGap);
        QCOMPARE(restored.outerGap, original.outerGap);
        QCOMPARE(restored.splitRatio, original.splitRatio);
        QCOMPARE(restored.masterCount, original.masterCount);
        QCOMPARE(restored.algorithmId, original.algorithmId);
        QCOMPARE(restored.smartGaps, original.smartGaps);
        QCOMPARE(restored.focusNewWindows, original.focusNewWindows);
        QCOMPARE(restored.focusFollowsMouse, original.focusFollowsMouse);
        QCOMPARE(restored.respectMinimumSize, original.respectMinimumSize);
        QCOMPARE(restored.insertPosition, original.insertPosition);
        // Round-trip must preserve overflowBehavior — not including it in
        // toJson/fromJson silently reset the field on any layout reload or
        // per-screen config snapshot (pre-fix landmine).
        QCOMPARE(restored.overflowBehavior, original.overflowBehavior);
    }

    // =========================================================================
    // AutotileConfig equality: overflowBehavior must participate so change
    // detection (syncFromSettings, per-screen propagation) sees real deltas.
    // =========================================================================

    void testConfigEquality_overflowBehaviorParticipates()
    {
        AutotileConfig a;
        AutotileConfig b;
        QVERIFY(a == b);

        b.overflowBehavior = PhosphorTiles::AutotileOverflowBehavior::Unlimited;
        QVERIFY(a != b);

        a.overflowBehavior = PhosphorTiles::AutotileOverflowBehavior::Unlimited;
        QVERIFY(a == b);
    }

    // =========================================================================
    // isWindowTiled: helper used by WindowDragAdaptor to decide whether to
    // enter drag-insert preview on a reorder drag. A window is "tiled" iff it
    // is tracked (m_states maps it to a state) AND not floating in
    // that state.
    // =========================================================================

    void testIsWindowTiled_untrackedReturnsFalse()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QVERIFY(!engine.isWindowTiled(QStringLiteral("never-opened")));
    }

    void testIsWindowTiled_trackedAndFloatingReturnsFalse()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        const QString windowId = QStringLiteral("win-1");
        engine.setAutotileScreens({screen});
        engine.windowOpened(windowId, screen);
        QCoreApplication::processEvents();

        // Tracked, not yet floating → tiled.
        QVERIFY(engine.isWindowTiled(windowId));

        // Flip to floating via the tiling state — the helper must now return false.
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        state->setFloating(windowId, true);
        QVERIFY(!engine.isWindowTiled(windowId));

        // Unfloat → tiled again.
        state->setFloating(windowId, false);
        QVERIFY(engine.isWindowTiled(windowId));
    }

    // =========================================================================
    // Retile tests (disabled engine)
    // =========================================================================

    void testRetile_disabledEngineNoOp()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QVERIFY(!engine.isEnabled());

        engine.retile();
        engine.retile(QStringLiteral("SomeScreen"));
    }

    // =========================================================================
    // Window lifecycle tests
    // =========================================================================

    void testWindowLifecycle()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString screenName = QStringLiteral("TestScreen");
        const QString windowId = QStringLiteral("win-lifecycle-1");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        QVERIFY(engine.isEnabled());

        QSignalSpy tilingSpy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

        engine.windowOpened(windowId, screenName);

        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QVERIFY(state != nullptr);
        QVERIFY(state->containsWindow(windowId));
        QCOMPARE(state->windowCount(), 1);

        QVERIFY(tilingSpy.count() >= 1);
        QCOMPARE(tilingSpy.last().first().toString(), screenName);

        tilingSpy.clear();
        engine.windowClosed(windowId);

        QCoreApplication::processEvents();

        QVERIFY(!state->containsWindow(windowId));
        QCOMPARE(state->windowCount(), 0);

        QVERIFY(tilingSpy.count() >= 1);
        QCOMPARE(tilingSpy.last().first().toString(), screenName);
    }

    // =========================================================================
    // lastManagedRect: the tile rect applyTiling last emitted for a window,
    // remembered PAST the float flip. performToggleFloat clears the tiled bit
    // before the compositor repositions the window, so the capture orchestrator
    // needs this rect to recognise the still-tiled live frame and refuse it as
    // float-back geometry (the "float restores onto its own tile" regression).
    // =========================================================================

    void testLastManagedRect_survivesFloatToggle_clearedOnClose()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("DP-1");
        engine.setAutotileScreens({screenName});

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        QCoreApplication::processEvents();

        // Force zones directly — unit tests have no real screen geometry, so
        // recalculateLayout() bails and applyTiling consumes what we set.
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QVERIFY(state);
        const QRect zoneA(10, 10, 950, 1060);
        const QRect zoneB(960, 10, 950, 1060);
        state->setCalculatedZones({zoneA, zoneB});
        engine.retile(screenName);

        const QStringList tiled = state->tiledWindows();
        QCOMPARE(tiled.size(), 2);
        QCOMPARE(engine.lastManagedRect(tiled.at(0)), zoneA);
        QCOMPARE(engine.lastManagedRect(tiled.at(1)), zoneB);
        QVERIFY(!engine.lastManagedRect(QStringLiteral("never-seen")).isValid());

        // The regression pin: after the float toggle the tiled bit is gone,
        // but the last-applied rect must still answer.
        const QString floated = tiled.at(0);
        engine.toggleWindowFloat(floated, screenName);
        QVERIFY(state->isFloating(floated));
        QCOMPARE(engine.lastManagedRect(floated), zoneA);

        engine.windowClosed(floated);
        QVERIFY(!engine.lastManagedRect(floated).isValid());
    }

    void testLastManagedRect_prunedWithStaleWindows()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("DP-1");
        engine.setAutotileScreens({screenName});

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QVERIFY(state);
        state->setCalculatedZones({QRect(10, 10, 950, 1060), QRect(960, 10, 950, 1060)});
        engine.retile(screenName);
        QVERIFY(engine.lastManagedRect(QStringLiteral("win-1")).isValid());

        engine.pruneStaleWindows({QStringLiteral("win-2")});
        QVERIFY(!engine.lastManagedRect(QStringLiteral("win-1")).isValid());
        QVERIFY(engine.lastManagedRect(QStringLiteral("win-2")).isValid());
    }
};

QTEST_MAIN(TestAutotileEngineCore)
#include "test_autotile_engine_core.moc"
