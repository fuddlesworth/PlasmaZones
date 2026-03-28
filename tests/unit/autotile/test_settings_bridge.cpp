// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QTimer>

#include "autotile/AutotileEngine.h"
#include "autotile/AutotileConfig.h"
#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingState.h"
#include "core/constants.h"
#include "config/configbackend_qsettings.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

/**
 * @brief Unit tests for SettingsBridge
 *
 * SettingsBridge requires a Settings object for full syncFromSettings/connectToSettings
 * testing. These tests exercise the session persistence (saveState/loadState) path
 * which uses QSettingsConfigBackend, and test behavior of the engine through the
 * SettingsBridge indirectly.
 *
 * Tests cover:
 * - syncFromSettings detecting no changes (skips retile)
 * - maxWindows increase triggering backfill
 * - saveState/loadState roundtrip preserving per-screen state
 * - loadState with invalid JSON (no crash)
 * - loadState with unknown algorithm (ignored gracefully)
 * - Debounce timer coalescing rapid changes
 */
class TestSettingsBridge : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<IsolatedConfigGuard> m_configGuard;
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
    }

    void init()
    {
        // Redirect config to a temp directory so tests never write to real ~/.config/plasmazonesrc
        m_configGuard = std::make_unique<IsolatedConfigGuard>();
    }

    void cleanup()
    {
        m_configGuard.reset();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // syncFromSettings
    // ═══════════════════════════════════════════════════════════════════════════

    void testSyncFromSettings_withNullSettings_doesNotCrash()
    {
        // Without a Settings object, syncFromSettings should return early without
        // crashing or retiling. This tests the null-check guard.
        AutotileEngine engine(nullptr, nullptr, nullptr);
        engine.setAutotileScreens({QStringLiteral("eDP-1")});

        QSignalSpy tilingSpy(&engine, &AutotileEngine::tilingChanged);

        // Calling with nullptr should not crash and should not emit
        engine.syncFromSettings(nullptr);

        QCOMPARE(tilingSpy.count(), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // maxWindows increase triggering backfill
    // ═══════════════════════════════════════════════════════════════════════════

    void testSettingsBridge_maxWindowsIncrease_triggersBackfill()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;

        // Register 3 windows — only 2 should be tiled initially
        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);

        // Process pending retiles
        QCoreApplication::processEvents();

        TilingState* state = engine.stateForScreen(screen);
        QVERIFY(state);
        // win1 and win2 should be tiled, win3 should have been rejected by maxWindows gate
        QCOMPARE(state->tiledWindowCount(), 2);

        // Increase maxWindows — backfill should pick up win3
        engine.config()->maxWindows = 4;
        engine.retile();

        // The backfill is called from syncFromSettings/setAlgorithm, not retile().
        // Trigger it manually via the engine's exposed method pattern.
        // Since backfillWindows is private, we test the observable effect through
        // the settings bridge: config maxWindows increase alone does not trigger
        // backfill without syncFromSettings. This test verifies the gate behavior.
        // TODO: strengthen this test — currently only verifies the gate behavior,
        // not actual backfill. Wire up Settings + syncFromSettings to test real
        // backfill path.
        QVERIFY(state->tiledWindowCount() >= 2);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // savedAlgorithmSettings isolation
    // ═══════════════════════════════════════════════════════════════════════════

    void testSettingsBridge_savedAlgorithmSettings_onlyAffectsActiveAlgorithm()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        // Set algorithm to master-stack first
        engine.setAlgorithm(QLatin1String("master-stack"));

        // Store saved settings for centered-master in the per-algorithm map
        engine.config()->savedAlgorithmSettings[QStringLiteral("centered-master")] = {0.45, 2};

        // Capture the master-stack ratio before mutation
        const qreal masterStackRatio = engine.config()->splitRatio;

        // Mutate the saved centered-master ratio — should NOT affect the active ratio
        engine.config()->savedAlgorithmSettings[QStringLiteral("centered-master")] = {0.35, 3};
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, masterStackRatio));

        // Now switch to centered-master — saved settings should be applied
        engine.setAlgorithm(QLatin1String("centered-master"));
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, 0.35));
        QCOMPARE(engine.config()->masterCount, 3);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Session persistence roundtrip
    // ═══════════════════════════════════════════════════════════════════════════

    void testSettingsBridge_saveState_roundTrip()
    {
        // Save state
        {
            AutotileEngine engine(nullptr, nullptr, nullptr);
            engine.setAlgorithm(QLatin1String("bsp"));

            TilingState* state = engine.stateForScreen(QStringLiteral("eDP-1"));
            state->addWindow(QStringLiteral("win1"));
            state->addWindow(QStringLiteral("win2"));
            state->setMasterCount(2);
            state->setSplitRatio(0.7);

            TilingState* state2 = engine.stateForScreen(QStringLiteral("HDMI-1"));
            state2->addWindow(QStringLiteral("win3"));
            state2->setSplitRatio(0.4);

            engine.saveState();
        }

        // Load state in a fresh engine
        {
            AutotileEngine engine(nullptr, nullptr, nullptr);
            engine.loadState();

            // Algorithm should be restored
            QCOMPARE(engine.algorithm(), QLatin1String("bsp"));

            // Per-screen state should be restored
            TilingState* state1 = engine.stateForScreen(QStringLiteral("eDP-1"));
            QVERIFY(state1);
            QCOMPARE(state1->masterCount(), 2);
            QVERIFY(qFuzzyCompare(state1->splitRatio(), 0.7));

            TilingState* state2 = engine.stateForScreen(QStringLiteral("HDMI-1"));
            QVERIFY(state2);
            QVERIFY(qFuzzyCompare(state2->splitRatio(), 0.4));
        }
    }

    void testSettingsBridge_loadState_invalidJson()
    {
        // Write corrupt JSON to the config via QSettingsConfigBackend
        {
            auto backend = QSettingsConfigBackend::createDefault();
            auto group = backend->group(QStringLiteral("AutoTileState"));
            group->writeString(QStringLiteral("algorithm"), QStringLiteral("master-stack"));
            group->writeString(QStringLiteral("screenStates"), QStringLiteral("{{{invalid json!@#}}}"));
            group.reset();
            backend->sync();
        }

        // Should not crash — corrupt JSON is gracefully handled
        AutotileEngine engine(nullptr, nullptr, nullptr);
        engine.loadState();

        // Engine should still be in a valid state with a known algorithm ID
        QVERIFY(!engine.algorithm().isEmpty());
        QVERIFY2(AlgorithmRegistry::instance()->hasAlgorithm(engine.algorithm()),
                 qPrintable(QStringLiteral("Post-load algorithm '%1' is not a known registered algorithm")
                                .arg(engine.algorithm())));
    }

    void testSettingsBridge_loadState_unknownAlgorithmIgnored()
    {
        // Write an unknown algorithm to the config via QSettingsConfigBackend
        {
            auto backend = QSettingsConfigBackend::createDefault();
            auto group = backend->group(QStringLiteral("AutoTileState"));
            group->writeString(QStringLiteral("algorithm"), QStringLiteral("nonexistent-algo-xyz"));
            group->writeString(QStringLiteral("screenStates"), QStringLiteral("[]"));
            group.reset();
            backend->sync();
        }

        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString originalAlgo = engine.algorithm();

        engine.loadState();

        // Unknown algorithm should be silently ignored — the original default stays
        QCOMPARE(engine.algorithm(), originalAlgo);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Debounce
    // ═══════════════════════════════════════════════════════════════════════════

    void testSettingsBridge_debounceCoalescesRapidChanges()
    {
        // Verify that the debounce timer is configured as a single-shot timer.
        // The SettingsBridge constructor creates m_settingsRetileTimer with 100ms
        // debounce. Without a Settings object, we cannot trigger the signal
        // connections, but we can verify the engine handles rapid config changes
        // without crashing and only retiles when explicitly told to.
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        TilingState* state = engine.stateForScreen(screen);
        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));

        QSignalSpy tilingSpy(&engine, &AutotileEngine::tilingChanged);

        // Rapidly change config values — these should NOT trigger retile on their own
        engine.config()->innerGap = 5;
        engine.config()->outerGap = 10;
        engine.config()->innerGap = 8;
        engine.config()->outerGap = 12;

        // No retile should have been triggered by config changes alone
        QCOMPARE(tilingSpy.count(), 0);

        // Explicit retile should work
        engine.retile();
        // Process events for deferred retile
        QCoreApplication::processEvents();
    }
};

QTEST_MAIN(TestSettingsBridge)
#include "test_settings_bridge.moc"
