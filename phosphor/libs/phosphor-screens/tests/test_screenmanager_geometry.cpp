// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Regression tests for ScreenManager's screen add / remove / move / resize
// -> available-geometry recompute sequence. Driven through a
// FakeScreenProvider so the topology changes that QScreen cannot stage in a
// unit test (the bugs behind discussions #461 / #465 escaped CI precisely
// because QScreen is uninstantiable by test code) become deterministically
// reproducible.

#include "FakeScreenProvider.h"

#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/PhysicalScreen.h>

#include <QSignalSpy>
#include <QTest>

using PhosphorScreens::FakeScreenProvider;
using PhosphorScreens::PhysicalScreen;
using PhosphorScreens::ScreenManager;
using PhosphorScreens::ScreenManagerConfig;

class TestScreenManagerGeometry : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        // Explicit registration so QSignalSpy can record the PhysicalScreen
        // arguments the ScreenManager signals carry.
        qRegisterMetaType<PhysicalScreen>();
    }

    // The manager's tracked set mirrors the provider — both the screens
    // present before start() and hot-plug add/remove afterward.
    void testTracksProviderScreenSet()
    {
        FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        mgr.start();
        QCOMPARE(mgr.screens().size(), 1);
        QCOMPARE(mgr.screens().first().name, QStringLiteral("DP-1"));

        QSignalSpy addedSpy(&mgr, &ScreenManager::screenAdded);
        QSignalSpy removedSpy(&mgr, &ScreenManager::screenRemoved);

        fake.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080));
        QCOMPARE(addedSpy.count(), 1);
        QCOMPARE(addedSpy.at(0).at(0).value<PhysicalScreen>().name, QStringLiteral("DP-2"));
        QCOMPARE(mgr.screens().size(), 2);

        fake.removeScreen(QStringLiteral("DP-1"));
        QCOMPARE(removedSpy.count(), 1);
        QCOMPARE(removedSpy.at(0).at(0).value<PhysicalScreen>().name, QStringLiteral("DP-1"));
        QCOMPARE(mgr.screens().size(), 1);
        QCOMPARE(mgr.screens().first().name, QStringLiteral("DP-2"));
    }

    // Moving an output recomputes its available geometry. Direct regression
    // for #465: before the geometry-change handler recomputed,
    // availableGeometryChanged never fired on a pure move and
    // actualAvailableGeometry kept returning the stale origin rect.
    void testMoveRecomputesAvailableGeometry()
    {
        FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        mgr.start();

        QSignalSpy geomSpy(&mgr, &ScreenManager::screenGeometryChanged);
        QSignalSpy availSpy(&mgr, &ScreenManager::availableGeometryChanged);

        const QRect moved(1920, 0, 1920, 1080);
        fake.moveScreen(QStringLiteral("DP-1"), moved);

        QCOMPARE(geomSpy.count(), 1);
        QCOMPARE(geomSpy.at(0).at(0).value<PhysicalScreen>().geometry, moved);

        QCOMPARE(availSpy.count(), 1);
        QCOMPARE(availSpy.at(0).at(0).value<PhysicalScreen>().name, QStringLiteral("DP-1"));
        QCOMPARE(availSpy.at(0).at(1).toRect(), moved);

        // No panel source / sensors, so available geometry == full rect —
        // and it must follow the move rather than stay at the old origin.
        QCOMPARE(mgr.screenGeometry(QStringLiteral("DP-1")), moved);
        QVERIFY(mgr.physicalScreenFor(QStringLiteral("DP-1")).isValid());
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), moved);
    }

    // A resize (origin fixed, extent changed) runs the same recompute path.
    void testResizeRecomputesAvailableGeometry()
    {
        FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        mgr.start();

        QSignalSpy availSpy(&mgr, &ScreenManager::availableGeometryChanged);

        const QRect resized(0, 0, 2560, 1440);
        fake.moveScreen(QStringLiteral("DP-1"), resized);

        QCOMPARE(availSpy.count(), 1);
        QCOMPARE(availSpy.at(0).at(1).toRect(), resized);
        QVERIFY(mgr.physicalScreenFor(QStringLiteral("DP-1")).isValid());
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), resized);

        // A no-op resize (identical geometry) is suppressed by
        // FakeScreenProvider — mirroring Qt, which does not fire
        // QScreen::geometryChanged when geometry() is unchanged — so it
        // triggers no recompute and no further availableGeometryChanged.
        fake.moveScreen(QStringLiteral("DP-1"), resized);
        QCOMPARE(availSpy.count(), 1);
    }

    // The #465 DPMS-wake shape exactly: an output drops, re-appears at a
    // transient (0,0) origin, then settles to its real desktop position.
    // The settle must propagate to the available-geometry cache.
    void testReAddAtTransientOriginThenSettle()
    {
        FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        mgr.start();

        // Output drops on DPMS-off, then re-appears at a transient origin.
        fake.removeScreen(QStringLiteral("DP-1"));
        const QRect transient(0, 0, 1920, 1080);
        fake.addScreen(QStringLiteral("DP-1"), transient);

        // The re-added output is tracked, sitting at its transient origin.
        QVERIFY(mgr.physicalScreenFor(QStringLiteral("DP-1")).isValid());
        QCOMPARE(mgr.screenGeometry(QStringLiteral("DP-1")), transient);
        // Pin the available rect at the transient stage: the re-add must not
        // carry a stale pre-removal available rect — the exact #465 failure.
        // With no panel source it equals the transient screen rect here.
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), transient);

        QSignalSpy availSpy(&mgr, &ScreenManager::availableGeometryChanged);

        // Compositor settles the output to its real desktop position.
        const QRect settled(2560, 0, 1920, 1080);
        fake.moveScreen(QStringLiteral("DP-1"), settled);

        QCOMPARE(availSpy.count(), 1);
        QCOMPARE(availSpy.at(0).at(1).toRect(), settled);
        QVERIFY(mgr.physicalScreenFor(QStringLiteral("DP-1")).isValid());
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), settled);
    }

    // Compositor-reported client area (the KWin effect's clientArea push)
    // overrides the panel-strut heuristic. Direct regression for discussion
    // #461: a top panel left the heuristic attributing the whole reservation
    // to the bottom edge — the compositor source pins the correct top inset.
    void testCompositorGeometryOverridesHeuristic()
    {
        FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        mgr.start();

        const PhysicalScreen screen = mgr.physicalScreenFor(QStringLiteral("DP-1"));
        QVERIFY(screen.isValid());
        // No panel source / sensors: the heuristic yields the full screen rect.
        QCOMPARE(mgr.actualAvailableGeometry(screen), QRect(0, 0, 1920, 1080));

        QSignalSpy availSpy(&mgr, &ScreenManager::availableGeometryChanged);

        // KWin reports a 32px top panel.
        const QRect topPanelWorkArea(0, 32, 1920, 1048);
        mgr.setCompositorAvailableGeometry(QStringLiteral("DP-1"), topPanelWorkArea);

        QCOMPARE(availSpy.count(), 1);
        QCOMPARE(availSpy.at(0).at(1).toRect(), topPanelWorkArea);
        QCOMPARE(mgr.actualAvailableGeometry(screen), topPanelWorkArea);

        // A redundant push of the identical rect is a no-op — no extra signal.
        mgr.setCompositorAvailableGeometry(QStringLiteral("DP-1"), topPanelWorkArea);
        QCOMPARE(availSpy.count(), 1);

        // Clearing the override (empty rect) reverts to the heuristic.
        mgr.setCompositorAvailableGeometry(QStringLiteral("DP-1"), QRect());
        QCOMPARE(availSpy.count(), 2);
        QCOMPARE(mgr.actualAvailableGeometry(screen), QRect(0, 0, 1920, 1080));
    }

    // A compositor rect spilling past the output is clamped to the screen —
    // guards against a snapshot that lags a resize corrupting downstream
    // relative-geometry math.
    void testCompositorGeometryClampedToScreen()
    {
        FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        mgr.start();

        // 32px top inset but height still the full 1080 — bottom edge spills
        // 32px past the output. The intersect clamps it back.
        mgr.setCompositorAvailableGeometry(QStringLiteral("DP-1"), QRect(0, 32, 1920, 1080));
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), QRect(0, 32, 1920, 1048));
    }

    // A compositor override is dropped when its screen disconnects, so a
    // same-connector reconnect starts from the heuristic rather than a stale
    // rect for the old output.
    void testCompositorGeometryDroppedOnScreenRemoval()
    {
        FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        ScreenManager mgr(ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        mgr.start();

        mgr.setCompositorAvailableGeometry(QStringLiteral("DP-1"), QRect(0, 32, 1920, 1048));
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), QRect(0, 32, 1920, 1048));

        fake.removeScreen(QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080));

        // Override gone — back to the heuristic's full-screen rect.
        QCOMPARE(mgr.actualAvailableGeometry(mgr.physicalScreenFor(QStringLiteral("DP-1"))), QRect(0, 0, 1920, 1080));
    }
};

QTEST_MAIN(TestScreenManagerGeometry)
#include "test_screenmanager_geometry.moc"
