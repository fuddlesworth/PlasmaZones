<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Restore Defaults for Rule-Backed Appearance & Gaps — Design Document

| | |
|---|---|
| **Status** | Two tracks — **(A) per-page Reset+Discard for the Windows appearance page: designed & being implemented** (this session); **(B) global Restore Defaults daemon-side reset: still deferred** |
| **Author** | — |
| **Date** | 2026-06-28 (updated 2026-07-01) |
| **Schema impact** | None (no config or rules-file schema change) |
| **Origin** | Descoped item from the PR #699 audit ("everything is a rule" refactor) |

---

## 0. Update (2026-07-01) — chosen approach for the per-page feature

This section supersedes the framing below for the **per-page Reset/Discard** work
that is actually being built. Sections 1–9 remain the design for the **global**
Restore Defaults path (track B), which is still deferred.

### 0.1 Corrected premise: appearance edits are STAGED, not live

The original doc reasoned from "`window-appearance`'s commit is a no-op." That is
true only of `WindowAppearanceController` itself, which is stateless
(`windowappearancecontroller.h`: `isDirty()` returns false, `apply()`/`discard()`
are empty). It is **misleading**: appearance edits are NOT written live. Every
Windows-page control routes through `RuleController` (`WindowAppearancePage.qml`
binds `ruleController: settingsController.rulesPage`, reads `ruleJson(id)`, writes
`updateRuleFromJson(...)`). Those edits mutate the in-memory `RuleController::m_model`
and flip a dirty bit; nothing reaches the daemon's `rules.json` until the **global
Save** pushes the staged set via `setAllRules`. So the Windows page is already a
proper staged surface — it just happens to share one `RuleModel` with the Rules
page, and its edits currently mark the **`rules`** page dirty.

### 0.2 Scope decided this session

- **Windows (`window-appearance`)** → per-page **Reset + Discard**, staged (commits
  on Save, revertible before it), via the breadcrumb kebab.
- **Rules** → **nothing** (no per-page Reset/Discard — user rules are the page's
  content, not resettable "settings"; a full wipe would be a separate, explicit,
  destructive action).
- **Attribution** → split so appearance edits badge **Windows** and user-rule edits
  badge **Rules**.

### 0.3 Design (per-page, staged — track A)

Because both pages stage into one shared `RuleController::m_model`, and the existing
per-page Reset/Discard machinery is stage-only (mutates in-memory, defers to Save),
the Windows reset/discard operate on `m_model` — **not** the daemon-side live
primitive of §5 (that is for the global path, track B).

1. **Shared baseline definitions.** Move `makeBaselineSkeleton` +
   `makeBaseline{Border,TitleBar,Gap}Rule()` out of `daemon.cpp`'s anonymous
   namespace into `src/core/baselinerules.{h,cpp}` (part of `plasmazones_core`,
   which both the daemon and the settings app link, and which already links
   `PhosphorRules` + `PhosphorCompositor` (`DecorationDefaults`) + `ConfigDefaults`
   + `PhosphorI18n`). `daemon.cpp` includes it and drops its local copies —
   single source of truth for the seeding path and the reset path (matches §5.2).

2. **`RuleController` value-based split dirty (against a saved snapshot).** Keep the
   existing `m_dirty` bit for the async apply/revert machinery; add
   `m_savedRules = m_model.rules()` captured at each daemon sync (the `fetchAndLoad`
   reply after `setRules`, and the successful `pushToDaemonAsync` reply). Derive two
   value-based queries by comparing `m_model.rules()` to `m_savedRules`
   (`Rule::operator==` exists):
   - `baselinesDirty()` — any of the 3 managed baseline rules (by id) differs from
     the snapshot → drives **window-appearance** dirty.
   - `userRulesDirty()` — the ordered list of non-managed rules differs → drives
     **rules** dirty.

3. **Baseline-scoped ops on `RuleController`** (mutate `m_model` directly, then
   `setDirty(baselinesDirty() || userRulesDirty())` to recompute the global bit):
   - `resetBaselines()` → `m_model.updateRule(makeBaseline*Rule())` for the 3 ids
     (restores factory match + actions, dropping any per-side gap actions the page
     added — `updateRule` replaces the managed rule in place, no renormalize).
   - `discardBaselineEdits()` → `m_model.updateRule(savedRuleById(id))` for the 3
     ids, leaving user rules untouched.

4. **`SettingsController` wiring.** A `reconcileRuleBackedDirty()` helper sets
   `m_dirtyPages` membership for `window-appearance` (= `baselinesDirty()`) and
   `rules` (= `userRulesDirty()`), emitting `dirtyPagesChanged` on a change. It
   replaces the three existing `RuleController` handlers (`dirtyChanged` guarded by
   `!m_loading && !m_saving`, plus `revertFinished`/`applyResult`) that today mark
   only `"rules"`. `pageSupportsReset`/`pageSupportsDiscard` gain `window-appearance`;
   `resetPage`/`discardPage("window-appearance")` call the baseline ops then
   `reconcileRuleBackedDirty()` (explicit, because a scoped op that leaves the global
   bit unchanged would not re-fire `dirtyChanged`). `isPageDirty` is unchanged — it
   reads `m_dirtyPages`, which reconcile keeps accurate.

5. **No new D-Bus, no daemon primitive** for track A. Save/Discard already flow
   through the registered `RuleController` staging domain; Save pushes `setAllRules`,
   Discard runs `revert()`.

### 0.4 What track A does NOT do

The **global Restore Defaults** (`SettingsController::defaults()`) still does not
reset the rule-backed appearance/gaps baselines — it continues to exclude
`window-appearance` and `rules` from its blanket mark. Fixing that is track B
(§2–§9): a daemon-side, live-persisting reset primitive. The two are independent;
track A ships the per-page affordance without it.

---

## 1. Problem

Before the appearance/gaps-to-rules refactor, window border, title-bar, corner-radius,
border-colour, and gap values were KConfig-backed settings. The settings app's
**Restore Defaults** action reset them through `Settings::reset()`.

After the refactor those values live on three managed baseline **rules** in
`rules.json` (the daemon's `rules.json`, not the KConfig store). `Settings::reset()`
no longer touches them, so **Restore Defaults silently no longer restores window
appearance or gaps**. This is a behavioural regression relative to the pre-refactor
app.

The current code documents the gap explicitly. In `SettingsController::defaults()`
(`src/settings/settingscontroller_lifecycle.cpp`):

```cpp
// "rules" and "window-appearance" are INTENTIONALLY excluded from the
// blanket-mark: both are rule-backed ...
// Resetting rule-backed defaults (window appearance / gaps) is out of scope
// for this entry point — it requires a separate daemon-side "reset rules" path.
QSet<QString> fullSet = validPageNames();
fullSet.remove(QStringLiteral("rules"));
fullSet.remove(QStringLiteral("window-appearance"));
```

This document specifies that "separate daemon-side reset rules path".

## 2. Current state (what exists today)

- The daemon owns three **managed baseline rules**, seeded on startup by
  `ensureManagedRule()` in `Daemon` (`src/daemon/daemon.cpp`):
  - `makeBaselineBorderRule()` — id `ConfigDefaults::baselineBorderRuleId()`, carries
    `SetBorderVisible` (default `DecorationDefaults::ShowBorder`).
  - `makeBaselineTitleBarRule()` — id `baselineTitleBarRuleId()`, carries
    `SetHideTitleBar` (default `DecorationDefaults::HideTitleBars`).
  - `makeBaselineGapRule()` — id `baselineGapRuleId()`, carries `SetInnerGap`,
    `SetOuterGap`, `SetUsePerSideOuterGap` (from `ConfigDefaults::innerGap()` /
    `outerGap()` / `usePerSideOuterGap()`). The four per-side outer-gap actions are
    added/removed by the Appearance page on demand.
- These rules are catch-all (`All{}`), `managed = true`, pinned to lowest priority
  (`INT_MIN`). The settings **Window Appearance** page edits their action *values*;
  the gap rule's actions are also the source of truth for `Settings::innerGap()` /
  `outerGap*()`.
- `ensureManagedRule()` reconciles the **action set only**: it seeds a baseline when
  absent and appends missing action *types*, but deliberately does **not** re-pin an
  existing baseline's values, priority, match, or managed flag (so an Appearance-page
  edit survives a daemon restart). It is therefore **not** a reset.
- The settings app talks to the daemon's rule store over D-Bus through
  `org.plasmazones.Rules` (`RuleAdaptor`, hand-written, in `src/dbus/ruleadaptor.*`),
  which exposes `getAllRules` / `setAllRules` / `addRule` / `updateRule` /
  `removeRule` / `setRuleEnabled` / `setRulePriority` and a `rulesChanged(bool)`
  signal. The daemon's `RuleStore` is the sole writer of `rules.json`.
- Per-monitor gap overrides are ordinary (non-managed) `ScreenId`-pinned rules whose
  id is derived by `WindowAppearanceController::perScreenGapRuleId()`.

## 3. Goals / non-goals

**Goals**
- Restore Defaults resets the three managed baseline rules' action values to their
  factory defaults (the `makeBaseline*Rule()` definitions).
- The reset is daemon-driven (the daemon owns the baseline definitions), persists to
  `rules.json`, and emits `rulesChanged` so the effect and the settings UI refresh.
- The settings app's `defaults()` triggers it, and the Window Appearance page reflects
  the reset (no stale dirty badge — see §6).

**Non-goals**
- No config or rules-file schema-version change.
- Not a "delete every rule" / factory-wipe action (see §4 for user-rule handling).
- No new per-key migration code (this is a user action, not a schema migration).

## 4. Product decision — what Restore Defaults touches

The pre-refactor Restore Defaults reset the global per-mode appearance **settings**.
Those settings are now the three managed baselines. The faithful equivalent is:

- **Always reset:** the three managed baseline rules (`Default borders`,
  `Default title bars`, `Default gaps`) to their `makeBaseline*Rule()` values,
  including dropping any per-side outer-gap actions the Appearance page added.

The open question is what to do with **user-authored override rules** (per-app
assignment rules, per-monitor gap overrides, per-mode appearance overrides, animation
rules, exclusions). Two candidate policies:

- **Policy A — baselines only (recommended).** Reset only the three managed baselines;
  leave every user-authored rule untouched. This matches how Restore Defaults already
  treats the Rules page (it does not delete user rules) and is the least destructive.
  Per-monitor gap overrides and per-app rules survive, exactly as other user rules do.
- **Policy B — baselines + a separate explicit "clear all rules".** Keep Policy A for
  Restore Defaults, and add a distinct, separately-confirmed "Remove all rules" action
  on the Rules page for users who want a full wipe. Restore Defaults never deletes user
  rules.

This document assumes **Policy A** for the Restore Defaults path. Policy B's "clear all
rules" is orthogonal and can ship independently.

## 5. Design

### 5.1 Daemon-side reset primitive

Add a reset operation that re-pins the three managed baselines to their canonical
`makeBaseline*Rule()` definitions, **overwriting** their actions (unlike
`ensureManagedRule`, which appends only):

```cpp
// Daemon (pseudocode)
void Daemon::resetManagedAppearanceDefaults()
{
    for (const Rule& desired : { makeBaselineBorderRule(),
                                 makeBaselineTitleBarRule(),
                                 makeBaselineGapRule() }) {
        if (m_ruleStore->ruleSet().ruleById(desired.id))
            m_ruleStore->updateRule(desired);   // overwrite actions/values
        else
            m_ruleStore->addRule(desired);      // seed if somehow missing
    }
    // RuleStore persists + emits rulesChanged → effect + settings refresh.
}
```

`updateRule` already drives persistence and the `rulesChanged` broadcast, so the
effect re-resolves appearance and the settings UI reloads through the existing
reactive paths.

### 5.2 Sharing the baseline definitions

`makeBaseline*Rule()` currently live in an anonymous namespace in `daemon.cpp`. The
reset primitive needs them, and (depending on §5.3) so might `RuleAdaptor`. Move the
three makers (and `makeBaselineSkeleton`) into a small shared unit, e.g.
`src/daemon/baselinerules.{h,cpp}` (or a `PhosphorRules`-adjacent helper), so the
seeding path and the reset path share one source of truth. No behaviour change — just
relocation. This also keeps the daemon's startup seeding and the reset from drifting.

### 5.3 D-Bus surface

Two options for exposing the reset to the settings app:

- **Option 1 — extend `org.plasmazones.Rules`.** Add a `resetManagedDefaults()` slot
  to `RuleAdaptor`. Because `RuleAdaptor` holds only the `RuleStore` (not the baseline
  makers), wire a `std::function<void()>` reset callback from the daemon into the
  adaptor at construction (the daemon already injects late-bound dependencies and
  clears them in `detach()`), keeping the baseline definitions daemon-side. Update
  `dbus/org.plasmazones.Rules.xml` (install-only introspection doc) to match.
- **Option 2 — daemon Control surface.** If a control/settings adaptor already
  brokers settings→daemon commands, add `resetManagedAppearanceDefaults()` there
  instead, avoiding a callback indirection through `RuleAdaptor`.

Option 1 keeps the reset on the same interface as the rest of the rule API and is the
recommended default.

### 5.4 Settings wiring

In `SettingsController::defaults()`, after `m_settings.reset()`, invoke the reset over
D-Bus (fire-and-forget, like `DaemonDBus::notifyReload()`):

```cpp
// Reset the rule-backed appearance/gap baselines to factory values.
RulesDBus::resetManagedDefaults();
```

The `rulesChanged` broadcast then refreshes the Window Appearance page through the
existing `RuleController` / `WindowAppearanceController` reactive bindings, so the
page's controls show the restored defaults without a manual reload.

## 6. Edge cases & interactions

- **Dirty badge.** `defaults()` already excludes `window-appearance` from the
  blanket dirty-mark (its commit is a no-op). After this change the reset happens
  daemon-side and the page reloads from `rulesChanged`, so it stays clean — no stale
  "unsaved changes" badge. The exclusion remains correct; do not re-add the page to
  the blanket-mark.
- **Per-side outer gaps.** The baseline gap rule does not carry the four per-side
  actions by default. Reset must restore that shape (drop any per-side actions the
  user added by toggling per-side gaps on), which `updateRule(makeBaselineGapRule())`
  does naturally because it overwrites the whole action list.
- **`Settings::innerGap()/outerGap*()`.** These read the gap baseline's actions, so a
  reset of that rule flows through to the gap getters via the existing
  `onRuleStoreChanged` fingerprint path — no extra wiring.
- **Daemon not running.** If the settings app can run without the daemon, the
  fire-and-forget reset is a no-op until the daemon starts; the next launch's
  `ensureManagedRule` seeding does not reset values, so the user would need to retry
  Restore Defaults once the daemon is up. Acceptable, and consistent with how other
  daemon-driven settings behave.
- **Concurrency / idempotency.** The reset is idempotent (overwrites to a fixed
  definition). Two back-to-back resets converge; `rulesChanged` fires per persisted
  change.

## 7. Alternatives considered

- **Settings-side reconstruction.** Have the settings app rewrite the three baseline
  rules to defaults itself (it can read `ConfigDefaults`). Rejected: it would
  duplicate the `makeBaseline*Rule()` definitions (which actions, what shape) on the
  settings side, creating a drift hazard with the daemon's seeding. The baselines are
  daemon-owned; the reset should be too.
- **Make `ensureManagedRule` reset values on startup.** Rejected: that would discard
  the user's Appearance-page edits on every daemon restart. The append-only seeding
  behaviour is intentional and must stay.

## 8. Testing

- **Unit (daemon / RuleStore).** After seeding + editing the baselines to non-default
  values, `resetManagedAppearanceDefaults()` restores each baseline's actions to its
  `makeBaseline*Rule()` definition (values, and the gap rule's per-side actions
  dropped), bumps the store revision, and persists.
- **Unit (settings).** `defaults()` issues the reset and leaves `window-appearance`
  non-dirty.
- **Policy A guard.** A user-authored override rule (e.g. a per-monitor gap rule or a
  per-app assignment) is present before and after the reset — Restore Defaults does
  not delete it.
- **Integration (manual).** Edit border width / gaps on the Window Appearance page,
  Restore Defaults, confirm the controls and the live window appearance return to
  factory values.

## 9. Open questions

1. **User-rule policy** (§4): confirm Policy A (baselines only) is the desired
   behaviour, and whether a separate "Remove all rules" action is wanted.
2. **D-Bus surface** (§5.3): `org.plasmazones.Rules.resetManagedDefaults()` vs a
   control-surface method.
3. **Scope of "appearance" reset**: should Restore Defaults also reset rule-backed
   overlay-property defaults if any exist, or strictly the three appearance/gap
   baselines?
