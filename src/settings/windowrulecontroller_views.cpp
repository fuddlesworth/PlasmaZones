// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Read-only / projection methods of WindowRuleController:
//   * sections() — canonical display-order
//   * rulesSnapshot() — full QML view of every rule
//   * monitorOverview() — per-screen aggregate tile data
//   * ruleScreenIds() — screen-id list for a rule
//   * matchFields/operatorsForField/actionTypes/defaultPayloadFor — author surfaces
//   * validationIssuesForJson/matchIsContextOnly — editor validation hooks
//
// Split out of windowrulecontroller.cpp to keep that file under the
// 800-line cap (see CLAUDE.md). All methods are members of
// PlasmaZones::WindowRuleController and operate on its private model
// state — same class, separate translation unit, no API change.

#include "windowrulecontroller.h"
#include "windowruleauthoring.h"

#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorZones/AssignmentEntry.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace PlasmaZones {

namespace {

namespace ActionType = PhosphorWindowRule::ActionType;
using PhosphorWindowRule::MatchExpression;
using PhosphorWindowRule::RuleAction;
using PhosphorWindowRule::WindowRule;

} // namespace

QVariantList WindowRuleController::sections() const
{
    // Canonical display order — Monitor & Layout first, Advanced last. The
    // enum values are emitted as data so QML never hardcodes them.
    static const QList<WindowRuleModel::Section> kOrder = {
        WindowRuleModel::Section::Monitor,   WindowRuleModel::Section::Application, WindowRuleModel::Section::Activity,
        WindowRuleModel::Section::Animation, WindowRuleModel::Section::Advanced,
    };
    QVariantList out;
    for (WindowRuleModel::Section s : kOrder) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(s);
        entry[QStringLiteral("label")] = WindowRuleModel::sectionLabel(s);
        out.append(entry);
    }
    return out;
}

QVariantList WindowRuleController::rulesSnapshot() const
{
    // Read every field through the model's own data() + role enum so the
    // section / summary logic stays in exactly one place and QML never has to
    // reference raw `Qt.UserRole + N` integers.
    QVariantList out;
    const int n = m_model.rowCount();
    for (int i = 0; i < n; ++i) {
        const QModelIndex idx = m_model.index(i, 0);
        QVariantMap entry;
        entry[QStringLiteral("ruleId")] = m_model.data(idx, WindowRuleModel::IdRole);
        entry[QStringLiteral("name")] = m_model.data(idx, WindowRuleModel::NameRole);
        entry[QStringLiteral("enabled")] = m_model.data(idx, WindowRuleModel::EnabledRole);
        entry[QStringLiteral("priority")] = m_model.data(idx, WindowRuleModel::PriorityRole);
        entry[QStringLiteral("section")] =
            static_cast<int>(m_model.data(idx, WindowRuleModel::SectionRole).value<WindowRuleModel::Section>());
        entry[QStringLiteral("matchSummary")] = m_model.data(idx, WindowRuleModel::MatchSummaryRole);
        entry[QStringLiteral("actionSummary")] = m_model.data(idx, WindowRuleModel::ActionSummaryRole);
        entry[QStringLiteral("conditionCount")] = m_model.data(idx, WindowRuleModel::ConditionCountRole);
        entry[QStringLiteral("actionCount")] = m_model.data(idx, WindowRuleModel::ActionCountRole);
        entry[QStringLiteral("isComposite")] = m_model.data(idx, WindowRuleModel::IsCompositeRole);
        // ScreenIdsRole is computed in the model's data() — no per-row
        // by-id lookup, so the snapshot stays O(n).
        entry[QStringLiteral("screenIds")] = m_model.data(idx, WindowRuleModel::ScreenIdsRole);
        entry[QStringLiteral("validationIssueCount")] = m_model.data(idx, WindowRuleModel::ValidationIssueCountRole);
        out.append(entry);
    }
    return out;
}

QVariantList WindowRuleController::monitorOverview(const QVariantList& screens) const
{
    // Per-screen accumulator — built in a single pass over the rule set so the
    // total cost is O(rules × actions + screens) rather than O(screens × rules
    // × actions). A screen with no pinned rule simply never gets an entry and
    // falls through to the default "not assigned" tile below.
    //
    // Rebuilt on every QML rebind (no cache). Rule counts in practice are
    // O(10s), so the recompute cost is negligible — if a user with O(100s)
    // of rules surfaces, add a cache here invalidated on m_model::dataChanged
    // / rowsInserted / rowsRemoved / modelReset.
    struct Summary
    {
        int ruleCount = 0;
        // Wire-string engine token (or empty) the highest-priority
        // matching `DisableEngine` action on this screen targets.
        // Resolved against the screen's effective `engineMode` at
        // output time to compute the tile's `engineDisabled` flag —
        // a `DisableEngine{mode:"scrolling"}` rule on a Snapping-mode
        // screen must NOT flip the tile to "Engine off" (the active
        // engine is still Snapping). Stored as a scalar QString
        // (first-non-empty-wins below mirrors the daemon's per-slot
        // cascade winner; a QSet would over-state by accumulating
        // every matching rule).
        QString disabledEngineMode;
        // SetSnappingLayout and SetTilingAlgorithm both target ONE shared
        // `ActionSlot::Layout` in the daemon (ruleaction.cpp), filled first-wins
        // by priority — so the overview tracks a single Layout-slot winner (the
        // token plus whether it's a tiling algorithm) rather than two
        // independent slots, which would let it surface a lower-priority layout
        // the daemon's cascade discarded.
        QString layoutToken;
        bool layoutIsTiling = false;
        // Mode the rule's `SetEngineMode` action selects (if any). The tile
        // shows the Layout-slot token only when its kind matches the active
        // engine (a snapping engine can't render a tiling-algorithm token).
        QString engineMode;
    };
    QHash<QString, Summary> byScreen;

    // Sort rules by descending priority before accumulation. Multiple
    // matching enabled context-only rules on the same screen all contribute,
    // but the "first non-empty wins" guards below (s.engineMode.isEmpty(),
    // s.disabledEngineMode.isEmpty(), s.layoutToken.isEmpty()) mean the
    // FIRST rule visited pins each slot. Without sorting that's "first
    // in rule-iteration order"; with sorting it's "highest priority" —
    // which matches the daemon's own resolution order for the same rule
    // set. Sort an index vector rather than the rule list — WindowRule
    // carries the full match tree + actions, so copying the list to
    // sort it was O(N × tree-depth). Sorting ints stays O(N log N) in
    // ints regardless of rule complexity.
    const auto& rules = m_model.rules();
    QList<qsizetype> indices;
    indices.reserve(rules.size());
    for (qsizetype i = 0; i < rules.size(); ++i)
        indices.append(i);
    std::stable_sort(indices.begin(), indices.end(), [&rules](qsizetype a, qsizetype b) {
        return rules[a].priority > rules[b].priority;
    });

    for (qsizetype idx : indices) {
        const WindowRule& rule = rules[idx];
        // Disabled rules never apply — mirror RuleEvaluator::resolve, which
        // skips !enabled before filling any slot — so the overview reflects the
        // effective (enabled-only) per-monitor state rather than inflating
        // ruleCount or labelling a tile from a rule the daemon ignores.
        if (!rule.enabled) {
            continue;
        }
        // Only context-only rules that pin a monitor count toward a tile.
        if (!rule.match.isContextOnly()) {
            continue;
        }
        const QStringList screenIds = WindowRuleModel::screenIdsOf(rule.match);
        if (screenIds.isEmpty()) {
            continue;
        }
        for (const QString& screenId : screenIds) {
            Summary& s = byScreen[screenId];
            ++s.ruleCount;
            for (const RuleAction& a : rule.actions) {
                if (a.type == ActionType::DisableEngine && s.disabledEngineMode.isEmpty()) {
                    // First-non-empty wins, mirroring the daemon's per-slot
                    // cascade resolution: `RuleEvaluator::highestPriorityMatch`
                    // selects ONE winner per slot, and the `engine-enable`
                    // slot's winning rule is the only one consulted for
                    // DisableEngine. Indices here are pre-sorted priority-DESC,
                    // so the first DisableEngine action seen IS the cascade
                    // winner. Output-time resolution against the active mode
                    // still prevents a Snapping-disable rule from labelling
                    // an Autotile-mode screen as "Engine off".
                    s.disabledEngineMode = a.params.value(PhosphorWindowRule::ActionParam::Mode).toString();
                } else if (a.type == ActionType::SetEngineMode && s.engineMode.isEmpty()) {
                    s.engineMode = a.params.value(PhosphorWindowRule::ActionParam::Mode).toString();
                } else if ((a.type == ActionType::SetSnappingLayout || a.type == ActionType::SetTilingAlgorithm)
                           && s.layoutToken.isEmpty()) {
                    // One shared Layout slot, first-wins by priority — record
                    // the winning token and its kind.
                    s.layoutIsTiling = a.type == ActionType::SetTilingAlgorithm;
                    s.layoutToken = a.params
                                        .value(s.layoutIsTiling ? PhosphorWindowRule::ActionParam::Algorithm
                                                                : PhosphorWindowRule::ActionParam::LayoutId)
                                        .toString();
                }
            }
        }
    }

    QVariantList out;
    for (const QVariant& sv : screens) {
        const QVariantMap screen = sv.toMap();
        // SettingsController::screens maps (screenInfoListToVariantList) key
        // the connector under "name" (always present) with "screenId" (EDID id)
        // as the alternate; fall back to it so the overview never silently
        // drops a tile whose name happens to be empty.
        QString screenId = screen.value(QStringLiteral("name")).toString();
        if (screenId.isEmpty()) {
            screenId = screen.value(QStringLiteral("screenId")).toString();
        }
        if (screenId.isEmpty()) {
            continue;
        }

        const auto it = byScreen.constFind(screenId);
        const bool assigned = it != byScreen.constEnd();
        const Summary summary = assigned ? *it : Summary{};

        QVariantMap tile;
        tile[QStringLiteral("screenId")] = screenId;
        // The daemon keeps ONE Layout-slot winner (first by priority). Show it
        // only when its kind matches the active engine — a snapping engine
        // can't render a tiling-algorithm token and vice versa — so a
        // lower-priority layout the cascade discarded never resurfaces, and a
        // mismatched-kind winner renders the engine alone rather than a
        // misleading name. An unset engine mode resolves to Snapping (the
        // cascade default the engineDisabled check below also applies), so a
        // bare layout/algorithm rule with no SetEngineMode is judged against
        // Snapping. Route the wire string through `modeFromWireString` so every
        // validator token (snapping / autotile / scrolling — see
        // `engineModeOptions()` in libs/phosphor-windowrule/src/ruleaction.cpp)
        // classifies correctly.
        QString layoutLabel;
        // Track WHICH lookup applies — split prevents a UUID-shaped algorithm
        // token from resolving via the snapping path (or a tokenised layoutId
        // via the tiling path) just because both were wired to one resolver.
        const WindowRuleModel::LabelLookup* labelLookup = nullptr;
        const std::optional<PhosphorZones::AssignmentEntry::Mode> mode = summary.engineMode.isEmpty()
            ? std::optional<PhosphorZones::AssignmentEntry::Mode>(PhosphorZones::AssignmentEntry::Snapping)
            : PhosphorZones::modeFromWireString(summary.engineMode);
        if (mode != PhosphorZones::AssignmentEntry::Scrolling && !summary.layoutToken.isEmpty()) {
            // Scrolling pins no layout; otherwise show the slot winner when its
            // kind matches the engine. A nullopt mode (an unrecognised non-empty
            // token) falls through to showing the winner.
            const bool kindMatches = mode == PhosphorZones::AssignmentEntry::Autotile ? summary.layoutIsTiling
                : mode == PhosphorZones::AssignmentEntry::Snapping                    ? !summary.layoutIsTiling
                                                                                      : true;
            if (kindMatches) {
                layoutLabel = summary.layoutToken;
                labelLookup = summary.layoutIsTiling ? &m_tilingAlgorithmLookup : &m_snappingLayoutLookup;
            }
        }
        // The token is the raw layoutId / algorithm name from the rule's
        // action params — resolve it to a user-facing label when a lookup
        // is wired so the tile reads "BSP" instead of "{25828c9b-…}".
        if (labelLookup && *labelLookup && !layoutLabel.isEmpty()) {
            const QString resolved = (*labelLookup)(layoutLabel);
            if (!resolved.isEmpty()) {
                layoutLabel = resolved;
            }
        }
        tile[QStringLiteral("layoutName")] = layoutLabel;
        // Resolve `engineDisabled` against the screen's effective engine
        // mode. The accumulator collected every DisableEngine token any
        // matching rule targets; the tile reads "engine off" only when
        // a disable rule targets the engine the screen actually runs.
        // For an unset engineMode (no SetEngineMode rule) the screen
        // defaults to Snapping per the cascade — match against that
        // sentinel. The QML reads this as `tilingEnabled` (kept for
        // backwards-compatibility with the existing tile component);
        // the field's semantics are now "the engine running on this
        // screen is NOT disabled".
        const QString effectiveModeWire = summary.engineMode.isEmpty()
            ? PhosphorZones::modeToWireString(PhosphorZones::AssignmentEntry::Snapping)
            : summary.engineMode;
        const bool engineDisabled =
            !summary.disabledEngineMode.isEmpty() && summary.disabledEngineMode == effectiveModeWire;
        tile[QStringLiteral("tilingEnabled")] = !engineDisabled;
        tile[QStringLiteral("ruleCount")] = summary.ruleCount;
        tile[QStringLiteral("assigned")] = assigned;
        out.append(tile);
    }
    return out;
}

QStringList WindowRuleController::ruleScreenIds(const QString& ruleId) const
{
    const WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
    if (rule.id.isNull()) {
        return {};
    }
    return WindowRuleModel::screenIdsOf(rule.match);
}

QVariantList WindowRuleController::matchFields() const
{
    return WindowRuleAuthoring::matchFields();
}

QVariantList WindowRuleController::operatorsForField(int fieldValue) const
{
    return WindowRuleAuthoring::operatorsForField(fieldValue);
}

QVariantList WindowRuleController::actionTypes() const
{
    return WindowRuleAuthoring::actionTypes();
}

QVariantMap WindowRuleController::defaultPayloadFor(const QString& typeWire) const
{
    return WindowRuleAuthoring::defaultPayloadFor(typeWire);
}

QVariantList WindowRuleController::validationIssuesForJson(const QVariantMap& ruleJson) const
{
    // Build a partial rule from the variant map — enough to run the semantic
    // compatibility check without requiring a full `WindowRule::fromJson`
    // (which would refuse a rule mid-edit: no id, no actions yet). The
    // validator only consults `match` and `actions`, so reconstruct just those.
    const QJsonObject obj = QJsonObject::fromVariantMap(ruleJson);

    WindowRule probe;
    const QJsonValue matchValue = obj.value(QLatin1String("match"));
    if (matchValue.isObject()) {
        if (const auto match = MatchExpression::fromJson(matchValue.toObject())) {
            probe.match = *match;
        }
        // A malformed match leaves `probe.match` as the default catch-all,
        // which is context-only — so the check still works as the user fills
        // out leaves: the issue only surfaces once a window-property leaf
        // lands AND a context action is present.
    }
    // Preserve every original action slot — including malformed entries
    // and non-object JSON values — as a placeholder `RuleAction{}` so
    // `probe.actions` index N corresponds to ruleJson.actions[N] in the
    // editor. The previous shape `continue`'d past invalid entries,
    // which made the validator's `issue.actionIndex` point at the wrong
    // QML editor row (every malformed entry above an issue shifted
    // subsequent indices down by one). The placeholder's empty type
    // maps to the default `Window` domain via `ActionRegistry::domainFor`'s
    // unregistered-type fallback, so the validator's only check
    // (`domain == Context && !matchIsContextOnly`) never trips on it —
    // no spurious issue is recorded against the placeholder slot.
    const QJsonValue actionsValue = obj.value(QLatin1String("actions"));
    if (actionsValue.isArray()) {
        for (const QJsonValue& v : actionsValue.toArray()) {
            if (v.isObject()) {
                if (const auto action = RuleAction::fromJson(v.toObject())) {
                    probe.actions.append(*action);
                    continue;
                }
            }
            // Malformed action (non-object JSON or descriptor-rejected
            // payload) — preserve a placeholder so index alignment with
            // the editor's actions array stays intact.
            probe.actions.append(RuleAction{});
        }
    }

    QVariantList out;
    for (const PhosphorWindowRule::ValidationIssue& issue : probe.validationIssues()) {
        QVariantMap m;
        m[QStringLiteral("code")] = static_cast<int>(issue.code);
        m[QStringLiteral("actionIndex")] = issue.actionIndex;
        m[QStringLiteral("actionType")] = issue.actionType;
        m[QStringLiteral("message")] = issue.message;
        out.append(m);
    }
    return out;
}

bool WindowRuleController::matchIsContextOnly(const QVariantMap& matchJson) const
{
    // An empty / unparseable match collapses to the default catch-all, which
    // is context-only by definition (no leaves to fail against a context
    // query). The picker treats this as "every action type compatible" so the
    // user can start with any action and add window predicates afterwards.
    const QJsonObject obj = QJsonObject::fromVariantMap(matchJson);
    if (obj.isEmpty()) {
        return true;
    }
    const auto match = MatchExpression::fromJson(obj);
    if (!match) {
        return true;
    }
    return match->isContextOnly();
}

} // namespace PlasmaZones
