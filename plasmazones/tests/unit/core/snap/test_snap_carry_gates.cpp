// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_carry_gates.cpp
 * @brief The snap engine's system placements obey what the user switched off
 *        (F445): the desktop carry, the membership re-apply, the
 *        layout-switch resnap and the rotation place nothing with snapping
 *        off, in a context the user disabled, on a desktop running no layout,
 *        or for a window snapping leaves alone. And the re-apply puts back
 *        only zones the layout in view still holds (F982). A batch or span
 *        with an empty zone commits nothing (F459).
 */

#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUuid>
#include <memory>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"
#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>
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
const QString kWindow = QStringLiteral("app|aaaaaaaa-0000-0000-0000-000000000001");

PhosphorEngine::DesktopSpan on(QSet<int> desktops)
{
    PhosphorEngine::DesktopSpan span;
    span.known = true;
    span.desktops = std::move(desktops);
    return span;
}

PhosphorEngine::DesktopSpan sticky()
{
    PhosphorEngine::DesktopSpan span;
    span.known = true;
    span.sticky = true;
    return span;
}

PhosphorEngine::DesktopSpanQuery spanOf(const PhosphorEngine::DesktopSpan& windowSpan)
{
    return [windowSpan](const QString& windowId) {
        return windowId == kWindow ? windowSpan : on({1});
    };
}
} // namespace

class TestSnapCarryGates : public QObject
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
        m_service->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(m_engine));

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);
        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
        // Desktop 2 runs a layout of its own, so a carry has somewhere to go.
        m_destination = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_destination);
        m_layoutManager->assignLayout(kScreen, 2, QString(), m_destination);
        QObject::connect(m_engine, &SnapEngine::resnapToNewLayoutRequested, m_engine, [this](const QString& payload) {
            const auto entries = PhosphorEngine::GeometryUtils::deserializeZoneAssignments(payload, nullptr);
            m_batch += entries;
            m_engine->applyBatchAssignments(entries);
        });
    }

    void cleanup()
    {
        m_engine->setExcludeRuleSet(nullptr);
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
        m_batch.clear();
        m_excludeSet.reset();
        m_guard.reset();
    }

    // Control: the carry these rows gate does run with nothing switched off.
    void carryRunsWhenNothingGatesIt()
    {
        snapOn(1, m_zoneIds[1]);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QCOMPARE(m_batch.size(), 1);
    }

    void snappingOffCarriesNothing()
    {
        snapOn(1, m_zoneIds[1]);
        m_settings->setSnappingEnabled(false);
        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QVERIFY2(m_batch.isEmpty(), "snapping off places nothing");
        QCOMPARE(result.released.size(), 1);
        QVERIFY(zonesOn(1).isEmpty());
    }

    void disabledDestinationCarriesNothing()
    {
        snapOn(1, m_zoneIds[1]);
        m_engine->setShouldRestorePredicate([](const QString&, int desktop) {
            return desktop != 2;
        });
        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QVERIFY2(m_batch.isEmpty(), "a desktop the user disabled snapping on takes nothing");
        QCOMPARE(result.released.size(), 1);
        m_engine->setShouldRestorePredicate({});
    }

    void excludedWindowCarriesNothing()
    {
        snapOn(1, m_zoneIds[1]);
        excludeApp(QStringLiteral("app"));
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QVERIFY2(m_batch.isEmpty(), "a window snapping leaves alone is not carried");
    }

    // A destination running no layout is skipped like one another mode
    // owns: the float-back it took instead moved the window for nothing.
    void noneDestinationTakesNoFloatBack()
    {
        snapOn(1, m_zoneIds[1]);
        recordFreeGeometry(QRect(100, 100, 600, 400));
        PhosphorZones::AssignmentEntry none;
        none.mode = PhosphorZones::AssignmentEntry::Snapping;
        none.snappingLayout = PhosphorZones::NoSnappingLayout;
        m_layoutManager->setAssignmentEntryDirect(kScreen, 2, QString(), none);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QVERIFY2(m_batch.isEmpty(), "no restore sentinel for a desktop with no layout");
    }

    // The membership re-apply on a desktop switch: control, then with
    // snapping switched off.
    void reapplyArmHonoursTheMasterSwitch()
    {
        holdOnBothDesktops();
        m_batch.clear();
        m_settings->setSnappingEnabled(false);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QVERIFY2(m_batch.isEmpty(), "snapping off re-applies nothing");

        m_settings->setSnappingEnabled(true);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QVERIFY2(!m_batch.isEmpty(), "control: with snapping on the switch re-applies the zone");
    }

    // The re-apply puts back only a zone the layout in view holds now: the
    // desktop's layout changed while it was hidden (F982).
    void reapplySkipsAZoneTheLayoutNoLongerHolds()
    {
        holdOnBothDesktops();
        // While desktop 2 is in view, desktop 1 switches to a layout without
        // the window's zone there.
        PhosphorZones::Layout* other = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(other);
        m_layoutManager->assignLayout(kScreen, 1, QString(), other);
        m_batch.clear();
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        for (const auto& entry : std::as_const(m_batch)) {
            QVERIFY2(entry.targetZoneId != m_zoneIds[0], "a zone the desktop's layout lacks must not be re-applied");
        }
    }

    void rotationSkipsAnExcludedWindow()
    {
        const QString other = QStringLiteral("other|bbbbbbbb-0000-0000-0000-000000000002");
        snapOn(1, m_zoneIds[0]);
        m_service->assignWindowToZone(other, m_zoneIds[1], kScreen, 1);
        excludeApp(QStringLiteral("app"));
        for (const auto& entry : m_engine->calculateRotation(true, kScreen)) {
            QVERIFY2(entry.windowId != kWindow, "an excluded window is not rotated");
        }
    }

    // A batch entry whose span holds an empty member commits nothing and sends
    // no geometry, so the effect is not handed a placement the daemon
    // never recorded (F459).
    void batchSendsNoGeometryForARefusedEntry()
    {
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        PhosphorEngine::ZoneAssignmentEntry entry;
        entry.windowId = kWindow;
        entry.targetZoneId = m_zoneIds[0];
        entry.targetZoneIds = {m_zoneIds[0], QString()};
        entry.targetGeometry = QRect(0, 0, 100, 100);
        entry.targetScreenId = kScreen;
        const PhosphorProtocol::WindowGeometryList geometries = m_engine->applyBatchAssignments({entry});
        QVERIFY(geometries.isEmpty());
        QVERIFY(zonesOn(1).isEmpty());
    }

    void multiZoneCommitRefusesAnEmptyMember()
    {
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QSignalSpy changed(m_engine, &SnapEngine::windowSnapStateChanged);
        m_engine->commitMultiZoneSnap(kWindow, {m_zoneIds[0], QString()}, kScreen);
        QCOMPARE(changed.count(), 0);
        QVERIFY(zonesOn(1).isEmpty());
    }

private:
    void snapOn(int desktop, const QString& zoneId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        m_service->assignWindowToZone(kWindow, zoneId, kScreen, desktop);
    }

    QStringList zonesOn(int desktop)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        SnapState* state = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        return state ? state->zonesForWindow(kWindow) : QStringList{};
    }

    /// kWindow on every desktop, holding zone 1 on desktop 1 and a zone of
    /// desktop 2's layout there; desktop 2 in view.
    void holdOnBothDesktops()
    {
        snapOn(1, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        m_service->assignWindowToZone(kWindow, m_destination->zones().at(1)->id().toString(), kScreen, 2);
    }

    void excludeApp(const QString& appId)
    {
        m_excludeSet = std::make_unique<PhosphorRules::RuleSet>();
        PhosphorRules::Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("exclude");
        rule.enabled = true;
        rule.match = PhosphorRules::MatchExpression::makeLeaf(PhosphorRules::Field::AppId,
                                                              PhosphorRules::Operator::AppIdMatches, appId);
        PhosphorRules::RuleAction action;
        action.type = QString(PhosphorRules::ActionType::Exclude);
        rule.actions.append(action);
        QVERIFY(m_excludeSet->addRule(rule));
        m_engine->setExcludeRuleSet(m_excludeSet.get());
    }

    void recordFreeGeometry(const QRect& rect)
    {
        WindowPlacement rec;
        rec.windowId = kWindow;
        rec.appId = QStringLiteral("app");
        rec.screenId = kScreen;
        rec.virtualDesktop = 1;
        rec.freeGeometryByScreen.insert(kScreen, rect);
        m_service->placementStore().record(rec);
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    PhosphorZones::Layout* m_destination = nullptr;
    QStringList m_zoneIds;
    QVector<PhosphorEngine::ZoneAssignmentEntry> m_batch;
    std::unique_ptr<PhosphorRules::RuleSet> m_excludeSet;
};

QTEST_MAIN(TestSnapCarryGates)
#include "test_snap_carry_gates.moc"
