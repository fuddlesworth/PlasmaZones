// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Bulk / batch layout-assignment operations — the multi-context "apply all"
// setters (per-screen, per-desktop, per-activity, combined), their shared
// driver, the round-trip projection accessors, and clearAutotileAssignments.
// The single-context resolution + mutation + query members live in
// layoutregistry_assignments.cpp.

#include <PhosphorZones/LayoutRegistry.h>

#include "layoutregistry_rulehelpers_p.h"
#include "zoneslogging.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>

#include <utility>

namespace PhosphorZones {

namespace PWR = PhosphorRules;

using namespace RuleHelpers;

void LayoutRegistry::clearAutotileAssignments()
{
    // Flip every Autotile assignment rule to Snapping (preserving both layout
    // fields) and drop autotile quick-layout slots.
    QList<PWR::Rule> updated = m_ruleStore->ruleSet().rules();
    // `affected` collects the FULL context dims for the debug log; the EMIT
    // loop below dedupes on (screen, desktop) only. That collapse is
    // deliberate and safe: the payload resolves under m_currentActivity and
    // layoutAssigned carries no activity, so two rules differing only in
    // activity would produce byte-identical signals, each re-running the
    // daemon's full engine-screen re-derive. (An older shape resolved the
    // payload under each rule's OWN activity, and there the per-activity
    // dedupe key was load-bearing — that payload is gone.)
    QSet<ContextDims> affected;
    bool changed = false;

    for (PWR::Rule& rule : updated) {
        // Context-assignment rules only — a window-property rule that
        // legitimately carries a SetEngineMode action is not an assignment.
        // MIXED rules ARE flipped: a context assigned to autotile must be
        // cleared whether or not the user also hung a SetOpacity on it, and
        // gating them out left "clear all autotile assignments" silently
        // leaving such a context on autotile. Their extra actions survive
        // because the rebuild below carries them across.
        if (!isContextAssignmentRule(rule)) {
            continue;
        }
        const AssignmentEntry entry = entryFromRuleMatchActions(rule);
        if (entry.mode != AssignmentEntry::Autotile) {
            continue;
        }
        // Flip to Snapping — preserve both layout fields so re-enabling
        // autotile can restore the previous algorithm, and carry over
        // everything that is not an assignment slot.
        const PWR::Rule previous = rule;
        rule.actions = PWR::ContextRuleBridge::makeAssignmentActions(modeToWireString(AssignmentEntry::Snapping),
                                                                     entry.snappingLayout, entry.tilingAlgorithm,
                                                                     entry.scrollingTemplateLayout);
        carryOverNonAssignmentActions(rule, previous);
        changed = true;

        // Recover the rule's own context for the layoutAssigned signal.
        affected.insert(decodeDims(rule.match));
    }

    // Drop autotile quick-layout slots — clearing autotile everywhere
    // includes the per-mode autotile bindings. Snapping slots (shared with
    // Scrolling as the template vocabulary) are untouched.
    auto& autotileSlots = m_quickLayoutSlots[slotIndexFor(AssignmentEntry::Autotile)];
    if (!autotileSlots.isEmpty()) {
        autotileSlots.clear();
        changed = true;
    }

    if (changed) {
        m_ruleStore->setAllRules(updated);
        writeQuickLayouts();
        // The EMIT dedupes on (screen, desktop) only. The payload resolves
        // under m_currentActivity and layoutAssigned carries no activity, so
        // two rules differing only in activity would produce two BYTE-IDENTICAL
        // signals — and each one re-runs updateEngineScreens, updateLayoutFilter
        // and diffActiveAssignments in the daemon. `affected` keeps the full
        // dims for the debug log's sake.
        QSet<QPair<QString, int>> emitted;
        for (const ContextDims& dims : std::as_const(affected)) {
            qCDebug(lcZonesLib) << "clearAutotileAssignments: flipped to Snapping for screen=" << dims.screenId
                                << "desktop=" << dims.virtualDesktop << "activity=" << dims.activity;
            const auto emitKey = qMakePair(dims.screenId, dims.virtualDesktop);
            if (emitted.contains(emitKey)) {
                continue;
            }
            emitted.insert(emitKey);
            // Route through emitLayoutAssigned rather than a bare
            // layoutAssigned(…, nullptr): the flip PRESERVED each rule's
            // snapping layout, so observers should receive the layout the
            // context now resolves to, not a null pointer that reads as
            // "no layout here". The PAYLOAD resolves under the CURRENT
            // activity — layoutAssigned carries only (screen, desktop, layout)
            // and every consumer reads it as "this screen's layout now" —
            // requestRecalculate recomputes zone geometry from it, the KCM
            // renders it, the drag popup selects it. A rule pinned to a
            // non-current activity changes nothing on screen, so resolving
            // under its own activity would fan out a layout that is not live.
            emitLayoutAssigned(dims.screenId, dims.virtualDesktop,
                               assignmentIdForScreen(dims.screenId, dims.virtualDesktop, m_currentActivity));
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
// @p familyMatches selects which existing rules to drop; @p emitDesktop is
// the desktop the closing layoutAssigned is computed under (@p emitDesktop < 0
// is the per-output-virtual-desktops sentinel, #648: each screen's own current
// desktop is resolved at emit time; 0 = base/any). There is no matching
// activity parameter: the emit payload always resolves under m_currentActivity,
// because layoutAssigned carries no activity and its consumers read it as the
// screen's live layout. @p label names the family in log output.
template<typename KeyT, typename DecodeFn, typename ValidFn, typename FamilyFn>
void LayoutRegistry::applyBatchAssignments(const QHash<KeyT, QString>& assignments, DecodeFn decode, ValidFn valid,
                                           FamilyFn familyMatches, int emitDesktop, const char* label)
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
        int priority = 0;
        QUuid id;
        // Every action of the prior rule that is NOT an assignment slot.
        // makeAssignmentRule emits only the assignment slot actions and the rebuilt
        // rule carries the deterministic context id, so it lands on the stored
        // rule whether or not the family drop spared it — a wholesale replace
        // therefore destroys a mixed rule's extra actions even when the purity
        // gate deliberately kept the rule. Carried back in at rebuild time.
        QList<PWR::RuleAction> preservedActions;
        // The user-facing identity, preserved for the same reason
        // upsertAssignmentRule preserves it: makeAssignmentRule is handed an
        // empty name and stamps managed=false, so a batch "apply all" would
        // blank the name the user gave a context rule in the Rules editor and
        // clear the managed flag on a built-in, making it user-deletable.
        QString name;
        bool managed = false;
    };
    QHash<KeyT, OldEntrySnapshot> oldEntries;
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const BatchContext ctx = decode(it.key());
        // Same validity gate the rebuild loop applies (and the Combined
        // sibling applies up front): an ill-formed key would otherwise route
        // through findExactContextRule with a degenerate triple and snapshot
        // an unrelated rule's entry as this key's prior state.
        if (!valid(ctx)) {
            continue;
        }
        if (const PWR::Rule* existing = findExactContextRule(ctx.screenId, ctx.virtualDesktop, ctx.activity)) {
            PWR::Rule carrier;
            carryOverNonAssignmentActions(carrier, *existing);
            oldEntries.insert(it.key(),
                              {entryFromRuleMatchActions(*existing), existing->enabled, existing->priority,
                               existing->id, carrier.actions, existing->name, existing->managed});
        }
    }

    // Priority seed for newly CREATED assignments — a winning top value so a
    // fresh assignment outranks any prior one (the priority-wins model).
    // Computed from the live rule set before the family drop; each new rule
    // claims the next value so back-to-back creates don't collide. An UPDATE
    // preserves its snapshot priority instead.
    int seedPriority = nextAssignmentPriority(m_ruleStore->ruleSet().rules());

    // Step 2 — drop every rule belonging to this family; keep the rest.
    //
    // A snapshot rule that is NOT in the family SURVIVES this drop:
    // findExactContextRule's shape fallback also claims a LAYOUT-ONLY rule
    // (no SetEngineMode action), and the family predicate below spares
    // exactly that shape. Step 3 rebuilds such a rule under its own id and
    // replaces it in place via `keptIndexById`, mirroring
    // upsertAssignmentRule's reassign-id + updateRule path. Appending the
    // rebuild instead would leave the old rule in the set at the same
    // priority, where RuleEvaluator's list-order tie-break hands it the win
    // and a duplicate accumulates on every apply.
    QList<PWR::Rule> kept;
    QHash<QUuid, int> keptIndexById;
    // Contexts whose rules the family drop removed. Any that step 3 does not
    // rebuild still needs a layoutAssigned at step 4 — the erasure changed
    // what the context resolves to just as much as a rewrite does.
    QSet<ContextDims> droppedContexts;
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
        // isPureAssignmentRule as well: a MIXED context rule (context-only
        // match + SetEngineMode + a non-assignment action such as SetOpacity,
        // LockContext or an animation override) satisfies the two family tests
        // but is not this batch's to own. Dropping it deleted the rule
        // outright when the incoming batch had no key for that context, and
        // rebuilt it through makeAssignmentRule — which emits only the
        // assignment slot actions — when it did, silently stripping the user's other
        // actions. Both sibling paths (findExactContextRule's shape fallback
        // and purgeSnappingLayoutFromAssignments) guard the same way.
        if (hasEngineModeAction(rule) && familyMatches(rule.match) && isPureAssignmentRule(rule)) {
            const ContextDims dims = decodeDims(rule.match);
            droppedContexts.insert(dims);
            continue;
        }
        keptIndexById.insert(rule.id, kept.size());
        kept.append(rule);
    }

    // Step 3 — rebuild the family from the incoming assignments.
    int count = 0;
    QSet<ContextDims> storedContexts;
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
        const bool hadOld = oldEntries.contains(it.key());
        const OldEntrySnapshot oldSnapshot = oldEntries.value(it.key());
        // An EMPTY incoming id is NOT "no assignment here". It is what a
        // MODE-ONLY pin looks like on the wire: a Snapping assignment with no
        // explicit layout has an empty activeLayoutId(), so both the projection
        // readers (desktopAssignments / activityAssignments) and the KCM's own
        // per-screen map hand the empty string straight back on an "apply all".
        // Dropping the entry on that value would ERASE the pin, because step 2
        // already removed the whole family from `kept`. Rebuild from the prior
        // snapshot instead, carrying its mode and both layout fields through
        // untouched. With no prior rule there is genuinely nothing to preserve,
        // so an empty id is skipped exactly as before.
        if (layoutId.isEmpty()) {
            if (!hadOld) {
                continue;
            }
        } else if (shouldSkipLayoutAssignment(layoutId, logContext)) {
            continue;
        }
        const AssignmentEntry entry =
            layoutId.isEmpty() ? oldSnapshot.entry : AssignmentEntry::fromLayoutId(layoutId, oldSnapshot.entry);
        // UPDATE preserves the stored priority; CREATE claims the next winning
        // seed value.
        const int priority = hadOld ? oldSnapshot.priority : seedPriority++;
        PWR::Rule rebuilt = PWR::ContextRuleBridge::makeAssignmentRule(
            QString(), ctx.screenId, ctx.virtualDesktop, ctx.activity, modeToWireString(entry.mode),
            entry.snappingLayout, entry.tilingAlgorithm, priority, entry.scrollingTemplateLayout);
        // Preserve the prior `enabled` flag — `makeAssignmentRule` always
        // stamps `enabled = true`. Mirrors the upsertAssignmentRule
        // precedent. If there's no prior snapshot (new assignment), the
        // default `enabled = true` from the OldEntrySnapshot ctor wins.
        rebuilt.enabled = oldSnapshot.enabled;
        // Carry the prior rule's identity, then replace in place if that rule
        // survived the family drop — see the step-2 note. On a rule the drop
        // already removed the carry is inert: the id is either the
        // deterministic one makeAssignmentRule stamps anyway, or the
        // settings-UI uuid upsertAssignmentRule would have preserved too.
        if (hadOld && !oldSnapshot.id.isNull()) {
            rebuilt.id = oldSnapshot.id;
        }
        // Merge, never replace. The purity gate on the family drop SPARES a
        // mixed rule, but sparing it is not enough on its own: the rebuild
        // carries the same deterministic id, so the kept-index write below
        // would overwrite the spared rule wholesale and strip exactly the
        // actions the gate meant to protect. The user-facing identity rides
        // across for the same reason upsertAssignmentRule preserves it.
        rebuilt.actions += oldSnapshot.preservedActions;
        rebuilt.name = oldSnapshot.name;
        rebuilt.managed = oldSnapshot.managed;
        if (const auto keptIt = keptIndexById.constFind(rebuilt.id); keptIt != keptIndexById.cend()) {
            kept[*keptIt] = rebuilt;
        } else {
            keptIndexById.insert(rebuilt.id, kept.size());
            kept.append(rebuilt);
        }
        storedContexts.insert(ContextDims{ctx.screenId, ctx.virtualDesktop, ctx.activity});
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << logContext;
    }

    // Step 4 — one commit, then signal per affected (screen, desktop).
    m_ruleStore->setAllRules(kept);
    QSet<ContextDims> emitContexts;
    for (const ContextDims& stored : std::as_const(storedContexts)) {
        // Per-output virtual desktops (#648): a desktop-family batch passes
        // emitDesktop < 0 so each screen refreshes against the desktop it is
        // actually showing, not a single global one. A concrete emitDesktop
        // (including 0 = base/any) is used verbatim.
        const int ed = emitDesktop < 0 ? currentVirtualDesktopForScreen(stored.screenId) : emitDesktop;
        emitContexts.insert(ContextDims{stored.screenId, ed, stored.activity});
    }
    // Union in the erased-only contexts; the set dedupes any that a rebuild
    // already covers.
    emitContexts.unite(droppedContexts);
    // The PAYLOAD resolves under the CURRENT activity: layoutAssigned carries
    // no activity of its own and every consumer reads it as "this screen's
    // layout now", so resolving under a rule's non-current activity would hand
    // them a layout that is not on screen. Because the payload is therefore
    // activity-INDEPENDENT, the emit also dedupes on (screen, desktop) only —
    // otherwise N rules differing solely in activity fan out N byte-identical
    // signals, each re-running the daemon's full engine-screen re-derive.
    QSet<QPair<QString, int>> emitted;
    for (const ContextDims& ctx : std::as_const(emitContexts)) {
        // ctx.virtualDesktop is already the RESOLVED emit desktop (the #648
        // per-output sentinel was folded in when emitContexts was built).
        const auto emitKey = qMakePair(ctx.screenId, ctx.virtualDesktop);
        if (emitted.contains(emitKey)) {
            continue;
        }
        emitted.insert(emitKey);
        emitLayoutAssigned(ctx.screenId, ctx.virtualDesktop,
                           assignmentIdForScreen(ctx.screenId, ctx.virtualDesktop, m_currentActivity));
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
        /*emitDesktop=*/0, "screen");
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
        matchIsExactContextDesktop, /*emitDesktop=*/-1, "desktop");
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
        PWR::ContextRuleBridge::matchIsExactContextActivityStrict, /*emitDesktop=*/0, "activity");
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
        int priority = 0;
        QUuid id;
        // Every action of the prior rule that is NOT an assignment slot.
        // makeAssignmentRule emits only the assignment slot actions and the rebuilt
        // rule carries the deterministic context id, so it lands on the stored
        // rule whether or not the family drop spared it — a wholesale replace
        // therefore destroys a mixed rule's extra actions even when the purity
        // gate deliberately kept the rule. Carried back in at rebuild time.
        QList<PWR::RuleAction> preservedActions;
        // The user-facing identity, preserved for the same reason
        // upsertAssignmentRule preserves it: makeAssignmentRule is handed an
        // empty name and stamps managed=false, so a batch "apply all" would
        // blank the name the user gave a context rule in the Rules editor and
        // clear the managed flag on a built-in, making it user-deletable.
        QString name;
        bool managed = false;
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
        if (const PWR::Rule* existing = findExactContextRule(key.screenId, key.virtualDesktop, key.activity)) {
            PWR::Rule carrier;
            carryOverNonAssignmentActions(carrier, *existing);
            oldEntries.insert(key,
                              {entryFromRuleMatchActions(*existing), existing->enabled, existing->priority,
                               existing->id, carrier.actions, existing->name, existing->managed});
        }
    }

    // Priority seed for newly CREATED combined assignments (see
    // applyBatchAssignments for the rationale).
    int seedPriority = nextAssignmentPriority(m_ruleStore->ruleSet().rules());

    // Family drop, with the same three carve-outs applyBatchAssignments makes:
    // an index so a surviving layout-only snapshot rule is replaced in place
    // rather than shadowed by a same-priority duplicate, a record of the
    // erased contexts so a drop without a rebuild still signals, and the
    // isPureAssignmentRule gate so a MIXED rule (context match + SetEngineMode
    // + SetOpacity or LockContext) is left alone. Without that third gate the
    // rebuild below emits only the assignment slot actions and the user's extra
    // action is destroyed with no diagnostic.
    QList<PWR::Rule> kept;
    QHash<QUuid, int> keptIndexById;
    QSet<CombinedAssignmentKey> droppedKeys;
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
        if (hasEngineModeAction(rule) && PWR::ContextRuleBridge::matchIsExactContextCombined(rule.match)
            && isPureAssignmentRule(rule)) {
            const ContextDims dims = decodeDims(rule.match);
            droppedKeys.insert(CombinedAssignmentKey{dims.screenId, dims.virtualDesktop, dims.activity});
            continue;
        }
        keptIndexById.insert(rule.id, kept.size());
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
        const bool hadOld = oldEntries.contains(key);
        const OldEntrySnapshot oldSnapshot = oldEntries.value(key);
        // Empty id = mode-only pin round-tripping back in, not a deletion.
        // Same hazard and same handling as applyBatchAssignments — see the
        // full rationale there.
        if (layoutId.isEmpty()) {
            if (!hadOld) {
                continue;
            }
        } else if (shouldSkipLayoutAssignment(layoutId, logContext)) {
            continue;
        }
        const AssignmentEntry entry =
            layoutId.isEmpty() ? oldSnapshot.entry : AssignmentEntry::fromLayoutId(layoutId, oldSnapshot.entry);
        const int priority = hadOld ? oldSnapshot.priority : seedPriority++;
        PWR::Rule rebuilt = PWR::ContextRuleBridge::makeAssignmentRule(
            QString(), key.screenId, key.virtualDesktop, key.activity, modeToWireString(entry.mode),
            entry.snappingLayout, entry.tilingAlgorithm, priority, entry.scrollingTemplateLayout);
        rebuilt.enabled = oldSnapshot.enabled;
        if (hadOld && !oldSnapshot.id.isNull()) {
            rebuilt.id = oldSnapshot.id;
        }
        // Merge, never replace — identical reasoning to applyBatchAssignments.
        rebuilt.actions += oldSnapshot.preservedActions;
        rebuilt.name = oldSnapshot.name;
        rebuilt.managed = oldSnapshot.managed;
        if (const auto keptIt = keptIndexById.constFind(rebuilt.id); keptIt != keptIndexById.cend()) {
            kept[*keptIt] = rebuilt;
        } else {
            keptIndexById.insert(rebuilt.id, kept.size());
            kept.append(rebuilt);
        }
        emittedKeys.insert(key);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << logContext;
    }

    m_ruleStore->setAllRules(kept);
    // One emit per touched (screen, desktop, activity) key, so a Combined rule
    // is never collapsed into a sibling's emit. The PAYLOAD resolves under the
    // CURRENT activity: layoutAssigned carries only (screenId, desktop,
    // layoutPtr) and its consumers read that as the screen's live layout, so a
    // Combined rule pinned to a non-current activity must not fan its layout
    // out as though it were on screen.
    emittedKeys.unite(droppedKeys);
    // Deduped on (screen, desktop): the payload is activity-independent, so
    // several Combined keys on one screen+desktop would otherwise fan out
    // byte-identical signals, each re-running the daemon's engine-screen
    // re-derive. Same reasoning as applyBatchAssignments.
    QSet<QPair<QString, int>> emitted;
    for (const CombinedAssignmentKey& emitKey : std::as_const(emittedKeys)) {
        const auto dedupKey = qMakePair(emitKey.screenId, emitKey.virtualDesktop);
        if (emitted.contains(dedupKey)) {
            continue;
        }
        emitted.insert(dedupKey);
        emitLayoutAssigned(emitKey.screenId, emitKey.virtualDesktop,
                           assignmentIdForScreen(emitKey.screenId, emitKey.virtualDesktop, m_currentActivity));
    }
    qCInfo(lcZonesLib) << "Batch set" << count << "combined assignments";
}

// NOTE (shared by the three projections below): the value is the decoded
// AssignmentEntry — mode plus all three payload fields — so the D-Bus
// getters can expose the scrolling template beside the activeLayoutId()
// (which for a Scrolling context stays the bare "scrolling:" sentinel).
// The batch SETTERS remain id-string-keyed; the rebuild seeds the template
// from the stored entry, so the write round trip is lossless either way.
QHash<CombinedAssignmentKey, AssignmentEntry> LayoutRegistry::combinedAssignments() const
{
    QHash<CombinedAssignmentKey, AssignmentEntry> result;
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
        // Strict Combined-only classifier — Activity-only and Desktop-only
        // rules stay in their own projections.
        //
        // NO purity gate. A MIXED rule (context assignment plus, say,
        // SetOpacity) still assigns a layout to this context, and these three
        // projections are what the D-Bus assignment getters return to the
        // settings UI — gating them made such a context render as unassigned
        // while resolveAssignmentEntry, which is purity-blind, still resolved
        // its layout. The gate was compensating for a destructive write path;
        // the rebuild now merges non-assignment actions instead of replacing
        // them, so the round trip is lossless and the read can be honest.
        if (!hasEngineModeAction(rule) || !PWR::ContextRuleBridge::matchIsExactContextCombined(rule.match)) {
            continue;
        }
        const ContextDims dims = decodeDims(rule.match);
        result[CombinedAssignmentKey{dims.screenId, dims.virtualDesktop, dims.activity}] =
            entryFromRuleMatchActions(rule);
    }
    return result;
}

QHash<QPair<QString, int>, AssignmentEntry> LayoutRegistry::desktopAssignments() const
{
    QHash<QPair<QString, int>, AssignmentEntry> result;
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
        // Use the same per-desktop family classifier the batch setter uses,
        // so a window-property rule carrying an engine-mode action plus an
        // incidental VirtualDesktop== predicate cannot leak in.
        // No purity gate, for the same reason as combinedAssignments.
        if (!hasEngineModeAction(rule) || !matchIsExactContextDesktop(rule.match)) {
            continue;
        }
        const ContextDims dims = decodeDims(rule.match);
        result[qMakePair(dims.screenId, dims.virtualDesktop)] = entryFromRuleMatchActions(rule);
    }
    return result;
}

QHash<QPair<QString, QString>, AssignmentEntry> LayoutRegistry::activityAssignments() const
{
    QHash<QPair<QString, QString>, AssignmentEntry> result;
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
        // Use the STRICT per-activity classifier (Activity-only, no
        // Combined) so screen+desktop+activity rules are NOT projected
        // into (screen, activity) here — the resulting QHash would
        // overwrite a Combined entry with a pure-Activity entry (or
        // vice versa) keyed by the same pair, silently losing one of
        // the rules. Combined rules live outside this projection and
        // are only reachable through the rule editor.
        // No purity gate, for the same reason as combinedAssignments.
        if (!hasEngineModeAction(rule) || !PWR::ContextRuleBridge::matchIsExactContextActivityStrict(rule.match)) {
            continue;
        }
        const ContextDims dims = decodeDims(rule.match);
        result[qMakePair(dims.screenId, dims.activity)] = entryFromRuleMatchActions(rule);
    }
    return result;
}

} // namespace PhosphorZones
