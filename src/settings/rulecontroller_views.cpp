// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Read-only / projection methods of RuleController:
//   * sections() — canonical display-order
//   * rulesSnapshot() — full QML view of every rule
//   * monitorOverview() — per-screen aggregate tile data
//   * matchFields/operatorsForField/actionTypes/defaultPayloadFor — author surfaces
//   * validationIssuesForJson/matchIsContextOnly — editor validation hooks
//
// All methods are members of PlasmaZones::RuleController and operate on its
// private model state. Same class as rulecontroller.cpp, separate translation
// unit, no API change.

#include "rulecontroller.h"
#include "ruleauthoring.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorZones/AssignmentEntry.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace PlasmaZones {

namespace {

namespace ActionType = PhosphorRules::ActionType;
using PhosphorRules::MatchExpression;
using PhosphorRules::Rule;
using PhosphorRules::RuleAction;

} // namespace

QVariantList RuleController::sections() const
{
    // Canonical display order — Monitor & Layout first, then the user-authored
    // sections, then the app-managed System defaults last. The enum values are
    // emitted as data so QML never hardcodes them.
    static const QList<RuleModel::Section> kOrder = {
        RuleModel::Section::Monitor,   RuleModel::Section::Application, RuleModel::Section::Activity,
        RuleModel::Section::Animation, RuleModel::Section::Advanced,    RuleModel::Section::System,
    };
    QVariantList out;
    for (RuleModel::Section s : kOrder) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(s);
        entry[QStringLiteral("label")] = RuleModel::sectionLabel(s);
        out.append(entry);
    }
    return out;
}

QVariantList RuleController::rulesSnapshot() const
{
    // Read every field through the model's own data() + role enum so the
    // section / summary logic stays in exactly one place and QML never has to
    // reference raw `Qt.UserRole + N` integers.
    QVariantList out;
    const int n = m_model.rowCount();
    for (int i = 0; i < n; ++i) {
        const QModelIndex idx = m_model.index(i, 0);
        QVariantMap entry;
        entry[QStringLiteral("ruleId")] = m_model.data(idx, RuleModel::IdRole);
        entry[QStringLiteral("name")] = m_model.data(idx, RuleModel::NameRole);
        entry[QStringLiteral("enabled")] = m_model.data(idx, RuleModel::EnabledRole);
        entry[QStringLiteral("priority")] = m_model.data(idx, RuleModel::PriorityRole);
        const auto section = m_model.data(idx, RuleModel::SectionRole).value<RuleModel::Section>();
        entry[QStringLiteral("section")] = static_cast<int>(section);
        // Localized section name, for the per-row badge the flat list shows so
        // category stays legible without grouping.
        entry[QStringLiteral("sectionLabel")] = RuleModel::sectionLabel(section);
        entry[QStringLiteral("matchSummary")] = m_model.data(idx, RuleModel::MatchSummaryRole);
        entry[QStringLiteral("actionSummary")] = m_model.data(idx, RuleModel::ActionSummaryRole);
        entry[QStringLiteral("conditionCount")] = m_model.data(idx, RuleModel::ConditionCountRole);
        entry[QStringLiteral("actionCount")] = m_model.data(idx, RuleModel::ActionCountRole);
        entry[QStringLiteral("isComposite")] = m_model.data(idx, RuleModel::IsCompositeRole);
        // ScreenIdsRole is computed in the model's data() — no per-row
        // by-id lookup, so the snapshot stays O(n).
        entry[QStringLiteral("screenIds")] = m_model.data(idx, RuleModel::ScreenIdsRole);
        entry[QStringLiteral("validationIssueCount")] = m_model.data(idx, RuleModel::ValidationIssueCountRole);
        entry[QStringLiteral("managed")] = m_model.data(idx, RuleModel::ManagedRole);
        out.append(entry);
    }
    return out;
}

QVariantList RuleController::monitorOverview(const QVariantList& screens) const
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
        // Wire-string engine tokens from EVERY monitor-axis `DisableEngine`
        // rule on this screen. Resolved against the screen's effective
        // `engineMode` at output time to compute the tile's `engineDisabled`
        // flag — a `DisableEngine{mode:"scrolling"}` rule on a Snapping-mode
        // screen must NOT flip the tile to "Engine off" (the active engine is
        // still Snapping).
        //
        // A SET, not a scalar, because the daemon's disable check is a per-mode
        // UNION and not a single-winner slot: `Settings::disableEntriesFor`
        // scans the whole rule store and keeps every disable rule whose token
        // equals the mode it was asked about, so priority plays no part. One
        // screen can carry a separate disable rule per engine — `disableRuleIdFor`
        // keys the engine into the rule's identity precisely so those are
        // distinct rules — and every disable rule is authored at the same
        // `kContextBandBase` priority. A scalar first-wins would therefore let a
        // disable for one engine mask the disable for the engine the screen
        // actually runs, on nothing better than store order.
        QSet<QString> disabledEngineModes;
        // Engine mode + BOTH layout tokens come from ONE rule: the highest-
        // priority rule on this screen carrying a SetEngineMode action — the
        // daemon's per-screen assignment winner. LayoutRegistry::
        // resolveContextAssignment picks it via highestPriorityMatch filtered to
        // `hasEngineModeAction(rule) && !isCatchAll`, then entryFromRuleMatchActions
        // reads the whole entry from that one rule, keeping BOTH layout tokens so
        // the active mode picks which applies. A bare layout rule with NO
        // SetEngineMode is never that winner, so the daemon never applies its
        // layout — and neither does the tile. Tracking engineMode and the layout
        // tokens as independent per-slot first-wins (the previous approach) would
        // compose an engine from one rule with a layout from another, or surface
        // a bare-layout rule's token the assignment cascade discards.
        bool assignmentResolved = false;
        QString engineMode;
        QString snappingLayout;
        QString tilingAlgorithm;
        // Layout-lock state from the highest-priority matching `LockContext`
        // rule on this screen. `lockResolved` is the first-wins guard (indices
        // are priority-DESC, so the first LockContext rule seen is the winner of
        // the daemon's single-winner Locked slot); `locked` is that rule's
        // boolean value — a higher-priority `value:false` rule reports unlocked
        // even when a lower one says lock, mirroring `resolveContextLocked`.
        // Like the engine/disable slots above, this does NOT model terminal-
        // action (Exclude) cascade termination — but Exclude is window-domain
        // and these accumulators only see context-only rules, which never carry
        // it, so the omission is unreachable here.
        bool lockResolved = false;
        bool locked = false;
    };
    QHash<QString, Summary> byScreen;

    // Sort rules by descending priority before accumulation. Multiple
    // matching enabled context-only rules on the same screen all contribute to
    // ruleCount, but the "first wins" guards below (s.assignmentResolved for the
    // engine-mode assignment winner, s.lockResolved for the Locked slot) mean
    // the FIRST rule visited pins each. Without sorting that's
    // "first in rule-iteration order"; with sorting it's "highest priority" —
    // which matches the daemon's own resolution order for the same rule
    // set. Sort an index vector rather than the rule list — Rule
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
        const Rule& rule = rules[idx];
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
        const QStringList screenIds = RuleModel::screenIdsOf(rule.match);
        if (screenIds.isEmpty()) {
            continue;
        }
        // The daemon does NOT run a DisableEngine rule through RuleEvaluator.
        // A monitor is disabled iff its id appears in `Settings::disabledMonitors`
        // (isMonitorDisabled → contextDisabledReason), and that list is built by
        // `Settings::disableEntriesFor`, which admits a rule only when it is a
        // MONITOR-AXIS disable rule: exactly one DisableEngine action carrying a
        // non-empty mode token (ContextRuleBridge::disableRuleMode) plus a match
        // whose `contextAxisFor` is Monitor. That second half is exactly what
        // `matchIsExactContextBase` computes — both sides call the same bridge
        // helper, so the tile and the daemon's gate admit the same rule set by
        // construction rather than by two predicates that have to be kept in
        // step. Anything else is honoured on a narrower axis or not at all:
        //   - screen + desktop / screen + activity feed disabledDesktops /
        //     disabledActivities, which gate one desktop or activity, not the
        //     monitor. The tile is a monitor-level surface, so labelling the
        //     whole screen "Engine off" from one of those overstates it.
        //   - a match carrying a window-property leaf, or pinning a non-dimension
        //     context field (Mode / TiledWindowCount / ScreenOrientation /
        //     ActiveLayout), is not a bare monitor pin — contextAxisFor reports
        //     CatchAll for it and the daemon never gates the monitor on it.
        //   - a rule carrying two DisableEngine actions is ambiguous
        //     (disableRuleMode returns nullopt) and is not a managed disable
        //     entry at all.
        // Accumulating any DisableEngine action regardless of shape put "Engine
        // off" on tiles for rules the daemon does not honour that way.
        const auto disableMode = PhosphorRules::ContextRuleBridge::disableRuleMode(rule);
        const bool isMonitorDisableRule =
            disableMode.has_value() && PhosphorRules::ContextRuleBridge::matchIsExactContextBase(rule.match);

        for (const QString& screenId : screenIds) {
            Summary& s = byScreen[screenId];
            ++s.ruleCount;
            // DisableEngine: accumulate EVERY monitor-axis disable token this
            // screen carries — the daemon unions them per mode, so there is no
            // winner to pick and priority is irrelevant here (see the field's
            // doc). Output-time resolution against the active mode still stops a
            // Snapping-disable rule from labelling an Autotile screen "off".
            // Independent of the assignment winner below — a DisableEngine rule
            // need not carry a SetEngineMode action.
            if (isMonitorDisableRule) {
                s.disabledEngineModes.insert(*disableMode);
            }
            bool ruleHasEngineMode = false;
            for (const RuleAction& a : rule.actions) {
                if (a.type == ActionType::SetEngineMode)
                    ruleHasEngineMode = true;
                else if (a.type == ActionType::LockContext && !s.lockResolved) {
                    // First-wins on the single Locked slot (priority-DESC): the
                    // highest-priority LockContext rule decides, value and all.
                    s.lockResolved = true;
                    s.locked = a.params.value(PhosphorRules::ActionParam::Value).toBool();
                }
            }
            // Engine/layout: capture from the FIRST rule (highest priority) that
            // carries a SetEngineMode action — the assignment winner. Read its
            // mode AND both layout tokens together so the tile can never compose
            // a layout from a different rule than the engine, nor surface a bare
            // layout rule (no SetEngineMode) the daemon's assignment discards.
            if (!s.assignmentResolved && ruleHasEngineMode) {
                s.assignmentResolved = true;
                for (const RuleAction& a : rule.actions) {
                    if (a.type == ActionType::SetEngineMode)
                        s.engineMode = a.params.value(PhosphorRules::ActionParam::Mode).toString();
                    else if (a.type == ActionType::SetSnappingLayout)
                        s.snappingLayout = a.params.value(PhosphorRules::ActionParam::LayoutId).toString();
                    else if (a.type == ActionType::SetTilingAlgorithm)
                        s.tilingAlgorithm = a.params.value(PhosphorRules::ActionParam::Algorithm).toString();
                }
            }
        }
    }

    QVariantList out;
    for (const QVariant& sv : screens) {
        const QVariantMap screen = sv.toMap();
        // Rule matches pin an EDID screen id, so the tile has to be keyed by the
        // same thing. On the daemon-served path "name" IS that id:
        // ScreenProvider::fetchScreens fills ScreenInfo::name from the daemon's
        // `getScreens`, which returns effective screen identifiers (see
        // dbus/org.plasmazones.Screen.xml), and parks the real connector under a
        // separate "connectorName" key this lookup never reads. "screenId" is the
        // alternate; fall back to it so the overview never silently drops a tile
        // whose name happens to be empty.
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
        // Show the assignment winner's layout, picked by ITS engine mode — a
        // snapping engine shows the winner's snapping layout, an autotile engine
        // its algorithm, scrolling neither — mirroring how the daemon's
        // AssignmentEntry (which carries both tokens) is consumed. No assignment
        // winner (no rule with a SetEngineMode action) → no engine pin → no
        // layout label, so a bare layout rule the daemon's cascade discards never
        // resurfaces. modeFromWireString defaults an unrecognised token to
        // Snapping, matching entryFromRuleMatchActions.
        QString layoutLabel;
        // Track WHICH lookup applies — split prevents a UUID-shaped algorithm
        // token from resolving via the snapping path (or a tokenised layoutId
        // via the tiling path) just because both were wired to one resolver.
        const RuleModel::LabelLookup* labelLookup = nullptr;
        if (summary.assignmentResolved) {
            const auto mode = PhosphorZones::modeFromWireString(summary.engineMode)
                                  .value_or(PhosphorZones::AssignmentEntry::Snapping);
            if (mode == PhosphorZones::AssignmentEntry::Snapping) {
                layoutLabel = summary.snappingLayout;
                labelLookup = &m_snappingLayoutLookup;
            } else if (mode == PhosphorZones::AssignmentEntry::Autotile) {
                layoutLabel = summary.tilingAlgorithm;
                labelLookup = &m_tilingAlgorithmLookup;
            }
            // Scrolling: no layout/algorithm to label.
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
        const bool engineDisabled = summary.disabledEngineModes.contains(effectiveModeWire);
        tile[QStringLiteral("tilingEnabled")] = !engineDisabled;
        tile[QStringLiteral("ruleCount")] = summary.ruleCount;
        tile[QStringLiteral("assigned")] = assigned;
        // True when a LockContext rule pins this monitor's layout (the
        // highest-priority matching rule's value). The tile shows a lock badge.
        tile[QStringLiteral("locked")] = summary.locked;
        out.append(tile);
    }
    return out;
}

QVariantList RuleController::matchFields() const
{
    return RuleAuthoring::matchFields();
}

QVariantList RuleController::operatorsForField(int fieldValue) const
{
    return RuleAuthoring::operatorsForField(fieldValue);
}

QVariantList RuleController::allOperators() const
{
    return RuleAuthoring::allOperators();
}

QString RuleController::matchValueHint(const QString& op) const
{
    return RuleAuthoring::matchValueHint(op);
}

QVariantList RuleController::actionTypes() const
{
    return RuleAuthoring::actionTypes();
}

QVariantMap RuleController::defaultPayloadFor(const QString& typeWire) const
{
    return RuleAuthoring::defaultPayloadFor(typeWire);
}

QVariantList RuleController::validationIssuesForJson(const QVariantMap& ruleJson) const
{
    // Build a partial rule from the variant map — enough to run the semantic
    // compatibility check without requiring a full `Rule::fromJson`
    // (which would refuse a rule mid-edit: no id, no actions yet). The
    // validator only consults `match` and `actions`, so reconstruct just those.
    const QJsonObject obj = QJsonObject::fromVariantMap(ruleJson);

    Rule probe;
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
    for (const PhosphorRules::ValidationIssue& issue : probe.validationIssues()) {
        QVariantMap m;
        m[QStringLiteral("code")] = static_cast<int>(issue.code);
        m[QStringLiteral("actionIndex")] = issue.actionIndex;
        m[QStringLiteral("actionType")] = issue.actionType;
        m[QStringLiteral("message")] = issue.message;
        out.append(m);
    }
    return out;
}

bool RuleController::matchIsContextOnly(const QVariantMap& matchJson) const
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
