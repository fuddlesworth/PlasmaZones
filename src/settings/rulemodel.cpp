// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rulemodel.h"

#include "ruleauthoring.h"

#include "../phosphor_i18n.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>

#include <PhosphorZones/AssignmentEntry.h>

#include <QJsonArray>
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

/// Human label for a single leaf predicate ("Monitor: LG Ultra HD").
/// @p screenLookup and @p activityLookup resolve the opaque ScreenId /
/// Activity UUID values to friendly names; an empty lookup is treated as
/// "identity" so the function stays usable in code paths that have not yet
/// wired the SettingsController-backed resolvers.
QString leafLabel(const MatchExpression::Predicate& predicate, const RuleModel::LabelLookup& screenLookup,
                  const RuleModel::LabelLookup& activityLookup, const RuleModel::LabelLookup& zoneLookup)
{
    // Pick the lookup matching the leaf's field. An empty lookup degenerates
    // to identity so this stays usable from code paths that have not yet
    // wired the SettingsController-backed resolvers.
    const RuleModel::LabelLookup* lookup = nullptr;
    if (predicate.field == Field::ScreenId) {
        lookup = &screenLookup;
    } else if (predicate.field == Field::Activity) {
        lookup = &activityLookup;
    } else if (predicate.field == Field::Zone) {
        lookup = &zoneLookup;
    }
    const auto resolveOne = [lookup](const QString& raw) {
        if (!lookup || !*lookup) {
            return raw;
        }
        const QString label = (*lookup)(raw);
        return label.isEmpty() ? raw : label;
    };

    // Mode is a closed wire-token vocabulary — render the friendly label
    // ("Mode: Snapping") rather than the raw token. An unknown token (a
    // hand-edited rule) round-trips verbatim.
    if (predicate.field == Field::Mode) {
        const QString token = predicate.value.toString();
        QString label = token;
        if (token == QLatin1String("snapping")) {
            label = PhosphorI18n::tr("Snapping");
        } else if (token == QLatin1String("tiling")) {
            label = PhosphorI18n::tr("Tiling");
        }
        return PhosphorI18n::tr("%1: %2").arg(RuleModel::fieldLabel(predicate.field), label);
    }

    // Boolean fields (Maximized, Keep above, Skip taskbar, …) render their
    // value as On / Off instead of the raw JSON "true" / "false" the generic
    // toString() fallback would emit, matching the editor toggle and the
    // expanded match tree (MatchExpressionView).
    if (PhosphorRules::fieldIsBool(predicate.field)) {
        return PhosphorI18n::tr("%1: %2").arg(RuleModel::fieldLabel(predicate.field),
                                              predicate.value.toBool() ? PhosphorI18n::tr("On")
                                                                       : PhosphorI18n::tr("Off"));
    }

    return PhosphorI18n::tr("%1: %2").arg(RuleModel::fieldLabel(predicate.field),
                                          resolveOne(predicate.value.toString()));
}

/// Localise a single engine-mode wire token. Returns an empty QString
/// for an empty input so callers can branch on it; unknown tokens
/// (a future picker option, a hand-edited rule) round-trip verbatim.
/// Routes through `PhosphorZones::modeFromWireString` so the wire-token
/// enumeration stays in one place — a future Mode enum extension lands
/// at the AssignmentEntry switch + the engineModeOptions() picker, not
/// here.
QString engineModeDisplayLabel(const QString& wire)
{
    if (wire.isEmpty()) {
        return {};
    }
    const auto mode = PhosphorZones::modeFromWireString(wire);
    if (!mode) {
        return wire;
    }
    switch (*mode) {
    case PhosphorZones::AssignmentEntry::Snapping:
        return PhosphorI18n::tr("Snapping");
    case PhosphorZones::AssignmentEntry::Autotile:
        return PhosphorI18n::tr("Autotile");
    case PhosphorZones::AssignmentEntry::Scrolling:
        return PhosphorI18n::tr("Scrolling");
    }
    return wire;
}

/// Human label for one action ("Snapping", "Float", "Excluded"). @p
/// snappingLayoutLookup resolves SetSnappingLayout's layoutId UUIDs;
/// @p tilingAlgorithmLookup resolves SetTilingAlgorithm's wire tokens
/// ("bsp", …) — split so a stray cross-resolve can't surface an algorithm
/// name in a snapping action's label or vice versa.
QString actionLabel(const RuleAction& action, const RuleModel::LabelLookup& snappingLayoutLookup,
                    const RuleModel::LabelLookup& tilingAlgorithmLookup,
                    const RuleModel::LabelLookup& shaderEffectLookup, const RuleModel::LabelLookup& overlayShaderLookup,
                    const RuleModel::LabelLookup& curveLookup, const RuleModel::LabelLookup& screenLookup)
{
    auto resolveWith = [](const QString& wire, const RuleModel::LabelLookup& lookup) {
        if (wire.isEmpty() || !lookup) {
            return wire;
        }
        const QString resolved = lookup(wire);
        return resolved.isEmpty() ? wire : resolved;
    };

    if (action.type == ActionType::SetEngineMode) {
        const QString mode = action.params.value(PhosphorRules::ActionParam::Mode).toString();
        const QString label = engineModeDisplayLabel(mode);
        return PhosphorI18n::tr("Engine: %1").arg(label.isEmpty() ? mode : label);
    }
    if (action.type == ActionType::SetSnappingLayout) {
        const QString layoutId = action.params.value(PhosphorRules::ActionParam::LayoutId).toString();
        return layoutId.isEmpty() ? PhosphorI18n::tr("Snapping layout")
                                  : PhosphorI18n::tr("Snapping: %1").arg(resolveWith(layoutId, snappingLayoutLookup));
    }
    if (action.type == ActionType::SetTilingAlgorithm) {
        const QString algo = action.params.value(PhosphorRules::ActionParam::Algorithm).toString();
        // Algorithms are wire tokens (`bsp`, `grid`, …). The dedicated
        // tilingAlgorithm lookup knows about autotile entries — the
        // RuleController wires it from settingsController.layouts,
        // which contains the displayName ("Binary Split") for each algorithm.
        return PhosphorI18n::tr("Tiling: %1").arg(resolveWith(algo, tilingAlgorithmLookup));
    }
    if (action.type == ActionType::DisableEngine) {
        // Name the engine being disabled — a rules list with "Disable
        // Snapping on DP-1" and "Disable Autotile on DP-2" otherwise reads
        // as two identical "Disabled" rows. Empty mode → fall back to
        // the generic "Disabled" label so a malformed rule still reads
        // sensibly.
        const QString mode = action.params.value(PhosphorRules::ActionParam::Mode).toString();
        const QString label = engineModeDisplayLabel(mode);
        if (label.isEmpty()) {
            return PhosphorI18n::tr("Disabled");
        }
        return PhosphorI18n::tr("Disable: %1").arg(label);
    }
    if (action.type == ActionType::Exclude) {
        return PhosphorI18n::tr("Excluded");
    }
    if (action.type == ActionType::Float) {
        return PhosphorI18n::tr("Float");
    }
    if (action.type == ActionType::SnapToZone) {
        const QJsonArray zones = action.params.value(PhosphorRules::ActionParam::Zones).toArray();
        QStringList nums;
        nums.reserve(zones.size());
        for (const QJsonValue& z : zones) {
            nums.append(QString::number(z.toInt()));
        }
        if (nums.isEmpty()) {
            return PhosphorI18n::tr("Snap to zone");
        }
        if (nums.size() == 1) {
            return PhosphorI18n::tr("Snap to zone %1").arg(nums.first());
        }
        return PhosphorI18n::tr("Snap to zones %1").arg(nums.join(QStringLiteral(", ")));
    }
    if (action.type == ActionType::RouteToScreen) {
        // Resolve the canonical target screen id to the same friendly monitor
        // label the ScreenId match-leaf surfaces (e.g. "LG Ultra HD · DP-2");
        // fall back to the raw id when no live monitor matches so a rule pinned
        // to an absent display stays legible.
        const QString screenId = action.params.value(PhosphorRules::ActionParam::TargetScreenId).toString();
        return screenId.isEmpty() ? PhosphorI18n::tr("Open on monitor")
                                  : PhosphorI18n::tr("Open on monitor: %1").arg(resolveWith(screenId, screenLookup));
    }
    if (action.type == ActionType::RouteToDesktop) {
        const int desktop = action.params.value(PhosphorRules::ActionParam::TargetDesktop).toInt();
        return desktop >= 1 ? PhosphorI18n::tr("Open on desktop %1").arg(desktop) : PhosphorI18n::tr("Open on desktop");
    }
    if (action.type == ActionType::SetOpacity) {
        // Mirror EVERY resolver reject path (shader_resolve.cpp's
        // resolveWindowOpacity) so the label never claims a behaviour
        // the runtime won't honour: null/undefined → label-only,
        // bool payload → "Opacity (invalid)", out-of-range value → same.
        const QJsonValue raw = action.params.value(PhosphorRules::ActionParam::Value);
        if (raw.isNull() || raw.isUndefined()) {
            return PhosphorI18n::tr("Opacity");
        }
        const QVariant rv = raw.toVariant();
        if (rv.typeId() == QMetaType::Bool) {
            return PhosphorI18n::tr("Opacity (invalid)");
        }
        bool ok = false;
        const double v = rv.toDouble(&ok);
        if (!ok || v < 0.0 || v > 1.0) {
            return PhosphorI18n::tr("Opacity (invalid)");
        }
        return PhosphorI18n::tr("Opacity: %1%").arg(static_cast<int>(v * 100.0 + 0.5));
    }
    if (action.type == ActionType::OverrideAnimationShader) {
        const QString id = action.params.value(PhosphorRules::ActionParam::EffectId).toString();
        return id.isEmpty() ? PhosphorI18n::tr("Block animation shader")
                            : PhosphorI18n::tr("Shader: %1").arg(resolveWith(id, shaderEffectLookup));
    }
    if (action.type == ActionType::OverrideAnimationTiming) {
        const int ms = action.params.value(PhosphorRules::ActionParam::DurationMs).toInt();
        return ms > 0 ? PhosphorI18n::tr("Duration: %1 ms").arg(ms) : PhosphorI18n::tr("Animation duration");
    }
    if (action.type == ActionType::OverrideAnimationCurve) {
        const QString curve = action.params.value(PhosphorRules::ActionParam::Curve).toString();
        return curve.isEmpty() ? PhosphorI18n::tr("Animation curve")
                               : PhosphorI18n::tr("Curve: %1").arg(resolveWith(curve, curveLookup));
    }
    if (action.type == ActionType::OverrideOverlayShader) {
        const QString id = action.params.value(PhosphorRules::ActionParam::EffectId).toString();
        return id.isEmpty() ? PhosphorI18n::tr("Overlay shader")
                            : PhosphorI18n::tr("Overlay shader: %1").arg(resolveWith(id, overlayShaderLookup));
    }
    if (action.type == ActionType::OverrideOverlayStyle) {
        const QString v = action.params.value(PhosphorRules::ActionParam::Value).toString();
        if (v == PhosphorRules::OverlayStyleToken::Rectangles) {
            return PhosphorI18n::tr("Overlay style: Zone rectangles");
        }
        if (v == PhosphorRules::OverlayStyleToken::Preview) {
            return PhosphorI18n::tr("Overlay style: Layout preview");
        }
        return PhosphorI18n::tr("Overlay style");
    }
    // ── single-value actions keyed on ActionParam::Value (restore-position,
    //    border / title-bar overrides, per-context gap overrides) ──
    {
        const QJsonValue raw = action.params.value(PhosphorRules::ActionParam::Value);
        // Boolean actions render their polarity-aware phrase ("Show border" /
        // "Hide border", …). The wording lives in RuleAuthoring so the editor
        // toggle caption and this summary always read the same; a non-boolean
        // action type returns empty and falls through to the cases below.
        if (const QString boolLabel = RuleAuthoring::boolActionStateLabel(action.type, raw.toBool());
            !boolLabel.isEmpty()) {
            return boolLabel;
        }
        if (action.type == ActionType::SetBorderWidth) {
            return PhosphorI18n::tr("Border width: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetBorderRadius) {
            return PhosphorI18n::tr("Corner radius: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetBorderColorActive || action.type == ActionType::SetBorderColorInactive) {
            // Each action carries a single colour in `value`. The accent sentinel
            // shows as a word, hex as upper-case.
            const QString value = raw.toString();
            const QString shown =
                value == PhosphorRules::BorderColorToken::Accent ? PhosphorI18n::tr("Accent") : value.toUpper();
            if (action.type == ActionType::SetBorderColorActive) {
                return PhosphorI18n::tr("Focused border: %1").arg(shown);
            }
            return PhosphorI18n::tr("Unfocused border: %1").arg(shown);
        }
        // ── per-context gap overrides ──
        if (action.type == ActionType::SetInnerGap) {
            return PhosphorI18n::tr("Gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGap) {
            return PhosphorI18n::tr("Outer gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGapTop) {
            return PhosphorI18n::tr("Top gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGapBottom) {
            return PhosphorI18n::tr("Bottom gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGapLeft) {
            return PhosphorI18n::tr("Left gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGapRight) {
            return PhosphorI18n::tr("Right gap: %1 px").arg(raw.toInt());
        }
    }
    return RuleModel::actionTypeFallbackLabel(action.type);
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
        return (pinsActivity && !pinsScreen) ? Section::Activity : Section::Monitor;
    }

    // Window-property match with no animation action — Applications.
    return Section::Application;
}

QString RuleModel::sectionLabel(Section section)
{
    switch (section) {
    case Section::Monitor:
        return PhosphorI18n::tr("Monitor & Layout");
    case Section::Application:
        return PhosphorI18n::tr("Applications");
    case Section::Activity:
        return PhosphorI18n::tr("Activities");
    case Section::Animation:
        return PhosphorI18n::tr("Animations");
    case Section::Advanced:
        return PhosphorI18n::tr("Advanced / Custom");
    case Section::System:
        return PhosphorI18n::tr("System");
    }
    return QString();
}

QString RuleModel::fieldLabel(Field field)
{
    switch (field) {
    case Field::AppId:
        return PhosphorI18n::tr("Application");
    case Field::WindowClass:
        return PhosphorI18n::tr("Window class");
    case Field::DesktopFile:
        return PhosphorI18n::tr("Desktop file");
    case Field::WindowRole:
        return PhosphorI18n::tr("Window role");
    case Field::Pid:
        return PhosphorI18n::tr("Process ID");
    case Field::Title:
        return PhosphorI18n::tr("Title");
    case Field::WindowType:
        return PhosphorI18n::tr("Window type");
    case Field::IsSticky:
        return PhosphorI18n::tr("Sticky");
    case Field::IsFullscreen:
        return PhosphorI18n::tr("Fullscreen");
    case Field::IsMaximized:
        return PhosphorI18n::tr("Maximized");
    case Field::IsMinimized:
        return PhosphorI18n::tr("Minimized");
    case Field::IsFocused:
        return PhosphorI18n::tr("Focused");
    case Field::ScreenId:
        return PhosphorI18n::tr("Monitor");
    case Field::VirtualDesktop:
        return PhosphorI18n::tr("Desktop");
    case Field::Activity:
        return PhosphorI18n::tr("Activity");
    case Field::IsTransient:
        return PhosphorI18n::tr("Transient");
    case Field::IsNotification:
        return PhosphorI18n::tr("Notification");
    case Field::Width:
        return PhosphorI18n::tr("Width");
    case Field::Height:
        return PhosphorI18n::tr("Height");
    case Field::KeepAbove:
        return PhosphorI18n::tr("Keep above");
    case Field::KeepBelow:
        return PhosphorI18n::tr("Keep below");
    case Field::SkipTaskbar:
        return PhosphorI18n::tr("Skip taskbar");
    case Field::SkipPager:
        return PhosphorI18n::tr("Skip pager");
    case Field::SkipSwitcher:
        return PhosphorI18n::tr("Skip switcher");
    case Field::IsModal:
        return PhosphorI18n::tr("Modal");
    case Field::HasDecoration:
        return PhosphorI18n::tr("Decorated");
    case Field::IsResizable:
        return PhosphorI18n::tr("Resizable");
    case Field::PositionX:
        return PhosphorI18n::tr("Position X");
    case Field::PositionY:
        return PhosphorI18n::tr("Position Y");
    case Field::CaptionNormal:
        return PhosphorI18n::tr("Title (no suffix)");
    case Field::IsFloating:
        return PhosphorI18n::tr("Floating");
    case Field::IsSnapped:
        return PhosphorI18n::tr("Snapped");
    case Field::IsTiled:
        return PhosphorI18n::tr("Tiled");
    case Field::Zone:
        return PhosphorI18n::tr("Zone");
    case Field::Mode:
        return PhosphorI18n::tr("Mode");
    case Field::TiledWindowCount:
        return PhosphorI18n::tr("Tiled window count");
    }
    return QString();
}

QString RuleModel::matchSummary(const MatchExpression& match) const
{
    if (match.isCatchAll()) {
        return PhosphorI18n::tr("Any window");
    }
    if (match.isLeaf()) {
        return leafLabel(match.predicate(), m_screenLookup, m_activityLookup, m_zoneLookup);
    }
    // A simple AND renders its leaves joined by " · ".
    if (match.kind() == MatchExpression::Kind::All) {
        QStringList parts;
        for (const MatchExpression& child : match.children()) {
            if (child.isLeaf()) {
                parts.append(leafLabel(child.predicate(), m_screenLookup, m_activityLookup, m_zoneLookup));
            } else {
                parts.append(PhosphorI18n::tr("(condition group)"));
            }
        }
        return parts.join(QStringLiteral(" · "));
    }
    // Any composite that is not a flat AND — count the leaves.
    const int n = conditionCount(match);
    return PhosphorI18n::tr("%n condition(s)", nullptr, n);
}

QString RuleModel::actionSummary(const QList<RuleAction>& actions) const
{
    if (actions.isEmpty()) {
        return PhosphorI18n::tr("No action");
    }
    QStringList parts;
    for (const RuleAction& a : actions) {
        parts.append(actionLabel(a, m_snappingLayoutLookup, m_tilingAlgorithmLookup, m_shaderEffectLookup,
                                 m_overlayShaderLookup, m_curveLookup, m_screenLookup));
    }
    return parts.join(QStringLiteral(" · "));
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

QString RuleModel::actionTypeFallbackLabel(const QString& type)
{
    // No built-in label covers this type — it is an unknown / legacy /
    // future-schema action. Surface the raw type id rather than an empty
    // string so the user at least sees what the rule carries.
    return type;
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

} // namespace PlasmaZones
