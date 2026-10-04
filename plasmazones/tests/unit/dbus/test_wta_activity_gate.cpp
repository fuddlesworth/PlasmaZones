// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_activity_gate.cpp
 * @brief The disabled-context gates on the snap restore paths ask about the
 *        current activity (F480): the instant-restore cache the effect reads
 *        (getPendingRestoreGeometries) and the snap engine's restore
 *        predicate. Both passed no activity, so a disabled activity never
 *        gated them.
 */

#include <QTest>
#include <QString>
#include <memory>

#include <PhosphorContext/IContextResolver.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

/// Answers ActivityDisabled for one activity and nothing else.
class ActivityDisablingResolver : public PhosphorContext::IContextResolver
{
public:
    QString disabledActivity;

    PhosphorContext::ContextHandle handleFor(const QString& screenId) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        return h;
    }
    PhosphorContext::ContextHandle globalHandle() const override
    {
        return {};
    }
    PhosphorContext::ContextHandle handleForMode(const QString& screenId,
                                                 PhosphorZones::AssignmentEntry::Mode mode) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        h.mode = mode;
        return h;
    }
    PhosphorContext::ContextHandle handleForPersisted(const QString& screenId, int virtualDesktop,
                                                      const QString& activity) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        h.virtualDesktop = virtualDesktop;
        h.activity = activity;
        return h;
    }
    int currentVirtualDesktop() const override
    {
        return 0;
    }
    QString currentActivity() const override
    {
        return QString();
    }
    PhosphorContext::DisabledReason disabledReason(const PhosphorContext::ContextHandle& h) const override
    {
        return (!disabledActivity.isEmpty() && h.activity == disabledActivity)
            ? PhosphorContext::DisabledReason::ActivityDisabled
            : PhosphorContext::DisabledReason::NotDisabled;
    }
    bool isLocked(const PhosphorContext::ContextHandle&) const override
    {
        return false;
    }
};

namespace {
const QString kActivity = QStringLiteral("act-Y");
} // namespace

class TestWtaActivityGate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_desktopManager = new PhosphorWorkspaces::VirtualDesktopManager(nullptr);
        m_parent = new QObject(nullptr);
        m_wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, m_desktopManager,
                                          nullptr, m_parent);
        m_snapEngine = new SnapEngine(m_layoutManager, m_wta->service(), m_zoneDetector, nullptr, nullptr);
        m_snapEngine->setEngineSettings(m_settings);
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        m_wta->service()->setSnapEngine(m_snapEngine);
        // setEngines installs the snap engine's restore predicate.
        m_wta->setEngines(m_snapEngine, nullptr, nullptr);
        m_wta->setContextResolver(&m_resolver);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);
        m_layoutManager->setCurrentActivity(kActivity);
        m_zoneId = m_testLayout->zones().first()->id().toString();
    }

    void cleanup()
    {
        m_wta->setContextResolver(nullptr);
        m_wta->service()->setSnapState(nullptr);
        m_wta->service()->setSnapEngine(nullptr);
        delete m_snapEngine;
        m_snapEngine = nullptr;
        delete m_parent; // owns the WTA
        m_parent = nullptr;
        m_wta = nullptr;
        delete m_desktopManager;
        m_desktopManager = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_testLayout = nullptr;
        m_resolver.disabledActivity.clear();
        m_guard.reset();
    }

    // The instant-restore cache leaves out a record when the activity in view
    // is disabled. It asked with no activity, so the effect teleported the
    // opener into a zone of a disabled activity.
    void pendingRestoreSkipsADisabledActivity()
    {
        QVERIFY(recordClosedSnap(QStringLiteral("app|closed")));
        // Positive control: another activity disabled, the record is served.
        m_resolver.disabledActivity = QStringLiteral("act-Z");
        QVERIFY(m_wta->getPendingRestoreGeometries().contains(QStringLiteral("\"app\"")));

        m_resolver.disabledActivity = kActivity;
        QCOMPARE(m_wta->getPendingRestoreGeometries(), QStringLiteral("{}"));
    }

    // The snap engine's restore predicate refuses an opener when the activity
    // in view is disabled.
    void restorePredicateRefusesADisabledActivity()
    {
        // Positive control: another activity disabled, the record restores.
        m_resolver.disabledActivity = QStringLiteral("act-Z");
        QVERIFY(recordClosedSnap(QStringLiteral("app|closed-1")));
        QVERIFY(m_snapEngine->resolveWindowRestore(QStringLiteral("app|open-1"), kScreen, false).shouldSnap);

        m_resolver.disabledActivity = kActivity;
        QVERIFY(recordClosedSnap(QStringLiteral("app|closed-2")));
        QVERIFY(!m_snapEngine->resolveWindowRestore(QStringLiteral("app|open-2"), kScreen, false).shouldSnap);
    }

private:
    /// A snapped placement record of a closed "app" window in zone 1 of the
    /// active layout on the fixture screen.
    bool recordClosedSnap(const QString& windowId)
    {
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = windowId;
        rec.appId = QStringLiteral("app");
        rec.screenId = kScreen;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{m_zoneId};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        return m_wta->service()->placementStore().record(rec);
    }

    const QString kScreen = QStringLiteral("DP-1");
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorWorkspaces::VirtualDesktopManager* m_desktopManager = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
    SnapEngine* m_snapEngine = nullptr;
    ActivityDisablingResolver m_resolver;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QString m_zoneId;
};

QTEST_MAIN(TestWtaActivityGate)
#include "test_wta_activity_gate.moc"
