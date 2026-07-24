// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ruletemplates.h"

#include "phosphor_i18n.h"

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>

#include <PhosphorZones/AssignmentEntry.h>

#include <QJsonArray>
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

/// The seeded SetEngineMode("snapping") + SetSnappingLayout action pair shared
/// by every snapping-assignment template — the same shape the old
/// MonitorStatePage assignment flow produced. The layout id is left empty for
/// the editor's picker; the engine mode is pre-set to "snapping" because the
/// templates' whole point is the snap layout.
void appendSnappingAssignmentActions(Rule& rule)
{
    RuleAction engineMode;
    engineMode.type = QString::fromLatin1(ActionType::SetEngineMode);
    engineMode.params.insert(ActionParam::Mode,
                             PhosphorZones::modeToWireString(PhosphorZones::AssignmentEntry::Snapping));
    rule.actions.append(engineMode);
    RuleAction layoutAction;
    layoutAction.type = QString::fromLatin1(ActionType::SetSnappingLayout);
    layoutAction.params.insert(ActionParam::LayoutId, QString());
    rule.actions.append(layoutAction);
}

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
        // 0 is the sticky/all-desktops sentinel — it never equals a real
        // desktop number (matchexpression.cpp, VD-1) and the editor's desktop
        // picker treats it as unset.
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
    // exclusion lists). Plus the classic per-app window rules (zone
    // placement, screen routing, floating) and a ScreenOrientation context
    // showcase — one-click starting points for the common cases, ordered
    // context band first, then application band, then animation. There is
    // deliberately NO smart-gaps (TiledWindowCount + gap actions) template:
    // smart gaps already ships as a plain autotile setting
    // (AutotileConfig::smartGaps, Settings → Tiling), and such a rule would
    // silently never fire anyway — the gap resolver (resolveContextGaps in
    // layoutregistry_assignments.cpp) never stamps tiledWindowCount into its
    // context query; only resolveAssignmentEntry does, which is why
    // TiledWindowCount works for algorithm-switch rules but not gap rules.
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
    out.append(entry(QLatin1String("layoutOnDesktop"), PhosphorI18n::tr("Set a layout on a virtual desktop"),
                     PhosphorI18n::tr("Pick a snapping layout to use on one virtual desktop."),
                     QLatin1String("virtual-desktops")));
    out.append(entry(QLatin1String("portraitLayout"), PhosphorI18n::tr("Set a layout for portrait monitors"),
                     PhosphorI18n::tr("Pick a snapping layout to use whenever a monitor is in portrait "
                                      "orientation. Handy for rotating screens."),
                     QLatin1String("object-rotate-right")));
    out.append(entry(QLatin1String("snapAppToZone"), PhosphorI18n::tr("Open an app in a zone"),
                     PhosphorI18n::tr("Snap one application's windows into a chosen zone when they open."),
                     QLatin1String("window-pin")));
    out.append(entry(QLatin1String("routeAppToScreen"), PhosphorI18n::tr("Open an app on a monitor"),
                     PhosphorI18n::tr("Send one application's windows to a chosen monitor when they open."),
                     QLatin1String("monitor")));
    out.append(entry(QLatin1String("floatApp"), PhosphorI18n::tr("Float an app"),
                     PhosphorI18n::tr("Keep one application's windows floating instead of tiled. The windows stay "
                                      "managed, unlike a full exclusion."),
                     QLatin1String("window-restore")));
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
        // The user fills in the screen and layout pickers (see
        // appendSnappingAssignmentActions for the seeded shape's rationale).
        appendSnappingAssignmentActions(rule);
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
    } else if (templateId == QLatin1String("layoutOnDesktop")) {
        rule.name = PhosphorI18n::tr("Snapping layout on virtual desktop");
        rule.priority = kContextBandBase;
        // Desktop twin of layoutOnMonitor — same seeded action pair, keyed on
        // the desktop number instead of the screen picker. Seed desktop 1 for
        // the same reason newEmptyRule("desktop") does: 0 is the
        // sticky/all-desktops sentinel and the desktop picker treats it as
        // unset.
        rule.match = MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 1);
        appendSnappingAssignmentActions(rule);
    } else if (templateId == QLatin1String("portraitLayout")) {
        rule.name = PhosphorI18n::tr("Layout for portrait monitors");
        rule.priority = kContextBandBase;
        // ScreenOrientation showcase: matches ANY portrait screen, so one rule
        // covers a rotating monitor in both positions (it simply stops
        // matching when the screen returns to landscape).
        rule.match = MatchExpression::makeLeaf(Field::ScreenOrientation, Operator::Equals, QStringLiteral("portrait"));
        appendSnappingAssignmentActions(rule);
    } else if (templateId == QLatin1String("snapAppToZone")) {
        rule.name = PhosphorI18n::tr("Open an app in a zone");
        rule.priority = kApplicationBandBase;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QString());
        // Seed zone ordinal 1 (the validator requires a non-empty list); the
        // user picks the real zone in the editor. Ordinals are layout-agnostic,
        // matching the snapToZone1..9 shortcuts.
        RuleAction action;
        action.type = QString::fromLatin1(ActionType::SnapToZone);
        action.params.insert(ActionParam::Zones, QJsonArray{1});
        rule.actions.append(action);
    } else if (templateId == QLatin1String("routeAppToScreen")) {
        rule.name = PhosphorI18n::tr("Open an app on a monitor");
        rule.priority = kApplicationBandBase;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QString());
        RuleAction action;
        action.type = QString::fromLatin1(ActionType::RouteToScreen);
        action.params.insert(ActionParam::TargetScreenId, QString());
        rule.actions.append(action);
    } else if (templateId == QLatin1String("floatApp")) {
        rule.name = PhosphorI18n::tr("Float an app");
        rule.priority = kApplicationBandBase;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QString());
        // Float keeps the window managed but out of tiling — the gentler
        // sibling of Exclude, and what most "this app shouldn't tile" asks
        // actually want (media players, calculators, launcher popups).
        RuleAction action;
        action.type = QString::fromLatin1(ActionType::Float);
        rule.actions.append(action);
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
