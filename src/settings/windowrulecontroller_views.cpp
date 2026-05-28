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
        bool tilingEnabled = true;
        QString snappingLayout;
        QString tilingAlgorithm;
        // Mode the rule's `SetEngineMode` action selects (if any). Used to
        // disambiguate which layout name the overview tile should show
        // when a rule carries BOTH a snapping layout and a tiling
        // algorithm — the engine mode is the source of truth for which
        // engine actually runs.
        QString engineMode;
    };
    QHash<QString, Summary> byScreen;

    // Sort rules by descending priority before accumulation. Multiple
    // matching context-only rules on the same screen all contribute, but
    // the "first non-empty wins" guards below (s.engineMode.isEmpty(),
    // s.snappingLayout.isEmpty(), s.tilingAlgorithm.isEmpty()) mean the
    // FIRST rule visited pins each slot. Without sorting that's "first
    // in rule-iteration order"; with sorting it's "highest priority" —
    // which matches the daemon's own resolution order for the same rule
    // set. Sort an index vector rather than the rule list — WindowRule
    // carries the full match tree + actions, so copying the list to
    // sort it was O(N × tree-depth). Sorting ints stays O(N log N) in
    // ints regardless of rule complexity.
    const auto& rules = m_model.rules();
    QList<int> indices;
    indices.reserve(rules.size());
    for (int i = 0; i < rules.size(); ++i)
        indices.append(i);
    std::stable_sort(indices.begin(), indices.end(), [&rules](int a, int b) {
        return rules[a].priority > rules[b].priority;
    });

    for (int idx : indices) {
        const WindowRule& rule = rules[idx];
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
                if (a.type == ActionType::DisableEngine) {
                    s.tilingEnabled = false;
                } else if (a.type == ActionType::SetEngineMode && s.engineMode.isEmpty()) {
                    s.engineMode = a.params.value(QLatin1String("mode")).toString();
                } else if (a.type == ActionType::SetSnappingLayout && s.snappingLayout.isEmpty()) {
                    s.snappingLayout = a.params.value(QLatin1String("layoutId")).toString();
                } else if (a.type == ActionType::SetTilingAlgorithm && s.tilingAlgorithm.isEmpty()) {
                    s.tilingAlgorithm = a.params.value(QLatin1String("algorithm")).toString();
                }
            }
        }
    }

    QVariantList out;
    for (const QVariant& sv : screens) {
        const QVariantMap screen = sv.toMap();
        // Settings screen maps key the connector under "name" (and sometimes
        // "id"); accept either so the overview never silently drops a tile.
        QString screenId = screen.value(QStringLiteral("name")).toString();
        if (screenId.isEmpty()) {
            screenId = screen.value(QStringLiteral("id")).toString();
        }
        if (screenId.isEmpty()) {
            continue;
        }

        const auto it = byScreen.constFind(screenId);
        const bool assigned = it != byScreen.constEnd();
        const Summary summary = assigned ? *it : Summary{};

        QVariantMap tile;
        tile[QStringLiteral("screenId")] = screenId;
        // Pick the layout token that matches the rule's engine mode. When a
        // rule sets BOTH a snapping layout AND a tiling algorithm (legal —
        // they live in independent slots) the engine mode decides which
        // one is actually visible. Without an explicit engine mode we
        // prefer the snapping layout (the more common case) and fall back
        // to the algorithm only when no snapping layout is set AT ALL —
        // crucially we DON'T cross-mix kinds: when the engine mode is
        // "snapping" but no snapping layout was provided, we leave the
        // label empty rather than showing a misleading autotile name.
        QString layoutLabel;
        // Track WHICH lookup applies — split prevents a UUID-shaped
        // algorithm token from resolving via the snapping path (or a
        // tokenised layoutId via the tiling path) just because both
        // were wired to the same generic resolver.
        const WindowRuleModel::LabelLookup* labelLookup = nullptr;
        // Route the engineMode wire string through `modeFromWireString` so
        // every token the validator accepts (snapping / autotile / scrolling
        // — see `engineModeOptions()` in
        // libs/phosphor-windowrule/src/ruleaction.cpp) classifies correctly.
        // The previous inline `== "autotile"` / `== "snapping"` chain
        // silently collapsed `"scrolling"` into the "no engine pin → prefer
        // snapping layout" branch, which mis-labelled Scrolling-mode rules
        // with their leftover snapping layout. Same bug class as the
        // `entryFromRuleMatchActions` fix in libs/phosphor-zones.
        const auto mode = PhosphorZones::modeFromWireString(summary.engineMode);
        if (mode == PhosphorZones::AssignmentEntry::Autotile) {
            // Autotile engine pinned: only show tiling-algorithm tokens.
            layoutLabel = summary.tilingAlgorithm;
            labelLookup = &m_tilingAlgorithmLookup;
        } else if (mode == PhosphorZones::AssignmentEntry::Snapping) {
            // Snapping engine pinned: only show snapping-layout tokens.
            layoutLabel = summary.snappingLayout;
            labelLookup = &m_snappingLayoutLookup;
        } else if (mode == PhosphorZones::AssignmentEntry::Scrolling) {
            // Scrolling engine pinned: neither snapping layout nor tiling
            // algorithm applies — the rule's intent is "place via the
            // scrolling engine" and there is no layout/algorithm to label.
            // Leave layoutLabel empty so the tile renders the engine alone.
        } else if (!summary.snappingLayout.isEmpty()) {
            // No engine pin: prefer the snapping layout (more common).
            layoutLabel = summary.snappingLayout;
            labelLookup = &m_snappingLayoutLookup;
        } else {
            // Last resort: a tiling-algorithm-only rule with no engine pin.
            layoutLabel = summary.tilingAlgorithm;
            labelLookup = &m_tilingAlgorithmLookup;
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
        tile[QStringLiteral("tilingEnabled")] = summary.tilingEnabled;
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
    const QJsonValue actionsValue = obj.value(QLatin1String("actions"));
    if (actionsValue.isArray()) {
        for (const QJsonValue& v : actionsValue.toArray()) {
            if (!v.isObject()) {
                continue;
            }
            if (const auto action = RuleAction::fromJson(v.toObject())) {
                probe.actions.append(*action);
            }
            // Malformed actions are silently dropped — the editor will fail
            // its own structural gates (param fields empty etc.) before save.
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
