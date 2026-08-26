// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rulemodel.h"

#include "phosphor_i18n.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorRules/ActionParams.h>
#include <PhosphorRules/ActionTypes.h>
#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>

#include <QStringList>

#include <algorithm>

namespace PlasmaZones {

namespace {

namespace ActionType = PhosphorRules::ActionType;
namespace Tag = PhosphorRules::Tag;
using PhosphorRules::Field;
using PhosphorRules::MatchExpression;
using PhosphorRules::Operator;
using PhosphorRules::Rule;
using PhosphorRules::RuleAction;

/// True if @p actions carry an OverrideAnimation* action (Animation ∩ Effect).
bool hasAnimationAction(const QList<RuleAction>& actions)
{
    const auto& registry = PhosphorRules::ActionRegistry::instance();
    for (const RuleAction& a : actions) {
        if (registry.hasTag(a.type, Tag::Animation) && registry.hasTag(a.type, Tag::Effect)) {
            return true;
        }
    }
    return false;
}

/// True if @p actions carry a context-domain action (layout / engine / disable /
/// lock / gap / overlay) — the actions resolved during the windowless context
/// pass, and the kind a Monitor & Layout / per-monitor rule produces. Classify by
/// ActionDomain rather than a single tag so a gap-only or overlay-only context
/// rule (e.g. a per-monitor gap override) is recognized too, not just LayoutEngine.
///
/// A mixed rule that carries BOTH a window-domain action (e.g. a border) and a
/// context-domain action (e.g. a gap) classifies as a context rule, so it lands in
/// the Monitor band rather than Advanced. That is intentional: the migrated
/// per-mode "appearance" rule (border + gap) reads naturally as a Monitor & Layout
/// rule. Its gap slots still resolve by specificity (band-independent), and its
/// border slot resolving at the Monitor band is the desired ordering.
bool hasContextAction(const QList<RuleAction>& actions)
{
    const auto& registry = PhosphorRules::ActionRegistry::instance();
    for (const RuleAction& a : actions) {
        if (registry.domainFor(a) == PhosphorRules::ActionDomain::Context) {
            return true;
        }
    }
    return false;
}

/// Collect every leaf field referenced anywhere in @p match.
void collectFields(const MatchExpression& match, QList<Field>& out)
{
    if (match.isLeaf()) {
        out.append(match.predicate().field);
        return;
    }
    for (const MatchExpression& child : match.children()) {
        collectFields(child, out);
    }
}

/// Append every non-empty ScreenId leaf value in @p match to @p out.
/// Only the scalar Equals shape pins a literal monitor (the value is a
/// connector / screen id string).
void collectScreenIds(const MatchExpression& match, QStringList& out)
{
    if (match.isLeaf()) {
        const auto& predicate = match.predicate();
        if (predicate.field == Field::ScreenId && predicate.op == Operator::Equals) {
            const QString value = predicate.value.toString();
            if (!value.isEmpty()) {
                out.append(value);
            }
            // Any operator other than Equals (substring, regex, app-id, or
            // numeric comparison) is not a literal monitor pin — its token never
            // equals a real connector id, so collecting it would silently
            // under-count the rule against every tile. Such a rule doesn't pin a
            // specific monitor, so it contributes no screen id.
        }
        return;
    }
    for (const MatchExpression& child : match.children()) {
        collectScreenIds(child, out);
    }
}

/// True if @p match is a flat AND of leaf predicates (or a bare leaf, or the
/// empty catch-all). A specialized section can edit exactly this shape;
/// anything deeper graduates to Advanced.
bool matchIsSimpleConjunction(const MatchExpression& match)
{
    if (match.isLeaf()) {
        return true;
    }
    if (match.kind() != MatchExpression::Kind::All) {
        return false;
    }
    for (const MatchExpression& child : match.children()) {
        if (!child.isLeaf()) {
            return false;
        }
    }
    return true;
}

// True when a rule carries only context-domain actions (e.g. gaps). For such a
// rule a catch-all match means "every context" rather than "any window", so the
// summary reads "Everywhere" instead of the window-oriented "Any window".
bool ruleActionsAreContextOnly(const QList<RuleAction>& actions)
{
    if (actions.isEmpty()) {
        return false;
    }
    const PhosphorRules::ActionRegistry& registry = PhosphorRules::ActionRegistry::instance();
    for (const RuleAction& action : actions) {
        if (registry.domainFor(action) != PhosphorRules::ActionDomain::Context) {
            return false;
        }
    }
    return true;
}

/// How many of @p rule's actions name an animation event no rule can drive.
///
/// A rule's animation action is resolved through the rule evaluator, which
/// needs a window to match against. On an event the compositor resolves
/// windowless the resolvers short-circuit before the evaluator runs, so such an
/// action is stored, listed, and never consulted. The rule editor's picker no
/// longer OFFERS those events, so a rule can only carry one if it was authored
/// before that filter existed or by hand.
///
/// The three action types are spelled out rather than tested as "carries an
/// event param", because a future action could carry an event for some other
/// purpose — the same reasoning InertAnimationEventChip.qml gives for its own
/// list. An event this build does not know is NOT counted: that is not "not per
/// window", it is not an event, and the editor renders it as missing instead.
int inertAnimationActionCount(const Rule& rule)
{
    int count = 0;
    const QStringList known = PhosphorAnimation::ProfilePaths::allBuiltInPaths();
    for (const RuleAction& action : rule.actions) {
        if (action.type != ActionType::OverrideAnimationShader && action.type != ActionType::OverrideAnimationTiming
            && action.type != ActionType::OverrideAnimationCurve) {
            continue;
        }
        const QString event = action.params.value(PhosphorRules::ActionParam::Event).toString();
        if (event.isEmpty()) {
            continue;
        }
        if (!known.contains(event)) {
            continue;
        }
        if (!PhosphorAnimation::ProfilePaths::eventPathResolvesPerWindow(event)) {
            ++count;
        }
    }
    return count;
}

} // namespace

RuleModel::RuleModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int RuleModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rules.size();
}

QHash<int, QByteArray> RuleModel::roleNames() const
{
    return {
        {IdRole, "ruleId"},
        {NameRole, "name"},
        {EnabledRole, "enabled"},
        {PriorityRole, "priority"},
        {SectionRole, "section"},
        {MatchSummaryRole, "matchSummary"},
        {ActionSummaryRole, "actionSummary"},
        {ConditionCountRole, "conditionCount"},
        {ActionCountRole, "actionCount"},
        {IsCompositeRole, "isComposite"},
        {ScreenIdsRole, "screenIds"},
        {ValidationIssueCountRole, "validationIssueCount"},
        {InertAnimationActionCountRole, "inertAnimationActionCount"},
        {ManagedRole, "managed"},
    };
}

QVariant RuleModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rules.size()) {
        return {};
    }
    const Rule& rule = m_rules.at(index.row());
    switch (role) {
    case IdRole:
        return rule.id.toString();
    case NameRole:
        return displayName(rule);
    case EnabledRole:
        return rule.enabled;
    case PriorityRole:
        return rule.priority;
    case SectionRole:
        return QVariant::fromValue(sectionFor(rule));
    case MatchSummaryRole:
        // A catch-all rule whose actions are all context-domain (gaps) applies to
        // every context, not "any window" — label it accordingly.
        if (rule.match.isCatchAll() && ruleActionsAreContextOnly(rule.actions)) {
            return PhosphorI18n::tr("Everywhere");
        }
        return matchSummary(rule.match);
    case ActionSummaryRole:
        return actionSummary(rule.actions);
    case ConditionCountRole:
        return conditionCount(rule.match);
    case ActionCountRole:
        return rule.actions.size();
    case IsCompositeRole:
        return !matchIsSimpleConjunction(rule.match);
    case ScreenIdsRole:
        return screenIdsOf(rule.match);
    case ValidationIssueCountRole:
        // Recomputed per query — the validator is cheap (one tree walk) and a
        // model-side cache would have to be invalidated on every rule edit, so
        // pay the trivial cost over keeping the staleness guard.
        return rule.validationIssues().size();
    case InertAnimationActionCountRole:
        // Recomputed per query for the same reason as above, and cheaper still:
        // a scan of the rule's own action list against a ten-entry predicate.
        return inertAnimationActionCount(rule);
    case ManagedRole:
        return rule.managed;
    default:
        return {};
    }
}

void RuleModel::setRules(const QList<Rule>& rules)
{
    beginResetModel();
    m_rules = rules;
    endResetModel();
    Q_EMIT countChanged();
}

Rule RuleModel::ruleById(const QUuid& id) const
{
    const int row = indexOf(id);
    return row < 0 ? Rule{} : m_rules.at(row);
}

bool RuleModel::contains(const QUuid& id) const
{
    return indexOf(id) >= 0;
}

int RuleModel::indexOf(const QUuid& id) const
{
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

bool RuleModel::addRule(const Rule& rule)
{
    if (rule.id.isNull() || !rule.isValid() || contains(rule.id)) {
        return false;
    }
    const int row = m_rules.size();
    beginInsertRows(QModelIndex(), row, row);
    m_rules.append(rule);
    endInsertRows();
    Q_EMIT countChanged();
    return true;
}

bool RuleModel::addRuleAt(const Rule& rule, int insertIndex)
{
    if (rule.id.isNull() || !rule.isValid() || contains(rule.id)) {
        return false;
    }
    // Clamp so callers don't have to range-check; -1 / negative goes
    // to the front, anything >= rowCount goes to the end. Matches the
    // semantics QML drag-reorder expects. Use qsizetype-clamped form
    // to avoid `-Wshorten-64-to-32` for the m_rules.size() cast;
    // beginInsertRows takes int (Qt API), so the final narrow is
    // both unavoidable and safe for any rule count ≤ INT_MAX (rules
    // realistically fit on a single page — N ≈ 10s, not billions).
    const qsizetype clampedRow = std::clamp(static_cast<qsizetype>(insertIndex), qsizetype{0}, m_rules.size());
    const int row = static_cast<int>(clampedRow);
    beginInsertRows(QModelIndex(), row, row);
    m_rules.insert(row, rule);
    endInsertRows();
    Q_EMIT countChanged();
    return true;
}

RuleModel::UpdateResult RuleModel::updateRule(const Rule& rule)
{
    const int row = indexOf(rule.id);
    if (row < 0 || !rule.isValid()) {
        return UpdateResult::NotFound;
    }
    if (m_rules.at(row) == rule) {
        return UpdateResult::Unchanged; // no-op — caller must not dirty the page
    }
    // An edit can move a rule into a different section (e.g. adding an
    // animation action). A plain dataChanged does not prompt the QML section
    // view to re-bucket it, so detect the shift and fire a structural signal.
    const Section before = sectionFor(m_rules.at(row));
    m_rules[row] = rule;
    const QModelIndex idx = index(row, 0);
    Q_EMIT dataChanged(idx, idx);
    if (sectionFor(rule) != before) {
        Q_EMIT ruleSectionChanged();
        return UpdateResult::AppliedSectionChanged;
    }
    return UpdateResult::Applied;
}

bool RuleModel::removeRule(const QUuid& id)
{
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    // Managed rules (the baseline appearance rule) are app-owned and must stay
    // present — refuse deletion. The UI hides the affordance via ManagedRole;
    // this is the model-level backstop against a programmatic caller.
    if (m_rules.at(row).managed) {
        return false;
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_rules.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
    return true;
}

bool RuleModel::moveRule(const QUuid& id, const QUuid& beforeId)
{
    const int from = indexOf(id);
    if (from < 0) {
        return false;
    }
    // Managed rules are pinned (their precedence comes from a fixed priority,
    // not list position) — refuse to reorder them. The UI hides drag for
    // managed rows; this is the model-level backstop.
    if (m_rules.at(from).managed) {
        return false;
    }
    int dest = beforeId.isNull() ? m_rules.size() : indexOf(beforeId);
    if (dest < 0) {
        dest = m_rules.size();
    }
    // Qt's beginMoveRows requires the destination row to be the index the
    // row will occupy *before* removal — when moving down, that is dest;
    // when moving up, also dest. A no-op move (from == dest or from + 1 ==
    // dest) is rejected to avoid an empty signal cycle.
    if (dest == from || dest == from + 1) {
        return true;
    }
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), dest)) {
        return false;
    }
    const Rule moved = m_rules.takeAt(from);
    const int insertAt = dest > from ? dest - 1 : dest;
    m_rules.insert(insertAt, moved);
    endMoveRows();
    return true;
}

void RuleModel::setPriorities(const QList<int>& priorities)
{
    if (priorities.size() != m_rules.size() || m_rules.isEmpty()) {
        return;
    }
    // Compute the narrowest [firstChanged..lastChanged] range that covers
    // every actually-modified row, then emit one dataChanged over that range.
    // A no-op call (no priorities differ) skips the emit entirely.
    int firstChanged = -1;
    int lastChanged = -1;
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].priority != priorities.at(i)) {
            m_rules[i].priority = priorities.at(i);
            if (firstChanged < 0) {
                firstChanged = i;
            }
            lastChanged = i;
        }
    }
    if (firstChanged < 0) {
        return;
    }
    Q_EMIT dataChanged(index(firstChanged, 0), index(lastChanged, 0), {PriorityRole});
}

RuleModel::Section RuleModel::sectionFor(const Rule& rule)
{
    // App-managed baseline rules (the seeded Default borders / title bars / gaps)
    // are System rules — grouped apart from user-authored ones regardless of
    // their match/actions.
    if (rule.managed) {
        return Section::System;
    }

    // Animation actions are decisive — a rule that touches an animation slot
    // belongs to the Animations group regardless of its match shape.
    if (hasAnimationAction(rule.actions)) {
        // A composite match a section cannot represent still graduates.
        return matchIsSimpleConjunction(rule.match) ? Section::Animation : Section::Advanced;
    }

    const bool simple = matchIsSimpleConjunction(rule.match);
    if (!simple) {
        return Section::Advanced; // composite — only Advanced can edit it
    }

    if (rule.match.isContextOnly()) {
        // Context-only rule. If it pins an Activity, the chip filter wants
        // it under Activity; otherwise it is a Monitor & Layout rule.
        if (!hasContextAction(rule.actions)) {
            return Section::Advanced; // context match but non-context action
        }
        QList<Field> fields;
        collectFields(rule.match, fields);
        const bool pinsActivity = fields.contains(Field::Activity);
        const bool pinsScreen = fields.contains(Field::ScreenId);
        // A rule pinned to an Activity but no monitor reads naturally as an
        // Activity rule; a monitor-pinned one is Monitor & Layout.
        if (pinsActivity && !pinsScreen) {
            return Section::Activity;
        }
        // Monitor & Layout means a rule scoped to a screen and to what runs on
        // it. ColorScheme is the one context field that says nothing about a
        // screen — it matches a system-wide light/dark state — so a rule whose
        // whole match is the colour scheme would file under a monitor heading
        // while naming no monitor. No curated section describes it, so it goes
        // to Advanced / Custom.
        const bool pinsScreenScoped = pinsScreen || fields.contains(Field::VirtualDesktop)
            || fields.contains(Field::ScreenOrientation) || fields.contains(Field::ActiveLayout)
            || fields.contains(Field::Mode) || fields.contains(Field::TiledWindowCount);
        return pinsScreenScoped ? Section::Monitor : Section::Advanced;
    }

    // Window-property match with no animation action — Applications.
    return Section::Application;
}

int RuleModel::conditionCount(const MatchExpression& match)
{
    QList<Field> fields;
    collectFields(match, fields);
    return fields.size();
}

QStringList RuleModel::screenIdsOf(const MatchExpression& match)
{
    QStringList out;
    collectScreenIds(match, out);
    return out;
}

void RuleModel::setScreenLabelLookup(LabelLookup fn)
{
    // Setters are install-once: the controller installs the closure during
    // construction. Re-emitting dataChanged here would force a full-row
    // rebind on every install, but the closures already read live state via
    // their captured `this`, so re-installing is redundant. Callers route
    // upstream change notifications through `refreshLabels()` instead, which
    // emits a single dataChanged covering every label-derived role.
    m_screenLookup = std::move(fn);
}

void RuleModel::setActivityLabelLookup(LabelLookup fn)
{
    m_activityLookup = std::move(fn);
}

void RuleModel::setZoneLabelLookup(LabelLookup fn)
{
    m_zoneLookup = std::move(fn);
}

void RuleModel::setVirtualDesktopLabelLookup(LabelLookup fn)
{
    m_virtualDesktopLookup = std::move(fn);
}

void RuleModel::setSnappingLayoutLabelLookup(LabelLookup fn)
{
    m_snappingLayoutLookup = std::move(fn);
}

void RuleModel::setTilingAlgorithmLabelLookup(LabelLookup fn)
{
    m_tilingAlgorithmLookup = std::move(fn);
}

void RuleModel::setShaderEffectLabelLookup(LabelLookup fn)
{
    m_shaderEffectLookup = std::move(fn);
}

void RuleModel::setAnimationEventLabelLookup(LabelLookup fn)
{
    m_animationEventLookup = std::move(fn);
}

void RuleModel::setDecorationPackLabelLookup(LabelLookup fn)
{
    m_decorationPackLookup = std::move(fn);
}

void RuleModel::setOverlayShaderLabelLookup(LabelLookup fn)
{
    m_overlayShaderLookup = std::move(fn);
}

void RuleModel::setCurveLabelLookup(LabelLookup fn)
{
    m_curveLookup = std::move(fn);
}

void RuleModel::refreshLabels()
{
    if (m_rules.isEmpty()) {
        return;
    }
    // One dataChanged covering every role whose value derives from a label
    // lookup. Coalesces the three-signal cascade (screens/activities/layouts
    // change) that previously ran nine separate emits.
    const QModelIndex top = index(0);
    const QModelIndex bottom = index(m_rules.size() - 1);
    Q_EMIT dataChanged(top, bottom, {NameRole, MatchSummaryRole, ActionSummaryRole});
}

QString RuleModel::displayName(const PhosphorRules::Rule& rule) const
{
    // A rule whose stored name matches the auto-stamped form is treated as
    // "no name" so the row's title falls back to the (lookup-resolved) match
    // summary. Without this, every legacy context rule shows raw connector
    // strings and activity UUIDs as its primary label.
    if (rule.name.isEmpty() || !rule.match.isContextOnly()) {
        return rule.name;
    }
    QString screenId;
    int virtualDesktop = 0;
    QString activity;
    PhosphorRules::ContextRuleBridge::contextDimsOf(rule.match, screenId, virtualDesktop, activity);
    if (rule.name == PhosphorRules::ContextRuleBridge::contextRuleName(screenId, virtualDesktop, activity)) {
        return QString();
    }
    return rule.name;
}

QString RuleModel::titleFor(const PhosphorRules::Rule& rule) const
{
    const QString name = displayName(rule);
    if (!name.isEmpty()) {
        return name;
    }
    // Mirror MatchSummaryRole's special case so a context-only catch-all
    // reads "Everywhere" here too, not "Any window".
    if (rule.match.isCatchAll() && ruleActionsAreContextOnly(rule.actions)) {
        return PhosphorI18n::tr("Everywhere");
    }
    return matchSummary(rule.match);
}

} // namespace PlasmaZones
