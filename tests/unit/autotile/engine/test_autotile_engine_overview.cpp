// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AutotileEngine's workspace-overview read surface: a per-key read of the
// existing TilingState that lists every held window exactly once with its
// absolute tile rect, answers nullopt for a key it never built, and leaves
// the state untouched. Runs the engine's real retile pipeline against a fake
// screen and the bundled Luau algorithms so calculatedZones is populated.

#include <QCoreApplication>
#include <QTest>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IOverviewModelSource.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "FakeScreenProvider.h"

#include "helpers/AutotileTestHelpers.h"
#include "helpers/ScriptedAlgoTestSetup.h"

using PhosphorEngine::OverviewWindowEntry;
using PhosphorEngine::PlacementStateKey;
using PhosphorTileEngine::AutotileEngine;

class TestAutotileEngineOverview : public QObject
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

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    void neverCreatedKey_answersNullopt()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(screen(), QRect(0, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();
        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());

        const PhosphorEngine::IOverviewModelSource& source = engine;
        QVERIFY(!source.overviewWindowsFor(keyFor(7)).has_value());
        // The read must not have built a state on the way through.
        QVERIFY(!engine.desktopsWithActiveState().contains(7));
    }

    void tiledWindowsOnDesktop2_listedWithAbsoluteRects()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(screen(), QRect(0, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();
        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({screen()});
        engine.setAlgorithm(QLatin1String("columns"));

        engine.setCurrentDesktopForScreen(screen(), 2);
        engine.windowOpened(QStringLiteral("a1"), screen());
        engine.windowOpened(QStringLiteral("a2"), screen());
        PhosphorTiles::TilingState* d2 = engine.tilingStateForScreen(screen());
        QTRY_COMPARE(d2->calculatedZones().size(), 2);

        const auto result = engine.overviewWindowsFor(keyFor(2));
        QVERIFY(result.has_value());
        QCOMPARE(result->size(), 2);
        const OverviewWindowEntry* e1 = find(*result, QStringLiteral("a1"));
        const OverviewWindowEntry* e2 = find(*result, QStringLiteral("a2"));
        QVERIFY(e1 && e2);
        QVERIFY(!e1->floating);
        QVERIFY(!e2->floating);
        QVERIFY(!e1->rect.isEmpty());
        QVERIFY(!e2->rect.isEmpty());
        QVERIFY(!e1->rect.intersects(e2->rect));
        // Absolute screen coordinates: both zones sit inside the fake output.
        const QRect output(0, 0, 1920, 1080);
        QVERIFY(output.contains(e1->rect));
        QVERIFY(output.contains(e2->rect));
        QCOMPARE(e1->rect, d2->calculatedZones().at(d2->tiledWindows().indexOf(QStringLiteral("a1"))));

        // Desktop 1 was seeded empty by setAutotileScreens, so it answers an
        // engaged but empty list; the windows live only under desktop 2.
        const auto d1 = engine.overviewWindowsFor(keyFor(1));
        if (d1.has_value()) {
            QVERIFY(d1->isEmpty());
        }
        QVERIFY(!engine.overviewWindowsFor(keyFor(3)).has_value());
    }

    void floatedWindow_listedAsFloating()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(screen(), QRect(0, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();
        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({screen()});
        engine.setAlgorithm(QLatin1String("columns"));

        engine.windowOpened(QStringLiteral("a1"), screen());
        engine.windowOpened(QStringLiteral("a2"), screen());
        engine.windowOpened(QStringLiteral("a3"), screen());
        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(screen());
        QTRY_COMPARE(d1->calculatedZones().size(), 3);
        // Let applyTiling run so the float carries its last tile rect.
        QCoreApplication::processEvents();
        const QRect tileBeforeFloat = engine.lastManagedRect(QStringLiteral("a2"));

        engine.setWindowFloat(QStringLiteral("a2"), true, screen());
        QTRY_COMPARE(d1->calculatedZones().size(), 2);

        const auto result = engine.overviewWindowsFor(keyFor(1));
        QVERIFY(result.has_value());
        QCOMPARE(result->size(), 3);
        const OverviewWindowEntry* f = find(*result, QStringLiteral("a2"));
        QVERIFY(f);
        QVERIFY(f->floating);
        QCOMPARE(f->rect, tileBeforeFloat);
        QVERIFY(!find(*result, QStringLiteral("a1"))->floating);
        QVERIFY(!find(*result, QStringLiteral("a3"))->floating);
    }

    void read_isInert()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(screen(), QRect(0, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();
        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({screen()});
        engine.setAlgorithm(QLatin1String("columns"));

        engine.windowOpened(QStringLiteral("a1"), screen());
        engine.windowOpened(QStringLiteral("a2"), screen());
        engine.windowOpened(QStringLiteral("a3"), screen());
        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(screen());
        QTRY_COMPARE(d1->calculatedZones().size(), 3);
        engine.setWindowFloat(QStringLiteral("a3"), true, screen());
        QTRY_COMPARE(d1->calculatedZones().size(), 2);
        QCoreApplication::processEvents();

        const QStringList tiledBefore = d1->tiledWindows();
        const QStringList orderBefore = d1->windowOrder();
        const QVector<QRect> zonesBefore = d1->calculatedZones();
        const QString focusBefore = d1->focusedWindow();
        const QSet<int> desktopsBefore = engine.desktopsWithActiveState();

        const auto first = engine.overviewWindowsFor(keyFor(1));
        const auto second = engine.overviewWindowsFor(keyFor(1));
        QCoreApplication::processEvents();

        QVERIFY(first.has_value() && second.has_value());
        QCOMPARE(first->size(), 3);
        QCOMPARE(d1->tiledWindows(), tiledBefore);
        QCOMPARE(d1->windowOrder(), orderBefore);
        QCOMPARE(d1->calculatedZones(), zonesBefore);
        QCOMPARE(d1->focusedWindow(), focusBefore);
        QCOMPARE(engine.desktopsWithActiveState(), desktopsBefore);
        // Deterministic: the same state answers the same list twice.
        for (int i = 0; i < first->size(); ++i) {
            QCOMPARE(first->at(i).windowId, second->at(i).windowId);
            QCOMPARE(first->at(i).rect, second->at(i).rect);
            QCOMPARE(first->at(i).floating, second->at(i).floating);
        }
    }
};

QTEST_GUILESS_MAIN(TestAutotileEngineOverview)
#include "test_autotile_engine_overview.moc"
