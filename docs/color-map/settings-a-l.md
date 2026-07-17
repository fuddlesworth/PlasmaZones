<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Color-usage inventory + remediation map — src/settings/qml/ (A–L)

Scope: every `Kirigami.Theme.*Color` / `colorSet`, `Qt.rgba()`, `Qt.lighter/darker/tint/alpha`, and color literal in QML files whose basename starts with A–L. Phosphor.Theme token usages: **0 found** (nothing skipped). `Qt.rgba(c.r, c.g, c.b, a)` is abbreviated `c@a` in expressions (e.g. `textColor@0.2`).

> **ERRATUM (read before using the tables below).**
> These tables are a PRE-REMEDIATION snapshot. Line numbers and every
> "current state" expression describe the tree BEFORE the fixes on the
> `fix/theme-color-pipeline` branch landed, so they no longer match the
> code. In addition, every Replacement cell that prescribes
> `Kirigami.Theme.separatorColor` is WRONG. That property does not exist
> in Kirigami and evaluates to `undefined` at runtime. The correct
> replacement is
> `Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
> Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)` (see the
> ruleset in the parent map, `../kirigami-color-map.md`). This banner
> supersedes the affected table cells and they have deliberately not
> been rewritten one by one.

## AboutPage.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 83 | `Kirigami.Theme.linkColor` | link text | OK | keep |
| 176 | `Kirigami.Theme.linkColor` | link text | OK | keep |

## ActionListView.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 50 | `_guideColor: textColor@0.75` | tree guide lines (stroke) | HACK fabricated border | `Kirigami.Theme.separatorColor` (guide is a separator); if the strong weight is intentional, `alternateBackgroundColor`. Change in lockstep with MatchExpressionView |
| 332 | `highlightColor` | THEN-leaf bullet dot (accent decoration) | OK | keep (deliberate accent, matches WHEN bullets) |
| 395 | `alternateBackgroundColor` | param pill fill | OK | keep, but MISSING colorSet — declare `Kirigami.Theme.colorSet: Kirigami.Theme.View` on the pill/list so alternate resolves against View |
| 397 | `border.color: textColor@0.2` | param pill border | HACK fabricated border | `Kirigami.Theme.separatorColor` |
| 416 | `_rawColor==="accent" ? highlightColor : ... backgroundColor : _rawColor` | color-param swatch fill (shows user data) | OK | keep (swatch previews stored value / accent sentinel) |
| 418 | `border.color: textColor@0.3` | swatch border | HACK fabricated border | `Kirigami.Theme.separatorColor` |

## ActionRow.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 548 | `disabledTextColor` | param hint text | OK | keep |
| 734 | `String(Kirigami.Theme.highlightColor)` fallback | color-param default value (data, painted as swatch) | OK | keep (theme-derived default, per CLAUDE.md no-hardcoding rule) |
| 746 | `_accentColor: ... highlightColor : appSettings...` | accent-sentinel swatch preview | OK | keep |

## AlgorithmPreview.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 33 | `windowColor: highlightColor` | zone-preview fill default | OK | keep (accent preview) |
| 34 | `windowBorder: textColor` | zone-preview border default | HACK fabricated border | `Kirigami.Theme.separatorColor`-based default — or keep only if it must mirror the live overlay's default border color (state that in a comment) |
| 113 | `Qt.rgba(windowColor…, 0.7)` | zone fill alpha in preview | OK | keep; tidier as `Qt.alpha(root.windowColor, 0.7)` |
| 114 | `Qt.rgba(windowBorder…, 0.9)` | zone border alpha in preview | HACK fabricated border (inherits L34's textColor) | follows L34's fix; tidier as `Qt.alpha(root.windowBorder, 0.9)` |

## AnimatedBoxTrack.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 34 | `disabledTextColor` | track rail (decorative line) | OK | keep (`separatorColor` also viable) |
| 44 | `disabledTextColor` (opacity 0.4) | start marker tick | OK | keep |
| 55 | `disabledTextColor` (opacity 0.4) | end marker tick | OK | keep |
| 68 | `highlightColor` | animated box (accent element) | OK | keep |

## AnimationEventCard.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 465 | `disabledTextColor` | "Current: …" inherit-summary text | OK | keep |

## AnimationProfileEditor.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 222 | `disabledTextColor` | secondary summary text | OK | keep |

## AnimationsPresetsPage.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 134 | `textColor` | preset title text | OK | keep |
| 139 | `disabledTextColor` | preset subtitle text | OK | keep |
| 178 | `textColor` | preset title text | OK | keep |
| 183 | `disabledTextColor` | preset subtitle text | OK | keep |
| 229 | `disabledTextColor` | empty-state / hint text | OK | keep |
| 274 | `textColor` | preset title text | OK | keep |
| 279 | `disabledTextColor` | preset subtitle text | OK | keep |
| 323 | `textColor` | preset title text | OK | keep |
| 328 | `disabledTextColor` | preset subtitle text | OK | keep |
| 370 | `disabledTextColor` | empty-state / hint text | OK | keep |

## ColorButton.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 20 | `border.color: activeFocus ? highlightColor : disabledTextColor` | focus ring / resting swatch border | MISUSE focus==highlight | `activeFocus ? Kirigami.Theme.focusColor : Kirigami.Theme.separatorColor` |
| 61 | `disabledTextColor@0.2` | transparency-checkerboard light square | OK | keep (standard transparency preview; `alternateBackgroundColor` also viable) |
| 62 | `backgroundColor` | transparency-checkerboard base square | OK | keep |

## CurveThumbnail.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 38 | `backgroundColor` | thumbnail card fill | OK | keep; MISSING colorSet — declare `Kirigami.Theme.colorSet: Kirigami.Theme.View` (card in a grid) |
| 39 | `(hover \|\| activeFocus) ? highlightColor : separatorColor ?? disabledTextColor` | hover + focus border | MISUSE focus==highlight | `activeFocus ? Kirigami.Theme.focusColor : hoverArea.containsMouse ? Kirigami.Theme.hoverColor : Kirigami.Theme.separatorColor` |
| 64 | `_strokeColor = highlightColor.toString()` | curve stroke (accent) | OK | keep |

## DecorationSurfaceCard.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 217 | `disabledTextColor` | "Current: …" inherit-summary text | OK | keep |

## DisplayMap.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 254 | `!perScreen ? highlightColor@0.1 : hover ? textColor@0.06 : transparent` | "All Monitors" chip fill (selection tint / hover wash) | HACK fabricated surface (hover branch; selection tint OK) | selected: keep `highlightColor@0.1`; hover: `Kirigami.Theme.alternateBackgroundColor` under `colorSet: View` (or `Qt.alpha(hoverColor, 0.15)`) |
| 256 | `selected: highlightColor@0.5 / focus: highlightColor@0.7 / else textColor@0.1` | chip border + focus ring | MISUSE focus==highlight (+ HACK fabricated border) | focus branch → `Kirigami.Theme.focusColor`; resting border → `Kirigami.Theme.separatorColor`; selected `highlightColor@0.5` OK |
| 342 | `selected ? highlightColor@0.18 : hover ? textColor@0.08 : textColor@0.04` | monitor tile fill | HACK fabricated surface (hover/rest branches; selection tint OK) | rest: `backgroundColor`, hover: `alternateBackgroundColor`, both under `colorSet: View`; keep selected tint |
| 344 | `selected ? highlightColor : focus ? highlightColor@0.7 : textColor@0.2` | tile border + focus ring | MISUSE focus==highlight (+ HACK fabricated border) | focus branch → `Kirigami.Theme.focusColor`; resting → `Kirigami.Theme.separatorColor`; selected `highlightColor` OK |
| 371 | `positiveTextColor@0.18` | "Primary" badge fill (informational, not success state) | MISUSE status as decoration | `Qt.alpha(Kirigami.Theme.highlightColor, 0.18)` |
| 379 | `positiveTextColor` | "Primary" badge text | MISUSE status as decoration | `Kirigami.Theme.textColor` (or `highlightColor` to pair with the tint) |
| 392 | `highlightColor` | override-marker dot (accent) | OK | keep |
| 394 | `border.color: backgroundColor` | knockout ring around override dot | OK | keep |

## EasingPreview.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 335 | `Kirigami.Theme.colorSet: Kirigami.Theme.View` | colorSet declaration on canvas card | OK | keep (model example — other cards should copy this) |
| 337 | `alternateBackgroundColor` | curve-canvas card fill | OK | keep |
| 349 | `active ? highlightColor : highlightColor@0.6` | bezier handle fill (accent) | OK | keep |
| 351 | `highlightedTextColor@0.8` | handle outline on highlight fill | OK | keep |
| 365 | `accentStr = highlightColor.toString()` | curve stroke (accent) | OK | keep |
| 366 | `gridStr = textColor@0.08` | chart gridlines (stroke) | HACK fabricated border | `Kirigami.Theme.separatorColor` |
| 389–390 | `pc = positiveTextColor; strokeStyle = pc@0.6` | y=0/y=1 reference lines (decorative, not success state) | MISUSE status as decoration | `Qt.alpha(Kirigami.Theme.textColor, 0.5)` or a `highlightColor@alpha` tint |
| 403 | `disabledTextColor@0.2` | linear-diagonal reference line | OK | keep |
| 418 | `highlightColor@0.5` | handle connection lines (accent, dashed) | OK | keep |
| 440 | `disabledTextColor` | bezier anchor dots | OK | keep |
| 465 | `disabledTextColor` | polyline anchor dots | OK | keep |
| 474 | `textColor@0.4` | axis label text | HACK (alpha-faded text) | `Kirigami.Theme.disabledTextColor` |
| 604 | `disabledTextColor` | curve value readout text | OK | keep |

## FileDropZone.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 52 | `_highlight ? highlightColor@0.12 : textColor@0.04` | drop-zone fill (drag-over tint / idle surface) | HACK fabricated surface (idle branch; drag tint OK) | idle: `Kirigami.Theme.alternateBackgroundColor` under `colorSet: View` (raised drop target); keep drag-over tint |
| 54 | `_highlight ? highlightColor : textColor@0.25` | drop-zone border | HACK fabricated border (idle branch) | idle: `Kirigami.Theme.separatorColor`; keep drag-over `highlightColor` |
| 64 | `_highlight ? highlightColor : disabledTextColor` | drop-zone icon | OK | keep |
| 69 | `_highlight ? highlightColor : disabledTextColor` | drop-zone label text | OK | keep |

## GlobalSearchField.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 203 | `highlighted ? highlightedTextColor : textColor` | result icon (symbolic recolor) | OK | keep |
| 214 | `highlighted ? highlightedTextColor : textColor` | result title text | OK | keep |
| 222 | `highlighted ? highlightedTextColor : disabledTextColor` | result subtitle text | OK | keep |
| 250 | `disabledTextColor` | empty-state text | OK | keep |

MISSING colorSet: the results popup/list should declare `Kirigami.Theme.colorSet: Kirigami.Theme.View`.

## GroupSortBar.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 76 | `disabledTextColor` | "Group:" caption text | OK | keep |
| 101 | `disabledTextColor` | "Sort:" caption text | OK | keep |

## ImportDropCard.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 85 | `disabledTextColor` | descriptive hint text | OK | keep |

## InputCapture.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 198 | `Qt.rgba(0, 0, 0, 0.4)` | `Overlay.modal` dim scrim behind capture dialog | OK | keep (modal dim over app chrome, matches qqc2-desktop-style convention; not a fabricated surface) |
| 266 | `textColor` | capture-overlay instruction text | OK | keep |
| 281 | `capturing ? highlightColor : textColor` | trigger label text (capture state accent) | OK | keep |
| 288 | `capturing ? highlightColor@0.15 : hover ? textColor@0.05 : transparent` | trigger background fill | HACK fabricated surface (hover branch; capture tint OK) | hover: `Kirigami.Theme.alternateBackgroundColor` under `colorSet: View` (or `Qt.alpha(hoverColor, 0.15)`); keep capture tint |
| 289 | `capturing ? highlightColor : hover ? disabledTextColor : transparent` | trigger border | OK | keep (`separatorColor` for the hover branch would be tidier) |

## KeyboardShortcutOverlay.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 37 | `overlayBg: textColor@0.6` | full-screen scrim behind overlay dialog | HACK fabricated scrim | `Qt.rgba(0, 0, 0, 0.4)`-style modal dim (as InputCapture L198) — textColor-based scrim goes *light* on dark themes, inverting the dim |
| 38 | `subtleBorder: textColor@0.15` | dialog border + key-chip border | HACK fabricated border | `Kirigami.Theme.separatorColor` |
| 39 | `keyChipBg: textColor@0.08` | key-chip fill | HACK fabricated surface | `Kirigami.Theme.alternateBackgroundColor` under `colorSet: View` on the dialog |
| 102 | `backgroundColor` | dialog body fill | OK | keep; MISSING colorSet — declare `Kirigami.Theme.colorSet: Kirigami.Theme.View` on the dialog rectangle |

## KeySequenceInput.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 242 | `capturing ? highlightColor : textColor` | field text (capture state accent) | OK | keep |
| 291 | `capturing ? highlightColor@0.2 : enabled ? backgroundColor : Qt.alpha(backgroundColor, 0.5)` | field background fill | OK | keep (capture tint in range; documented disabled-alpha workaround) |
| 292 | `capturing ? highlightColor : activeFocus ? highlightColor : disabledTextColor` | field border + focus ring | MISUSE focus==highlight | `activeFocus ? Kirigami.Theme.focusColor : Kirigami.Theme.separatorColor` for the non-capturing branches; keep capturing `highlightColor` |

## LayoutComboBox.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 402 | `backgroundColor` | popup background fill | OK | keep; MISSING colorSet — declare `Kirigami.Theme.colorSet: Kirigami.Theme.View` on the popup |
| 403 | `border.color: textColor@0.2` | popup border | HACK fabricated border | `Kirigami.Theme.separatorColor` |
| 437 | `highlighted ? highlightColor@0.15 : backgroundColor` | delegate selection tint / opaque row fill | OK | keep (documented 0.15 wash, in 0.1–0.25 range) |
| 450 | `textColor` | checkmark icon recolor | OK | keep |
| 466 | `textColor@0.2` | mini layout-preview placeholder fill | HACK fabricated surface | `Kirigami.Theme.alternateBackgroundColor` under `colorSet: View` |
| 467 | `highlighted ? highlightColor : textColor@0.9` | mini-preview border (strong) | HACK fabricated border | `Kirigami.Theme.alternateBackgroundColor` (strong) or `separatorColor` if de-emphasis is acceptable |
| 486 | `textColor@0.2` | "None" placeholder fill | HACK fabricated surface | `Kirigami.Theme.alternateBackgroundColor` under `colorSet: View` |
| 487 | `highlighted ? highlightColor : textColor@0.9` | "None" placeholder border (strong) | HACK fabricated border | `Kirigami.Theme.alternateBackgroundColor` (strong) or `separatorColor` |
| 496 | `textColor` | "unavailable" icon recolor | OK | keep |
| 512 | `textColor` | delegate title text | OK | keep |
| 552 | `textColor` (opacity 0.7) | delegate subtitle text | OK | keep (`disabledTextColor` without opacity would be more idiomatic) |

## LayoutGridDelegate.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 141 | `highlightColor@0.15` | card fill, selected (selection tint) | OK | keep |
| 144 | `textColor@0.06` | card fill, hovered | HACK fabricated surface | `Kirigami.Theme.alternateBackgroundColor` under `colorSet: View` (or `Qt.alpha(hoverColor, 0.15)`) |
| 146 | `textColor@0.03` | card fill, resting | HACK fabricated surface | `Kirigami.Theme.backgroundColor` under `colorSet: View` (card on grid page); MISSING colorSet on the card |
| 154 | `highlightColor` (activeFocus) | card focus ring | MISUSE focus==highlight | `Kirigami.Theme.focusColor` |
| 157 | `highlightColor@0.5` (selected) | card border, selected | OK | keep |
| 160 | `highlightColor@0.3` (hovered) | card border, hovered | MISUSE hover drawn with highlight | `Kirigami.Theme.hoverColor` (optionally @alpha) |
| 162 | `textColor@0.08` (resting) | card border, resting | HACK fabricated border | `Kirigami.Theme.separatorColor` |
| 240 | `positiveTextColor` | "default layout/algorithm" favorite icon (marker, not success state) | MISUSE status as decoration | `Kirigami.Theme.textColor` (or a `highlightColor` accent) |
| 259 | `disabledTextColor` | system/lock icon recolor | OK | keep |
| 292 | `disabledTextColor` | filter icon recolor | OK | keep |
| 348 | `effectiveAuto ? textColor : disabledTextColor` | auto-toggle icon recolor | OK | keep |
| 364 | `hidden ? disabledTextColor : textColor` | visibility icon recolor | OK | keep |
| 398 | `disabledTextColor` | zone-count footer text | OK | keep |

## LayoutManageCard.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 57 | `disabledTextColor` | card description text | OK | keep |

## LayoutThumbnail.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 79 | `textColor@previewOpacity` | thumbnail background fill | HACK fabricated surface | `Kirigami.Theme.alternateBackgroundColor` under `colorSet: View`; MISSING colorSet on the thumbnail |
| 81 | `isSelected ? highlightColor : textColor@borderOpacity` | thumbnail border (selected OK) | HACK fabricated border (unselected branch) | unselected: `Kirigami.Theme.separatorColor`; keep selected `highlightColor` |
| 122 | `backgroundColor@0.9` | label plate over zone preview (legibility scrim) | OK | keep; tidier as `Qt.alpha(Kirigami.Theme.backgroundColor, 0.9)` |

## Totals

| Verdict | Count |
|---|---|
| OK | 74 |
| HACK (fabricated surface / border / scrim / alpha-faded text) | 25 |
| MISUSE (focus==highlight, hover==highlight, status-as-decoration) | 11 |
| **Total entries** | **110** |

- MISSING colorSet notes: **8** components — ActionListView param pill, CurveThumbnail, FileDropZone, GlobalSearchField results popup, KeyboardShortcutOverlay dialog, LayoutComboBox popup, LayoutGridDelegate card, LayoutThumbnail (all → `Kirigami.Theme.View`). EasingPreview L335 already declares View correctly and is the in-tree model to copy.
- Phosphor.Theme token usages skipped: **0** (none exist in A–L files).
- HACK breakdown: 14 fabricated borders, 9 fabricated surfaces, 1 fabricated scrim (KeyboardShortcutOverlay L37, theme-inverting), 1 alpha-faded text (EasingPreview L474).
- MISUSE breakdown: 6 focus==highlight (ColorButton L20, CurveThumbnail L39, DisplayMap L256, DisplayMap L344, KeySequenceInput L292, LayoutGridDelegate L154), 1 hover==highlight (LayoutGridDelegate L160), 4 status-as-decoration (DisplayMap L371, DisplayMap L379, EasingPreview L389–390, LayoutGridDelegate L240).
