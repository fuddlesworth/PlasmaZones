<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Per-Mode Window Appearance — Implementation Plan

| | |
|---|---|
| **Status** | Implemented (PR #551). Historical, two layers: (1) the title-bar/borderless mechanics described below (`setWindowBorderless`, per-screen `borderlessWindowsByScreen` buckets) were superseded by the DecorationManager owner model in PR #608; (2) the §2/§5/§7 page-tree naming ("Zones" page as `SnappingZonesPage.qml` / `snapping-zones`, `kSnappingVisualChildren`, `snapping-visual-cat`) was superseded by the later Overlay/Window restructure — the implemented page is `SnappingOverlayAppearancePage.qml` registered as `snapping-overlay-appearance` (title "Appearance" under the "Overlay" category) with `snapping-overlay-cat` / `snapping-window-cat` parents (see settingscontroller_pageregistration.cpp) |
| **Branch** | `feature/per-mode-window-appearance` (off `v3.1`) |
| **Date** | 2026-05-30 |

Give **Snapping** its own per-window appearance settings (border, corner radius, hide
title bars, accent colour) mirroring what **Autotiling** already has, and untangle the
naming so that the page currently called "Snapping › Appearance" — which actually
configures the **drag-time zone overlay**, not the snapped window — is renamed to **Zones**.

This is the task-level *how* and *order*: every file to create/modify, grounded in the
current code, so nothing is missed during implementation.

---

## 1. Background — how the existing pieces work

### 1.1 Tiling "Appearance" = window decoration (the model to mirror)
- **Config:** `Tiling.Appearance.{Colors,Decorations,Borders}` groups. Keys: `autotileHideTitleBars`,
  `autotileShowBorder`, `autotileBorderWidth` (+Min/Max), `autotileBorderRadius` (+Min/Max),
  `autotileBorderColor`, `autotileInactiveBorderColor`, `autotileUseSystemBorderColors`.
  Chain: `configdefaults.h` (defaults/bounds) → `configkeys.h` (group/key accessors) →
  `isettings.h` (virtuals + NOTIFY) → `settings.{h,cpp}` (Q_PROPERTY/getter/setter via
  `P_STORE_*` macros; `applyAutotileBorderSystemColor()` accent helper) →
  `settingsadaptor.cpp` (`REGISTER_*_SETTING` — D-Bus by-name, how the effect reads them).
- **Settings UI:** `TilingAppearanceController` (thin `PhosphorControl::PageController`,
  CONSTANT bounds only) + `TilingAppearancePage.qml` (Colors / Decorations / Borders cards,
  binding directly to `appSettings.autotile*`). Registered `regPage(m_tilingAppearancePage,
  "tiling-visual-cat", "Appearance", "TilingAppearancePage.qml", …)`.
- **Effect render:** `kwin-effect/.../borders.cpp` draws a native `KWin::OutlinedBorderItem`
  parented under each window's `KWin::WindowItem` (NOT a shader/overlay); title-bar hiding is
  KWin's `setNoBorder(true)` (`autotilehandler/state.cpp::setWindowBorderless`). The draw code
  is **mode-agnostic** — it only asks the gating predicate `shouldShowBorderForWindow(windowId)`.
- **State + gating:** `BorderState` (`libs/phosphor-compositor/.../AutotileState.h`), owned by
  `AutotileHandler` as `m_border`, holds width/radius/colour/show/hide plus per-screen
  `tiledWindowsByScreen` / `borderlessWindowsByScreen` buckets. `shouldShowBorderForWindow`
  checks those buckets. Buckets are populated **only** by autotile flows.
- **Settings → effect:** `daemon_bringup.cpp::loadCachedSettings()` fetches each `autotile*`
  key by name and pushes into `m_autotileHandler`'s `BorderState`, then `updateAllBorders()`.
  Re-run on every daemon `settingsChanged` broadcast (live updates work).

### 1.2 Snapping "Appearance" = zone overlay (mis-titled)
- Every setting on `SnappingAppearancePage.qml` configures the **drop-zone overlay shown
  during a drag**: `useSystemColors`, `highlightColor`/`inactiveColor`/`borderColor`,
  active/inactive **opacity**, zone **border** width/radius, **zone labels** (font + scale).
- **Config:** `Snapping.Appearance.{Colors,Opacity,Border,Labels}`. Consumed by the daemon's
  overlay renderer (`overlayservice/overlay_data.cpp`, `rendering/zoneshaderitem.cpp`) and the
  layer-shell zone QML (`ZoneOverlayContent.qml` etc.) — drag-time visuals, not the post-snap window.

### 1.3 Snapped windows get NO decoration today
- Borders / title-bar hiding are **autotile-exclusive**. `borders.cpp` has zero snap references.
- Autotile and snap track window state in **separate** structures: autotile `BorderState`
  vs snap `SnapState` (`libs/phosphor-snap-engine/.../SnapState.h`, which has no border fields).
- ⇒ Snapped-window appearance is **net-new effect wiring** (Phase 3), not a toggle on existing code.

---

## 2. Naming scheme (settled)

| Surface | UI title | Config group | C++ accessors |
|---|---|---|---|
| Zone overlay (existing) | **Zones** (was "Appearance") | `Snapping.Zones.{Colors,Opacity,Border,Labels}` (migrated from `Snapping.Appearance.*`) | unchanged (`borderWidth()`, `activeOpacity()`, …) — only the *group* accessor renames |
| Snap window appearance (new) | **Window Appearance** | `Snapping.Appearance.{Colors,Decorations,Borders}` (freed by the migration) | `snapping*` (parallel to `autotile*`) |
| Tiling window appearance (existing) | **Window Appearance** (was "Appearance") | `Tiling.Appearance.*` (unchanged) | `autotile*` (unchanged) |

Rationale: moving the zone overlay to `Snapping.Zones.*` makes `Snapping.Appearance.*` mean
*window* appearance, symmetric with `Tiling.Appearance.*`. Snap window settings are
**independent** of tiling's (separate keys, separate values). The Tiling page rename is UI-only.

**No new schema version.** v3.1 is unreleased and already bumped the schema (v3 → v4); we do not
stack a second bump on an unreleased branch. The zone-group rename folds into the **existing
`migrateV3ToV4`** so a released v3 user runs one step (v3 → v4) that applies both the exclusions
fold and the zone rename. Trade-off (accepted): a dev config already stamped `_version: 4` won't
re-run v4, so its customised zone settings reset to defaults once — fine for an unreleased branch
(re-tweak or wipe). `ConfigSchemaVersion` stays `4`.

---

## 3. Phase order

```
1a (extend v3→v4 migration: zone rename)  →  0 (page renames)  →  1b (snap window keys)
   →  2 (snap window page)  →  3 (mode-aware effect borders)
```

Each phase is independently buildable + committable. 1a is first because the config namespace
is the foundation everything else references.

---

## 4. Phase 1a — Extend the v3→v4 migration (zone group rename)

Move the zone-overlay sub-groups `Snapping.Appearance.*` → `Snapping.Zones.*` inside the
**existing** `migrateV3ToV4`. No version bump.

### Tasks (ordered)
1. `src/config/configkeys.h`:
   - Repoint the live zone group accessors to the new paths: `snappingAppearanceColorsGroup`
     → `Snapping.Zones.Colors` (rename to `snappingZonesColorsGroup`), and likewise
     `…OpacityGroup`, `…BorderGroup`, `…LabelsGroup` → `Snapping.Zones.{Opacity,Border,Labels}`.
   - Add `v3*`-prefixed **legacy** group accessors returning the OLD `Snapping.Appearance.*`
     paths (the established `v1*`/`v3*` pattern — used only by the migration for readability).
2. `src/config/settings.cpp` / `settings.h`: update every zone getter/setter `P_STORE_*` macro
   call to use the renamed `snappingZones*Group` accessors. (Getter/setter C++ names unchanged.)
3. `src/config/configmigration.cpp`: inside `migrateV3ToV4`, after the existing exclusions/
   assignments transform, move each `Snapping.Appearance.<sub>` object to
   `Snapping.Zones.<sub>` in the JSON root (preserve keys verbatim). Idempotent — no-op when the
   source group is absent (e.g. a config that never customised zones, or one re-running the step).
   `ConfigSchemaVersion` stays `4`; no new `MigrationStep`.
4. Grep the WHOLE repo for any direct string reference to `Snapping.Appearance.` (there should be
   none outside configkeys + migration; consumers call the Settings getters).

### Files
- `src/config/configmigration.cpp` (extend `migrateV3ToV4`), `src/config/configkeys.h` (group
  rename + v3 legacy accessors), `src/config/settings.cpp`/`.h` (zone getters repointed).
  **No** change to `configmigration.h` (version stays 4).

### Tests
- **Extend** `tests/unit/config/test_migration_v3_to_v4.cpp` (do NOT add a new file): a populated
  `Snapping.Appearance.*` zone group is moved key-for-key to `Snapping.Zones.*`; absent group is a
  no-op; idempotent re-run; coexists with the existing exclusions-fold assertions.
- `StubSettings.h` unaffected (no ISettings surface change in 1a).

### Top gotchas
- Zone defaults delegate to `PhosphorZones::ZoneDefaults::*` — unchanged; only the storage path moves.
- Keep getter/setter C++ names (`borderWidth`, `activeOpacity`) so consumers don't churn.
- Folding into v4 means a config already at v4 skips it (accepted — see §2).

---

## 5. Phase 0 — Recategorize existing pages (UI-only)

### Tasks (ordered)
1. Rename `src/settings/qml/SnappingAppearancePage.qml` → `SnappingZonesPage.qml`; title → "Zones".
   Keep the colour-import action surface.
2. Rename controller `SnappingAppearanceController` → `SnappingZonesController`
   (`src/settings/snappingappearancecontroller.{h,cpp}` → `snappingzonescontroller.{h,cpp}`),
   page id `snapping-appearance` → `snapping-zones`.
3. Rename Tiling page title → "Window Appearance" (id can stay `tiling-appearance`; title only).
4. Update sync surfaces in `settingscontroller_pageregistration.cpp`: `regPage` line, the
   `kSnappingVisualChildren` set, `parentPageRedirects` (`snapping-visual-cat` → `snapping-zones`),
   `validPageNames`.
5. `src/settings/CMakeLists.txt`: rename in sources block + `qt6_add_qml_module` QML_FILES.
6. `src/settings/settingscontroller.{h,cpp}`: rename `m_snappingAppearancePage` member, getter,
   `Q_PROPERTY`, and the ctor construction line.

### Files
- `src/settings/qml/SnappingZonesPage.qml` (renamed), `snappingzonescontroller.{h,cpp}` (renamed),
  `settingscontroller.{h,cpp}`, `settingscontroller_pageregistration.cpp`, `CMakeLists.txt`.

### Tests / verify
- Build `plasmazones-settings`; run: "Zones" page shows identical content; Tiling page titled
  "Window Appearance"; `--page=snapping-zones` resolves; no QML "not a type" error.

### Top gotchas
- Page ids are app-local/transient (not persisted to user config), so the id rename is low-risk.
- All QML files MUST stay listed in `qt6_add_qml_module` (missing = runtime "not a type").

---

## 6. Phase 1b — Snap window-appearance config keys

Add 7 `snapping*` keys under the freed `Snapping.Appearance.{Colors,Decorations,Borders}`,
full add-a-setting chain mirroring `autotile*`.

### Keys
`snappingHideTitleBars` (bool), `snappingShowBorder` (bool), `snappingBorderWidth`
(+Min/Max), `snappingBorderRadius` (+Min/Max), `snappingBorderColor`,
`snappingInactiveBorderColor`, `snappingUseSystemBorderColors`.

### Tasks (ordered)
1. `configkeys.h`: new `Snapping.Appearance.{Colors,Decorations,Borders}` groups; reuse generic
   key accessors (`widthKey`/`radiusKey`/`activeKey`/`inactiveKey`/`useSystemKey`/
   `hideTitleBarsKey`/`showBorderKey`).
2. `configdefaults.h`: default + bounds accessors (mirror `autotile*` defaults).
3. `isettings.h`: 7 virtuals + NOTIFY signals.
4. `settings.{h,cpp}`: Q_PROPERTY/getter/setter/member via `P_STORE_*`;
   `applySnappingBorderSystemColor()` accent helper; load/save/reset.
5. `settingsadaptor.cpp`: `REGISTER_*_SETTING` entries (D-Bus by-name).
6. `tests/unit/helpers/StubSettings.h`: add the 7 overrides.

### Tests
- Migration tests unaffected (new keys, no migration). Build + `ctest` (StubSettings must compile
  against the new ISettings virtuals).

### Top gotchas
- Config KEY strings are generic (group disambiguates); only the **C++ accessor names** are
  `snapping*`-prefixed to stay distinct from zone (`borderWidth`) and autotile (`autotileBorderWidth`).

---

## 7. Phase 2 — Snap window-appearance page

### Tasks (ordered)
1. New `src/settings/qml/SnappingWindowAppearancePage.qml` — Colors / Decorations / Borders cards
   binding `appSettings.snapping*`, mirroring `TilingAppearancePage.qml`.
2. New `SnappingWindowAppearanceController` (bounds facade like `TilingAppearanceController`).
3. `settingscontroller.{h,cpp}`: member + getter + `Q_PROPERTY CONSTANT` + ctor construction.
4. Register `snapping-window-appearance` under `snapping-visual-cat` (regPage); update
   `kSnappingVisualChildren`, `validPageNames`, CMakeLists (sources + QML_FILES).

### Files
- `SnappingWindowAppearancePage.qml`, `snappingwindowappearancecontroller.{h,cpp}`,
  `settingscontroller.{h,cpp}`, `settingscontroller_pageregistration.cpp`, `CMakeLists.txt`.

### Tests / verify
- Build + run: new page edits persist to the `snapping*` keys (effect consumption lands in Phase 3).

---

## 8. Phase 3 — Mode-aware window borders + title-bar hiding (effect)

The heavy phase. Generalise the border machinery so a window's border/title-bar uses the
settings of the **mode that manages it** (snap vs autotile). Reuse `OutlinedBorderItem` draw +
`setNoBorder` hide — change only the gating + settings source.

### Tasks (ordered)
1. Decide where the effect learns "window X is snap-committed" (no decoration tracking exists
   today). Candidates: (a) the daemon's snap-commit/float-release path pushes the snapped-window
   set to the effect; (b) the effect derives it from existing snap state it already tracks for
   geometry. **Resolve this first** — it dictates the rest of the phase.
2. Add a snap-side border state (second `BorderState`, or make the state mode-keyed), fed by the
   commit/release path.
3. Make `shouldShowBorderForWindow` / `updateWindowBorder` (`borders.cpp`) mode-parameterised:
   resolve the window's managing mode, then read that mode's width/radius/colour/show/hide.
4. Extend `daemon_bringup.cpp::loadCachedSettings()` to fetch the `snapping*` keys into the
   snap border state (same shape as the `autotile*` block; re-run on `settingsChanged`).
5. Title-bar hiding for snapped windows via `setNoBorder`, mirroring `setWindowBorderless`.

### Files
- `kwin-effect/.../borders.cpp` (mode-aware gating + draw), `libs/phosphor-compositor/.../AutotileState.h`
  (or a new snap border state), `kwin-effect/.../daemon_bringup.cpp` (settings load), plus the
  snap-commit tracking wiring identified in task 1 (likely `kwin-effect/.../drag_snap.cpp` +
  the snap commit path).

### Tests / verify
- Build effect; run: a window snapped into a zone gets a border / hidden title bar per the new
  page, independently of tiling; floating it removes the decoration; live setting updates apply.

### Top gotchas
- A snapped window stays user-draggable; hiding its title bar removes the drag handle until
  re-shown (accepted per the "full mirror" decision).
- Don't duplicate `OutlinedBorderItem` logic — extend the gating/settings, keep one draw path.
- Active/inactive colour is a runtime check against `effects->activeWindow()`, not stored state.
