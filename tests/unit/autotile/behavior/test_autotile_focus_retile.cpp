// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Focus-driven retiling: an algorithm that declares retileOnFocus (e.g. Theater,
// whose centered spotlight follows the focused window) must make the engine
// reflow when focus moves between tiled windows. An algorithm that does not
// declare it (e.g. Columns) must NOT retile on focus, since a focus change moves
// nothing for index-placed layouts. Runs the engine's real retile pipeline with
// real screen geometry and the bundled Luau algorithms.

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorScreens/Manager.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "FakeScreenProvider.h"

#include "helpers/AutotileTestHelpers.h"
#include "helpers/ScriptedAlgoTestSetup.h"

using PhosphorTileEngine::AutotileEngine;

class TestAutotileFocusRetile : public QObject
{
    Q_OBJECT

    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

    // The Theater spotlight is the single widest (centered) zone; return the
    // window that currently owns it. tiledWindows() is parallel to calculatedZones().
    static QString spotlightWindow(PhosphorTiles::TilingState* state)
    {
        const QVector<QRect> zones = state->calculatedZones();
        const QStringList wins = state->tiledWindows();
        if (zones.isEmpty() || zones.size() != wins.size()) {
            return QString();
        }
        int best = 0;
        for (int i = 1; i < zones.size(); ++i) {
            if (zones[i].width() > zones[best].width()) {
                best = i;
            }
        }
        return wins.at(best);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    void theater_focusChange_movesSpotlight()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();

        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("DP-1")});
        engine.setAlgorithm(QLatin1String("theater"));
        engine.windowOpened(QStringLiteral("a1"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a2"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a3"), QStringLiteral("DP-1"));

        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(QStringLiteral("DP-1"));
        QTRY_COMPARE(d1->calculatedZones().size(), 3);

        // Focus the first window: it must hold the spotlight.
        engine.windowFocused(QStringLiteral("a1"), QStringLiteral("DP-1"));
        QTRY_COMPARE(spotlightWindow(d1), QStringLiteral("a1"));

        // Focusing a different tiled window must reflow so the spotlight follows.
        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.windowFocused(QStringLiteral("a3"), QStringLiteral("DP-1"));
        QTRY_COMPARE(spotlightWindow(d1), QStringLiteral("a3"));
        QVERIFY2(tiledSpy.count() > 0, "focus change did not emit a retile to the compositor");
    }

    void theater_focusBeforeTrack_seedsSpotlightOnAdd()
    {
        // Daemon-restart ordering: the effect re-notifies the active window during
        // bring-up (windowFocused) BEFORE the window re-announce (windowOpened)
        // lands. The focused id names a window the engine doesn't track yet, so the
        // notification must be stashed and replayed once the window is added —
        // otherwise Theater has no focused window and defaults the spotlight to
        // index 0 until the user clicks around.
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();

        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("DP-1")});
        engine.setAlgorithm(QLatin1String("theater"));

        // Focus arrives for a window that is not yet tracked (pre-announce).
        engine.windowFocused(QStringLiteral("a2"), QStringLiteral("DP-1"));

        // Windows are (re)announced afterwards. a2 is neither first nor last, so a
        // spotlight on a2 can only come from the replayed focus, not a default.
        engine.windowOpened(QStringLiteral("a1"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a2"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a3"), QStringLiteral("DP-1"));

        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(QStringLiteral("DP-1"));
        QTRY_COMPARE(d1->calculatedZones().size(), 3);
        QCOMPARE(d1->focusedWindow(), QStringLiteral("a2"));
        QTRY_COMPARE(spotlightWindow(d1), QStringLiteral("a2"));
    }

    void columns_focusChange_doesNotRetile()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();

        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("DP-1")});
        engine.setAlgorithm(QLatin1String("columns"));
        engine.windowOpened(QStringLiteral("a1"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a2"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a3"), QStringLiteral("DP-1"));

        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(QStringLiteral("DP-1"));
        QTRY_COMPARE(d1->calculatedZones().size(), 3);

        // Establish focus, then watch: a non-focus-driven layout must stay put.
        engine.windowFocused(QStringLiteral("a1"), QStringLiteral("DP-1"));
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.windowFocused(QStringLiteral("a3"), QStringLiteral("DP-1"));
        QCoreApplication::processEvents();
        QCOMPARE(tiledSpy.count(), 0);
    }

    void theater_refocusSameWindow_doesNotRetile()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();

        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("DP-1")});
        engine.setAlgorithm(QLatin1String("theater"));
        engine.windowOpened(QStringLiteral("a1"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a2"), QStringLiteral("DP-1"));

        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(QStringLiteral("DP-1"));
        QTRY_COMPARE(d1->calculatedZones().size(), 2);

        // Establish focus on a1, then re-focus the SAME window: even a
        // focus-driven layout must not reflow when focus did not actually move.
        engine.windowFocused(QStringLiteral("a1"), QStringLiteral("DP-1"));
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.windowFocused(QStringLiteral("a1"), QStringLiteral("DP-1"));
        QCoreApplication::processEvents();
        QCOMPARE(tiledSpy.count(), 0);
    }

    void theater_floatingWindowFocus_doesNotRetile()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();

        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("DP-1")});
        engine.setAlgorithm(QLatin1String("theater"));
        engine.windowOpened(QStringLiteral("a1"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a2"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a3"), QStringLiteral("DP-1"));

        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(QStringLiteral("DP-1"));
        QTRY_COMPARE(d1->calculatedZones().size(), 3);

        engine.windowFocused(QStringLiteral("a1"), QStringLiteral("DP-1"));
        QCoreApplication::processEvents();

        // Float a3: it leaves the tiled set (this reflow is expected, before the spy).
        engine.floatWindow(QStringLiteral("a3"));
        QCoreApplication::processEvents();
        QTRY_COMPARE(d1->tiledWindows().size(), 2);

        // Focusing the now-floating window must not reflow the layout — the guard
        // only reflows when the focused window is tiled.
        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.windowFocused(QStringLiteral("a3"), QStringLiteral("DP-1"));
        QCoreApplication::processEvents();
        QCOMPARE(tiledSpy.count(), 0);
    }
};

QTEST_MAIN(TestAutotileFocusRetile)
#include "test_autotile_focus_retile.moc"
