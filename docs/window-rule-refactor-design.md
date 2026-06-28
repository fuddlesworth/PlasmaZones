<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Rule Refactor — Design Document

| | |
|---|---|
| **Status** | Draft / proposal |
| **Tracking** | [Discussion #240](https://github.com/fuddlesworth/PlasmaZones/discussions/240) — roadmap "Anytime" item |
| **Author** | — |
| **Date** | 2026-05-17 |
| **Schema impact** | Config schema `v3 → v4` |
| **Companion** | `docs/rules-page-mockup.svg` (settings UI mockup) |

---

## 1. Summary

PlasmaZones has several independent window/context matching subsystems, each with its
own data shape, settings UI, persistence path, and match semantics. This document
proposes consolidating them into a **single `Rule` framework**: one match-expression
language, one pluggable action set, one serialization format, one evaluation pipeline, and
one settings UI surface.

The refactor ships no user-visible feature on its own. Its value is **code health** and
**unblocking future per-window features** (per-window engine override, per-window opacity,
per-window scrolling width preset) — each of which would otherwise grow its own parallel
matcher.

---

## 2. Current state

Five subsystems perform window- or context-matching today. A common misconception — and an
imprecision in discussion #240 — is that they are redundant duplicate matchers. They are
not: **they match on two different axes.**

### 2.1 Context-axis matchers (keyed by screen / desktop / activity)

| Subsystem | Type | Key | Storage |
|---|---|---|---|
| **Zone Assignments** | `AssignmentEntry` (`libs/phosphor-zones`) | `(screenId, virtualDesktop, activity)` with most-specific-first **cascade** | `~/.config/plasmazones/assignments.json` |
| **Per-mode disable lists** | `Display.{Snapping,Autotile}Disabled*` | `screenId` / `screenId/desktop` / `screenId/activity` strings | `config.json` → `Display` group |
| **Per-screen autotile overrides** | `PerScreenConfigResolver` (`libs/phosphor-tile-engine`) | `screenId` | `config.json` → `AutotileScreen:*` groups |

Zone Assignments have **no window-property fields at all** (no class/title/role). They
answer *"what layout/engine does this screen-context use?"* — a question asked when there
is no window. Resolution is a deterministic cascade: exact → activity → desktop → screen →
provider default.

### 2.2 Window-property-axis matchers (keyed by window class / appId)

| Subsystem | Type | Match semantics | Storage |
|---|---|---|---|
| **Animation App Rules** | `AnimationAppRule` (`libs/phosphor-animation`) | `windowClass` substring (CI) + exact `eventPath`; ordered list, first-match | `config.json` → `Animations/AnimationAppRules` |
| **Effect exclusion lists** | `matchesExclusionLists()` (`kwin-effect/.../window_filtering.cpp`) | substring (CI) on appId / windowClass | `config.json`, mirrored into effect |

### 2.3 Consequences of the current design

- **No shared match cache or pipeline.** Animation rules and effect filtering re-resolve
  on every event.
- **Duplicated match code.** `matchesExclusionLists()` substring logic is hand-duplicated
  between the effect and the daemon and must be kept in lockstep manually.
- **Five UIs.** `MonitorAssignmentsCard`, `ActivityAssignmentsCard`, the two assignment
  pages, the Animations App-Rules page, the exclusion-list section — five places, two
  controllers (`AnimationsPageController`, `SnappingBridge`/`TilingBridge`).
- **Every new rule type is a new subsystem.** Adding "per-window opacity" today means a new
  matcher, a new JSON shape, a new UI, a new persistence path.

---

## 3. Goals and non-goals

### Goals

- One composable match-expression language spanning **both** axes (window properties *and*
  context).
- One pluggable action set — new rule types register an action, not a parallel matcher.
- One serialization format, one config file, one schema-versioned migration.
- One shared evaluation pipeline + match cache, linkable by both the daemon and the KWin
  effect.
- One settings UI surface.
- Lossless migration of all existing Assignments, Animation App Rules, disable lists, and
  exclusion lists.

### Non-goals

- New end-user features (those come *after*, unblocked by this work).
- Changing the window-identity format (`appId|instanceId` — see project memory).
- Replacing `PerScreenConfigResolver`'s *value resolution* (gaps, split ratios). Per-screen
  autotile **config values** are not "rules"; only the *screen-targeting* concern is
  unified. `PerScreenConfigResolver` continues to own effective-value computation.

---

## 4. Architecture

### 4.1 New library: `phosphor-rule` (LGPL-2.1-or-later)

A new reusable library under `libs/phosphor-rule/`, LGPL like the other `phosphor-*`
libraries. Both the GPL KWin effect and the GPL daemon link it (GPL→LGPL linking is
permitted), so there is exactly **one** evaluator implementation — eliminating the
daemon/effect match-code duplication.

```
libs/phosphor-rule/
  include/PhosphorRule/
    MatchExpression.h     — composable predicate tree
    WindowQuery.h         — attribute bag evaluated against an expression
    RuleAction.h          — pluggable action descriptor + ActionRegistry
    Rule.h          — { id, name, enabled, priority, match, actions }
    RuleSet.h       — ordered collection; serialization; resolution
    RuleEvaluator.h       — evaluation + match cache
  src/...
  tests/...
```

### 4.2 `MatchExpression` — the predicate tree

An expression is either a **leaf predicate** or a **composite**:

- **Leaf** — `Predicate { Field field; Operator op; QVariant value; }`
- **Composite** — `All { children }` (AND), `Any { children }` (OR), `None { children }` (NOT-any)

**Fields** (`enum class Field`):

| Group | Fields |
|---|---|
| Window identity | `AppId`, `WindowClass`, `DesktopFile`, `WindowRole`, `Pid` |
| Window content | `Title` |
| Window state | `WindowType`, `IsSticky`, `IsFullscreen`, `IsMinimized` |
| Context | `ScreenId`, `VirtualDesktop`, `Activity` |

**Operators** (`enum class Operator`):

| Operator | Applies to | Notes |
|---|---|---|
| `Equals` | any | case-insensitive for strings |
| `Contains`, `StartsWith`, `EndsWith` | strings | case-insensitive |
| `Regex` | strings | `QRegularExpression`, precompiled & cached per predicate |
| `AppIdMatches` | `AppId` | segment-aware reverse-DNS match — reuses existing `PhosphorIdentity::WindowId::appIdMatches()` |
| `In` | `VirtualDesktop`, `Activity`, `ScreenId` | value is a set |
| `Equals`/`GreaterThan`/`LessThan` | `Pid`, `VirtualDesktop` | numeric compare |

A context-only expression (only `ScreenId`/`VirtualDesktop`/`Activity` predicates, no
window fields) is a fully valid expression — it simply matches every window in that
context, and matches a windowless context query too. **This is how zone Assignments become
rules.**

### 4.3 `WindowQuery` — the evaluation input

```cpp
struct WindowQuery {
    // Window attributes — absent when evaluating a windowless context query.
    std::optional<QString> appId, windowClass, title, windowRole, desktopFile;
    std::optional<int>     pid;
    std::optional<WindowType> windowType;
    bool isSticky = false, isFullscreen = false, isMinimized = false;
    // Context attributes — always present.
    QString screenId;
    int     virtualDesktop = 0;   // 0 = all desktops
    QString activity;             // empty = all activities
};
```

A predicate on an **absent** window field evaluates to `false` (it cannot match). This is
what makes window-property rules naturally inert during windowless context resolution
without any special-casing.

### 4.4 `RuleAction` — pluggable, slot-based

Each action declares a **slot**. Conflict resolution is **first-matching-rule-wins per
slot**; actions in different slots stack. This exactly preserves the current
`AnimationAppRule` behaviour, where the shader axis and timing axis resolve independently.

| Action type | Slot | Replaces |
|---|---|---|
| `SetEngineMode` | `engine-mode` | Assignment `mode` |
| `SetSnappingLayout` | `layout` | Assignment `snappingLayout` |
| `SetTilingAlgorithm` | `layout` | Assignment `tilingAlgorithm` |
| `DisableEngine` | `engine-enable` | per-mode disable lists |
| `Exclude` | `manage` (terminal) | effect exclusion lists |
| `Float` | `float` | — (new, was implicit) |
| `OverrideAnimationShader` | `anim-shader:<event>` | `AnimationAppRule` shader kind |
| `OverrideAnimationTiming` | `anim-timing:<event>` | `AnimationAppRule` timing kind |
| `SetOpacity` | `opacity` | — (future) |
| *future* | *new slot* | — |

`Exclude` is **terminal**: once a rule with `Exclude` matches, evaluation stops and the
window is not managed at all.

Action types register with an `ActionRegistry` that provides their slot, JSON
(de)serialization, validation, and UI editor descriptor. **Adding a future rule type =
registering one action.** No new matcher, no new file, no new UI page.

### 4.5 `RuleEvaluator` — one evaluation model

There is a single evaluation model. `RuleEvaluator::resolve(query)` walks `RuleSet`
in **descending priority** (ties broken by list order), accumulates the first action that
fills each slot, and returns the resolved action set.

- **Per-window evaluation** — `query` carries window attributes; window-property and
  context predicates both apply.
- **Windowless context evaluation** — `query` carries only context attributes;
  window-property predicates evaluate `false`, so only context-only rules contribute. This
  reproduces the old Assignment cascade.

**Priority encodes specificity.** The Assignment cascade (exact → activity → desktop →
screen → default) is reproduced by assigning higher priority to rules that pin more
context dimensions. The migration computes these priorities; the catch-all provider
default becomes a lowest-priority rule with an empty match expression.

**Match cache.** Keyed by `(windowId, ruleSetRevision)` → resolved action set; invalidated
on any rule edit or relevant window-metadata change. This is a net new capability — today
nothing is cached.

---

## 5. Serialization

### 5.1 New file: `~/.config/plasmazones/rules.json`

A **single dedicated file**, separate from `config.json`. Rationale:

- Satisfies the discussion's "one serialization format".
- Kept separate from `config.json` so frequent daemon-driven rule writes (assignment
  changes) do not churn the cold user-settings blob — the same reason `assignments.json`
  was split out at schema v2.
- The daemon-is-sole-writer model (settings architecture refactor) means there is no
  cross-process write contention; one file is safe.

`assignments.json` is **superseded** by `rules.json`. The `Animations/AnimationAppRules`
array and `Display.*Disabled*` keys are removed from `config.json` by the migration.

### 5.2 Rule object shape

```jsonc
{
  "_version": 4,
  "rules": [
    {
      "id": "{uuid}",          // QUuid, toString() with braces
      "name": "Keep VS Code dialogs floating",
      "enabled": true,
      "priority": 720,         // higher evaluated first; ties → list order
      "match": { /* MatchExpression */ },
      "actions": [ /* RuleAction[] */ ]
    }
  ]
}
```

**Match expression** — leaf or composite:

```jsonc
{ "field": "windowClass", "op": "contains", "value": "firefox" }   // leaf
{ "all":  [ /* ... */ ] }   // AND
{ "any":  [ /* ... */ ] }   // OR
{ "none": [ /* ... */ ] }   // NOT (none-of)
```

**Worked examples** (see also the SVG mockup):

```jsonc
// Migrated zone Assignment — context-only match, most specific → high priority
{
  "id": "{...}", "name": "DP-2 · Desktop 2 · Work", "enabled": true, "priority": 400,
  "match": { "all": [
    { "field": "screenId",       "op": "equals", "value": "DP-2" },
    { "field": "virtualDesktop", "op": "equals", "value": 2 },
    { "field": "activity",       "op": "equals", "value": "{work-uuid}" }
  ]},
  "actions": [
    { "type": "setEngineMode",      "mode": "autotile" },
    { "type": "setTilingAlgorithm", "algorithm": "dwindle" },
    { "type": "setSnappingLayout",  "layoutId": "{grid-uuid}" }   // kept → lossless mode toggle
  ]
}

// Migrated animation App Rule — window-property match
{
  "id": "{...}", "name": "Firefox open animation", "enabled": true, "priority": 200,
  "match": { "field": "windowClass", "op": "contains", "value": "firefox" },
  "actions": [
    { "type": "overrideAnimationShader", "event": "window.open",
      "effectId": "dissolve", "params": { "scale": 0.8, "blur": 12 } }
  ]
}

// Composite rule — the capability the refactor unlocks
{
  "id": "{...}", "name": "Keep VS Code dialogs floating", "enabled": true, "priority": 720,
  "match": { "all": [
    { "field": "appId", "op": "appIdMatches", "value": "code" },
    { "any": [
      { "field": "windowType", "op": "equals", "value": "dialog" },
      { "field": "title",      "op": "regex",  "value": "Settings|Preferences|About .*" }
    ]},
    { "none": [
      { "field": "screenId", "op": "equals", "value": "eDP-1" }
    ]}
  ]},
  "actions": [
    { "type": "float" },
    { "type": "setOpacity",  "value": 0.95 },
    { "type": "setEngineMode", "mode": "snapping" }
  ]
}
```

### 5.3 Loader robustness

Follow the proven `AnimationAppRule::fromJson()` discipline: strict validation gates, drop
malformed rules with a logged diagnostic, canonicalize-on-save (compare round-tripped JSON
to detect schema drift). Never silently corrupt the rule set.

---

## 6. Settings UI

One page replaces all five current surfaces. The page is **approachable by default,
powerful on disclosure** — not "an advanced editor". See `docs/rules-page-mockup.svg`.

### 6.1 Page structure

- **Monitors overview** — a compact, *read-only* row of monitor tiles (assigned layout,
  tiling-on/off, rule count, unassigned state). It is a visualization, not an editor;
  clicking a tile filters the list. It preserves the at-a-glance spatial overview that the
  current `MonitorAssignmentsCard` provides.
- **Search + `Add rule`** and **filter chips** (All / Monitor / Application / Activity /
  Animation).
- **Grouped rule list** — one store, section headers derived from each rule's primary
  subject: *Monitor & Layout*, *Applications*, *Activities*, *Animations*, *Advanced /
  Custom*.
- Each row: enabled dot · human-readable match summary · `→` · action summary ·
  edit/delete. Composite rules show condition/action-count badges.

### 6.2 Priority is never a raw number where it does not belong

- *Monitor & Layout* etc. — priority is **derived from match specificity**; the user only
  picks layouts.
- *Animations* — priority is **list order**; drag to reorder.
- *Advanced / Custom* — priority is exposed as an integer in the expanded editor only.

### 6.3 Guided authoring

`Add rule` opens a **subject chooser** (`When: Monitor ▾ / Application ▾ / Activity ▾ /
Custom…`). The common case ("assign layout to monitor") stays a two-dropdown operation.
The AND/OR/NOT tree editor appears only under *Custom* or via *Add condition*. A rule
edited beyond a section's representable shape "graduates" into *Advanced / Custom*.

### 6.4 Controller / D-Bus consolidation

`AnimationsPageController`, `SnappingBridge`, and `TilingBridge` collapse into a single
`RuleController` exposing one `RuleModel` (`QAbstractListModel`). The settings
app talks to one D-Bus adaptor for the rule store; the daemon remains sole writer.

**Deleted** (superseded, not rewired): `MonitorAssignmentsCard.qml`,
`ActivityAssignmentsCard.qml`, `SnappingAssignmentsPage.qml`, `TilingAssignmentsPage.qml`,
`AssignmentsAppRulesPage.qml`, `AppRulesCard.qml`, and the App-Rules portion of
`AnimationsAppRulesPage.qml`. Salvageable components (the monitor visualization, the layout
combo from `AssignmentRow.qml`) move into the new page.

---

## 7. Prerequisite: window-metadata plumbing

The discussion's target fields include `role`, `virtualDesktop`, and `activity`, which are
not currently available to the matcher. This is **Phase 0** — the engine is only as good as
its inputs.

| Attribute | Current state | Work |
|---|---|---|
| `windowRole` | in KWin API, never captured | add to `setWindowMetadata` D-Bus call + `WindowRegistry::WindowMetadata` |
| `pid` | used effect-locally, not in daemon registry | add to metadata payload |
| per-window `virtualDesktop` / `activity` | effect detects crossings (`window_lifecycle.cpp`), daemon registry does not store per-window | land those signals into `WindowRegistry` |
| `windowType` | scattered boolean `isDialog()`/`isUtility()`/… checks | extract into a `WindowType` enum, add to metadata |

---

## 8. Migration (schema v3 → v4)

Per `CLAUDE.md`, schema-version migrations are the **only** sanctioned place for structural
restructuring; no ad-hoc per-key fallbacks.

1. `configmigration.h` — `ConfigSchemaVersion = 4`; declare `migrateV3ToV4`.
2. `configmigration.cpp` — register in `migrationSteps()` and the PhosphorConfig schema;
   implement `migrateV3ToV4`.
3. `migrateV3ToV4` performs a **lossless** consolidation into `rules.json`:
   - Each `assignments.json` `AssignmentEntry` → a context-only `Rule`. Priority
     derived from pinned-dimension count so list resolution reproduces the cascade
     (activity-pinned > desktop-pinned > screen-only). Provider defaults → lowest-priority
     empty-match rules.
   - Each `AnimationAppRule` → a `Rule` (`WindowClass Contains <pattern>` →
     `OverrideAnimation*` action). List order preserved as priority.
   - Each `Display.*Disabled*` entry → a context rule with a `DisableEngine` action.
   - Each exclusion-list entry → a rule with an `Exclude` action.
4. Old keys (`Animations/AnimationAppRules`, `Display.*Disabled*`) are dropped;
   `assignments.json` is removed after successful conversion.

---

## 9. Phasing

| Phase | Scope | Ships |
|---|---|---|
| **0** | Metadata plumbing — `windowRole`, `pid`, per-window desktop/activity, `WindowType` enum into `WindowRegistry` + D-Bus | independently |
| **1** | `phosphor-rule` library — `MatchExpression`, `WindowQuery`, `RuleAction` + registry, `RuleSet`, `RuleEvaluator`, match cache, full Qt Test suite | independently |
| **2** | Wire window-property matchers (Animation App Rules + effect exclusion lists) through the evaluator; both effect and daemon link the library | independently |
| **3** | Bring context matchers (Assignments, per-mode disable lists) in as context-only rules; schema `v3→v4` migration; `rules.json` becomes the store | with migration |
| **4** | New unified settings page + `RuleController`; delete the five legacy UIs | with Phase 3 |
| **5** | New per-window capabilities (per-window engine override, opacity, scroll-width) — each is just a new registered action | incrementally |

Phases 0–2 are non-breaking and independently shippable. Phases 3–4 land together (the
schema migration and the UI re-point must be atomic). Phase 5 is open-ended upside.

---

## 10. Risks and open questions

- **Cascade ↔ priority fidelity.** Reproducing the Assignment cascade — including
  "activity beats desktop" and provider-default fallback — via priority ordering is the
  highest-risk correctness work. Needs a dedicated test suite mirroring
  `test_layoutmanager_assignment.cpp`.
- **Mode-toggle losslessness.** `AssignmentEntry` stores both `snappingLayout` and
  `tilingAlgorithm` so toggling mode is lossless. The migrated rule must carry both
  `SetSnappingLayout` and `SetTilingAlgorithm` actions plus `SetEngineMode`.
- **Evaluation cost.** Assignment resolution is a cheap hash cascade today; as rule
  evaluation it is O(rules) per query. The match cache covers this; context queries are
  not hot. Confirm with a benchmark in Phase 1.
- **Effect/daemon parity.** Solved by both linking one library — but the build must ensure
  the effect's GPL target links the LGPL `phosphor-rule` cleanly.
- **UI graduation UX.** The "rule outgrew its section" case must be communicated clearly,
  never silently corrupt a rule a section cannot fully represent.
- **Test migration.** The three legacy subsystems have strong test suites; those become
  integration tests. The new engine needs its own unit suite.

---

## 11. References

- Discussion #240 — roadmap.
- Project memory: `project_window_rule_refactor.md`, `project_per_mode_disable_lists.md`,
  `project_explicit_assignments.md`.
- `docs/rules-page-mockup.svg` — settings UI mockup.
- Current subsystems: `libs/phosphor-zones/` (Assignments), `libs/phosphor-animation/`
  (Animation App Rules), `libs/phosphor-tile-engine/PerScreenConfigResolver`,
  `kwin-effect/plasmazoneseffect/window_filtering.cpp`, `src/config/configmigration.cpp`.
