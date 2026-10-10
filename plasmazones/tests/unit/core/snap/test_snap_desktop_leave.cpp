// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_desktop_leave.cpp
 * @brief What a window leaving a desktop or activity takes with it: the
 *        record's snap slot when the membership pass releases it onto a
 *        desktop snap does not run, and the desktop and activity prunes of a
 *        window still open there. Same fixture shape as
 *        test_snap_desktop_membership.
 */

#include <QGuiApplication>
#include <QSet>
#include <QSignalSpy>
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
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;
using PhosphorEngine::WindowPlacement;
using PhosphorSnapEngine::SnapEngine;
using PhosphorSnapEngine::SnapState;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kScreen = QStringLiteral("DP-1");
const QString kInstance = QStringLiteral("11111111-2222-3333-4444-555555555555");
const QString kWindow = QStringLiteral("app|") + kInstance;
const QString kClosed = QStringLiteral("app|66666666-7777-8888-9999-000000000000");

PhosphorEngine::DesktopSpanQuery onDesktops(QSet<int> desktops)
{
    return [desktops](const QString&) {
        PhosphorEngine::DesktopSpan span;
        span.known = true;
        span.desktops = desktops;
        return span;
    };
}
} // namespace

class TestSnapDesktopLeave : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        m_service->setSnapEngine(m_engine);
        installFullResolver();

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
        m_engine->setWindowRegistry(nullptr);
        m_service->setSnapState(nullptr);
        m_service->setSnapEngine(nullptr);
        delete m_engine;
        m_engine = nullptr;
        delete m_service;
        m_service = nullptr;
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

    // A window snapped on desktop 1 that KWin moves to a desktop running
    // autotile is carried nowhere: its record's snap slot goes with the
    // membership, or a later snapping pass on desktop 2 would read desktop
    // 1's zone back as that desktop's (F624).
    void aMoveOntoATilingDesktopReleasesTheSnapSlot()
    {
        PhosphorZones::AssignmentEntry tiling;
        tiling.mode = PhosphorZones::AssignmentEntry::Mode::Autotile;
        tiling.tilingAlgorithm = QStringLiteral("bsp");
        m_layoutManager->setAssignmentEntryDirect(kScreen, 2, QString(), tiling);
        snapOn(1, kWindow, m_zoneIds[0]);
        QCOMPARE(slotState(kWindow), QString(WindowPlacement::stateSnapped()));

        const auto result = m_engine->reconcileWindowMemberships(kWindow, onDesktops({2}));

        QCOMPARE(result.released.size(), 1);
        QCOMPARE(slotState(kWindow), QString(WindowPlacement::stateReleased()));
    }

    // A window carried into a zone on the snapping desktop it moved to keeps
    // its slot: snap still places it.
    void aCarriedWindowKeepsTheSnapSlot()
    {
        snapOn(1, kWindow, m_zoneIds[0]);

        m_engine->reconcileWindowMemberships(kWindow, onDesktops({2}));

        QCOMPARE(slotState(kWindow), QString(WindowPlacement::stateSnapped()));
    }

    // A removed desktop takes the snap memory of a window still open on it:
    // the slot is released and the window is told it is unsnapped. A record
    // of a window no longer open is left to the store's own history (F122).
    void aDesktopPruneReleasesAWindowStillOpenThere()
    {
        PhosphorEngine::WindowRegistry registry;
        registry.canonicalizeWindowId(kWindow);
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.virtualDesktop = 2;
        registry.upsert(kInstance, meta);
        m_engine->setWindowRegistry(&registry);
        snapOn(2, kWindow, m_zoneIds[0]);
        snapOn(2, kClosed, m_zoneIds[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);

        QSignalSpy stateSpy(m_engine, &SnapEngine::windowSnapStateChanged);
        m_engine->pruneStatesForDesktop(2);

        QCOMPARE(slotState(kWindow), QString(WindowPlacement::stateReleased()));
        QCOMPARE(stateSpy.count(), 1);
        const auto entry = stateSpy.first().at(1).value<PhosphorProtocol::WindowStateEntry>();
        QCOMPARE(entry.windowId, kWindow);
        QCOMPARE(entry.changeType, QStringLiteral("unsnapped"));
        QCOMPARE(slotState(kClosed), QString(WindowPlacement::stateSnapped()));
    }

    // An empty activity list is the activity service going away, not every
    // activity removed: the stores stay. A real removal releases what a
    // window still open there held (F423, F122).
    void anActivityPruneKeepsStoresOnAnEmptyListAndReleasesOnARemoval()
    {
        PhosphorEngine::WindowRegistry registry;
        registry.canonicalizeWindowId(kWindow);
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.virtualDesktop = 1;
        meta.activity = QStringLiteral("act-a");
        registry.upsert(kInstance, meta);
        m_engine->setWindowRegistry(&registry);
        // Both readers of the activity, as the daemon sets them: a pinned
        // store key takes the layout registry's, the view the context's.
        m_layoutManager->setCurrentActivity(QStringLiteral("act-a"));
        m_engine->setCurrentActivity(QStringLiteral("act-a"));
        snapOn(1, kWindow, m_zoneIds[0]);
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});

        m_engine->pruneStatesForActivities({});
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        QCOMPARE(slotState(kWindow), QString(WindowPlacement::stateSnapped()));

        QSignalSpy stateSpy(m_engine, &SnapEngine::windowSnapStateChanged);
        m_engine->pruneStatesForActivities({QStringLiteral("act-b")});
        QVERIFY(zonesOn(1, kWindow).isEmpty());
        QCOMPARE(slotState(kWindow), QString(WindowPlacement::stateReleased()));
        QCOMPARE(stateSpy.count(), 1);
        m_engine->setCurrentActivity(QString());
        m_layoutManager->setCurrentActivity(QString());
    }

    // A window on two activities, snapped while the second is in view, is a
    // member there: the reconcile does not read its first activity as the
    // only one and release it (F426).
    void aWindowOnSeveralActivitiesKeepsItsZoneInEach()
    {
        const QString a = QStringLiteral("act-a");
        const QString b = QStringLiteral("act-b");
        m_layoutManager->setCurrentActivity(b);
        m_engine->setCurrentActivity(b);
        snapOn(1, kWindow, m_zoneIds[0]);

        const PhosphorEngine::DesktopSpanQuery onBoth = [a, b](const QString&) {
            PhosphorEngine::DesktopSpan span;
            span.known = true;
            span.desktops = {1};
            span.activity = a;
            span.activities = QStringList{a, b};
            return span;
        };
        const auto result = m_engine->reconcileWindowMemberships(kWindow, onBoth);

        QVERIFY(result.released.isEmpty());
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        QCOMPARE(slotState(kWindow), QString(WindowPlacement::stateSnapped()));
        m_engine->setCurrentActivity(QString());
        m_layoutManager->setCurrentActivity(QString());
    }

    // Removing desktop 2 renumbers 3 to 2: the stores move, and so do the
    // desktop numbers they hold for each window and for the last-used zone,
    // which otherwise named the desktop that took the old number (F145).
    void aDesktopRemovalRenumbersTheDesktopsTheStoresHold()
    {
        snapOn(3, kWindow, m_zoneIds[0]);
        auto* onThree = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        onThree->restoreLastUsedZone(m_zoneIds[0], kScreen, QStringLiteral("app"), 3);
        QCOMPARE(onThree->desktopForWindow(kWindow), 3);

        m_engine->pruneStatesForDesktop(2);
        m_engine->renumberDesktopsAfterRemoval(2);

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        auto* onTwo = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        QCOMPARE(onTwo, onThree);
        QCOMPARE(onTwo->zonesForWindow(kWindow), QStringList{m_zoneIds[0]});
        QCOMPARE(onTwo->desktopForWindow(kWindow), 2);
        QCOMPARE(onTwo->lastUsedDesktop(), 2);
    }

private:
    void installFullResolver()
    {
        m_service->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(m_engine));
    }

    /// Snap @p windowId into @p zoneId on @p desktop of kScreen through the
    /// service, the way a commit does, and record the capture.
    void snapOn(int desktop, const QString& windowId, const QString& zoneId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        m_service->assignWindowToZone(windowId, zoneId, kScreen, desktop);
        if (const auto placement = m_engine->capturePlacement(windowId)) {
            m_service->placementStore().record(*placement);
        }
    }

    /// The zones @p windowId holds on @p desktop's store, read from that store.
    QStringList zonesOn(int desktop, const QString& windowId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        SnapState* state = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        return state ? state->zonesForWindow(windowId) : QStringList{};
    }

    QString slotState(const QString& windowId) const
    {
        const auto rec = m_service->placementStore().peekExact(windowId);
        return rec ? rec->slotFor(WindowPlacement::snapEngineId()).state : QString();
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestSnapDesktopLeave)
#include "test_snap_desktop_leave.moc"
