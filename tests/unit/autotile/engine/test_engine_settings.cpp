// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "helpers/AutotileTestHelpers.h"
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include "core/types/constants.h"
#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestEngineSettings : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<IsolatedConfigGuard> m_configGuard;
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    void init()
    {
        m_configGuard = std::make_unique<IsolatedConfigGuard>();
    }

    void cleanup()
    {
        m_configGuard.reset();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // refreshConfigFromSettings
    // ═══════════════════════════════════════════════════════════════════════════

    void testRefreshConfig_withNullSettings_doesNotCrash()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("eDP-1")});

        QSignalSpy tilingSpy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

        engine.refreshConfigFromSettings();

        QCOMPARE(tilingSpy.count(), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // maxWindows increase triggering backfill
    // ═══════════════════════════════════════════════════════════════════════════

    void testMaxWindowsIncrease_triggersBackfill()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QCOMPARE(state->tiledWindowCount(), 2);

        Settings settings;
        settings.setAutotileMaxWindows(4);
        engine.setEngineSettings(&settings);
        engine.refreshConfigFromSettings();
        QCoreApplication::processEvents();

        // All three opened windows must now tile: raising the cap from 2 to 4
        // recovers the previously-overflowed win3. >= 2 would also pass if the
        // recovery did nothing, so assert the exact count the regression targets.
        // QTRY_, because an over-cap window is a real overflow-floated member of
        // the state now (not an unmanaged phantom backfillWindows re-adopts), so
        // its recovery rides OverflowManager::recoverIfRoom on the settings
        // retile timer rather than landing synchronously in refreshConfigFromSettings.
        QTRY_COMPARE(state->tiledWindowCount(), 3);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // savedAlgorithmSettings isolation
    // ═══════════════════════════════════════════════════════════════════════════

    void testSavedAlgorithmSettings_onlyAffectsActiveAlgorithm()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.setAlgorithm(QLatin1String("master-stack"));

        AlgorithmSettings cmSaved;
        cmSaved.splitRatio = 0.45;
        cmSaved.masterCount = 2;
        engine.config()->savedAlgorithmSettings[QStringLiteral("centered-master")] = cmSaved;

        const qreal masterStackRatio = engine.config()->splitRatio;

        cmSaved.splitRatio = 0.35;
        cmSaved.masterCount = 3;
        engine.config()->savedAlgorithmSettings[QStringLiteral("centered-master")] = cmSaved;
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, masterStackRatio));

        engine.setAlgorithm(QLatin1String("centered-master"));
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, 0.35));
        QCOMPARE(engine.config()->masterCount, 3);
    }

    // Switching algorithms with no user customization must not author ANY config
    // delta — not the global Tiling.Algorithm/MaxWindows key, and not a
    // default-valued per-algorithm slot. Both would surface as a spurious "you
    // changed this" row in the config profile diff. grid is the algorithm from the
    // original report: its defaultMaxWindows of 9 differs from the global default,
    // so a leak shows up as "FROM 5 TO 9".
    void testAlgorithmSwitch_untouchedSwitchWritesNoSpuriousDelta()
    {
        Settings settings;
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setEngineSettings(&settings);

        auto* gridAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("grid"));
        QVERIFY(gridAlgo);

        engine.setAlgorithm(QLatin1String("master-stack"));
        const int globalBefore = settings.autotileMaxWindows();

        engine.setAlgorithm(QLatin1String("grid"));

        // The switch took effect in the engine's live config: grid runs at its own
        // default cap, not the global one.
        QCOMPARE(engine.config()->maxWindows, gridAlgo->defaultMaxWindows());
        // Guard against a vacuous pass: the assertions below are only meaningful if
        // grid's default differs from the global.
        QVERIFY(gridAlgo->defaultMaxWindows() != globalBefore);
        // The global key is untouched...
        QCOMPARE(settings.autotileMaxWindows(), globalBefore);
        // ...and no default-valued per-algorithm slot was persisted. A slot that
        // merely echoes the algorithm's defaults would be a diff row for a change
        // the user never made. Neither the switched-to (grid) nor the switched-from
        // (master-stack) algorithm was customized, so neither may appear on disk.
        const QVariantMap persisted = settings.autotilePerAlgorithmSettings();
        QVERIFY2(!persisted.contains(QStringLiteral("grid")),
                 "default-valued grid slot leaked into persisted per-algorithm settings");
        QVERIFY2(!persisted.contains(QStringLiteral("master-stack")),
                 "default-valued master-stack slot leaked into persisted per-algorithm settings");
    }

    // A routine settings refresh (algorithm unchanged) must not clobber the current
    // algorithm's per-algorithm maxWindows back to the global key. Because
    // default-valued slots are no longer persisted, the current algorithm has no
    // slot to restore from, so the refresh path must fall back to the algorithm's
    // own default rather than the global value SYNC_FIELD reads.
    void testRefresh_unchangedAlgorithm_keepsPerAlgoMaxWindows()
    {
        Settings settings;
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setEngineSettings(&settings);

        auto* gridAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("grid"));
        QVERIFY(gridAlgo);
        QVERIFY(gridAlgo->defaultMaxWindows() != settings.autotileMaxWindows());

        // Armed before the switch, since the switch is what starts the guard.
        QSignalSpy persistSpy(&engine, &AutotileEngine::settingsPersistRequested);

        // Make grid the default algorithm and land on it.
        settings.setDefaultAutotileAlgorithm(QLatin1String("grid"));
        engine.setAlgorithm(QLatin1String("grid"));
        QCOMPARE(engine.config()->maxWindows, gridAlgo->defaultMaxWindows());

        // The switch arms the write-back guard timer, which suppresses the
        // maxWindows re-read. The clobber only bites on a LATER refresh once
        // that guard has lapsed, so wait for the guard's OWN completion signal
        // rather than sleeping past a hard-coded interval. A fixed qWait is
        // coupled to the production constant in the FALSE-PASS direction: raise
        // the interval, or load the machine, and the guard has not lapsed, the
        // clobber path is never exercised, and the test passes for exactly the
        // reason this comment warns about — silently.
        QVERIFY(persistSpy.wait(5000));

        // A refresh with the algorithm unchanged (grid stays the default) must not
        // pull maxWindows down to the global key. The global still holds its schema
        // default, so grid's own default is authoritative.
        engine.refreshConfigFromSettings();
        QCOMPARE(engine.config()->maxWindows, gridAlgo->defaultMaxWindows());
    }

    // Discussion #853: a settings-driven refresh that ALSO switches the ambient
    // algorithm must not stamp the outgoing algorithm's slot from the engine's
    // live scalars. The saved map was just reloaded from disk inside the same
    // refresh, so the outgoing slot in it is the value the user explicitly
    // saved (settings app: slider → per-algorithm slot → Save → reload); the
    // live scalars are stale — and, once the write-back guard has lapsed,
    // already clobbered by the global SYNC_FIELDs. The stamp turned "set
    // three-column max windows to 7, then switch algorithms" into a silent
    // revert to 5 (its default), after which persistablePerAlgoSettings pruned
    // the slot entirely.
    void testRefresh_settingsDrivenSwitch_preservesOutgoingSlot()
    {
        Settings settings;
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setEngineSettings(&settings);

        auto* threeCol = m_scriptSetup.registry()->algorithm(QLatin1String("three-column"));
        QVERIFY(threeCol);
        const int customMaxWindows = threeCol->defaultMaxWindows() + 2;

        // Land on three-column as the ambient algorithm.
        settings.setDefaultAutotileAlgorithm(QLatin1String("three-column"));
        engine.setAlgorithm(QLatin1String("three-column"));

        // The settings app writes the user's customization: a per-algorithm
        // slot only, never the global key (TilingAlgorithmController).
        QVariantMap slot;
        slot[PhosphorTiles::AutotileJsonKeys::MaxWindows] = customMaxWindows;
        slot[PhosphorTiles::AutotileJsonKeys::SplitRatio] = 0.6;
        slot[PhosphorTiles::AutotileJsonKeys::MasterCount] = 1;
        QVariantMap perAlgo;
        perAlgo[QStringLiteral("three-column")] = slot;
        settings.setAutotilePerAlgorithmSettings(perAlgo);

        // Let the switch's write-back guard lapse so the refresh below runs the
        // global SYNC_FIELDs — the worst-case variant, where the live scalars
        // hold the global default rather than merely stale values.
        QTest::qWait(600);

        // The settings app also switched the default algorithm before Save; the
        // daemon reload delivers both changes in one refresh.
        settings.setDefaultAutotileAlgorithm(QLatin1String("master-stack"));
        engine.refreshConfigFromSettings();
        QCOMPARE(engine.algorithm(), QStringLiteral("master-stack"));

        // The freshly-saved slot survives the switch, both in the live config
        // and in what writeBackTuning() persisted back to settings.
        const auto savedIt = engine.config()->savedAlgorithmSettings.constFind(QStringLiteral("three-column"));
        QVERIFY2(savedIt != engine.config()->savedAlgorithmSettings.constEnd(),
                 "three-column slot was pruned by the settings-driven switch");
        QCOMPARE(savedIt->maxWindows, customMaxWindows);
        const QVariantMap persisted =
            settings.autotilePerAlgorithmSettings().value(QStringLiteral("three-column")).toMap();
        QCOMPARE(persisted.value(PhosphorTiles::AutotileJsonKeys::MaxWindows).toInt(), customMaxWindows);

        // Switching back restores the customization.
        engine.setAlgorithm(QLatin1String("three-column"));
        QCOMPARE(engine.config()->maxWindows, customMaxWindows);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Session persistence roundtrip
    // ═══════════════════════════════════════════════════════════════════════════

    void testPersistenceDelegate_noOpWithoutDelegate()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        state->addWindow(QStringLiteral("win1"));

        engine.saveState();
        engine.loadState();

        QVERIFY(!engine.algorithm().isEmpty());
        QCOMPARE(state->windowCount(), 1);
    }

    void testPersistenceDelegate_invokesCallbacks()
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
    // Race condition: shortcut adjustment vs refreshConfigFromSettings
    // ═══════════════════════════════════════════════════════════════════════════

    void testRefreshConfig_preservesPerDesktopRatio()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen, 0, 0);
        QCoreApplication::processEvents();

        Settings settings;
        engine.setEngineSettings(&settings);

        settings.setAutotileSplitRatio(0.5);
        engine.refreshConfigFromSettings();
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, 0.5));

        engine.windowFocused(QStringLiteral("win1"), screen);
        engine.increaseMasterRatio(0.1);
        const qreal adjustedRatio = state->splitRatio();
        QVERIFY(qFuzzyCompare(adjustedRatio, 0.6));

        {
            const QSignalBlocker blocker(&settings);
            settings.setAutotileSplitRatio(0.5);
        }

        engine.refreshConfigFromSettings();

        // The per-desktop tuned ratio survives the refresh — propagate skips
        // user-tuned states. The global config tracks the settings default (0.5),
        // and the adjustment lives in the state, not the global config.
        QVERIFY2(qFuzzyCompare(state->splitRatio(), adjustedRatio),
                 qPrintable(QStringLiteral("refresh clobbered per-desktop ratio: expected %1, got %2")
                                .arg(adjustedRatio)
                                .arg(state->splitRatio())));
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, 0.5));
    }

    void testRefreshConfig_preservesPerDesktopMasterCount()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("win1"), screen, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen, 0, 0);
        engine.windowOpened(QStringLiteral("win3"), screen, 0, 0);
        QCoreApplication::processEvents();

        Settings settings;
        engine.setEngineSettings(&settings);

        settings.setAutotileMasterCount(1);
        engine.refreshConfigFromSettings();
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QCOMPARE(engine.config()->masterCount, 1);

        engine.windowFocused(QStringLiteral("win1"), screen);
        engine.increaseMasterCount();
        QCOMPARE(state->masterCount(), 2);

        {
            const QSignalBlocker blocker(&settings);
            settings.setAutotileMasterCount(1);
        }

        engine.refreshConfigFromSettings();

        // The per-desktop tuned master count survives the refresh; the global
        // config tracks the settings default (1).
        QCOMPARE(state->masterCount(), 2);
        QCOMPARE(engine.config()->masterCount, 1);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Overflow behavior (Float <-> Unlimited)
    // ═══════════════════════════════════════════════════════════════════════════

    void testOverflowBehavior_floatToUnlimited_backfillsExcess()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;
        engine.config()->overflowBehavior = PhosphorTiles::AutotileOverflowBehavior::Float;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QCOMPARE(state->tiledWindowCount(), 2);

        Settings settings;
        settings.setAutotileMaxWindows(2);
        settings.setAutotileOverflowBehavior(PhosphorTiles::AutotileOverflowBehavior::Unlimited);
        engine.setEngineSettings(&settings);
        engine.refreshConfigFromSettings();
        QCoreApplication::processEvents();

        QCOMPARE(engine.config()->overflowBehavior, PhosphorTiles::AutotileOverflowBehavior::Unlimited);
        // QTRY_: recovery rides the settings retile timer (see testMaxWindowsIncrease_triggersBackfill).
        QTRY_COMPARE(state->tiledWindowCount(), 3);
    }

    void testOverflowBehavior_floatToUnlimited_combinedWithMaxIncrease_singleBackfill()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->maxWindows = 2;
        engine.config()->overflowBehavior = PhosphorTiles::AutotileOverflowBehavior::Float;

        engine.windowOpened(QStringLiteral("win1"), screen);
        engine.windowOpened(QStringLiteral("win2"), screen);
        engine.windowOpened(QStringLiteral("win3"), screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QCOMPARE(state->tiledWindowCount(), 2);

        Settings settings;
        settings.setAutotileMaxWindows(4);
        settings.setAutotileOverflowBehavior(PhosphorTiles::AutotileOverflowBehavior::Unlimited);
        engine.setEngineSettings(&settings);
        engine.refreshConfigFromSettings();
        QCoreApplication::processEvents();

        // QTRY_: recovery rides the settings retile timer (see testMaxWindowsIncrease_triggersBackfill).
        QTRY_COMPARE(state->tiledWindowCount(), 3);
        QCOMPARE(engine.config()->overflowBehavior, PhosphorTiles::AutotileOverflowBehavior::Unlimited);
        QCOMPARE(engine.config()->maxWindows, 4);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Direct config mutation is silent; explicit retile drives placement
    // ═══════════════════════════════════════════════════════════════════════════

    void testDirectConfigMutationIsSilent_explicitRetileDrivesPlacement()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));

        QSignalSpy tilingSpy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

        engine.config()->innerGap = 5;
        engine.config()->outerGap = 10;
        engine.config()->innerGap = 8;
        engine.config()->outerGap = 12;

        // Direct writes to config() mutate the struct in place, bypassing the
        // settings-driven retile path entirely, so none of them emits
        // placementChanged on their own.
        QCOMPARE(tilingSpy.count(), 0);

        engine.retile();
        QCoreApplication::processEvents();

        // Only the deliberate retile() reaches the renderer.
        QVERIFY2(tilingSpy.count() > 0, "explicit retile() must drive placementChanged");
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Shortcut ratio adjustment stays local (no settings write-back)
    // ═══════════════════════════════════════════════════════════════════════════

    void testShortcutAdjustment_doesNotWriteBackToSettings()
    {
        // A per-desktop ratio tweak via shortcut must stay local: it changes the
        // active state's ratio but must NOT write the global settings (no bleed).
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        Settings settings;
        engine.setEngineSettings(&settings);
        engine.refreshConfigFromSettings();

        engine.windowOpened(QStringLiteral("win1"), screen, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen, 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        const qreal ratioBefore = settings.autotileSplitRatio();
        const qreal stateBefore = state->splitRatio();
        engine.windowFocused(QStringLiteral("win1"), screen);
        engine.increaseMasterRatio(0.1);

        // Local effect on the active state...
        QVERIFY(qFuzzyCompare(state->splitRatio(), stateBefore + 0.1));
        // ...but the global settings are untouched.
        QVERIFY(qFuzzyCompare(settings.autotileSplitRatio(), ratioBefore));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Tile geometry: a tiled window fills its zone EXACTLY — there is NO border
    // inset. The KWin effect's border shader recolours each window's own outermost
    // band (inside the frame), so the border never pushes the tile past its slot
    // (mirrors the snap side, DaemonGeometryResolver::snapBorderInset == 0). Tile
    // spacing comes from the zone gap/padding settings, not the border width.
    // ═══════════════════════════════════════════════════════════════════════════

    void testTileGeometry_fillsZoneNoBorderInset()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));

        // Tiling never insets by border width (the border draws inside the frame,
        // and window border appearance is resolved through the window rules now),
        // so tiles fill their zones exactly.
        Settings settings;
        engine.setEngineSettings(&settings);

        engine.windowOpened(QStringLiteral("win-1"), screen);
        engine.windowOpened(QStringLiteral("win-2"), screen);
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        const QRect zoneA(10, 10, 950, 1060);
        const QRect zoneB(960, 10, 950, 1060);
        state->setCalculatedZones({zoneA, zoneB});
        engine.retile(screen);

        QVERIFY(tiledSpy.count() >= 1);
        const QJsonArray arr = QJsonDocument::fromJson(tiledSpy.last().first().toString().toUtf8()).array();
        QCOMPARE(arr.size(), 2);
        QHash<QString, QRect> emitted;
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            emitted.insert(o.value(QLatin1String("windowId")).toString(),
                           QRect(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt(),
                                 o.value(QLatin1String("width")).toInt(), o.value(QLatin1String("height")).toInt()));
        }
        // Tiles fill their zones exactly — no border inset (master-stack order:
        // win-1 → zoneA, win-2 → zoneB).
        QCOMPARE(emitted.value(QStringLiteral("win-1")), zoneA);
        QCOMPARE(emitted.value(QStringLiteral("win-2")), zoneB);
    }
};

QTEST_MAIN(TestEngineSettings)
#include "test_engine_settings.moc"
