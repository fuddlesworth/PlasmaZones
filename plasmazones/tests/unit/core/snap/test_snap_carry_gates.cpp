// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_carry_gates.cpp
 * @brief The snap engine's system placements obey what the user switched off
 *        (F445): the desktop carry, the membership re-apply, the
 *        layout-switch resnap and the rotation place nothing with snapping
 *        off, in a context the user disabled, on a desktop running no layout,
 *        or for a window snapping leaves alone, and a floating membership is
 *        not carried into a zone. And the re-apply puts back
 *        only zones the layout in view still holds (F982). A batch or span
 *        with an empty zone commits nothing (F459). A restore pinned to one
 *        desktop leaves the window's other desktops alone (F477), and a float
 *        parked before the window had a store ends with the unfloat (F331).
 *        A window snapping leaves alone gets no remembered zone back on an
 *        adoption, no re-apply or statement on a switch, and no owed
 *        float-back.
 */

#include <QHash>
#include <QRect>
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
#include <PhosphorSnapEngine/INavigationStateProvider.h>
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

/// The compositor's frames, as the effect reports them. Outside the anonymous
/// namespace: the test class holds one as a member, and a member of an
/// internal-linkage type is -Wsubobject-linkage.
class GateFrameNav : public PhosphorSnapEngine::INavigationStateProvider
{
public:
    QString lastCursorScreenName() const override
    {
        return kScreen;
    }
    QString lastActiveScreenName() const override
    {
        return kScreen;
    }
    QString lastActiveWindowId() const override
    {
        return {};
    }
    QRect frameGeometry(const QString& windowId) const override
    {
        return m_frames.value(windowId);
    }

    QHash<QString, QRect> m_frames;
};

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
        m_engine->setNavigationStateProvider(nullptr);
        m_nav.m_frames.clear();
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

    // A membership floating with its zone still held is not carried into a
    // zone: the window sits at its own free geometry, so there is no foreign
    // rect to correct, and it arrives floating. Only the float bit is set here.
    void aFloatingMembershipStillHoldingItsZoneIsNotCarried()
    {
        snapOn(1, m_zoneIds[1]);
        static_cast<SnapState*>(m_engine->stateForScreen(kScreen))->setFloating(kWindow, true);
        QCOMPARE(zonesOn(1), QStringList{m_zoneIds[1]});
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QVERIFY2(m_batch.isEmpty(), "a floating window is not carried into a zone");
        QVERIFY(zonesOn(2).isEmpty());
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

    // Rotation leaves a screen snapping does not run, a context the user
    // disabled and everything with snapping off alone (F367).
    void rotationSkipsATilingScreen()
    {
        snapTwo();
        m_engine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Autotile;
        });
        QVERIFY(m_engine->calculateRotation(true, QString()).isEmpty());
        m_engine->setLiveModeResolver({});
    }

    void rotationSkipsADisabledContext()
    {
        snapTwo();
        m_engine->setShouldRestorePredicate([](const QString&, int) {
            return false;
        });
        QVERIFY(m_engine->calculateRotation(true, QString()).isEmpty());
        m_engine->setShouldRestorePredicate({});
    }

    void rotationWithSnappingOffIsEmpty()
    {
        snapTwo();
        m_settings->setSnappingEnabled(false);
        QVERIFY(m_engine->calculateRotation(true, QString()).isEmpty());
    }

    // What the rotate verb reports, and the batch it sends (F845).
    void rotateReportsSingleZone()
    {
        PhosphorZones::Layout* one = createTestLayout(1, m_layoutManager);
        m_layoutManager->addLayout(one);
        m_layoutManager->assignLayout(kScreen, 1, QString(), one);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QSignalSpy feedback(m_engine, &SnapEngine::navigationFeedback);
        m_engine->rotateWindowsInLayout(true, kScreen);
        QCOMPARE(feedback.count(), 1);
        QCOMPARE(feedback.first().at(2).toString(), QStringLiteral("single_zone"));
    }

    void rotateReportsNoSnappedWindows()
    {
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QSignalSpy feedback(m_engine, &SnapEngine::navigationFeedback);
        m_engine->rotateWindowsInLayout(true, kScreen);
        QCOMPARE(feedback.count(), 1);
        QCOMPARE(feedback.first().at(2).toString(), QStringLiteral("no_snapped_windows"));
    }

    void rotateSendsARotateBatch()
    {
        snapTwo();
        QSignalSpy batches(m_engine, &SnapEngine::applyGeometriesBatch);
        QSignalSpy feedback(m_engine, &SnapEngine::navigationFeedback);
        m_engine->rotateWindowsInLayout(true, kScreen);
        QCOMPARE(batches.count(), 1);
        QCOMPARE(batches.first().at(1).toString(), QStringLiteral("rotate"));
        QCOMPARE(feedback.count(), 1);
        QCOMPARE(feedback.first().at(2).toString(), QStringLiteral("clockwise:2"));
        QCOMPARE(zonesOn(1), QStringList{m_zoneIds[1]});
    }

    void rotateRefusesANamedTilingScreen()
    {
        snapTwo();
        m_engine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Autotile;
        });
        QSignalSpy batches(m_engine, &SnapEngine::applyGeometriesBatch);
        m_engine->rotateWindowsInLayout(true, kScreen);
        QCOMPARE(batches.count(), 0);
        m_engine->setLiveModeResolver({});
        QCOMPARE(zonesOn(1), QStringList{m_zoneIds[0]});
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

    // A restore pinned to desktop 2 floats the window there and leaves the
    // zones it holds on desktops 1 and 3 and its float-back alone (F477).
    void aPinnedRestoreKeepsTheWindowsOtherDesktopZones()
    {
        recordFreeGeometry(QRect(100, 100, 600, 400));
        snapOn(1, m_zoneIds[0]);
        snapOn(3, m_zoneIds[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        PhosphorEngine::ZoneAssignmentEntry restore;
        restore.windowId = kWindow;
        restore.targetZoneId = PhosphorEngine::RestoreSentinel;
        restore.targetGeometry = QRect(100, 100, 600, 400);
        restore.targetScreenId = kScreen;
        restore.virtualDesktop = 2;

        m_engine->applyBatchAssignments({restore});

        QCOMPARE(zonesOn(1), QStringList{m_zoneIds[0]});
        QCOMPARE(zonesOn(3), QStringList{m_zoneIds[1]});
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QVERIFY(static_cast<SnapState*>(m_engine->stateForScreen(kScreen))->isFloating(kWindow));
        const auto rec = m_service->placementStore().peekExact(kWindow);
        QVERIFY(rec.has_value());
        QVERIFY2(rec->freeGeometryByScreen.value(kScreen).isValid(), "the float-back stays the free spot there");
    }

    // A float set before the window had a store parks in the global holder.
    // A restore that grants the window a store without the eviction keeps
    // that bit readable, and the unfloat must clear it (F331).
    void aParkedFloatClearsWithTheUnfloat()
    {
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->setFloatingWithResidence(kWindow, kScreen);
        PhosphorEngine::EngineSlot slot;
        slot.zonesByDesktop.insert(2, {m_destination->zones().at(0)->id().toString()});
        m_engine->seedPersistedDesktopZones(kWindow, slot, kScreen, 1, QString());
        QVERIFY(m_engine->isWindowTracked(kWindow));
        QVERIFY(m_engine->isFloating(kWindow));

        m_engine->setFloating(kWindow, false);

        QVERIFY2(!m_engine->isFloating(kWindow), "the unfloat clears the parked bit");
    }

    // A window snapping leaves alone gets no remembered zone back when it is
    // adopted into a desktop, as the carry refuses it one.
    void adoptionRestoresNoRememberedZoneForAnExcludedWindow()
    {
        const QString remembered = m_destination->zones().at(1)->id().toString();
        snapOn(1, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        m_service->assignWindowToZone(kWindow, remembered, kScreen, 2);
        const auto captured = m_engine->capturePlacement(kWindow);
        QVERIFY(captured.has_value());
        QCOMPARE(captured->slotFor(WindowPlacement::snapEngineId()).zonesByDesktop.value(2), QStringList{remembered});
        m_service->placementStore().record(*captured);
        m_engine->forgetWindow(kWindow); // the handoff dropped every context

        snapOn(1, m_zoneIds[0]);
        excludeApp(QStringLiteral("app"));
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const auto result = m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));

        QCOMPARE(result.adopted.size(), 1);
        QVERIFY2(zonesOn(2).isEmpty(), "an excluded window gets no remembered zone back");
    }

    // ...nor, on a desktop switch, the re-apply of its zone or a statement of
    // the state the entered desktop holds for it: neither the unsnap of a
    // desktop that holds nothing for it nor the float of one where it floats.
    void anExcludedWindowIsNeitherReappliedNorStatedOnASwitch()
    {
        snapOn(1, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        excludeApp(QStringLiteral("app"));
        m_batch.clear();
        QSignalSpy states(m_engine, &SnapEngine::windowSnapStateChanged);
        QSignalSpy floats(m_engine, &SnapEngine::windowFloatingChanged);
        const auto switchAwayAndBack = [this] {
            for (const int desktop : {1, 2}) {
                m_engine->setCurrentDesktopForScreen(kScreen, desktop);
                m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
            }
        };

        switchAwayAndBack();
        QVERIFY2(m_batch.isEmpty(), "an excluded window is not re-applied");
        QCOMPARE(states.count(), 0);

        m_engine->stateForWindowOnScreen(kWindow, kScreen, 2)->setFloatingOnScreen(kWindow, kScreen, 2);
        switchAwayAndBack();
        QCOMPARE(floats.count(), 0);
    }

    // ...nor the float-back a carry left owed for a hidden desktop's first
    // showing, when snapping has come to leave it alone since.
    void anExcludedWindowIsOwedNoFloatBack()
    {
        snapOn(1, m_zoneIds[0]);
        m_engine->stateForWindowOnScreen(kWindow, kScreen, 2)->setFloatingOnScreen(kWindow, kScreen, 2);
        recordFreeGeometry(QRect(100, 100, 600, 400));
        m_nav.m_frames.insert(kWindow, m_service->resolveZoneGeometry({m_zoneIds[0]}, kScreen));
        m_engine->setNavigationStateProvider(&m_nav);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QVERIFY2(m_batch.isEmpty(), "precondition: a kept float on a hidden desktop takes no apply now");
        excludeApp(QStringLiteral("app"));
        QSignalSpy restated(m_engine, &SnapEngine::restatementGeometryRequested);

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({2})));

        QCOMPARE(restated.count(), 0);
    }

private:
    /// kWindow in zone 1 and a second window in zone 2, on desktop 1.
    void snapTwo()
    {
        snapOn(1, m_zoneIds[0]);
        m_service->assignWindowToZone(QStringLiteral("other|bbbbbbbb-0000-0000-0000-000000000002"), m_zoneIds[1],
                                      kScreen, 1);
    }

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
    GateFrameNav m_nav;
};

QTEST_MAIN(TestSnapCarryGates)
#include "test_snap_carry_gates.moc"
