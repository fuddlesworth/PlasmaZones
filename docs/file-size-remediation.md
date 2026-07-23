<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# File Size Remediation Plan

Catalog of every tracked source file at or above the 1000-line target as of 2026-07-22
(branch `v3.3`), with a split plan or a documented exception for each.

**Policy recap** (from CLAUDE.md): under 1000 lines is the target; 1000 to 1150 is the
grace band, tolerated and not a review finding on its own; past 1150 a file must be
split by concern. Files listed in the Exceptions Register below are sanctioned to
exceed the ceiling for the stated structural reason and are NOT review findings;
everything else over 1150 is scheduled work.

## Status: executed 2026-07-22

All split plans in §2 were executed on branch `refactor/file-size` in three waves,
each verified with both build trees (unity and no-unity) and a full ctest run
(311/311 passing). After remediation, the only files above the 1150-line ceiling
are the six entries in the Exceptions Register below (current sizes:
StubSettings.h 2338, plasmazoneseffect.h 2170, settings.h 1704, AutotileEngine.h
1635, windowtrackingadaptor.h 1269, pluau.luau 1189). Everything else is at or
under 1150; the grace-band list in §4 remains accurate as the set of tolerated
files with their pre-identified seams. The §4 catalog shows the pre-remediation
sizes for the historical record.

Verdict legend:

- **SPLIT** — over the hard ceiling, has a concrete plan below, scheduled work.
- **EXCEPTION** — over the ceiling but structurally unsplittable (or splitting is
  actively harmful); recorded in the Exceptions Register with justification and
  revisit trigger.
- **TOLERATED** — in the 1000–1150 grace band. No action required. Where an easy
  seam exists it is noted so the next person to grow the file knows where to cut.

---

## 1. Exceptions Register

These files are sanctioned to exceed the 1150-line ceiling. Each entry names the
constraint and the condition under which it should be revisited.

| File | Lines | Constraint | Revisit when |
|---|---|---|---|
| `src/config/settings.h` | 1704 | Single Q_OBJECT class with 217 Q_PROPERTY declarations behind `ISettings`. moc requires the whole class declaration in one header. Splitting into domain sub-settings QObjects changes QML property paths, the D-Bus settingsadaptor surface, and the ISettings interface consumed by daemon/editor/settings; that is a cross-cutting refactor, not a file-size fix. | ISettings is ever decomposed into domain sub-objects (long-term direction already noted at settings.h:119). |
| `tests/unit/helpers/StubSettings.h` | 2338 | 309 overrides mirroring the aggregate ISettings surface. Size is a direct mirror of the production interface, not test bloat. Splitting into mixin headers fragments shared member state (documented in-file cross-references) for zero gain. **Dependent exception**: shrinks automatically when settings.h does. | Same trigger as settings.h; then split into per-domain stub mixins along the existing section comments. |
| `src/dbus/windowtrackingadaptor/windowtrackingadaptor.h` | 1269 | Single moc'd Q_CLASSINFO D-Bus adaptor class (`org.plasmazones.WindowTracking`). Splitting the header means splitting the D-Bus interface itself, a wire-protocol change affecting the KWin effect, phosphorctl, tests, and the checked-in XML. ~60% of the lines are safety-critical bus-exposure doc comments. | A protocol-version bump ever happens anyway; then consider a second adaptor class (query surface) on the same object path. |
| `kwin-effect/plasmazoneseffect/plasmazoneseffect.h` | 2272 | **Partial exception.** Single Q_OBJECT class deriving KWin::OffscreenEffect; all overrides, signals, and slots must sit in one class declaration. Planned type/state-struct extraction (see §2.1) gets it to roughly 1550–1650; the KWin API puts a floor of ~600–800 lines of unavoidable declarations under it, and the rest of the reduction requires a real delegation refactor (manager objects), tracked as follow-up work, not a quick split. | After the §2.1 extractions land, reassess whether a DecorationManager-style delegation is worth the effort. |
| `libs/phosphor-tiles/src/pluau/pluau.luau` | 1189 | Luau sandbox prelude loaded as a single frozen chunk (`runPrelude` in luautilealgorithm.cpp:317). There is no module system in the sandbox; splitting into sequential preludes would force helpers into frozen **globals** visible to untrusted user scripts, growing the security/API surface. | If it keeps growing, add a CMake/qrc-time concatenation step so it can be multiple source files but one runtime chunk. |
| `libs/phosphor-tile-engine/include/PhosphorTileEngine/AutotileEngine.h` | 1761 | **Partial exception.** Single Q_OBJECT class; 1174 of 1761 lines are Doxygen prose. Nested-struct extraction plus doc trimming (see §2.2) lands it around 1100–1200. Any residual over 1150 is documentation on a language-atomic class declaration. | After the §2.2 extraction lands, if still over ceiling, accept the remainder. |

Grace-band headers that look like exceptions but are merely TOLERATED (single-class
headers under 1150, no action): `src/daemon/daemon.h` (1150), `src/daemon/overlayservice.h`
(1137), `src/settings/controller/settingscontroller.h` (1149),
`PhosphorZones/LayoutRegistry.h` (1119), `PhosphorSnapEngine/SnapEngine.h` (1079).
Warning for daemon.h: it now sits exactly at 1150 after one round of doc-essay
trimming, so any further declaration must be paid for by trimming again.

Completed split: `src/daemon/daemon/signals.cpp` (was 1088) had
`initializeAutotile()` extracted to `src/daemon/daemon/autotile_init.cpp`,
leaving 625 and 568 lines respectively.

`src/ui/PassiveOverlayShell.qml` (1047) is TOLERATED but flagged **near-exception**:
C++ (`OverlayService`) writes properties directly onto its slot Items and inner
Components resolve them via QML lexical scope by design. Splitting it breaks that
contract across every OSD/overlay path. Do not split casually; if it ever passes 1150,
extract mainOverlaySlot first.

---

## 2. Split plans (files over the 1150 ceiling)

Ordered by size. Effort: S under half a day, M about a day, L multi-day. All new
files must be added to the owning CMake source list and carry SPDX headers with the
tree-correct license (LGPL under `libs/`, GPL elsewhere).

### 2.1 kwin-effect (all splittable; the class is already partitioned across ~30 .cpp files)

**`paint_pipeline.cpp` (2441) — L, medium risk (hot path).**
Keep screen passes + prePaintWindow + the thin non-shader tail of paintWindow (~950).
Extract the ~970-line shader-transition branch of `paintWindow` into a private method
`paintShaderTransitionWindow(...)` in new `paint_shader_window.cpp` (~1000); it mutates
~15 locals of paintWindow, so pass a small context struct and return a handled/terminate
bool for its early-return paths. Move `captureOldWindowSnapshot` + the quad-deform
`apply` hook to `paint_capture.cpp` (~560). Promote the shared anon-namespace helpers
(lines 41–154) into `shader_internal.h` or a new `paint_internal.h`. Preserve exact
ordering around the re-entrant `captureOldWindowSnapshot`/drawWindow path.

**`shader_transitions.cpp` (2422) — M, medium risk.**
Three cuts: (a) `shader_config_dbus.cpp` takes the D-Bus loaders (lines ~1831–2422,
~600); (b) `shader_textures.cpp` takes the texture LRU/warm-up + anon-ns texture
helpers (~350); (c) extract the self-contained GL compile/vertex-stage-assembly block
(~300 lines) out of `beginShaderTransition`. Final: transitions ~1100, config ~600,
textures/compile ~650. Keep the supersession-carry block intact (pointer invalidated
by erase; strict ordering).

**`plasmazoneseffect.h` (2272) — M now, L follow-up. Partial exception (see §1).**
Now: extract nested types (FocusFadeState, WindowAppearanceDefault,
PendingFrameGeometry, CompiledPackResolver, WindowLayerSnapshot, MinimizeShaderStamp)
to namespace scope in a new `effect_state.h` (~250–350 lines out); group the trailing
drag-activation/daemon-gate/id-cache member blocks into 2–3 plain structs held by
value (~250 more, wide-but-shallow diff across ~30 .cpp files). Lands ~1550–1650.
Follow-up: DecorationManager-style delegation mirroring ShaderTransitionManager.

**`lifecycle.cpp` (1502) — S/M, low risk.**
Decompose the ~1110-line constructor along its existing comment seams into 4–6 private
init methods (`initMotionClocks()`, `initTimersAndDebounce()`,
`connectGlobalWindowSignals()`, `initDaemonWatcher()`, …) defined in new
`lifecycle_wiring.cpp` (~700). Ctor shell + dtor + state methods stay (~750).
Init-list stays in the ctor; preserve call order exactly ("connect signals FIRST,
then iterate screens").

**`window_lifecycle.cpp` (1495) — S, low risk.**
Move `setupWindowConnections` + `beginMaximizeShaderMorph` into new
`window_connections.cpp` (~790); slots + notify* + lookups stay (~710). Optionally
move the `findWindowById*` trio into the existing `window_query.cpp` (check its size
first).

**`daemon_bringup.cpp` (1378) — S, low risk.**
New `daemon_settings.cpp` takes `slotSettingsChanged` + the `loadSettingAsync` member
template + `loadCachedSettings` (lines ~566–1225, ~680). All users of the template
live in the new file, so it needs no header. Bringup sequence + navigation wiring
stay (~700).

### 2.2 phosphor-tile-engine

**`AutotileEngine.cpp` (5177) — L, low-medium risk. Worst offender in the repo.**
Partition one class across ~12 concern files in a new `src/autotileengine/` dir,
mirroring the daemon/effect pattern. Bands are contiguous in the current file:

| New file | Content (current lines) | ~Lines |
|---|---|---|
| `core.cpp` | preamble, ctor, window tracking (1–463) | 500 |
| `context.cpp` | queries, desktop/activity context, sticky pins, setAutotileScreens (463–959) | 530 |
| `algorithm_state.cpp` | algorithm select, tilingStateForScreen, script-state stash (959–1465) | 530 |
| `config.cpp` | refreshConfigFromSettings, per-screen overrides, effective* (1465–1834) | 400 |
| `retile.cpp` | retile scheduling/retry, order ops (1834–2163 + 4696–4820) | 500 |
| `float_handoff.cpp` | float toggle, handoff receive/release (2163–2506) | 380 |
| `window_lifecycle.cpp` | opened/closed/focused, min-size, migration, revalidate (2506–3149) | 700 |
| `resize.cpp` | onWindowResized, tree-resize reflow, geometry changes (3149–3462) | 330 |
| `insert.cpp` | insert/remove window, insert-float sync (3462–3734) | 330 |
| `layout_apply.cpp` | recalculateLayout, applyTiling, shouldTileWindow, backfill (3734–4034 + 4341–4696) | 750 |
| `drag_preview.cpp` | drag-insert preview state machine (4034–4341) | 330 |
| `facade.cpp` | window-id canonicalization, NavigationContext facade, IPlacementEngine (4820–5177) | 450 |

Shared anon-namespace helpers and file-local `using` aliases move to an internal
`engine_internal.h` (pattern: kwin-effect's `shader_internal.h`). Optional second
pass: extract a `WindowIdCanonicalizer` helper class and the drag-preview state
machine as real classes; deferred because they read engine state directly.

**`AutotileEngine.h` (1761) — M. Partial exception (see §1).**
Extract nested structs `MigrationArrival`, `StashedScriptState`, `DragInsertPreview`
(with doc blocks) into `AutotileEngineTypes.h` (~300–350 out) as namespace-level
types; relocate design-history prose into the new .cpp files during the split.
Target ~1100–1200.

### 2.3 src/config and src/settings

**`settings.cpp` (3876) — L, low-moderate risk.**
Extend the existing `src/config/settings/` partial dir: `disable.cpp` (~530),
`animationprofile.cpp` (~430), `profiletrees.cpp` (~340), `uienums.cpp` (~420),
`triggers.cpp` (~500), `systemcolors.cpp` (~300). Core load/save/reset stays
(~950–1100). Shared anon-ns helpers (`resolveScreenId`, `canonicalDisableEntries`,
`parseCommaList`, `writeProfileObject`) and the `patchProfileField` template move to
a private `settings/settings_detail.h` (precedent: `animations_controller_detail.h`).

**`configmigration.cpp` (3765) — M/L, moderate risk.**
Per-version files matching the append-only registry design; each future bump adds a
file instead of growing this one. `configmigration.cpp` keeps the chain
runner/guard/INI-to-JSON (~670); then `configmigration_v2.cpp` (~590),
`configmigration_v3.cpp` (~145), `configmigration_v4.cpp` (~310),
`configmigration_v4rules.cpp` (animation-app-rule conversion, ~680),
`configmigration_v4finalize.cpp` (~950). The v4 stash-key constants shared between
writer and finalizer go to `configmigration_v4detail.h`; the dot-path JSON helpers
to `configmigration_util.h`. Do not change idempotency-guard semantics; migration
tests provide coverage.

**`configdefaults.h` (1931) — M, low risk.**
Split via chained inheritance so every `ConfigDefaults::foo()` call site is untouched:
`configdefaults_appearance.h` → `_gaps.h` → `_limits.h` → `_shaders.h` → `_screens.h`,
each a class inheriting the previous, with `ConfigDefaults` as the final link.
Order the chain so cross-referenced accessors come earlier.

**`ruleauthoring.cpp` (1284) — S, very low risk.**
Pure free-function catalog. New `ruleauthoring_actions.cpp` takes the action-type
catalog (~640: paramLabel/paramHint/paramsForActionType/actionTypes/defaultPayloadFor);
match-side stays (~640). Shared option-list helpers are already namespace-public via
the header.

### 2.4 Libraries

**`phosphor-animation/src/surfaceanimator.cpp` (2614) — L, medium risk.**
Introduce `surfaceanimator_p.h` declaring `Private`, `SteadyClock`,
`ShaderAttachResult`, and detail helpers (anon-ns helpers gain external linkage under
a `detail` namespace; the file already uses an `internal` namespace to dodge
-Wsubobject-linkage). Then: `surfaceanimator_shaderattach.cpp` (anchor search,
sibling hiding, attachShaderToAnchor, ~650), `surfaceanimator_tracks.cpp`
(runLeg, teardown, reuse cache, cancel, ~800), `surfaceanimator_tick.cpp`
(tickAll, uniform push/seed, ~300). Public API stays (~450). Private's inline
method bodies become out-of-line; large but mechanical diff.

**`phosphor-rendering/src/shadereffect.cpp` (1603) — M, low risk.**
`shadereffect_params.cpp` takes setShaderParams + custom params/colors + audio +
user textures + wallpaper (~600, optionally plus buffer setters). Lifecycle,
timing, source handling, preamble/scaffold, node creation stay (~900). Precedent
in-lib: `shadernoderhicore/pipeline/setters/uniforms.cpp`.

**`phosphor-rules/src/ruleaction.cpp` (1445) — S, very low risk.**
`registerBuiltins()` is a ~1100-line pure data table of ~49 ActionDescriptor
registrations. Split along its existing banners into `registerBuiltinsEngine()` /
`registerBuiltinsAppearance()` (+ misc) in `ruleaction_builtins_engine.cpp` and
`ruleaction_builtins_appearance.cpp` (~550 each); core to/fromJson + registry stays
(~350). Preserve registration order by calling sub-methods in sequence.

**`phosphor-placement/src/lifecycle.cpp` (1327) — S/M, low risk.**
Move the cohesive virtual-screen migration cluster (lines 82–660, ~590:
findNearestVirtualScreen, migrate*To/FromVirtual, prune helpers) into new
`virtualscreenmigration.cpp`. Rest stays (~740).

**`phosphor-placement/include/.../WindowTrackingService.h` (1276) — M, moderate risk.**
Move non-trivial inline bodies out-of-line (~150–250 saved; route them to the
concern-matching sibling .cpp, keeping WindowTrackingService.cpp under ~1000) and
extract the public `SnapStateResolver` struct into its own
`PhosphorPlacement/SnapStateResolver.h` (grep consumers first; daemon/tests include
changes). Lands ~950–1050; residual is tolerated.

### 2.5 src/daemon and src/dbus

**`daemon.cpp` (2877) — L, medium risk (init ordering is documented as intentional).**
Continue the `src/daemon/daemon/` partition: `shader_warmup.cpp` (~650: animation/
surface shader setup + the warm-bake lambda blocks lifted out of init()),
`init_services.cpp` (~550: settings/layout wiring, settings-changed handler, resnap),
`init_adaptors.cpp` (~850: adaptor construction + engines factory + context-resolver
wiring), `lifecycle.cpp` (~730: start(), bridge warnings, the ~395-line stop(),
plasma workspace state). daemon.cpp keeps ctor/dtor + a thin init() calling the
phases in the exact current order (~350). init() locals shared across blocks
(the engines factory result) become parameters or members.

**`windowtrackingadaptor.cpp` (1292) — S, low risk.**
Follow the adaptor's existing 8-file pattern: new `lifecycle.cpp` (~600: capture/
prune/windowClosed/windowActivated/metadata/geometry notifications) and `queries.cpp`
(~350: getZoneForWindow, getWindowsInZone, geometry getters). Ctor + setters +
validation stay (~350). Note: these files are hand-written, not generated (generation
only occurs in phosphor-service-notifications/-sni into build dirs).

### 2.6 src/editor

**`EditorController.h` (1160) — M, medium risk. Only 10 lines over ceiling.**
Extract the ~20 gap properties into a new `EditorGapsModel` QObject exposed as
`Q_PROPERTY(EditorGapsModel* gaps READ gaps CONSTANT)` backed by the existing
`controller/gaps.cpp`; QML call sites change mechanically from `controller.outerGapTop`
to `controller.gaps.outerGapTop` (PropertyPanel.qml, LayoutSettingsDialog.qml).
The sub-object keeps a back-pointer for hasUnsavedChanges, same pattern as the
existing services.

### 2.7 Tests

Qt Test files split by creating additional test classes/executables (one more
`p_add_test`/`pa_add_test` line each) and extracting shared fixtures into headers
under `tests/unit/helpers/` (precedent: VirtualScreenTestHelpers.h). Frozen migration
tests still split; the moves are mechanical and one-time.

- **`test_migration_v3_to_v4.cpp` (2448) — M.** Fixture to `MigrationV3V4Fixture.h`;
  new targets `..._animations` (~700) and `..._exclusions` (~450); core rules/cascade/
  idempotency stays (~1150; move the supersede group over if still tight).
- **`test_snap_engine.cpp` (2101) — M.** Fixture header + new targets
  `test_snap_engine_restore` (~900, the still-growing concern) and
  `test_snap_engine_exclude` (~450); lifecycle/float/save-load stays (~800).
- **`test_configmigration.cpp` (1772) — S/M.** Extract `test_migration_v1_animation.cpp`
  (~450) and `test_layoutsettings_relocation.cpp` (~250); frozen core chain stays (~1070).
- **`test_rule_cascade_fidelity.cpp` (1642) — M.** Fixture to `RuleCascadeFixture.h`;
  new `test_rule_cascade_context` (~800, active growth area); precedence/tie-break
  stays (~650).
- **`test_rule_controller.cpp` (1518) — S/M.** New `test_rule_controller_overview`
  takes the ten monitorOverview slots (~500–600); measure, then optionally move the
  vocabulary/metadata slots too.
- **`test_animationshaderregistry.cpp` (1395) — S.** New
  `pa_test_shaderparamtranslation` (~800: translateAnimationParams/slot-key/color/
  texture tests); registry discovery + parse-security tests stay (~600).
- **`test_layoutmanager_assignment.cpp` (1323) — S/M.** New
  `test_layoutmanager_default_synthesis` (~560: Level1Default/suppression); rest ~760.
- **`test_surface_animator.cpp` (1191) — S.** Extract the harness plumbing to
  `SurfaceAnimatorTestHarness.h` (~140 out, lands in grace band). Move the
  cancel/UAF block unmodified if a second target is ever needed.

---

## 3. Suggested sequencing

1. **Wave 1 (S efforts, near-zero risk):** ruleaction.cpp, ruleauthoring.cpp,
   phosphor-placement lifecycle.cpp, layoutregistry_assignments.cpp (grace-band
   opportunistic), windowtrackingadaptor.cpp, enginewiring.cpp, daemon_bringup.cpp,
   window_lifecycle.cpp, the S-effort test splits.
2. **Wave 2 (M):** configmigration.cpp, configdefaults.h, shadereffect.cpp,
   shader_transitions.cpp, effect lifecycle.cpp, EditorController.h,
   WindowTrackingService.h, AutotileEngine.h, plasmazoneseffect.h type extraction,
   remaining test splits.
3. **Wave 3 (L, one PR each with focused review):** AutotileEngine.cpp, settings.cpp,
   daemon.cpp, surfaceanimator.cpp, paint_pipeline.cpp.
4. Record follow-ups: plasmazoneseffect.h delegation refactor; settings.h/StubSettings.h
   revisit on ISettings decomposition.

Every wave: build both trees (`build/` and `build-nounity/`; unity builds mask missing
includes) and run ctest before committing. New files go into the owning CMake source
list; missing QML files in `qt6_add_qml_module` fail at runtime, not build time.

---

## 4. Full catalog (≥950 lines, 2026-07-22)

| Lines | File | Verdict |
|---|---|---|
| 5177 | libs/phosphor-tile-engine/src/AutotileEngine.cpp | SPLIT (§2.2) |
| 3876 | src/config/settings.cpp | SPLIT (§2.3) |
| 3765 | src/config/configmigration.cpp | SPLIT (§2.3) |
| 2877 | src/daemon/daemon.cpp | SPLIT (§2.5) |
| 2614 | libs/phosphor-animation/src/surfaceanimator.cpp | SPLIT (§2.4) |
| 2448 | tests/unit/config/migrations/test_migration_v3_to_v4.cpp | SPLIT (§2.7) |
| 2441 | kwin-effect/plasmazoneseffect/paint_pipeline.cpp | SPLIT (§2.1) |
| 2422 | kwin-effect/plasmazoneseffect/shader_transitions.cpp | SPLIT (§2.1) |
| 2338 | tests/unit/helpers/StubSettings.h | EXCEPTION (dependent) |
| 2272 | kwin-effect/plasmazoneseffect/plasmazoneseffect.h | SPLIT partial + EXCEPTION |
| 2101 | tests/unit/core/snap/test_snap_engine.cpp | SPLIT (§2.7) |
| 1931 | src/config/configdefaults.h | SPLIT (§2.3) |
| 1772 | tests/unit/config/migrations/test_configmigration.cpp | SPLIT (§2.7) |
| 1761 | libs/phosphor-tile-engine/include/PhosphorTileEngine/AutotileEngine.h | SPLIT partial + EXCEPTION |
| 1704 | src/config/settings.h | EXCEPTION |
| 1642 | tests/unit/core/test_rule_cascade_fidelity.cpp | SPLIT (§2.7) |
| 1603 | libs/phosphor-rendering/src/shadereffect.cpp | SPLIT (§2.4) |
| 1518 | tests/unit/settings/rules/test_rule_controller.cpp | SPLIT (§2.7) |
| 1502 | kwin-effect/plasmazoneseffect/lifecycle.cpp | SPLIT (§2.1) |
| 1495 | kwin-effect/plasmazoneseffect/window_lifecycle.cpp | SPLIT (§2.1) |
| 1445 | libs/phosphor-rules/src/ruleaction.cpp | SPLIT (§2.4) |
| 1395 | libs/phosphor-animation/tests/test_animationshaderregistry.cpp | SPLIT (§2.7) |
| 1378 | kwin-effect/plasmazoneseffect/daemon_bringup.cpp | SPLIT (§2.1) |
| 1327 | libs/phosphor-placement/src/lifecycle.cpp | SPLIT (§2.4) |
| 1323 | tests/unit/core/layout/test_layoutmanager_assignment.cpp | SPLIT (§2.7) |
| 1292 | src/dbus/windowtrackingadaptor/windowtrackingadaptor.cpp | SPLIT (§2.5) |
| 1284 | src/settings/rules/ruleauthoring.cpp | SPLIT (§2.3) |
| 1276 | libs/phosphor-placement/include/PhosphorPlacement/WindowTrackingService.h | SPLIT (§2.4) |
| 1269 | src/dbus/windowtrackingadaptor/windowtrackingadaptor.h | EXCEPTION |
| 1191 | libs/phosphor-animation/tests/test_surface_animator.cpp | SPLIT (§2.7, helper extraction) |
| 1189 | libs/phosphor-tiles/src/pluau/pluau.luau | EXCEPTION |
| 1160 | src/editor/EditorController.h | SPLIT (§2.6) |
| 1149 | src/settings/controller/settingscontroller.h | TOLERATED |
| 1150 | src/daemon/daemon.h | TOLERATED at the ceiling; doc essays trimmed once already, next addition must trim again |
| 1137 | src/daemon/overlayservice.h | TOLERATED |
| 1131 | src/config/settingsschema.cpp | TOLERATED (seam: per-domain append functions) |
| 1130 | libs/phosphor-zones/src/layoutregistry_assignments.cpp | TOLERATED (easy seam: contextresolve vs CRUD) |
| 1128 | libs/phosphor-shaders/src/shaderregistry.cpp | TOLERATED (seam: pack parsing) |
| 1123 | src/settings/rules/rulemodel.cpp | TOLERATED (seam: label composers) |
| 1121 | src/settings/qml/pages/animations/AnimationEventCard.qml | TOLERATED (limited-yield js extraction) |
| 1119 | libs/phosphor-zones/include/PhosphorZones/LayoutRegistry.h | TOLERATED |
| 1111 | src/editor/controller/layout.cpp | TOLERATED (easy seam: launch.cpp) |
| 1107 | libs/phosphor-control/tests/test_application_controller.cpp | TOLERATED (seam: async batch band) |
| 1106 | tests/unit/dbus/test_wta_convenience.cpp | TOLERATED |
| 1099 | libs/phosphor-config/src/jsonbackend.cpp | TOLERATED (trivial seam: JsonGroup) |
| 1090 | src/daemon/daemon/start.cpp | TOLERATED (easy seam: shortcuts.cpp / virtual_screens.cpp) |
| 1089 | tests/unit/settings/stores/test_profilestore.cpp | TOLERATED |
| 1087 | src/editor/qml/PropertyPanel.qml | TOLERATED (twinFormLayouts obstacle) |
| 1080 | src/settings/stores/profilestore.cpp | TOLERATED (seam: CRUD partial) |
| 1079 | libs/phosphor-snap-engine/include/PhosphorSnapEngine/SnapEngine.h | TOLERATED |
| 1074 | tests/unit/core/screens/test_virtual_screen.cpp | TOLERATED |
| 1062 | src/dbus/windowtrackingadaptor/enginewiring.cpp | TOLERATED (easy seam: rules.cpp / crossmode.cpp) |
| 1047 | src/ui/PassiveOverlayShell.qml | TOLERATED, near-exception (C++ slot coupling) |
| 1044 | src/settings/qml/Main.qml | TOLERATED (seam: menu, profile handler) |
| 1022 | src/settings/qml/pages/screens/VirtualScreensPage.qml | TOLERATED (cleanest QML seam: geometry js lib) |
| 1019 | libs/phosphor-rules/tests/test_ruleaction.cpp | TOLERATED (growth-watch: action vocabulary) |
| 1015 | src/settings/controller/settingscontroller.cpp | TOLERATED |
| 1015 | src/editor/qml/EditorWindow.qml | TOLERATED (easy seam: EditorDialogs.qml) |
| 1003 | libs/phosphor-tiles/src/luautilealgorithm.cpp | TOLERATED |
| 997 | src/config/configkeys.h | OK (seam noted: Legacy struct) |
| 999 | tests/unit/config/settings/test_settings_core.cpp | OK |
| 992 | tests/unit/settings/services/test_algorithm_scaffold.cpp | OK |
| 991 | src/editor/qml/ShaderSettingsDialog.qml | OK |
| 985 | libs/phosphor-tile-engine/src/NavigationController.cpp | OK |
| 975 | libs/phosphor-layer/src/surface.cpp | OK |
| 964 | libs/phosphor-placement/src/WindowTrackingService.cpp | OK (receives WTS.h inline bodies; keep under ~1000) |
| 956 | src/settings/controller/settingscontroller_session.cpp | OK |
| 950 | kwin-effect/autotilehandler/tiling.cpp | OK (seam noted: geometry.cpp) |
