// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pins the autotile seed-order filter (Daemon::seedAutotileOrderForScreen's
// admission predicate, extracted as filterAutotileSeedOrder): minimized
// windows stay as positional placeholders, live user floats and durable
// snap-slot floats are dropped, plain tiled windows pass through.

#include <QObject>
#include <QTest>

#include "daemon/daemon/seedorderfilter.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubZoneDetector.h"
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
PhosphorEngine::WindowMetadata makeMeta(const QString& appId, bool minimized)
{
    PhosphorEngine::WindowMetadata meta;
    meta.appId = appId;
    meta.isMinimized = minimized;
    return meta;
}
} // namespace

class TestAutotileSeedFilter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_parent = new QObject(nullptr);
        auto* layoutManager =
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), m_parent);
        auto* virtualDesktopManager = new PhosphorWorkspaces::VirtualDesktopManager(m_parent);
        auto* zoneDetector = new StubZoneDetector(m_parent);
        m_service =
            new PhosphorPlacement::WindowTrackingService(layoutManager, zoneDetector, nullptr, virtualDesktopManager,
                                                         nullptr, PhosphorPlacement::PlacementConfig{}, m_parent);
        m_registry = new PhosphorEngine::WindowRegistry(m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_guard.reset();
    }

    void tiledWindows_pass()
    {
        m_registry->upsert(QStringLiteral("u1"), makeMeta(QStringLiteral("kate"), false));
        QStringList order{QStringLiteral("kate|u1")};
        filterAutotileSeedOrder(order, m_service, m_registry);
        QCOMPARE(order, QStringList{QStringLiteral("kate|u1")});
    }

    void liveFloat_dropped()
    {
        m_registry->upsert(QStringLiteral("u1"), makeMeta(QStringLiteral("kate"), false));
        m_service->setWindowFloating(QStringLiteral("kate|u1"), true);
        QStringList order{QStringLiteral("kate|u1")};
        filterAutotileSeedOrder(order, m_service, m_registry);
        QVERIFY(order.isEmpty());
    }

    void minimizedFloat_keptAsPlaceholder()
    {
        // A minimize-floated window (suspension float) keeps its position in
        // the order; the engine's strict seed defers tiling it until its
        // windowOpened arrives.
        m_registry->upsert(QStringLiteral("u1"), makeMeta(QStringLiteral("kate"), true));
        m_service->setWindowFloating(QStringLiteral("kate|u1"), true);
        QStringList order{QStringLiteral("kate|u1")};
        filterAutotileSeedOrder(order, m_service, m_registry);
        QCOMPARE(order, QStringList{QStringLiteral("kate|u1")});
    }

    void durableSnapFloatSlot_dropped()
    {
        // The mode flips before the second seed, so the live floating resolver
        // reads the empty autotile slot; the instance-exact record's floating
        // snap slot is what preserves the source-mode user float.
        m_registry->upsert(QStringLiteral("u1"), makeMeta(QStringLiteral("kate"), false));
        PhosphorEngine::WindowPlacement p;
        p.windowId = QStringLiteral("kate|u1");
        p.appId = QStringLiteral("kate");
        p.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        p.engines.insert(QString(PhosphorEngine::WindowPlacement::snapEngineId()), slot);
        p.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(10, 10, 400, 300));
        QVERIFY(m_service->placementStore().record(p));

        QStringList order{QStringLiteral("kate|u1")};
        filterAutotileSeedOrder(order, m_service, m_registry);
        QVERIFY(order.isEmpty());
    }

    void durableSnapFloatSlot_droppedEvenWhenMinimized()
    {
        // A user-floated-then-minimized window must not become a tile
        // placeholder: on unminimize it stays floating.
        m_registry->upsert(QStringLiteral("u1"), makeMeta(QStringLiteral("kate"), true));
        PhosphorEngine::WindowPlacement p;
        p.windowId = QStringLiteral("kate|u1");
        p.appId = QStringLiteral("kate");
        p.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        p.engines.insert(QString(PhosphorEngine::WindowPlacement::snapEngineId()), slot);
        p.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(10, 10, 400, 300));
        QVERIFY(m_service->placementStore().record(p));

        QStringList order{QStringLiteral("kate|u1")};
        filterAutotileSeedOrder(order, m_service, m_registry);
        QVERIFY(order.isEmpty());
    }

    void unknownWindow_noRegistryRecord_passes()
    {
        // No registry record and no float state: nothing disqualifies the
        // entry; the engine-side strict seed applies its own conservative
        // deferral for unknown windows.
        QStringList order{QStringLiteral("kate|ghost")};
        filterAutotileSeedOrder(order, m_service, m_registry);
        QCOMPARE(order, QStringList{QStringLiteral("kate|ghost")});
    }

    void nullRegistry_floatFilterStillApplies()
    {
        m_service->setWindowFloating(QStringLiteral("kate|u1"), true);
        QStringList order{QStringLiteral("kate|u1")};
        filterAutotileSeedOrder(order, m_service, nullptr);
        QVERIFY(order.isEmpty());
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QObject* m_parent = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    PhosphorEngine::WindowRegistry* m_registry = nullptr;
};

QTEST_MAIN(TestAutotileSeedFilter)
#include "test_autotile_seed_filter.moc"
