// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_desktop_context.cpp
 * @brief recordedSnapZones answers for the desktop the window's screen shows (F964).
 *
 * The fixture runs the snap engine's per-desktop stores through the daemon's
 * resolver, with a VirtualDesktopManager showing desktop 1 on the screen and a
 * WindowRegistry carrying each window's desktops. A window held only in
 * another desktop's store used to answer that store's zone whenever it held
 * no other, which is right for a window that lives on that desktop and wrong
 * for one the registry places on the desktop in view: a sticky window snapped
 * only on desktop 2 took desktop 2's zone on desktop 1 on a return to snapping.
 */

#include <QString>
#include <QStringList>
#include <QTest>
#include <memory>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/SnapStateResolver.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;
using PhosphorSnapEngine::SnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kScreen = QStringLiteral("DP-1");
} // namespace

class TestWtsDesktopContext : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_vdm = std::make_unique<PhosphorWorkspaces::VirtualDesktopManager>(nullptr);
        m_vdm->updateScreenDesktop(kScreen, 1);
        m_registry = std::make_unique<PhosphorEngine::WindowRegistry>(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, m_vdm.get());
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_service->setSnapEngine(m_engine);
        m_service->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(m_engine));
        m_service->setWindowRegistry(m_registry.get());

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);
        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
    }

    void cleanup()
    {
        m_service->setWindowRegistry(nullptr);
        m_service->setSnapState(nullptr);
        m_service->setSnapEngine(nullptr);
        delete m_engine;
        m_engine = nullptr;
        delete m_service;
        m_service = nullptr;
        m_registry.reset();
        m_vdm.reset();
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_testLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
    }

    // A sticky window snapped only on desktop 2, with no record of a zone for
    // desktop 1, has no zone on desktop 1.
    void recordedSnapZones_stickySnappedOnlyElsewhereAnswersNothing()
    {
        const QString w = announce(QStringLiteral("sticky"), /*sticky=*/true, 0);
        snapOnDesktopTwo(w, m_zoneIds[1]);
        QVERIFY2(m_service->recordedSnapZones(w).isEmpty(), "desktop 2's zone must not answer on desktop 1");
    }

    // ...and with a record naming a zone per desktop, desktop 1's answers.
    void recordedSnapZones_answersTheRecordsZoneForTheDesktopInView()
    {
        const QString w = announce(QStringLiteral("sticky-rec"), /*sticky=*/true, 0);
        snapOnDesktopTwo(w, m_zoneIds[1]);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = w;
        rec.appId = QStringLiteral("app");
        rec.screenId = kScreen;
        rec.virtualDesktop = 2;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{m_zoneIds[1]};
        slot.zonesByDesktop = {{1, {m_zoneIds[0]}}, {2, {m_zoneIds[1]}}};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        QVERIFY(m_service->placementStore().record(rec));

        QCOMPARE(m_service->recordedSnapZones(w), QStringList{m_zoneIds[0]});
    }

    // A window that lives on desktop 2 alone keeps its zone there while the
    // screen shows desktop 1.
    void recordedSnapZones_singleDesktopWindowElsewhereKeepsItsZone()
    {
        const QString w = announce(QStringLiteral("on-two"), /*sticky=*/false, 2);
        snapOnDesktopTwo(w, m_zoneIds[2]);
        QCOMPARE(m_service->recordedSnapZones(w), QStringList{m_zoneIds[2]});
    }

private:
    /// Register @p instance with the registry and answer its composite id.
    QString announce(const QString& instance, bool sticky, int desktop)
    {
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.virtualDesktop = desktop;
        meta.isSticky = sticky;
        m_registry->upsert(instance, meta);
        return QStringLiteral("app|") + instance;
    }

    /// Snap @p windowId into @p zoneId in desktop 2's store, then show desktop 1.
    void snapOnDesktopTwo(const QString& windowId, const QString& zoneId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_service->assignWindowToZone(windowId, zoneId, kScreen, 2);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QCOMPARE(m_service->zonesForWindow(windowId), QStringList{zoneId});
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    std::unique_ptr<PhosphorWorkspaces::VirtualDesktopManager> m_vdm;
    std::unique_ptr<PhosphorEngine::WindowRegistry> m_registry;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsDesktopContext)
#include "test_wts_desktop_context.moc"
