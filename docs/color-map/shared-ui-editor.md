# Color-Usage Inventory & Remediation Map — src/shared, src/ui, src/editor/qml

Scope: every QML line using `Kirigami.Theme.*Color`, `Kirigami.Theme.colorSet`, `Qt.rgba/lighter/darker/tint/alpha`,
`Theme.withAlpha` (editor helper = `Qt.rgba(c.r,c.g,c.b,a)`), or color literals in:
`/home/nlavender/Projects/PlasmaZones/src/shared/`, `src/ui/`, `src/editor/qml/`.

Notation: `K.T` = `Kirigami.Theme`; `rgba(X, a)` = `Qt.rgba(X.r, X.g, X.b, a)` (per-channel spread elided).
Line numbers as of 2026-07-16.

> **ERRATUM (read before using the tables below).**
> These tables are a PRE-REMEDIATION snapshot. Line numbers and every
> "current state" expression describe the tree BEFORE the fixes on the
> `fix/theme-color-pipeline` branch landed, so they no longer match the
> code. In addition, every Replacement cell that prescribes
> `Kirigami.Theme.separatorColor` (including the HACK-BORDER verdict
> definition above) is WRONG. That property does not exist in Kirigami
> and evaluates to `undefined` at runtime. The correct replacement is
> `Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
> Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)` (see the
> ruleset in the parent map, `../kirigami-color-map.md`). This banner
> supersedes the affected table cells and they have deliberately not
> been rewritten one by one.
> Additionally, `src/ui/ZoneSelector.qml` and `src/ui/LayoutPreview.qml`
> were DELETED on this branch (runtime-dead: nothing instantiated them),
> so their rows below are historical.

Verdict tokens:
- **OK** — correct KDE semantic use.
- **OK-OVERLAY** — self-contained contrast scrim/chrome on a compositor overlay rendered over arbitrary desktop content; legitimate there, not a pattern to copy into settings UI.
- **HACK-SURFACE** — fabricated surface: fill built from `textColor@alpha` instead of a background role.
- **HACK-BORDER** — fabricated border: `textColor@alpha` (or solid textColor) border instead of `separatorColor`/`alternateBackgroundColor`.
- **HACK-TEXT** — secondary/dim text via `textColor@alpha` instead of `disabledTextColor`.
- **MISUSE-STATUS** — status color (`positive/active/neutral/negativeTextColor`, `linkColor`) used as decoration.
- **MISUSE-FOCUS** — `highlightColor` focus ring instead of `focusColor`.
- **MISUSE-MATH** — per-channel arithmetic (`c.r*k`, channel blends) instead of `Qt.lighter/darker/tint`.
- **MISUSE-PAIRING** — `highlightedTextColor` placed on a non-highlight fill.
- **LITERAL** — hardcoded `"white"/"black"/#hex` needing a theme role.

Note on hot-spot list: `LayoutThumbnail.qml` and `AlgorithmPreview.qml` live in
`/home/nlavender/Projects/PlasmaZones/src/settings/qml/` — **outside the three directories assigned here**; they are
not inventoried in this file and need their own pass. `Phosphor.Theme` tokens: **0 occurrences** in scope (all
`Phosphor*` hits are `PhosphorMotionAnimation`, not color tokens).

---

## src/shared/

### ShaderCompileErrorBanner.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 30 | `K.T.backgroundColor` | banner fill | OK | — |
| 33 | `border.color: K.T.negativeTextColor` | error banner border | OK | — |
| 46 | `K.T.negativeTextColor` | error icon | OK | — |
| 55 | `K.T.negativeTextColor` | error title text | OK | — |
| 73 | `K.T.textColor` | body text | OK | — |

MISSING colorSet: none needed (inline banner inherits host).

### CategoryBadge.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 35 | `rgba(K.T.neutralTextColor, backgroundOpacity)` | "Dynamic" badge fill | MISUSE-STATUS | `Qt.alpha(K.T.highlightColor, 0.15)` |
| 38 | `rgba(K.T.activeTextColor, backgroundOpacity)` | "Auto" badge fill | MISUSE-STATUS | `Qt.alpha(K.T.highlightColor, 0.15)` |
| 40 | `rgba(K.T.textColor, backgroundOpacity)` | "Manual" badge fill | OK | — (ruleset-sanctioned badge tint; alt: `Qt.alpha(K.T.textColor, 0.15)` stays) |
| 57 | `K.T.neutralTextColor` | "Dynamic" badge label | MISUSE-STATUS | `K.T.textColor` |
| 60 | `K.T.activeTextColor` | "Auto" badge label | MISUSE-STATUS | `K.T.textColor` (or `highlightColor` if differentiation needed) |
| 62 | `K.T.textColor` | "Manual" badge label | OK | — |

### PopupFrame.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 36 | `property color backgroundColor: K.T.backgroundColor` | popup fill source | OK | — |
| 37 | `property color textColor: K.T.textColor` | popup text source | OK | — |
| 115 | `shadowColor: rgba(root.backgroundColor, style.shadowAlpha)` | popup drop shadow | OK-OVERLAY | prefer `Qt.rgba(0,0,0,style.shadowAlpha)` — shadows are black over media, bg-tinted shadow is invisible on same-color bg |
| 128 | `rgba(root.backgroundColor, style.backgroundAlpha)` | OSD chrome fill | OK-OVERLAY | — |
| 130 | `border.color: rgba(root.textColor, style.borderAlpha)` | OSD chrome border | HACK-BORDER | `K.T.separatorColor` (keep alpha if needed for overlay) |

### AspectRatioBadge.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 43 | `K.T.textColor` | "standard" badge color | OK | — |
| 45 | `K.T.positiveTextColor` | "ultrawide" badge color | MISUSE-STATUS | `K.T.textColor` or `K.T.highlightColor` |
| 47 | `K.T.neutralTextColor` | "super-ultrawide" badge color | MISUSE-STATUS | `K.T.textColor` |
| 49 | `K.T.activeTextColor` | "portrait" badge color | MISUSE-STATUS | `K.T.textColor` |
| 51 | `K.T.textColor` | fallback badge color | OK | — |
| 60 | `rgba(badgeColor, backgroundOpacity)` | badge fill (derived) | MISUSE-STATUS | inherits 45–49; after fix becomes `Qt.alpha(K.T.textColor, 0.15)` — then OK |

### LayoutCard.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 47 | `zoneHighlightColor: K.T.highlightColor` | zone highlight source | OK | — |
| 48 | `zoneInactiveColor: K.T.textColor` | inactive zone fill source | HACK-SURFACE | `K.T.alternateBackgroundColor` under `colorSet: View` |
| 49 | `zoneBorderColor: K.T.textColor` | zone border source | HACK-BORDER | `K.T.separatorColor` |
| 54 | `highlightColor: K.T.highlightColor` | card highlight source | OK | — |
| 55 | `textColor: K.T.textColor` | card text source | OK | — |
| 56 | `backgroundColor: K.T.backgroundColor` | card fill source | OK | — |
| 76 | `rgba(root.highlightColor, style.fillActive)` | active card fill | OK | — (keep alpha ≤0.25) |
| 79 | `rgba(root.highlightColor, style.fillSelected)` | selected card fill | OK | — (keep alpha ≤0.25) |
| 82 | `rgba(root.textColor, style.fillHovered)` | hovered card fill | HACK-SURFACE | `Qt.alpha(K.T.hoverColor, 0.2)` or `alternateBackgroundColor` |
| 88 | `rgba(root.highlightColor, style.borderActive)` | active card border | OK | — |
| 91 | `rgba(root.highlightColor, style.borderSelected)` | selected card border | OK | — |
| 200 | `rgba(root.textColor, style.fillNeutral)` | neutral card fill | HACK-SURFACE | `K.T.alternateBackgroundColor` under `colorSet: View` |
| 239 | `K.T.highlightColor` | active-check dot fill | OK | — |
| 248 | `K.T.highlightedTextColor` | checkmark on highlight dot | OK | — |
| 354 | `rgba(root.textColor, style.labelDimAlpha)` | dim label text | HACK-TEXT | `K.T.disabledTextColor` |

MISSING colorSet: add `Kirigami.Theme.colorSet: Kirigami.Theme.View` (card grid embedded in settings pages).

### ParameterRow.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 303 | `_missingColorFallback: K.T.backgroundColor` | missing-value swatch fallback | OK | — |
| 343 | `activeFocus ? K.T.focusColor : rgba(K.T.textColor, 0.3)` | swatch focus ring / rest border | OK (focusColor) + HACK-BORDER (rest branch) | rest branch → `K.T.separatorColor` |

(Line 300 is a comment referencing `"#ffffff"`, not code.)

### ParameterSection.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 114 | `K.T.textColor` | section header text | OK | — |
| 169 | `rgba(K.T.highlightColor, 0.2)` | reset-badge selection tint | OK | — |
| 177 | `K.T.textColor` | badge text | OK | — |
| 184 | `hovered ? K.T.hoverColor : "transparent"` | header hover fill | OK | — |
| 195 | `rgba(K.T.textColor, 0.2)` | section separator line | HACK-BORDER | `K.T.separatorColor` |

### CategoryMenuButton.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 340 | `palette.window: K.T.backgroundColor` | menu palette bridge | OK | — |
| 623 | `palette.window: K.T.backgroundColor` | menu palette bridge | OK | — |

### CapabilityBadgeRow.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 59 | `K.T.positiveTextColor` | "persistent" capability icon | MISUSE-STATUS | `K.T.textColor` (differentiate by icon, not status hue) |
| 69 | `K.T.highlightColor` | "reflows" capability icon | OK | — (ruleset-sanctioned accent) |
| 80 | `K.T.neutralTextColor` | "script state" capability icon | MISUSE-STATUS | `K.T.textColor` |
| 90 | `K.T.activeTextColor` | "single-window" capability icon | MISUSE-STATUS | `K.T.textColor` |
| 101 | `K.T.linkColor` | "follows focus" capability icon | MISUSE-STATUS | `K.T.textColor` |

### ZonePreview.qml  (settings-embedded preview; hot spot)
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 86 | `highlightColor: rgba(K.T.highlightColor, 0.7)` | selected zone fill default | OK | — (alpha stripped at 190; opacity is the real control) |
| 88 | `inactiveColor: rgba(K.T.textColor, 0.4)` | inactive zone fill default | HACK-SURFACE | `K.T.alternateBackgroundColor` (colorSet View) or `Qt.alpha(K.T.backgroundColor, …)` |
| 90 | `borderColor: rgba(K.T.textColor, 0.9)` | zone border default | HACK-BORDER | `K.T.separatorColor` |
| 92 | `labelFontColor: K.T.textColor` | zone number label | OK | — |
| 190 | `Qt.rgba(base…, 1.0)` | alpha-strip so `opacity` is sole alpha | OK | — (documented shader-parity technique) |
| 196 | `Qt.rgba(Math.min(1, root.highlightColor.r * 1.2), …, 1)` | hovered zone border brighten | MISUSE-MATH | `Qt.lighter(root.highlightColor, 1.2)` |
| 327 | `K.T.positiveTextColor` | master-zone indicator dot | MISUSE-STATUS | `K.T.highlightColor` (or `Qt.alpha(highlightColor, 0.8)`) |

MISSING colorSet: add `Kirigami.Theme.colorSet: Kirigami.Theme.View` when embedded in settings pages.

### ParameterEditor.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 583 | `K.T.disabledTextColor` | placeholder/hint text | OK | — |

---

## src/ui/  (compositor overlays / OSDs — rendered over arbitrary desktop content)

### LayoutPreview.qml  (hot spot)
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 38 | `highlightColor: rgba(K.T.highlightColor, 0.5)` | hovered layout-card fill | OK-OVERLAY | — |
| 39 | `activeColor: rgba(K.T.highlightColor, 0.7)` | active layout-card fill | OK-OVERLAY | — |
| 40 | `borderColor: rgba(K.T.textColor, 0.6)` | card border default | HACK-BORDER | `K.T.separatorColor` (or bg-derived contrast line) |
| 70 | `rgba(K.T.textColor, inactiveOpacity)` | inactive card fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` — derive overlay fills from backgroundColor, not textColor |
| 73 | `isHovered||isActive ? K.T.textColor : rgba(K.T.textColor, hoverOpacity)` | card border (both states) | HACK-BORDER | hover/active → `K.T.highlightColor`; rest → `K.T.separatorColor` |
| 126 | `rgba(K.T.backgroundColor, 0.7)` | label strip scrim | OK-OVERLAY | — |
| 156 | `K.T.textColor` | layout name text | OK | — |
| 178 | `K.T.highlightColor` | active-check dot fill | OK | — |
| 188 | `K.T.highlightedTextColor` | checkmark on highlight dot | OK | — |
| 206 | `Qt.rgba(0, 0, 0, 0.5)` | lock overlay scrim | HACK-SURFACE | `rgba(K.T.backgroundColor, 0.5)` — match sibling LayoutOsdContent:188 / ZoneSelectorContent:482 (not media, so black@alpha exemption does not apply) |
| 214 | `color: "white"` | lock icon on scrim | LITERAL | `K.T.textColor` (once scrim is backgroundColor-derived) |

### RenderNodeOverlay.qml  (hot spot)
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 50 | `labelFontColor: K.T.textColor` | zone label | OK | — |
| 65 | `highlightColor: rgba(K.T.highlightColor, 0.7)` | active zone fill | OK-OVERLAY | — |
| 66 | `inactiveColor: rgba(K.T.textColor, 0.4)` | inactive zone fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` |
| 67 | `borderColor: rgba(K.T.textColor, 0.9)` | zone border | HACK-BORDER | `K.T.separatorColor` @0.9, or contrast line derived from backgroundColor |

### RenderNodeOverlayContent.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 37 | `labelFontColor: K.T.textColor` | zone label | OK | — |
| 52 | `highlightColor: rgba(K.T.highlightColor, 0.7)` | active zone fill | OK-OVERLAY | — |
| 53 | `inactiveColor: rgba(K.T.textColor, 0.4)` | inactive zone fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` |
| 54 | `borderColor: rgba(K.T.textColor, 0.9)` | zone border | HACK-BORDER | `K.T.separatorColor` |
| 180 | `K.T.backgroundColor` | error banner fill | OK-OVERLAY | — |
| 183 | `border.color: K.T.negativeTextColor` | error banner border | OK | — |
| 192 | `K.T.textColor` | error banner text | OK | — |

### ZoneOverlayContent.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 23 | `highlightColor: rgba(K.T.highlightColor, 0.7)` | active zone fill | OK-OVERLAY | — |
| 24 | `inactiveColor: rgba(K.T.textColor, 0.4)` | inactive zone fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` |
| 25 | `borderColor: rgba(K.T.textColor, 0.9)` | zone border | HACK-BORDER | `K.T.separatorColor` |
| 26 | `labelFontColor: K.T.textColor` | zone label | OK | — |
| 99 | `rgba(K.T.textColor, 0.5)` | "no zones" debug text | HACK-TEXT | `K.T.disabledTextColor` |
| 194 | `rgba(K.T.backgroundColor, 0.85)` | preview card chrome fill | OK-OVERLAY | — |
| 195 | `border.color: rgba(root.borderColor, 0.15)` | preview card border | HACK-BORDER | `K.T.separatorColor` (borderColor is textColor-derived; double fabrication) |

### ZoneSelector.qml  (hot spot)
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 25 | `activeColor: rgba(K.T.highlightColor, activeOpacity)` | active layout fill | OK-OVERLAY | — |
| 29 | `backgroundColor: rgba(K.T.backgroundColor, backgroundOpacity)` | selector chrome fill | OK-OVERLAY | — |
| 31 | `borderColor: rgba(K.T.textColor, borderOpacity)` | selector border | HACK-BORDER | `K.T.separatorColor` |
| 47 | `highlightColor: rgba(K.T.highlightColor, highlightOpacity)` | hover layout fill | OK-OVERLAY | — |
| 202 | `border.color: rgba(K.T.highlightColor, expanded ? 0.3 : 0.1)` | glow ring | OK | — |
| 223 | `rgba(K.T.textColor, textSecondaryOpacity)` | header secondary text | HACK-TEXT | `K.T.disabledTextColor` |
| 344 | `rgba(K.T.textColor, textSecondaryOpacity)` | empty-state text | HACK-TEXT | `K.T.disabledTextColor` |

### SnapAssistContent.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 39 | `highlightColor: rgba(K.T.highlightColor, 0.7)` | active zone fill | OK-OVERLAY | — |
| 40 | `inactiveColor: rgba(K.T.textColor, 0.4)` | inactive zone fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` |
| 41 | `borderColor: rgba(K.T.textColor, 0.9)` | zone border | HACK-BORDER | `K.T.separatorColor` |
| 66 | `rgba(K.T.backgroundColor, 0.25)` | backdrop dim scrim | OK-OVERLAY | — |
| 103 | `rgba(zoneBg.fillColor, zoneBg.fillOpacity)` | zone fill (derived) | OK | — (inherits verdict of source; fix at 40) |
| 182 | `hovered ? rgba(K.T.highlightColor, 0.35) : rgba(K.T.backgroundColor, 0.75)` | candidate card fill | OK-OVERLAY | — (hover tint slightly >0.25 band; consider 0.25) |
| 183 | `hovered ? K.T.highlightColor : rgba(K.T.textColor, 0.5)` | candidate card border | OK (hover) + HACK-BORDER (rest) | rest → `K.T.separatorColor` |
| 223 | `K.T.textColor` | candidate caption | OK | — |

### ZoneSelectorContent.qml  (hot spot)
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 105 | `highlightColor: rgba(K.T.highlightColor, _fallbackHighlightAlpha)` | active zone fill | OK-OVERLAY | — |
| 106 | `inactiveColor: rgba(K.T.textColor, _fallbackInactiveAlpha)` | inactive zone fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` |
| 107 | `borderColor: rgba(K.T.textColor, _fallbackBorderAlpha)` | zone border | HACK-BORDER | `K.T.separatorColor` |
| 114 | `backgroundColor: K.T.backgroundColor` | panel chrome source | OK | — |
| 115 | `textColor: K.T.textColor` | text source | OK | — |
| 118 | `fadeColor: rgba(backgroundColor, fadeOpacity)` | auto-scroll edge fade | OK-OVERLAY | — |
| 482 | `rgba(K.T.backgroundColor, 0.5)` | lock overlay scrim | OK-OVERLAY | — |
| 490 | `K.T.highlightedTextColor` | lock icon on bg scrim | MISUSE-PAIRING | `K.T.textColor` (fill under it is backgroundColor, not highlight) |
| 602 | `K.T.disabledTextColor` | empty-state text | OK | — |

### LayoutOsdContent.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 75 | `backgroundColor: K.T.backgroundColor` | OSD chrome source | OK | — |
| 76 | `textColor: K.T.textColor` | OSD text source | OK | — |
| 77 | `highlightColor: K.T.highlightColor` | accent source | OK | — |
| 151 | `rgba(root.textColor, 0.08)` | preview-well fill | HACK-SURFACE | `K.T.alternateBackgroundColor` (or `Qt.alpha(K.T.backgroundColor, …)` if translucency needed) |
| 188 | `rgba(K.T.backgroundColor, 0.5)` | lock overlay scrim | OK-OVERLAY | — |
| 196 | `K.T.highlightedTextColor` | lock icon on bg scrim | MISUSE-PAIRING | `K.T.textColor` |
| 204 | `rgba(K.T.backgroundColor, 0.5)` | disabled overlay scrim | OK-OVERLAY | — |
| 212 | `K.T.neutralTextColor` | disabled icon | MISUSE-STATUS | `K.T.disabledTextColor` |

### NavigationOsdContent.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 43 | `backgroundColor: K.T.backgroundColor` | OSD chrome source | OK | — |
| 44 | `textColor: K.T.textColor` | OSD text source | OK | — |
| 45 | `highlightColor: K.T.highlightColor` | accent source | OK | — |
| 46 | `errorColor: K.T.negativeTextColor` | error indication | OK | — |

### LayoutPickerContent.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 64 | `backgroundColor: K.T.backgroundColor` | picker chrome source | OK | — |
| 65 | `textColor: K.T.textColor` | text source | OK | — |
| 66 | `highlightColor: K.T.highlightColor` | accent source | OK | — |
| 68 | `inactiveColor: rgba(K.T.textColor, 0.4)` | inactive zone fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` |
| 69 | `borderColor: rgba(K.T.textColor, 0.9)` | zone border | HACK-BORDER | `K.T.separatorColor` |
| 344 | `rgba(K.T.backgroundColor, 0.5)` | lock overlay scrim | OK-OVERLAY | — |
| 352 | `K.T.highlightedTextColor` | lock icon on bg scrim | MISUSE-PAIRING | `K.T.textColor` |

### ZoneItem.qml  (hot spot)
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 20 | `highlightColor: rgba(K.T.highlightColor, 0.7)` | active zone fill | OK-OVERLAY | — |
| 21 | `inactiveColor: rgba(K.T.textColor, 0.4)` | inactive zone fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` |
| 22 | `borderColor: rgba(K.T.textColor, 0.9)` | zone border | HACK-BORDER | `K.T.separatorColor` |
| 23 | `labelFontColor: K.T.textColor` | zone label | OK | — |
| 51 | `Qt.rgba(base…, 1.0)` | alpha-strip (opacity is sole alpha) | OK | — (shader-parity technique, documented) |
| 62 | `Qt.rgba(Math.min(1, baseColor.r*0.7 + highlightColor.r*0.3), …, baseColor.a)` | multi-zone border brighten | MISUSE-MATH | `Qt.tint(zoneItem.borderColor, Qt.alpha(zoneItem.highlightColor, 0.3))` |

### ZoneLabel.qml  (hot spot)
> **Erratum:** `ZoneLabel.qml` was DELETED on this branch (orphaned component: nothing instantiated it), so the rows below are historical.

| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 24 | `labelFontColor: K.T.textColor` | zone label text | OK | — |
| 55 | `root.labelFontColor \|\| Qt.rgba(1, 1, 1, 1)` | init-guard fallback | OK-OVERLAY | — (defensive default before C++ pushes settings) |
| 58 | `luminance > 0.5 ? Qt.rgba(K.T.backgroundColor.r*0.2, …, 0.8) : Qt.rgba(1 - K.T.backgroundColor.r*0.2, …, 0.8)` | label contrast outline over arbitrary bg | MISUSE-MATH | `luminance > 0.5 ? Qt.rgba(0,0,0,0.8) : Qt.rgba(1,1,1,0.8)` — the intent (a11y outline) is OK-OVERLAY; the bg-channel arithmetic adds nothing. Alt: `Qt.darker/lighter(K.T.backgroundColor, 5)@0.8` |

### PassiveOverlayShell.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 139 | `backgroundColor: K.T.backgroundColor` | OSD chrome source | OK | — |
| 140 | `textColor: K.T.textColor` | text source | OK | — |
| 141 | `highlightColor: K.T.highlightColor` | accent source | OK | — |
| 168 | `errorColor: K.T.negativeTextColor` | error source | OK | — |
| 315 | `highlightColor: rgba(K.T.highlightColor, 0.7)` | zone fill (snap assist slot) | OK-OVERLAY | — |
| 316 | `inactiveColor: rgba(K.T.textColor, 0.4)` | inactive zone fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` |
| 317 | `borderColor: rgba(K.T.textColor, 0.9)` | zone border | HACK-BORDER | `K.T.separatorColor` |
| 419 | `backgroundColor: K.T.backgroundColor` | picker slot chrome | OK | — |
| 420 | `textColor: K.T.textColor` | text source | OK | — |
| 421 | `highlightColor: K.T.highlightColor` | accent source | OK | — |
| 422 | `inactiveColor: K.T.disabledTextColor` | inactive indication | OK | — |
| 423 | `borderColor: K.T.textColor` | border source | HACK-BORDER | `K.T.separatorColor` |
| 434 | `labelFontColor: K.T.textColor` | zone label | OK | — |
| 601 | `highlightColor: rgba(K.T.highlightColor, 0.7)` | zone fill (overlay slot) | OK-OVERLAY | — |
| 602 | `inactiveColor: rgba(K.T.textColor, 0.4)` | inactive zone fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` |
| 603 | `borderColor: rgba(K.T.textColor, 0.9)` | zone border | HACK-BORDER | `K.T.separatorColor` |
| 610 | `backgroundColor: K.T.backgroundColor` | OSD slot chrome | OK | — |
| 611 | `textColor: K.T.textColor` | text source | OK | — |
| 770 | `highlightColor: rgba(K.T.highlightColor, 0.7)` | zone fill (render-node slot) | OK-OVERLAY | — |
| 771 | `inactiveColor: rgba(K.T.textColor, 0.4)` | inactive zone fill | HACK-SURFACE | `Qt.alpha(K.T.backgroundColor, 0.4)` |
| 772 | `borderColor: rgba(K.T.textColor, 0.9)` | zone border | HACK-BORDER | `K.T.separatorColor` |
| 773 | `labelFontColor: K.T.textColor` | zone label | OK | — |

---

## src/editor/qml/  (full-screen editor window; translucent over compositor)

### GridOverlay.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 29 | `ctx.strokeStyle = rgba(K.T.textColor, 0.3)` | canvas grid lines | HACK-BORDER | `K.T.separatorColor` |

### EditorNotifications.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 37 | `accentColor: K.T.positiveTextColor` | success notification accent | OK | — (genuine status) |
| 50 | `accentColor: K.T.negativeTextColor` | error notification accent | OK | — |

### SnapIndicator.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 89 | `Qt.rgba(K.T.highlightColor.r * 1.3, …, 1)` | snap guide line (H) | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.3)` |
| 98 | `rgba(K.T.textColor, 0.3)` | guide line shadow/halo | HACK-BORDER | `K.T.separatorColor` (or drop; the lightened line suffices) |
| 111 | `Qt.rgba(K.T.highlightColor.r * 1.5, …, 1)` | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 121 | same as 111 | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 131 | same as 111 | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 141 | same as 111 | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 151 | same as 111 | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 161 | same as 111 | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 184 | `Qt.rgba(K.T.highlightColor.r * 1.3, …, 1)` | snap guide line (V) | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.3)` |
| 193 | `rgba(K.T.textColor, 0.3)` | guide line shadow/halo | HACK-BORDER | `K.T.separatorColor` |
| 206 | `Qt.rgba(K.T.highlightColor.r * 1.5, …, 1)` | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 216 | same | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 226 | same | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 236 | same | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 246 | same | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |
| 256 | same | snap tick mark | MISUSE-MATH | `Qt.lighter(K.T.highlightColor, 1.5)` |

### DimensionTooltip.qml  (hot spot)
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 56 | `Kirigami.Theme.colorSet: Kirigami.Theme.Tooltip` | tooltip colorSet | OK | — (exemplary; the pattern other files are missing) |
| 57 | `K.T.backgroundColor` | tooltip fill | OK | — |
| 58 | `border.color: rgba(K.T.textColor, 0.15)` | tooltip border | HACK-BORDER | `K.T.separatorColor` |
| 83 | `K.T.disabledTextColor` | dimension caption | OK | — |
| 90 | `K.T.textColor` | dimension value | OK | — |
| 97 | `K.T.disabledTextColor` | dimension caption | OK | — |
| 104 | `K.T.textColor` | dimension value | OK | — |

### HelpDialogContent.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 292 | `K.T.highlightColor` | section accent bar | OK | — |
| 327 | `K.T.linkColor` | keyboard-shortcut text | MISUSE-STATUS | `K.T.textColor` (monospace already differentiates; not a link) |

### ZoneContent.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 51 | `K.T.textColor` (with `opacity: 0.8`) | zone number label | OK | — (consider disabledTextColor instead of opacity) |
| 67 | `K.T.textColor` (with `opacity: 0.6`) | zone name label | OK | — (same note) |

### CanvasMouseHandler.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 89 | `rgba(K.T.highlightColor, 0.2)` | rubber-band selection fill | OK | — |
| 90 | `border.color: K.T.highlightColor` | rubber-band border | OK | — |

### NotificationBanner.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 45 | `Theme.withAlpha(K.T.backgroundColor, Theme.panelAlpha)` | banner chrome fill | OK-OVERLAY | — (editor window is translucent over desktop) |
| 77 | `K.T.textColor` | banner text | OK | — |

### SectionHeader.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 28 | `K.T.highlightColor` | section accent bar | OK | — |

### EditorZone.qml  (hot spot)
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 446 | `Qt.rgba(r, g, b, a)` | parseColor() ARGB-hex utility | OK | — (parsing, not styling) |
| 453 | `Qt.rgba(r, g, b, a)` | parseColor() shorthand utility | OK | — |
| 466 | `useCustom ? rgba(customHighlightColor, a*customActiveOpacity) / rgba(customInactiveColor, a*customInactiveOpacity) : isSelected ? rgba(K.T.highlightColor, 0.3) : rgba(K.T.textColor, 0.15)` | zone fill (custom + theme fallback) | OK (custom branches) + HACK-SURFACE (theme inactive branch) | inactive fallback → `Qt.alpha(K.T.backgroundColor, 0.4)` or `alternateBackgroundColor`; selected `highlightColor@0.3` acceptable (selection tint, trim to 0.25) |
| 467 | `useCustom ? customBorderColor : isSelected ? K.T.highlightColor : containsMouse ? rgba(K.T.textColor, 0.5) : rgba(K.T.textColor, 0.4)` | zone border | OK (custom/selected) + HACK-BORDER (hover/rest) | hover → `K.T.hoverColor`; rest → `K.T.separatorColor` |
| 491 | `K.T.highlightColor` | multi-selection check dot | OK | — |
| 502 | `K.T.highlightedTextColor` | checkmark on highlight dot | OK | — |

### ShaderSettingsDialog.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 603 | `palette.window: K.T.backgroundColor` | palette bridge | OK | — |
| 797 | `K.T.backgroundColor` | shader preview well fill | OK | — (MISSING colorSet: use `View` for the preview well) |

### ColorPickerRow.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 58 | `border.color: K.T.disabledTextColor` | swatch border | HACK-BORDER | `K.T.separatorColor` |
| 60 | `Qt.rgba(baseColor…, baseColor.a * opacityMultiplier)` | swatch preview (user color × slider) | OK | — (user-data math, not theme fabrication) |
| 78 | `K.T.disabledTextColor` | checkerboard/placeholder tint | OK | — |

### TemplatePreview.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 38 | `var zoneColor = rgba(K.T.highlightColor, 0.6)` | template zone fill | OK | — (canvas preview; consider ≤0.25 to match selection-tint band) |
| 39 | `var borderColor = rgba(K.T.textColor, 0.8)` | template zone border | HACK-BORDER | `K.T.separatorColor` |

### TopBar.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 81 | `isActive ? K.T.highlightColor : K.T.textColor` | screen-button icon | OK | — |
| 95 | `isActive ? withAlpha(K.T.highlightColor, 0.15) : hovered ? withAlpha(K.T.textColor, 0.06) : "transparent"` | screen-button fill | OK (active) + HACK-SURFACE (hover) | hover → `Qt.alpha(K.T.hoverColor, 0.2)` |
| 97 | `isActive ? withAlpha(K.T.highlightColor, 0.4) : hovered ? withAlpha(K.T.textColor, 0.15) : "transparent"` | screen-button border | OK (active) + HACK-BORDER (hover) | hover → `K.T.separatorColor` |
| 148 | `K.T.disabledTextColor` | "Layout:" caption | OK | — |
| 154 | `rgba(K.T.neutralTextColor, 0.15)` | "Preview" mode badge fill | OK | — (preview mode is a genuine caution state; neutral fits) |
| 164 | `K.T.neutralTextColor` | "Preview" badge text | OK | — (same rationale) |
| 211 | `withAlpha(K.T.textColor, activeFocus ? 0.08 : 0.04)` | layout-name field fill | HACK-SURFACE | `K.T.alternateBackgroundColor` (colorSet View) |
| 214 | `activeFocus ? withAlpha(K.T.highlightColor, 0.4) : withAlpha(K.T.textColor, 0.08)` | field focus ring / rest border | MISUSE-FOCUS + HACK-BORDER | focus → `K.T.focusColor`; rest → `K.T.separatorColor` |
| 223 | `K.T.disabledTextColor` | field placeholder | OK | — |
| 497 | `withAlpha(K.T.backgroundColor, Theme.toolbarAlpha)` | toolbar chrome fill | OK-OVERLAY | — (MISSING colorSet: `Header` for the toolbar) |
| 504 | `withAlpha(K.T.textColor, 0.08)` | toolbar bottom separator | HACK-BORDER | `K.T.separatorColor` |

### DividerHandle.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 84 | `(hover\|\|drag) ? rgba(K.T.highlightColor, drag?0.4:0.25) : rgba(K.T.textColor, 0.08)` | divider handle fill | OK (hover/drag) + HACK-SURFACE (idle) | idle → `Qt.alpha(K.T.backgroundColor, 0.3)` or `alternateBackgroundColor` |
| 85 | `(hover\|\|drag) ? K.T.highlightColor : rgba(K.T.textColor, 0.2)` | divider handle border | OK (hover/drag) + HACK-BORDER (idle) | idle → `K.T.separatorColor` |
| 138 | `(hover\|\|drag) ? K.T.highlightColor : rgba(K.T.textColor, 0.4)` | grip dots | OK (hover) + HACK-BORDER (idle) | idle → `K.T.disabledTextColor` |
| 163 | `(hover\|\|drag) ? K.T.highlightColor : rgba(K.T.textColor, 0.3)` | grip line | OK (hover) + HACK-BORDER (idle) | idle → `K.T.separatorColor` |

### EditorWindow.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 200 | `withAlpha(K.T.highlightColor, Theme.zoneHighlightAlpha)` | zone highlight default | OK | — |
| 201 | `withAlpha(K.T.disabledTextColor, Theme.zoneInactiveAlpha)` | inactive zone fill default | OK | — (already avoids raw textColor; bg-derived would be even better) |
| 202 | `withAlpha(K.T.disabledTextColor, Theme.zoneBorderAlpha)` | zone border default | HACK-BORDER | `K.T.separatorColor` |
| 238 | `rgba(K.T.backgroundColor, 0.7)` | editor window backdrop over compositor | OK-OVERLAY | — |
| 706 | `hover ? withAlpha(K.T.highlightColor, 0.3) : withAlpha(K.T.backgroundColor, 0.9)` | exit-button fill | OK-OVERLAY | — (hover could use `hoverColor`) |
| 707 | `border.color: withAlpha(K.T.textColor, 0.2)` | exit-button border | HACK-BORDER | `K.T.separatorColor` |

### ZoneActionButton.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 47 | `withAlpha(K.T.negativeTextColor, 0.5)` | destructive-action border | OK | — (destructive = genuine negative status) |
| 49 | `hovered ? withAlpha(K.T.textColor, 0.4) : withAlpha(K.T.textColor, 0.15)` | button border | HACK-BORDER | `K.T.separatorColor`; hover → `K.T.hoverColor` |
| 54 | `withAlpha(K.T.highlightColor, 0.8)` | pressed fill | OK | — |
| 57 | `withAlpha(K.T.negativeTextColor, 0.5)` | destructive hover fill | OK | — |
| 59 | `hovered ? withAlpha(K.T.highlightColor, 0.4) : withAlpha(K.T.textColor, 0.08)` | button fill | OK (hover; consider hoverColor) + HACK-SURFACE (rest) | rest → `Qt.alpha(K.T.backgroundColor, 0.5)` or `alternateBackgroundColor` |

### ResizeHandles.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 147 | `containsMouse\|\|pressed ? K.T.highlightColor : K.T.backgroundColor` | resize handle fill | OK | — (hover branch: consider `hoverColor`) |
| 148 | `containsMouse\|\|pressed ? K.T.highlightColor : rgba(K.T.textColor, 0.6)` | resize handle border | OK (active) + HACK-BORDER (rest) | rest → `K.T.separatorColor` |
| 167 | `border.color: rgba(K.T.textColor, 0.3)` | edge handle border | HACK-BORDER | `K.T.separatorColor` |

### ControlBar.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 388 | `K.T.negativeTextColor` | "unsaved changes" icon | MISUSE-STATUS | `K.T.neutralTextColor` (unsaved = caution, not error) |
| 395 | `K.T.negativeTextColor` | "unsaved changes" text | MISUSE-STATUS | `K.T.neutralTextColor` |
| 471 | `withAlpha(K.T.backgroundColor, Theme.toolbarAlpha)` | control bar chrome fill | OK-OVERLAY | — (MISSING colorSet: `Header`) |
| 478 | `withAlpha(K.T.textColor, 0.08)` | control bar top separator | HACK-BORDER | `K.T.separatorColor` |

### PropertyPanel.qml
| Line | Expression | Role | Verdict | Replacement |
|---|---|---|---|---|
| 52 | `withAlpha(K.T.highlightColor, Theme.zoneHighlightAlpha)` | zone highlight default | OK | — |
| 53 | `withAlpha(K.T.disabledTextColor, Theme.zoneInactiveAlpha)` | inactive zone fill default | OK | — |
| 54 | `withAlpha(K.T.disabledTextColor, Theme.zoneBorderAlpha)` | zone border default | HACK-BORDER | `K.T.separatorColor` |
| 124 | `withAlpha(K.T.backgroundColor, Theme.panelAlpha)` | panel chrome fill | OK-OVERLAY | — (MISSING colorSet: `View` for the panel body) |
| 126 | `border.color: withAlpha(K.T.textColor, 0.08)` | panel border | HACK-BORDER | `K.T.separatorColor` |
| 170 | `K.T.highlightColor` | panel title accent bar | OK | — |
| 351 | `K.T.disabledTextColor` | multi-select hint text | OK | — |
| 438 | `hasError ? rgba(K.T.negativeTextColor, 0.15) : palette.base` | name-field error tint | OK | — |
| 440 | `hasError ? K.T.negativeTextColor : palette.shadow` | name-field error border | OK | — (rest branch uses palette.shadow — consider `separatorColor`) |
| 461 | `K.T.negativeTextColor` | validation error text | OK | — |
| 498 | `K.T.negativeTextColor` | validation error text | OK | — |
| 806 | `K.T.backgroundColor` | combo popup fill | OK | — |
| 807 | `border.color: rgba(K.T.textColor, 0.2)` | combo popup border | HACK-BORDER | `K.T.separatorColor` |
| 822 | `highlighted ? K.T.highlightColor : isCurrentSelection ? rgba(K.T.highlightColor, 0.15) : K.T.backgroundColor` | combo delegate fill | OK | — (textbook selection tint) |
| 827 | `highlighted ? K.T.highlightedTextColor : K.T.textColor` | combo delegate text | OK | — |

Files in scope with **zero** color findings (clean): src/shared/ShaderParamsEditor.qml, ZoneShaderRenderer.qml;
src/ui/OsdDismissable.qml, SurfaceDecoration.qml; src/editor/qml/ActionButtons.qml, AppearanceSpinBox.qml,
DividerManager.qml, EditorShortcuts.qml, KeyboardNavigation.qml, LayoutSettingsDialog.qml, OpacitySliderRow.qml,
VisibilitySettingsDialog.qml, ZoneColorDialog.qml, ZoneContextMenu.qml, ZoneDragHandler.qml, ZoneFillAnimation.qml,
ZoneGeometrySync.qml, ZoneOperations.qml (color logic lives in ColorUtils.js / ThemeHelpers.js, JS files out of QML scope;
ThemeHelpers.js `withAlpha()` call-sites are inventoried at their QML locations above).

---

## Totals per verdict

Counting rule: each table row counts once under its primary verdict; rows with a conditional split
(`OK + HACK-*` branches) are counted under the non-OK branch since that is what needs remediation.

| Verdict | Count |
|---|---|
| OK | 118 |
| OK-OVERLAY | 33 |
| HACK-SURFACE | 22 |
| HACK-BORDER | 42 |
| HACK-TEXT | 4 |
| MISUSE-STATUS | 17 |
| MISUSE-FOCUS | 1 |
| MISUSE-MATH | 17 |
| MISUSE-PAIRING | 3 |
| LITERAL | 1 |
| **Total rows** | **258** |

MISSING colorSet notes (not rows): 6 — LayoutCard.qml (View), ZonePreview.qml (View),
ShaderSettingsDialog.qml preview well (View), TopBar.qml toolbar (Header), ControlBar.qml (Header),
PropertyPanel.qml panel body (View). DimensionTooltip.qml already sets `Tooltip` correctly.

Out of scope (count only): `Phosphor.Theme` color tokens — 0 occurrences.

Deferred to a separate pass (outside assigned directories): `src/settings/qml/LayoutThumbnail.qml`,
`src/settings/qml/AlgorithmPreview.qml` (named in the prior audit's hot-spot list but not under
src/shared, src/ui, or src/editor/qml).
