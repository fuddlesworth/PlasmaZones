// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_snap_navigation.cpp
 * @brief The snap keyboard verbs on two outputs: where a move lands when it
 *        reaches the edge of a layout, and what it commits there.
 */

#include "wta_snap_nav_fixture.h"

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>

#include <QSignalSpy>
#include <QUuid>

#include <functional>
#include <memory>

using PhosphorEngine::NavigationContext;
using PhosphorSnapEngine::SnapEngine;

class TestWtaSnapNavigation : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A move off the right edge of DP-1 enters DP-2's layout and commits
    // there, with no membership left behind on DP-1 (F841).
    void moveIntoTheNeighbourOutputCommitsThere()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("nav-1"), QRect(1300, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft);
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        f.focus(w, kLeft);
        QSignalSpy applies(f.snap.get(), &SnapEngine::applyGeometryRequested);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(f.snap->screenForTrackedWindow(w), kRight);
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(6).toString(), kRight);
        const PhosphorSnapEngine::SnapState* left =
            static_cast<const PhosphorSnapEngine::SnapState*>(f.snap->stateForScreen(kLeft));
        QVERIFY(!left || !f.snap->holdsWindowInState(w, left));
    }

    // A tiling neighbour output takes the window before the next desktop is
    // tried (F841).
    void tilingNeighbourOutputIsTriedBeforeTheDesktop()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("nav-2"), QRect(1300, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft);
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        f.cross.desktopCount = 2;
        f.setMode(kRight, 1, PhosphorZones::AssignmentEntry::Autotile);
        f.focus(w, kLeft);
        QSignalSpy crossMode(f.snap.get(), &PhosphorEngine::PlacementEngineBase::crossModeMoveRequested);
        QSignalSpy desktopMove(f.snap.get(), &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(crossMode.count(), 1);
        QCOMPARE(desktopMove.count(), 0);
    }

    // The minimum-size threshold reads the live frame, not the size the
    // window opened at (F226).
    void minimumSizeReadsTheLiveFrame()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.isMinimized = false;
        meta.width = 100;
        meta.height = 100;
        f.registry.upsert(QStringLiteral("grown-1"), meta);
        const QString w = QStringLiteral("app|grown-1");
        f.registry.canonicalizeWindowId(w);
        f.setFrame(w, QRect(100, 100, 800, 600));
        f.settings.setMinimumWindowWidth(200);
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->moveFocusedToPosition(1, NavigationContext{w, kLeft});
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(0));
        for (const QList<QVariant>& args : std::as_const(feedback)) {
            QVERIFY(args.at(2).toString() != QLatin1String("excluded"));
        }
    }

    // A rule naming the mode excludes on the keyboard path, which stamps it
    // (F881).
    void modeRuleExcludesOnTheKeyboardPath()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("moded-1"));
        f.snapOn(w, {f.zone(0)}, kLeft);
        excludeWhen(
            f,
            PhosphorRules::MatchExpression::makeAll(
                {PhosphorRules::MatchExpression::makeLeaf(PhosphorRules::Field::AppId,
                                                          PhosphorRules::Operator::AppIdMatches, QStringLiteral("app")),
                 PhosphorRules::MatchExpression::makeLeaf(PhosphorRules::Field::Mode, PhosphorRules::Operator::Equals,
                                                          QStringLiteral("snapping"))}));
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(0));
        QVERIFY(!feedback.isEmpty());
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("excluded"));
        f.snap->setExcludeRuleSet(nullptr);
    }

    // A negated leaf on a field the query cannot answer does not exclude
    // every window (F881).
    void negatedUnanswerableLeafDoesNotExcludeEverything()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("negated-1"));
        f.snapOn(w, {f.zone(0)}, kLeft);
        excludeWhen(f,
                    PhosphorRules::MatchExpression::makeNone({PhosphorRules::MatchExpression::makeLeaf(
                        PhosphorRules::Field::IsSnapped, PhosphorRules::Operator::Equals, true)}));
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(1));
        f.snap->setExcludeRuleSet(nullptr);
    }

    // ── L14.3: the keyboard verbs ask the context they land in ──

    // A move onto a monitor where a rule excludes the window is refused
    // (F159).
    void moveOntoAMonitorWhereAnExcludeRuleAppliesIsRefused()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("land-1"), QRect(1300, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft);
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        excludeWhen(f, appAnd(PhosphorRules::Field::ScreenId, kRight));
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        QSignalSpy applies(f.snap.get(), &SnapEngine::applyGeometryRequested);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(applies.count(), 0);
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(2));
        QCOMPARE(f.snap->screenForTrackedWindow(w), kLeft);
        QVERIFY(!feedback.isEmpty());
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("excluded"));
        QCOMPARE(feedback.last().at(5).toString(), kRight);
        f.snap->setExcludeRuleSet(nullptr);
    }

    // A window snapping does not track is judged on the screen it is on
    // (F159).
    void untrackedWindowIsJudgedOnTheScreenItIsOn()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("land-2"), QRect(2400, 100, 400, 300));
        excludeWhen(f, appAnd(PhosphorRules::Field::ScreenId, kRight));
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->moveFocusedInDirection(QStringLiteral("left"), NavigationContext{w, kRight});
        QVERIFY(f.snap->zoneForWindow(w).isEmpty());
        QVERIFY(!feedback.isEmpty());
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("excluded"));
        f.snap->setExcludeRuleSet(nullptr);
    }

    // A swap partner a rule excludes from the zone it would land in refuses
    // the swap (F159).
    void excludedPartnerRefusesTheSwap()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("swap-a"));
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("other");
        meta.isMinimized = false;
        f.registry.upsert(QStringLiteral("swap-b"), meta);
        const QString partner = QStringLiteral("other|swap-b");
        f.registry.canonicalizeWindowId(partner);
        f.snapOn(w, {f.zone(0)}, kLeft);
        f.snapOn(partner, {f.zone(1)}, kLeft);
        excludeWhen(f,
                    PhosphorRules::MatchExpression::makeLeaf(
                        PhosphorRules::Field::AppId, PhosphorRules::Operator::AppIdMatches, QStringLiteral("other")));
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->swapFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(0));
        QCOMPARE(f.snap->zoneForWindow(partner), f.zone(1));
        QVERIFY(!feedback.isEmpty());
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("excluded"));
        f.snap->setExcludeRuleSet(nullptr);
    }

    // A move to a desktop where a rule excludes the window is refused (F159).
    void crossDesktopMoveRefusesAnExcludedDesktop()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("land-3"), QRect(1300, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        f.cross.desktopCount = 2;
        excludeWhen(f, appAnd(PhosphorRules::Field::VirtualDesktop, 2));
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        QSignalSpy desktopMove(f.snap.get(), &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(desktopMove.count(), 0);
        QVERIFY(!feedback.isEmpty());
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("excluded"));
        f.snap->setExcludeRuleSet(nullptr);
    }

    // A move onto a monitor where snapping is off takes the window there
    // unsnapped, at its float-back carried across (F208, decision Q2).
    void moveOntoADisabledMonitorLandsThereUnsnapped()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("off-1"), QRect(1300, 100, 400, 300));
        f.wta->service()->recordFreeGeometry(w, kLeft, QRect(300, 200, 640, 480), true);
        f.snapOn(w, {f.zone(2)}, kLeft);
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        f.snap->setShouldRestorePredicate([](const QString& screen, int) {
            return screen != kRight;
        });
        QSignalSpy states(f.snap.get(), &SnapEngine::windowSnapStateChanged);
        QSignalSpy applies(f.snap.get(), &SnapEngine::applyGeometryRequested);
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QVERIFY(!f.snap->isWindowTracked(w));
        QCOMPARE(states.count(), 1);
        QCOMPARE(states.first().at(1).value<PhosphorProtocol::WindowStateEntry>().changeType,
                 QStringLiteral("unsnapped"));
        QCOMPARE(applies.count(), 1);
        QVERIFY(applies.first().at(5).toString().isEmpty());
        QCOMPARE(applies.first().at(6).toString(), kRight);
        QCOMPARE(QRect(applies.first().at(1).toInt(), applies.first().at(2).toInt(), applies.first().at(3).toInt(),
                       applies.first().at(4).toInt()),
                 QRect(2220, 200, 640, 480));
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("screen:right"));
        f.snap->setShouldRestorePredicate({});
    }

    void disabledMonitorTakesItsOwnFloatBackFirst()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("off-2"), QRect(1300, 100, 400, 300));
        f.wta->service()->recordFreeGeometry(w, kLeft, QRect(300, 200, 640, 480), true);
        f.wta->service()->recordFreeGeometry(w, kRight, QRect(2500, 300, 500, 400), true);
        f.snapOn(w, {f.zone(2)}, kLeft);
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        f.snap->setShouldRestorePredicate([](const QString& screen, int) {
            return screen != kRight;
        });
        QSignalSpy applies(f.snap.get(), &SnapEngine::applyGeometryRequested);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(applies.count(), 1);
        QCOMPARE(QRect(applies.first().at(1).toInt(), applies.first().at(2).toInt(), applies.first().at(3).toInt(),
                       applies.first().at(4).toInt()),
                 QRect(2500, 300, 500, 400));
        f.snap->setShouldRestorePredicate({});
    }

    void disabledMonitorWithNoFloatBackCarriesTheFrame()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("off-3"));
        f.snapOn(w, {f.zone(2)}, kLeft);
        const QRect zoneRect = f.zoneRect(2, kLeft);
        f.setFrame(w, zoneRect);
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        f.snap->setShouldRestorePredicate([](const QString& screen, int) {
            return screen != kRight;
        });
        QSignalSpy applies(f.snap.get(), &SnapEngine::applyGeometryRequested);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(applies.count(), 1);
        QCOMPARE(QRect(applies.first().at(1).toInt(), applies.first().at(2).toInt(), applies.first().at(3).toInt(),
                       applies.first().at(4).toInt()),
                 zoneRect.translated(1920, 0));
        f.snap->setShouldRestorePredicate({});
    }

    // A move to a desktop where snapping is off takes the window there
    // unsnapped: the free apply first, while it is visible, then the move
    // (F208, decision Q2).
    void moveToADisabledDesktopLandsThereUnsnapped()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("off-4"), QRect(1300, 100, 400, 300));
        const QRect free(300, 200, 640, 480);
        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        f.cross.desktopCount = 2;
        f.snap->setShouldRestorePredicate([](const QString&, int desktop) {
            return desktop != 2;
        });
        QStringList order;
        QRect applied;
        QObject::connect(f.snap.get(), &SnapEngine::applyGeometryRequested, f.snap.get(),
                         [&](const QString&, int x, int y, int width, int height, const QString& zoneId) {
                             applied = QRect(x, y, width, height);
                             order.append(zoneId.isEmpty() ? QStringLiteral("free") : QStringLiteral("zone"));
                         });
        QObject::connect(f.snap.get(), &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested, f.snap.get(),
                         [&](const QString&, int desktop) {
                             order.append(QStringLiteral("desktop:%1").arg(desktop));
                         });
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(order, (QStringList{QStringLiteral("free"), QStringLiteral("desktop:2")}));
        QCOMPARE(applied, free);
        QVERIFY(!f.snap->isWindowTracked(w));
        f.snap->setShouldRestorePredicate({});
    }

    // A disabled monitor has no zone set to exchange: the swap reports the
    // edge (decision reading of Q1/Q2).
    void swapTowardADisabledMonitorReportsTheEdge()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("off-5"), QRect(1300, 100, 400, 300));
        const QString w2 = f.live(QStringLiteral("off-6"), QRect(2000, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft);
        f.snapOn(w2, {f.zone(0)}, kRight);
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        f.snap->setShouldRestorePredicate([](const QString& screen, int) {
            return screen != kRight;
        });
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->swapFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(f.snap->screenForTrackedWindow(w), kLeft);
        QCOMPARE(f.snap->screenForTrackedWindow(w2), kRight);
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_adjacent_zone"));
        f.snap->setShouldRestorePredicate({});
    }

    // Focus still crosses into a disabled monitor (F208 guard).
    void focusStillCrossesIntoADisabledMonitor()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("off-7"), QRect(1300, 100, 400, 300));
        const QString w2 = f.live(QStringLiteral("off-8"), QRect(2000, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft);
        f.snapOn(w2, {f.zone(0)}, kRight);
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        f.snap->setShouldRestorePredicate([](const QString& screen, int) {
            return screen != kRight;
        });
        QSignalSpy activate(f.snap.get(), &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
        f.snap->focusInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(activate.count(), 1);
        QCOMPARE(activate.first().at(0).toString(), w2);
        f.snap->setShouldRestorePredicate({});
    }

    // Unfloating an excluded window would snap it: refused (F243).
    void unfloatOfAnExcludedWindowIsRefused()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("float-1"));
        f.snapThenFloat(w, kLeft);
        QVERIFY(f.snap->isFloating(w));
        excludeWhen(f,
                    PhosphorRules::MatchExpression::makeLeaf(
                        PhosphorRules::Field::AppId, PhosphorRules::Operator::AppIdMatches, QStringLiteral("app")));
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->toggleFocusedFloat(NavigationContext{w, kLeft});
        QVERIFY(f.snap->isFloating(w));
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("excluded"));
        f.snap->setExcludeRuleSet(nullptr);
    }

    // Every snapping verb reports an exclusion once and commits nothing (F840).
    void everyVerbReportsAnExclusionOnce()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("excl-all"));
        f.snapOn(w, {f.zone(1)}, kLeft);
        excludeWhen(f,
                    PhosphorRules::MatchExpression::makeLeaf(
                        PhosphorRules::Field::AppId, PhosphorRules::Operator::AppIdMatches, QStringLiteral("app")));
        const NavigationContext ctx{w, kLeft};
        const QList<std::function<void()>> verbs = {
            [&] {
                f.snap->moveFocusedInDirection(QStringLiteral("right"), ctx);
            },
            [&] {
                f.snap->spanFocusedInDirection(QStringLiteral("right"), ctx);
            },
            [&] {
                f.snap->swapFocusedInDirection(QStringLiteral("right"), ctx);
            },
            [&] {
                f.snap->moveFocusedToPosition(1, ctx);
            },
            [&] {
                f.snap->pushFocusedToEmptyZone(ctx);
            },
        };
        for (const auto& verb : verbs) {
            QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
            QSignalSpy applies(f.snap.get(), &SnapEngine::applyGeometryRequested);
            verb();
            QCOMPARE(feedback.count(), 1);
            QCOMPARE(feedback.first().at(2).toString(), QStringLiteral("excluded"));
            QCOMPARE(feedback.first().at(5).toString(), kLeft);
            QCOMPARE(applies.count(), 0);
            QCOMPARE(f.snap->zoneForWindow(w), f.zone(1));
        }
        f.snap->setExcludeRuleSet(nullptr);
    }

private:
    /// One Exclude rule matching @p match, installed on the snap engine.
    void excludeWhen(SnapNavFixture& f, const PhosphorRules::MatchExpression& match)
    {
        m_rules = std::make_unique<PhosphorRules::RuleSet>();
        PhosphorRules::Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("exclude");
        rule.enabled = true;
        rule.match = match;
        PhosphorRules::RuleAction action;
        action.type = QString(PhosphorRules::ActionType::Exclude);
        rule.actions.append(action);
        QVERIFY(m_rules->addRule(rule));
        f.snap->setExcludeRuleSet(m_rules.get());
    }

    /// AppId "app" AND @p field Equals @p value.
    static PhosphorRules::MatchExpression appAnd(PhosphorRules::Field field, const QVariant& value)
    {
        return PhosphorRules::MatchExpression::makeAll(
            {PhosphorRules::MatchExpression::makeLeaf(PhosphorRules::Field::AppId,
                                                      PhosphorRules::Operator::AppIdMatches, QStringLiteral("app")),
             PhosphorRules::MatchExpression::makeLeaf(field, PhosphorRules::Operator::Equals, value)});
    }

    std::unique_ptr<PhosphorRules::RuleSet> m_rules;
};

QTEST_MAIN(TestWtaSnapNavigation)
#include "test_wta_snap_navigation.moc"
