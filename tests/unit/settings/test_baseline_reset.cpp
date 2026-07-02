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

#include <QObject>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <limits>

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleStore.h>

#include "../../../src/config/configdefaults.h"
#include "../../../src/core/baselinerules.h"
#include "../../../src/dbus/ruleadaptor.h"
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

// The full seeded rule set: the 3 managed appearance baselines + one user rule.
QList<Rule> seededSet(const Rule& user)
{
    return {makeBaselineBorderRule(), makeBaselineTitleBarRule(), makeBaselineGapRule(), user};
}

// Every managed baseline (all 8) + one user rule — for the scoped per-group
// dirty/reset/discard tests, which need the sibling groups present to prove
// isolation.
QList<Rule> fullBaselineSet(const Rule& user)
{
    return {makeBaselineBorderRule(),
            makeBaselineTitleBarRule(),
            makeBaselineGapRule(),
            makeBaselineOverlayRule(),
            makeBaselineGeneralMinWidthRule(),
            makeBaselineGeneralMinHeightRule(),
            makeBaselineAnimationMinWidthRule(),
            makeBaselineAnimationMinHeightRule(),
            user};
}

// A copy of @p rule with its min-size match threshold rewritten to @p n.
Rule withThreshold(Rule rule, int n)
{
    rule.match = PhosphorRules::MatchExpression::makeLeaf(rule.match.predicate().field,
                                                          PhosphorRules::Operator::LessThan, QVariant(n));
    return rule;
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

    // The scoped baseline groups (overlay / general min-size / animation
    // min-size) attribute dirty independently: an edit in one group flips ONLY
    // that group's dirty bit.
    void scopedBaselineDirtySplitsPerGroup()
    {
        RuleController controller;
        const Rule user = makeUserRule(QStringLiteral("u"));
        controller.model()->setRules(fullBaselineSet(user));
        controller.captureSavedSnapshot();

        QVERIFY(!controller.overlayBaselineDirty());
        QVERIFY(!controller.generalMinSizeBaselineDirty());
        QVERIFY(!controller.animationMinSizeBaselineDirty());

        // Overlay edit → overlay only.
        Rule overlay = makeBaselineOverlayRule();
        overlay.actions[0].params.insert(QString(PhosphorRules::ActionParam::Value), QStringLiteral("#FF123456"));
        controller.model()->updateRule(overlay);
        QVERIFY(controller.overlayBaselineDirty());
        QVERIFY(!controller.generalMinSizeBaselineDirty());
        QVERIFY(!controller.animationMinSizeBaselineDirty());
        QVERIFY(!controller.baselinesDirty());
        QVERIFY(!controller.userRulesDirty());
        controller.discardOverlayBaseline();
        QVERIFY(!controller.overlayBaselineDirty());

        // General min-size threshold edit → general group only.
        controller.model()->updateRule(withThreshold(makeBaselineGeneralMinWidthRule(), 500));
        QVERIFY(controller.generalMinSizeBaselineDirty());
        QVERIFY(!controller.overlayBaselineDirty());
        QVERIFY(!controller.animationMinSizeBaselineDirty());
        controller.discardGeneralMinSizeBaseline();
        QVERIFY(!controller.generalMinSizeBaselineDirty());

        // Animation min-size threshold edit → animation group only.
        controller.model()->updateRule(withThreshold(makeBaselineAnimationMinHeightRule(), 300));
        QVERIFY(controller.animationMinSizeBaselineDirty());
        QVERIFY(!controller.generalMinSizeBaselineDirty());
        QVERIFY(!controller.overlayBaselineDirty());
        controller.discardAnimationMinSizeBaseline();
        QVERIFY(!controller.animationMinSizeBaselineDirty());
    }

    // Each scoped reset rewrites ONLY its group to factory, leaving the sibling
    // groups' divergences and the user rule in place.
    void scopedBaselineResetIsolatesGroups()
    {
        RuleController controller;
        const Rule user = makeUserRule(QStringLiteral("keep-me"));
        controller.model()->setRules(fullBaselineSet(user));
        controller.captureSavedSnapshot();

        // Diverge all three groups.
        Rule overlay = makeBaselineOverlayRule();
        overlay.actions[0].params.insert(QString(PhosphorRules::ActionParam::Value), QStringLiteral("#FF123456"));
        controller.model()->updateRule(overlay);
        controller.model()->updateRule(withThreshold(makeBaselineGeneralMinWidthRule(), 500));
        controller.model()->updateRule(withThreshold(makeBaselineAnimationMinWidthRule(), 300));

        // Reset the general group: back to the on-by-default factory thresholds.
        controller.resetGeneralMinSizeBaseline();
        QCOMPARE(controller.model()->ruleById(ConfigDefaults::generalMinWidthRuleId()),
                 makeBaselineGeneralMinWidthRule());
        QVERIFY(!controller.generalMinSizeBaselineDirty());
        // Siblings untouched.
        QVERIFY(controller.overlayBaselineDirty());
        QVERIFY(controller.animationMinSizeBaselineDirty());

        // Reset the animation group: back to the off-by-default (0) factory.
        controller.resetAnimationMinSizeBaseline();
        QCOMPARE(controller.model()->ruleById(ConfigDefaults::animationMinWidthRuleId()),
                 makeBaselineAnimationMinWidthRule());
        QVERIFY(!controller.animationMinSizeBaselineDirty());
        QVERIFY(controller.overlayBaselineDirty());

        // Reset the overlay group last; user rule survived every reset.
        controller.resetOverlayBaseline();
        QCOMPARE(controller.model()->ruleById(ConfigDefaults::baselineOverlayRuleId()), makeBaselineOverlayRule());
        QVERIFY(!controller.overlayBaselineDirty());
        QCOMPARE(controller.model()->ruleById(user.id), user);
        QVERIFY(!controller.userRulesDirty());
    }

    // Each scoped discard restores ONLY its group from the snapshot, leaving a
    // concurrently-edited user rule staged.
    void scopedBaselineDiscardIsolatesUserRules()
    {
        RuleController controller;
        const Rule user = makeUserRule(QStringLiteral("mine"));
        controller.model()->setRules(fullBaselineSet(user));
        controller.captureSavedSnapshot();

        controller.model()->updateRule(withThreshold(makeBaselineGeneralMinHeightRule(), 999));
        controller.model()->updateRule(withThreshold(makeBaselineAnimationMinWidthRule(), 250));
        Rule editedUser = user;
        editedUser.name = QStringLiteral("edited");
        controller.model()->updateRule(editedUser);

        controller.discardGeneralMinSizeBaseline();
        controller.discardAnimationMinSizeBaseline();

        // Both min-size groups reverted to the snapshot (factory), user edit kept.
        QCOMPARE(controller.model()->ruleById(ConfigDefaults::generalMinHeightRuleId()),
                 makeBaselineGeneralMinHeightRule());
        QCOMPARE(controller.model()->ruleById(ConfigDefaults::animationMinWidthRuleId()),
                 makeBaselineAnimationMinWidthRule());
        QVERIFY(!controller.generalMinSizeBaselineDirty());
        QVERIFY(!controller.animationMinSizeBaselineDirty());
        QCOMPARE(controller.model()->ruleById(user.id).name, QStringLiteral("edited"));
        QVERIFY(controller.userRulesDirty());
    }

    // updateRuleFromJson on a MANAGED baseline must apply a match edit (the
    // v5 min-size thresholds and the Apply-to scope selector both edit the
    // match through this exact path) while still pinning the app-owned
    // identity: the managed flag and the lowest priority survive even a
    // payload that lies about them. Regression guard for the pre-v5
    // force-preserve that silently dropped every managed match edit.
    void updateRuleFromJsonAppliesManagedMatchEditKeepsIdentity()
    {
        RuleController controller;
        const Rule user = makeUserRule(QStringLiteral("u"));
        controller.model()->setRules(fullBaselineSet(user));
        controller.captureSavedSnapshot();

        // Edit the general min-width threshold the way GeneralPage.writeMinSize
        // does, with a hostile payload that also tries to demote the rule.
        QVariantMap payload = controller.ruleJson(ConfigDefaults::generalMinWidthRuleId().toString());
        QVERIFY(!payload.isEmpty());
        QVariantMap match;
        match.insert(QStringLiteral("field"), QStringLiteral("width"));
        match.insert(QStringLiteral("op"), QStringLiteral("lessThan"));
        match.insert(QStringLiteral("value"), 500);
        payload.insert(QStringLiteral("match"), match);
        payload.insert(QStringLiteral("managed"), false);
        payload.insert(QStringLiteral("priority"), 42);
        QVERIFY(controller.updateRuleFromJson(payload));

        const Rule updated = controller.model()->ruleById(ConfigDefaults::generalMinWidthRuleId());
        // The match edit lands…
        QCOMPARE(updated.match, withThreshold(makeBaselineGeneralMinWidthRule(), 500).match);
        // …while the identity pins hold against the hostile payload.
        QVERIFY(updated.managed);
        QCOMPARE(updated.priority, std::numeric_limits<int>::min());
        // And the group's dirty split sees the staged edit.
        QVERIFY(controller.generalMinSizeBaselineDirty());
    }

    // Track B: the daemon-side reset (RuleAdaptor::resetManagedDefaults) restores
    // every managed baseline to factory in the store and preserves user rules
    // (Policy A), persisting once. A baseline absent from the seeded set (here:
    // overlay + the four min-size baselines) is seeded by the reset, so the
    // final count is all 8 baselines + the user rule.
    void daemonResetRestoresBaselinesPreservesUserRules()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PhosphorRules::RuleStore store(dir.filePath(QStringLiteral("rules.json")));

        // Seed a border baseline flipped ON (diverges from factory) + a user rule.
        Rule border = makeBaselineBorderRule();
        border.actions[0].params.insert(QString(PhosphorRules::ActionParam::Value), true);
        const Rule user = makeUserRule(QStringLiteral("keep"));
        store.setAllRules({border, makeBaselineTitleBarRule(), makeBaselineGapRule(), user});

        QObject holder; // QDBusAbstractAdaptor needs a parent object.
        RuleAdaptor adaptor(&store, &holder);
        adaptor.resetManagedDefaults();

        const auto resetBorder = store.ruleSet().ruleById(ConfigDefaults::baselineBorderRuleId());
        QVERIFY(resetBorder.has_value());
        QCOMPARE(resetBorder.value(), makeBaselineBorderRule());
        // The absent baselines were seeded at factory.
        QCOMPARE(store.ruleSet().ruleById(ConfigDefaults::baselineOverlayRuleId()).value(), makeBaselineOverlayRule());
        QCOMPARE(store.ruleSet().ruleById(ConfigDefaults::generalMinWidthRuleId()).value(),
                 makeBaselineGeneralMinWidthRule());
        QCOMPARE(store.ruleSet().ruleById(ConfigDefaults::animationMinWidthRuleId()).value(),
                 makeBaselineAnimationMinWidthRule());
        // User rule preserved byte-for-byte (Policy A); total = 8 baselines + user.
        const auto keptUser = store.ruleSet().ruleById(user.id);
        QVERIFY(keptUser.has_value());
        QCOMPARE(keptUser.value(), user);
        QCOMPARE(store.ruleSet().rules().size(), 9);
    }

    // resetManagedDefaults must operate on the CURRENTLY PERSISTED set, not the
    // adaptor store's (possibly stale) in-memory set. Reproduces the real
    // scenario: the settings process rewrote rules.json out-of-band (as
    // Settings::reset() does to drop disable rules) while the daemon's borrowed
    // store still holds the old in-memory set. The m_store->load() at the top of
    // resetManagedDefaults must pick up the on-disk set first. Without it, this
    // test fails (the stale user rule survives / the on-disk one is lost).
    void daemonResetReloadsOnDiskSetFirst()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("rules.json"));

        // Adaptor's store: in-memory set carries userStale.
        PhosphorRules::RuleStore store(path);
        const Rule userStale = makeUserRule(QStringLiteral("stale"));
        store.setAllRules({makeBaselineBorderRule(), makeBaselineTitleBarRule(), makeBaselineGapRule(), userStale});

        // Out-of-band write (a second process/store) replaces userStale with
        // userDisk on disk. The adaptor's `store` does NOT see this yet.
        const Rule userDisk = makeUserRule(QStringLiteral("on-disk"));
        {
            PhosphorRules::RuleStore external(path);
            external.setAllRules(
                {makeBaselineBorderRule(), makeBaselineTitleBarRule(), makeBaselineGapRule(), userDisk});
        }

        QObject holder;
        RuleAdaptor adaptor(&store, &holder);
        adaptor.resetManagedDefaults();

        // The reload must have run: the on-disk user rule wins, the stale one is gone.
        QVERIFY(store.ruleSet().ruleById(userDisk.id).has_value());
        QVERIFY(!store.ruleSet().ruleById(userStale.id).has_value());
        // All 8 baselines (the 5 absent ones seeded by the reset) + the on-disk user rule.
        QCOMPARE(store.ruleSet().rules().size(), 9);
        // Baselines still reset to factory.
        QCOMPARE(store.ruleSet().ruleById(ConfigDefaults::baselineBorderRuleId()).value(), makeBaselineBorderRule());
    }
};

QTEST_MAIN(TestBaselineReset)
#include "test_baseline_reset.moc"
