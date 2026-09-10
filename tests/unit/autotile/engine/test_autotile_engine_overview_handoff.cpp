// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AutotileEngine's cross-desktop handoff for the workspace overview: a
// handoffReceive carrying toDesktop adopts the window into that desktop's
// TilingState at once, recomputes that state's zones so overviewWindowsFor
// reports the tile, and applies no geometry to the desktop the screen is
// showing. Also covers insertIndexForPoint, the per-key slot resolver the
// daemon feeds into HandoffContext::insertIndex. Runs the engine's real
// retile pipeline against a fake screen and the bundled Luau algorithms.

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "FakeScreenProvider.h"

#include "helpers/AutotileTestHelpers.h"
#include "helpers/ScriptedAlgoTestSetup.h"

using PhosphorEngine::OverviewWindowEntry;
using PhosphorEngine::PlacementStateKey;
using PhosphorTileEngine::AutotileEngine;

class TestAutotileEngineOverviewHandoff : public QObject
{
    Q_OBJECT

    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

    static const QString& screen()
    {
        static const QString s = QStringLiteral("DP-1");
        return s;
    }

    static PlacementStateKey keyFor(int desktop)
    {
        return PlacementStateKey{screen(), desktop, QString()};
    }

    static const OverviewWindowEntry* find(const QList<OverviewWindowEntry>& entries, const QString& id)
    {
        for (const OverviewWindowEntry& e : entries) {
            if (e.windowId == id) {
                return &e;
            }
        }
        return nullptr;
    }

    static QStringList ids(const QList<OverviewWindowEntry>& entries)
    {
        QStringList out;
        for (const OverviewWindowEntry& e : entries) {
            out.append(e.windowId);
        }
        return out;
    }

    /// Window ids named in every windowsTiled batch the spy recorded.
    static QStringList tiledIdsIn(const QSignalSpy& spy)
    {
        QStringList out;
        for (const QList<QVariant>& args : spy) {
            const QJsonArray arr = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).array();
            for (const QJsonValue& v : arr) {
                out.append(v.toObject().value(QLatin1String("windowId")).toString());
            }
        }
        return out;
    }

    struct Rig
    {
        PhosphorScreens::FakeScreenProvider provider;
        std::unique_ptr<PhosphorScreens::ScreenManager> manager;
        std::unique_ptr<AutotileEngine> engine;

        Rig()
        {
            provider.addScreen(screen(), QRect(0, 0, 1920, 1080));
            manager = std::make_unique<PhosphorScreens::ScreenManager>(
                PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
            manager->start();
            engine = std::make_unique<AutotileEngine>(nullptr, nullptr, manager.get(),
                                                      PlasmaZones::TestHelpers::testRegistry());
            engine->setAutotileScreens({screen()});
            engine->setAlgorithm(QLatin1String("columns"));
        }

        /// Open @p count windows on the current desktop and let the retile run.
        void open(const QStringList& windows)
        {
            for (const QString& w : windows) {
                engine->windowOpened(w, screen());
            }
            PhosphorTiles::TilingState* state = engine->tilingStateForScreen(screen());
            QTRY_COMPARE(state->calculatedZones().size(), windows.size());
            QCoreApplication::processEvents();
        }
    };

    static PhosphorEngine::IPlacementEngine::HandoffContext contextFor(const QString& windowId, int toDesktop,
                                                                       int insertIndex = -1)
    {
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.fromEngineId = QStringLiteral("snap");
        ctx.toScreenId = screen();
        ctx.toDesktop = toDesktop;
        ctx.insertIndex = insertIndex;
        return ctx;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    void receiveOntoOtherDesktop_adoptsThereAndLeavesCurrentAlone()
    {
        Rig rig;
        rig.open({QStringLiteral("a1"), QStringLiteral("a2")});
        const auto d1Before = rig.engine->overviewWindowsFor(keyFor(1));
        QVERIFY(d1Before.has_value());
        QCOMPARE(d1Before->size(), 2);

        rig.engine->handoffReceive(contextFor(QStringLiteral("b1"), 2));

        const auto d2 = rig.engine->overviewWindowsFor(keyFor(2));
        QVERIFY(d2.has_value());
        QCOMPARE(ids(*d2), QStringList{QStringLiteral("b1")});
        const OverviewWindowEntry* b1 = find(*d2, QStringLiteral("b1"));
        QVERIFY(b1);
        QVERIFY(!b1->floating);
        QVERIFY(!b1->rect.isEmpty());
        QVERIFY(QRect(0, 0, 1920, 1080).contains(b1->rect));
        // The current desktop's state never saw the window; only desktop 2 did.
        const PhosphorTiles::TilingState* currentState = rig.engine->tilingStateForScreen(screen());
        QVERIFY(currentState);
        QVERIFY(!currentState->containsWindow(QStringLiteral("b1")));
        QVERIFY(rig.engine->desktopsWithActiveState().contains(2));
        QCOMPARE(rig.engine->screenForTrackedWindow(QStringLiteral("b1")), screen());

        // Desktop 1's window set and zones are untouched.
        const auto d1After = rig.engine->overviewWindowsFor(keyFor(1));
        QVERIFY(d1After.has_value());
        QCOMPARE(ids(*d1After), ids(*d1Before));
        for (int i = 0; i < d1Before->size(); ++i) {
            QCOMPARE(d1After->at(i).rect, d1Before->at(i).rect);
        }
    }

    void receiveOntoOtherDesktop_appliesNoGeometry()
    {
        Rig rig;
        rig.open({QStringLiteral("a1"), QStringLiteral("a2")});

        QSignalSpy tiled(rig.engine.get(), &AutotileEngine::windowsTiled);
        QSignalSpy placement(rig.engine.get(), &AutotileEngine::placementChanged);
        rig.engine->handoffReceive(contextFor(QStringLiteral("b1"), 2));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QCOMPARE(tiled.count(), 0);
        QCOMPARE(placement.count(), 0);
    }

    void receiveOntoOtherDesktop_honoursInsertIndex()
    {
        Rig rig;
        rig.engine->handoffReceive(contextFor(QStringLiteral("b1"), 2));
        rig.engine->handoffReceive(contextFor(QStringLiteral("b2"), 2));
        rig.engine->handoffReceive(contextFor(QStringLiteral("b3"), 2, 1));

        const auto d2 = rig.engine->overviewWindowsFor(keyFor(2));
        QVERIFY(d2.has_value());
        QCOMPARE(ids(*d2), (QStringList{QStringLiteral("b1"), QStringLiteral("b3"), QStringLiteral("b2")}));
        // Three columns, none overlapping, all sized.
        for (const OverviewWindowEntry& e : *d2) {
            QVERIFY(!e.rect.isEmpty());
        }
        QVERIFY(!d2->at(0).rect.intersects(d2->at(1).rect));
        QVERIFY(!d2->at(1).rect.intersects(d2->at(2).rect));
    }

    void insertIndexForPoint_zoneHitAppendAndUnknownKey()
    {
        Rig rig;
        rig.open({QStringLiteral("a1"), QStringLiteral("a2"), QStringLiteral("a3")});
        const PhosphorTiles::TilingState* state = rig.engine->tilingStateForScreen(screen());
        const QVector<QRect> zones = state->calculatedZones();
        QCOMPARE(zones.size(), 3);

        QCOMPARE(rig.engine->insertIndexForPoint(keyFor(1), zones.at(0).center()), 0);
        QCOMPARE(rig.engine->insertIndexForPoint(keyFor(1), zones.at(1).center()), 1);
        QCOMPARE(rig.engine->insertIndexForPoint(keyFor(1), zones.at(2).center()), 2);
        // Outside every zone: append past the last window.
        QCOMPARE(rig.engine->insertIndexForPoint(keyFor(1), QPoint(-50, -50)), 3);
        // Unknown key: 0, and the read built nothing.
        QCOMPARE(rig.engine->insertIndexForPoint(keyFor(5), zones.at(1).center()), 0);
        QVERIFY(!rig.engine->overviewWindowsFor(keyFor(5)).has_value());

        // The answer is a window-order index: with a float ahead of the tiled
        // set, the zone hit maps to the tiled window's order position.
        rig.engine->setWindowFloat(QStringLiteral("a1"), true, screen());
        QTRY_COMPARE(state->calculatedZones().size(), 2);
        const QVector<QRect> twoZones = state->calculatedZones();
        QCOMPARE(state->windowOrder().indexOf(QStringLiteral("a2")), 1);
        QCOMPARE(rig.engine->insertIndexForPoint(keyFor(1), twoZones.at(0).center()), 1);
        QCOMPARE(rig.engine->insertIndexForPoint(keyFor(1), QPoint(-50, -50)), 3);
    }

    void receiveOntoCurrentDesktop_tilesImmediately()
    {
        Rig rig;
        rig.open({QStringLiteral("a1")});

        QSignalSpy tiled(rig.engine.get(), &AutotileEngine::windowsTiled);
        rig.engine->handoffReceive(contextFor(QStringLiteral("b1"), 1));

        QVERIFY(tiled.count() >= 1);
        QVERIFY(tiledIdsIn(tiled).contains(QStringLiteral("b1")));
        const auto d1 = rig.engine->overviewWindowsFor(keyFor(1));
        QVERIFY(d1.has_value());
        QCOMPARE(d1->size(), 2);
        QVERIFY(!find(*d1, QStringLiteral("b1"))->rect.isEmpty());
        // Nothing leaked into a desktop 2 state.
        QVERIFY(!rig.engine->overviewWindowsFor(keyFor(2)).has_value());
    }

    void switchingToTargetDesktop_retileAppliesTheAdoptedWindow()
    {
        Rig rig;
        rig.open({QStringLiteral("a1"), QStringLiteral("a2")});
        rig.engine->handoffReceive(contextFor(QStringLiteral("b1"), 2));
        const QRect recorded = find(*rig.engine->overviewWindowsFor(keyFor(2)), QStringLiteral("b1"))->rect;
        QVERIFY(!recorded.isEmpty());

        rig.engine->setCurrentDesktopForScreen(screen(), 2);
        QSignalSpy tiled(rig.engine.get(), &AutotileEngine::windowsTiled);
        rig.engine->retile(screen());

        QCOMPARE(tiled.count(), 1);
        const QJsonArray arr = QJsonDocument::fromJson(tiled.at(0).at(0).toString().toUtf8()).array();
        QCOMPARE(arr.size(), 1);
        const QJsonObject entry = arr.at(0).toObject();
        QCOMPARE(entry.value(QLatin1String("windowId")).toString(), QStringLiteral("b1"));
        QCOMPARE(entry.value(QLatin1String("screenId")).toString(), screen());
        const QRect applied(entry.value(QLatin1String("x")).toInt(), entry.value(QLatin1String("y")).toInt(),
                            entry.value(QLatin1String("width")).toInt(), entry.value(QLatin1String("height")).toInt());
        QCOMPARE(applied, recorded);
        QCOMPARE(rig.engine->lastManagedRect(QStringLiteral("b1")), recorded);
    }
};

QTEST_GUILESS_MAIN(TestAutotileEngineOverviewHandoff)
#include "test_autotile_engine_overview_handoff.moc"
