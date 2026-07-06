# Rule Expansion Plan (v3.2, branch: rule-expansion)

Expand the rules ACTION/FIELD vocabulary so power users can author more per-window / per-context
rules, WITHOUT retiring config. This is the opposite of the busted `feat/config-rules-fold-v5`
branch, which turned config purely rule-backed. We do NOT want that.

## Core invariant

Every consumer reads **`rule-slot ?? config-getter`** — the rule slot is optional, the config
getter is the fallback. Config stays authoritative for the global default. Per-screen behavior is
expressed as a `WHEN ScreenId Equals <edid>` rule on top of the global config default (option "ii":
no new per-screen config stores; existing per-screen config stores for gaps/zone-selector stay
untouched).

Consequences:
- No config group is retired. No `finalizeV5Conversion`/strip pattern.
- No config schema migration. `RuleSet::SchemaVersion` untouched (new fields: unknown tokens are
  dropped by the loader; new actions that don't retire config cost nothing).
- `phosphor-rules` / `phosphor-zones` edits are LGPL (SPDX `LGPL-2.1-or-later`). Translated labels
  live in the GPL `src/settings` layer (`ruleauthoring.cpp`), NEVER in the lib.
- Every step ends with `cmake --build build --parallel $(nproc)` + `ctest --output-on-failure` green,
  and the ~3 QML render sites + `ruleauthoring.cpp` labels wired (else raw/untranslated UI).

## Generic recipe — new ACTION (5 edit points)

1. `libs/phosphor-rules/include/PhosphorRules/RuleAction.h` — add `ActionType::X`, any
   `ActionParam::*` key, `ActionSlot::X`.
2. `libs/phosphor-rules/src/ruleaction.cpp` `registerBuiltins()` — one `registerAction(ActionDescriptor{...})`.
3. Consumer — resolve the slot, `slot ?? config`.
4. QML 3-place value renderer:
   - `src/settings/qml/ActionRow.qml` (editor input, dispatch on `ParamSchema.kind`)
   - `src/settings/qml/ActionListView.qml` (read-only summary)
   - `src/settings/ruleauthoring.cpp` (`defaultPayloadFor()` default seeding + translated labels)
   A new `kind` string needs a branch in all three; reusing an existing kind needs only labels/default.

## Generic recipe — new FIELD (WHEN side)

1. `libs/phosphor-rules/include/PhosphorRules/MatchTypes.h` — enum `Field` + `kFieldTable` row.
2. `libs/phosphor-rules/include/PhosphorRules/WindowQuery.h` — struct field + `valueForField()` case.
3. Populate at the query-build site.
4. QML 3-place: `src/settings/qml/MatchLeafEditor.qml`, `src/settings/qml/MatchExpressionView.qml`,
   `src/settings/ruleauthoring.cpp` (label + `valueKind` seeding).
5. WINDOW fields consumed daemon-side additionally need the 4-stage metadata plumb:
   effect query (`kwin-effect/plasmazoneseffect/window_query.cpp`) → a{sv} push
   (`window_identity.cpp`) → `WindowMetadata` struct/parse
   (`PhosphorEngine/WindowRegistry.h` + `windowtrackingadaptor.cpp` ~:858) → `buildRuleQueryForWindow`
   (`enginewiring.cpp:385-440`).
   CONTEXT fields populate at `makeContextQuery` (`layoutregistry_rulehelpers.cpp:21-30`) and
   `ContextResolver::handleFor` (`contextresolver.cpp:36-51`).

---

## STATUS

- **Tier 1 overlay appearance: DONE** (uncommitted, branch `rule-expansion`). 8 Context actions
  (`SetOverlay{HighlightColor,InactiveColor,BorderColor,ActiveOpacity,InactiveOpacity,BorderWidth,
  BorderRadius,ShowZoneNumbers}`) registered; `ContextOverlayOverride` extended + populated in
  `resolveContextOverlay`; consumers layered `rule ?? config` at the main overlay (QML-property +
  shader-texture paths), snap-assist, and layout picker; labels/seeding in `ruleauthoring.cpp`
  (overlay colours seed a concrete hex, NOT the accent sentinel — validator is `hasHexColor`). No
  new QML kind (reused color/percent/number/bool → zero `ActionRow.qml`/`ActionListView.qml`
  changes). Tests: domain-pin list + validation (`test_ruleaction.cpp`) + resolution
  (`test_rule_cascade_fidelity.cpp::testContextOverlay_appearanceOverrides`). Full suite 251/251
  green. NOT yet runtime-verified in-app; NOT committed.
- **Tier 3 window bool fields: DONE** (uncommitted). `IsMovable` (36) + `IsMaximizable` (37) added:
  `MatchTypes.h` enum+table+FieldCount(38), `WindowQuery` field+valueForField, full 4-stage window
  plumb (window_query.cpp `kw->isMovable()`/`isMaximizable()` → window_identity a{sv} → ServiceConstants
  keys → WindowRegistry struct+operator== → windowtrackingadaptor parse+copy → enginewiring
  buildRuleQueryForWindow), labels (rulemodel fieldLabel, ruleauthoring fieldCategory "State" +
  fieldDescription). Auto-render as `bool` kind (no QML change). Tests: matchtypes FieldCount canary
  36→38, test_windowquery capability-field coverage. 251/251 green. NOT runtime-verified; NOT committed.
- **Tier 3 ScreenOrientation: DONE** (uncommitted). Field 38 (Context/String "portrait"/"landscape").
  Provider `LayoutRegistry::setScreenOrientationProvider` (mirrors setTiledWindowCountProvider) +
  private `stampScreenOrientation` helper called at ALL 5 makeContextQuery sites (orientation is
  layout-independent → safe in the assignment cascade too). Daemon wires it from
  `ScreenManager::screenGeometry` (height>width → portrait; invalid → nullopt/inert); cleared on
  teardown. Effect per-window query stamps it from `w->screen()->geometry()`. QML: `orientation`
  valueKind → dropdown (mirrors `mode`), summary label branches in rulemodel matchSummary +
  MatchExpressionView. Tests: FieldCount 38→39, valueForField context, rule_controller kind whitelist,
  and `testContextOrientation_stampedAndGatesRule` (provider→stamp→gap-rule match, portrait fires /
  landscape+unknown inert). 251/251 green. NOT runtime-verified; NOT committed.
- **Tier 3 ActiveLayout: DONE** (committed). Field 39 (Context/String; snap UUID or "autotile:<algo>").
  Stamped in the 3 daemon-facing NON-assignment resolvers only — resolveContextGaps/Locked/Overlay —
  via `assignmentIdForScreen` (accurate for snap+autotile incl. global-default fallback; safe from
  these resolvers, which are not in its call chain). EXCLUDED from resolveAssignmentEntry and
  resolveContextDefaultAssignment (both in the assignmentIdForScreen chain → would recurse; documented
  inline). The active layout is a NON-rule-set input (global default can change without a rule-set
  revision bump), so it is folded into each resolver's cache KEY via `contextCacheKeyToken` (mirrors the
  tiledWindowCount "twc:" token) to avoid stale hits. Match-side layout picker: new `layout` valueKind →
  `layoutValueEditor` over `appSettings.layouts` (MatchLeafEditor), expanded-tree + collapsed-summary
  resolution (MatchExpressionView; rulemodel leafLabel threads m_snappingLayoutLookup, autotile ids show
  raw). Tests: FieldCount 39→40, valueForField, rule_controller `layout` kind, and
  `testContextActiveLayout_stampedAndGatesRule` (default-provider layout id gates a gap rule; proves
  no recursion by completing). 251/251 green. NOT runtime-verified.
  The caveat holds: ActiveLayout gates gaps/lock/overlay, but cannot drive the layout assignment itself.
- `ColorScheme` still deferred (no source in pipeline).
- **Tier 2 restore actions: DONE** (committed). Two Window-domain bool actions mirroring RestorePosition:
  `SetRestoreToZoneOnLogin` and `SetRestoreSizeOnUnsnap` (both seed FALSE = opt-out, since the governing
  settings default ON). New resolvers `shouldRestoreToZoneOnLogin` / `shouldRestoreSizeOnUnsnap` on
  WindowTrackingAdaptor (public; clone shouldRestoreFloatedPosition, resolve slot ?? global getter).
  Consumers: ManagedRestorePredicate (enginewiring) now calls shouldRestoreToZoneOnLogin;
  restore-size at float.cpp (direct), drop.cpp + drag.cpp (through m_windowTracking). No QML change
  (bool kind); labels + boolActionStateLabel in ruleauthoring. Tests: kWindowDomainTypes canary + a
  wta resolution test (rule(false) overrides global ON; unmatched falls back). 251/251 green.
  `UnfloatFallbackToZone` stays deferred (engine-internal via ISnapSettings, no daemon evaluator seam).
- **Tier 1 tiling Seam A (max/split/master): DONE** (committed). Three Context actions SetMaxWindows
  (number 1-12), SetSplitRatio (percent, wire [0.1,0.9]), SetMasterCount (number 1-5), category
  "layoutEngine". New `ContextTilingParams` struct + `LayoutRegistry::resolveContextTilingParams`
  (concrete, NOT on interface since the daemon holds a concrete LayoutRegistry; NON-cached since it runs
  on screen/layout changes not per-cursor — which also lets it stamp activeLayout with no cache-key
  fold). Daemon updateAutotileScreens (autotile.cpp) layers the resolved values onto the config-derived
  per-screen override map AFTER the algorithm block (rule wins over config + algo-default MaxWindows).
  These 3 work inject-only: applyPerScreenConfig pushes split/master to TilingState, effectiveMaxWindows
  reads the map. No QML change (number/percent kinds). Tests: kContextDomainTypes canary, validation
  ranges, resolveContextTilingParams per-slot composition. 251/251 green.
- **Tier 1 tiling Seam A InsertPosition: DONE** (committed). SetInsertPosition Context action (enum,
  tokens end/afterFocused/asMaster via InsertPositionToken, mapped to AutotileInsertPosition int 0/1/2 in
  resolveContextTilingParams → ContextTilingParams.insertPosition). New PerScreenConfigResolver::
  effectiveInsertPosition + AutotileEngine::effectiveInsertPosition delegating; consumer
  insertWindowByConfigOrder switched from m_config->insertPosition to
  effectiveInsertPosition(screenForWindow(windowId)) — which ALSO makes the previously-dead per-screen
  InsertPosition config live (latent-bug fix). Daemon injects into the override map. No QML change (enum
  kind). Tests: domain canary, enum validation, resolver mapping asMaster→2. 251/251 green.
- **Tier 1 tiling Seam B OverflowBehavior: DONE** (committed). SetOverflowBehavior Context action (enum
  float/unlimited via OverflowBehaviorToken → AutotileOverflowBehavior int 0/1). Reused the SAME
  override-map path as Seam A (NOT a context-provider closure — the earlier trace suggested one, but the
  override map reaches the tile-engine consumer cleanly and is consistent): ContextTilingParams gained
  overflowBehavior; resolveContextTilingParams reads the slot; new PerScreenKeys::OverflowBehavior; daemon
  injects; new PerScreenConfigResolver::effectiveOverflowBehavior; effectiveMaxWindows now checks
  effectiveOverflowBehavior(screenId) instead of the global config (also revives per-screen overflow
  config). No QML change (enum). 251/251 green.
- **Tier 1 tiling Seam B SetDragBehavior: DONE** (committed). SetDragBehavior Context action (enum
  float/reorder via DragBehaviorToken → AutotileDragBehavior int 0/1). ContextTilingParams gained
  dragBehavior; resolveContextTilingParams reads the slot. Consumer is the drag adaptor, NOT the tile
  engine: new WindowDragAdaptor::effectiveReorderMode(screenId) resolves via m_layoutManager (rule else
  global setting). The static computeDragPolicy gained a `bool reorderMode` param (its 2 callers at
  drag_protocol.cpp resolve+pass effectiveReorderMode); the member-method site (:181) calls it directly.
  test_drag_policy call sites updated to pass reorderMode. No QML change (enum). 251/251 green.
  **Tier 1 tiling COMPLETE** (all of Seam A + Seam B).
- **SetAlgorithmParam: DONE** (committed). Context action mirroring OverrideOverlayShader: carries the
  target algorithm token (ActionParam::Algorithm, kind "tilingAlgorithm" picker) + a free-form params
  blob (ActionParam::Params, allowed-but-undeclared). resolveContextTilingParams reads algorithmParamTarget
  + algorithmParams into ContextTilingParams. Daemon injects overrides[PerScreenKeys::CustomParams] ONLY
  when the target == the screen's effective algorithm (guard lives daemon-side where both are known); the
  engine (AutotileEngine recalculateLayout) layers the per-screen override map over config customParams via
  new PerScreenConfigResolver::effectiveCustomParamsOverride, filtered again by algo->hasCustomParam. QML:
  dedicated inline `_algorithmParamsEditor` in ActionRow.qml (a Repeater over
  tilingAlgorithmPage.customParamsForAlgorithm(algorithm) rendering SettingsSlider/SettingsSwitch/
  WideComboBox per number/bool/enum, writing to action.params via _writeAlgorithmParam) — a dedicated
  editor NOT the shader one (schema field names name/minValue/maxValue + number/bool/enum vocab differ);
  tilingAlgorithmPage exposed in RulesPage.qml _editorAppSettings. Tests: domain canary, validation
  (algorithm required, params free-form, strict allowedKeys), resolver mapping (target=bsp, ratio=0.7).
  251/251 green; QML compiles (qmlcachegen). NOT runtime-verified in-app.
  **ADDITIVE RULE EXPANSION COMPLETE** — all planned actions/fields shipped.
- Deferred tail (each needs a net-new seam, revisit if wanted): OSD suppression (SuppressOsd — no rule
  seam, daemon gates read Settings directly), ColorScheme match field (no source in the pipeline; needs a
  system-scheme watcher), UnfloatFallbackToZone (engine-internal via ISnapSettings, no daemon evaluator seam).

## Tier 1 — Overlay appearance Context actions  ★ START HERE

Cleanest seam: `resolveContextOverlay` already exists; the override object is already in scope at
every config read site.

Backend (phosphor-zones, LGPL):
- Extend `ContextOverlayOverride` (`libs/phosphor-zones/include/PhosphorZones/AssignmentEntry.h:218-228`)
  with optional fields: `highlightColor, inactiveColor, borderColor` (QColor), `activeOpacity,
  inactiveOpacity` (double), `borderWidth, borderRadius` (int), `showZoneNumbers` (bool).
- Populate in `LayoutRegistry::resolveContextOverlay`
  (`libs/phosphor-zones/src/layoutregistry_assignments.cpp:422-462`), mirroring the existing
  `OverlayShader`/`OverlayStyle` blocks, off 8 new `ActionSlot`s.

Actions (phosphor-rules, LGPL) — category `overlay`, Context domain, `Tag::Overlay`:
`SetOverlayHighlightColor`, `SetOverlayInactiveColor`, `SetOverlayBorderColor`,
`SetOverlayActiveOpacity`, `SetOverlayInactiveOpacity`, `SetOverlayBorderWidth`,
`SetOverlayBorderRadius`, `SetOverlayShowZoneNumbers`.
Reuse existing param kinds (`color`, `percent`/`number`, `bool`) → NO new QML kind; 3-place renderer
needs only label/default entries.

Consumer splices (daemon):
- QML-property path: give `writeColorSettings` (`src/daemon/overlayservice/internal.h:342-352`) an
  optional `const ContextOverlayOverride*`; `override.highlightColor.value_or(settings->highlightColor())`.
  Hoist the already-present `overlayOverrideForScreen` resolve above `overlay.cpp:740`.
  `borderWidth`→`overlay.cpp:742`, `borderRadius`→`:743`, `showZoneNumbers`→`:745`.
  Secondary call sites: `snapassist.cpp:209/:573`, `selector_update.cpp:153`.
- Shader-texture path: `overlay_data.cpp:317-327`, insert between `zone->useCustomColors()` and the
  `m_settings->…()` fallback (override already resolved at `:279`). Leave the unconditional per-zone
  custom writes at `:264-272` alone (they win via `useCustomColors`).

Config getters (fallbacks, `src/core/settings_interfaces.h`): `highlightColor()`:197,
`inactiveColor()`:199, `borderColor()`:201, `activeOpacity()`:205, `inactiveOpacity()`:207,
`borderWidth()`:209, `borderRadius()`:211, `showZoneNumbers()`:179.

### Tier 1b — OSD suppression (DEFERRED tail)
`SuppressOsd` (+ per-toggle). No existing rule seam; the 3 OSD gates read Settings directly:
`showOsdOnLayoutSwitch()` (`daemon.cpp:1747`, `signals.cpp:66/:666/:699`), `showOsdOnDesktopSwitch()`
(`signals.cpp:878`, `osd.cpp:521/:538`), `showNavigationOsd()` (`navigation.cpp:375/:496/:526`,
`signals.cpp:784/:1001/:1076`). Needs a new resolver path analogous to `resolveContextOverlay`.
Lower value, more plumbing.

## Tier 1 (tiling) — Context param actions

Decode in `entryFromRuleMatchActions` (`libs/phosphor-zones/src/layoutregistry_rulehelpers.cpp:141-173`),
carry on `AssignmentEntry`, inject via one of two seams.

Seam A — per-screen override map (`src/daemon/daemon/autotile.cpp:127-135`; base =
`getPerScreenAutotileSettings(screenId)`, algo injected at `:133-135`, pushed via
`applyPerScreenConfig`):
- `SetMaxWindows` — EASIEST; inject into `overrides`; `effectiveMaxWindows`
  (`PerScreenConfigResolver.cpp:284-327`) already reads the map, no resolver change.
- `SetSplitRatio` — inject; pushed to `TilingState` at `PerScreenConfigResolver.cpp:51`.
- `SetMasterCount` — inject; `PerScreenConfigResolver.cpp:57`.
- `SetInsertPosition` — inject **+ add `effectiveInsertPosition(screenId)`** on the resolver and
  switch the consumer at `AutotileEngine.cpp:3225` off `m_config->insertPosition` (ignores per-screen today).

Seam B — context-provider closure (gap template, `PerScreenConfigResolver.h:106-110`,
`setContextGapProvider` consulted first in `effectiveInnerGap`/etc.):
- `SetOverflowBehavior` — add `setContextOverflowProvider`; consult in `effectiveMaxWindows` /
  `AutotileEngine.cpp:1363`.
- `SetDragBehavior` — consumer `drag_protocol.cpp:66/:181` reads Settings directly; resolve
  daemon-side or via closure.
- `SetAlgorithmParam` — MOST COMPLEX. Context-param closure; layer at `AutotileEngine.cpp:3379-3392`
  before the `hasCustomParam` filter; GUARD on `effectiveAlgorithmId(screenId)` matching the param's
  target algorithm (params are algo-scoped). Dynamic param schema in QML reuses the shader-uniform
  render path (`customParamsForAlgorithm` at `tilingalgorithmcontroller.cpp:115` + `ActionRow.qml`
  shader-param branch).

Precedent: `SetTilingAlgorithm`/`SetEngineMode` decode at `rulehelpers.cpp:170-171` →
`AssignmentEntry` → `assignmentIdForScreen` (`autotile.cpp:73-75`) → `overrides[Algorithm]` (`:135`).

## Tier 2 — Window restore actions

Template: `WindowTrackingAdaptor::shouldRestoreFloatedPosition()` (`enginewiring.cpp:443-478`) —
resolve slot, else per-engine config default. Query built by `buildRuleQueryForWindow`
(`enginewiring.cpp:385-440`). Existing `RestorePosition` action registered `ruleaction.cpp:753-761`.

- `SetRestoreToZoneOnLogin` — EASIEST; the `ManagedRestorePredicate` at `enginewiring.cpp:181`
  already receives `windowId` (comment says shaped for exactly this). Resolve
  `ActionSlot::SetRestoreToZoneOnLogin ?? restoreWindowsToZonesOnLogin()`.
- `SetRestoreSizeOnUnsnap` — 3 sites: `float.cpp:44` (same class, direct), `drop.cpp:413` +
  `drag.cpp:727` (in `WindowDragAdaptor`, resolve through `m_windowTracking`). Add
  `shouldRestoreSizeOnUnsnap(windowId)` on `WindowTrackingAdaptor`. Fallback getter
  `restoreOriginalSizeOnUnsnap()`.
- `UnfloatFallbackToZone` — FLAGGED / likely DEFER. Consumer engine-internal (`float.cpp:301` via
  `ISnapSettings`), no daemon evaluator seam. Needs a new `SnapEngine` predicate or daemon-side
  pre-resolve. Config-only for now.

## Tier 3 — New match FIELDS (WHEN side)

- `IsMovable` (Window/bool) — effect: `query.isMovable = kw->isMovable()` at `window_query.cpp:172`;
  daemon: 4-stage plumb.
- `IsMaximizable` (Window/bool) — effect: `window_query.cpp:167`; daemon: 4-stage plumb.
- `ScreenOrientation` (Context/enum) — compute from `ScreenManager::screenGeometry(screenId)` (same
  source as `AutotileEngine.cpp:3356`) into `makeContextQuery`. Pairs with per-screen story.
- `ActiveLayout` (Context/string) — source local to `LayoutRegistry` (`layoutForScreen`); thread into
  `makeContextQuery`. Effect path stays disengaged (context-only).
- `ColorScheme` (Context/enum) — DEFER; no source in the pipeline today, needs a new system-scheme
  watcher (`KColorSchemeManager`/appearance portal).

## Sequencing

1. Tier 1 overlay appearance (8 actions) — highest value, cleanest seam, no new QML kind/resolver.
2. Tier 3 cheap fields — `IsMovable`, `IsMaximizable`, `ScreenOrientation`, `ActiveLayout` (skip `ColorScheme`).
3. Tier 2 restore — `SetRestoreToZoneOnLogin` + `SetRestoreSizeOnUnsnap` (skip `UnfloatFallbackToZone`).
4. Tier 1 tiling — Seam A (`SetMaxWindows`/`SetSplitRatio`/`SetMasterCount`/`SetInsertPosition`),
   then Seam B (`SetOverflowBehavior`/`SetDragBehavior`), then `SetAlgorithmParam`.
5. Deferred tail — Tier 1b OSD, `ColorScheme`, `UnfloatFallbackToZone` (each needs a net-new seam).
