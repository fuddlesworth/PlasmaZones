// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// End-to-end cross-output move with a REAL algorithm + real screen geometry, so
// the engine's own retile pipeline actually runs (unlike the manual-zone tests
// in test_navigation_cross_surface). Reproduces two real-hardware bugs:
//   1. the source monitor must reflow after a window leaves it, and
//   2. a lone window on the destination monitor must be movable back.
//
// Own binary (own process-wide algorithm registry) so loading the Luau
// algorithms can't disturb the manual-zone tests.

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "FakeScreenProvider.h"
#include "core/crosssurfaceresolver.h"

#include "../helpers/AutotileTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

using PhosphorEngine::NavigationContext;
using PhosphorTileEngine::AutotileEngine;

class TestNavigationRetile : public QObject
{
    Q_OBJECT

    // Loads the bundled Luau algorithms into the process-wide testRegistry(),
    // so engines built with it tile for real.
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    void crossOutput_move_retilesSourceAndAllowsMoveBack()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
        provider.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();

        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
        PlasmaZones::CrossSurfaceResolver resolver(&manager, nullptr);
        engine.setCrossSurfaceResolver(&resolver);
        engine.setAutotileScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        engine.windowOpened(QStringLiteral("a1"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a2"), QStringLiteral("DP-1"));
        QTest::qWait(20); // let the post-open retile compute real zones

        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(QStringLiteral("DP-1"));
        QCOMPARE(d1->tiledWindows().size(), 2);
        QCOMPARE(d1->calculatedZones().size(), 2); // two real, non-empty tiles

        // Cross the geometrically-rightmost window right to DP-2.
        const QStringList d1wins = d1->tiledWindows();
        const QVector<QRect> d1zones = d1->calculatedZones();
        int rightIdx = 0;
        for (int i = 1; i < d1zones.size(); ++i) {
            if (d1zones[i].center().x() > d1zones[rightIdx].center().x()) {
                rightIdx = i;
            }
        }
        const QString rightWin = d1wins.at(rightIdx);
        const QString remainingWin = (rightWin == d1wins.at(0)) ? d1wins.at(1) : d1wins.at(0);

        // Capture the geometry batches the engine emits to the compositor from
        // the move onward.
        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        engine.swapFocusedInDirection(QStringLiteral("right"), NavigationContext{rightWin, QStringLiteral("DP-1")});
        QTest::qWait(20);

        // BUG 2: the source monitor must reflow — its one remaining window now
        // fills (nearly) the whole monitor, not the old half-width tile.
        PhosphorTiles::TilingState* d1after = engine.tilingStateForScreen(QStringLiteral("DP-1"));
        QCOMPARE(d1after->tiledWindows().size(), 1);
        QCOMPARE(d1after->calculatedZones().size(), 1);
        QVERIFY2(d1after->calculatedZones().first().width() > 1500,
                 "source monitor did not reflow after the window left (still half-width)");

        // BUG 2 (the real-hardware half): the reflow geometry must be EMITTED to
        // the compositor, not just recomputed in the engine's state.
        bool sourceReflowEmitted = false;
        for (const auto& call : tiledSpy) {
            if (call.at(0).toString().contains(remainingWin)) {
                sourceReflowEmitted = true;
            }
        }
        QVERIFY2(sourceReflowEmitted, "source reflow geometry was never emitted to the compositor");

        // The window moved to DP-2 and tiled there.
        PhosphorTiles::TilingState* d2 = engine.tilingStateForScreen(QStringLiteral("DP-2"));
        QVERIFY(d2->tiledWindows().contains(rightWin));

        // BUG 1: that lone window on DP-2 must be movable back to DP-1.
        engine.swapFocusedInDirection(QStringLiteral("left"), NavigationContext{rightWin, QStringLiteral("DP-2")});
        QTest::qWait(20);
        QVERIFY(engine.tilingStateForScreen(QStringLiteral("DP-1"))->tiledWindows().contains(rightWin));
        QVERIFY(!engine.tilingStateForScreen(QStringLiteral("DP-2"))->tiledWindows().contains(rightWin));
    }

    // A cross-DESKTOP move leaves desktop placement to the compositor: the
    // engine only emits windowDesktopMoveRequested, and the source reflows
    // REACTIVELY when the effect reports the window left the current desktop
    // (windowClosed → onWindowRemoved → retile) — the same path a native KWin
    // desktop move takes. This is the "retiling isn't happening when we move a
    // window off a context" bug: prove the remaining window on the source
    // context reflows AND the reflow geometry is emitted, with a real algorithm.
    void contextRemoval_reactiveWindowClosed_reflowsSource()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();

        AutotileEngine engine(nullptr, nullptr, &manager, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("DP-1")});
        engine.windowOpened(QStringLiteral("a1"), QStringLiteral("DP-1"));
        engine.windowOpened(QStringLiteral("a2"), QStringLiteral("DP-1"));
        QTest::qWait(20);

        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(QStringLiteral("DP-1"));
        QCOMPARE(d1->tiledWindows().size(), 2);
        QCOMPARE(d1->calculatedZones().size(), 2);
        const QString leaving = d1->tiledWindows().at(0);
        const QString staying = d1->tiledWindows().at(1);

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);
        // Simulate the compositor reporting the window left the context — exactly
        // what the effect's "moved off current desktop, removed from autotile"
        // sends after windowToDesktops relocates the real window.
        engine.windowClosed(leaving);
        QTest::qWait(20);

        PhosphorTiles::TilingState* after = engine.tilingStateForScreen(QStringLiteral("DP-1"));
        QCOMPARE(after->tiledWindows().size(), 1);
        QCOMPARE(after->calculatedZones().size(), 1);
        QVERIFY2(after->calculatedZones().first().width() > 1500,
                 "remaining window did not reflow after the other left the context (still half-width)");

        bool reflowEmitted = false;
        for (const auto& call : tiledSpy) {
            if (call.at(0).toString().contains(staying)) {
                reflowEmitted = true;
            }
        }
        QVERIFY2(reflowEmitted, "context-removal reflow geometry was never emitted to the compositor");
    }
};

QTEST_MAIN(TestNavigationRetile)
#include "test_navigation_retile.moc"
