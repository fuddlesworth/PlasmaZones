<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Rule Refactor — Implementation Plan

| | |
|---|---|
| **Status** | Implemented — under review |
| **Companion** | `docs/window-rule-refactor-design.md` (architecture), `docs/rules-page-mockup.svg` (UI) |
| **Branch** | `feature/window-rule-refactor` (off `v3.1`) |
| **Date** | 2026-05-17 |

This is the task-level plan. The *what* and *why* live in the design doc; this document
is the *how* and the *order* — every file to create/modify/delete, grounded in the current
code, so nothing is missed during implementation.

---

## 1. Phase dependency graph

```
Phase 0  Metadata plumbing  ─┐
                             ├─►  Phase 2  Window-property matchers ─┐
Phase 1  phosphor-rule ┘                                       ├─► Phase 3 ─► Phase 4
   │                                                                 │
   └──────────────────────────────────────────────────────────────► Phase 3  Context matchers + migration
                                                                          │
                                                                     Phase 4  Unified settings UI
```

- **Phase 0** and **Phase 1** are independent of each other **except** for the `WindowType`
  enum (see §2.1) — Phase 0 must land that enum first. Both are non-breaking and shippable
  alone.
- **Phase 2** needs Phase 1 (hard) and the `WindowType`/`pid`/`windowRole` fields are *not*
  required by Phase 2's two subsystems, so Phase 0 is only a soft dependency for it.
- **Phase 3** needs Phase 1 (hard) and Phase 0 (hard — context rules resolve against live
  windows whose per-window desktop/activity come from Phase 0). It carries the schema
  `v3→v4` migration.
- **Phase 4** needs Phase 3 (hard — the `rules.json` store, the D-Bus adaptor, the
  migration must exist before the UI points at them).
- **Phases 3 + 4 land together** (the migration and the UI re-point are atomic). Phases
  0–2 are independently releasable.

---

## 2. Cross-phase decisions (conflicts resolved)

The per-phase agents surfaced four points needing a project-level decision. Resolved here;
the per-phase sections below assume these.

### 2.1 `WindowType` enum lives in `phosphor-protocol`

Phase 0 wanted it in `phosphor-protocol`; Phase 1 wanted it in `phosphor-rule`.
**Decision: `phosphor-protocol`.** It is the lowest shared LGPL library, the enum crosses
D-Bus (as `int`), and `phosphor-rule` is a higher-level consumer — the dependency
arrow must point `phosphor-rule → phosphor-protocol`, never the reverse.

Consequence: Phase 0 **task 1** (define `WindowType` in
`libs/phosphor-protocol/include/PhosphorProtocol/WindowTypeEnum.h`) is a **prerequisite for
Phase 1's `WindowQuery`**. Phase 1's library gains a `find_package(PhosphorProtocol)` +
`target_link_libraries(... PUBLIC PhosphorProtocol::PhosphorProtocol)`. The header must
stay free of `QtDBus`/`Q_DECLARE_METATYPE` so the effect can include it cheaply.

### 2.2 `RuleStore` is Phase 3, not Phase 2

Phase 2 uses **in-memory bridges only** — no file, no schema change. The `RuleStore`
(load/save `rules.json`, daemon-sole-writer) is created in **Phase 3** alongside the
migration. Phase 2 does not touch disk.

### 2.3 Bridge helpers — header-only, in `phosphor-rule`

`AnimationAppRule → Rule` conversion is needed by both Phase 2 (effect, in-memory)
and Phase 3 (the migration). To avoid duplicating non-trivial conversion logic, it lives in
`phosphor-rule` as **header-only inline free functions** (`AnimationAppRuleBridge.h`,
`ExclusionListBridge.h`). Header-only means `phosphor-rule` takes an *include-time*
dependency on `phosphor-animation` headers only for consumers that include the bridge
header — no hard link edge on the core library, no cycle (`phosphor-animation` never links
`phosphor-rule`). The effect and the migration each include the bridge header.

### 2.4 Daemon-side exclusion enforcement migrates in Phase 3

Phase 2 is **effect-focused**: it wires the animation resolvers and the effect's exclusion
filtering through the evaluator, and links `phosphor-rule` into *both* the effect and
the daemon (to establish the shared binary). The daemon's own exclusion checks
(`navigation_actions.cpp`, `lifecycle.cpp` — which use `appIdMatches`, not `Contains`)
migrate in **Phase 3**, when the unified store exists and the effect-vs-daemon operator
divergence is deliberately reconciled by the migration.

### 2.5 Design-doc note: `LayoutFilterBar.qml` / `LayoutFilterLogic.js` are retained

These are consumed by `LayoutsPage.qml` (the layout gallery), not by any assignment page —
they survive Phase 4. `LayoutComboBox.qml` and `WindowPickerDialog.qml`
are likewise retained and reused by the new page.

---

## 3. Phase 0 — Window-metadata plumbing

**Goal:** make `windowRole`, `pid`, per-window `virtualDesktop`/`activity`, and a unified
`WindowType` enum available to the daemon. Purely additive; no deletions.

### Tasks (ordered)

1. Define `WindowType` enum + `windowTypeTo/FromString` — `phosphor-protocol` (critical path).
2. Extend `WindowMetadata` struct (5 new fields) + widen `operator==`.
3. Update the `org.plasmazones.WindowTracking.xml` (introspection-parity only — hand-written adaptor).
4. Change `WindowTrackingAdaptor::setWindowMetadata` signature (`.h` + `.cpp`).
5. Effect-side `WindowType` mapping helper (anonymous-namespace free function in `window_identity.cpp`).
6. `pushWindowMetadata()` reads + sends the 5 new attributes.
7. Add `desktopsChanged`/`activitiesChanged`/`windowRoleChanged` to the effect's metadata re-push connection set.
8. Verify `WindowRegistry` consumers (struct widening is transparent — `upsert` is field-agnostic).
9. Tests.

### Files

- **Create:** `libs/phosphor-protocol/include/PhosphorProtocol/WindowTypeEnum.h`
- **Modify:** `libs/phosphor-engine/include/PhosphorEngine/WindowRegistry.h` (struct + `operator==`),
  `libs/phosphor-engine/CMakeLists.txt` (verify `PhosphorProtocol` dep edge),
  `src/dbus/windowtrackingadaptor.h` / `.cpp` (signature, field population, range-clamp the `windowType` int),
  `dbus/org.plasmazones.WindowTracking.xml` (add `setWindowMetadata` method block),
  `kwin-effect/plasmazoneseffect/window_identity.cpp` (capture + 9-arg call + mapping helper),
  `kwin-effect/plasmazoneseffect/window_lifecycle.cpp` (`setupWindowConnections` — extra `pushLatest` connections).
- **Delete:** none.

### Signature change

`setWindowMetadata(instanceId, appId, desktopFile, title)` →
`setWindowMetadata(instanceId, appId, desktopFile, title, windowRole, int pid, int virtualDesktop, activity, int windowType)`.
`windowType` crosses D-Bus as `int`; adaptor range-checks then casts to the enum. **Replace
the slot outright — do not keep a 4-arg overload** (overloaded D-Bus slots are ambiguous).

### Key decisions

- `WindowType` value set = union of the scattered KWin boolean predicates
  (`Normal, Dialog, Utility, Toolbar, Menu, Splash, Dock, Notification, Desktop,
  OnScreenDisplay, Popup, Unknown`). `isModal()` is **not** a type — modality is orthogonal
  state.
- The effect-side mapping is **ordered most-specific-first** (a modal dialog satisfies
  several predicates).
- Per-window desktop/activity are captured at `pushWindowMetadata` time and refreshed via
  the KWin-native `desktopsChanged`/`activitiesChanged` window signals — *not* via the
  noisy screen-crossing detection path.

### Tests

`tests/unit/core/test_window_registry.cpp` (per-field change detection, no-op on identical
wide metadata), `libs/phosphor-protocol/tests/` (enum round-trip), new
`WindowTrackingAdaptor` coverage for the 9-arg slot incl. out-of-range `windowType` clamp.

### Top gotchas

- D-Bus signature change is a wire break — fine because effect + daemon ship in lockstep.
- `windowRole` is X11-only — empty for Wayland-native windows; the match engine treats
  empty as non-matching, never a wildcard.
- `virtualDesktop = 0` means "all desktops" — a sticky window must map to `0`, guarded by
  `isOnAllDesktops()` before reading `x11DesktopNumber()` (1-based).
- Effect must never include daemon headers — `WindowType` lives in `phosphor-protocol`
  precisely for this.

---

## 4. Phase 1 — `phosphor-rule` library

**Goal:** the LGPL-2.1+ rule engine — linkable by the GPL effect and GPL daemon.

### Tasks (ordered)

1. Directory skeleton `libs/phosphor-rule/{include/PhosphorRule,src,tests}`.
2. `MatchTypes.h` — `Field`/`Operator` enums + string conversion.
3. `WindowQuery.h` — attribute bag + `valueForField()` accessor (includes `WindowType` from `phosphor-protocol`).
4. `MatchExpression` — leaf/composite tree, evaluation, JSON, cached compiled regex.
5. `RuleAction` + `ActionRegistry` — pluggable descriptors, built-in registration.
6. `Rule` — `{id,name,enabled,priority,match,actions}`.
7. `RuleSet` — ordered collection, revision counter, file (de)serialization.
8. `RuleEvaluator` — `resolve()`, slot accumulation, terminal `Exclude`, match cache.
9. `PhosphorRule.h` umbrella + `rulelogging`.
10. `CMakeLists.txt` + `PhosphorRuleConfig.cmake.in` (mirror `phosphor-zones`).
11. `tests/` + wire into top-level `CMakeLists.txt` after `phosphor-identity`.
12. Evaluator benchmark test.

### Files

- **Create:** the full `libs/phosphor-rule/` tree — 10 public headers, 8 source files,
  ~13 test files, `CMakeLists.txt`, `PhosphorRuleConfig.cmake.in`, `README.md`
  (see the Phase 1 agent's file-layout table — reproduced in the design doc's library
  section).
- **Modify:** top-level `CMakeLists.txt` — `add_subdirectory(libs/phosphor-rule)`
  after the `phosphor-identity` line.
- **Delete:** none.

### Key decisions

- Dependency surface: `Qt6::Core` + `PhosphorProtocol` (for `WindowType`) + `PhosphorIdentity`
  (PRIVATE — `appIdMatches()`). **No `Qt6::Gui`** — keep the effect's transitive link cost low.
- `MatchExpression` is a copyable value type (lives in `QList<Rule>`); the compiled
  regex cache is `mutable std::shared_ptr<QRegularExpression>`.
- Empty `All{}` = always-true catch-all (the migrated provider default).
- Every loader follows the `AnimationAppRule::fromJson` strict-validation precedent:
  `std::optional`, drop-malformed-with-`qCWarning`, canonicalize-on-save.
- `RuleSet::fromJson` **refuses** a non-v4 `_version` — migration is the config
  layer's job, never the library's.
- `RuleEvaluator` exposes both `resolve()` and `resolveCached()`, and a `clearCache()` for
  metadata-driven invalidation. The `ResolvedActions` result **must distinguish
  slot-unfilled from slot-filled-with-empty-value** (the animation engaged-empty `effectId`
  sentinel depends on it).
- Header-only bridge headers `AnimationAppRuleBridge.h` / `ExclusionListBridge.h` (see §2.3).

### Tests

One executable per type; all headless (`QT_QPA_PLATFORM=offscreen`). The critical one is
`test_ruleevaluator_cascade.cpp` — written **now**, in Phase 1, against the *behaviour* of
the existing `LayoutRegistry` cascade, so Phase 3's migration has a behavioural oracle.

### Top gotchas

- LGPL-2.1-or-later headers on **every** file — a GPL-3 header (as on the design docs)
  would defeat the architecture.
- `WindowType` ownership: include from `phosphor-protocol`; do not redefine.
- No QObjects / QML / D-Bus in this library — those are Phase 4, in the GPL settings app.

---

## 5. Phase 2 — Window-property matchers through the evaluator

**Goal:** animation App Rules and the effect exclusion lists resolve via `RuleEvaluator`,
behaviour byte-identical, on-disk format unchanged. Non-breaking.

### Tasks (ordered)

1. Link `phosphor-rule` into the effect and the daemon (`kwin-effect/CMakeLists.txt`,
   `src/CMakeLists.txt`) — establishes the shared binary.
2. Add a `RuleEvaluator` + cached `RuleSet` to `ShaderTransitionManager`, rebuilt
   whenever `appRules()` changes.
3. Replace the three resolver call sites (`shader_transitions.cpp:1178/1214`,
   `drag_snap.cpp:475/522`) with `RuleEvaluator::resolve(WindowQuery)` + slot extraction —
   keep `resolveAnimationShaderProfile/Duration/MotionProfile` as shims whose *bodies* are
   reimplemented on the evaluator.
4. Route the effect's `matchesExclusionLists()` (both call sites) and
   `shouldAnimateWindow()`'s rule-override loop through the evaluator.
5. `WindowQuery` factory from `KWin::EffectWindow*` (effect translation unit only — keeps
   KWin types out of the library).
6. Behaviour-parity verification pass (old-vs-new over a fixture corpus).

### Files

- **Modify:** `kwin-effect/CMakeLists.txt`, `src/CMakeLists.txt` (link),
  `kwin-effect/shadertransitionmanager.h` (evaluator + rule-set members),
  `kwin-effect/plasmazoneseffect/shader_transitions.cpp` (`loadAnimationAppRulesFromDbus`,
  `tryBeginShaderForEvent`), `kwin-effect/plasmazoneseffect/drag_snap.cpp`,
  `kwin-effect/plasmazoneseffect/window_filtering.cpp` (`shouldHandleWindow`,
  `shouldAnimateWindow`), `kwin-effect/plasmazoneseffect/daemon_bringup.cpp` (rebuild the
  effect exclusion rule set on the load callbacks), `kwin-effect/plasmazoneseffect.h`.
- **Create:** `WindowQuery` factory (small `window_query.cpp` in the effect, or a helper in
  `window_filtering.cpp`); parity tests.
- **Delete:** `matchesExclusionLists()` is kept as a private fallback during Phase 2 and
  removed only after the parity pass.

### Key decisions

- **In-memory bridge only** — `Settings::animationAppRules()` and the exclusion `QStringList`s
  load from `config.json` exactly as today; the bridge converts the loaded objects into a
  `RuleSet` whenever they change.
- Animation actions use **event-scoped slots** (`anim-shader:<event>`, `anim-timing:<event>`)
  so the shader axis and timing axis stay independent and `eventPath` exact-match is
  reproduced.
- Effect exclusion bridge uses `Contains` on `DesktopFile`/`WindowClass`; the daemon's
  divergent `appIdMatches` semantics are **left untouched in Phase 2** (reconciled in
  Phase 3). Empty patterns must be dropped (a `Contains ""` predicate matches everything).
- The duration clamp, curve `tryCreate` fallback, engaged-empty `effectId` sentinel, and
  empty-input short-circuits stay in the resolver *shim*, not the bridge/evaluator.
- `shouldAnimateWindow()`'s "any rule re-enables this class" check needs an
  event-agnostic `hasAnyMatch`-style query, not a per-event slot lookup.

### Tests

New `libs/phosphor-rule/tests/test_animationapprulebridge.cpp` /
`libs/phosphor-rule/tests/test_exclusionlistbridge.cpp`;
`libs/phosphor-animation/tests/test_animationappruleresolver.cpp` becomes an
integration test over bridge+evaluator; net-new effect-filtering parity test
(none exists today).

### Top gotchas

- Engaged-empty `effectId` sentinel — "rule matched, effectId empty" (block) vs "no rule"
  (tree fallthrough) must stay distinct in `ResolvedActions`.
- Preserve the `!ruleSet.isEmpty()` fast path so a no-rules user pays nothing.
- The match cache must be wired (Phase 1) before declaring parity — `shouldHandleWindow`
  sits on an O(N²) hot path.

---

## 6. Phase 3 — Context matchers + schema migration

**Goal:** zone Assignments and per-mode disable lists become context-only `Rule`s;
introduce `rules.json` + its D-Bus adaptor; ship `migrateV3ToV4`.

### Tasks (ordered)

1. Context-rule priority formula + shared helper.
2. `rulesFilePath()` in `ConfigDefaults`; `createRulesBackend()` in `configbackends`.
3. `RuleStore` — load/save `rules.json`, daemon-sole-writer.
4. `migrateV3ToV4` + `finalizeV4Conversion`; register the step; `ConfigSchemaVersion = 4`.
5. Reimplement `LayoutRegistry`'s cascade methods on `RuleEvaluator` + `RuleStore`
   — **keep every public signature byte-identical** (27 call sites untouched).
6. Reimplement `IZoneVisualizationSettings::isMonitorDisabled/isDesktopDisabled/isActivityDisabled`
   on the rule store — `isContextDisabled`/`contextDisabledReason` bodies unchanged (18 call sites untouched).
7. Migrate the daemon's own exclusion enforcement (`navigation_actions.cpp`, `lifecycle.cpp`).
8. `RuleAdaptor` D-Bus object (`org.plasmazones.Rules`) + register in the daemon.
9. Relocate `QuickLayouts` slots into the `quicklayouts.json` sidecar; rename `assignments.json` → `assignments.json.migrated` after conversion.
10. Tests (cascade-fidelity, migration, store).
11. Cleanup: delete `createAssignmentsBackend()`, `assignmentsFilePath()`, `Assignment:`/`QuickLayouts` group I/O.

### Cascade → priority formula

```
priority = 300 + (activityPinned ? 200 : 0) + (desktopPinned ? 100 : 0) + (screenPinned ? 10 : 0)
```

| Cascade level | priority | |
|---|---|---|
| Exact (screen+desktop+activity) | 610 | most specific |
| Screen + activity | 510 | activity weight 200 > desktop 100 ⇒ beats screen+desktop |
| Screen + desktop | 410 | |
| Screen only | 310 | display default |
| Provider default | 0 | empty-`All{}` catch-all rule |

"Activity beats desktop" is structurally guaranteed (200 > 100). **Connector-name and
virtual-screen fallback are NOT priority bands** — they are recursive key rewrites, kept as
a query-side retry loop inside the reimplemented `layoutForScreen`.

### Files

- **Create:** `src/dbus/ruleadaptor.h` / `.cpp`,
  `tests/unit/core/test_rule_cascade_fidelity.cpp`,
  `tests/unit/config/test_migration_v3_to_v4.cpp`, `tests/unit/config/test_rule_store.cpp`.
- **Modify:** `src/config/configmigration.h` / `.cpp` (version bump, step registration,
  `migrateV3ToV4` + `finalizeV4Conversion`, called from `ensureJsonConfigImpl`),
  `src/config/configbackends.h` / `.cpp`, `src/config/configdefaults.h` / `.cpp`,
  `libs/phosphor-zones/src/layoutregistry_assignments.cpp` /
  `layoutregistry_persistence.cpp`, `libs/phosphor-zones/include/PhosphorZones/LayoutRegistry.h`
  (member/constructor change), `libs/phosphor-zones/CMakeLists.txt` (link `phosphor-rule`),
  `src/daemon/daemon.cpp` (`LayoutRegistry` construction, register `RuleAdaptor`),
  `src/config/settings.cpp` (`IZoneVisualizationSettings` impl), `src/CMakeLists.txt`,
  `src/core/settings_interfaces.h` (doc comments).
- **Delete (cleanup, after migration verified):** `createAssignmentsBackend()`,
  `assignmentsFilePath()`, the `Assignment:`/`QuickLayouts` group I/O.

### Key decisions

- The migration touches **two files**, but a `MigrationStep` is `void(QJsonObject&)`.
  Split: `migrateV3ToV4` does the `config.json`-side key removals + stashes the data;
  `finalizeV4Conversion` (a post-chain step) reads `assignments.json`, writes
  `rules.json`, retires `assignments.json` (renamed to `assignments.json.migrated`
  so a downgrade can recover the previous schema).
- The `assignments.json` rename-to-`.migrated` is the **irreversible commit** — do it
  last, after `rules.json` is durably written (temp-write + rename + fsync).
  Idempotency: skip if `rules.json` already exists at `_version ≥ 4`.
- Migrated assignment rules carry **all three** actions (`SetEngineMode`,
  `SetSnappingLayout`, `SetTilingAlgorithm`) to preserve the mode-toggle losslessness
  invariant.
- `hasExplicitAssignment` becomes an **exact-shape store lookup**, never a
  `RuleEvaluator::resolve` (which always returns the catch-all).
- `QuickLayouts` slots are *not* rules — relocate them to the `quicklayouts.json` sidecar
  (the file `LayoutRegistry` reads), a sibling of `rules.json`.
- `RuleAdaptor` is hand-written (`Q_CLASSINFO` + public slots), like every other
  adaptor in this codebase — no `.xml` codegen. `LayoutAdaptor`'s assignment methods stay
  (now rule-backed); Phase 4 deletes the legacy D-Bus surface.

### Tests

`test_rule_cascade_fidelity.cpp` ports every scenario from
`test_layoutmanager_assignment.cpp` against the rule-backed registry — the highest-risk
correctness work. `test_migration_v3_to_v4.cpp` asserts exact priorities, losslessness,
`assignments.json` retirement, idempotency. Existing `test_settings_disable_per_mode.cpp`
must pass unchanged — that is the disable-list parity proof.

### Top gotchas

- Mode-toggle losslessness; `hasExplicitAssignment` exactness; two-file migration; the
  irreversible `assignments.json` rename-to-`.migrated`; connector/VS fallback is not
  priority; `RuleEvaluator` tie-break must be a `stable_sort`.

---

## 7. Phase 4 — Unified settings UI

**Goal:** one Rules page replaces five legacy surfaces; one `RuleController` +
`RuleModel` replaces three controllers/bridges.

### Tasks (ordered)

1. C++ scaffolding — `RuleModel` (`QAbstractListModel`), `RuleController`.
2. Wire `RuleController` into `SettingsController` (`Q_PROPERTY ... CONSTANT`,
   construct in ctor, dirty-tracking + revert/commit hooks).
3. D-Bus client plumbing to the Phase 3 `org.plasmazones.Rules` adaptor.
4. Leaf QML components (bottom-up): `RuleRow`, `MatchExpressionEditor` (recursive),
   `MatchLeafEditor`, `ActionListEditor`, `ActionRow`, `MonitorOverviewTile`.
5. Composite editors: `RuleEditorSheet`, `AddRuleSheet` (guided subject chooser).
6. `RulesPage` assembly + `MonitorOverview` strip + grouped/filtered list.
7. `qt_add_qml_module` `QML_FILES` add/remove; C++ sources into `plasmazones_settings_SRCS`.
8. `Main.qml` navigation re-point (`_pageComponents`, `_childItems`, `_mainItems`).
9. Relocate the non-rule "Window Filtering" section out of `AnimationsAppRulesPage.qml`
   into `AnimationsGeneralPage.qml`.
10. Delete legacy QML + the `AnimationsPageController` app-rule block + the three bridges.
11. Prune now-orphaned `SettingsController` Q_INVOKABLEs — **verification-gated** (grep for
    other consumers first).
12. Tests.
13. `qmllint` + i18n sweep.

### Files

- **Create:** C++ — `src/settings/rulecontroller.{h,cpp}`,
  `src/settings/rulemodel.{h,cpp}`. QML — `RulesPage.qml`, `MonitorOverview.qml`,
  `MonitorOverviewTile.qml`, `RuleRow.qml`, `RuleEditorSheet.qml`, `AddRuleSheet.qml`,
  `MatchExpressionEditor.qml`, `MatchLeafEditor.qml`, `ActionListEditor.qml`, `ActionRow.qml`.
  Tests — `tests/unit/settings/test_window_rule_model.cpp`, `test_window_rule_controller.cpp`.
- **Modify:** `src/settings/CMakeLists.txt`, `src/settings/qml/Main.qml`,
  `src/settings/settingscontroller.h` / `.cpp`, `src/settings/animationspagecontroller.h` /
  `.cpp` (remove app-rule block), `src/settings/qml/AnimationsGeneralPage.qml` (receive the
  relocated Window Filtering section), `tests/unit/CMakeLists.txt`.
- **Delete:** `MonitorAssignmentsCard.qml`, `ActivityAssignmentsCard.qml`,
  `SnappingAssignmentsPage.qml`, `TilingAssignmentsPage.qml`, `AssignmentsAppRulesPage.qml`,
  `AppRulesCard.qml`, `AnimationsAppRulesPage.qml` (after relocation), `AssignmentRow.qml`,
  `SnappingBridge.qml`, `TilingBridge.qml`, `SharedBridge.qml` (verify no other consumer).
- **Do NOT delete:** `LayoutFilterBar.qml`, `LayoutFilterLogic.js`, `LayoutComboBox.qml`,
  `WindowPickerDialog.qml` (all retained / reused).

### Key decisions

- **One `RuleModel`, no per-section proxy models.** A C++-computed `SectionRole`
  buckets each rule; `RulesPage` builds a derived `sectionModel` (JS, reactive on
  search/chip/monitor-filter) over the single model.
- Priority is **derived/hidden** in Monitor/Application/Activity sections, **list-order**
  (drag) in Animations, **an explicit SpinBox only** in Advanced/Custom.
- The `viewMode` 0/1 snapping-vs-tiling split disappears — the action type carries it.
- `WindowPickerDialog` is reused as-is (binds to `SettingsController`, not a bridge).
- `AnimationsPageController` is only *partially* gutted — shader overrides, motion sets,
  presets stay; only the app-rule block leaves.
- The "Window Filtering" section (min width/height, transient toggles) is animation-global
  config, **not** per-rules — relocated, not deleted. Only the animation exclusion
  *lists* migrate to `Exclude` rules.

### Tests

`test_window_rule_model.cpp` (roles, `SectionRole` derivation incl. "graduation" to
Advanced, summaries), `test_window_rule_controller.cpp` (CRUD by **UUID**, `moveRule`,
`newEmptyRule`, staging contract, `monitorOverview`).

### Top gotchas

- Hard block on Phase 3.
- `AnimationsAppRulesPage.qml` is mixed — relocate before deleting.
- `SettingsController` Q_INVOKABLE pruning must be grep-verified (`MonitorStatePage.qml`
  etc. may share some).
- Every new QML file (incl. the recursively-instantiated `MatchExpressionEditor.qml`) must
  be in `QML_FILES` or it fails to load — the `qmllint` target catches this.
- i18n on every string; `Kirigami.Theme`/`Units` (the SVG accent `#3daee9` is
  `Kirigami.Theme.highlightColor`); `Accessible.name` on every control; `screenId` /
  activity-UUID / layout-UUID keys, never indices.

---

## 8. Consolidated risk register

| Risk | Phase | Mitigation |
|---|---|---|
| Cascade↔priority fidelity (incl. activity-beats-desktop, provider fallback) | 3 | `test_rule_cascade_fidelity.cpp` ports the full `test_layoutmanager_assignment.cpp` suite; the priority formula is written in Phase 1's cascade test as the oracle |
| Engaged-empty `effectId` sentinel collapses to "no rule" | 1, 2 | `ResolvedActions` distinguishes slot-unfilled from slot-filled-empty; pinned by `test_ruleevaluator.cpp` |
| Mode-toggle losslessness lost in migration | 3 | migrated rules carry all three engine/layout actions; `assignmentEntryForScreen` reads all three slots |
| Two-file migration / crash mid-convert | 3 | `migrateV3ToV4` + `finalizeV4Conversion` split; temp-write+rename+fsync; `assignments.json` renamed to `.migrated` last; idempotency guard |
| Effect/daemon match-code divergence | 2, 3 | both link the one LGPL evaluator; Phase 2 preserves the existing `Contains` vs `appIdMatches` divergence, Phase 3 reconciles it deliberately |
| Evaluation cost on hot paths | 1, 2 | match cache keyed `(windowId, revision)`; `!ruleSet.isEmpty()` fast path; Phase 1 benchmark |
| `WindowType` ownership / dependency direction | 0, 1 | enum in `phosphor-protocol`; Phase 0 lands it before Phase 1 |
| UI "graduation" of rules a section can't represent | 4 | `SectionRole` → `advanced`; hint shown; specialized views never corrupt an unrepresentable rule |
| `assignments.json` write-churn re-introduced | 3 | `rules.json` is a dedicated file, separate from `config.json`; daemon sole writer |
| Legacy test suites | all | become integration tests over the new engine; new unit suites added per phase |

---

## 9. Test strategy summary

- **Phase 1** ships the engine's unit suite *and* the cascade-fidelity oracle test up front.
- Existing suites (`libs/phosphor-animation/tests/test_animationapprule.cpp`,
  `libs/phosphor-animation/tests/test_animationappruleresolver.cpp`,
  `tests/unit/core/test_layoutmanager_assignment.cpp`,
  `tests/unit/config/test_settings_disable_per_mode.cpp`) are retained and
  become **integration tests** /
  **parity proofs** over the new path — a legacy test still passing is the parity
  guarantee. `test_animations_app_rules` was retired during the refactor and replaced
  by `test_window_rule_controller` + `test_window_rule_model` (the settings page now
  owns rule authoring through the unified controller, so the legacy bridge test was
  superseded).
- Every phase adds its own unit tests; net-new coverage where none existed (effect window
  filtering, the rule evaluator, the migration).
- `ctest` must be green and the build must succeed before each phase is committed
  (per `CLAUDE.md`).

---

## 10. References

- `docs/window-rule-refactor-design.md` — architecture and rationale.
- `docs/rules-page-mockup.svg` — settings UI mockup.
- Discussion #240 — roadmap.
- Project memory: `project_window_rule_refactor.md`.
