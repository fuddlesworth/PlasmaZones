// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "autotile/AutotileEngine.h"
#include "../helpers/AutotileTestHelpers.h"
#include "autotile/AutotileConfig.h"
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "core/constants.h"

#include "../helpers/ScriptedAlgoTestSetup.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace PlasmaZones;

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
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
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
        QSignalSpy spy(&engine, &PhosphorEngineApi::PlacementEngineBase::algorithmChanged);

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

        QSignalSpy spy(&engine, &PhosphorEngineApi::PlacementEngineBase::algorithmChanged);
        engine.setAlgorithm(QStringLiteral("nonexistent-algorithm"));

        QCOMPARE(engine.algorithm(), PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId());
        QCOMPARE(spy.count(), 1);
    }

    void testAlgorithm_sameValueNoSignal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSignalSpy spy(&engine, &PhosphorEngineApi::PlacementEngineBase::algorithmChanged);

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
    // is tracked (m_windowToStateKey maps it to a state) AND not floating in
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

        QSignalSpy tilingSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::placementChanged);

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
};

QTEST_MAIN(TestAutotileEngineCore)
#include "test_autotile_engine_core.moc"
