// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QTimer>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "../helpers/AutotileTestHelpers.h"
#include <PhosphorTileEngine/AutotileConfig.h>
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

class TestEngineSettings : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<IsolatedConfigGuard> m_configGuard;
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

    static QJsonObject buildEntry(const QString& screenId, int desktop, const QStringList& windowOrder,
                                  const QStringList& floatingWindows = {})
    {
        QJsonObject entry;
        entry[QLatin1String("screen")] = screenId;
        entry[QLatin1String("desktop")] = desktop;
        entry[QLatin1String("activity")] = QString();
        QJsonArray orderArray;
        for (const QString& w : windowOrder)
            orderArray.append(w);
        entry[QLatin1String("windowOrder")] = orderArray;
        if (!floatingWindows.isEmpty()) {
            QJsonArray floatArray;
            for (const QString& w : floatingWindows)
                floatArray.append(w);
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

        QSignalSpy tilingSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::placementChanged);

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

    void testSerializeWindowOrders_roundTrip()
    {
        QJsonArray serialized;

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

        {
            AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
            engine.deserializeWindowOrders(serialized);

            QJsonObject entry = serialized[0].toObject();
            QVERIFY(!entry[QLatin1String("screen")].toString().isEmpty());
            QVERIFY(entry.contains(QLatin1String("windowOrder")));
            QVERIFY(entry[QLatin1String("windowOrder")].toArray().size() > 0);

            QVERIFY(!entry.contains(QLatin1String("masterCount")));
            QVERIFY(!entry.contains(QLatin1String("splitRatio")));
        }
    }

    void testDeserializeWindowOrders_emptyArray()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.deserializeWindowOrders(QJsonArray{});
        QVERIFY(!engine.algorithm().isEmpty());
    }

    void testSerializeWindowOrders_includesFloating()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        state->addWindow(QStringLiteral("firefox|{uuid1}"));
        state->addWindow(QStringLiteral("konsole|{uuid2}"));
        state->addWindow(QStringLiteral("dolphin|{uuid3}"));
        state->setFloating(QStringLiteral("konsole|{uuid2}"), true);

        QJsonArray serialized = engine.serializeWindowOrders();
        QCOMPARE(serialized.size(), 1);

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

    void testDeserializeWindowOrders_multiDesktop_onlyRestoresCurrentContext()
    {
        QJsonArray multiDesktopData;
        multiDesktopData.append(
            buildEntry(QStringLiteral("eDP-1"), 1, {QStringLiteral("win1"), QStringLiteral("win2")}));
        multiDesktopData.append(
            buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("win3"), QStringLiteral("win4")}));

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.deserializeWindowOrders(multiDesktopData);

        engine.setAutotileScreens({QStringLiteral("eDP-1")});
        engine.windowOpened(QStringLiteral("win2"), QStringLiteral("eDP-1"), 0, 0);
        engine.windowOpened(QStringLiteral("win1"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state);
        const QStringList order = state->windowOrder();
        QCOMPARE(order.size(), 2);
        QCOMPARE(order.at(0), QStringLiteral("win1"));
        QCOMPARE(order.at(1), QStringLiteral("win2"));
    }

    void testDeserializeWindowOrders_multiDesktop_promotesOnSwitch()
    {
        QJsonArray multiDesktopData;
        multiDesktopData.append(
            buildEntry(QStringLiteral("eDP-1"), 1, {QStringLiteral("win1"), QStringLiteral("win2")}));
        multiDesktopData.append(
            buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("win3"), QStringLiteral("win4")}));

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.deserializeWindowOrders(multiDesktopData);

        engine.setAutotileScreens({QStringLiteral("eDP-1")});
        engine.setCurrentDesktop(2);
        QCoreApplication::processEvents();

        engine.windowOpened(QStringLiteral("win4"), QStringLiteral("eDP-1"), 0, 0);
        engine.windowOpened(QStringLiteral("win3"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state);
        const QStringList order = state->windowOrder();
        QCOMPARE(order.size(), 2);
        QCOMPARE(order.at(0), QStringLiteral("win3"));
        QCOMPARE(order.at(1), QStringLiteral("win4"));
    }

    void testDeserializeWindowOrders_floatingRestoresAllContexts()
    {
        QJsonArray data;
        data.append(buildEntry(QStringLiteral("eDP-1"), 1, {QStringLiteral("win1")}, {QStringLiteral("win1")}));
        data.append(buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("win2")}, {QStringLiteral("win2")}));

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.deserializeWindowOrders(data);

        engine.setAutotileScreens({QStringLiteral("eDP-1")});

        engine.setCurrentDesktop(2);
        QCoreApplication::processEvents();
        engine.windowOpened(QStringLiteral("win2"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state);
        QVERIFY2(state->isFloating(QStringLiteral("win2")),
                 "Window on desktop 2 should be restored as floating from saved state");
    }

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

        QSignalSpy tilingSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::placementChanged);

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

    void testSimultaneousDesktopActivitySwitch_promotesCorrectContext()
    {
        const QString activityA = QStringLiteral("activity-aaaa");
        const QString activityB = QStringLiteral("activity-bbbb");

        QJsonArray data;
        data.append(buildEntry(QStringLiteral("eDP-1"), 2, {QStringLiteral("winA1"), QStringLiteral("winA2")}));
        {
            QJsonObject entry = data[0].toObject();
            entry[QLatin1String("activity")] = activityA;
            data[0] = entry;
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

        engine.setCurrentDesktop(2);
        engine.setCurrentActivity(activityB);
        QCoreApplication::processEvents();

        engine.setAutotileScreens({QStringLiteral("eDP-1")});
        engine.windowOpened(QStringLiteral("winB2"), QStringLiteral("eDP-1"), 0, 0);
        engine.windowOpened(QStringLiteral("winB1"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state);
        const QStringList order = state->windowOrder();
        QCOMPARE(order.size(), 2);
        QCOMPARE(order.at(0), QStringLiteral("winB1"));
        QCOMPARE(order.at(1), QStringLiteral("winB2"));
    }

    void testSimultaneousSwitch_doesNotConsumeWrongActivityEntry()
    {
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

        engine.setCurrentDesktop(2);
        engine.setCurrentActivity(activityB);
        QCoreApplication::processEvents();

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
        QCOMPARE(order.at(0), QStringLiteral("winA1"));
        QCOMPARE(order.at(1), QStringLiteral("winA2"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // WTA integration: save/load roundtrip through config backend
    // ═══════════════════════════════════════════════════════════════════════════

    void testWtaRoundtrip_autotileOrdersSurviveSaveLoad()
    {
        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        auto backend = std::make_unique<PhosphorConfig::JsonBackend>(ConfigDefaults::configFilePath());

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QStringLiteral("eDP-1"));
        state->addWindow(QStringLiteral("firefox|{uuid1}"));
        state->addWindow(QStringLiteral("konsole|{uuid2}"));
        state->setFloating(QStringLiteral("konsole|{uuid2}"), true);

        QJsonArray serialized = engine.serializeWindowOrders();
        QCOMPARE(serialized.size(), 1);

        auto tracking = backend->group(ConfigKeys::windowTrackingGroup());
        tracking->writeString(ConfigKeys::autotileWindowOrdersKey(),
                              QString::fromUtf8(QJsonDocument(serialized).toJson(QJsonDocument::Compact)));
        tracking.reset();
        backend->sync();

        auto backend2 = std::make_unique<PhosphorConfig::JsonBackend>(ConfigDefaults::configFilePath());
        backend2->sync();
        auto tracking2 = backend2->group(ConfigKeys::windowTrackingGroup());
        QString readBack = tracking2->readString(ConfigKeys::autotileWindowOrdersKey(), QString());
        QVERIFY(!readBack.isEmpty());

        QJsonDocument doc = QJsonDocument::fromJson(readBack.toUtf8());
        QVERIFY(doc.isArray());

        AutotileEngine engine2(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine2.deserializeWindowOrders(doc.array());

        engine2.setAutotileScreens({QStringLiteral("eDP-1")});
        engine2.windowOpened(QStringLiteral("konsole|{uuid2}"), QStringLiteral("eDP-1"), 0, 0);
        engine2.windowOpened(QStringLiteral("firefox|{uuid1}"), QStringLiteral("eDP-1"), 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state2 = engine2.tilingStateForScreen(QStringLiteral("eDP-1"));
        QVERIFY(state2);

        const QStringList order = state2->windowOrder();
        QCOMPARE(order.size(), 2);
        QCOMPARE(order.at(0), QStringLiteral("firefox|{uuid1}"));
        QCOMPARE(order.at(1), QStringLiteral("konsole|{uuid2}"));

        QVERIFY(state2->isFloating(QStringLiteral("konsole|{uuid2}")));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Write-back signal
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

        QSignalSpy writeBackSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::settingsWriteBackRequested);
        engine.windowFocused(QStringLiteral("win1"), screen);
        engine.increaseMasterRatio(0.1);

        QVERIFY(writeBackSpy.count() > 0);
        const QVariantMap wb = writeBackSpy.last().at(0).toMap();
        QVERIFY(wb.contains(QLatin1String("autotileSplitRatio")));
    }
};

QTEST_MAIN(TestEngineSettings)
#include "test_engine_settings.moc"
