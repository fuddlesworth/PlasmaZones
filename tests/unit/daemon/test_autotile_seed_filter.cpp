// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pins the autotile seed-order filter (Daemon::seedAutotileOrderForScreen's
// admission predicate, extracted as filterEngineSeedOrder): float is per
// engine, so non-minimized windows always pass (a source-mode float must not
// make the window untileable in the target engine); minimized windows stay as
// positional placeholders EXCEPT user-floated-then-minimized ones (durable
// floating snap slot), which are dropped so unminimize restores the float.

#include <QObject>
#include <QTest>

#include "daemon/daemon/seedorderfilter.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
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
        m_service = new PhosphorPlacement::WindowTrackingService(layoutManager, nullptr, virtualDesktopManager, nullptr,
                                                                 PhosphorPlacement::PlacementConfig{}, m_parent);
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
        filterEngineSeedOrder(order, m_service, m_registry);
        QCOMPARE(order, QStringList{QStringLiteral("kate|u1")});
    }

    void liveFloat_passes()
    {
        // The toggle seeds BEFORE the assignment flips, so a live float read
        // here is the SOURCE mode's float (per-engine, not this engine's).
        // It must not disqualify the window: dropping it made a snap-floated
        // window untileable by mode swap.
        m_registry->upsert(QStringLiteral("u1"), makeMeta(QStringLiteral("kate"), false));
        m_service->setWindowFloating(QStringLiteral("kate|u1"), true);
        QStringList order{QStringLiteral("kate|u1")};
        filterEngineSeedOrder(order, m_service, m_registry);
        QCOMPARE(order, QStringList{QStringLiteral("kate|u1")});
    }

    void minimizedFloat_keptAsPlaceholder()
    {
        // A minimize-floated window (suspension float) keeps its position in
        // the order; the engine's strict seed defers tiling it until its
        // windowOpened arrives.
        m_registry->upsert(QStringLiteral("u1"), makeMeta(QStringLiteral("kate"), true));
        m_service->setWindowFloating(QStringLiteral("kate|u1"), true);
        QStringList order{QStringLiteral("kate|u1")};
        filterEngineSeedOrder(order, m_service, m_registry);
        QCOMPARE(order, QStringList{QStringLiteral("kate|u1")});
    }

    void durableSnapFloatSlot_passes()
    {
        // The snap slot's stateFloating is the window's SNAPPING-mode verdict.
        // It stays untouched for the return-to-snapping restore, but it must
        // not leak into a tiling seed: dropping on it meant even an explicit
        // Meta+F tile in autotile never survived a mode round trip (each
        // snapping interlude re-poisoned the next seed).
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
        filterEngineSeedOrder(order, m_service, m_registry);
        QCOMPARE(order, QStringList{QStringLiteral("kate|u1")});
    }

    void durableSnapFloatSlot_droppedWhenMinimized()
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
        filterEngineSeedOrder(order, m_service, m_registry);
        QVERIFY(order.isEmpty());
    }

    void unknownWindow_noRegistryRecord_passes()
    {
        // No registry record and no float state: nothing disqualifies the
        // entry; the engine-side strict seed applies its own conservative
        // deferral for unknown windows.
        QStringList order{QStringLiteral("kate|ghost")};
        filterEngineSeedOrder(order, m_service, m_registry);
        QCOMPARE(order, QStringList{QStringLiteral("kate|ghost")});
    }

    void nullRegistry_liveFloatStillPasses()
    {
        // Without a registry, minimized state is unknowable and every entry
        // reads as non-minimized, which under per-engine admission means it
        // passes. The seed call site warns loudly about the missing registry;
        // the filter itself stays consistent with the non-minimized rule.
        m_service->setWindowFloating(QStringLiteral("kate|u1"), true);
        QStringList order{QStringLiteral("kate|u1")};
        filterEngineSeedOrder(order, m_service, nullptr);
        QCOMPARE(order, QStringList{QStringLiteral("kate|u1")});
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QObject* m_parent = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    PhosphorEngine::WindowRegistry* m_registry = nullptr;
};

QTEST_MAIN(TestAutotileSeedFilter)
#include "test_autotile_seed_filter.moc"
