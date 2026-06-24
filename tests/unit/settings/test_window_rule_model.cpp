// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_rule_model.cpp
 * @brief Coverage for WindowRuleModel — the single flat model behind the
 *        unified Window Rules page.
 *
 * Pins:
 *   - role exposure (id / name / enabled / summaries / counts),
 *   - the C++-derived SectionRole bucketing, including "graduation" of a
 *     composite rule to Advanced,
 *   - CRUD by UUID (add / update / remove) and reorder,
 *   - the no-op update contract (updating to an identical rule does not churn).
 */

#include <QSignalSpy>
#include <QTest>

#include <PhosphorWindowRules/MatchExpression.h>
#include <PhosphorWindowRules/RuleAction.h>
#include <PhosphorWindowRules/WindowRule.h>

#include "settings/windowrulemodel.h"

using namespace PlasmaZones;
using namespace PhosphorWindowRules;

namespace {

/// Build a context-only layout-assignment rule pinned to @p screenId.
WindowRule monitorRule(const QString& screenId, const QString& name)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.priority = 300;
    rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, screenId);
    RuleAction engine;
    engine.type = QString(ActionType::SetEngineMode);
    engine.params.insert(ActionParam::Mode, QStringLiteral("autotile"));
    rule.actions = {engine};
    return rule;
}

/// Build a window-property float rule for @p appId.
WindowRule applicationRule(const QString& appId, const QString& name)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.priority = 200;
    rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, appId);
    RuleAction floatAction;
    floatAction.type = QString(ActionType::Float);
    rule.actions = {floatAction};
    return rule;
}

/// Build an animation-override rule.
WindowRule animationRule(const QString& windowClass, const QString& name)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.priority = 100;
    rule.match = MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, windowClass);
    RuleAction shader;
    shader.type = QString(ActionType::OverrideAnimationShader);
    shader.params.insert(ActionParam::Event, QStringLiteral("window.open"));
    shader.params.insert(ActionParam::EffectId, QStringLiteral("dissolve"));
    rule.actions = {shader};
    return rule;
}

/// Build a composite (ALL of {a leaf, a nested ANY}) rule — the kind that
/// must graduate to Advanced.
WindowRule compositeRule(const QString& name)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.priority = 500;
    const MatchExpression any = MatchExpression::makeAny(
        {MatchExpression::makeLeaf(Field::Title, Operator::Contains, QStringLiteral("Settings")),
         MatchExpression::makeLeaf(Field::Title, Operator::Contains, QStringLiteral("About"))});
    rule.match = MatchExpression::makeAll(
        {MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("code")), any});
    RuleAction floatAction;
    floatAction.type = QString(ActionType::Float);
    rule.actions = {floatAction};
    return rule;
}

} // namespace

class TestWindowRuleModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void rolesExposed();
    void sectionDerivation();
    void compositeGraduatesToAdvanced();
    void crudByUuid();
    void updateNoOpDoesNotChurn();
    void reorder();
    void addRuleAtInsertsAtIndexWithSingleSignal();
    void validationIssueCountRole();
    void screenIdsOfCollectsOnlyLiteralPinOperators();
    void actionSummaryRendersAllEngineModes();
    void disableEngineNamesTheModeBeingDisabled();
    void setOpacityRendersValidValuesAndGuardsRejectPaths();
    void restorePositionRendersValueAwareLabel();
    void shaderAndCurveLabelsResolveThroughLookups();
    void routeActionsRenderFriendlyLabels();
};

void TestWindowRuleModel::rolesExposed()
{
    WindowRuleModel model;
    const WindowRule rule = monitorRule(QStringLiteral("DP-2"), QStringLiteral("Work monitor"));
    model.setRules({rule});

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex idx = model.index(0, 0);
    QCOMPARE(model.data(idx, WindowRuleModel::IdRole).toString(), rule.id.toString());
    QCOMPARE(model.data(idx, WindowRuleModel::NameRole).toString(), QStringLiteral("Work monitor"));
    QCOMPARE(model.data(idx, WindowRuleModel::EnabledRole).toBool(), true);
    QCOMPARE(model.data(idx, WindowRuleModel::ConditionCountRole).toInt(), 1);
    QCOMPARE(model.data(idx, WindowRuleModel::ActionCountRole).toInt(), 1);
    QVERIFY(!model.data(idx, WindowRuleModel::MatchSummaryRole).toString().isEmpty());
    QVERIFY(!model.data(idx, WindowRuleModel::ActionSummaryRole).toString().isEmpty());
}

void TestWindowRuleModel::sectionDerivation()
{
    QCOMPARE(WindowRuleModel::sectionFor(monitorRule(QStringLiteral("DP-1"), QString())),
             WindowRuleModel::Section::Monitor);
    QCOMPARE(WindowRuleModel::sectionFor(applicationRule(QStringLiteral("firefox"), QString())),
             WindowRuleModel::Section::Application);
    QCOMPARE(WindowRuleModel::sectionFor(animationRule(QStringLiteral("firefox"), QString())),
             WindowRuleModel::Section::Animation);

    // An Activity-pinned context rule (no monitor leaf) reads as Activity.
    WindowRule activity;
    activity.id = QUuid::createUuid();
    activity.match = MatchExpression::makeLeaf(Field::Activity, Operator::Equals, QStringLiteral("{uuid}"));
    RuleAction engine;
    engine.type = QString(ActionType::SetEngineMode);
    engine.params.insert(ActionParam::Mode, QStringLiteral("snapping"));
    activity.actions = {engine};
    QCOMPARE(WindowRuleModel::sectionFor(activity), WindowRuleModel::Section::Activity);
}

void TestWindowRuleModel::compositeGraduatesToAdvanced()
{
    const WindowRule composite = compositeRule(QStringLiteral("VS Code dialogs"));
    // A nested ANY inside an ALL cannot be edited by any specialized section.
    QCOMPARE(WindowRuleModel::sectionFor(composite), WindowRuleModel::Section::Advanced);

    WindowRuleModel model;
    model.setRules({composite});
    QCOMPARE(model.data(model.index(0, 0), WindowRuleModel::IsCompositeRole).toBool(), true);
    QCOMPARE(model.data(model.index(0, 0), WindowRuleModel::SectionRole).value<WindowRuleModel::Section>(),
             WindowRuleModel::Section::Advanced);
    // The whole match has three leaf predicates.
    QCOMPARE(model.data(model.index(0, 0), WindowRuleModel::ConditionCountRole).toInt(), 3);
}

void TestWindowRuleModel::crudByUuid()
{
    WindowRuleModel model;
    WindowRule a = monitorRule(QStringLiteral("DP-1"), QStringLiteral("A"));
    WindowRule b = applicationRule(QStringLiteral("konsole"), QStringLiteral("B"));

    QVERIFY(model.addRule(a));
    QVERIFY(model.addRule(b));
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.contains(a.id));

    // Duplicate id is rejected.
    QVERIFY(!model.addRule(a));

    // Update by id — a real change applies.
    a.name = QStringLiteral("A renamed");
    QCOMPARE(model.updateRule(a), WindowRuleModel::UpdateResult::Applied);
    QCOMPARE(model.ruleById(a.id).name, QStringLiteral("A renamed"));

    // Update of an absent id fails.
    WindowRule ghost = monitorRule(QStringLiteral("DP-9"), QStringLiteral("Ghost"));
    QCOMPARE(model.updateRule(ghost), WindowRuleModel::UpdateResult::NotFound);

    // Remove by id.
    QVERIFY(model.removeRule(a.id));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(!model.contains(a.id));
    QVERIFY(!model.removeRule(a.id));
}

void TestWindowRuleModel::updateNoOpDoesNotChurn()
{
    WindowRuleModel model;
    const WindowRule a = monitorRule(QStringLiteral("DP-1"), QStringLiteral("A"));
    model.setRules({a});

    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy sectionSpy(&model, &WindowRuleModel::ruleSectionChanged);

    // Updating to an identical rule reports Unchanged and must NOT emit
    // dataChanged or ruleSectionChanged.
    QCOMPARE(model.updateRule(a), WindowRuleModel::UpdateResult::Unchanged);
    QCOMPARE(dataSpy.count(), 0);
    QCOMPARE(sectionSpy.count(), 0);
}

void TestWindowRuleModel::reorder()
{
    WindowRuleModel model;
    const WindowRule a = monitorRule(QStringLiteral("DP-1"), QStringLiteral("A"));
    const WindowRule b = monitorRule(QStringLiteral("DP-2"), QStringLiteral("B"));
    const WindowRule c = monitorRule(QStringLiteral("DP-3"), QStringLiteral("C"));
    model.setRules({a, b, c});

    // Move C before A — order becomes C, A, B.
    QVERIFY(model.moveRule(c.id, a.id));
    QCOMPARE(model.index(0, 0).data(WindowRuleModel::IdRole).toString(), c.id.toString());
    QCOMPARE(model.index(1, 0).data(WindowRuleModel::IdRole).toString(), a.id.toString());
    QCOMPARE(model.index(2, 0).data(WindowRuleModel::IdRole).toString(), b.id.toString());

    // Move A to the end (null beforeId) — order becomes C, B, A.
    QVERIFY(model.moveRule(a.id, QUuid()));
    QCOMPARE(model.index(2, 0).data(WindowRuleModel::IdRole).toString(), a.id.toString());

    // An unknown id fails.
    QVERIFY(!model.moveRule(QUuid::createUuid(), QUuid()));
}

void TestWindowRuleModel::addRuleAtInsertsAtIndexWithSingleSignal()
{
    // Pin the F#5 contract: addRuleAt inserts at the requested slot
    // with EXACTLY one rowsInserted signal (no follow-up moveRows /
    // dataChanged churn). The prior duplicateRule shape fired up to
    // four model signals per click; this test guards against that
    // regressing.
    WindowRuleModel model;
    const WindowRule a = monitorRule(QStringLiteral("DP-1"), QStringLiteral("A"));
    const WindowRule b = monitorRule(QStringLiteral("DP-2"), QStringLiteral("B"));
    const WindowRule c = monitorRule(QStringLiteral("DP-3"), QStringLiteral("C"));
    model.setRules({a, b, c});

    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy countSpy(&model, &WindowRuleModel::countChanged);

    const WindowRule mid = monitorRule(QStringLiteral("DP-4"), QStringLiteral("Mid"));
    QVERIFY(model.addRuleAt(mid, 1));

    // Order is now A, Mid, B, C — Mid lands at the requested slot.
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.index(1, 0).data(WindowRuleModel::IdRole).toString(), mid.id.toString());

    // Exactly one rowsInserted, no moveRows, no dataChanged. countChanged
    // fires once.
    QCOMPARE(insertSpy.count(), 1);
    QCOMPARE(moveSpy.count(), 0);
    QCOMPARE(dataSpy.count(), 0);
    QCOMPARE(countSpy.count(), 1);

    // Out-of-range indices clamp without erroring.
    const WindowRule head = monitorRule(QStringLiteral("DP-5"), QStringLiteral("Head"));
    QVERIFY(model.addRuleAt(head, -10));
    QCOMPARE(model.index(0, 0).data(WindowRuleModel::IdRole).toString(), head.id.toString());

    const WindowRule tail = monitorRule(QStringLiteral("DP-6"), QStringLiteral("Tail"));
    QVERIFY(model.addRuleAt(tail, 9999));
    QCOMPARE(model.index(model.rowCount() - 1, 0).data(WindowRuleModel::IdRole).toString(), tail.id.toString());

    // Duplicate id rejected even via addRuleAt.
    QVERIFY(!model.addRuleAt(a, 0));
}

void TestWindowRuleModel::validationIssueCountRole()
{
    // A well-formed rule (window-property match + window-domain Float action)
    // reports zero issues. The monitor rule above is a context-only match +
    // context action — also zero. Then the silently-never-fires combination
    // (context-domain SetEngineMode with a window-property match) reports 1.
    WindowRule cleanWindowRule = applicationRule(QStringLiteral("firefox"), QStringLiteral("clean window rule"));
    WindowRule cleanContextRule = monitorRule(QStringLiteral("DP-1"), QStringLiteral("clean monitor rule"));

    // Construct the bad combination explicitly — same shape as the rule the
    // load-time guard warns about.
    WindowRule badRule;
    badRule.id = QUuid::createUuid();
    badRule.name = QStringLiteral("firefox autotile");
    badRule.priority = 200;
    badRule.match = MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox"));
    RuleAction engine;
    engine.type = QString(ActionType::SetEngineMode);
    engine.params.insert(ActionParam::Mode, QStringLiteral("autotile"));
    badRule.actions = {engine};

    WindowRuleModel model;
    model.setRules({cleanWindowRule, cleanContextRule, badRule});

    QCOMPARE(model.data(model.index(0, 0), WindowRuleModel::ValidationIssueCountRole).toInt(), 0);
    QCOMPARE(model.data(model.index(1, 0), WindowRuleModel::ValidationIssueCountRole).toInt(), 0);
    QCOMPARE(model.data(model.index(2, 0), WindowRuleModel::ValidationIssueCountRole).toInt(), 1);

    // The role surfaces via its registered name so QML can bind by string.
    QVERIFY(model.roleNames().values().contains(QByteArrayLiteral("validationIssueCount")));
}

void TestWindowRuleModel::screenIdsOfCollectsOnlyLiteralPinOperators()
{
    // Only Equals is a literal monitor pin. A substring / regex operator
    // (StartsWith, Contains, …) never equals a real connector id, so collecting
    // its token would silently under-count the rule against every monitor tile.
    // Such rules must contribute NO screen id.
    const auto eq = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1"));
    QCOMPARE(WindowRuleModel::screenIdsOf(eq), QStringList{QStringLiteral("DP-1")});

    const auto startsWith = MatchExpression::makeLeaf(Field::ScreenId, Operator::StartsWith, QStringLiteral("DP"));
    QVERIFY(WindowRuleModel::screenIdsOf(startsWith).isEmpty());

    const auto contains = MatchExpression::makeLeaf(Field::ScreenId, Operator::Contains, QStringLiteral("HDMI"));
    QVERIFY(WindowRuleModel::screenIdsOf(contains).isEmpty());
}

void TestWindowRuleModel::actionSummaryRendersAllEngineModes()
{
    // Pin that `SetEngineMode` actionLabel renders all three vocabulary
    // tokens (snapping / autotile / scrolling) as their localised display
    // strings. A regression that collapsed the Scrolling branch back into
    // raw-wire-token fallback would silently revert "Engine: Scrolling"
    // to lowercase "Engine: scrolling" — caught by this assertion.
    const auto buildRule = [](const QString& modeToken) {
        WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.priority = 300;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1"));
        RuleAction engine;
        engine.type = QString(ActionType::SetEngineMode);
        engine.params.insert(ActionParam::Mode, modeToken);
        rule.actions = {engine};
        return rule;
    };
    WindowRuleModel model;
    model.setRules({buildRule(QStringLiteral("snapping")), buildRule(QStringLiteral("autotile")),
                    buildRule(QStringLiteral("scrolling"))});
    QCOMPARE(model.rowCount(), 3);
    const QString s0 = model.data(model.index(0, 0), WindowRuleModel::ActionSummaryRole).toString();
    const QString s1 = model.data(model.index(1, 0), WindowRuleModel::ActionSummaryRole).toString();
    const QString s2 = model.data(model.index(2, 0), WindowRuleModel::ActionSummaryRole).toString();
    // Each summary must end with the properly-cased localised label —
    // the i18n surface may add prefixes ("Engine: ") but the casing of
    // the engine name is the load-bearing contract.
    QVERIFY2(s0.contains(QStringLiteral("Snapping")), qPrintable(s0));
    QVERIFY2(s1.contains(QStringLiteral("Autotile")), qPrintable(s1));
    QVERIFY2(s2.contains(QStringLiteral("Scrolling")), qPrintable(s2));
    // Negative assertion: no summary should leak the lowercase wire
    // token — that would indicate the i18n branch fell through.
    QVERIFY2(!s2.contains(QStringLiteral("scrolling")), qPrintable(s2));
}

void TestWindowRuleModel::disableEngineNamesTheModeBeingDisabled()
{
    // Pin that `DisableEngine` actionLabel names the engine being
    // disabled rather than rendering a generic "Disabled" for every mode.
    // A regression that collapsed the per-mode branches into "Disabled"
    // would make two distinct disable rules read identically in the
    // rules list — caught by asserting the labels differ.
    const auto buildRule = [](const QString& modeToken) {
        WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.priority = 300;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1"));
        RuleAction disable;
        disable.type = QString(ActionType::DisableEngine);
        disable.params.insert(ActionParam::Mode, modeToken);
        rule.actions = {disable};
        return rule;
    };
    WindowRuleModel model;
    model.setRules({buildRule(QStringLiteral("snapping")), buildRule(QStringLiteral("autotile")),
                    buildRule(QStringLiteral("scrolling"))});
    const QString s0 = model.data(model.index(0, 0), WindowRuleModel::ActionSummaryRole).toString();
    const QString s1 = model.data(model.index(1, 0), WindowRuleModel::ActionSummaryRole).toString();
    const QString s2 = model.data(model.index(2, 0), WindowRuleModel::ActionSummaryRole).toString();
    QVERIFY2(s0.contains(QStringLiteral("Snapping")), qPrintable(s0));
    QVERIFY2(s1.contains(QStringLiteral("Autotile")), qPrintable(s1));
    QVERIFY2(s2.contains(QStringLiteral("Scrolling")), qPrintable(s2));
    // The three labels must be pairwise-distinct — otherwise a user with
    // multiple disable rules sees ambiguous "Disabled" rows.
    QVERIFY(s0 != s1);
    QVERIFY(s1 != s2);
    QVERIFY(s0 != s2);
}

void TestWindowRuleModel::setOpacityRendersValidValuesAndGuardsRejectPaths()
{
    // Pin that the SetOpacity actionLabel matches every resolver reject
    // path (shader_resolve.cpp::resolveWindowOpacity): valid in-range
    // values render as a percentage; null/undefined Value renders as
    // bare "Opacity"; bool / out-of-range values render as
    // "Opacity (invalid)". Without this guard the label would lie about
    // a behaviour the runtime won't honour.
    const auto buildRule = [](std::function<void(RuleAction&)> tweakAction) {
        WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.priority = 200;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("firefox"));
        RuleAction action;
        action.type = QString(ActionType::SetOpacity);
        tweakAction(action);
        rule.actions = {action};
        return rule;
    };

    WindowRuleModel model;
    model.setRules({
        buildRule([](RuleAction& a) {
            a.params.insert(ActionParam::Value, 0.5);
        }),
        buildRule([](RuleAction& a) {
            a.params.insert(ActionParam::Value, 1.0);
        }),
        buildRule([](RuleAction&) { }),
        buildRule([](RuleAction& a) {
            a.params.insert(ActionParam::Value, true);
        }),
        buildRule([](RuleAction& a) {
            a.params.insert(ActionParam::Value, 2.0);
        }),
    });

    const auto labelAt = [&](int row) {
        return model.data(model.index(row, 0), WindowRuleModel::ActionSummaryRole).toString();
    };
    QVERIFY2(labelAt(0).contains(QStringLiteral("50")), qPrintable(labelAt(0)));
    QVERIFY2(labelAt(1).contains(QStringLiteral("100")), qPrintable(labelAt(1)));
    // Missing Value: bare "Opacity" placeholder (no percent number)
    QCOMPARE(labelAt(2), QStringLiteral("Opacity"));
    // Bool payload: rejected with the (invalid) marker
    QVERIFY2(labelAt(3).contains(QStringLiteral("invalid")), qPrintable(labelAt(3)));
    // Out-of-range: same marker
    QVERIFY2(labelAt(4).contains(QStringLiteral("invalid")), qPrintable(labelAt(4)));
}

void TestWindowRuleModel::restorePositionRendersValueAwareLabel()
{
    // Pin that the RestorePosition actionLabel is value-aware: true and false
    // render distinct, human-readable chips (not the raw "restorePosition" wire
    // string). Guards picker↔model label consistency for the bool action.
    const auto buildRule = [](bool value) {
        WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.priority = 200;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("org.kde.dolphin"));
        RuleAction action;
        action.type = QString(ActionType::RestorePosition);
        action.params.insert(ActionParam::Value, value);
        rule.actions = {action};
        return rule;
    };

    WindowRuleModel model;
    model.setRules({buildRule(true), buildRule(false)});

    const auto labelAt = [&](int row) {
        return model.data(model.index(row, 0), WindowRuleModel::ActionSummaryRole).toString();
    };
    // Never the raw wire token.
    QVERIFY2(!labelAt(0).contains(QStringLiteral("restorePosition")), qPrintable(labelAt(0)));
    QVERIFY2(!labelAt(1).contains(QStringLiteral("restorePosition")), qPrintable(labelAt(1)));
    // true vs false produce distinct labels.
    QVERIFY(labelAt(0) != labelAt(1));
    // Pin BOTH branches' text, not just the false case: true reads as the
    // affirmative "Restore …", false as the negated "Don't restore …".
    QVERIFY2(labelAt(0).contains(QStringLiteral("Restore position")), qPrintable(labelAt(0)));
    QVERIFY2(labelAt(1).contains(QStringLiteral("Don't")), qPrintable(labelAt(1)));
}

void TestWindowRuleModel::shaderAndCurveLabelsResolveThroughLookups()
{
    // The action summary resolves OverrideAnimationShader effect ids and
    // OverrideAnimationCurve wire strings to friendly names through the
    // injected lookups (the registry / CurvePresets sources the rule editor
    // uses). With no lookup wired the raw value round-trips behind the label,
    // so a missing resolver degrades gracefully rather than hiding the payload.
    // A null payload uses the dedicated placeholder label (lookup not consulted).
    const auto shaderRuleWith = [](const QString& effectId) {
        WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.priority = 100;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("firefox"));
        RuleAction shader;
        shader.type = QString(ActionType::OverrideAnimationShader);
        if (!effectId.isNull()) {
            shader.params.insert(ActionParam::EffectId, effectId);
        }
        rule.actions = {shader};
        return rule;
    };
    const auto curveRuleWith = [](const QString& wire) {
        WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.priority = 99;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("konsole"));
        RuleAction curve;
        curve.type = QString(ActionType::OverrideAnimationCurve);
        if (!wire.isNull()) {
            curve.params.insert(ActionParam::Curve, wire);
        }
        rule.actions = {curve};
        return rule;
    };

    WindowRuleModel model;
    model.setRules({
        shaderRuleWith(QStringLiteral("dissolve")),
        curveRuleWith(QStringLiteral("0.33,1.00,0.68,1.00")),
        shaderRuleWith(QString()), // empty effect id → placeholder, never the lookup
        curveRuleWith(QString()), // empty curve → placeholder
        shaderRuleWith(QStringLiteral("mystery")), // unknown id → lookup passthrough
    });

    const auto summaryAt = [&](int row) {
        return model.data(model.index(row, 0), WindowRuleModel::ActionSummaryRole).toString();
    };

    // No lookups wired → the raw id / wire string round-trips behind the label;
    // empty payloads use the dedicated placeholders regardless of the lookup.
    QCOMPARE(summaryAt(0), QStringLiteral("Shader: dissolve"));
    QCOMPARE(summaryAt(1), QStringLiteral("Curve: 0.33,1.00,0.68,1.00"));
    QCOMPARE(summaryAt(2), QStringLiteral("Block animation shader"));
    QCOMPARE(summaryAt(3), QStringLiteral("Animation curve"));

    // Installing the resolvers and calling refreshLabels must emit exactly one
    // coalesced dataChanged spanning every label-derived role — the contract
    // the settings layer relies on after a lookup-source change. The setters
    // themselves are install-once and emit nothing, so the count pins refresh.
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    model.setShaderEffectLabelLookup([](const QString& id) {
        return id == QLatin1String("dissolve") ? QStringLiteral("Dissolve") : id;
    });
    model.setCurveLabelLookup([](const QString& wire) {
        return wire == QLatin1String("0.33,1.00,0.68,1.00") ? QStringLiteral("Standard (Cubic)") : wire;
    });
    model.refreshLabels();

    QCOMPARE(changedSpy.count(), 1);
    const QList<int> roles = changedSpy.at(0).at(2).value<QList<int>>();
    QVERIFY(roles.contains(WindowRuleModel::ActionSummaryRole));

    QCOMPARE(summaryAt(0), QStringLiteral("Shader: Dissolve"));
    QCOMPARE(summaryAt(1), QStringLiteral("Curve: Standard (Cubic)"));
    // Empty payloads still render their placeholders; an unknown id passes
    // through the lookup unchanged (the resolver returns its input).
    QCOMPARE(summaryAt(2), QStringLiteral("Block animation shader"));
    QCOMPARE(summaryAt(3), QStringLiteral("Animation curve"));
    QCOMPARE(summaryAt(4), QStringLiteral("Shader: mystery"));
}

void TestWindowRuleModel::routeActionsRenderFriendlyLabels()
{
    // Pin that RouteToScreen / RouteToDesktop render human labels in the action
    // summary rather than leaking their raw wire tokens. Both actions are
    // user-authorable (they appear in the action picker), so a missing
    // actionLabel branch would surface "routeToScreen" / "routeToDesktop" in the
    // rules list while the editor shows "Open on monitor" / "Open on virtual
    // desktop" — an inconsistency this test guards against. RouteToScreen also
    // resolves its target through the injected screen lookup, mirroring the
    // ScreenId match-leaf path, and falls back to the raw id when unresolved.
    const auto screenRuleWith = [](const QString& screenId) {
        WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.priority = 200;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("firefox"));
        RuleAction route;
        route.type = QString(ActionType::RouteToScreen);
        if (!screenId.isNull()) {
            route.params.insert(ActionParam::TargetScreenId, screenId);
        }
        rule.actions = {route};
        return rule;
    };
    const auto desktopRuleWith = [](int desktop) {
        WindowRule rule;
        rule.id = QUuid::createUuid();
        rule.priority = 199;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("konsole"));
        RuleAction route;
        route.type = QString(ActionType::RouteToDesktop);
        if (desktop >= 1) {
            route.params.insert(ActionParam::TargetDesktop, desktop);
        }
        rule.actions = {route};
        return rule;
    };

    WindowRuleModel model;
    model.setRules({
        screenRuleWith(QStringLiteral("DP-2")), // resolved through the lookup once installed
        screenRuleWith(QString()), // no target → placeholder, lookup not consulted
        desktopRuleWith(2),
        desktopRuleWith(0), // absent/invalid desktop → placeholder
    });

    const auto summaryAt = [&](int row) {
        return model.data(model.index(row, 0), WindowRuleModel::ActionSummaryRole).toString();
    };

    // No screen lookup wired → the raw canonical id round-trips behind the label.
    QCOMPARE(summaryAt(0), QStringLiteral("Open on monitor: DP-2"));
    QCOMPARE(summaryAt(1), QStringLiteral("Open on monitor"));
    QCOMPARE(summaryAt(2), QStringLiteral("Open on desktop 2"));
    QCOMPARE(summaryAt(3), QStringLiteral("Open on desktop"));

    // No summary may leak the lowercase wire token — that signals the
    // actionLabel branch fell through to the raw-type fallback.
    for (int row = 0; row < model.rowCount(); ++row) {
        QVERIFY2(!summaryAt(row).contains(QStringLiteral("routeToScreen")), qPrintable(summaryAt(row)));
        QVERIFY2(!summaryAt(row).contains(QStringLiteral("routeToDesktop")), qPrintable(summaryAt(row)));
    }

    // Installing the screen lookup resolves the target to its friendly monitor
    // label; refreshLabels coalesces the update into one dataChanged.
    model.setScreenLabelLookup([](const QString& id) {
        return id == QLatin1String("DP-2") ? QStringLiteral("LG Ultra HD · DP-2") : id;
    });
    model.refreshLabels();
    QCOMPARE(summaryAt(0), QStringLiteral("Open on monitor: LG Ultra HD · DP-2"));
}

QTEST_MAIN(TestWindowRuleModel)

#include "test_window_rule_model.moc"
