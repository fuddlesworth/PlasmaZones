# Color-Usage Inventory — Phosphor Libraries

Scope: `libs/phosphor-control`, `libs/phosphor-shell*` (shell, shell-widgets, shell-osd,
shell-notifications, shell-patterns), `libs/phosphor-theme`, `libs/phosphor-popout`.
`examples/` and `tests/` excluded. Read-only audit; no code modified.

Verdicts: **OK** (correct token use), **HACK** (fabricated color that should be a theme
token), **MISUSE** (semantically wrong token), **BOUNDARY** (Kirigami and Phosphor tokens
mixed in one component).

> **ERRATUM (read before using the tables below).**
> These tables are a PRE-REMEDIATION snapshot. Line numbers and every
> "current state" expression describe the tree BEFORE the fixes on the
> `fix/theme-color-pipeline` branch landed, so they no longer match the
> code. In addition, any Replacement cell that prescribes
> `Kirigami.Theme.separatorColor` is WRONG. That property does not exist
> in Kirigami and evaluates to `undefined` at runtime. The correct
> replacement is
> `Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
> Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)` (see the
> ruleset in the parent map, `../kirigami-color-map.md`). This banner
> supersedes the affected table cells and they have deliberately not
> been rewritten one by one.
>
> Status update: the §2 phosphor-control HACK findings and the §4 HACK
> totals were RESOLVED on this branch. `ThemeHelpers.js` is deleted and
> its call sites (SidebarRow, SidebarBackButton, UnsavedChangesFooter)
> now use the real scheme roles.

---

## 1. Where Phosphor.Theme gets its tokens from

`Phosphor.Theme` (`libs/phosphor-theme/qml/Phosphor/Theme/Theme.qml`) is a QML singleton
whose every named accessor (`Theme.primary`, `Theme.on_surface`, …) indexes the
`PaletteStore.palette` QVariantMap (a `Q_PROPERTY` with `NOTIFY paletteChanged`, so all
bindings retint live). The token *sources*, in order:

1. **Hardcoded bootstrap** — `src/defaultpalette.cpp::defaultDarkPalette()` seeds the
   store at construction with a hand-authored dark palette (Material 3 + ANSI-16 status +
   4 brand-gradient stops, hex literals like `#3B82F6`). This is what renders if a host
   does nothing.
2. **Palette JSON file** — a host may call `PaletteStore.loadFromFile(path)`
   (`src/palettestore.cpp:364`). The file (wrapped `{"tokens":{...}}` or flat) is parsed,
   merged over the current palette, and **hot-reloaded live** via a
   `QFileSystemWatcher` on both the file and its parent directory with an 80 ms debounce
   (`palettestore.cpp:55-146`, `733`). Atomic-rename writers (matugen, vim, QSaveFile)
   are handled through the directory watch.
3. **MatugenRunner** — `src/matugenrunner.cpp` spawns the external `matugen` binary on a
   wallpaper path (`matugen image <wp> --json hex`), normalises matugen's key names to
   the canonical snake_case tokens, and emits `paletteReady(map)`; a host wires that to
   `PaletteStore.applyTokens()`.

**It does NOT derive from Kirigami.Theme, KColorScheme, KConfig, or `kdeglobals` in any
way** — there is not a single KDE-colour reference anywhere in `libs/phosphor-theme/`
(sources, headers, QML, CMakeLists). Missing tokens fall back to a loud `#ff00ff`
sentinel (`Theme.qml:44`) rather than any system colour.

### Does Phosphor.Theme track the KDE color scheme live?

**No.** It never reads the KDE scheme, so a Plasma color-scheme change (including one
matugen applies through `kdeglobals`) is invisible to it. It follows matugen updates
**only** through its own channel: if the host points `PaletteStore.loadFromFile()` at a
matugen-emitted Phosphor palette JSON, the file watcher makes every subsequent matugen
run retint the shell live; alternatively the host can run `MatugenRunner` itself. As of
this audit **no non-example code in the repo wires either path** (grep for
`PaletteStore` / `loadFromFile` / `paletteReady` outside `libs/phosphor-theme` and
examples finds nothing), so Phosphor.Theme currently sits on the hardcoded default dark
palette at runtime.

### Library split (why there are no BOUNDARY findings)

The two token worlds never meet inside one component:

- `phosphor-control` (the settings-app chrome) is **pure Kirigami** — it imports
  `org.kde.kirigami` only and never `Phosphor.Theme`.
- `phosphor-shell-widgets` / `-osd` / `-notifications` / `-popout` are **pure
  Phosphor.Theme** — they never import Kirigami.
- `phosphor-shell` and `phosphor-shell-patterns` contain no color usage at all
  (windowing/C++ plumbing; `PerScreen.qml` has no colors).

---

## 2. Per-file findings

### libs/phosphor-theme/qml/Phosphor/Theme/Theme.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 44 | `readonly property color missingTokenColor: "#ff00ff"` | Missing-token debug sentinel | OK | None — deliberate, documented, test-visible sentinel. |
| 46–94 | `_t("background")` … `_t("brand_stop_3")` | Token accessors over `PaletteStore.palette` | OK | None — this is the token source. |
| 61 | `palette["surface_tint"] !== undefined ? palette["surface_tint"] : primary` | M3 surface-tint with primary fallback | OK | None — matches the M3 default and stays binding-tracked. |

### libs/phosphor-theme/qml/Phosphor/Theme/StateLayer.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 32 | `Qt.rgba(color.r, color.g, color.b, disabled_content)` | Central M3 disabled-content tint helper | OK | None — this is the sanctioned, single-place alpha application the widget set routes through. |
| 35 | `Qt.rgba(color.r, color.g, color.b, disabled_container)` | Central M3 disabled-container tint helper | OK | None — same rationale. |

### libs/phosphor-theme/src/defaultpalette.cpp

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 27–76 | `QColor(QLatin1String("#050916"))` etc. (30 hex literals) | Bootstrap default dark palette | OK | None — this file IS the hardcoded token source, by design ("must compile without runtime dependencies"). |

### libs/phosphor-control/qml/ThemeHelpers.js

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 18 | `Qt.rgba(baseColor.r, baseColor.g, baseColor.b, alpha)` (`withAlpha`) | Shared surface-fabrication helper | HACK | The helper centralises the `Qt.rgba(textColor, α)` fabrication idiom rather than eliminating it. Retarget the call sites to real scheme roles (below); what remains can use `Kirigami.ColorUtils` if a genuine alpha need survives. |
| 26–29 | `ACTIVE_TINT_ALPHA = 0.18` / `HOVER_TINT_ALPHA = 0.06` / `SUBTLE_BACKGROUND_ALPHA = 0.15` / `SUBTLE_OUTLINE_ALPHA = 0.2` | Alpha presets feeding the fabrications | HACK | Dissolve together with the call sites; the KColorScheme roles carry these distinctions natively. |

### libs/phosphor-control/qml/SidebarRow.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 127 | `Qt.rgba(0, 0, 0, 0)` | Divider row: no background | OK | Cosmetic: `"transparent"` reads clearer (the SidebarBackButton comment explains why the literal was avoided — Behavior gating already handles that). |
| 130 | `ThemeHelpers.activeTint(Kirigami.Theme.highlightColor)` | Active-row selection background (highlightColor @ 0.18 α) | HACK | Fabricated selection surface. Use `Kirigami.Theme.colorSet: Kirigami.Theme.Selection` + `backgroundColor` on the delegate background (with matching `Selection` text colors on content), or at minimum `Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.highlightColor, k)` so the result stays opaque and scheme-correct. |
| 133 | `ThemeHelpers.hoverTint(Kirigami.Theme.textColor)` | Hover background (textColor @ 0.06 α) | HACK | Fabricated hover surface from a text role. Use `Kirigami.Theme.hoverColor` (DecorationHover role) as the tint source, or `alternateBackgroundColor`; KColorScheme provides a real hover decoration color. |
| 135 | `Qt.rgba(0, 0, 0, 0)` | Resting row: no background | OK | Same cosmetic note as line 127. |

### libs/phosphor-control/qml/SidebarBackButton.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 68 | `backButton.hovered ? ThemeHelpers.hoverTint(Kirigami.Theme.textColor) : Qt.rgba(0, 0, 0, 0)` | Hover background on the drill-out header | HACK | Same as SidebarRow:133 — `Kirigami.Theme.hoverColor`-based tint (keep visual parity with SidebarRow, both files cross-reference each other). |

### libs/phosphor-control/qml/UnsavedChangesFooter.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 45–46 | `Kirigami.Theme.colorSet: Kirigami.Theme.Window` + `inherit: false` | Pin the footer chrome to the Window color set | OK | None — see §3 below; consistent with its mount point. |
| 69 | `dirty ? Kirigami.Theme.highlightColor : ThemeHelpers.withAlpha(Kirigami.Theme.textColor, SUBTLE_BACKGROUND_ALPHA)` | 1-px persistent accent/divider line | HACK | Dirty branch (highlightColor as an attention accent) is acceptable. The resting branch fabricates a separator from `textColor` @ 0.15 α — use the theme's separator idiom (`Kirigami.Separator`, or `Kirigami.ColorUtils.linearInterpolation(backgroundColor, textColor, 0.15)` for an opaque line). Note: the comment at lines 104–106 claims the persistent line uses 0.4 alpha; it actually uses 0.15 (doc rot). |
| 100 | `ThemeHelpers.activeTint(Kirigami.Theme.neutralTextColor)` | Unsaved-changes bar surface (warning text-color @ 0.18 α fill) | HACK | Fabricated status surface from a text role — the scheme already has the exact role: `Kirigami.Theme.neutralBackgroundColor`. Use it directly (opaque, scheme-authored, correct contrast with `neutralTextColor` content). |
| 111 | `ThemeHelpers.withAlpha(Kirigami.Theme.neutralTextColor, 0.4)` | Top accent line of the bar | HACK | Derive from the same replacement surface: `Qt.darker/Qt.lighter` on `neutralBackgroundColor`, or keep `neutralTextColor` but via `Kirigami.ColorUtils.linearInterpolation` with the bar background so the line is opaque. |
| 144 | `color: Kirigami.Theme.neutralTextColor` (icon) | Warning-status icon tint on a status message | OK | None — genuine status semantics (unsaved work = caution), not decoration. Pairs correctly with a `neutralBackgroundColor` surface. |
| 150 | `color: Kirigami.Theme.neutralTextColor` (label) | Warning-status label on a status message | OK | None — same rationale. |

### libs/phosphor-control — other QML (Sidebar.qml, Breadcrumbs.qml, PageHost.qml, SettingsAppWindow.qml, AboutPageShell.qml, DiscardChangesDialog.qml, SidebarRow content, LoaderHelpers.js)

No color expressions at all — these rely entirely on default Kirigami/QQC2 styling and
express emphasis through `opacity` (0.3–0.8) on text/icons, which is fine. **Clean.**

### libs/phosphor-shell-widgets/qml/Phosphor/Widgets/PhosphorButton.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 70 | `_hasContainer ? StateLayer.disabledContainer(Theme.on_surface) : "transparent"` | Disabled container fill | OK | None — M3 disabled pattern via the central StateLayer helper. |
| 73 / 75 / 77 | `Theme.primary` / `Theme.primary_container` / `"transparent"` | Variant container fills | OK | None. |
| 85 | `StateLayer.disabledContent(Theme.on_surface)` | Disabled content color | OK | None. |
| 88–92 | `Theme.on_primary` / `Theme.on_primary_container` / `Theme.primary` | Variant content colors | OK | None — correct on-role pairing. |
| 105 | `root.enabled ? Theme.outline : StateLayer.disabledContainer(Theme.on_surface)` | Outlined-variant border | OK | None. |

### libs/phosphor-shell-widgets/qml/Phosphor/Widgets/PhosphorCard.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 57 | `Qt.tint(Theme.surface_container, Qt.rgba(Theme.surface_tint.r, .g, .b, a))` | M3 elevation surface-tint overlay | OK | None — this is the M3-specified tint composite; the alpha comes from the `Tokens.elevation_*` design tokens and the color from the `surface_tint` token, both binding-tracked. Only improvement: route the per-channel decomposition through a `StateLayer`-style helper (e.g. `Theme.withAlpha()`) so `Qt.rgba(c.r,c.g,c.b,a)` has a single QML home. |

### libs/phosphor-shell-widgets/qml/Phosphor/Widgets/ElevationShadow.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 52 | `shadowColor: Qt.rgba(0, 0, 0, 1)` | Drop-shadow ink | OK | None — shadows are intentionally opaque black with strength via the tokenised `shadowOpacity`; not a theme surface. |

### libs/phosphor-shell-widgets/qml/Phosphor/Widgets/PhosphorPill.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 62 | `StateLayer.disabledContainer(Theme.on_surface)` | Disabled chip fill | OK | None. |
| 63 | `selected ? Theme.secondary_container : "transparent"` | Selected chip fill | OK | None — M3 filter-chip pattern. (Minor: selected content uses `on_surface` at line 69 where M3 uses `on_secondary_container`; visually equivalent in most palettes but the paired on-role would be stricter.) |
| 68–69 | `StateLayer.disabledContent(...)` / `Theme.on_surface` / `Theme.on_surface_variant` | Chip content colors | OK | See note above. |
| 79 | `border.color: Theme.outline_variant` | Resting chip outline | OK | None. |

### libs/phosphor-shell-widgets/qml/Phosphor/Widgets/PhosphorRipple.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 38 | `property color rippleColor: Theme.on_surface` | State-layer/ripple foreground default | OK | None. |
| 72 / 110 | `color: root.rippleColor` + `opacity: StateLayer.pressed/hover/focus` | Hover/press/focus state layer | OK | None — textbook M3 state layer with tokenised opacities. |

### libs/phosphor-shell-widgets/qml/Phosphor/Widgets/PhosphorSlider.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 115 | `StateLayer.disabledContent(Theme.on_surface)` | Shared disabled tint | OK | None. |
| 124 | `enabled ? Theme.surface_variant : StateLayer.disabledContainer(Theme.on_surface)` | Inactive track | OK | None. |
| 134 | `enabled ? Theme.primary : _disabledTint` | Active track | OK | None. |
| 144–145 | `color: Theme.primary` + `opacity: activeFocus ? StateLayer.focus : 0` | Keyboard-focus halo | OK | None — focus indicator built from accent + focus-opacity token. |
| 164 | `enabled ? Theme.primary : _disabledTint` | Handle | OK | None. |

### libs/phosphor-shell-widgets/qml/Phosphor/Widgets/PhosphorTextField.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 51 | `color: "transparent"` | Field container (outlined style) | OK | None. |
| 53 | `!enabled ? StateLayer.disabledContainer(...) : (_focused ? Theme.primary : Theme.outline)` | Outline; focus tint | OK | None — focus indicated by the primary accent per M3 outlined-field spec. |
| 75–77 | `Theme.on_surface` / `Theme.primary` (selection) / `Theme.on_primary` (selected text) | Input text + selection | OK | None. |
| 88 | `enabled ? Theme.on_surface_variant : _disabledTint` | Placeholder | OK | None. |

### libs/phosphor-shell-widgets/qml/Phosphor/Widgets/{ConnectedShape,ConnectedCorner,BarCanvas}.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| ConnectedShape:30–31 | `fillColor: Theme.surface_container`, `strokeColor: "transparent"` | Connected-corner surface painter defaults | OK | None. |
| ConnectedCorner:37 | `fillColor: Theme.surface_container` | Fillet default | OK | None. |
| BarCanvas:41 | `property color color: Theme.surface_container` | Bar surface default | OK | None. |

### libs/phosphor-shell-osd (OSDCard, VolumeOSD, BrightnessOSD, MicOSD, CapsLockOSD, OSDHost)

| File:Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| OSDCard:47 | `Theme.surface_container_high` | Card surface | OK | None. |
| OSDCard:77 | `Theme.on_surface` | Label | OK | None. |
| OSDCard:90 / 96 | `Theme.surface_variant` / `Theme.primary` | Progress track / fill | OK | None. |
| VolumeOSD:35 / 45 | `Theme.on_surface` (glyph, wave) | Speaker glyph | OK | None. |
| VolumeOSD:56 | `osd.value <= 0 ? Theme.error : "transparent"` | Muted cross | OK | Status color communicating a real state (muted), not decoration. |
| MicOSD:25 | `active ? Theme.on_surface_variant : Theme.on_surface` | Glyph dim when muted | OK | None. |
| MicOSD:65 | `active ? Theme.error : "transparent"` | Mute slash | OK | Genuine status semantics. |
| CapsLockOSD:25 | `active ? Theme.primary : Theme.on_surface` | Caps-on accent | OK | None — accent-as-state, not a status color. |
| BrightnessOSD:34 / 52 | `Theme.on_surface` | Sun glyph | OK | None. |
| OSDHost | opacity-only transitions | — | OK | No color usage. |

### libs/phosphor-shell-notifications (Toast, ToastHost)

| File:Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| Toast:58 | `Theme.surface_container_high` | Toast surface | OK | None. |
| Toast:74 | `color: Theme.error` (visible only when `urgency >= 2`) | Critical-urgency accent stripe | OK | Status color bound to actual status (critical notification) — not decoration. |
| Toast:93 | `Theme.surface_variant` | Image placeholder | OK | None. |
| Toast:111 / 121 / 133 | `on_surface_variant` / `on_surface` / `on_surface_variant` | App name / summary / body | OK | None. |
| Toast:160–161 | `Theme.on_surface` @ `StateLayer.hover` | Close-button hover layer | OK | None. |
| Toast:177–178 | `fillColor: "transparent"`, `strokeColor: Theme.on_surface_variant` | Close glyph | OK | None. |
| ToastHost | opacity-only animations | — | OK | No color usage. |

### libs/phosphor-popout/qml/Phosphor/Popout/PopoutHost.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 55 | `property color backdropColor: "transparent"` | Host-configurable modal backdrop, default off | OK | None — the host supplies a scrim token when wanted; documented `.a > 0.01` gate at 243 is threshold logic, not a color fabrication. |

### libs/phosphor-shell, libs/phosphor-shell-patterns

No color usage anywhere (C++ windowing/pattern plumbing; `PerScreen.qml` contains no
color expressions). **Clean.**

---

## 3. UnsavedChangesFooter.qml:45 — `colorSet: Window` consistency

**Consistent — OK.** The footer is mounted directly in the window-chrome
`ColumnLayout` of `Kirigami.ApplicationWindow` (`SettingsAppWindow.qml:553`), as a
sibling of the breadcrumb row and `PageHost`, not inside any page. ApplicationWindow
chrome renders in the Window color set, and no other component in `phosphor-control`
sets a `colorSet` at all (this is the library's only occurrence). The
`inherit: false` pin is defensive and correct: hosted pages inside `PageHost` may set
`View` or other sets, and without the pin a page-level set could bleed into the footer
via inheritance. The only wrinkle is that the colors actually painted on that
Window-set surface are the fabricated tints flagged above (lines 69/100/111); fixing
those to `neutralBackgroundColor` etc. keeps the `Window` pin exactly right.

---

## 4. Totals

| Verdict | Count | Where |
|---|---|---|
| OK | 55 | phosphor-theme (token source + StateLayer), all of phosphor-shell-widgets, -osd, -notifications, -popout, plus footer colorSet + status text usage |
| HACK | 7 | ThemeHelpers.js:18 (+ its alpha presets), SidebarRow.qml:130, SidebarRow.qml:133, SidebarBackButton.qml:68, UnsavedChangesFooter.qml:69, :100, :111 — all in phosphor-control, all `Qt.rgba(role, α)` surface fabrications routed through ThemeHelpers |
| MISUSE | 0 | — |
| BOUNDARY | 0 | Kirigami and Phosphor tokens never mix in a single component (strict library split, see §1) |

Unclassifiable files: none — every in-scope file was read and either classified or
confirmed color-free.

Key structural observations:

- Every HACK is the same idiom (`ThemeHelpers.withAlpha` over a Kirigami text/highlight
  role) concentrated in `phosphor-control`; retargeting the four call sites to
  `Selection` colorSet + `backgroundColor`, `Kirigami.Theme.hoverColor`, and
  `Kirigami.Theme.neutralBackgroundColor` clears the entire column and lets
  ThemeHelpers.js shrink to nothing.
- The Phosphor-token side is exemplary: alpha application is centralised in the
  StateLayer singleton, elevation tinting in Tokens/PhosphorCard, status colors are
  only used for genuine status, and every binding is written to stay palette-change
  live.
- Phosphor.Theme will not follow matugen (or any KDE scheme change) until something
  wires `PaletteStore.loadFromFile()` at a matugen-emitted palette JSON or runs
  `MatugenRunner`; nothing in the repo currently does.
