# Folding config into the unified Rules system — v3.2 / schema v5 design

**Status:** Design, pending implementation approval.
**Target branch:** `v3.2` (config schema **v5**, currently **unshipped**).
**Scope:** Extend the in-flight v5 migration to fold four remaining config groups into the unified Rules store, plus a set of additive rule-vocabulary changes that carry no migration cost. Explicitly bound what must *not* fold.

---

## 1. Motivation

v3.2 already folded window appearance (borders, title bars, gaps, opacity), snap-to-zone, screen/desktop routing, and the animation app-rule / exclusion lists into the unified Rules store, and it bumped the config schema to **v5**. Each cross-file fold costs a schema migration, and migrations are write-once / test-forever complexity the project deliberately minimizes (see `CLAUDE.md` "No Ad-Hoc Backwards Compatibility"). The goal here is to identify **every** remaining fold worth doing and land them in a **single** migration, so we do not pay another bump later.

The decisive fact for sequencing: **schema v5 has not shipped.** The v3.2 branch performed the v5 bump but no user has a v5 config on disk. That means the four folds below **extend the existing `migrateV4ToV5` + `finalizeV5Conversion` in place** — we redefine what the (unshipped) v4→v5 step does. We do **not** add a v5→v6. `ConfigSchemaVersion` stays **5** (`src/config/configmigration.h:76`).

> If v5 had shipped, absorbing new folds into it silently would be exactly the ad-hoc migration `CLAUDE.md` forbids (a shipped v5 config already stripped its stash), and we would instead add exactly one `migrateV5ToV6`. It has **not** shipped, so this document assumes the extend-in-place path.

---

## 2. The bump-cost taxonomy (the organizing principle)

Every candidate change falls into one of three cost classes. This is the lens for the whole document:

| Class | What it is | Schema cost |
|---|---|---|
| **A. New match Field (condition)** | Append one enum value + one `kFieldTable` row + bump `FieldCount` | **None.** Wire format unchanged; the strict loader drops unknown field tokens (`fieldFromString → nullopt`, `MatchTypes.h:178-186`). `RuleSet::SchemaVersion` stays 4, decoupled from `ConfigSchemaVersion`. Ships in any release, forever. |
| **B. New action that retires no config** | Register one `ActionDescriptor` + declare its `ActionType`/`ActionSlot`/`ActionParam` | **None.** Additive vocabulary in the open `rules.json` store; pre-upgrade binaries simply don't parse it. Ships anytime. |
| **C. Fold that *retires* a config group** | Move an existing config group's authority into rules and delete the keys | **One migration.** This is the *only* class that constrains the "minimize bumps" goal. |

**Consequence:** only Class-C items create batching pressure. Everything in Class A/B is out of the batch and can land whenever convenient. The batch is exactly the four Class-C folds in §5.

---

## 3. Rules-system primitives (verified reference)

Implementers must mirror these existing patterns exactly. All citations verified against the current tree.

### 3.1 Match Fields — conditions
`libs/phosphor-rules/include/PhosphorRules/MatchTypes.h`
- `enum class Field` (`:20`) + the `kFieldTable[]` descriptor table (`:100`) + `FieldCount` (`:66`), guarded by two `static_assert`s (table size, enum order). Each row: `{Field, wire, FieldType, FieldSource}`.
- `FieldSource::Window` (absent during windowless context resolution) vs `FieldSource::Context` (screen / desktop / activity / mode / tiledWindowCount — resolvable without a window).
- Adding a field is Class A: append the enum value, append the table row, bump `FieldCount`. No migration.

### 3.2 Actions — attributes
`libs/phosphor-rules/include/PhosphorRules/RuleAction.h`, registered in `libs/phosphor-rules/src/ruleaction.cpp::registerBuiltins()` (`:318`).
- Vocabulary constants live in namespaces `ActionType` (`:280`), `ActionParam` (`:405`), `ActionSlot` (`:474`), and `Tag` (`:131`).
- `ActionDomain::Context` vs `ActionDomain::Window` (`:36`) selects which query shape fills the action's slot. A Context action matched against window-property predicates silently never fires; `Rule::validationIssues()` flags this.
- Conflict resolution is **first-matching-rule-wins per slot**; distinct slots stack. One slot per property is the established convention (see the per-side gap slots).
- Descriptor template — the SetInnerGap descriptor, `ruleaction.cpp:888-906`, is the canonical Context value-action shape:
  ```cpp
  registerAction(ActionDescriptor{
      .type = QString(ActionType::SetInnerGap),
      .slotFor = constantSlot(ActionSlot::InnerGap),
      .validate = [](const QJsonObject& p) { return hasNumberInRange(p, ActionParam::Value, kMaxGap); },
      .terminal = false,
      .allowedKeys = {QString(ActionParam::Value)},
      .domain = ActionDomain::Context,
      .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("number"),
                   .min = 0.0, .max = kMaxGap, .defaultDisplay = 8.0}},
      .category = QStringLiteral("gap"),
      .displayOrder = 0,
      .tags = {QString(Tag::Gap)},
  });
  ```
- `ParamSchema.kind` (`MatchTypes`/`RuleAction.h:107`) is the UI hint the QML editor dispatches on: canonical kinds `string`, `number`, `percent`, `enum`, `bool`, `color`, plus picker-aware kinds (`snappingLayout`, `tilingAlgorithm`, `animationEvent`, `shaderEffect`, `overlayShader`, `zoneOrdinals`, `curveEditor`, `screenId`, `virtualDesktop`).

### 3.3 Managed baseline rules — the "default that rules override"
`src/core/baselinerules.cpp` / `.h`
- `makeBaselineSkeleton(id, name)` (`:24`): `managed = true`, `priority = std::numeric_limits<int>::min()`, empty `All{}` match. Any user rule (higher priority) overrides it per slot.
- `makeBaselineBorderRule()` / `makeBaselineTitleBarRule()` / `makeBaselineGapRule()` (`:45` / `:72` / `:105`) seed only parent actions; dependent actions are added on demand by the settings page.
- Single source of truth for **both** the daemon's startup seeding (`ensureManagedRule`) **and** the settings app's per-page reset. IDs come from `ConfigDefaults::baseline{Border,TitleBar,Gap}RuleId()`.
- The gap resolver deliberately **excludes** the managed gap rule from the override tier so its values surface only as the level-4 global default (`RuleAction.h:99-104`).

### 3.4 The migration machinery — stash → finalize → strip
`src/config/configmigration.{h,cpp}` — the v5 step is the template to extend. Verified structure:

**migrate-side** (`migrateV4ToV5`, `configmigration.cpp:3559`; runs as a `void(QJsonObject&)` step, config.json only):
1. Read the retiring v4 groups, stash only values that **differ from the frozen v4 compile defaults** under the temporary root key `_v5AppearanceStash` (`ConfigKeys::Legacy::v5AppearanceStashKey()`).
2. Remove the consumed keys/groups (helpers: `removeGroupAtSegments`, `stripKeysAtPath` `:3469`), preserving surviving non-consumed keys.
3. Stamp `_version = 5`, guarded by an idempotency check (`:3562`).
- Gating helpers: `stashIntIfDiffers` / `stashBoolIfDiffers` (`:3384` / `:3392`) insert a normalized field only when present AND non-default; `stashColor` (`:3402`) normalizes to `#AARRGGBB`.
- **Frozen-spelling discipline (mandatory):** every retiring group path, leaf-key spelling, and compile-default value is pinned as a file-scope `constexpr QLatin1String` / literal in the anonymous namespace (`:3293-3361`), because the live `ConfigDefaults` accessors for retired settings are deleted and library constants may drift. New folds MUST add their own frozen constants the same way.

**finalize-side** (`finalizeV5Conversion`, `configmigration.cpp:3681`; runs after the chain from `ensureJsonConfigImpl`, can touch rules.json):
1. Read `_v5AppearanceStash`; return clean no-op if absent (idempotency).
2. Convert each stashed field into `RuleAction`s (`actionsFromFields` `:3497`, `makeValueAction` `:3486`), **clamping** out-of-range values (`kMaxBorderWidth=10`, `kMaxBorderRadius=20`, `kMaxGap=500`) — an unclamped bad value fails `Rule::isValid()`, `addRule` silently drops the whole rule, and the stash is stripped unconditionally, so a single bad value would permanently lose every override for that scope.
3. Build **non-managed** override Rules with **deterministic V5 UUIDs** (`QUuid::createUuidV5(seedNamespace, seedBytes)`) so re-runs cannot duplicate and the settings UI find-or-creates the identical rule. Per-mode appearance rules match `Field::Mode` (`:3746`); per-screen gap rules match `Field::ScreenId` keyed by `Settings::canonicalPerScreenKey` (`:3775`). Seed priorities `kAppearanceOverridePriority=500`, `kPerScreenGapPriority=300` (`:3554-3555`) — only need to sit above the INT_MIN baselines; the settings controller re-stamps band priorities on next save.
4. Load rules.json (`RuleSet::loadFromFile`), `addRule` each (rejects colliding IDs → re-run safe), `saveToFile`, then strip `_v5AppearanceStash` and rewrite config.json atomically. `finalizeV4Conversion` runs first and guarantees rules.json exists; a load failure here defers (returns false, leaves stash) so the next startup retries.

### 3.5 Per-screen stores (fold #1 and #2 touch these)
`src/config/settings/perscreen.cpp`
- `ConfigDefaults::zoneSelectorGroupPrefix()` — per-screen **zone-selector** overrides, keys = `kPerScreenKeys[]` (`:115`): `Position, LayoutMode, SizeMode, MaxRows, PreviewWidth, PreviewHeight, PreviewLockAspect, GridColumns, TriggerDistance` (`ZoneSelectorConfigKey::` in `src/core/settings_interfaces.h`). Accessor `Settings::getPerScreenZoneSelectorSettings` (`:634`); reader `readPerScreenZoneSelectorEntry` (`:258`); load/save at `:396` / `:452`.
- `ConfigDefaults::autotileScreenGroupPrefix()` — per-screen **autotile** overrides, keys = `kPerScreenAutotileKeys[]`: `Algorithm, SplitRatio, SplitRatioStep, MasterCount, FocusNewWindows, SmartGaps, MaxWindows, InsertPosition, FocusFollowsMouse, RespectMinimumSize, ...` (`PerScreenAutotileKey::` in `settings_interfaces.h`). **The per-screen gap keys are deliberately absent** — gaps are already rule-backed and merged in by `perScreenGapRuleOverrides`. `migrateV4ToV5` already walks `PerScreen.Autotile.<screenId>` and strips only the gap keys, **leaving algorithm / split / master / max live** (`configmigration.cpp:3617-3635`) — which is precisely the seam fold #2 extends.

---

## 4. Decision rubric — rule-only vs default-overridable

Applied to every candidate. Four placement categories, all already present in the tree:

- **(a) Rule-only action** — no default; inert until authored. For inherently-targeted values (`SnapToZone`, `RouteToScreen`, per-app `Exclude`).
- **(b1) Managed baseline rule *is* the default** — a `managed`/`priority=INT_MIN` rule the resolver excludes from the override tier (border / gap / appearance pattern).
- **(b2) Plain config default + override action** — scalar in config, a dedicated action overrides per context/window (assignment / restore-position pattern). Cheaper: no seeding/reset machinery.
- **(c) Plain config, never rule-touched** — infra, topology, tool prefs, UI ordering.

**Decision tree**
1. Meaningful per-window or per-context **behavioural** axis (screen / desktop / activity / mode / tiledWindowCount)? **No → (c).** Stop.
2. Sensible universal default with zero authoring? **No → (a).**
3. Does the consuming resolver already run through the rule engine and would a single source of truth help? **Yes → (b1)**, else **→ (b2)**.

**Guardrail:** never promote (c)→(b) merely because a matcher *could* name it. Virtual-screen geometry *defines* the coordinate space rules match against — topology, not behaviour.

---

## 5. Part A — the four retiring folds (Class C, batched into v5)

All four extend `migrateV4ToV5` (add stash groups + frozen constants) and `finalizeV5Conversion` (add rule builders). None adds a new version. Each fold's default behaviour is seeded by the **daemon** as managed baseline rules (never written by the migration); the migration ports **only user-differing values**, mirroring how the existing v5 stash carries nothing for a clean config.

### Fold #1 — Per-screen Zone Selector overrides → `ScreenId` context rules  ·  Priority: High

**Retires:** the per-screen zone-selector override store at `ConfigDefaults::zoneSelectorGroupPrefix()` (keys in `kPerScreenKeys[]`). The **global** zone-selector defaults (`zoneSelectorEnabled()` and the global `ZoneSelectorConfigKey` values) stay as plain config — they are the baseline the per-screen rules override.

**Target shape — (b2), single GENERIC action.** Like tiling (fold #2), the **global** zone-selector defaults stay in config; only the **per-screen** values fold into `ScreenId`-matched rules. Rather than nine per-property action types, a single generic `SetZoneSelectorProperty` action carries `{ property, value }` and computes its slot (`zone-selector:<property>`) from the property — so independent per-property rules still cascade, from one action type (the same computed-slot mechanic the event-scoped animation actions use). The nine properties (`Position`, `LayoutMode`, `SizeMode`, `MaxRows`, `PreviewWidth`, `PreviewHeight`, `PreviewLockAspect`, `GridColumns`, `TriggerDistance`) are a closed `ZoneSelectorProperty` token set whose strings match the `ZoneSelectorConfigKey` names 1:1 (no translation table). `PreviewLockAspect` carries a bool, the rest ints; a frozen per-property spec table in `ruleaction.cpp` validates each value's shape and range.
- Rule domain **Context**, matched on `Field::ScreenId Equals <canonicalScreenId>`, deterministic id `QUuid::createUuidV5(ConfigDefaults::perScreenZoneSelectorRuleNamespaceId(), canonicalScreenId)` (namespace `{0a5e1b00-…-000000000006}`).
- Consumer: `Settings::perScreenZoneSelectorRuleOverrides` iterates the rule's `SetZoneSelectorProperty` actions (property token → override-map key), and `getPerScreenZoneSelectorSettings` overlays them onto the config-store map — mirroring `perScreenTilingRuleOverrides`.

**Migration:** `migrateV4ToV5` stashes each screen's differing zone-selector values under a new `kStashZoneSelector` section (keyed by screen id, field names = property tokens) and removes the whole `PerScreen.ZoneSelector` category. `finalizeV5Conversion` emits one `ScreenId` rule per screen, one `SetZoneSelectorProperty` action per differing property, each validated through the `ActionRegistry` and skipped if rejected (an out-of-range value falls back to the global default instead of dropping the whole rule).

**Settings page:** `SnappingZoneSelectorPage.qml` keeps its per-monitor scope UI but writes rules via a thin controller (the `WindowAppearanceController` pattern) — the remaining, delicate UI step.

**Why b2 not rule-only:** users expect one global popup config with occasional per-monitor tweaks; rule-only would force a rule for the common case.

**Implementation status (this branch):** vocabulary (generic action + computed slot + frozen per-property spec + validation/round-trip/slot tests), consumer read-overlay (`perScreenZoneSelectorRuleNamespaceId`, `perScreenZoneSelectorRuleOverrides`, the `getPerScreenZoneSelectorSettings` overlay), migration (stash + finalize + round-trip test; two pre-existing per-screen-preservation tests updated to the new fold-out contract), and settings labels (action label, param labels, the nine enum-option labels, new "Zone selector" picker category) are landed and green — full build clean, 78/78 tests across rules/config/settings/migration/autotile pass. **Remaining:** rewire `SnappingZoneSelectorPage.qml` per-screen controls to write the rules + remove the store-write path + mirror the per-screen scope/reset/dirty logic (the delicate UI step, batched with the other pages).

### Fold #2 — Per-screen tiling Split/Master/Max (+ `Algorithm` dedup) → `ScreenId` context rules  ·  Priority: High

**Retires:** the still-live per-screen autotile keys under `autotileScreenGroupPrefix()` that are genuinely per-screen *spatial* knobs: `SplitRatio`, `MasterCount`, `MaxWindows`, and the redundant per-screen `Algorithm` key (which duplicates the existing `SetTilingAlgorithm` Context action — this is a two-sources-of-truth dedup). The **global** defaults stay as plain config; the behavioural per-screen keys (`FocusNewWindows`, `SmartGaps`, `InsertPosition`, `FocusFollowsMouse`, `RespectMinimumSize`) **stay in the per-screen store** — see §7.

**Target shape — (b2), NOT full b1.** The **global** split/master/max defaults **stay in config** (`Settings::autotileSplitRatio()` etc read `ConfigDefaults`); only the **per-screen** values become `ScreenId`-matched override rules. This differs deliberately from gaps, which went full b1 (the global gap default *is* the baseline gap rule). Gaps earned b1 because they also surface on the appearance-page baseline and carry per-mode/context overrides; tiling split/master/max have neither, so the per-screen fold alone removes the hybrid without the cost of a managed baseline tiling rule, reset seeding, or rerouting the global getters. The per-screen fold still fully resolves the "gaps-from-rules but split/master/max-from-config" inconsistency.
- New Context (`ScreenId`-matched) actions: `SetSplitRatio` (percent kind, validate `[0.1, 0.9]`), `SetMasterCount` (number, validate `[1, 5]`), `SetMaxWindows` (number, validate `[1, 12]`). Bounds pinned as literals in `ruleaction.cpp` (no phosphor-tiles dependency); values mirror `AutotileDefaults` (`AutotileConstants.h:41-66`).
- `Algorithm` folds into the **existing** `SetTilingAlgorithm` Context action — no new action, just migrate the per-screen `Algorithm` value into a `ScreenId`-matched `SetTilingAlgorithm` rule and delete the per-screen key. **(Deferred sub-step:** the per-screen algorithm-resolution path (`PerScreenConfigResolver::effectiveAlgorithmId`) must be mapped before removing the config read, to avoid double-apply against a `SetTilingAlgorithm` rule.)
- Consumer: `Settings::perScreenTilingRuleOverrides(store, screenId)` reads `SetSplitRatio`/`SetMasterCount`/`SetMaxWindows` from the deterministic per-screen tiling rule and `getPerScreenAutotileSettings` overlays them onto the config-backed map — exactly mirroring `perScreenGapRuleOverrides`. Per-screen rule id = `createUuidV5(ConfigDefaults::perScreenTilingRuleNamespaceId(), canonicalScreenId)` (namespace `{0a5e1b00-…-000000000005}`, distinct from the gap namespace).

**Migration:** extend the existing `PerScreen.Autotile.<screenId>` walk in `migrateV4ToV5` (`configmigration.cpp:3620-3635`) — it already strips gap keys per screen; add SplitRatio/MasterCount/MaxWindows/Algorithm to the stripped-and-stashed set under a new `kStashPerScreenTiling` section. `finalizeV5Conversion` emits one `ScreenId` `SetSplitRatio`/`SetMasterCount`/`SetMaxWindows` rule (namespaced under `perScreenTilingRuleNamespaceId`) and (if Algorithm differed) one `ScreenId` `SetTilingAlgorithm` rule per screen. Reuse `canonicalPerScreenKey` and the deterministic-id derivation.

**Settings page:** `TilingAlgorithmPage.qml` per-screen split/master/max controls become thin editors that write the per-screen tiling rules (find-or-create by the same deterministic id) instead of `setPerScreenAutotileSetting`. Per-algorithm saved settings (`savedAlgorithmSettings`) stay config (see §6, algorithm params). Requires mirroring the per-screen scope-chip / reset / dirty logic gaps already use.

**Why this fold is the cleanest:** it removes a genuine hybrid — today `getPerScreenAutotileSettings` reads gaps from rules but split/master/max from config. After the fold both come from rules.

**Implementation status (this branch):** four layers landed and green —
1. **Vocabulary** — the three Context actions + descriptors + validators, domain-completeness test updated, positive range/round-trip/stray-key tests (`libs/phosphor-rules`).
2. **Consumer read-overlay** — `ConfigDefaults::perScreenTilingRuleNamespaceId()`, `Settings::perScreenTilingRuleOverrides()`, and the `getPerScreenAutotileSettings()` overlay (`src/config`).
3. **Migration** — `migrateV4ToV5` stashes per-screen split/master/max (differing-from-default; leaves `Algorithm` + behavioural keys live), `finalizeV5Conversion` emits `ScreenId` tiling rules under the distinct namespace; two round-trip tests (full fold + clean-defaults-no-rule) added, existing v4→v5 tests unaffected.
4. **Settings-layer labels** — action-type labels + param labels for the three actions (`ruleauthoring.cpp`); they reuse the existing `percent`/`number` value renderers, so no new QML value-kind.

Full build clean; 71/71 tests across rules/config/settings/migration/autotile pass. **Remaining (coupled, delicate — needs interactive settings-app verification):** rewire `TilingAlgorithmPage.qml` per-screen controls to write the tiling rules (RuleController find-or-create by the deterministic id, like `WindowAppearanceController` does for gaps), remove the three keys from `kPerScreenAutotileKeys`, and mirror the per-screen scope-chip / reset / dirty logic (the code warns of data-loss-on-reset there). Plus the **Algorithm dedup** sub-step (needs the `PerScreenConfigResolver::effectiveAlgorithmId` path mapped first to avoid double-apply).

### Fold #3 — Animation window-filtering → `ExcludeAnimations` rules  ·  **PARTIAL (min-size only)**

**Semantic finding (why only partial):** `shouldAnimateWindow` (`window_filtering.cpp:230-356`) evaluates the transient and notification/OSD type-exclusions **before** the generic rule-override gate, with a special "type-targeted opt-in" probe (`hasMatchTargetingFields`) — a class-only `OverrideAnimation` rule does **not** re-enable a transient/notification window. Baseline `ExcludeAnimations` rules are evaluated **after** the generic override gate, so folding the transient/notification toggles into ordinary exclusion rules would let a class-only rule re-enable them — a behavior change. The `IsTransient`/`IsNotification` fields *can* represent the filters, but the ordering + opt-in semantics don't survive. So the two boolean toggles **stay in config**.

**Min-width/height fold cleanly** and were done: they already sit after the generic override gate (any matching rule bypasses them), so `Width/Height LessThan N` exclusion rules preserve semantics exactly.

**Retires:** `Animations.WindowFiltering.MinimumWindowWidth` / `MinimumWindowHeight` (default 0 = off). Keeps `ExcludeTransientWindows` / `ExcludeNotificationsAndOsd` in the same group.

**Target shape (migration-only, no new actions, no effect-code change):** two non-managed `ExcludeAnimations` rules (fixed ids `ConfigDefaults::animationMin{Width,Height}RuleId()`, `{…007}`/`{…008}`), match `Width`/`Height LessThan N`. The effect **already** consumes `ExcludeAnimations` rules (`m_animationExclusionEvaluator`), and its `m_animationMinWindowWidth/Height` checks go inert once the config keys strip (default 0), so the migrated rules take over automatically. Default 0 → no rule.

**Implementation status (this branch):** migration landed and green — `migrateV4ToV5` stashes the two min-size values (differing-from-0), `finalizeV5Conversion` emits the two `ExcludeAnimations` rules; round-trip test added (width folds, height-0 produces no rule, boolean toggle survives, key stripped). Full build clean, 97/97 tests pass. **Remaining:** the (future, batched) Animations page rework to edit the two rules' match thresholds by their fixed ids, plus formally removing the effect's now-inert min-size check + the two D-Bus/config accessors.

**Note — this is the structurally different fold:** #1/#2/#4 are value-actions; #3 encodes user preference in the baseline rule's *match*, not an action value (analogous to how the appearance page rewrites the baseline border rule's "Apply to" match). The daemon seeds the default-on filters as managed baseline `ExcludeAnimations` rules; the migration ports a user-customized threshold by rewriting the corresponding baseline rule's match leaf (e.g. `Width LessThan 200`) or by toggling the rule's `enabled`. Design the match-encoding explicitly before coding.

**Settings page:** the "Window Filtering" section of `AnimationsGeneralPage.qml` becomes a thin editor over these baseline rules; the global animation-defaults section (master enable, duration, curve, sequence, stagger) stays plain config.

**Migration:** stash the differing filter values under a new `kStashAnimationFilter` section; `finalizeV5Conversion` applies them as match edits to the seeded baseline `ExcludeAnimations` rules (find-or-create by deterministic id).

### Fold #4 — Drag-overlay appearance (`Snapping.Zones.*`) → baseline overlay-appearance rule + context overrides  ·  Priority: Medium (heaviest; decide inclusion now)

**Retires:** the drag-time zone-overlay appearance group `Snapping.Zones.*` (`configdefaults.h:169-283`): `highlightColor`, `inactiveColor`, `borderColor`, `activeOpacity`, `inactiveOpacity`, `borderWidth`, `borderRadius`, `showZoneNumbers`, zone-label settings, blur/effects. (This group was renamed from `Snapping.Appearance.*` by the v4 migration, `configmigration.h:52-56`.)

**Scope decision — BASELINE-ONLY (mirror the gap baseline), not full per-context.** The overlay appearance is consumed at ~6 daemon call sites via **global** `settings->highlightColor()` getters (plus a direct `ConfigDefaults::highlightColor()` in `zoneshaderitem.cpp:108`), NOT through `resolveContextOverlay` (which today carries only shader/style). Threading a resolved context through all those consumers (full per-context overlay themes) was judged too invasive for the value. Instead: make the **global** overlay appearance a managed baseline rule (exactly like border/gap), so `Settings::highlightColor()` etc read the baseline rule (fallback `ConfigDefaults`), the `Snapping.Zones.*` config group is retired, and overlay appearance becomes consistent with the other appearance baselines. Per-context overrides can be added additively later (the actions are already Context-domain) with **no** further bump.

**Target shape (b1, baseline-only):**
- New Context overlay-appearance actions, one slot per property, extending the overlay family: `SetOverlayHighlightColor` / `SetOverlayInactiveColor` / `SetOverlayBorderColor` (color), `SetOverlayActiveOpacity` / `SetOverlayInactiveOpacity` (percent `[0,1]`), `SetOverlayBorderWidth` (0–10) / `SetOverlayBorderRadius` (0–50) (number), `SetOverlayShowZoneNumbers` (bool). All `Tag::Overlay`, domain **Context**, new `overlay-*` slots.
- Managed baseline overlay rule (`ConfigDefaults::baselineOverlayRuleId()`, new) seeded by the daemon carrying the default appearance; `Settings::highlightColor()`/`activeOpacity()`/… read it via a `*ValueFromRule` helper (mirroring `gapValueFromRule`), falling back to `ConfigDefaults`.
- **Coupling:** the migration (strip `Snapping.Zones.*`) and the getter-rewiring MUST land together, or a migrated custom value would be lost.

**Migration:** `Snapping.Zones.*` colours/opacities/border/zone-numbers → the baseline overlay rule's actions, mirroring the mode-appearance stash (`useSystemColors` on → colours are accent-driven, contribute no colour action, same as `buildModeStash`). Effect/perf knobs (blur, frame rate, audio visualizer) **stay global** — see §7.

**Settings page:** the Colors/Opacity/Border/Zone-Numbers sections of `SnappingOverlayAppearancePage.qml` become a thin editor over the baseline overlay rule (the `WindowAppearanceController` pattern).

**Implementation status (this branch):** vocabulary layer landed and green — the 8 Context overlay-appearance actions + descriptors + validators (hex colour / `[0,1]` opacity / bounded border / bool), domain-completeness test updated, positive validation test added; full build clean, 100/100 tests pass.

**Coupling finding (why the rest can't land separately):** an attempt to land the backend (baseline seeder + Settings-getter rewiring + migration) revealed that the overlay appearance is a **config-backed "phase-1" per-page reset page** — its `borderWidth`/colours/opacity live under `pageOwnedConfigKeys` and the per-page reset/discard/dirty infrastructure operates on those **config keys**. Rewiring the getters to read the baseline rule breaks (a) `set(x); get()` round-trips (setter writes config, getter reads rule) and (b) the config-based reset/discard for those keys (`test_settings_core`, `test_settings_pagereset` fail). Fixing that requires converting the overlay page's reset/discard from config-based to rule-based — which **is** the settings-page rework. So unlike gaps (whose page was already rule-backed via `WindowAppearanceController`), fold #4-finish is **not separable** from the overlay-page UI rework; the backend attempt was reverted to keep the tree green.

**Remaining (must land together, in the UI batch):** `baselineOverlayRuleId()` + `makeBaselineOverlayRule()` seeder (+ daemon `ensureManagedRule`), rewire the 7 `Settings` overlay getters (+ `zoneshaderitem.cpp` direct read) to read the baseline rule, remove the overlay keys from `pageOwnedConfigKeys`, convert `SnappingOverlayAppearancePage` to a rule editor with rule-based reset/discard (the `WindowAppearanceController` pattern), the migration, and labels — with a running daemon/overlay to verify appearance.

### Settings-page UI rework plan (the interactive batch)

The per-screen gap page already implements the exact pattern to replicate. Mapped:
- **Card = "dumb" view.** `GapsSettingsCard.qml` exposes `*Value` properties and emits `*Modified` signals; it holds no scope/rule logic. The **host page** (`WindowAppearancePage.qml`) feeds the values for the current scope and handles the edits.
- **Scope chip = `SettingsCard` header**, driven by `scopeEnabled` + `scopeAppSettings` + `scopeHasOverridesMethod` + `scopeClearerMethod`, with the active screen in `settingsController.scopeScreenName` (global = `""`).
- **Rule id + read/write in the controller.** `WindowAppearanceController::perScreenGapRuleId(screenName)` gives the deterministic id; `settingscontroller_perscreen.cpp` reads (`Settings::perScreenGapRuleOverrides`) and writes (RuleController find-or-create). Global values read/write the managed baseline rule.

**Per page:**
1. **`TilingAlgorithmPage`** — wire the per-screen split/master/max controls to this scope-chip infra. Add `perScreenTilingRuleId(screenName)` (namespace `perScreenTilingRuleNamespaceId`) + read (`perScreenTilingRuleOverrides`) / write (RuleController) in the controller; `scopeHasOverridesMethod` = "a tiling rule exists for this screen", `scopeClearerMethod` = "delete it". Global values stay `Settings::autotileSplitRatio()` etc (config). Remove the 3 keys from `kPerScreenAutotileKeys`.
2. **`SnappingZoneSelectorPage`** — same, over the generic `SetZoneSelectorProperty` actions (`perScreenZoneSelectorRuleNamespaceId`); the per-screen store's write path (`setPerScreenZoneSelectorSetting`) is replaced by rule writes.
3. **`AnimationsGeneralPage` "Window Filtering"** — the min-width/height controls edit the two `ExcludeAnimations` rules' match thresholds (`animationMin{Width,Height}RuleId`); remove the two config keys + the now-inert effect min-size check.
4. **`SnappingOverlayAppearancePage` (fold #4-finish, coupled)** — convert from a config-backed phase-1 reset page to a rule editor over the baseline overlay rule: land `baselineOverlayRuleId` + `makeBaselineOverlayRule` + daemon seeding + the 7 Settings-getter rewires + the migration, remove the overlay keys from `pageOwnedConfigKeys`, and switch the page's reset/discard to rule-based. See §Fold #4.

**Verification:** each page's scope-switch / per-monitor override dot / reset / discard is visual and must be checked in the running settings app + daemon. This batch is not headless-verifiable — do it app-connected.

### Optional fold #4b — General window-management exclusion trio

The general (non-animation) window-filter knobs `excludeTransientWindows` / `minimumWindowWidth` / `minimumWindowHeight` (`configdefaults.h:494-518`, moved to the General page in v4) are the exact analog of fold #3 for *management* exclusion (`Exclude` rules rather than `ExcludeAnimations`). Same shape, same effort. Include alongside #3 if desired; otherwise leave as plain config (it is a coherent global default and folding is optional, not forced).

---

## 6. Part B — additive changes (Class A/B, no migration, out of the batch)

These ship whenever convenient. Listed for completeness and because several are cheap wins.

### 6.1 New match Fields (Class A)
Append to `Field` + `kFieldTable` + bump `FieldCount`. No bump.

| Field | Type / Source | Runtime source | Value |
|---|---|---|---|
| `IsMovable`, `IsMaximizable`, `IsMinimizable` | Bool / Window | `KWin::Window::isMovable()` etc. (siblings of `IsResizable`) | High |
| `ResourceName` | String / Window | `resourceName()` (X11 instance-name, distinct from `windowClass`) | Medium |
| `ColorScheme` | String / Window | `colorScheme()` | Medium |
| `IsInputMethod` | Bool / Window | `EffectWindow::isInputMethod()` | Medium — exclude IME/OSK from tiling |
| **`ActiveLayout`** | String / Context | `ILayoutService` | **High** — "when layout X is active here, apply rule Y"; also the enabler that would let per-layout gap/overlay defaults be re-expressed as context rules later (see §8) |
| `ScreenOrientation`, `ScreenWidth`/`Height`, `ScreenCount` | Enum/Int / Context | needs context-query plumbing (more than a table row) | High / Medium |

Context-sourced fields (`ActiveLayout`, screen geometry/orientation/count) require wiring the value into the windowless context query (`ContextRuleBridge` / the resolver), which is more than a table row but still needs **no migration**.

Operator additions (set-membership `In {…}`, `NotEquals`, range `Between`) are ergonomic only — all already expressible via `Any/None/All` composites. One hard limit to document, not fix: `VirtualDesktop` numeric compare can't see the `0 = sticky` sentinel (use `IsSticky`).

### 6.2 `SetAlgorithmParam` — per-context custom algorithm params (Class B, b2)
**Corrected verdict:** feasible, and the "dynamic per-Luau-script schema can't be edited in a rule" objection is **false**. The rule editor already renders a data-driven, id-dependent param schema for shader overrides: `ActionRow.qml:81-128` reads `effectId` from the action's own params and calls `controller.shaderParameters(effectId)` to fetch the runtime-declared uniform schema, then renders a `ShaderParameterEditor`. The algorithm case is *easier* — the equivalent accessor `TilingAlgorithmController::customParamsForAlgorithm(algorithmId)` already exists (`src/settings/tilingalgorithmcontroller.h:76`) and drives the dynamic controls on `TilingAlgorithmPage.qml`.

- Shape: a single generic `SetAlgorithmParam` (Context; params `algorithmId` + a dynamic param map, or `algorithmId`+`paramKey`+`value`). Per-param typed actions are impossible (param set unknown at compile time); the generic action reads its schema from the action's own `algorithmId`, so there is no cross-action dependency.
- Category **(b2)**: the per-algorithm default already lives in the script's `CustomParamDef`, so there is nothing for a baseline rule to own; a Context action overrides per screen/desktop/activity on top of the global `savedAlgorithmSettings` default. The runtime already filters stale params via `hasCustomParam`.
- **Additive form needs no migration.** Only *retiring* the global `savedAlgorithmSettings` / `PerScreenConfigResolver` hybrid into rules would touch a migration, and that is the same optional dedup as fold #2 — not required in this batch. Rank Medium.

### 6.3 Other additive actions (Class B)
`SetOverflowBehavior`, `SetInsertPosition` (tiling, Context) · `SetRestoreSizeOnUnsnap`, `SetRestoreToZoneOnLogin` (snapping, Window) · `Event` param on `ExcludeAnimations` for per-event window disable · `SuppressOsd` / `SetOsdStyle` (Context). All override a surviving global default (b2); none retires config; all ship anytime.

---

## 7. Part C — explicitly do NOT fold (usability guardrails)

Folding these would hurt usability; they stay plain config (category c). Documented so a future pass doesn't re-litigate them.

- **Drag / snap triggers and modifiers** (`dragActivationTriggers`, `zoneSpanTriggers`, `snapAssistTriggers`, `alwaysActivateOnDrag`, thresholds): a trigger *initiates* the gesture — there is no matched window or resolved context before it fires. Per-window/per-context is incoherent.
- **Master enables and GPU budget:** `animationsEnabled`, `enableShaderEffects`, `shaderFrameRate`, audio-visualizer settings. Per-window frame rate is meaningless; per-window disable already exists via `ExcludeAnimations` / `OverrideAnimationShader(effectId="")`.
- **Behavioural per-screen tiling flags:** `SmartGaps`, `FocusNewWindows`, `FocusFollowsMouse`, `RespectMinimumSize`, `InsertPosition` (as a *default*). Keep in the per-screen store / Behavior page; per-context rules for these would bloat the rule-picker with low-value toggles. (`SetInsertPosition` / `SetOverflowBehavior` may still be added as *optional* advanced Context actions per §6.3 — additive, not a fold.)
- **Reduced-motion / accessibility** (not yet present): if added, must be a **global** accessibility master that suppresses all animation — never a per-window action a rule could silently re-enable.
- **Infra / topology / tool prefs:** rendering backend, config export/import, editor shortcuts and grid-snapping, virtual-screen geometry, layout/algorithm display ordering, quick-shortcut keybindings. No per-window/context behavioural axis.
- **Algorithm custom-param *defaults*** stay in the Luau `CustomParamDef` (script-owned). Only the optional per-context *override* (`SetAlgorithmParam`, §6.2) is a rule.

---

## 8. Layout / zone overrides — no batch impact

`appRules` is fully retired (v3→v4 fold into `SnapToZone` / `RouteToScreen`; only migration/test/doc references remain). Remaining layout/zone state splits into:
- **Intrinsic to the template (stays in Layout/Zone):** zone geometry & count, aspect-ratio bounds, per-zone appearance, `useFullScreenGeometry`.
- **Already rule-backed:** active-layout assignment, lock, default-assignment, per-context gap/overlay overrides.
- **Intentional precedence stack (not duplication):** per-layout gaps and per-layout overlay (shader/style) exist as *both* a layout template default *and* a context-rule override. Leave as-is.

Critically, per-layout settings live in `layout-settings.json`, which carries its **own independent `SchemaVersion = 1`** (`LayoutSettingsStore.h`), decoupled from `ConfigSchemaVersion`. Any future move to rules-only authority there is a **sidecar** version bump and never competes with the config-schema bump this document minimizes. The `ActiveLayout` Field (§6.1) is the mechanism that would enable that deliberate future step. **Nothing here enters the v5 batch.**

---

## 9. Implementation plan

### 9.1 Sequencing (freeze the fold list first)
The whole point of the batch is design-once / migrate-once. Confirm the fold list (§5, including the #4 include/exclude and #4b optional decisions) **before** writing the migration. Then:

1. **Rule vocabulary** (`libs/phosphor-rules`): declare new `ActionType` / `ActionSlot` / `ActionParam` constants (`RuleAction.h`); register descriptors in `registerBuiltins()` (`ruleaction.cpp`), each with validator, `allowedKeys`, domain, `params` schema, category, `displayOrder`, tags. Add any new `Field` values used by fold #3's baseline matches only if a needed field is missing (all four required fields already exist).
2. **Baseline seeders** (`src/core/baselinerules.{cpp,h}`): add `makeBaseline*` for the new managed defaults (overlay appearance for #4; the animation-filter `ExcludeAnimations` rules for #3). Wire them into the daemon's `ensureManagedRule` startup seeding and the settings per-page reset (shared source of truth — see `docs/restore-defaults-rule-reset-design.md`).
3. **Consumers:** extend `PerScreenConfigResolver` (#1 zone-selector, #2 tiling) and `LayoutRegistry::resolveContextOverlay` (#4) and the effect's `window_filtering` gate (#3) to read from rules instead of the retired config keys.
4. **Migration** (`src/config/configmigration.cpp`): extend `migrateV4ToV5` with frozen constants + stash sections (`kStashZoneSelector`, `kStashPerScreenTiling`, `kStashAnimationFilter`, `kStashOverlayAppearance`) and the corresponding strip logic; extend `finalizeV5Conversion` with the rule builders (deterministic V5 UUIDs, clamps, `addRule` merge). Keep `ConfigSchemaVersion = 5`.
5. **Settings pages:** convert `SnappingZoneSelectorPage`, `TilingAlgorithmPage` (per-screen section), `AnimationsGeneralPage` (window-filtering section), `SnappingOverlayAppearancePage` (appearance sections) to thin controllers over rules (the `WindowAppearanceController` pattern).
6. **Config accessor cleanup:** delete the retired `ConfigDefaults` accessors for folded keys (the migration's frozen constants make it independent of them). Do **not** leave fallback reads (forbidden by `CLAUDE.md`).

### 9.2 Invariants to preserve (checklist)
- [ ] `ConfigSchemaVersion` stays **5**; no new migration function/version.
- [ ] Every retiring group path / leaf-key spelling / compile default is a **frozen file-scope constant** in the migration's anonymous namespace.
- [ ] Migration ports **only user-differing** values; clean configs stash nothing. Defaults are seeded by daemon baseline rules, not the migration.
- [ ] All finalize-side values **clamped** to the action validators' ranges before rule construction (an out-of-range value drops the whole rule).
- [ ] Override-rule IDs are **deterministic** (`QUuid::createUuidV5`) and match the settings controller's find-or-create derivation (`perScreenGapRuleId`-style), so migration and UI converge on the same rule.
- [ ] New actions use **one slot per property** (per-property cascade) and correct `ActionDomain` (Context for screen/desktop/activity/mode; Window for per-window).
- [ ] Managed baselines stay `priority = INT_MIN`; the relevant resolvers **exclude** them from the override tier where the existing gap resolver does.
- [ ] `finalizeV5Conversion` remains **idempotent** and crash-safe: runs only while the stash is present; defers (returns false, keeps stash) on a rules.json load/write failure.

### 9.3 Testing strategy
- **Migration round-trip** (`tests/unit/config`): per fold, a v4 config with (a) clean defaults → no stash, no new rules; (b) customized values → exact expected rules with deterministic IDs, correct matches, clamped values; (c) partial-crash re-run (stash present, rules already added) → no duplicates, converges clean. Extend the existing v4→v5 migration tests.
- **Action descriptors** (`libs/phosphor-rules/tests`): validator accept/reject, `allowedKeys` strictness, domain classification, round-trip `toJson`/`fromJson` for each new action.
- **Resolver behaviour:** per-screen zone-selector / tiling values resolve from rules (`PerScreen` accessor tests); overlay appearance resolves via `resolveContextOverlay`; animation filter gate reads rules.
- **Baseline reset:** per-page reset restores exactly the seeded baseline rules (shared `baselinerules` definitions).
- **Frozen-spelling guard:** a test asserting the migration reads the exact historical on-disk key spellings (as the existing `kV4Snap*` / `kV4Auto*` constants are pinned).
- Full `ctest` after each layer per `CLAUDE.md`; verify build before any commit.

---

## 10. Open decisions
1. **Include fold #4 (overlay appearance) in this batch?** It is the only fold needing new `resolveContextOverlay` plumbing. Include (recommended, avoids a future bump) or defer (accepts a future v6). Class C — cannot be deferred cheaply once v5 ships.
2. **Include optional fold #4b (general exclusion trio)?** Same shape/effort as #3; coherent either way.
3. **`SetAlgorithmParam` now or later?** Additive (no bump); can land independently of the batch. Recommend after the batch as its own change.
4. Confirm the OSD `WindowType` value(s) for fold #3's notifications/OSD match branch during implementation.

---

## 11. Summary
- **Four Class-C folds** (`#1` zone-selector per-screen, `#2` tiling split/master/max + Algorithm dedup, `#3` animation window-filtering, `#4` overlay appearance) extend the **unshipped v5 migration in place** — one migration, no v6, `ConfigSchemaVersion` stays 5.
- **Class A/B additions** (new Fields incl. `ActiveLayout`; `SetAlgorithmParam`; `SetOverflowBehavior` / `SetInsertPosition` / `SetRestoreSizeOnUnsnap` / `SetRestoreToZoneOnLogin` / per-event `ExcludeAnimations` / `SuppressOsd`) carry **no migration** and ship whenever convenient.
- **Algorithm custom params** and **layout/zone overrides** stay out of the config batch (the former additive-only via `SetAlgorithmParam`; the latter already-rule, intrinsic, or governed by the independently-versioned `layout-settings.json` sidecar).
