<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Surface taxonomy refactor

**Status:** all phases shipped on PR #434 (branch `refactor/surface-taxonomy`).
This document was written as the up-front plan and is preserved as
record of the design decisions. Cross-references below to "phase 0",
"phase 1", etc. refer to commits on that branch.

## Why

The current surface naming flattens three independent concerns into one
`Role`/`Roles` vocabulary and borrows from two different lexicons
(Plasma-shell-flavor terms + generic WM terms + PZ-internal terms).
That works while PZ is a Plasma add-on. It will not survive the
plugin-based-compositor direction (Phosphor as a standalone shell): a
third-party tool that links `phosphor-layer` should not have to learn
"notification" means "PZ OSD" or that `CornerToast` is a UI pattern
masquerading as a layer-shell primitive.

## Three axes the taxonomy needs to keep separate

| Axis | Concern | Vocabulary today | Lives in |
|------|---------|------------------|----------|
| 1. **Compositor primitive** | wlr-layer-shell config: `layer`, `anchors`, `exclusiveZone`, `keyboardInteractivity` | `Roles::Background`, parts of every `Roles::*` | `phosphor-layer` |
| 2. **UI pattern** | What kind of user-facing surface this is: panel, toast, modal, HUD, snap-preview, wallpaper | `Roles::TopPanel`, `Roles::CornerToast`, `Roles::CenteredModal`, `Roles::FullscreenOverlay`, `Roles::FloatingOverlay` | `phosphor-layer` (wrong home) |
| 3. **Application role** | What PZ uses this surface for | `PzRoles::SnapAssist`, `PzRoles::LayoutPicker`, `PzRoles::ZoneSelector`, `PzRoles::Notification`, `PzRoles::Overlay`, `PzRoles::PassiveShell`, `PzRoles::ShaderPreview`, `PzRoles::LayoutOsd` | `src/daemon/overlayservice/pz_roles.h` |

Today axis 1 + axis 2 are fused inside `phosphor-layer` and axis 3
re-references the fused result. The smell is loudest at:

- `Roles::CornerToast`: a UI pattern in a primitives lib.
- `PzRoles::Notification`: an axis-3 role named after an axis-2 pattern
  borrowed from Plasma's lexicon. It's actually an HUD/OSD, not a Plasma
  notification.
- `PzRoles::Overlay`: too generic to convey what it is.
- `PzRoles::PassiveShell`: names an implementation strategy (one shared
  surface, many slots), not a role.

## Target shape

```
phosphor-layer        (axis 1 only)
├── Anchor, Layer, KeyboardInteractivity, ExclusiveZone
├── struct SurfaceProfile { layer, anchors, exclusiveZone, keyboard,
│                           margins }
└── (no named roles here)

phosphor-shell-patterns   (axis 2, new lib OR sub-namespace)
├── Patterns::Wallpaper     ≈ today's Background
├── Patterns::Panel(edge)   ≈ today's TopPanel/BottomPanel/Left|RightDock
├── Patterns::Toast(corner) ≈ today's CornerToast
├── Patterns::Modal         ≈ today's CenteredModal
├── Patterns::Hud           ≈ today's FullscreenOverlay (kbd-None case)
├── Patterns::Floating      ≈ today's FloatingOverlay
└── each Pattern is a SurfaceProfile recipe + default scope hint

PzRoles                  (axis 3, PZ-only)
├── LayoutOsd       → Hud + LayoutOsd animator + LayoutOsd.qml
├── NavigationOsd   → Hud
├── SnapAssistView  → Modal (or Hud if kbd-None) + per-screen anchor
├── LayoutPicker    → Modal
├── ZoneSelector    → Hud + per-VS anchors
├── ZoneOverlay     → Hud (full-screen click-through)
├── ShaderPreview   → Floating
├── PassiveShell    → STAYS but moves to an implementation detail of
│                     OverlayService, not a Role consumers see
```

## Renaming map (Plasma-flavor → neutral)

| Today (axis 2 in phosphor-layer) | Proposed |
|----|----|
| `Roles::Background` | `Patterns::Wallpaper` |
| `Roles::TopPanel` | `Patterns::Panel(Edge::Top)` |
| `Roles::BottomPanel` | `Patterns::Panel(Edge::Bottom)` |
| `Roles::LeftDock` | `Patterns::Panel(Edge::Left)` |
| `Roles::RightDock` | `Patterns::Panel(Edge::Right)` |
| `Roles::CenteredModal` | `Patterns::Modal` |
| `Roles::CornerToast` | `Patterns::Toast(Corner::TopRight)` |
| `Roles::FullscreenOverlay` | `Patterns::Hud` |
| `Roles::FloatingOverlay` | `Patterns::Floating` |

| Today (axis 3 in PzRoles) | Proposed |
|----|----|
| `Overlay` | `ZoneOverlay` |
| `Notification` | `LayoutOsd` for the layout OSD config consumer, `NavigationOsd` for the nav OSD consumer. Drop the shared "Notification" handle, since it isn't one. |
| `LayoutOsd` (already exists?) | merge with above |
| `PassiveShell` | stays in code but stops being a public Role; lives as `OverlayService::PassiveShellHost` |
| Others (`SnapAssist`, `LayoutPicker`, `ZoneSelector`, `ShaderPreview`) | keep names, restate as `Pattern + animator config + content` triples |

The "widgets" word the user mentioned belongs to axis 4 (QML content
hosted by an axis-3 role). Not part of the role taxonomy at all,
already correctly scoped inside QML modules. No change needed for
widgets beyond not letting the word leak into role names.

## Migration phases (each is a separate PR)

### Phase 0: non-breaking foundation

- Land this doc. Get sign-off on the axis split.
- Add a `Patterns` namespace inside `phosphor-layer` (no new lib yet,
  same compile target). Define `Patterns::Wallpaper`, `Patterns::Panel`,
  etc. as alias `Role` values pointing at the same protocol config as
  the existing `Roles::*`.
- Mark the old `Roles::*` names as `[[deprecated]]` with a one-line
  `// use Patterns::X` note. No call-site changes yet.

### Phase 1: call-site migration (mechanical)

- Find/replace `PhosphorLayer::Roles::Background` →
  `PhosphorLayer::Patterns::Wallpaper`, and one rename per existing
  preset.
- Update `PzRoles` to compose `Patterns::*` (axis 2) instead of
  `Roles::*` (axis 1+2 fused). Same runtime behaviour.
- Delete the deprecated `Roles::*` aliases.

### Phase 2: PzRoles cleanup

- Rename `PzRoles::Notification` → split into `LayoutOsd` /
  `NavigationOsd`. Update `registerConfigForRole` keys.
- Rename `PzRoles::Overlay` → `PzRoles::ZoneOverlay`.
- Move `PzRoles::PassiveShell` out of the public role list; expose it as
  an internal type of `OverlayService` (clients of `PzRoles` don't need
  to name it).
- All `PzRoles::*` becomes a thin struct `{ Pattern, animatorKey,
  scopePrefix }` rather than a customised `PhosphorLayer::Role`. This is
  the "role composes a pattern" step.

### Phase 3: shell-patterns extraction

- Once `Patterns::*` is stable and `PzRoles` is the only consumer that
  knows about PZ-specific roles, lift `Patterns` out of `phosphor-layer`
  into a sibling lib `phosphor-shell-patterns`. The split is then:
  - `phosphor-layer`: protocol primitives + `SurfaceProfile` struct.
  - `phosphor-shell-patterns`: UI-pattern recipes.
  - `phosphor-*` (rest of the kit): anyone who wants to build a shell.
  - `PzRoles` (in the PZ daemon): PZ-specific composition.
- Phosphor-as-standalone-WM (per `MEMORY.md project_phosphor_rebrand`)
  links `phosphor-shell-patterns` directly and skips `PzRoles`.

## Settings UI organization (the same smell, surfaced differently)

The Animations section in the settings app mirrors the same axis muddle.
Current `src/settings/qml/Animations*Page.qml`:

| Page | What it actually configures | Vocabulary axis |
|------|-----------------------------|-----------------|
| `AnimationsPopupsPage` | `popup.*` resolver subtree: zone selector, layout picker, snap-assist (all PZ transient overlays) | Borrowed Plasma word for PZ-internal category |
| `AnimationsNotificationsPage` | `osd.*` resolver subtree: LayoutOsd + NavigationOsd (PZ OSDs, not notifications) | Borrowed Plasma word, even more misleading: these aren't notifications |
| `AnimationsPanelsPage` | `panel.*` resolver subtree: settings nav rail, editor property panel (in-app slide-in surfaces) | Borrowed Plasma word, collides with system-panel concept |
| `AnimationsWidgetsPage` | Zone widgets (PZ-specific QML) | Generic-ish but means PZ widgets |
| `AnimationsWindowsPage` | Window move/resize animations (real WM windows) | Generic WM, correct |
| `AnimationsWorkspacesPage` | Virtual desktop transitions | Generic WM, correct |
| `AnimationsZonesPage` | Snap-to-zone motion | PZ-specific, correct |
| `AnimationsMotionSetsPage`, `…ShadersPage`, `…PresetsPage`, `…AppRulesPage`, `…GeneralPage` | Cross-cutting (configuration of the system itself, not a surface type) | Meta, fine |

The three offenders: **Popups, Notifications, Panels**: share three
properties:

1. They borrow Plasma-shell words for purely PZ-internal subtrees.
2. The subtrees they represent (`popup.*`, `osd.*`, `panel.*`) are
   defined by `ShaderProfileTree::resolve`'s walk-up cascade, which is an
   *implementation detail* leaking into the UI labels.
3. To a new user, "Popups" vs "Notifications" vs "Panels" is just three
   words for "things that show up briefly": not three categories with
   obvious meaning.

### Target settings layout

Group Animations sub-pages by **what the user perceives**, not by the
internal resolver path. Two groups, plus the cross-cutting meta pages:

**Surface motion (what slides/fades on screen):**
- `Overlays`: formerly Popups + Notifications. PZ overlays (snap-assist,
  layout picker, zone selector, layout OSD, navigation OSD). Inside this
  page, sub-cards for each surface family.
- `Side panels`: formerly Panels. In-app slide-in surfaces (settings
  nav rail, editor property panel). Renamed to disambiguate from
  system-panel concept.
- `Window motion`: formerly Windows. Real-WM window animations.
- `Workspace transitions`: formerly Workspaces. VD switching.
- `Zone snapping`: formerly Zones + Widgets. Snap motion + zone widget
  decoration. Merging Widgets here because zone-widget animations only
  ever play in a snap context.

**Configuration (cross-cutting):**
- `Motion sets`, `Shaders`, `Presets`, `App rules`, `General`: unchanged.

The resolver subtrees (`popup.*`, `osd.*`, `panel.*`) stay as-is in the
profile tree code. The UI labels stop leaking them. A small lookup table
in `AnimationsPageController` maps display-name → subtree path.

### Snapping vs Tiling pages: sanity check, no change needed

`SnappingBehaviorPage` / `TilingAlgorithmPage` / etc. are correctly
organised: top-level by PZ mode (Snapping vs Tiling), second level by
concern (Behavior, Appearance, Ordering…). Both are fully PZ-internal
words. Leave alone.

### Migration step for settings

This goes in **Phase 2** alongside the `PzRoles::Notification` rename:

- Rename `AnimationsPopupsPage.qml` → `AnimationsOverlaysPage.qml`.
- Rename `AnimationsNotificationsPage.qml` → merge into Overlays page as
  a sub-card, or `AnimationsOsdsPage.qml` if we keep them separate.
- Rename `AnimationsPanelsPage.qml` → `AnimationsSidePanelsPage.qml`.
- Merge `AnimationsWidgetsPage.qml` into `AnimationsZonesPage.qml` (one
  combined "Zone snapping & widgets" page).
- Update the navigation sidebar in `Main.qml` to use the new labels.
- Update i18n strings: each rename is one `i18n("Popups")` → `i18n("Overlays")`.
- Resolver-tree code (`ShaderProfileTree::resolve`) stays unchanged.
  the rename is purely UI/label.

The diff is mostly file renames + nav-sidebar label edits + i18n
updates. Maybe 100-200 lines. Translators need a heads-up.

## Out of scope for this branch

- The `SurfaceConfig::anchorsOverride` escape hatch that `ZoneSelector`
  uses today (the preset ships `AnchorNone` and `selector.cpp` always
  overrides). Once patterns are first-class, `Patterns::Hud` with a
  per-VS anchor parameter can replace the override; that's a follow-up.
- The "VS-aware anchoring" math currently sprinkled across overlay
  setup. Belongs to a separate VS-surfaces refactor.
- Audio/wallpaper roles for the eventual `phosphor-wm`: that's a
  Phosphor concern, not PZ.

## Open questions

1. **`Patterns` in `phosphor-layer` vs a new lib in phase 0?** Splitting
   the lib later costs one extra PR; not splitting at all costs a lib
   that mixes protocol primitives with UI patterns. The doc proposes
   "stay in-lib for phase 0-2, split in phase 3" to keep diffs small.
2. **Drop `Plasma` flavor in the daemon namespace too?** The C++
   namespace `PlasmaZones::PzRoles` still says "Plasma". When Phosphor
   stands alone, that namespace becomes a misnomer. Rebrand is tracked
   in `project_phosphor_rebrand`; this refactor should not pre-empt it.
3. **What about the `CornerToast` `Top|Right` baked anchor?** The
   replacement `Patterns::Toast(Corner)` takes a corner parameter so
   `Toast(TopRight)`, `Toast(BottomLeft)`, etc. all share the same
   recipe. One value-typed parameter, three explicit lines saved.
4. **Per-instance scope prefixes** (`makePerInstanceRole` in
   `pz_roles.h:123`): keep as-is, they are an axis-3 concern (animator
   config lookup key per (role, screen, generation)). The refactor
   preserves the longest-prefix lookup contract documented there.

## Concrete first commit on this branch

The smallest commit that makes the axis split visible without changing
runtime behaviour:

- Add `libs/phosphor-layer/include/PhosphorLayer/Patterns.h` declaring
  `Patterns::Wallpaper`, `Patterns::Panel(Edge)`, `Patterns::Toast(Corner)`,
  `Patterns::Modal`, `Patterns::Hud`, `Patterns::Floating`: each
  returning a `Role` (or `SurfaceProfile`) identical to the corresponding
  current `Roles::*` value.
- Mark `Roles::*` `[[deprecated("use PhosphorLayer::Patterns::*")]]`.
- Update `pz_roles.h` to compose `Patterns::*`.

That's phase 0. No semantic change, zero call-site churn outside
`pz_roles.h`, full test pass, ready to land.
