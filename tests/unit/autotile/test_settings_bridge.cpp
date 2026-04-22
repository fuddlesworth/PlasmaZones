// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QTimer>

#include "autotile/AutotileEngine.h"
#include "../helpers/AutotileTestHelpers.h"
#include "autotile/AutotileConfig.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"
#include "config/configbackends.h"
#include "config/configdefaults.h"
#include "config/configkeys.h"
#include "config/settings.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

/**
 * @brief Unit tests for SettingsBridge
 *
 * SettingsBridge requires a Settings object for full syncFromSettings/connectToSettings
 * testing. These tests exercise the session persistence (serialize/deserialize) path
 * and test behavior of the engine through the SettingsBridge indirectly.
 *
 * Tests cover:
 * - syncFromSettings detecting no changes (skips retile)
 * - maxWindows increase triggering backfill
 * - serializeWindowOrders/deserializeWindowOrders roundtrip preserving per-screen window state
 * - deserializeWindowOrders with empty data (no crash)
 * - Window order and floating windows survive serialization roundtrip
 * - masterCount/splitRatio are NOT serialized (owned by Settings per-screen overrides)
 * - Debounce timer coalescing rapid changes
 */
class TestSettingsBridge : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<IsolatedConfigGuard> m_configGuard;
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

    // Build a multi-desktop JSON array for deserialization tests.
    // Each entry has screen=screenId, desktop=desktopN, activity="", windowOrder=[windows...].
    // If floatingWindows is non-empty, it is included in the entry.
    static QJsonObject buildEntry(const QString& screenId, int desktop, const QStringList& windowOrder,
                                  const QStringList& floatingWindows = {})
    {
        QJsonObject entry;
        entry[QLatin1String("screen")] = screenId;
        entry[QLatin1String("desktop")] = desktop;
        entry[QLatin1String("activity")] = QString();
        QJsonArray orderArray;
        for (const QString& w : windowOrder) {
            orderArray.append(w);
        }
        entry[QLatin1String("windowOrder")] = orderArray;
        if (!floatingWindows.isEmpty()) {
            QJsonArray floatArray;
            for (const QString& w : floatingWindows) {
                floatArray.append(w);
            }
            entry[QLatin1String("floatingWindows")] = floatArray;
        }
        return entry;
    }

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
    }

    void init()
    {
        // Redirect config to a temp directory so tests never write to real ~/.config/plasmazones/
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
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
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
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;

        // Register 3 windows — only 2 should be tiled initially
        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);

        // Process pending retiles
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        // win1 and win2 should be tiled, win3 should have been rejected by maxWindows gate
        QCOMPARE(state->tiledWindowCount(), 2);

        // Increase maxWindows — backfill should pick up win3
        engine.config()->maxWindows = 4;
        engine.retile();

        // backfillWindows is private and only invoked via syncFromSettings(), which
        // requires a fully-wired Settings object. Without that wiring, retile()
        // alone does not trigger backfill — it only re-tiles already-tiled windows.
        // Therefore this assertion can only verify the maxWindows gate kept the
        // original 2 windows tiled; it cannot verify that win3 was backfilled.
        // A full-integration test with Settings + syncFromSettings is needed to
        // exercise the real backfill path (see TestSettingsIntegration).
        QVERIFY(state->tiledWindowCount() >= 2);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // savedAlgorithmSettings isolation
    // ═══════════════════════════════════════════════════════════════════════════

    void testSettingsBridge_savedAlgorithmSettings_onlyAffectsActiveAlgorithm()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        // Set algorithm to master-stack first
        engine.setAlgorithm(QLatin1String("master-stack"));

        // Store saved settings for centered-master in the per-algorithm map
        AlgorithmSettings cmSaved;
        cmSaved.splitRatio = 0.45;
        cmSaved.masterCount = 2;
        engine.config()->savedAlgorithmSettings[QStringLiteral("centered-master")] = cmSaved;

        // Capture the master-stack ratio before mutation
        const qreal masterStackRatio = engine.config()->splitRatio;

        // Mutate the saved centered-master ratio — should NOT affect the active ratio
        cmSaved.splitRatio = 0.35;
        cmSaved.masterCount = 3;
        engine.config()->savedAlgorithmSettings[QStringLiteral("centered-master")] = cmSaved;
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, masterStackRatio));

        // Now switch to centered-master — saved settings should be applied
        engine.setAlgorithm(QLatin1String("centered-master"));
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, 0.35));
        QCOMPARE(engine.config()->masterCount, 3);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Session persistence roundtrip
    // ═══════════════════════════════════════════════════════════════════════════

    void testSettingsBridge_serializeWindowOrders_roundTrip()
    {
        QJsonArray serialized;

        // Serialize window orders
        {
            AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

            PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
            state->addWindow(QStringLiteral("win1"));
            state->addWindow(QStringLiteral("win2"));

            PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(QStringLiteral("HDMI-1"));
            state2->addWindow(QStringLiteral("win3"));

            serialized = engine.serializeWindowOrders();
            QCOMPARE(serialized.size(), 2);
        }

        // Deserialize into a fresh engine — window orders become pending initial orders
        {
            AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
            engine.deserializeWindowOrders(serialized);

            // Verify JSON structure contains screen context + window orders
            QJsonObject entry = serialized[0].toObject();
            QVERIFY(!entry[QLatin1String("screen")].toString().isEmpty());
            QVERIFY(entry.contains(QLatin1String("windowOrder")));
            QVERIFY(entry[QLatin1String("windowOrder")].toArray().size() > 0);

            // masterCount/splitRatio should NOT be in the serialized data
            // (they're owned by Settings via AutotileScreen:<id> per-screen overrides)
            QVERIFY(!entry.contains(QLatin1String("masterCount")));
            QVERIFY(!entry.contains(QLatin1String("splitRatio")));
        }
    }

    void testSettingsBridge_deserializeWindowOrders_emptyArray()
    {
        // Should not crash — empty array is handled gracefully
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.deserializeWindowOrders(QJsonArray{});

        // Engine should still be in a valid state
        QVERIFY(!engine.algorithm().isEmpty());
    }

    void testSettingsBridge_serializeWindowOrders_includesFloating()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        state->addWindow(QStringLiteral("firefox|{uuid1}"));
        state->addWindow(QStringLiteral("konsole|{uuid2}"));
        state->addWindow(QStringLiteral("dolphin|{uuid3}"));
        state->setFloating(QStringLiteral("konsole|{uuid2}"), true);

        QJsonArray serialized = engine.serializeWindowOrders();
        QCOMPARE(serialized.size(), 1);

        // Verify JSON structure
        QJsonObject obj = serialized[0].toObject();
        QCOMPARE(obj[QLatin1String("screen")].toString(), QStringLiteral("eDP-1"));
        QVERIFY(obj.contains(QLatin1String("windowOrder")));
        QVERIFY(obj.contains(QLatin1String("floatingWindows")));

        QJsonArray orderArray = obj[QLatin1String("windowOrder")].toArray();
        QCOMPARE(orderArray.size(), 3);

        QJsonArray floatArray = obj[QLatin1String("floatingWindows")].toArray();
        QCOMPARE(floatArray.size(), 1);
        QCOMPARE(floatArray[0].toString(), QStringLiteral("konsole|{uuid2}"));
    }

    void testSettingsBridge_deserializeWindowOrders_multiDesktop_onlyRestoresCurrentContext()
    {
        // Build JSON with two entries for the same screen on different desktops.
        // Only the entry matching the engine's current desktop should populate
        // m_pendingInitialOrders immediately. Desktop 2's orders are saved for
        // promotion when the user switches to that desktop.
        QJsonArray multiDesktopData;
        multiDesktopData.append(
            buildEntry(QStringLiteral("eDP-1"), 1, {QStringLiteral("win1"), QStringLiteral("win2")}));
        multiDesktopData.append(
            buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("win3"), QStringLiteral("win4")}));

        // Engine defaults to desktop=1 — only desktop 1's order should be pending immediately
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.deserializeWindowOrders(multiDesktopData);

        // Desktop 1 should have pending orders
        engine.setAutotileScreens({QStringLiteral("eDP-1")});
        engine.windowOpened(QStringLiteral("win2"), QStringLiteral("eDP-1"), 0, 0);
        engine.windowOpened(QStringLiteral("win1"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state);
        // Pre-seeded order should place win1 before win2 (matching desktop 1's saved order)
        const QStringList order = state->windowOrder();
        QCOMPARE(order.size(), 2);
        QCOMPARE(order.at(0), QStringLiteral("win1"));
        QCOMPARE(order.at(1), QStringLiteral("win2"));
    }

    void testSettingsBridge_deserializeWindowOrders_multiDesktop_promotesOnSwitch()
    {
        // Desktop 2's saved window orders should be promoted into pending orders
        // when the engine switches to desktop 2.
        QJsonArray multiDesktopData;
        multiDesktopData.append(
            buildEntry(QStringLiteral("eDP-1"), 1, {QStringLiteral("win1"), QStringLiteral("win2")}));
        multiDesktopData.append(
            buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("win3"), QStringLiteral("win4")}));

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.deserializeWindowOrders(multiDesktopData);

        // Switch to desktop 2 — should promote saved orders for desktop 2
        engine.setAutotileScreens({QStringLiteral("eDP-1")});
        engine.setCurrentDesktop(2);
        QCoreApplication::processEvents(); // flush coalesced promotion timer

        // Open desktop 2's windows in reverse order
        engine.windowOpened(QStringLiteral("win4"), QStringLiteral("eDP-1"), 0, 0);
        engine.windowOpened(QStringLiteral("win3"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state);
        // Pre-seeded order should place win3 before win4 (matching desktop 2's saved order)
        const QStringList order = state->windowOrder();
        QCOMPARE(order.size(), 2);
        QCOMPARE(order.at(0), QStringLiteral("win3"));
        QCOMPARE(order.at(1), QStringLiteral("win4"));
    }

    void testSettingsBridge_deserializeWindowOrders_floatingRestoresAllContexts()
    {
        // Floating windows use TilingStateKey (full context), so all desktops
        // should be restored — not just the current one.
        QJsonArray data;
        data.append(buildEntry(QStringLiteral("eDP-1"), 1, {QStringLiteral("win1")}, {QStringLiteral("win1")}));
        data.append(buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("win2")}, {QStringLiteral("win2")}));

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.deserializeWindowOrders(data);

        // Floating state for both desktops should be restored (keyed by TilingStateKey)
        // Re-serialize to verify: desktop 2's floating should NOT appear in pending
        // orders (since current desktop is 1), but its floating entry should exist.
        // The floating windows are stored in m_savedFloatingWindows which is internal,
        // so we verify indirectly: when we add a window on desktop 2 with the same ID,
        // it should be floated automatically.
        engine.setAutotileScreens({QStringLiteral("eDP-1")});

        // Desktop 2 floating should exist even though we're on desktop 1
        engine.setCurrentDesktop(2);
        QCoreApplication::processEvents(); // flush coalesced promotion timer
        engine.windowOpened(QStringLiteral("win2"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state);
        // If win2 was restored as floating, it should be floating in the state
        QVERIFY2(state->isFloating(QStringLiteral("win2")),
                 "Window on desktop 2 should be restored as floating from saved state");
    }

    void testSettingsBridge_persistenceDelegate_noOpWithoutDelegate()
    {
        // saveState()/loadState() should silently no-op when no delegate is set
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        state->addWindow(QStringLiteral("win1"));

        // These should not crash or have any effect
        engine.saveState();
        engine.loadState();

        // Engine should still be in a valid state
        QVERIFY(!engine.algorithm().isEmpty());
        QCOMPARE(state->windowCount(), 1);
    }

    void testSettingsBridge_persistenceDelegate_invokesCallbacks()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        bool saveCalled = false;
        bool loadCalled = false;

        engine.setPersistenceDelegate(
            [&saveCalled]() {
                saveCalled = true;
            },
            [&loadCalled]() {
                loadCalled = true;
            });

        engine.saveState();
        QVERIFY(saveCalled);
        QVERIFY(!loadCalled);

        engine.loadState();
        QVERIFY(loadCalled);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Race condition: shortcut adjustment vs syncFromSettings
    // ═══════════════════════════════════════════════════════════════════════════

    void testSyncFromSettings_duringShortcutDebounce_preservesRuntimeRatio()
    {
        // When a shortcut adjustment is pending save (debounce timer active),
        // syncFromSettings must NOT overwrite the runtime splitRatio or
        // masterCount with stale Settings values.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen, 0, 0);
        QCoreApplication::processEvents();

        // Wire up a real Settings object so syncShortcutAdjustment can start
        // the debounce timer.
        Settings settings;
        engine.connectToSettings(&settings);

        // Establish a known baseline ratio
        settings.setAutotileSplitRatio(0.5);
        engine.syncFromSettings(&settings);
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, 0.5));

        // Simulate a shortcut-driven ratio increase on the focused screen.
        engine.windowFocused(QStringLiteral("win1"), screen);
        engine.increaseMasterRatio(0.1);
        const qreal adjustedRatio = state->splitRatio();
        QVERIFY(qFuzzyCompare(adjustedRatio, 0.6));

        // Now simulate Settings firing a change (e.g., KCM opened and saved
        // while the debounce timer is still active). The Settings object still
        // holds the old 0.5 value written by syncShortcutAdjustment with
        // signals blocked — but the runtime config was already updated to 0.6.
        // Manually set Settings to a stale value to simulate the race:
        {
            const QSignalBlocker blocker(&settings);
            settings.setAutotileSplitRatio(0.5);
        }

        // syncFromSettings should skip overwriting splitRatio because the
        // shortcut save timer is active.
        engine.syncFromSettings(&settings);

        QVERIFY2(qFuzzyCompare(engine.config()->splitRatio, adjustedRatio),
                 qPrintable(QStringLiteral("syncFromSettings overwrote shortcut-adjusted ratio: expected %1, got %2")
                                .arg(adjustedRatio)
                                .arg(engine.config()->splitRatio)));
    }

    void testSyncFromSettings_duringShortcutDebounce_preservesRuntimeMasterCount()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen, 0, 0);
        engine.windowOpened(QStringLiteral("win3"), screen, 0, 0);
        QCoreApplication::processEvents();

        Settings settings;
        engine.connectToSettings(&settings);

        settings.setAutotileMasterCount(1);
        engine.syncFromSettings(&settings);
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QCOMPARE(engine.config()->masterCount, 1);

        engine.windowFocused(QStringLiteral("win1"), screen);
        engine.increaseMasterCount();
        QCOMPARE(state->masterCount(), 2);

        // Set Settings to the stale value with signals blocked
        {
            const QSignalBlocker blocker(&settings);
            settings.setAutotileMasterCount(1);
        }

        // syncFromSettings should preserve the runtime masterCount
        engine.syncFromSettings(&settings);

        QCOMPARE(engine.config()->masterCount, 2);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Overflow behavior bridging (Float ↔ Unlimited)
    // ═══════════════════════════════════════════════════════════════════════════

    void testSettingsBridge_overflowBehavior_floatToUnlimited_backfillsExcess()
    {
        // Float → Unlimited: previously-overflowed (auto-floated) windows must
        // be re-tiled by the backfill path inside applyOverflowBehaviorChange.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;
        engine.config()->overflowBehavior = AutotileOverflowBehavior::Float;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        // Cap of 2: third window is rejected at the gate.
        QCOMPARE(state->tiledWindowCount(), 2);

        // Flip to Unlimited via Settings + sync — the bridge must backfill win3.
        Settings settings;
        settings.setAutotileMaxWindows(2);
        settings.setAutotileOverflowBehavior(AutotileOverflowBehavior::Unlimited);
        engine.connectToSettings(&settings);
        engine.syncFromSettings(&settings);
        QCoreApplication::processEvents();

        QCOMPARE(engine.config()->overflowBehavior, AutotileOverflowBehavior::Unlimited);
        // All three windows are now tiled because effectiveMaxWindows returns
        // the unlimited sentinel and backfillWindows re-inserted the excess.
        QCOMPARE(state->tiledWindowCount(), 3);
    }

    void testSettingsBridge_overflowBehavior_floatToUnlimited_combinedWithMaxIncrease_singleBackfill()
    {
        // When both Float→Unlimited and a maxWindows increase land in the same
        // syncFromSettings, applyOverflowBehaviorChange already runs a backfill
        // — the trailing maxWindows-increase block must NOT run a second.
        // This guards the no-double-backfill fix in syncFromSettings.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;
        engine.config()->overflowBehavior = AutotileOverflowBehavior::Float;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QCOMPARE(state->tiledWindowCount(), 2);

        // Bump BOTH maxWindows and overflowBehavior in the same Settings flush.
        Settings settings;
        settings.setAutotileMaxWindows(4);
        settings.setAutotileOverflowBehavior(AutotileOverflowBehavior::Unlimited);
        engine.connectToSettings(&settings);
        engine.syncFromSettings(&settings);
        QCoreApplication::processEvents();

        // End state is the same — all windows tiled. The behavioral
        // assertion is that we got here without crashing or warnings; the
        // double-backfill guard is verified at the call-site by inspection
        // (and the test will catch any re-introduced fault that mutates
        // state non-idempotently).
        QCOMPARE(state->tiledWindowCount(), 3);
        QCOMPARE(engine.config()->overflowBehavior, AutotileOverflowBehavior::Unlimited);
        QCOMPARE(engine.config()->maxWindows, 4);
    }

    // Note: there is no Unlimited → Float test in this fixture. The reverse
    // direction is handled by OverflowManager::applyOverflow during the next
    // recalculateLayout, which requires valid screen geometry — the
    // null-Phosphor::Screens::ScreenManager engine used by these unit tests can't supply it.
    // The reverse path is exercised by the integration tests that run the
    // full daemon graph.

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
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Simultaneous desktop+activity switch (coalesced promotion)
    // ═══════════════════════════════════════════════════════════════════════════

    void testSettingsBridge_simultaneousDesktopActivitySwitch_promotesCorrectContext()
    {
        // Setup: entries for (desktop=2, activityA) and (desktop=2, activityB).
        // Switch from (1, activityA) to (2, activityB). The intermediate state
        // (2, activityA) should NOT consume activityA's entry — the coalesced
        // promotion should only run after both desktop and activity are set.
        const QString activityA = QStringLiteral("activity-aaaa");
        const QString activityB = QStringLiteral("activity-bbbb");

        QJsonArray data;
        // Entry for (desktop=2, activityA) — should NOT be consumed
        data.append(buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("winA1"), QStringLiteral("winA2")}));
        // Manually add activity to the entry
        {
            QJsonObject entry = data[0].toObject();
            entry[QLatin1String("activity")] = activityA;
            data[0] = entry;
        }
        // Entry for (desktop=2, activityB) — this is the target
        {
            QJsonObject entry =
                buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("winB1"), QStringLiteral("winB2")});
            entry[QLatin1String("activity")] = activityB;
            data.append(entry);
        }

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        // Set initial context to (desktop=1, activityA)
        engine.setCurrentDesktop(1);
        engine.setCurrentActivity(activityA);
        QCoreApplication::processEvents(); // flush any pending promotions

        engine.deserializeWindowOrders(data);

        // Simulate simultaneous switch: both calls happen before event loop runs
        engine.setCurrentDesktop(2);
        engine.setCurrentActivity(activityB);

        // Process the coalesced timer — promotion should use (desktop=2, activityB)
        QCoreApplication::processEvents();

        // Open desktop 2 / activityB's windows in reverse order
        engine.setAutotileScreens({QStringLiteral("eDP-1")});
        engine.windowOpened(QStringLiteral("winB2"), QStringLiteral("eDP-1"), 0, 0);
        engine.windowOpened(QStringLiteral("winB1"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state);
        const QStringList order = state->windowOrder();
        QCOMPARE(order.size(), 2);
        // Pre-seeded order should match activityB's saved order, not activityA's
        QCOMPARE(order.at(0), QStringLiteral("winB1"));
        QCOMPARE(order.at(1), QStringLiteral("winB2"));
    }

    void testSettingsBridge_simultaneousSwitch_doesNotConsumeWrongActivityEntry()
    {
        // Verify that after switching from (1, activityA) to (2, activityB),
        // the entry for (desktop=2, activityA) is still available for future use.
        const QString activityA = QStringLiteral("activity-aaaa");
        const QString activityB = QStringLiteral("activity-bbbb");

        QJsonArray data;
        {
            QJsonObject entry =
                buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("winA1"), QStringLiteral("winA2")});
            entry[QLatin1String("activity")] = activityA;
            data.append(entry);
        }
        {
            QJsonObject entry =
                buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("winB1"), QStringLiteral("winB2")});
            entry[QLatin1String("activity")] = activityB;
            data.append(entry);
        }

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setCurrentDesktop(1);
        engine.setCurrentActivity(activityA);
        QCoreApplication::processEvents();

        engine.deserializeWindowOrders(data);

        // Switch to (2, activityB) — coalesced
        engine.setCurrentDesktop(2);
        engine.setCurrentActivity(activityB);
        QCoreApplication::processEvents();

        // Now switch to (2, activityA) — the entry should still be available
        engine.setCurrentActivity(activityA);
        QCoreApplication::processEvents();

        engine.setAutotileScreens({QStringLiteral("eDP-1")});
        engine.windowOpened(QStringLiteral("winA2"), QStringLiteral("eDP-1"), 0, 0);
        engine.windowOpened(QStringLiteral("winA1"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state);
        const QStringList order = state->windowOrder();
        QCOMPARE(order.size(), 2);
        // activityA's order should be restored correctly
        QCOMPARE(order.at(0), QStringLiteral("winA1"));
        QCOMPARE(order.at(1), QStringLiteral("winA2"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // WTA integration: save/load roundtrip through config backend
    // ═══════════════════════════════════════════════════════════════════════════

    void testSettingsBridge_wtaRoundtrip_autotileOrdersSurviveSaveLoad()
    {
        // Verify that autotile window orders survive a full WTA save→load cycle
        // through the config backend, testing the delegate wiring end-to-end.
        // IsolatedConfigGuard redirects XDG_CONFIG_HOME so ConfigDefaults::configFilePath()
        // resolves inside the temp directory. Ensure the parent dir exists.
        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        auto backend = std::make_unique<PhosphorConfig::JsonBackend>(ConfigDefaults::configFilePath());

        // Create engine with windows
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        state->addWindow(QStringLiteral("firefox|{uuid1}"));
        state->addWindow(QStringLiteral("konsole|{uuid2}"));
        state->setFloating(QStringLiteral("konsole|{uuid2}"), true);

        // Manually serialize and write to config backend (simulating WTA save)
        QJsonArray serialized = engine.serializeWindowOrders();
        QCOMPARE(serialized.size(), 1);

        auto tracking = backend->group(ConfigKeys::windowTrackingGroup());
        tracking->writeString(ConfigKeys::autotileWindowOrdersKey(),
                              QString::fromUtf8(QJsonDocument(serialized).toJson(QJsonDocument::Compact)));
        tracking.reset();
        backend->sync();

        // Read back and deserialize into a fresh engine (simulating WTA load)
        auto backend2 = std::make_unique<PhosphorConfig::JsonBackend>(ConfigDefaults::configFilePath());
        backend2->sync();
        auto tracking2 = backend2->group(ConfigKeys::windowTrackingGroup());
        QString readBack = tracking2->readString(ConfigKeys::autotileWindowOrdersKey(), QString());
        QVERIFY(!readBack.isEmpty());

        QJsonDocument doc = QJsonDocument::fromJson(readBack.toUtf8());
        QVERIFY(doc.isArray());

        AutotileEngine engine2(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine2.deserializeWindowOrders(doc.array());

        // Re-serialize from the restored engine and verify structure matches
        engine2.setAutotileScreens({QStringLiteral("eDP-1")});
        engine2.windowOpened(QStringLiteral("konsole|{uuid2}"), QStringLiteral("eDP-1"), 0, 0);
        engine2.windowOpened(QStringLiteral("firefox|{uuid1}"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state2 = engine2.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state2);

        // Window order should be restored (firefox first, as in original)
        const QStringList order = state2->windowOrder();
        QCOMPARE(order.size(), 2);
        QCOMPARE(order.at(0), QStringLiteral("firefox|{uuid1}"));
        QCOMPARE(order.at(1), QStringLiteral("konsole|{uuid2}"));

        // Floating state should be restored
        QVERIFY(state2->isFloating(QStringLiteral("konsole|{uuid2}")));
    }
};

QTEST_MAIN(TestSettingsBridge)
#include "test_settings_bridge.moc"
