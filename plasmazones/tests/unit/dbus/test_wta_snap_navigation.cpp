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

    std::unique_ptr<PhosphorRules::RuleSet> m_rules;
};

QTEST_MAIN(TestWtaSnapNavigation)
#include "test_wta_snap_navigation.moc"
