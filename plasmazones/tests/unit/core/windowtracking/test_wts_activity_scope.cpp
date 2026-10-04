// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_activity_scope.cpp
 * @brief The snap walks read the context each store is filed under (F127).
 *
 * A snap store is keyed by (screen, desktop, activity), but the walks over
 * them saw only the screen and the window's recorded desktop. A window snapped
 * in another activity therefore counted in the activity in view: it was
 * resnapped into this activity's layout, occupied its zones, seeded its
 * autotile order, and lost its zone when this activity's layout changed. The
 * fixture runs the snap engine's stores through the daemon's resolver, which
 * reports each store's key.
 */

#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>
#include <memory>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorPlacement/WindowTrackingService.h>
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
const QString kScreen = QStringLiteral("DP-1");
const QString kOtherScreen = QStringLiteral("DP-2");
const QString kActivityX = QStringLiteral("act-X");
const QString kActivityY = QStringLiteral("act-Y");
const QString kWindow = QStringLiteral("app|activity-x");
const QString kControl = QStringLiteral("app|activity-y");
} // namespace

class TestWtsActivityScope : public QObject
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
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, m_vdm.get());
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->setCurrentDesktopForScreen(kOtherScreen, 1);
        m_service->setSnapEngine(m_engine);
        m_service->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(m_engine));

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
        m_service->setSnapState(nullptr);
        m_service->setSnapEngine(nullptr);
        delete m_engine;
        m_engine = nullptr;
        delete m_service;
        m_service = nullptr;
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

    // A layout switch in activity Y leaves activity X's window out of the
    // resnap buffer: its zone belongs to the layout X runs.
    void populateLeavesAnotherActivitysWindow()
    {
        snapBoth();
        m_service->populateResnapBufferForAllScreens({}, {kScreen}, 1);
        QSet<QString> ids;
        for (const PhosphorEngine::ResnapEntry& e : m_service->takeResnapBuffer()) {
            ids.insert(e.windowId);
        }
        QVERIFY(ids.contains(kControl));
        QVERIFY2(!ids.contains(kWindow), "a window snapped in another activity must not be resnapped here");
    }

    // Another activity's window does not occupy this activity's zones (F311).
    void occupancyIgnoresAnotherActivity()
    {
        snapBoth();
        const QSet<QUuid> occupied = m_service->buildOccupiedZoneSet(kScreen, 1);
        QVERIFY(occupied.contains(QUuid::fromString(m_zoneIds[1])));
        QVERIFY2(!occupied.contains(QUuid::fromString(m_zoneIds[0])), "zone 1 is occupied only in activity X");
    }

    // ...and does not enter this activity's autotile seed (F139).
    void seedIgnoresAnotherActivity()
    {
        snapBoth();
        const QStringList seed = m_service->buildZoneOrderedWindowList(kScreen);
        QVERIFY(seed.contains(kControl));
        QVERIFY2(!seed.contains(kWindow), "a window snapped in another activity must not seed this one");
    }

    // A layout change in activity Y keeps the zone activity X's window holds
    // there, as a layout change on one desktop keeps another desktop's.
    void layoutChangeKeepsAnotherActivitysZone()
    {
        snapBoth();
        PhosphorZones::Layout* other = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(other);
        const QString otherId = other->id().toString();
        m_layoutManager->setDefaultLayoutIdProvider([otherId]() {
            return otherId;
        });
        m_layoutManager->setActiveLayout(other);
        m_service->onLayoutChanged();

        QVERIFY2(m_service->zonesForWindow(kControl).isEmpty(), "the window in view loses the zone its layout lost");
        switchTo(kActivityX);
        QCOMPARE(m_service->zonesForWindow(kWindow), QStringList{m_zoneIds[0]});
        for (SnapState* state : m_engine->allSnapStates()) {
            const auto key = m_engine->keyForState(state);
            QVERIFY2(!(key && key->activity == kActivityY && !state->zonesForWindow(kWindow).isEmpty()),
                     "no store of activity Y holds the window");
        }
    }

    // The engine's own walks skip another activity's store too.
    void currentAssignmentsSkipAnotherActivity()
    {
        snapBoth();
        for (const PhosphorEngine::ZoneAssignmentEntry& e : m_engine->calculateResnapFromCurrentAssignments(kScreen)) {
            QVERIFY2(e.windowId != kWindow, "the current-assignments resnap carries another activity's window");
        }
    }

    void rotationSkipsAnotherActivity()
    {
        snapBoth();
        for (const PhosphorEngine::ZoneAssignmentEntry& e : m_engine->calculateRotation(true, kScreen)) {
            QVERIFY2(e.windowId != kWindow, "the rotation carries another activity's window");
        }
    }

    // A sticky window (recorded desktop 0) still counts on every desktop
    // under the keyed walk (F565): its store key names the desktop it was
    // filed on, and the walk keeps its "every desktop" meaning.
    void walkKeepsStickyAcrossDesktops()
    {
        switchTo(kActivityX);
        m_service->assignWindowToZone(kWindow, m_zoneIds[0], kScreen, 0);
        QVERIFY(m_service->buildOccupiedZoneSet(kScreen, 2).contains(QUuid::fromString(m_zoneIds[0])));
    }

    // The autotile seed holds exactly the windows snapped on the screen's
    // desktop in view, plus the sticky one, ordered by zone number (F802).
    void seedOrdersByZoneNumberOnTheDesktopInView()
    {
        switchTo(kActivityX);
        const QString inZone2 = QStringLiteral("app|seed-z2");
        const QString inZone1 = QStringLiteral("app|seed-z1");
        const QString otherDesktop = QStringLiteral("app|seed-d2");
        const QString otherScreen = QStringLiteral("app|seed-other-screen");
        const QString sticky = QStringLiteral("app|seed-sticky");
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_service->assignWindowToZone(otherDesktop, m_zoneIds[0], kScreen, 2);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_service->assignWindowToZone(inZone2, m_zoneIds[1], kScreen, 1);
        m_service->assignWindowToZone(inZone1, m_zoneIds[0], kScreen, 1);
        m_service->assignWindowToZone(otherScreen, m_zoneIds[0], kOtherScreen, 1);
        m_service->assignWindowToZone(sticky, m_zoneIds[2], kScreen, 0);

        const QStringList seed = m_service->buildZoneOrderedWindowList(kScreen);
        QCOMPARE(QSet<QString>(seed.begin(), seed.end()), (QSet<QString>{inZone1, inZone2, sticky}));
        QVERIFY(seed.indexOf(inZone1) < seed.indexOf(inZone2));
    }

private:
    /// kWindow snapped in zone 1 while activity X is current, kControl in zone
    /// 2 while Y is, and Y left current.
    void snapBoth()
    {
        switchTo(kActivityX);
        m_service->assignWindowToZone(kWindow, m_zoneIds[0], kScreen, 1);
        switchTo(kActivityY);
        m_service->assignWindowToZone(kControl, m_zoneIds[1], kScreen, 1);
    }

    void switchTo(const QString& activity)
    {
        m_engine->setCurrentActivity(activity);
        m_layoutManager->setCurrentActivity(activity);
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    std::unique_ptr<PhosphorWorkspaces::VirtualDesktopManager> m_vdm;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsActivityScope)
#include "test_wts_activity_scope.moc"
