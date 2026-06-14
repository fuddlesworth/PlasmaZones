// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "../helpers/AutotileTestHelpers.h"
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"
#include "config/settings.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

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

        QVERIFY(state->tiledWindowCount() >= 2);
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

    void testRefreshConfig_duringShortcutDebounce_preservesRuntimeRatio()
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

        QVERIFY2(qFuzzyCompare(engine.config()->splitRatio, adjustedRatio),
                 qPrintable(
                     QStringLiteral("refreshConfigFromSettings overwrote shortcut-adjusted ratio: expected %1, got %2")
                         .arg(adjustedRatio)
                         .arg(engine.config()->splitRatio)));
    }

    void testRefreshConfig_duringShortcutDebounce_preservesRuntimeMasterCount()
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

        QCOMPARE(engine.config()->masterCount, 2);
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
        QCOMPARE(state->tiledWindowCount(), 3);
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

        QCOMPARE(state->tiledWindowCount(), 3);
        QCOMPARE(engine.config()->overflowBehavior, PhosphorTiles::AutotileOverflowBehavior::Unlimited);
        QCOMPARE(engine.config()->maxWindows, 4);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Debounce: rapid changes coalesce
    // ═══════════════════════════════════════════════════════════════════════════

    void testDebounceCoalescesRapidChanges()
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

        QCOMPARE(tilingSpy.count(), 0);

        engine.retile();
        QCoreApplication::processEvents();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Simultaneous desktop+activity switch (coalesced promotion)
    // ═══════════════════════════════════════════════════════════════════════════

    void testSettingsWriteBack_emittedOnShortcutAdjustment()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        Settings settings;
        engine.setEngineSettings(&settings);
        engine.refreshConfigFromSettings();

        engine.windowOpened(QStringLiteral("win1"), screen, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen, 0, 0);
        QCoreApplication::processEvents();

        const qreal ratioBefore = settings.autotileSplitRatio();
        engine.windowFocused(QStringLiteral("win1"), screen);
        engine.increaseMasterRatio(0.1);

        QVERIFY(settings.autotileSplitRatio() != ratioBefore);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotile border inset: when the autotile show-border setting is on, each
    // tiled window's frame is shrunk by the border width so the border the KWin
    // effect draws on the window edge sits INSIDE the tile, separating adjacent
    // tiles. Drives retile with known calculatedZones and asserts the emitted
    // windowsTiled geometry against the inset rect.
    // ═══════════════════════════════════════════════════════════════════════════

    void testTileGeometry_insetByAutotileBorder()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));

        constexpr int kBorder = 6;
        Settings settings;
        settings.setAutotileShowBorder(true);
        settings.setAutotileBorderWidth(kBorder);
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

        // Map each emitted entry by windowId, then compare to the inset of its
        // source zone (master-stack order: win-1 → zoneA, win-2 → zoneB).
        QHash<QString, QRect> emitted;
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            emitted.insert(o.value(QLatin1String("windowId")).toString(),
                           QRect(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt(),
                                 o.value(QLatin1String("width")).toInt(), o.value(QLatin1String("height")).toInt()));
        }
        QCOMPARE(emitted.value(QStringLiteral("win-1")), zoneA.adjusted(kBorder, kBorder, -kBorder, -kBorder));
        QCOMPARE(emitted.value(QStringLiteral("win-2")), zoneB.adjusted(kBorder, kBorder, -kBorder, -kBorder));
    }

    void testTileGeometry_noInsetWhenBorderOff()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));

        Settings settings;
        settings.setAutotileShowBorder(false);
        settings.setAutotileBorderWidth(6);
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
        // Show-border off → geometry is the un-inset tile rect.
        QHash<QString, QRect> emitted;
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            emitted.insert(o.value(QLatin1String("windowId")).toString(),
                           QRect(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt(),
                                 o.value(QLatin1String("width")).toInt(), o.value(QLatin1String("height")).toInt()));
        }
        QCOMPARE(emitted.value(QStringLiteral("win-1")), zoneA);
        QCOMPARE(emitted.value(QStringLiteral("win-2")), zoneB);
    }

    void testTileGeometry_noInsetWhenTitleBarsHidden()
    {
        // Borderless (hide-title-bars) autotile: even with show-border on and a
        // non-zero width, the window fills its tile and the border is recoloured
        // inside the content edge — so the tile must NOT be inset (mirrors the
        // snap-side DaemonGeometryResolver borderless gate). Guards the
        // `!autotileHideTitleBars()` term in the applyTiling inset gate.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));

        Settings settings;
        settings.setAutotileShowBorder(true);
        settings.setAutotileHideTitleBars(true);
        settings.setAutotileBorderWidth(6);
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
        // Borderless → un-inset tile rect, despite show-border on + non-zero width.
        QCOMPARE(emitted.value(QStringLiteral("win-1")), zoneA);
        QCOMPARE(emitted.value(QStringLiteral("win-2")), zoneB);
    }

    void testTileGeometry_degenerateClampWhenTileSmallerThanInset()
    {
        // A tile too small to absorb 2*inset must keep a >= 1 px extent rather than
        // collapse to an empty/inverted rect (the PhosphorGeometry::insetRect
        // degenerate clamp). zone 8x8 inset by 6 → top-left shifts +6, extent
        // clamps to 1x1.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));

        constexpr int kBorder = 6;
        Settings settings;
        settings.setAutotileShowBorder(true);
        settings.setAutotileHideTitleBars(false); // decorated → inset applies
        settings.setAutotileBorderWidth(kBorder);
        engine.setEngineSettings(&settings);

        engine.windowOpened(QStringLiteral("win-1"), screen);
        engine.windowOpened(QStringLiteral("win-2"), screen);
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        const QRect tiny(100, 200, 8, 8);
        state->setCalculatedZones({tiny, tiny});
        engine.retile(screen);

        QVERIFY(tiledSpy.count() >= 1);
        const QJsonArray arr = QJsonDocument::fromJson(tiledSpy.last().first().toString().toUtf8()).array();
        QCOMPARE(arr.size(), 2);
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            const QRect r(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt(),
                          o.value(QLatin1String("width")).toInt(), o.value(QLatin1String("height")).toInt());
            // adjusted(6,6,-6,-6) on (100,200,8,8) inverts → clamped to 1x1 at the
            // inset top-left (106, 206).
            QCOMPARE(r, QRect(106, 206, 1, 1));
        }
    }
};

QTEST_MAIN(TestEngineSettings)
#include "test_engine_settings.moc"
