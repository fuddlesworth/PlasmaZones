// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_reopen_same_screen.cpp
 * @brief The reopen contract through the tiling adaptor's open dispatch: a
 *        window is placed on the screen it announces on, never pulled back to
 *        the screen its own record names.
 *
 * Wires a REAL ScreenManager (FakePhysicalScreenSource) because the dispatch
 * only releases a record whose screen is present, so a record on an unplugged
 * output stays parked for its return.
 */

#include <QTest>
#include <QCoreApplication>
#include <QRect>
#include <QString>
#include <memory>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "FakePhysicalScreenSource.h"
#include "dbus/tilingadaptor/tilingadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "helpers/AutotileTestHelpers.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PhosphorEngine::WindowPlacement;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QRect kOutput1(0, 0, 1600, 900);
const QRect kOutput2(1600, 0, 1600, 900);
} // namespace

class TestReopenSameScreen : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    PlasmaZones::StubZoneDetector* m_zoneDetector = nullptr;

    void assignMode(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode)
    {
        PhosphorZones::AssignmentEntry entry;
        entry.mode = mode;
        if (mode == PhosphorZones::AssignmentEntry::Autotile) {
            entry.tilingAlgorithm = QStringLiteral("dwindle");
        }
        m_layoutManager->setAssignmentEntryDirect(screenId, 0, QString(), entry);
    }

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new PlasmaZones::StubZoneDetector(nullptr);
    }

    void cleanup()
    {
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_guard.reset();
    }

    // F600: a window whose own record is tiled on DP-1 announces on DP-2 (it
    // was moved while nothing tracked it). It is tiled on DP-2 where it
    // stands, and its DP-1 slot no longer reads as a home.
    void testOwnRecordOnAnotherOutput_tilesWhereItAnnounces()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), kOutput1, QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-2"), kOutput2, QStringLiteral("DP-2"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        assignMode(QStringLiteral("DP-1"), PhosphorZones::AssignmentEntry::Autotile);
        assignMode(QStringLiteral("DP-2"), PhosphorZones::AssignmentEntry::Autotile);
        PhosphorTileEngine::AutotileEngine autotile(m_layoutManager, wta->service(), nullptr,
                                                    PlasmaZones::TestHelpers::testRegistry());
        autotile.setAutotileScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        wta->setEngines(nullptr, &autotile, nullptr);
        // Null screen manager on the adaptor: the panel gate never engages, so
        // the open dispatches synchronously.
        auto* tiling = new TilingAdaptor(nullptr, &parent);
        tiling->setWindowTrackingAdaptor(wta);
        tiling->setLifecycleEngines(QVector<PhosphorEngine::IPlacementEngine*>{&autotile});

        const QString w = QStringLiteral("term|f600");
        WindowPlacement own;
        own.windowId = w;
        own.appId = QStringLiteral("term");
        own.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = QString(WindowPlacement::stateTiled());
        slot.order = 0;
        own.engines.insert(autotile.engineId(), slot);
        QVERIFY(wta->service()->placementStore().record(own));

        tiling->windowOpened(w, QStringLiteral("DP-2"), 0, 0);
        QCoreApplication::processEvents();

        QCOMPARE(autotile.heldScreenForWindow(w), QStringLiteral("DP-2"));
        const auto rec = wta->service()->placementStore().peekExact(w);
        QVERIFY(rec.has_value());
        QVERIFY2(!(rec->screenId == QStringLiteral("DP-1")
                   && rec->slotFor(autotile.engineId()).state == QString(WindowPlacement::stateTiled())),
                 "the slot on the screen the window left must not stand as a home");

        tiling->clearEngine();
        wta->setEngines(nullptr, nullptr, nullptr);
    }

    // F621: a tiled window moved from an autotile output to a scrolling one
    // (release, then re-announce) becomes a strip column there, and the
    // mirror becomes a tile. Neither engine defers the arrival to the other's
    // record of where the window used to be.
    void testMoveBetweenTilingEngines_adoptedOnArrival()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), kOutput1, QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-2"), kOutput2, QStringLiteral("DP-2"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        assignMode(QStringLiteral("DP-1"), PhosphorZones::AssignmentEntry::Autotile);
        assignMode(QStringLiteral("DP-2"), PhosphorZones::AssignmentEntry::Scrolling);
        PhosphorTileEngine::AutotileEngine autotile(m_layoutManager, wta->service(), nullptr,
                                                    PlasmaZones::TestHelpers::testRegistry());
        autotile.setAutotileScreens({QStringLiteral("DP-1")});
        auto* scroll = new PhosphorScrollEngine::ScrollEngine(wta->service(), nullptr, &parent);
        scroll->setScreenGeometryProviders(
            [](const QString&) {
                return kOutput2;
            },
            [](const QString&) {
                return kOutput2;
            });
        scroll->setActiveScreens({QStringLiteral("DP-2")});
        wta->setEngines(nullptr, &autotile, scroll);
        auto* tiling = new TilingAdaptor(nullptr, &parent);
        tiling->setWindowTrackingAdaptor(wta);
        tiling->setLifecycleEngines(QVector<PhosphorEngine::IPlacementEngine*>{&autotile, scroll});

        // Autotile to scrolling.
        const QString a = QStringLiteral("term|f621a");
        tiling->windowOpened(a, QStringLiteral("DP-1"), 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(autotile.heldScreenForWindow(a), QStringLiteral("DP-1"));
        wta->captureWindowPlacement(a);
        tiling->releaseWindowTracking(a);
        tiling->windowOpened(a, QStringLiteral("DP-2"), 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(scroll->heldScreenForWindow(a), QStringLiteral("DP-2"));
        QVERIFY(autotile.heldScreenForWindow(a).isEmpty());

        // Scrolling to autotile.
        const QString b = QStringLiteral("term|f621b");
        tiling->windowOpened(b, QStringLiteral("DP-2"), 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(scroll->heldScreenForWindow(b), QStringLiteral("DP-2"));
        wta->captureWindowPlacement(b);
        tiling->releaseWindowTracking(b);
        tiling->windowOpened(b, QStringLiteral("DP-1"), 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(autotile.heldScreenForWindow(b), QStringLiteral("DP-1"));
        QVERIFY(scroll->heldScreenForWindow(b).isEmpty());

        tiling->clearEngine();
        wta->setEngines(nullptr, nullptr, nullptr);
    }

    // F1000: a snap float on DP-1 moved to autotile DP-2 while nothing
    // reported the crossing (an effect reload) is announced on DP-2 only.
    // Its adoption there releases snap's memory on DP-1.
    void testAdoptionReleasesSnapMemoryOnTheScreenLeft()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), kOutput1, QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-2"), kOutput2, QStringLiteral("DP-2"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        PhosphorZones::Layout* layout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);
        assignMode(QStringLiteral("DP-2"), PhosphorZones::AssignmentEntry::Autotile);
        PhosphorSnapEngine::SnapEngine snap(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        snap.setEngineSettings(m_settings);
        wta->service()->setSnapState(snap.snapState());
        wta->service()->setSnapEngine(&snap);
        // Per-screen stores, as the daemon wires them.
        PhosphorPlacement::WindowTrackingService::SnapStateResolver resolver;
        resolver.forWindow = [&snap](const QString& id) {
            return snap.stateForWindow(id);
        };
        resolver.forWindowOnScreen = [&snap](const QString& id, const QString& s, int desktop) {
            return snap.stateForWindowOnScreen(id, s, desktop);
        };
        resolver.forScreen = [&snap](const QString& s) {
            return static_cast<PhosphorSnapEngine::SnapState*>(snap.stateForScreen(s));
        };
        resolver.globals = [&snap]() {
            return snap.globalState();
        };
        resolver.allStates = [&snap]() {
            return snap.allSnapStates();
        };
        resolver.forgetWindow = [&snap](const QString& id) {
            snap.forgetWindow(id);
        };
        resolver.holdsWindow = [&snap](const QString& id, const PhosphorSnapEngine::SnapState* state) {
            return snap.holdsWindowInState(id, state);
        };
        wta->service()->setSnapStateResolver(resolver);
        PhosphorTileEngine::AutotileEngine autotile(m_layoutManager, wta->service(), nullptr,
                                                    PlasmaZones::TestHelpers::testRegistry());
        autotile.setAutotileScreens({QStringLiteral("DP-2")});
        wta->setEngines(&snap, &autotile, nullptr);
        auto* tiling = new TilingAdaptor(nullptr, &parent);
        tiling->setWindowTrackingAdaptor(wta);
        tiling->setLifecycleEngines(QVector<PhosphorEngine::IPlacementEngine*>{&autotile});

        const QString w = QStringLiteral("term|f1000");
        snap.setCurrentDesktopForScreen(QStringLiteral("DP-1"), 1);
        snap.commitSnap(w, layout->zones().first()->id().toString(), QStringLiteral("DP-1"));
        snap.setWindowFloat(w, true, QStringLiteral("DP-1"));
        QCOMPARE(snap.screenForTrackedWindow(w), QStringLiteral("DP-1"));

        tiling->windowOpened(w, QStringLiteral("DP-2"), 0, 0);
        QCoreApplication::processEvents();

        QCOMPARE(autotile.heldScreenForWindow(w), QStringLiteral("DP-2"));
        QVERIFY2(snap.screenForTrackedWindow(w).isEmpty(), "snap must not keep the window on the screen it left");
        QVERIFY(!snap.isFloating(w));

        tiling->clearEngine();
        wta->setEngines(nullptr, nullptr, nullptr);
        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
    }
};

QTEST_MAIN(TestReopenSameScreen)
#include "test_reopen_same_screen.moc"
