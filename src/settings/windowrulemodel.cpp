// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowrulemodel.h"

#include "../phosphor_i18n.h"

#include <PhosphorWindowRules/ContextRuleBridge.h>
#include <PhosphorWindowRules/MatchTypes.h>
#include <PhosphorWindowRules/RuleAction.h>

#include <PhosphorZones/AssignmentEntry.h>

#include <QStringList>

#include <algorithm>

namespace PlasmaZones {

namespace {

namespace ActionType = PhosphorWindowRules::ActionType;
namespace Tag = PhosphorWindowRules::Tag;
using PhosphorWindowRules::Field;
using PhosphorWindowRules::MatchExpression;
using PhosphorWindowRules::Operator;
using PhosphorWindowRules::RuleAction;
using PhosphorWindowRules::WindowRule;

/// True if @p actions carry an OverrideAnimation* action (Animation ∩ Effect).
bool hasAnimationAction(const QList<RuleAction>& actions)
{
    const auto& registry = PhosphorWindowRules::ActionRegistry::instance();
    for (const RuleAction& a : actions) {
        if (registry.hasTag(a.type, Tag::Animation) && registry.hasTag(a.type, Tag::Effect)) {
            return true;
        }
    }
    return false;
}

/// True if @p actions carry a context-targeting action (layout / engine /
/// disable / lock) — the kind a Monitor & Layout rule produces.
bool hasContextAction(const QList<RuleAction>& actions)
{
    const auto& registry = PhosphorWindowRules::ActionRegistry::instance();
    for (const RuleAction& a : actions) {
        if (registry.hasTag(a.type, Tag::LayoutEngine)) {
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
/// Handles both the scalar shape (Equals/Contains/…: value is a QString) and
/// the set-membership shape (Operator::In: value is a QVariantList or
/// QStringList — `toString()` returns empty for a list, so the entries must be
/// iterated explicitly). Mirrors the dual-shape handling in
/// `MatchExpression::evaluate` for the In case.
void collectScreenIds(const MatchExpression& match, QStringList& out)
{
    if (match.isLeaf()) {
        const auto& predicate = match.predicate();
        if (predicate.field == Field::ScreenId) {
            if (predicate.op == Operator::In) {
                // The wire form is a QVariantList; a programmatically built
                // leaf may carry a QStringList. Accept either — same contract
                // as the validator/evaluator.
                if (predicate.value.metaType().id() == QMetaType::QStringList) {
                    for (const QString& member : predicate.value.toStringList()) {
                        if (!member.isEmpty()) {
                            out.append(member);
                        }
                    }
                } else {
                    for (const QVariant& member : predicate.value.toList()) {
                        const QString value = member.toString();
                        if (!value.isEmpty()) {
                            out.append(value);
                        }
                    }
                }
            } else if (predicate.op == Operator::Equals) {
                const QString value = predicate.value.toString();
                if (!value.isEmpty()) {
                    out.append(value);
                }
            }
            // Any operator other than Equals/In (substring, regex, app-id, or
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
QString leafLabel(const MatchExpression::Predicate& predicate, const WindowRuleModel::LabelLookup& screenLookup,
                  const WindowRuleModel::LabelLookup& activityLookup, const WindowRuleModel::LabelLookup& zoneLookup)
{
    // Pick the lookup matching the leaf's field. An empty lookup degenerates
    // to identity so this stays usable from code paths that have not yet
    // wired the SettingsController-backed resolvers.
    const WindowRuleModel::LabelLookup* lookup = nullptr;
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

    // An `In` operator stores the candidate set as a QVariantList or
    // QStringList. `toString()` on either of those returns the empty string,
    // so without this fan-out a rule like "ScreenId In [DP-2, DP-3]" would
    // render as "Monitor: " with nothing after the colon. Mirror the dual-
    // shape handling pattern the collectScreenIds helper uses.
    if (predicate.op == Operator::In) {
        QStringList resolved;
        const QVariant& v = predicate.value;
        if (v.metaType().id() == QMetaType::QStringList) {
            const QStringList list = v.toStringList();
            resolved.reserve(list.size());
            for (const QString& raw : list) {
                resolved.append(resolveOne(raw));
            }
        } else {
            const QVariantList list = v.toList();
            resolved.reserve(list.size());
            for (const QVariant& item : list) {
                resolved.append(resolveOne(item.toString()));
            }
        }
        // QStringList::join keeps the rendered list short; falling back to
        // a single space-joined line matches how the daemon logs the same set.
        return PhosphorI18n::tr("%1: %2").arg(WindowRuleModel::fieldLabel(predicate.field),
                                              resolved.join(QStringLiteral(", ")));
    }

    return PhosphorI18n::tr("%1: %2").arg(WindowRuleModel::fieldLabel(predicate.field),
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
QString actionLabel(const RuleAction& action, const WindowRuleModel::LabelLookup& snappingLayoutLookup,
                    const WindowRuleModel::LabelLookup& tilingAlgorithmLookup,
                    const WindowRuleModel::LabelLookup& shaderEffectLookup,
                    const WindowRuleModel::LabelLookup& curveLookup)
{
    auto resolveWith = [](const QString& wire, const WindowRuleModel::LabelLookup& lookup) {
        if (wire.isEmpty() || !lookup) {
            return wire;
        }
        const QString resolved = lookup(wire);
        return resolved.isEmpty() ? wire : resolved;
    };

    if (action.type == ActionType::SetEngineMode) {
        const QString mode = action.params.value(PhosphorWindowRules::ActionParam::Mode).toString();
        const QString label = engineModeDisplayLabel(mode);
        return PhosphorI18n::tr("Engine: %1").arg(label.isEmpty() ? mode : label);
    }
    if (action.type == ActionType::SetSnappingLayout) {
        const QString layoutId = action.params.value(PhosphorWindowRules::ActionParam::LayoutId).toString();
        return layoutId.isEmpty() ? PhosphorI18n::tr("Snapping layout")
                                  : PhosphorI18n::tr("Snapping: %1").arg(resolveWith(layoutId, snappingLayoutLookup));
    }
    if (action.type == ActionType::SetTilingAlgorithm) {
        const QString algo = action.params.value(PhosphorWindowRules::ActionParam::Algorithm).toString();
        // Algorithms are wire tokens (`bsp`, `grid`, …). The dedicated
        // tilingAlgorithm lookup knows about autotile entries — the
        // WindowRuleController wires it from settingsController.layouts,
        // which contains the displayName ("Binary Split") for each algorithm.
        return PhosphorI18n::tr("Tiling: %1").arg(resolveWith(algo, tilingAlgorithmLookup));
    }
    if (action.type == ActionType::DisableEngine) {
        // Name the engine being disabled — a rules list with "Disable
        // Snapping on DP-1" and "Disable Autotile on DP-2" otherwise reads
        // as two identical "Disabled" rows. Empty mode → fall back to
        // the generic "Disabled" label so a malformed rule still reads
        // sensibly.
        const QString mode = action.params.value(PhosphorWindowRules::ActionParam::Mode).toString();
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
    if (action.type == ActionType::SetOpacity) {
        // Mirror EVERY resolver reject path (shader_resolve.cpp's
        // resolveWindowOpacity) so the label never claims a behaviour
        // the runtime won't honour: null/undefined → label-only,
        // bool payload → "Opacity (invalid)", out-of-range value → same.
        const QJsonValue raw = action.params.value(PhosphorWindowRules::ActionParam::Value);
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
        const QString id = action.params.value(PhosphorWindowRules::ActionParam::EffectId).toString();
        return id.isEmpty() ? PhosphorI18n::tr("Block animation shader")
                            : PhosphorI18n::tr("Shader: %1").arg(resolveWith(id, shaderEffectLookup));
    }
    if (action.type == ActionType::OverrideAnimationTiming) {
        const int ms = action.params.value(PhosphorWindowRules::ActionParam::DurationMs).toInt();
        return ms > 0 ? PhosphorI18n::tr("Duration: %1 ms").arg(ms) : PhosphorI18n::tr("Animation duration");
    }
    if (action.type == ActionType::OverrideAnimationCurve) {
        const QString curve = action.params.value(PhosphorWindowRules::ActionParam::Curve).toString();
        return curve.isEmpty() ? PhosphorI18n::tr("Animation curve")
                               : PhosphorI18n::tr("Curve: %1").arg(resolveWith(curve, curveLookup));
    }
    // ── single-value actions keyed on ActionParam::Value (restore-position,
    //    border / title-bar overrides, per-context gap overrides) ──
    {
        const QJsonValue raw = action.params.value(PhosphorWindowRules::ActionParam::Value);
        if (action.type == ActionType::RestorePosition) {
            return raw.toBool() ? PhosphorI18n::tr("Restore position on login")
                                : PhosphorI18n::tr("Don't restore position on login");
        }
        if (action.type == ActionType::SetHideTitleBar) {
            return raw.toBool() ? PhosphorI18n::tr("Hide title bars") : PhosphorI18n::tr("Show title bars");
        }
        if (action.type == ActionType::LockContext) {
            return raw.toBool() ? PhosphorI18n::tr("Lock layout") : PhosphorI18n::tr("Don't lock layout");
        }
        if (action.type == ActionType::SetBorderVisible) {
            return raw.toBool() ? PhosphorI18n::tr("Show border") : PhosphorI18n::tr("Hide border");
        }
        if (action.type == ActionType::SetBorderWidth) {
            return PhosphorI18n::tr("Border width: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetBorderRadius) {
            return PhosphorI18n::tr("Corner radius: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetBorderColor) {
            return PhosphorI18n::tr("Border: %1").arg(raw.toString().toUpper());
        }
        // ── per-context gap overrides ──
        if (action.type == ActionType::SetZonePadding) {
            return PhosphorI18n::tr("Zone padding: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGap) {
            return PhosphorI18n::tr("Outer gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetUsePerSideOuterGap) {
            return raw.toBool() ? PhosphorI18n::tr("Per-side outer gaps") : PhosphorI18n::tr("Uniform outer gap");
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
    return WindowRuleModel::actionTypeFallbackLabel(action.type);
}

} // namespace

WindowRuleModel::WindowRuleModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int WindowRuleModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rules.size();
}

QHash<int, QByteArray> WindowRuleModel::roleNames() const
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
    };
}

QVariant WindowRuleModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rules.size()) {
        return {};
    }
    const WindowRule& rule = m_rules.at(index.row());
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
    default:
        return {};
    }
}

void WindowRuleModel::setRules(const QList<WindowRule>& rules)
{
    beginResetModel();
    m_rules = rules;
    endResetModel();
    Q_EMIT countChanged();
}

WindowRule WindowRuleModel::ruleById(const QUuid& id) const
{
    const int row = indexOf(id);
    return row < 0 ? WindowRule{} : m_rules.at(row);
}

bool WindowRuleModel::contains(const QUuid& id) const
{
    return indexOf(id) >= 0;
}

int WindowRuleModel::indexOf(const QUuid& id) const
{
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

bool WindowRuleModel::addRule(const WindowRule& rule)
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

bool WindowRuleModel::addRuleAt(const WindowRule& rule, int insertIndex)
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

WindowRuleModel::UpdateResult WindowRuleModel::updateRule(const WindowRule& rule)
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
    }
    return UpdateResult::Applied;
}

bool WindowRuleModel::removeRule(const QUuid& id)
{
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_rules.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
    return true;
}

bool WindowRuleModel::moveRule(const QUuid& id, const QUuid& beforeId)
{
    const int from = indexOf(id);
    if (from < 0) {
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
    const WindowRule moved = m_rules.takeAt(from);
    const int insertAt = dest > from ? dest - 1 : dest;
    m_rules.insert(insertAt, moved);
    endMoveRows();
    return true;
}

void WindowRuleModel::setPriorities(const QList<int>& priorities)
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

WindowRuleModel::Section WindowRuleModel::sectionFor(const WindowRule& rule)
{
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

QString WindowRuleModel::sectionLabel(Section section)
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
    }
    return QString();
}

QString WindowRuleModel::fieldLabel(Field field)
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
    case Field::Zone:
        return PhosphorI18n::tr("Zone");
    }
    return QString();
}

QString WindowRuleModel::matchSummary(const MatchExpression& match) const
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

QString WindowRuleModel::actionSummary(const QList<RuleAction>& actions) const
{
    if (actions.isEmpty()) {
        return PhosphorI18n::tr("No action");
    }
    QStringList parts;
    for (const RuleAction& a : actions) {
        parts.append(
            actionLabel(a, m_snappingLayoutLookup, m_tilingAlgorithmLookup, m_shaderEffectLookup, m_curveLookup));
    }
    return parts.join(QStringLiteral(" · "));
}

int WindowRuleModel::conditionCount(const MatchExpression& match)
{
    QList<Field> fields;
    collectFields(match, fields);
    return fields.size();
}

QStringList WindowRuleModel::screenIdsOf(const MatchExpression& match)
{
    QStringList out;
    collectScreenIds(match, out);
    return out;
}

QString WindowRuleModel::actionTypeFallbackLabel(const QString& type)
{
    // No built-in label covers this type — it is an unknown / legacy /
    // future-schema action. Surface the raw type id rather than an empty
    // string so the user at least sees what the rule carries.
    return type;
}

void WindowRuleModel::setScreenLabelLookup(LabelLookup fn)
{
    // Setters are install-once: the controller installs the closure during
    // construction. Re-emitting dataChanged here would force a full-row
    // rebind on every install, but the closures already read live state via
    // their captured `this`, so re-installing is redundant. Callers route
    // upstream change notifications through `refreshLabels()` instead, which
    // emits a single dataChanged covering every label-derived role.
    m_screenLookup = std::move(fn);
}

void WindowRuleModel::setActivityLabelLookup(LabelLookup fn)
{
    m_activityLookup = std::move(fn);
}

void WindowRuleModel::setZoneLabelLookup(LabelLookup fn)
{
    m_zoneLookup = std::move(fn);
}

void WindowRuleModel::setSnappingLayoutLabelLookup(LabelLookup fn)
{
    m_snappingLayoutLookup = std::move(fn);
}

void WindowRuleModel::setTilingAlgorithmLabelLookup(LabelLookup fn)
{
    m_tilingAlgorithmLookup = std::move(fn);
}

void WindowRuleModel::setShaderEffectLabelLookup(LabelLookup fn)
{
    m_shaderEffectLookup = std::move(fn);
}

void WindowRuleModel::setCurveLabelLookup(LabelLookup fn)
{
    m_curveLookup = std::move(fn);
}

void WindowRuleModel::refreshLabels()
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

QString WindowRuleModel::displayName(const PhosphorWindowRules::WindowRule& rule) const
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
    PhosphorWindowRules::ContextRuleBridge::contextDimsOf(rule.match, screenId, virtualDesktop, activity);
    if (rule.name == PhosphorWindowRules::ContextRuleBridge::contextRuleName(screenId, virtualDesktop, activity)) {
        return QString();
    }
    return rule.name;
}

} // namespace PlasmaZones
