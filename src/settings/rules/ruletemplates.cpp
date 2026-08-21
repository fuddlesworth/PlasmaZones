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
    // before the unified rule store: monitor → layout / algorithm /
    // scrolling mode and virtual desktop → layout (assignments), plus the
    // classic per-app window rules (zone placement, screen routing, floating,
    // exclusion). One-click starting points for the
    // common cases, ordered context band first, then application band.
    //
    // The bar for a tile is that it saves the user something a from-scratch
    // subject plus the action picker does not: either it seeds the coordinated
    // actions a context assignment needs (the mode plus, where the engine
    // needs one, its layout or algorithm — see scrollingOnMonitor below for
    // the case where it deliberately needs nothing else), or it is one of the
    // handful of per-app rules people open this dialog specifically to write.
    // Single-action showcase
    // tiles were removed rather than kept for discoverability — the action
    // picker's Window submenus now surface those actions directly, and a grid
    // long enough to scroll costs more than the showcase was worth. That
    // ruled out: the layout lock (the rule form of a shortcut the user
    // already has), a portrait-orientation showcase, the per-app zone-restore
    // veto, per-app decoration removal, and the small-window animation skip.
    //
    // There is also deliberately NO smart-gaps (TiledWindowCount + gap actions) template:
    // smart gaps already ships as a plain autotile setting
    // (AutotileConfig::smartGaps, Settings → Tiling), and such a rule would
    // silently never fire anyway — the gap resolver (resolveContextGaps in
    // layoutregistry_contextresolve.cpp) never stamps tiledWindowCount into its
    // context query; only resolveAssignmentEntry does, which is why
    // TiledWindowCount works for algorithm-switch rules but not gap rules.
    QVariantList out;
    out.append(entry(QLatin1String("layoutOnMonitor"), PhosphorI18n::tr("Set a layout on a monitor"),
                     PhosphorI18n::tr("Pick a snapping layout to use on one monitor."), QLatin1String("view-grid")));
    out.append(entry(QLatin1String("algorithmOnMonitor"), PhosphorI18n::tr("Set a tiling algorithm on a monitor"),
                     PhosphorI18n::tr("Pick a tiling algorithm to use on one monitor."),
                     QLatin1String("view-list-tree")));
    out.append(entry(QLatin1String("scrollingOnMonitor"), PhosphorI18n::tr("Use scrolling mode on a monitor"),
                     PhosphorI18n::tr("Switch one monitor to the scrolling placement mode."),
                     QLatin1String("view-list-details")));
    out.append(entry(QLatin1String("layoutOnDesktop"), PhosphorI18n::tr("Set a layout on a virtual desktop"),
                     PhosphorI18n::tr("Pick a snapping layout to use on one virtual desktop."),
                     QLatin1String("virtual-desktops")));
    out.append(entry(QLatin1String("snapAppToZone"), PhosphorI18n::tr("Open an app in a zone"),
                     PhosphorI18n::tr("Snap one application's windows into a chosen zone when they open."),
                     QLatin1String("window-pin")));
    out.append(entry(QLatin1String("routeAppToScreen"), PhosphorI18n::tr("Open an app on a monitor"),
                     PhosphorI18n::tr("Send one application's windows to a chosen monitor when they open."),
                     QLatin1String("monitor")));
    out.append(entry(QLatin1String("floatApp"), PhosphorI18n::tr("Float an app"),
                     PhosphorI18n::tr("Keep one application's windows floating instead of tiled. The windows stay "
                                      "managed, so they can still be dragged into a zone."),
                     QLatin1String("window-restore")));
    out.append(entry(QLatin1String("excludeApp"), PhosphorI18n::tr("Exclude an app from placement"),
                     PhosphorI18n::tr("Keep one application's windows out of tiling, snapping, and scrolling. "
                                      "Borders, decoration packs, and animations still apply."),
                     // Not edit-delete-remove: a generic red X said "delete"
                     // for a rule that deletes nothing, and it was the shared
                     // icon of three adjacent tiles, which made that block of
                     // the grid read as one card repeated. Unpinning says "this
                     // window is not held by the layout", and it reads against
                     // snapAppToZone's window-pin a row above as the deliberate
                     // opposite it is.
                     QLatin1String("window-unpin")));
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
    } else if (templateId == QLatin1String("scrollingOnMonitor")) {
        rule.name = PhosphorI18n::tr("Scrolling mode on monitor");
        rule.priority = kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QString());
        // Assignment flow like the algorithm template, but mode-only on
        // purpose. SetScrollingTemplate exists and is deliberately not seeded
        // here: a scrolling screen with no template is a legitimate end state,
        // and leaving the action out keeps the quick-start immediately savable.
        // The user adds the template action from the action picker when wanted.
        RuleAction engineMode;
        engineMode.type = QString::fromLatin1(ActionType::SetEngineMode);
        engineMode.params.insert(ActionParam::Mode,
                                 PhosphorZones::modeToWireString(PhosphorZones::AssignmentEntry::Scrolling));
        rule.actions.append(engineMode);
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
    } else if (templateId == QLatin1String("snapAppToZone")) {
        rule.name = PhosphorI18n::tr("Open an app in a zone");
        rule.priority = kApplicationBandBase;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QString());
        // Seed zone ordinal 1 so the action has a target (the validator needs
        // one entry across the ordinal and name lists); the user picks the real
        // zone, by number or by name, in the editor. The template deliberately
        // seeds an ordinal rather than a name: ordinals are layout-agnostic,
        // matching the snapToZone1..9 shortcuts, and need no layout to exist.
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
        // The id predates the ExcludePlacement retarget and is not persisted
        // anywhere (newRuleFromTemplate returns plain rule JSON), so it keeps
        // its historical spelling.
        rule.name = PhosphorI18n::tr("Exclude an app from placement");
        rule.priority = kApplicationBandBase;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QString());
        // ExcludePlacement, not the blanket Exclude: the template's title and
        // description promise a placement-only exclusion, and stripping
        // decorations too was the pre-split behavior of the only action
        // available then. (Blanket Exclude covers placement and decorations;
        // it does not suppress animations, it only cancels per-window
        // animation overrides.) Rules already created from this template keep
        // their stored blanket action.
        RuleAction action;
        action.type = QString::fromLatin1(ActionType::ExcludePlacement);
        rule.actions.append(action);
    } else {
        return {};
    }
    return rule.toJson().toVariantMap();
}

} // namespace PlasmaZones::RuleTemplates
