# Color-usage inventory & remediation map — src/settings/qml, files M–Z

Scope: `/home/nlavender/Projects/PlasmaZones/src/settings/qml/*.qml` with basenames starting M–Z
(case-insensitive; no files start with digits or underscore). 74 QML files scanned; 39 files have
findings. No QML was modified.

> **ERRATUM (read before using the tables below).**
> These tables are a PRE-REMEDIATION snapshot. Line numbers and every
> "current state" expression describe the tree BEFORE the fixes on the
> `fix/theme-color-pipeline` branch landed, so they no longer match the
> code. In addition, every Replacement cell that prescribes
> `K.T.separatorColor` is WRONG. That property does not exist in
> Kirigami and evaluates to `undefined` at runtime. The correct
> replacement is
> `Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
> Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)` (see the
> ruleset in the parent map, `../kirigami-color-map.md`). This banner
> supersedes the affected table cells and they have deliberately not
> been rewritten one by one.
>
> **Recorded decision (do not re-litigate the "under View" rows).**
> WizardConfigCard, WizardPreviewFrame, WhatsNewPage, and
> WizardStepIndicator deliberately resolve `alternateBackgroundColor`
> in their Window/dialog context WITHOUT a `View` colorSet declaration.
> The result is visually sound in that context, so the rows below that
> ask for the alternate role to resolve "under View" are superseded for
> these four files.

**Shorthand** (expressions are exact modulo this legend):
`K.T` = `Kirigami.Theme`; `hl` = `K.T.highlightColor`; `txt` = `K.T.textColor`;
`bg` = `K.T.backgroundColor`; `altBg` = `K.T.alternateBackgroundColor`;
`c@a` = `Qt.rgba(c.r, c.g, c.b, a)`.

**Verdicts**: OK · HACK surface (fabricated surface → bg/altBg under `K.T.colorSet: K.T.View`) ·
HACK border (fabricated border → separatorColor / altBg) · HACK inverted (hand-rolled inverted
toast) · HACK alpha-text (alpha'd textColor as text → disabledTextColor) · MISUSE focus
(focus ring uses highlightColor → focusColor) · MISUSE hover (hover uses highlightColor →
hoverColor) · MISUSE status (status color as decoration → textColor or hl@0.1–0.25) ·
MISUSE fg-on-hl (wrong foreground on a highlight fill → highlightedTextColor) ·
MISSING colorSet (structural; not counted in the expression total).

Out of scope: `Phosphor.Theme` tokens — **0 occurrences** in the M–Z set (nothing skipped).
`Qt.lighter/darker/tint` — 0 occurrences. Per-channel math (`Qt.rgba(c.r*k,…)`) — 0 occurrences.

---

## Main.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 178 | `settingsController.daemonRunning ? K.T.positiveTextColor : K.T.negativeTextColor` | daemon status dot (header) | OK | keep — genuine running/stopped status |
| 1111 | `K.T.neutralTextColor` | sidebar "unsaved changes" dirty badge dot | OK | keep — genuine pending-changes state (neutral = caution) |
| 156–167 | *(structural — `headerExtras` / `headerTrailing` top-bar slots)* | top bar / header toolbar | MISSING colorSet | declare `K.T.colorSet: K.T.Header` (+ `K.T.inherit: false`) on the header toolbar container — KNOWN finding |
| 1074 | *(structural — `sidebar.trailingDelegate`, sidebar rows)* | sidebar navigation chrome | MISSING colorSet | declare `K.T.colorSet: K.T.Header` on the sidebar chrome (or `View` if it should read as a list) — KNOWN finding; Main.qml declares no colorSet anywhere |

## MatchExpressionView.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 309 | `txt@0.75` (`_guideColor`) | tree connector guide lines (stroke) | HACK border | `K.T.separatorColor` (if too faint, keep stroke width 2 rather than fabricating from txt) |
| 495 | `kind==="all" ? K.T.positiveTextColor : kind==="none" ? K.T.negativeTextColor : K.T.neutralTextColor` | ALL/ANY/NONE group-kind badge tint source | MISUSE status | `K.T.textColor` or `hl@0.15–0.25`; kind is a category, not a status |
| 507 | `Qt.rgba(_tint.r,_tint.g,_tint.b,0.4)` | group-kind badge fill | MISUSE status | `hl@0.18` (differentiate kinds by label/icon, not stoplight colors) |
| 509 | `Qt.rgba(_tint.r,_tint.g,_tint.b,0.9)` | group-kind badge border | MISUSE status | `K.T.separatorColor` or `hl@0.4` |
| 528 | `K.T.textColor` | badge label text | OK | keep |
| 550 | `K.T.highlightColor` | leaf bullet dot (accent) | OK | keep — accent decoration, not status |
| 597 | `K.T.alternateBackgroundColor` | value chip fill | OK | keep (ensure enclosing view declares `View` colorSet) |
| 599 | `txt@0.2` | value chip border | HACK border | `K.T.separatorColor` |

## MatchLeafEditor.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 189 | `K.T.highlightColor` | field help/hint icon accent | OK | keep |
| 408 | `K.T.disabledTextColor` | value hint text | OK | keep |

## MetadataChip.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 35 | `root.highlighted ? hl@0.18 : K.T.alternateBackgroundColor` | chip fill / selection tint | OK | keep — selection tint in blessed range, proper token otherwise |

## ModifierAndMouseCheckBoxes.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 199 | `K.T.backgroundColor` | multi-trigger container fill | OK | keep (declare `View` colorSet on the container) |
| 200 | `K.T.disabledTextColor` | container border | HACK border | `K.T.separatorColor` |
| 241 | `hoverHandler.hovered ? K.T.highlightColor : K.T.textColor` | trigger label text on hover | MISUSE hover | `K.T.hoverColor` for hovered branch |
| 348 | `inputCapture.capturing ? K.T.highlightColor : K.T.textColor` | capture field text (recording state) | OK | keep — accent for active capture |
| 350 | `K.T.disabledTextColor` | placeholder text | OK | keep |
| 393 | `capturing ? hl@0.2 : (field.enabled ? bg : Qt.alpha(bg, 0.5))` | capture field background | OK | keep — hl tint in range, bg token proper; disabled via `Qt.alpha(bg,0.5)` acceptable |
| 394 | `capturing ? hl : (field.activeFocus ? hl : K.T.disabledTextColor)` | field border incl. focus ring | MISUSE focus | `K.T.focusColor` for activeFocus branch; `K.T.separatorColor` for idle branch |

## MonitorOverviewTile.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 51 | `selected ? hl@0.1 : containsMouse ? txt@0.06 : "transparent"` | tile fill (selection/hover) | HACK surface | selected branch OK; hover branch → `Qt.alpha(K.T.hoverColor, 0.1)` or `altBg` under `View` colorSet |
| 53 | `selected ? hl@0.5 : activeFocus ? hl@0.7 : txt@0.1` | tile border incl. focus ring | MISUSE focus | activeFocus branch → `K.T.focusColor`; idle branch → `K.T.separatorColor`; selected branch OK |
| 95 | `_isPrimary ? K.T.positiveTextColor@0.15 : "transparent"` | "Primary" badge fill | MISUSE status | `hl@0.15` — "primary monitor" is a category, not a positive status |
| 103 | `K.T.positiveTextColor` | "Primary" badge text | MISUSE status | `K.T.textColor` (on hl tint) or `K.T.highlightColor` |

## MonitorScopeChip.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 86 | `hovered\|\|popup.visible ? hl@0.12 : txt@0.05` | scope pill fill (hover/idle) | HACK surface | idle → `altBg` under `View`; hover → `Qt.alpha(K.T.hoverColor, 0.12)` |
| 88 | `isPerScreen\|\|popup.visible ? hl@0.6 : txt@0.15` | pill border (active/idle) | HACK border | active accent branch OK; idle → `K.T.separatorColor` |
| 101 | `chip.isPerScreen ? K.T.highlightColor : K.T.textColor` | monitor icon (active accent) | OK | keep |
| 116 | `K.T.highlightColor` | per-screen active dot | OK | keep — accent state, not status color |
| 203 | `K.T.backgroundColor` | popup background | OK | keep (see MISSING entry) |
| 205 | `txt@0.15` | popup border | HACK border | `K.T.separatorColor` |
| 201 | *(structural — popup `background:`)* | scope popup | MISSING colorSet | declare `K.T.colorSet: K.T.View` + `inherit: false` on the popup background |

## NewAlgorithmDialog.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 209 | `templateDelegate.selected ? K.T.highlightColor : K.T.disabledTextColor` | template icon (selected accent) | OK | keep |
| 269 | `K.T.disabledTextColor` | preview icon | OK | keep |

## NewLayoutDialog.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 312 | `hl@(selected ? 0.8 : 0.5)` | zone-preview zone fill (`highlightColor` prop) | OK | keep — accent tint for preview media |
| 343 | `hl@0.7` | selected-template large preview zones | OK | keep |

## OrderingPage.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 165 | `K.T.highlightColor` | drag-active delegate border | OK | keep — drag emphasis |
| 168 | `hl@0.15` | drag-active delegate fill | OK | keep — tint in range |
| 171 | `hl@0.06` | hover delegate fill | MISUSE hover | `Qt.alpha(K.T.hoverColor, 0.1)` |
| 249 | `K.T.disabledTextColor` | position number label | OK | keep |
| 259 | `txt@0.2` | layout thumbnail placeholder fill | HACK surface | `altBg` under `View` colorSet |
| 260 | `delegateHover.hovered ? hl : txt@0.9` | thumbnail border (hover/idle) | HACK border | idle → `K.T.separatorColor` (strong: `altBg`); hover → `K.T.hoverColor` |
| 305 | `K.T.disabledTextColor` | description text | OK | keep |
| 318 | `hl@0.15` | zone-count badge fill | OK | keep — accent tint in range |
| 327 | `K.T.highlightColor` | zone-count badge text | OK | keep |

## PositionPicker.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 59 | `K.T.backgroundColor` | screen frame fill | OK | keep (declare `View` colorSet on the frame) |
| 61 | `txt@0.3` | screen frame border | HACK border | `K.T.separatorColor` |
| 75 | `txt@0.05` | screen inner area fill | HACK surface | `altBg` under `View` colorSet |
| 124 | `K.T.highlightColor` | selected cell fill | OK | keep — selection |
| 127 | `hl@0.4` | hovered cell fill | MISUSE hover | `Qt.alpha(K.T.hoverColor, 0.2)` |
| 129 | `txt@0.15` | idle cell fill | HACK surface | `altBg` under `View` colorSet |
| 133 | `K.T.highlightColor` (activeFocus branch) | cell focus ring | MISUSE focus | `K.T.focusColor` |
| 136 | `K.T.highlightColor` (selected branch) | selected cell border | OK | keep |
| 139 | `K.T.highlightColor` (hover branch) | hovered cell border | MISUSE hover | `K.T.hoverColor` |
| 141 | `txt@0.3` | idle cell border | HACK border | `K.T.separatorColor` |
| 149 | `K.T.backgroundColor` | edge-bar glyph on selected (highlight-filled) cell | MISUSE fg-on-hl | `K.T.highlightedTextColor` |
| 169 | `K.T.backgroundColor` | center indicator glyph on highlight fill | MISUSE fg-on-hl | `K.T.highlightedTextColor` |
| 180 | `K.T.backgroundColor` | edge-bar glyph on highlight fill | MISUSE fg-on-hl | `K.T.highlightedTextColor` |

## RuleRow.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 104 | `hl@0.4` | rule-kind pill fill | OK | keep (consider lowering to hl@0.18 to match sibling badges) |
| 106 | `hl@0.9` | rule-kind pill border | OK | keep — accent border |
| 119 | `K.T.textColor` | pill label text | OK | keep |
| 198 | `hl@0.18` | section badge fill | OK | keep — tint in range |
| 220 | `K.T.alternateBackgroundColor` | composite chip fill | OK | keep (row container should declare `View`) |
| 243 | `K.T.alternateBackgroundColor` | condition chip fill | OK | keep |
| 262 | `K.T.alternateBackgroundColor` | action chip fill | OK | keep |
| 283 | `K.T.alternateBackgroundColor` | priority chip fill | OK | keep |

## RuleStartTile.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 43 | `pressed ? hl@0.25 : containsMouse ? hl@0.12 : txt@0.05` | tile fill (pressed/hover/idle) | HACK surface | pressed tint OK; hover → `Qt.alpha(K.T.hoverColor, 0.12)`; idle → `altBg` under `View` |
| 45 | `containsMouse \|\| activeFocus ? hl@0.5 : txt@0.1` | tile border incl. focus ring | MISUSE focus | activeFocus → `K.T.focusColor`; hover → `K.T.hoverColor`; idle → `K.T.separatorColor` |

## SegmentedViewSwitch.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 54 | `selected ? hl@0.1 : containsMouse ? txt@0.06 : "transparent"` | segment tile fill | HACK surface | selected tint OK; hover branch → `Qt.alpha(K.T.hoverColor, 0.1)` or `altBg` under `View` |
| 59 | `activeFocus ? hl@0.7 : selected ? hl@0.5 : txt@0.1` | tile border incl. focus ring | MISUSE focus | activeFocus → `K.T.focusColor`; selected branch OK; idle → `K.T.separatorColor` |

## SettingsButtonGroup.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 54 | `hl@0.2` | active option fill (selection) | OK | keep — tint in range |
| 57 | `txt@0.08` | hovered option fill | HACK surface | `Qt.alpha(K.T.hoverColor, 0.1)` or `altBg` under `View` |
| 59 | `txt@0.04` | idle option fill | HACK surface | `K.T.backgroundColor`/`altBg` under `View` colorSet |
| 62 | `activeFocus ? hl : isActive ? hl@0.4 : txt@0.08` | option border incl. focus ring | MISUSE focus | activeFocus → `K.T.focusColor`; active branch OK; idle → `K.T.separatorColor` |

## SettingsCard.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 213 | `txt@0.03` | card fill ("slightly elevated") | HACK surface | `altBg` under `K.T.colorSet: K.T.View` (card-on-page elevation) |
| 217 | `txt@0.04` | disabled card border | HACK border | `K.T.separatorColor` (dim via `opacity` when disabled) |
| 220 | `hl@0.4` | hovered card border | MISUSE hover | `K.T.hoverColor` |
| 222 | `txt@0.08` | idle card border | HACK border | `K.T.separatorColor` |
| 247 | `txt@0.03` | card header band fill | HACK surface | `altBg` under `View` (or `Header` colorSet on the header band) |
| 289 | `K.T.highlightColor` | header focus ring | MISUSE focus | `K.T.focusColor` |
| 205 | *(structural — card root/`cardBg`)* | settings card | MISSING colorSet | declare `K.T.colorSet: K.T.View` + `inherit: false`; this file is the template many other cards copy |

## SettingsFlickable.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 204 | `K.T.highlightColor` | search scroll-to-anchor attention flash border | OK | keep — attention/locate flash, not a keyboard-focus ring |

## SettingsRow.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 98 | `K.T.disabledTextColor` | row description text | OK | keep |

## SettingsSwitch.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 45 | `K.T.highlightColor` | switch focus ring | MISUSE focus | `K.T.focusColor` |
| 57 | `checked ? hl : txt@0.2` | switch track fill | HACK surface | checked branch OK; unchecked → `altBg` under `View` colorSet |
| 66 | `K.T.highlightedTextColor` | switch knob fill | OK | keep (correct on the checked highlight track; acceptable on unchecked) |
| 67 | `txt@0.15` | knob border | HACK border | `K.T.separatorColor` |
| 101 | `checked ? K.T.textColor : K.T.disabledTextColor` | switch label text | OK | keep |

## ShaderBrowserCard.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 94 | `txt@(hovered ? 0.06 : 0.04)` | card fill | HACK surface | `altBg` under `View` colorSet (hover via `Qt.alpha(K.T.hoverColor, …)`) |
| 98 | `K.T.focusColor` | card focus ring | OK | keep — exemplary; copy this pattern elsewhere |
| 101 | `hl@0.4` | hovered card border | MISUSE hover | `K.T.hoverColor` |
| 103 | `txt@0.12` | idle card border | HACK border | `K.T.separatorColor` |
| 132 | `txt@0.08` | preview thumbnail placeholder fill | HACK surface | `altBg` under `View` |
| 134 | `txt@0.12` | preview placeholder border | HACK border | `K.T.separatorColor` |
| 180 | `K.T.positiveTextColor` | "User" source badge text | MISUSE status | `K.T.textColor` or `K.T.highlightColor` — provenance is a category, not a positive status |
| 191 | `K.T.disabledTextColor` | description text | OK | keep |
| 206 | `K.T.disabledTextColor` | parameter-count text | OK | keep |
| 229 | `K.T.disabledTextColor` | usage chip text | OK | keep |
| 90 | *(structural — card root Rectangle)* | shader card | MISSING colorSet | declare `K.T.colorSet: K.T.View` + `inherit: false` |

## ShaderBrowserDetailDialog.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 255 | `hl@0.85` | category chip fill | OK | keep — near-solid highlight chip with proper contrasting text |
| 267 | `K.T.highlightedTextColor` | category chip text | OK | keep |
| 275 | `K.T.positiveTextColor` | "User" source badge text | MISUSE status | `K.T.textColor` or `K.T.highlightColor` (same as ShaderBrowserCard:180) |
| 286 | `K.T.disabledTextColor` | parameter-count text | OK | keep |
| 316 | `K.T.disabledTextColor` | author/version metadata text | OK | keep |
| 345 | `K.T.positiveTextColor` | "in use" checkmark icon | OK | keep — genuine positive state (effect is in use) |
| 363 | `K.T.disabledTextColor` | usage list text | OK | keep |
| 453 | `index % 2 === 0 ? "transparent" : txt@0.04` | parameter table zebra-stripe fill | HACK surface | `altBg` under `View` colorSet for odd rows |
| 491 | `K.T.disabledTextColor` | parameter detail text | OK | keep |
| 522 | `"black"` | live shader preview backdrop | OK | keep — intentional true-black media backdrop (commented); theme tint would contaminate previewed colors |
| 524 | `txt@0.15` | preview frame border | HACK border | `K.T.separatorColor` |
| 640 | `K.T.disabledTextColor` | "Preview unavailable" placeholder text | OK | keep |

## ShaderSetsPage.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 140 | `K.T.disabledTextColor` | save-description text | OK | keep |
| 243 | `K.T.disabledTextColor` | empty-state text | OK | keep |

## ShortcutCaptureField.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 173 | `capturing ? K.T.highlightColor : K.T.textColor` | capture field text (recording state) | OK | keep — accent for active capture |

## SpringPreview.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 106 | `K.T.colorSet: K.T.View` | chart surface colorSet declaration | OK | keep — exemplary; the pattern other cards should follow |
| 108 | `K.T.alternateBackgroundColor` | chart background fill | OK | keep |
| 125 | `K.T.highlightColor.toString()` | spring curve stroke (accent data line) | OK | keep |
| 126 | `hl@0.2 .toString()` | dimmed secondary curve stroke | OK | keep |
| 127 | `txt@0.08 .toString()` | canvas grid lines | HACK border | `K.T.separatorColor.toString()` |
| 158 | `var pc = K.T.positiveTextColor` | reference-line color source (y=0/y=1 guides) | MISUSE status | `K.T.textColor` (with alpha) or `hl` — guides are decoration, not a positive status |
| 159 | `Qt.rgba(pc.r,pc.g,pc.b,0.6).toString()` | dashed reference-line stroke | MISUSE status | `Qt.alpha(K.T.separatorColor, …)` or `hl@0.6` |
| 230 | `txt@0.4 .toString()` | canvas axis-label text fill | HACK alpha-text | `K.T.disabledTextColor.toString()` |
| 266 | `K.T.disabledTextColor` | spring parameter readout text | OK | keep |

## TilingAlgorithmPage.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 195 | `K.T.backgroundColor` | algorithm preview backdrop fill | OK | keep (declare `View` colorSet on the preview container) |
| 196 | `K.T.disabledTextColor` | preview backdrop border | HACK border | `K.T.separatorColor` |

## Toast.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 28 | `txt@0.85` (`toastBg`) | toast surface (hand-inverted) | HACK inverted | `K.T.colorSet: K.T.Tooltip` + `K.T.inherit: false`, fill with `K.T.backgroundColor` |
| 71 | `K.T.backgroundColor` | toast message text (inverted fg) | HACK inverted | `K.T.textColor` under the `Tooltip` colorSet |
| 28 | *(structural)* | toast component | MISSING colorSet | declare `K.T.colorSet: K.T.Tooltip` — replaces both hand-inverted colors above |

## VirtualScreenPreview.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 36 | `bg@0.5` | preview canvas fill | OK | keep (or solid `bg` under `View` colorSet) — built from the correct surface token |
| 37 | `txt@0.3` | preview canvas border | HACK border | `K.T.separatorColor` |
| 46 | `K.T.disabledTextColor` | empty-state text | OK | keep |
| 66 | `hl@0.15` | screen region fill (accent tint) | OK | keep — tint in range |
| 67 | `K.T.highlightColor` | screen region border | OK | keep — accent frame |
| 87 | `K.T.textColor` | region title text | OK | keep |
| 106 | `K.T.disabledTextColor` | region size detail text | OK | keep |
| 143 | `containsMouse\|\|pressed ? hl : txt@0.5` | column divider line | HACK border | idle → `K.T.separatorColor`; hover/drag → `K.T.hoverColor` |
| 166 | `containsMouse\|\|pressed ? hl@0.3 : txt@0.1` | column drag-handle fill | HACK surface | idle → `altBg` under `View`; hover → `Qt.alpha(K.T.hoverColor, 0.3)` |
| 168 | `containsMouse\|\|pressed ? hl : txt@0.2` | column drag-handle border | HACK border | idle → `K.T.separatorColor`; hover → `K.T.hoverColor` |
| 183 | `K.T.textColor` (opacity 0.5) | handle grip dots | OK | keep — opacity on proper token |
| 252 | `containsMouse\|\|pressed ? hl : txt@0.5` | row divider line | HACK border | idle → `K.T.separatorColor`; hover/drag → `K.T.hoverColor` |
| 275 | `containsMouse\|\|pressed ? hl@0.3 : txt@0.1` | row drag-handle fill | HACK surface | idle → `altBg` under `View`; hover → `Qt.alpha(K.T.hoverColor, 0.3)` |
| 277 | `containsMouse\|\|pressed ? hl : txt@0.2` | row drag-handle border | HACK border | idle → `K.T.separatorColor`; hover → `K.T.hoverColor` |
| 292 | `K.T.textColor` (opacity 0.5) | handle grip dots | OK | keep |

## VirtualScreensPage.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 495 | `K.T.disabledTextColor` | resolution/summary text | OK | keep |
| 686 | `active ? hl@0.15 : hovered ? txt@0.06 : txt@0.03` | preset card fill | HACK surface | active tint OK; hover → `Qt.alpha(K.T.hoverColor, 0.1)`; idle → `altBg` under `View` |
| 688 | `active ? hl@0.5 : hovered ? hl@0.3 : txt@0.08` | preset card border | HACK border | active accent OK; hover → `K.T.hoverColor`; idle → `K.T.separatorColor` |
| 712 | `txt@0.08` | preset thumbnail fill | HACK surface | `altBg` under `View` |
| 714 | `active ? hl : txt@0.12` | preset thumbnail border | HACK border | active accent OK; idle → `K.T.separatorColor` |
| 744 | `K.T.disabledTextColor` | preset detail text | OK | keep |
| 881 | `K.T.disabledTextColor` | row index label | OK | keep |
| 909 | `K.T.disabledTextColor` | width-percent readout | OK | keep |
| 923 | `K.T.disabledTextColor` | width-pixels readout | OK | keep |
| 937 | `palette.buttonText: K.T.negativeTextColor` | destructive "Remove Subdivisions" button text | OK | keep — genuine destructive-action semantics |

## WhatsNewPage.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 13 | `txt@0.03` (`subtleBg`) | changelog card fill | HACK surface | `altBg` under `K.T.colorSet: K.T.View` |
| 14 | `txt@0.08` (`subtleBorder`) | changelog card border | HACK border | `K.T.separatorColor` |
| 45 | `cardHover.hovered ? hl@0.4 : root.subtleBorder` | card border on hover | MISUSE hover | `K.T.hoverColor` (idle → `K.T.separatorColor`) |

## WideComboBox.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 162 | `K.T.backgroundColor` | popup background fill | OK | keep (see MISSING entry) |
| 163 | `txt@0.2` | popup border | HACK border | `K.T.separatorColor` |
| 181 | `highlighted ? hl : isCurrentSelection ? hl@0.15 : bg` | list delegate fill (keyboard highlight / current / idle) | OK | keep — textbook selection handling |
| 186 | `highlighted ? K.T.highlightedTextColor : K.T.textColor` | delegate text | OK | keep |
| 161 | *(structural — popup `background:`)* | combo popup list | MISSING colorSet | declare `K.T.colorSet: K.T.View` + `inherit: false` on the popup |

## WindowAppearancePage.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 175 | `K.T.disabledTextColor` | informational note text | OK | keep |
| 357 | `K.T.disabledTextColor` | informational note text | OK | keep |

## WindowPickerDialog.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 289 | `highlighted ? hl : "transparent"` | window list delegate selection fill | OK | keep |
| 302 | `highlighted ? K.T.highlightedTextColor : K.T.textColor` | delegate icon recolor | OK | keep |
| 313 | `highlighted ? K.T.highlightedTextColor : K.T.textColor` | delegate primary text | OK | keep |
| 320 | `highlighted ? K.T.highlightedTextColor : K.T.disabledTextColor` | delegate secondary text | OK | keep |

## WizardConfigCard.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 18 | `WizardUtils.wizardColors(K.T.textColor, K.T.highlightColor)` | card fill+border palette (fabricated: `txt@0.03` bg, `txt@0.08` border, `hl@0.3` accent) | HACK surface | fix at the source — `WizardUtils.js:32-45 wizardColors()`: subtleBg/defaultBg → `altBg` under `View`; subtleBorder/defaultBorder → `K.T.separatorColor`; hoverBorder → `K.T.hoverColor` |

## WizardFooter.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 39 | `K.T.negativeTextColor` | wizard validation error text | OK | keep |

## WizardPreviewBadge.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 26 | `bg@0.9` | badge scrim over preview frame | OK | keep — theme surface token at high alpha over preview media |

## WizardPreviewFrame.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 21 | `WizardUtils.wizardColors(K.T.textColor, K.T.highlightColor)` | preview frame fill/border palette | HACK surface | same as WizardConfigCard:18 — remediate `WizardUtils.js wizardColors()` |

## WizardStepIndicator.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 43 | `K.T.highlightColor` | current-step dot fill | OK | keep |
| 46 | `hl@0.4` | completed-step dot fill | OK | keep — progress accent |
| 48 | `txt@0.15` | upcoming-step dot fill | HACK surface | `altBg` under `View` (or `K.T.disabledTextColor` at reduced size) |
| 60 | `index <= currentStep ? K.T.highlightedTextColor : K.T.textColor` | step number text | OK | keep |
| 90 | `completed ? hl@0.4 : txt@0.15` | step connector line | HACK border | completed accent OK; upcoming → `K.T.separatorColor` |

## WizardTemplateCard.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 26 | `WizardUtils.wizardColors(K.T.textColor, K.T.highlightColor)` | card fill/border palette | HACK surface | remediate `WizardUtils.js wizardColors()` (see WizardConfigCard:18) |
| 74 | `activeFocus ? hl : selected ? _selectedBorder : isHovered ? _hoverBorder : _defaultBorder` | card border incl. focus ring | MISUSE focus | activeFocus → `K.T.focusColor`; selected `hl@0.6` OK; hover → `K.T.hoverColor`; default → `K.T.separatorColor` |
| 147 | `K.T.highlightColor` | selected checkmark badge fill | OK | keep |
| 155 | `K.T.highlightedTextColor` | checkmark icon on highlight fill | OK | keep |
| 66 | *(structural — card root)* | wizard template card | MISSING colorSet | declare `K.T.colorSet: K.T.View` + `inherit: false` |

## ZoneSelectorSection.qml

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 339 | `hl@0.4` | zone-selector preview accent frame | OK | keep — accent frame decoration |
| 360 | `txt@0.35` | mini zone cell fill | HACK surface | `altBg` under `View` (or `hl@0.15` to match real zone previews) |
| 361 | `txt@0.7` | mini zone cell border (strong) | HACK border | `K.T.separatorColor`; if too subtle, `altBg` per strong-border rule |
| 369 | `K.T.textColor` (opacity 0.5) | zone number text | OK | keep |

---

## Totals

| Verdict | Count |
|---|---|
| OK | 95 |
| HACK — fabricated surface (`Qt.rgba(txt, a)` as fill → bg/altBg under `View` colorSet) | 25 |
| HACK — fabricated border (`Qt.rgba(txt, a)` as stroke → separatorColor / altBg) | 27 |
| HACK — hand-inverted toast (surface + text → `Tooltip` colorSet) | 2 |
| HACK — alpha'd textColor as text (→ disabledTextColor) | 1 |
| MISUSE — focus ring drawn with highlightColor (→ focusColor) | 9 |
| MISUSE — hover drawn with highlightColor (→ hoverColor) | 7 |
| MISUSE — status color as decoration (→ textColor / hl@alpha) | 9 |
| MISUSE — wrong foreground on highlight fill (→ highlightedTextColor) | 3 |
| MISUSE — per-channel math (`Qt.rgba(c.r*k, …)`) | 0 |
| **Total classified color expressions** | **178** (across 39 files) |
| MISSING colorSet (structural findings, counted separately) | 8 — Main.qml header (156–167) and sidebar (1074) → `Header` [KNOWN]; SettingsCard.qml (~205), ShaderBrowserCard.qml (~90), WideComboBox.qml popup (161), MonitorScopeChip.qml popup (201), WizardTemplateCard.qml (~66) → `View`; Toast.qml (28/71) → `Tooltip` |
| Phosphor.Theme token usages (out of scope, skipped) | 0 |
| Unclassifiable lines/files | none |

Highest-leverage fixes: `WizardUtils.js wizardColors()` (single function feeding three Wizard*
components), `SettingsCard.qml` (the fabricated-surface template other cards cite), and the 9
focusColor substitutions (mechanical).
