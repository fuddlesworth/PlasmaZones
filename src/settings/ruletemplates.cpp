// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ruletemplates.h"

#include "../phosphor_i18n.h"

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>

#include <PhosphorZones/AssignmentEntry.h>

#include <QLatin1StringView>
#include <QUuid>

namespace PlasmaZones::RuleTemplates {

namespace {

namespace ActionType = PhosphorRules::ActionType;
namespace ActionParam = PhosphorRules::ActionParam;
using PhosphorRules::Field;
using PhosphorRules::MatchExpression;
using PhosphorRules::Operator;
using PhosphorRules::Rule;
using PhosphorRules::RuleAction;

} // namespace

QVariantMap newEmptyRule(const QString& subject)
{
    Rule rule;
    rule.id = QUuid::createUuid();
    rule.enabled = true;

    if (subject == QLatin1String("monitor")) {
        rule.name = PhosphorI18n::tr("New monitor rule");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QString());
    } else if (subject == QLatin1String("desktop")) {
        rule.name = PhosphorI18n::tr("New desktop rule");
        rule.priority = kContextBandBase;
        // VirtualDesktop is numeric; seed with 1 (typical first desktop).
        // Validator rejects 0 because 0 means "all desktops" and is the same
        // as having no predicate at all.
        rule.match = MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 1);
    } else if (subject == QLatin1String("application")) {
        rule.name = PhosphorI18n::tr("New application rule");
        rule.priority = kApplicationBandBase;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QString());
    } else if (subject == QLatin1String("activity")) {
        rule.name = PhosphorI18n::tr("New activity rule");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::Activity, Operator::Equals, QString());
    } else if (subject == QLatin1String("animation")) {
        rule.name = PhosphorI18n::tr("New animation rule");
        rule.priority = kAnimationBandBase;
        // Animation overrides typically apply globally — the action carries
        // the event scope (see `anim-shader:`/`anim-timing:`/`anim-curve:`
        // slot prefixes). Start with an always-true match so the user goes
        // straight to picking event + override in the action editor.
        rule.match = MatchExpression{};
    } else {
        // "custom" — start from the always-true catch-all so the user builds
        // the tree from scratch in the Advanced editor.
        rule.name = PhosphorI18n::tr("New custom rule");
        rule.priority = kAdvancedBandBase;
        rule.match = MatchExpression{};
    }
    return rule.toJson().toVariantMap();
}

QVariantList ruleTemplates()
{
    auto entry = [](QLatin1StringView id, const QString& label, const QString& description, QLatin1StringView icon) {
        QVariantMap m;
        m[QStringLiteral("id")] = QString::fromLatin1(id);
        m[QStringLiteral("label")] = label;
        m[QStringLiteral("description")] = description;
        m[QStringLiteral("icon")] = QString::fromLatin1(icon);
        return m;
    };

    // Templates mirror the flows the per-settings pages used to author
    // before the unified rule store: monitor → layout / algorithm
    // (assignments) and app → exclusion (per-mode disable + animation
    // exclusion lists). That's where the bulk of real-world rules live, so
    // these give one-click starting points for the common cases — plus a
    // size-based animation-exclusion showcase for the Width match field.
    QVariantList out;
    out.append(entry(QLatin1String("layoutOnMonitor"), PhosphorI18n::tr("Set a layout on a monitor"),
                     PhosphorI18n::tr("Pick a snapping layout to use on one monitor."), QLatin1String("view-grid")));
    out.append(entry(QLatin1String("algorithmOnMonitor"), PhosphorI18n::tr("Set a tiling algorithm on a monitor"),
                     PhosphorI18n::tr("Pick an autotile algorithm to use on one monitor."),
                     QLatin1String("view-list-tree")));
    out.append(entry(QLatin1String("lockLayoutOnMonitor"), PhosphorI18n::tr("Lock the layout on a monitor"),
                     PhosphorI18n::tr("Pin the active layout on one monitor so it can't be switched. This is the "
                                      "rule-driven version of the lock-layout shortcut."),
                     QLatin1String("object-locked")));
    out.append(entry(QLatin1String("excludeApp"), PhosphorI18n::tr("Exclude an app from tiling"),
                     PhosphorI18n::tr("Keep one application's windows out of the snap and autotile engines entirely."),
                     QLatin1String("edit-delete-remove")));
    out.append(entry(QLatin1String("excludeSmallFromAnimations"), PhosphorI18n::tr("Don't animate small windows"),
                     PhosphorI18n::tr("Skip open and close animations for windows narrower than a chosen width. Handy "
                                      "for tiny popups and tool windows."),
                     QLatin1String("edit-delete-remove")));
    return out;
}

QVariantMap newRuleFromTemplate(const QString& templateId)
{
    Rule rule;
    rule.id = QUuid::createUuid();
    rule.enabled = true;

    if (templateId == QLatin1String("layoutOnMonitor")) {
        rule.name = PhosphorI18n::tr("Snapping layout on monitor");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QString());
        // Two seeded actions — set engine mode AND pick the layout — so the
        // editor opens with the same shape the old MonitorStatePage
        // assignment flow produced. The user fills in the screen and layout
        // pickers; the engine-mode is pre-set to "snapping" because the
        // template's whole point is the snap layout.
        RuleAction engineMode;
        engineMode.type = QString::fromLatin1(ActionType::SetEngineMode);
        engineMode.params.insert(ActionParam::Mode,
                                 PhosphorZones::modeToWireString(PhosphorZones::AssignmentEntry::Snapping));
        rule.actions.append(engineMode);
        RuleAction layoutAction;
        layoutAction.type = QString::fromLatin1(ActionType::SetSnappingLayout);
        layoutAction.params.insert(ActionParam::LayoutId, QString());
        rule.actions.append(layoutAction);
    } else if (templateId == QLatin1String("algorithmOnMonitor")) {
        rule.name = PhosphorI18n::tr("Tiling algorithm on monitor");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QString());
        // Mirror of the layout template, but for the autotile engine + an
        // algorithm picker. Same rationale: this is the assignment flow.
        RuleAction engineMode;
        engineMode.type = QString::fromLatin1(ActionType::SetEngineMode);
        engineMode.params.insert(ActionParam::Mode,
                                 PhosphorZones::modeToWireString(PhosphorZones::AssignmentEntry::Autotile));
        rule.actions.append(engineMode);
        RuleAction algoAction;
        algoAction.type = QString::fromLatin1(ActionType::SetTilingAlgorithm);
        algoAction.params.insert(ActionParam::Algorithm, QString());
        rule.actions.append(algoAction);
    } else if (templateId == QLatin1String("lockLayoutOnMonitor")) {
        rule.name = PhosphorI18n::tr("Lock layout on monitor");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QString());
        // Single LockContext action seeded to lock (value = true) — the user
        // fills in the screen picker. Mode-agnostic and live-resolved, so it
        // pins whichever layout is active on that monitor without persisting.
        RuleAction lockAction;
        lockAction.type = QString::fromLatin1(ActionType::LockContext);
        lockAction.params.insert(ActionParam::Value, true);
        rule.actions.append(lockAction);
    } else if (templateId == QLatin1String("excludeApp")) {
        rule.name = PhosphorI18n::tr("Exclude an app from tiling");
        rule.priority = kApplicationBandBase;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QString());
        RuleAction action;
        action.type = QString::fromLatin1(ActionType::Exclude);
        rule.actions.append(action);
    } else if (templateId == QLatin1String("excludeSmallFromAnimations")) {
        rule.name = PhosphorI18n::tr("Don't animate small windows");
        rule.priority = kAnimationBandBase;
        // Seed a 300px width threshold; the user adjusts it in the editor.
        // ExcludeAnimations is terminal, so any matching window skips its
        // open/close animation. Showcases the Width numeric match field.
        rule.match = MatchExpression::makeLeaf(Field::Width, Operator::LessThan, 300);
        RuleAction action;
        action.type = QString::fromLatin1(ActionType::ExcludeAnimations);
        rule.actions.append(action);
    } else {
        return {};
    }
    return rule.toJson().toVariantMap();
}

} // namespace PlasmaZones::RuleTemplates
