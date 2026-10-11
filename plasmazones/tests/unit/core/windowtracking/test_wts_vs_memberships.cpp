// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_vs_memberships.cpp
 * @brief A virtual-screen change judges each zone by its own store's context (F533, F566)
 *        and re-files each store under the screen it records (F105).
 *
 * The prunes after a split or a reconfigure used to judge every membership by
 * the layout of the desktop in view, skip a window by its routed float bit,
 * and unassign through the window's primary store. The fixture runs the snap
 * engine's per-desktop stores through the daemon's resolver over a headless
 * 3840x2160 output, with a VirtualDesktopManager the layout registry follows.
 */

#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>
#include <memory>

#include "FakePhysicalScreenSource.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorPlacement/SnapStateResolver.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;
using PhosphorSnapEngine::SnapEngine;
using PhosphorSnapEngine::SnapState;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kPhys = QStringLiteral("DP-1");
const QString kVs0 = PhosphorIdentity::VirtualScreenId::make(kPhys, 0);
const QString kVs1 = PhosphorIdentity::VirtualScreenId::make(kPhys, 1);
const QString kWindow = QStringLiteral("app|vs-member");
} // namespace

class TestWtsVsMemberships : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_source = std::make_unique<PhosphorScreens::FakePhysicalScreenSource>();
        m_source->addScreen(kPhys, QRect(0, 0, 3840, 2160));
        m_manager = std::make_unique<PhosphorScreens::ScreenManager>(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = m_source.get(), .useGeometrySensors = false});
        m_manager->start();
        m_vdm = std::make_unique<PhosphorWorkspaces::VirtualDesktopManager>(nullptr);
        m_layoutManager->setCurrentVirtualDesktopProvider([this](const QString& screenId) -> std::optional<int> {
            const int d = m_vdm->currentDesktopForScreen(screenId);
            return d >= 1 ? std::optional<int>(d) : std::nullopt;
        });
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_manager.get(), m_vdm.get());
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        m_service->setSnapEngine(m_engine);
        m_service->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(m_engine));
        show(1);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);
        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
        m_vsLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(m_vsLayout);
    }

    void cleanup()
    {
        m_service->setEngineFloatResolver(nullptr);
        m_service->setSnapState(nullptr);
        m_service->setSnapEngine(nullptr);
        delete m_engine;
        m_engine = nullptr;
        delete m_service;
        m_service = nullptr;
        m_layoutManager->setCurrentVirtualDesktopProvider(nullptr);
        m_vdm.reset();
        m_manager.reset();
        m_source.reset();
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_testLayout = nullptr;
        m_vsLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
    }

    // Desktop 2 of the left half runs its own layout. A reconfigure while
    // desktop 1 is in view keeps the zone the window holds there.
    void vsPrune_otherDesktopKeepsItsOwnLayoutsZone()
    {
        const QStringList vsIds = split();
        PhosphorZones::Layout* desktopTwo = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(desktopTwo);
        m_layoutManager->assignLayout(kVs0, 1, QString(), m_vsLayout);
        m_layoutManager->assignLayout(kVs0, 2, QString(), desktopTwo);
        const QString zone = desktopTwo->zones().first()->id().toString();
        show(2);
        m_service->assignWindowToZone(kWindow, zone, kVs0, 2);
        show(1);

        m_service->migrateScreenAssignmentsToVirtual(kPhys, vsIds, m_manager.get());

        SnapState* store = storeFor(kVs0, 2);
        QVERIFY(store);
        QCOMPARE(store->zonesForWindow(kWindow), QStringList{zone});
    }

    // A window snapped on two desktops of the monitor loses the zone in both
    // stores when the split's layout has neither, and its record forgets both
    // desktops' zones.
    void vsPrune_everyMemberStoreCleared()
    {
        holdOnDesktopsOneAndTwo(kPhys, m_zoneIds[0], m_zoneIds[2]);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = kWindow;
        rec.appId = QStringLiteral("app");
        rec.screenId = kPhys;
        rec.virtualDesktop = 2;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{m_zoneIds[2]};
        slot.zonesByDesktop = {{1, {m_zoneIds[0]}}, {2, {m_zoneIds[2]}}};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        QVERIFY(m_service->placementStore().record(rec));
        show(1);

        const QStringList vsIds = split();
        for (const QString& vs : vsIds) {
            m_layoutManager->assignLayout(vs, 1, QString(), m_vsLayout);
            m_layoutManager->assignLayout(vs, 2, QString(), m_vsLayout);
        }
        m_service->migrateScreenAssignmentsToVirtual(kPhys, vsIds, m_manager.get());

        for (const SnapState* state : m_engine->allSnapStates()) {
            QVERIFY2(state->zonesForWindow(kWindow).isEmpty(), "every member store loses the zone");
        }
        const auto after = m_service->placementStore().peekExact(kWindow);
        QVERIFY(after.has_value());
        QVERIFY2(after->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).zonesByDesktop.isEmpty(),
                 "the record forgets both desktops' zones");
    }

    // The window floats on desktop 1 and is snapped on desktop 2 in a zone the
    // split's layout lacks. The float keeps its store; desktop 2 is pruned
    // whatever the routed float bit says (F566).
    void vsPrune_floatOnOneDesktopStillPrunesTheOther()
    {
        const QStringList vsIds = split();
        m_layoutManager->assignLayout(kVs0, 1, QString(), m_vsLayout);
        m_layoutManager->assignLayout(kVs0, 2, QString(), m_vsLayout);
        const QString kept = m_vsLayout->zones().first()->id().toString();
        holdOnDesktopsOneAndTwo(kVs0, kept, m_zoneIds[0]);
        show(1);
        SnapState* floated = storeFor(kVs0, 1);
        QVERIFY(floated);
        floated->setFloating(kWindow, true);
        // The routed bit answers the window's primary, here floating.
        m_service->setEngineFloatResolver([](const QString& windowId) {
            return windowId == kWindow;
        });

        m_service->migrateScreenAssignmentsToVirtual(kPhys, vsIds, m_manager.get());

        SnapState* snapped = storeFor(kVs0, 2);
        QVERIFY(snapped);
        QVERIFY2(snapped->zonesForWindow(kWindow).isEmpty(), "desktop 2's stale zone is pruned");
        QCOMPARE(floated->zonesForWindow(kWindow), QStringList{kept});
        QVERIFY(floated->isFloating(kWindow));
    }

    // Desktop 2 tiles. Its frozen snap memory is not judged against the
    // layout desktop 1 runs, and stays for its return to snapping (F264).
    void vsPrune_tilingContextKeepsFrozenZones()
    {
        const QStringList vsIds = split();
        m_layoutManager->assignLayout(kVs0, 1, QString(), m_vsLayout);
        PhosphorZones::AssignmentEntry tiling;
        tiling.mode = PhosphorZones::AssignmentEntry::Autotile;
        tiling.tilingAlgorithm = QStringLiteral("bsp");
        m_layoutManager->setAssignmentEntryDirect(kVs0, 2, QString(), tiling);
        show(2);
        m_service->assignWindowToZone(kWindow, m_zoneIds[0], kVs0, 2);
        show(1);

        m_service->migrateScreenAssignmentsToVirtual(kPhys, vsIds, m_manager.get());

        SnapState* store = storeFor(kVs0, 2);
        QVERIFY(store);
        QCOMPARE(store->zonesForWindow(kWindow), QStringList{m_zoneIds[0]});
    }

    // A window pruned to no zone at all gives up its record's snap slot, so
    // the next switch's populate does not put it back in the pruned zone.
    void vsPrune_noRepopulateAfterPrune()
    {
        const QStringList vsIds = split();
        m_layoutManager->assignLayout(kVs0, 1, QString(), m_vsLayout);
        m_service->assignWindowToZone(kWindow, m_zoneIds[0], kVs0, 1);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = kWindow;
        rec.appId = QStringLiteral("app");
        rec.screenId = kVs0;
        rec.virtualDesktop = 1;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{m_zoneIds[0]};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        QVERIFY(m_service->placementStore().record(rec));

        m_service->migrateScreenAssignmentsToVirtual(kPhys, vsIds, m_manager.get());
        QVERIFY(m_service->zonesForWindow(kWindow).isEmpty());

        m_service->populateResnapBufferForAllScreens({}, {kVs0}, 1);
        for (const PhosphorEngine::ResnapEntry& e : m_service->takeResnapBuffer()) {
            QVERIFY2(e.windowId != kWindow, "the pruned window must not be re-admitted from its record");
        }
    }

    // A window snapped before the split is filed under the store for the
    // virtual screen its zone lies in, desktop kept, not left on the
    // monitor's key (F105).
    void vsMigration_rekeysToTheVirtualScreensStore()
    {
        show(1);
        m_service->assignWindowToZone(kWindow, m_zoneIds[0], kPhys, 1);
        const QStringList vsIds = split();
        shareTheLayout(vsIds, m_testLayout);

        m_service->migrateScreenAssignmentsToVirtual(kPhys, vsIds, m_manager.get());

        SnapState* store = storeFor(kVs0, 1);
        QVERIFY2(store, "the left half's desktop 1 store exists");
        QVERIFY(m_engine->holdsWindowInState(kWindow, store));
        QCOMPARE(store->zonesForWindow(kWindow), QStringList{m_zoneIds[0]});
        QCOMPARE(store->screenForWindow(kWindow), kVs0);
        const SnapState* old = storeFor(kPhys, 1);
        QVERIFY2(!old || !m_engine->holdsWindowInState(kWindow, old), "the monitor's key no longer holds it");
    }

    // A window on desktops 1 and 2 of the monitor, snapped in a left-half
    // zone on each. After the split a move on desktop 1 is a move within the
    // left half and leaves desktop 2's zone alone; filed under the monitor's
    // key it read as a move off the monitor and released desktop 2 (F105).
    void vsMigration_moveKeepsOtherDesktop()
    {
        PhosphorZones::Layout* quarters = createTestLayout(4, m_layoutManager);
        m_layoutManager->addLayout(quarters);
        const QString first = quarters->zones().at(0)->id().toString();
        const QString second = quarters->zones().at(1)->id().toString();
        for (int desktop : {1, 2}) {
            m_layoutManager->assignLayout(kPhys, desktop, QString(), quarters);
        }
        holdOnDesktopsOneAndTwo(kPhys, first, second);
        show(1);
        const QStringList vsIds = split();
        shareTheLayout(vsIds, quarters);
        m_service->migrateScreenAssignmentsToVirtual(kPhys, vsIds, m_manager.get());

        m_service->assignWindowToZone(kWindow, second, kVs0, 1);

        SnapState* desktopOne = storeFor(kVs0, 1);
        QVERIFY(desktopOne);
        QCOMPARE(desktopOne->zonesForWindow(kWindow), QStringList{second});
        SnapState* desktopTwo = storeFor(kVs0, 2);
        QVERIFY2(desktopTwo, "desktop 2's store moved to the left half");
        QVERIFY2(m_engine->holdsWindowInState(kWindow, desktopTwo), "desktop 2 keeps its membership");
        QCOMPARE(desktopTwo->zonesForWindow(kWindow), QStringList{second});
    }

    // A window floated from a zone before the split moves with its float and
    // the home its unfloat returns to, re-pointed at the left half.
    void vsMigration_floatingWindowKeepsPreFloatHome()
    {
        show(1);
        m_service->assignWindowToZone(kWindow, m_zoneIds[0], kPhys, 1);
        SnapState* before = storeFor(kPhys, 1);
        QVERIFY(before);
        before->unsnapForFloat(kWindow);
        before->setFloating(kWindow, true);
        const QStringList vsIds = split();
        shareTheLayout(vsIds, m_testLayout);

        m_service->migrateScreenAssignmentsToVirtual(kPhys, vsIds, m_manager.get());

        SnapState* store = storeFor(kVs0, 1);
        QVERIFY(store);
        QVERIFY(m_engine->holdsWindowInState(kWindow, store));
        QVERIFY(store->isFloating(kWindow));
        QCOMPARE(store->preFloatZones(kWindow), QStringList{m_zoneIds[0]});
        QCOMPARE(store->preFloatScreen(kWindow), kVs0);
    }

private:
    /// Every desktop-1 and desktop-2 context of both halves runs @p layout.
    void shareTheLayout(const QStringList& vsIds, PhosphorZones::Layout* layout)
    {
        for (const QString& vs : vsIds) {
            for (int desktop : {1, 2}) {
                m_layoutManager->assignLayout(vs, desktop, QString(), layout);
            }
        }
    }

    /// Split DP-1 50/50 side by side and answer the virtual ids.
    QStringList split()
    {
        const auto makeDef = [](int index, const QRectF& region) {
            PhosphorScreens::VirtualScreenDef def;
            def.id = PhosphorIdentity::VirtualScreenId::make(kPhys, index);
            def.physicalScreenId = kPhys;
            def.displayName = QStringLiteral("Half %1").arg(index);
            def.region = region;
            def.index = index;
            return def;
        };
        PhosphorScreens::VirtualScreenConfig config;
        config.physicalScreenId = kPhys;
        config.screens.append(makeDef(0, QRectF(0, 0, 0.5, 1.0)));
        config.screens.append(makeDef(1, QRectF(0.5, 0, 0.5, 1.0)));
        if (!m_manager->setVirtualScreenConfig(kPhys, config)) {
            qFatal("could not split DP-1");
        }
        return m_manager->virtualScreenIdsFor(kPhys);
    }

    /// Show @p desktop on the monitor and both its halves.
    void show(int desktop)
    {
        m_vdm->updateScreenDesktop(kPhys, desktop);
        for (const QString& screen : {kPhys, kVs0, kVs1}) {
            m_engine->setCurrentDesktopForScreen(screen, desktop);
        }
    }

    /// kWindow on desktops 1 and 2 of @p screen, in @p zoneOne on 1 and @p zoneTwo on 2.
    void holdOnDesktopsOneAndTwo(const QString& screen, const QString& zoneOne, const QString& zoneTwo)
    {
        show(1);
        m_service->assignWindowToZone(kWindow, zoneOne, screen, 1);
        show(2);
        m_engine->reconcileDesktopMemberships(screen, [](const QString&) {
            PhosphorEngine::DesktopSpan span;
            span.known = true;
            span.desktops = {1, 2};
            return span;
        });
        m_service->assignWindowToZone(kWindow, zoneTwo, screen, 2);
    }

    /// The store filed under (@p screen, @p desktop).
    SnapState* storeFor(const QString& screen, int desktop) const
    {
        for (SnapState* state : m_engine->allSnapStates()) {
            const auto key = m_engine->keyForState(state);
            if (key && key->screenId == screen && key->desktop == desktop) {
                return state;
            }
        }
        return nullptr;
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    std::unique_ptr<PhosphorScreens::FakePhysicalScreenSource> m_source;
    std::unique_ptr<PhosphorScreens::ScreenManager> m_manager;
    std::unique_ptr<PhosphorWorkspaces::VirtualDesktopManager> m_vdm;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    PhosphorZones::Layout* m_vsLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsVsMemberships)
#include "test_wts_vs_memberships.moc"
