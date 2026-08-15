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

namespace {

// One walk, both directions. @p shouldFlip selects the entries to convert and
// @p target is the mode they are rewritten to; both layout fields are carried
// across verbatim, which is what lets the pair round-trip. Returns the flipped
// rules through @p updated and the (screen, desktop) pairs to announce through
// @p affected — dedup'ed, see purgeSnappingLayoutFromAssignments for the
// duplicate-emit hazard that guards against. The caller commits and emits,
// because the two directions differ in what else they persist.
//
// The rewrite replaces only the three assignment slots. A MIXED context rule
// (context match + SetEngineMode alongside SetOpacity / Float / Exclude /
// LockContext / OverrideAnimation* / gap actions) must still flip — a mixed
// autotile rule is a real autotile assignment and a global disable has to
// neuter it — so the predicate deliberately does NOT gate on
// isPureAssignmentRule the way the purge path at layoutregistry_assignments.cpp
// does. Assigning makeAssignmentActions' output wholesale would therefore drop
// every non-assignment action such a rule carries, in BOTH directions. Instead
// the fresh slot actions are followed by every surviving non-slot action, in
// their original order relative to each other (their position relative to the
// slot actions is NOT preserved — survivors always follow the rebuilt slots).
// Dropping a gap action here also changed the gap
// fingerprint the settings layer watches, which re-entered the settings handler
// mid-write; preserving the actions closes that re-entrancy exposure too.
template<typename PredicateFn>
int flipAssignmentModes(QList<PWR::Rule>& updated, QSet<QPair<QString, int>>& affected, AssignmentEntry::Mode target,
                        PredicateFn shouldFlip)
{
    int flipped = 0;
    for (PWR::Rule& rule : updated) {
        // Only context-assignment rules are flipped. A window-property rule's
        // SetEngineMode is a per-window intent, not a context assignment, so a
        // global autotile disable has no business neutering it.
        if (!isContextAssignmentRule(rule)) {
            continue;
        }
        const AssignmentEntry entry = entryFromRuleMatchActions(rule);
        if (!shouldFlip(entry)) {
            continue;
        }
        QList<PWR::RuleAction> rebuiltActions = PWR::ContextRuleBridge::makeAssignmentActions(
            modeToWireString(target), entry.snappingLayout, entry.tilingAlgorithm);
        for (const PWR::RuleAction& action : std::as_const(rule.actions)) {
            if (!isAssignmentSlotAction(action)) {
                rebuiltActions.append(action);
            }
        }
        rule.actions = rebuiltActions;
        ++flipped;

        // Recover (screen, desktop) for the layoutAssigned signal.
        const ContextDims dims = decodeDims(rule.match);
        affected.insert(qMakePair(dims.screenId, dims.virtualDesktop));
    }
    return flipped;
}

} // namespace

void LayoutRegistry::clearAutotileAssignments()
{
    // Flip every Autotile assignment rule to Snapping (preserving both layout
    // fields) and drop autotile quick-layout slots.
    QList<PWR::Rule> updated = m_ruleStore->ruleSet().rules();
    QSet<QPair<QString, int>> affected;

    // Flip to Snapping — preserve both layout fields so re-enabling
    // autotile can restore the previous algorithm.
    const int flipped =
        flipAssignmentModes(updated, affected, AssignmentEntry::Snapping, [](const AssignmentEntry& entry) {
            return entry.mode == AssignmentEntry::Autotile;
        });
    bool changed = flipped > 0;

    // Drop autotile quick-layout slots — clearing autotile everywhere
    // includes the per-mode autotile bindings. Snapping slots are untouched.
    // This is a ONE-WAY loss: restoreAutotileAssignments rebuilds the assignment
    // rules but has nothing to rebuild these from, and writeQuickLayouts()
    // persists the wipe immediately.
    auto& autotileSlots = m_quickLayoutSlots[modeIndex(AssignmentEntry::Autotile)];
    if (!autotileSlots.isEmpty()) {
        autotileSlots.clear();
        changed = true;
    }

    if (changed) {
        if (!m_ruleStore->setAllRules(updated)) {
            qCWarning(lcZonesLib) << "clearAutotileAssignments: rule store refused to persist the flipped assignments";
        }
        writeQuickLayouts();
        // Advisory emits: the layout pointer is always null, so these say "this
        // context moved, re-resolve it" rather than carrying the new layout.
        //
        // A context rule with no screen dimension decodes to an empty screenId
        // and is skipped: the per-screen subscribers cannot act on it, and the
        // LayoutAdaptor would relay a screenLayoutChanged("") onto the bus. The
        // screen-agnostic subscribers (drag adaptor, overlay refresh) therefore
        // see nothing when a screenless rule is the ONLY entry a flip touched,
        // which is what the settings-driven reconcile that follows covers.
        for (const auto& [sid, desk] : std::as_const(affected)) {
            if (sid.isEmpty()) {
                continue;
            }
            qCDebug(lcZonesLib) << "clearAutotileAssignments: flipped to Snapping for screen=" << sid
                                << "desktop=" << desk;
            Q_EMIT layoutAssigned(sid, desk, nullptr);
        }
        qCInfo(lcZonesLib) << "Cleared all autotile assignments";
    }
}

int LayoutRegistry::restoreAutotileAssignments()
{
    // Reverse direction of clearAutotileAssignments for the ASSIGNMENT RULES:
    // flip context-assignment rules back to Autotile where a previous global
    // disable neutered them. The quick-layout slots that disable wiped are not
    // recoverable and stay gone.
    //
    // The disable is global — it walks every context-assignment rule regardless
    // of desktop or activity — while the re-enable in the daemon only ever wrote
    // the CURRENT desktop per screen. A Settings off/on round trip therefore
    // stranded every other desktop and every activity-pinned context in Snapping
    // with no way back short of re-assigning each by hand. This closes that.
    //
    // The discriminator is a non-empty tilingAlgorithm on a Snapping entry, and
    // it means exactly one thing: this context ran autotile at some point. Both
    // assignLayout and purgeSnappingLayoutFromAssignments preserve the field, so
    // it survives a user manually switching the context back to Snapping just as
    // it survives the global disable. The restore therefore revives EVERY
    // context carrying algorithm memory, the hand-switched ones included. That
    // is the intended semantics of the global enable: autotile comes back
    // everywhere it has ever been configured, not only where the last disable
    // took it away.
    //
    // Note also that the daemon fires this on every entering-autotile edge, not
    // only after a disable — toggling snapping OFF while autotile is already on
    // is such an edge and revives the same set.
    //
    // snappingLayout is preserved on the way back, symmetric with the disable,
    // so a second disable returns each context to the same snap layout.
    QList<PWR::Rule> updated = m_ruleStore->ruleSet().rules();
    QSet<QPair<QString, int>> affected;

    const int restored =
        flipAssignmentModes(updated, affected, AssignmentEntry::Autotile, [](const AssignmentEntry& entry) {
            return entry.mode == AssignmentEntry::Snapping && !entry.tilingAlgorithm.isEmpty();
        });

    if (restored > 0) {
        if (!m_ruleStore->setAllRules(updated)) {
            qCWarning(lcZonesLib)
                << "restoreAutotileAssignments: rule store refused to persist the restored assignments";
        }
        // Advisory emits with a null layout pointer, and screenless context
        // rules skipped — see the sibling loop in clearAutotileAssignments.
        for (const auto& [sid, desk] : std::as_const(affected)) {
            if (sid.isEmpty()) {
                continue;
            }
            qCDebug(lcZonesLib) << "restoreAutotileAssignments: flipped back to Autotile for screen=" << sid
                                << "desktop=" << desk;
            Q_EMIT layoutAssigned(sid, desk, nullptr);
        }
        qCInfo(lcZonesLib) << "Restored" << restored << "autotile assignments";
    }
    return restored;
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
    // "apply all" call that runs a batch setter. Non-assignment actions a
    // mixed rule carries are captured too — the rebuild emits only the three
    // slot actions, and dropping the rest here was the last rebuild path
    // that still clobbered them (flipAssignmentModes and upsertAssignmentRule
    // both preserve survivors the same way).
    struct OldEntrySnapshot
    {
        AssignmentEntry entry;
        bool enabled = true;
        int priority = 0;
        QList<PWR::RuleAction> survivors;
    };
    QHash<KeyT, OldEntrySnapshot> oldEntries;
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const BatchContext ctx = decode(it.key());
        if (const PWR::Rule* existing = findExactContextRule(ctx.screenId, ctx.virtualDesktop, ctx.activity)) {
            QList<PWR::RuleAction> survivors;
            for (const PWR::RuleAction& action : existing->actions) {
                if (!isAssignmentSlotAction(action)) {
                    survivors.append(action);
                }
            }
            oldEntries.insert(
                it.key(),
                {entryFromRuleMatchActions(*existing), existing->enabled, existing->priority, std::move(survivors)});
        }
    }

    // Priority seed for newly CREATED assignments — a winning top value so a
    // fresh assignment outranks any prior one (the priority-wins model).
    // Computed from the live rule set before the family drop; each new rule
    // claims the next value so back-to-back creates don't collide. An UPDATE
    // preserves its snapshot priority instead.
    int seedPriority = nextAssignmentPriority(m_ruleStore->ruleSet().rules());

    // Step 2 — drop every rule belonging to this family; keep the rest.
    QList<PWR::Rule> kept;
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
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
        const bool hadOld = oldEntries.contains(it.key());
        const OldEntrySnapshot oldSnapshot = oldEntries.value(it.key());
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldSnapshot.entry);
        // UPDATE preserves the stored priority; CREATE claims the next winning
        // seed value.
        const int priority = hadOld ? oldSnapshot.priority : seedPriority++;
        PWR::Rule rebuilt = PWR::ContextRuleBridge::makeAssignmentRule(
            QString(), ctx.screenId, ctx.virtualDesktop, ctx.activity, modeToWireString(entry.mode),
            entry.snappingLayout, entry.tilingAlgorithm, priority);
        // Preserve the prior `enabled` flag — `makeAssignmentRule` always
        // stamps `enabled = true`. Mirrors the upsertAssignmentRule
        // precedent. If there's no prior snapshot (new assignment), the
        // default `enabled = true` from the OldEntrySnapshot ctor wins.
        rebuilt.enabled = oldSnapshot.enabled;
        rebuilt.actions.append(oldSnapshot.survivors);
        kept.append(rebuilt);
        storedScreens.insert(ctx.screenId);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << logContext;
    }

    // Step 4 — one commit, then signal per stored screen.
    //
    // The emit is UNCONDITIONAL, not value-changed-gated. What the signal
    // carries is the RESOLVED cascade value for the screen, and the only
    // per-key state captured before the family drop is each key's own stored
    // AssignmentEntry (Step 1) — not what the cascade resolved to. Gating would
    // mean a second full assignmentIdForScreen pass over every incoming key's
    // screen before the drop, purely to suppress a signal whose subscribers
    // (geometry recalc, daemon refresh) already no-op on an unchanged value. A
    // KCM "apply all" is the only caller, so the redundant emits are bounded by
    // the screen count of one save.
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
        int priority = 0;
        // Non-slot actions carried across the rebuild — see the
        // applyBatchAssignments snapshot for the clobber this closes.
        QList<PWR::RuleAction> survivors;
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
            QList<PWR::RuleAction> survivors;
            for (const PWR::RuleAction& action : existing->actions) {
                if (!isAssignmentSlotAction(action)) {
                    survivors.append(action);
                }
            }
            oldEntries.insert(
                key,
                {entryFromRuleMatchActions(*existing), existing->enabled, existing->priority, std::move(survivors)});
        }
    }

    // Priority seed for newly CREATED combined assignments (see
    // applyBatchAssignments for the rationale).
    int seedPriority = nextAssignmentPriority(m_ruleStore->ruleSet().rules());

    QList<PWR::Rule> kept;
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
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
        const bool hadOld = oldEntries.contains(key);
        const OldEntrySnapshot oldSnapshot = oldEntries.value(key);
        const AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldSnapshot.entry);
        const int priority = hadOld ? oldSnapshot.priority : seedPriority++;
        PWR::Rule rebuilt = PWR::ContextRuleBridge::makeAssignmentRule(
            QString(), key.screenId, key.virtualDesktop, key.activity, modeToWireString(entry.mode),
            entry.snappingLayout, entry.tilingAlgorithm, priority);
        rebuilt.enabled = oldSnapshot.enabled;
        rebuilt.actions.append(oldSnapshot.survivors);
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
    //
    // Only the keys that were STORED emit. A context deleted by omission (it was
    // in the family before this batch and is absent from the incoming map) is
    // dropped from the rule set with no layoutAssigned of its own. The daemon
    // still learns about it, through setAllRules → rulesChanged → the assignment
    // reconcile, so direct layoutAssigned subscribers must not treat this signal
    // as a complete record of which contexts a batch touched.
    for (const CombinedAssignmentKey& emitKey : std::as_const(emittedKeys)) {
        emitLayoutAssigned(emitKey.screenId, emitKey.virtualDesktop,
                           assignmentIdForScreen(emitKey.screenId, emitKey.virtualDesktop, emitKey.activity));
    }
    qCInfo(lcZonesLib) << "Batch set" << count << "combined assignments";
}

QHash<CombinedAssignmentKey, QString> LayoutRegistry::combinedAssignments() const
{
    QHash<CombinedAssignmentKey, QString> result;
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
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
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
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
    for (const PWR::Rule& rule : m_ruleStore->ruleSet().rules()) {
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
