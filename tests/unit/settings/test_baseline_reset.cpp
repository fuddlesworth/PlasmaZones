// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_baseline_reset.cpp
 * @brief Coverage for the per-page appearance Reset/Discard primitives:
 *        the shared baseline-rule makers (core/baselinerules.h) and
 *        RuleController's value-based baseline/user dirty split plus
 *        resetBaselines() / discardBaselineEdits().
 *
 * All exercised without a live daemon: the model is populated directly and
 * captureSavedSnapshot() establishes the "last saved" baseline.
 */

#include <QTest>

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>

#include "../../../src/core/baselinerules.h"
#include "../../../src/config/configdefaults.h"
#include "../../../src/settings/rulecontroller.h"
#include "../../../src/settings/rulemodel.h"

using namespace PlasmaZones;
using PhosphorRules::Rule;

namespace {

// A minimal non-managed user rule, distinct from the managed baselines.
Rule makeUserRule(const QString& name)
{
    Rule r;
    r.id = QUuid::createUuid();
    r.name = name;
    r.managed = false;
    r.priority = 100;
    r.match = PhosphorRules::MatchExpression{}; // empty = matches all, valid
    // A rule needs at least one registered action to be valid (Rule::isValid).
    PhosphorRules::RuleAction a;
    a.type = QString(PhosphorRules::ActionType::SetBorderVisible);
    a.params.insert(QString(PhosphorRules::ActionParam::Value), true);
    r.actions = {a};
    return r;
}

// The full seeded rule set: the 3 managed baselines + one user rule.
QList<Rule> seededSet(const Rule& user)
{
    return {makeBaselineBorderRule(), makeBaselineTitleBarRule(), makeBaselineGapRule(), user};
}

} // namespace

class TestBaselineReset : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // The moved makers keep their canonical ids, managed flag, and action shape.
    void makersProduceExpectedShape()
    {
        const Rule border = makeBaselineBorderRule();
        QCOMPARE(border.id, ConfigDefaults::baselineBorderRuleId());
        QVERIFY(border.managed);
        QCOMPARE(border.actions.size(), 1);
        QCOMPARE(border.actions.at(0).type, QString(PhosphorRules::ActionType::SetBorderVisible));

        const Rule titleBar = makeBaselineTitleBarRule();
        QCOMPARE(titleBar.id, ConfigDefaults::baselineTitleBarRuleId());
        QVERIFY(titleBar.managed);

        const Rule gap = makeBaselineGapRule();
        QCOMPARE(gap.id, ConfigDefaults::baselineGapRuleId());
        QVERIFY(gap.managed);
        // Inner gap, outer gap, per-side toggle — the three parent actions only.
        QCOMPARE(gap.actions.size(), 3);
    }

    // baselinesDirty tracks the managed subset; userRulesDirty the rest.
    void dirtySplitAttributesCorrectly()
    {
        RuleController controller;
        const Rule user = makeUserRule(QStringLiteral("u"));
        controller.model()->setRules(seededSet(user));
        controller.captureSavedSnapshot();

        QVERIFY(!controller.baselinesDirty());
        QVERIFY(!controller.userRulesDirty());

        // Editing a managed baseline dirties appearance only.
        Rule border = makeBaselineBorderRule();
        border.actions[0].params.insert(QString(PhosphorRules::ActionParam::Value), true);
        controller.model()->updateRule(border);
        QVERIFY(controller.baselinesDirty());
        QVERIFY(!controller.userRulesDirty());

        // Restore appearance, then edit the user rule — dirties rules only.
        controller.discardBaselineEdits();
        QVERIFY(!controller.baselinesDirty());
        Rule editedUser = user;
        editedUser.name = QStringLiteral("renamed");
        controller.model()->updateRule(editedUser);
        QVERIFY(!controller.baselinesDirty());
        QVERIFY(controller.userRulesDirty());
    }

    // resetBaselines rewrites the 3 baselines to factory and leaves user rules.
    void resetBaselinesRestoresFactoryAndKeepsUserRules()
    {
        RuleController controller;
        const Rule user = makeUserRule(QStringLiteral("keep-me"));

        // Seed with a border baseline flipped ON and a gap baseline carrying an
        // extra per-side action — both diverge from factory.
        Rule border = makeBaselineBorderRule();
        border.actions[0].params.insert(QString(PhosphorRules::ActionParam::Value), true);
        Rule gap = makeBaselineGapRule();
        PhosphorRules::RuleAction perSide;
        perSide.type = QString(PhosphorRules::ActionType::SetOuterGapTop);
        perSide.params.insert(QString(PhosphorRules::ActionParam::Value), 7);
        gap.actions.append(perSide);
        controller.model()->setRules({border, makeBaselineTitleBarRule(), gap, user});
        controller.captureSavedSnapshot();

        controller.resetBaselines();

        // Baselines back to factory (gap's per-side action dropped).
        QCOMPARE(controller.model()->ruleById(ConfigDefaults::baselineBorderRuleId()), makeBaselineBorderRule());
        QCOMPARE(controller.model()->ruleById(ConfigDefaults::baselineGapRuleId()), makeBaselineGapRule());
        QCOMPARE(controller.model()->ruleById(ConfigDefaults::baselineGapRuleId()).actions.size(), 3);
        // User rule untouched, count unchanged.
        QCOMPARE(controller.model()->rowCount(), 4);
        QCOMPARE(controller.model()->ruleById(user.id), user);
    }

    // discardBaselineEdits restores baselines from the snapshot without touching
    // a concurrently-edited user rule.
    void discardBaselineEditsIsolatesUserRules()
    {
        RuleController controller;
        const Rule user = makeUserRule(QStringLiteral("mine"));
        controller.model()->setRules(seededSet(user));
        controller.captureSavedSnapshot();

        // Edit both a baseline and the user rule.
        Rule border = makeBaselineBorderRule();
        border.actions[0].params.insert(QString(PhosphorRules::ActionParam::Value), true);
        controller.model()->updateRule(border);
        Rule editedUser = user;
        editedUser.name = QStringLiteral("edited");
        controller.model()->updateRule(editedUser);

        controller.discardBaselineEdits();

        // Baseline reverted to the snapshot (factory OFF), user edit preserved.
        QCOMPARE(controller.model()->ruleById(ConfigDefaults::baselineBorderRuleId()), makeBaselineBorderRule());
        QCOMPARE(controller.model()->ruleById(user.id).name, QStringLiteral("edited"));
        QVERIFY(!controller.baselinesDirty());
        QVERIFY(controller.userRulesDirty());
    }
};

QTEST_MAIN(TestBaselineReset)
#include "test_baseline_reset.moc"
