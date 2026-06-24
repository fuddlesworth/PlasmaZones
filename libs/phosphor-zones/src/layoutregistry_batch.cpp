// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Bulk / batch layout-assignment operations — the multi-context "apply all"
// setters (per-screen, per-desktop, per-activity, combined), their shared
// driver, the round-trip projection accessors, and clearAutotileAssignments.
// Split from layoutregistry_assignments.cpp to keep each TU under the 800-line
// limit; the single-context resolution + mutation + query members stay there.

#include <PhosphorZones/LayoutRegistry.h>

#include "layoutregistry_rulehelpers_p.h"
#include "zoneslogging.h"

#include <PhosphorWindowRules/ContextRuleBridge.h>
#include <PhosphorWindowRules/RuleAction.h>
#include <PhosphorWindowRules/WindowRule.h>

#include <utility>

namespace PhosphorZones {

namespace PWR = PhosphorWindowRules;

using namespace RuleHelpers;

void LayoutRegistry::clearAutotileAssignments()
{
    // Flip every Autotile assignment rule to Snapping (preserving both layout
    // fields) and drop autotile quick-layout slots.
    QList<PWR::WindowRule> updated = m_ruleStore->ruleSet().rules();
    // Dedup (screenId, virtualDesktop) — see purgeSnappingLayoutFromAssignments
    // above for the duplicate-emit hazard this guards against.
    QSet<QPair<QString, int>> affected;
    bool changed = false;

    for (PWR::WindowRule& rule : updated) {
        // Only pure context-assignment rules are flipped — a window-property
        // rule that legitimately carries a SetEngineMode action must not be
        // rebuilt (makeAssignmentActions would drop its other actions).
        if (!isContextAssignmentRule(rule)) {
            continue;
        }
        const AssignmentEntry entry = entryFromRuleMatchActions(rule);
        if (entry.mode != AssignmentEntry::Autotile) {
            continue;
        }
        // Flip to Snapping — preserve both layout fields so re-enabling
        // autotile can restore the previous algorithm.
        rule.actions = PWR::ContextRuleBridge::makeAssignmentActions(modeToWireString(AssignmentEntry::Snapping),
                                                                     entry.snappingLayout, entry.tilingAlgorithm);
        changed = true;

        // Recover (screen, desktop) for the layoutAssigned signal.
        const ContextDims dims = decodeDims(rule.match);
        affected.insert(qMakePair(dims.screenId, dims.virtualDesktop));
    }

    // Drop autotile quick-layout slots — clearing autotile everywhere
    // includes the per-mode autotile bindings. Snapping slots are untouched.
    auto& autotileSlots = m_quickLayoutSlots[modeIndex(AssignmentEntry::Autotile)];
    if (!autotileSlots.isEmpty()) {
        autotileSlots.clear();
        changed = true;
    }

    if (changed) {
        m_ruleStore->setAllRules(updated);
        writeQuickLayouts();
        for (const auto& [sid, desk] : std::as_const(affected)) {
            qCDebug(lcZonesLib) << "clearAutotileAssignments: flipped to Snapping for screen=" << sid
                                << "desktop=" << desk;
            Q_EMIT layoutAssigned(sid, desk, nullptr);
        }
        qCInfo(lcZonesLib) << "Cleared all autotile assignments";
    }
}

// ── Batch setters ───────────────────────────────────────────────────────────

namespace {

// The decoded (screen, desktop, activity) cascade context a single
// batch-assignment hash key pins — each batch setter supplies a key→context
// decoder, the shared driver below works in terms of this tuple.
struct BatchContext
{
    QString screenId;
    int virtualDesktop = 0;
    QString activity;
};

} // anonymous namespace

// Shared driver for the three per-family batch setters — the numbered steps
// below replace the ~150 near-identical lines the setters used to inline.
// @p decode maps a hash key to its cascade context; @p valid rejects an
// ill-formed context (a desktop entry must pin desktop > 0, an activity
// entry a non-empty activity, else a wrong-family base rule is built);
// @p familyMatches selects which existing rules to drop; @p emitDesktop /
// @p emitActivity are the context the closing layoutAssigned is computed
// under (@p emitDesktop < 0 is the per-output-virtual-desktops sentinel, #648:
// each screen's own current desktop is resolved at emit time; 0 = base/any);
// @p label names the family in log output.
template<typename KeyT, typename DecodeFn, typename ValidFn, typename FamilyFn>
void LayoutRegistry::applyBatchAssignments(const QHash<KeyT, QString>& assignments, DecodeFn decode, ValidFn valid,
                                           FamilyFn familyMatches, int emitDesktop, const QString& emitActivity,
                                           const char* label)
{
    // Step 1 — snapshot existing exact-context entries AND their `enabled`
    // flag for every incoming key. The flag survives the batch rebuild
    // below — mirrors `upsertAssignmentRule`'s `rule.enabled =
    // existing->enabled` preservation. Without this capture, disabled
    // assignment rules silently flip back to enabled on any KCM
    // "apply all" call that runs a batch setter.
    struct OldEntrySnapshot
    {
        AssignmentEntry entry;
        bool enabled = true;
    };
    QHash<KeyT, OldEntrySnapshot> oldEntries;
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const BatchContext ctx = decode(it.key());
        if (const PWR::WindowRule* existing = findExactContextRule(ctx.screenId, ctx.virtualDesktop, ctx.activity)) {
            oldEntries.insert(it.key(), {entryFromRuleMatchActions(*existing), existing->enabled});
        }
    }

    // Step 2 — drop every rule belonging to this family; keep the rest.
    QList<PWR::WindowRule> kept;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (hasEngineModeAction(rule) && familyMatches(rule.match)) {
            continue;
        }
        kept.append(rule);
    }

    // Step 3 — rebuild the family from the incoming assignments.
    int count = 0;
    QSet<QString> storedScreens;
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const BatchContext ctx = decode(it.key());
        const QString& layoutId = it.value();
        if (!valid(ctx)) {
            qCWarning(lcZonesLib) << "Skipping invalid" << label << "assignment:" << ctx.screenId << ctx.virtualDesktop
                                  << ctx.activity;
            continue;
        }
        // logContext is the log-friendly identity string; the rule name itself
        // stays empty so the settings UI can render a friendly title from
        // resolved screen / activity labels rather than baking the raw ids in.
        const QString logContext = contextRuleName(ctx.screenId, ctx.virtualDesktop, ctx.activity);
        if (shouldSkipLayoutAssignment(layoutId, logContext)) {
            continue;
        }
        const OldEntrySnapshot oldSnapshot = oldEntries.value(it.key());
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldSnapshot.entry);
        PWR::WindowRule rebuilt = PWR::ContextRuleBridge::makeAssignmentRule(
            QString(), ctx.screenId, ctx.virtualDesktop, ctx.activity, modeToWireString(entry.mode),
            entry.snappingLayout, entry.tilingAlgorithm);
        // Preserve the prior `enabled` flag — `makeAssignmentRule` always
        // stamps `enabled = true`. Mirrors the upsertAssignmentRule
        // precedent. If there's no prior snapshot (new assignment), the
        // default `enabled = true` from the OldEntrySnapshot ctor wins.
        rebuilt.enabled = oldSnapshot.enabled;
        kept.append(rebuilt);
        storedScreens.insert(ctx.screenId);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << logContext;
    }

    // Step 4 — one commit, then signal per stored screen.
    m_ruleStore->setAllRules(kept);
    for (const QString& screenId : storedScreens) {
        // Per-output virtual desktops (#648): a desktop-family batch passes
        // emitDesktop < 0 so each screen refreshes against the desktop it is
        // actually showing, not a single global one. A concrete emitDesktop
        // (including 0 = base/any) is used verbatim.
        const int ed = emitDesktop < 0 ? currentVirtualDesktopForScreen(screenId) : emitDesktop;
        emitLayoutAssigned(screenId, ed, assignmentIdForScreen(screenId, ed, emitActivity));
    }
    qCInfo(lcZonesLib) << "Batch set" << count << label << "assignments";
}

void LayoutRegistry::setAllScreenAssignments(const QHash<QString, QString>& assignments)
{
    applyBatchAssignments(
        assignments,
        [](const QString& screenId) {
            return BatchContext{screenId, 0, QString()};
        },
        [](const BatchContext& ctx) {
            return !ctx.screenId.isEmpty();
        },
        matchIsExactContextBase,
        /*emitDesktop=*/0, /*emitActivity=*/QString(), "screen");
}

void LayoutRegistry::setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments)
{
    applyBatchAssignments(
        assignments,
        [](const QPair<QString, int>& key) {
            return BatchContext{key.first, key.second, QString()};
        },
        [](const BatchContext& ctx) {
            return !ctx.screenId.isEmpty() && ctx.virtualDesktop >= 1;
        },
        matchIsExactContextDesktop, /*emitDesktop=*/-1, m_currentActivity, "desktop");
}

void LayoutRegistry::setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments)
{
    // Use the STRICT per-activity classifier (Activity-only, no Combined) so
    // a `activityAssignments() → setAllActivityAssignments()` round-trip
    // doesn't silently drop the desktop pin on screen+desktop+activity
    // rules. Combined rules live outside the Activity batch API's
    // round-trip and are preserved untouched in the rule store.
    applyBatchAssignments(
        assignments,
        [](const QPair<QString, QString>& key) {
            return BatchContext{key.first, 0, key.second};
        },
        [](const BatchContext& ctx) {
            return !ctx.screenId.isEmpty() && !ctx.activity.isEmpty();
        },
        PWR::ContextRuleBridge::matchIsExactContextActivityStrict, /*emitDesktop=*/0, /*emitActivity=*/QString(),
        "activity");
}

void LayoutRegistry::setAllCombinedAssignments(const QHash<CombinedAssignmentKey, QString>& assignments)
{
    // Combined batch — sibling of the Activity / Desktop batches, kept
    // separate because the (screen, desktop, activity) emit context
    // differs per-rule (unlike Activity where every rebuilt rule lands
    // at desktop=0). The shape mirrors applyBatchAssignments step-by-step:
    // snapshot prior entries → drop the Combined family → rebuild → emit
    // per-(screen, desktop) layoutAssigned.

    struct OldEntrySnapshot
    {
        AssignmentEntry entry;
        bool enabled = true;
    };
    QHash<CombinedAssignmentKey, OldEntrySnapshot> oldEntries;
    // Validity gate up front: a malformed key (zero desktop or empty
    // activity) would otherwise route through findExactContextRule with
    // a degenerate (screen, 0, "") triple — the Monitor-axis canonical
    // shape — and could snapshot an unrelated Monitor rule's entry into
    // oldEntries[malformedKey]. The rebuild loop below rejects the same
    // malformed key, so the dead snapshot is harmless today, but the
    // mislabeled work is a footgun for a future refactor.
    const auto isValidCombinedKey = [](const CombinedAssignmentKey& key) {
        return !key.screenId.isEmpty() && key.virtualDesktop > 0 && !key.activity.isEmpty();
    };
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const CombinedAssignmentKey& key = it.key();
        if (!isValidCombinedKey(key)) {
            continue;
        }
        if (const PWR::WindowRule* existing = findExactContextRule(key.screenId, key.virtualDesktop, key.activity)) {
            oldEntries.insert(key, {entryFromRuleMatchActions(*existing), existing->enabled});
        }
    }

    QList<PWR::WindowRule> kept;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        if (hasEngineModeAction(rule) && PWR::ContextRuleBridge::matchIsExactContextCombined(rule.match)) {
            continue;
        }
        kept.append(rule);
    }

    int count = 0;
    // Per-(screen, desktop, activity) rule emit — multiple Combined rules
    // at the same (screen, desktop) but different activities each fire
    // their own layoutAssigned so observers can refresh against the
    // exact rule that landed.
    QSet<CombinedAssignmentKey> emittedKeys;
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const CombinedAssignmentKey& key = it.key();
        const QString& layoutId = it.value();
        if (!isValidCombinedKey(key)) {
            qCWarning(lcZonesLib) << "Skipping invalid combined assignment:" << key.screenId << key.virtualDesktop
                                  << key.activity;
            continue;
        }
        const QString logContext = contextRuleName(key.screenId, key.virtualDesktop, key.activity);
        if (shouldSkipLayoutAssignment(layoutId, logContext)) {
            continue;
        }
        const OldEntrySnapshot oldSnapshot = oldEntries.value(key);
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldSnapshot.entry);
        PWR::WindowRule rebuilt = PWR::ContextRuleBridge::makeAssignmentRule(
            QString(), key.screenId, key.virtualDesktop, key.activity, modeToWireString(entry.mode),
            entry.snappingLayout, entry.tilingAlgorithm);
        rebuilt.enabled = oldSnapshot.enabled;
        kept.append(rebuilt);
        emittedKeys.insert(key);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << logContext;
    }

    m_ruleStore->setAllRules(kept);
    // Per-rule emit using the rule's own (screen, desktop, activity) — the
    // earlier shape passed an empty activity to assignmentIdForScreen, which
    // never resolved to the just-stored Combined rule (Combined rules pin a
    // non-empty activity, so the empty-activity cascade query falls through
    // to a wider Desktop/Monitor rule). Observers would receive
    // layoutAssigned(screen, desktop, wrongLayoutPtr) for the just-stored
    // Combined rule. The signal still only carries (screenId, desktop,
    // layoutPtr), but the layoutPtr we resolve here is now the right one.
    for (const CombinedAssignmentKey& emitKey : std::as_const(emittedKeys)) {
        emitLayoutAssigned(emitKey.screenId, emitKey.virtualDesktop,
                           assignmentIdForScreen(emitKey.screenId, emitKey.virtualDesktop, emitKey.activity));
    }
    qCInfo(lcZonesLib) << "Batch set" << count << "combined assignments";
}

QHash<CombinedAssignmentKey, QString> LayoutRegistry::combinedAssignments() const
{
    QHash<CombinedAssignmentKey, QString> result;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        // Strict Combined-only classifier — Activity-only and Desktop-only
        // rules stay in their own projections.
        if (!hasEngineModeAction(rule) || !PWR::ContextRuleBridge::matchIsExactContextCombined(rule.match)) {
            continue;
        }
        const ContextDims dims = decodeDims(rule.match);
        result[CombinedAssignmentKey{dims.screenId, dims.virtualDesktop, dims.activity}] =
            entryFromRuleMatchActions(rule).activeLayoutId();
    }
    return result;
}

QHash<QPair<QString, int>, QString> LayoutRegistry::desktopAssignments() const
{
    QHash<QPair<QString, int>, QString> result;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        // Use the same per-desktop family classifier the batch setter uses,
        // so a window-property rule carrying an engine-mode action plus an
        // incidental VirtualDesktop== predicate cannot leak in.
        if (!hasEngineModeAction(rule) || !matchIsExactContextDesktop(rule.match)) {
            continue;
        }
        const ContextDims dims = decodeDims(rule.match);
        result[qMakePair(dims.screenId, dims.virtualDesktop)] = entryFromRuleMatchActions(rule).activeLayoutId();
    }
    return result;
}

QHash<QPair<QString, QString>, QString> LayoutRegistry::activityAssignments() const
{
    QHash<QPair<QString, QString>, QString> result;
    for (const PWR::WindowRule& rule : m_ruleStore->ruleSet().rules()) {
        // Use the STRICT per-activity classifier (Activity-only, no
        // Combined) so screen+desktop+activity rules are NOT projected
        // into (screen, activity) here — the resulting QHash would
        // overwrite a Combined entry with a pure-Activity entry (or
        // vice versa) keyed by the same pair, silently losing one of
        // the rules. Combined rules live outside this projection and
        // are only reachable through the rule editor.
        if (!hasEngineModeAction(rule) || !PWR::ContextRuleBridge::matchIsExactContextActivityStrict(rule.match)) {
            continue;
        }
        const ContextDims dims = decodeDims(rule.match);
        result[qMakePair(dims.screenId, dims.activity)] = entryFromRuleMatchActions(rule).activeLayoutId();
    }
    return result;
}

} // namespace PhosphorZones
