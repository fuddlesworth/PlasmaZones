# Kirigami Color Usage — Inventory & Remediation Map

Generated 2026-07-16 from a full scan of the QML tree (`src/settings/qml`,
`src/shared`, `src/ui`, `src/editor/qml`, `libs/phosphor-control`).
**Scope note:** the shell libraries (`phosphor-shell*`, `phosphor-popout`,
`phosphor-theme`) are deliberately excluded; `phosphor-control` is included
because it paints the settings app's own sidebar/footer chrome with
Kirigami.Theme.

Per-file detail tables (Line | Expression | Role | Verdict | Replacement):

- [settings A–L](color-map/settings-a-l.md)
- [settings M–Z](color-map/settings-m-z.md)
- [shared / ui / editor](color-map/shared-ui-editor.md)
- [phosphor libs (boundary; mostly out of scope)](color-map/phosphor-libs.md)

## Why this exists

The app fabricates most surfaces by compositing `Kirigami.Theme.textColor`
at low alpha over the window background (`Qt.rgba(textColor, 0.03–0.4)`).
On a dark scheme textColor is near-white, so every card, tile, and inactive
zone renders as flat grey-slate — the wallpaper-generated theme colors
cannot reach them. Separately, focus/hover ring code assumes
highlight/focus/hover share a hue (they deliberately don't in the desktop
scheme), and status colors (active/positive/neutral) are used as a
decorative palette.

## The ruleset (what each thing SHOULD be)

| If the code paints… | Use | Never |
|---|---|---|
| a card/tile/list surface | `Kirigami.Theme.backgroundColor` / `alternateBackgroundColor` under `Kirigami.Theme.colorSet: Kirigami.Theme.View` | `Qt.rgba(textColor, α)` |
| a subtle border / separator | `Kirigami.ColorUtils.linearInterpolation(backgroundColor, textColor, frameContrast)` — **`Kirigami.Theme.separatorColor` DOES NOT EXIST** (verified against installed Kirigami; it evaluates undefined and broke 84 call sites before being swept) | `Qt.rgba(textColor, 0.08–0.3)` |
| a strong border / stroke | `alternateBackgroundColor` | `Qt.rgba(textColor, 0.9)` |
| a selection tint / selected fill | `highlightColor` (full or @α ≤ 0.25) | — (this is already mostly right) |
| text on a highlight fill | `highlightedTextColor` | plain `textColor` |
| a keyboard-focus ring | `Kirigami.Theme.focusColor` | `highlightColor` |
| a hover affordance | `Kirigami.Theme.hoverColor` | `highlightColor` |
| de-emphasized text | `disabledTextColor` | `textColor` @ α |
| success / warning / error / attention **status** | `positive` / `neutral` / `negative` / `activeTextColor` | using these as badge/category decoration |
| a decorative badge/category tint | `textColor` or `highlightColor` @ α | status colors |
| brighten/darken a theme color | `Qt.lighter()` / `Qt.darker()` / `Qt.tint()` | per-channel `Qt.rgba(c.r*k, …)` |
| toolbars / page headers / sidebar chrome | declare `colorSet: Kirigami.Theme.Header` | inheriting Window silently |
| tooltips / toasts | `colorSet: Kirigami.Theme.Tooltip` | hand-inverted colors |
| an overlay scrim over arbitrary desktop content (compositor OSDs) | self-contained `Qt.rgba(0,0,0,α)` is sanctioned (`OK-OVERLAY`) | textColor-derived scrims (invert on light themes) |

KDE→Kirigami source mapping for reference: `highlightColor` ←
`[Colors:Selection] BackgroundNormal`; `focusColor`/`hoverColor` ←
`DecorationFocus`/`DecorationHover`; `activeTextColor` ← `ForegroundActive`
(**attention** semantics, not "active item"); Header colorSet ←
`[Colors:Header]`. In the wallpaper-generated scheme, Selection+Header carry
the wallpaper's second hue, Decoration* the primary hue, active/positive the
tertiary accent — which is exactly why the misuses above render in
"wrong-looking" colors.

## Grand totals

| Verdict | Count |
|---|---|
| OK (semantically correct) | 287 |
| OK-OVERLAY (sanctioned compositor-overlay scrims) | 33 |
| HACK — fabricated surface | 56 |
| HACK — fabricated border | 83 |
| HACK — other (scrim / alpha-text / hand-inverted toast) | 16 |
| MISUSE — focus/hover drawn with highlightColor | 24 |
| MISUSE — status color as decoration | 30 |
| MISUSE — per-channel math | 17 |
| MISUSE — wrong foreground on (non-)highlight fill | 6 |
| LITERAL (`"white"` lock icon, LayoutPreview.qml:214) | 1 |
| **Total classified expressions** | **553** |
| MISSING `colorSet` declarations (structural) | 22 |

## Ranked fix plan (leverage first)

1. **`SettingsCard.qml` (~205–247)** — the fabricated-surface template other
   cards copy. Fix here + `colorSet: View`; many cards inherit visually.
2. **The copy-pasted zone-fill trio** (`highlightColor@0.7` active /
   `textColor@0.4` inactive fill / `textColor@0.9` border) appears in **8
   overlay files** (ZoneItem, ZonePreview, RenderNodeOverlay(+Content),
   ZoneOverlayContent, SnapAssistContent, ZoneSelectorContent,
   LayoutPickerContent, PassiveOverlayShell ×3). Centralize the defaults
   (shared component or singleton) and fix once — resolves ~24 rows.
3. **`WizardUtils.js wizardColors()`** — one function feeds
   WizardConfigCard/WizardPreviewFrame/WizardTemplateCard.
4. **`Main.qml` header slots (156–167) + sidebar delegate (1074)** →
   `colorSet: Header` (plus TopBar.qml / ControlBar.qml in the editor).
   This is where the scheme's second wallpaper hue finally appears.
5. **Mechanical focus/hover swaps** — 16 focus rings → `focusColor`,
   8 hovers → `hoverColor` (see fragment tables; all one-line).
6. **`SnapIndicator.qml`** — 14 of the 17 per-channel-math rows in one file
   → `Qt.lighter(highlightColor, k)`.
7. **Badges** (CategoryBadge, AspectRatioBadge, CapabilityBadgeRow,
   DisplayMap "Primary", ZonePreview master dot) — status colors →
   neutral chrome tints.
8. **Toast.qml** → `colorSet: Tooltip` instead of hand-inverted colors.
9. Remaining fabricated borders → `separatorColor` (bulk, low risk).

## In-tree exemplars (copy these)

- `EasingPreview.qml:335` / `SpringPreview.qml:106` — correct `View` colorSet
- `ShaderBrowserCard.qml:98`, `ParameterRow.qml:343` — correct `focusColor`
- `DimensionTooltip.qml:56` — correct `Tooltip` colorSet
- `UnsavedChangesFooter.qml:45` — correct `Window` pin with `inherit: false`
- `ParameterSection.qml:184` — the one correct `hoverColor`

## Post-fix findings (discovered after the QML pass)

- **RETIRED: the settings app's hand-rolled `org.plasmazones.common` copy.**
  It used to embed hand-listed .qml files + a hand-written qmldir "to stay
  standalone" — a non-reason, since the module is STATIC and linking the
  generated plugin is equally standalone. The drift broke
  LayoutThumbnail→LayoutCard ("X is not a type" only in settings, while the
  daemon and tests worked). Now settings links
  `plasmazones_shared_qmlplugin` + `Q_IMPORT_QML_PLUGIN` exactly like the
  daemon; the hand qmldir is deleted and the bug class is gone. Two hard-won
  build notes: `Q_IMPORT_QML_PLUGIN` must sit at FILE scope (inside the
  file's anonymous namespace, the extern symbol binds internally →
  undefined reference), and the importing .cpp must be excluded from unity
  builds (`SKIP_UNITY_BUILD_INCLUSION`). Diagnosis trick that found it all:
  `dbus-run-session -- env QT_QPA_PLATFORM=offscreen
  ./build/bin/plasmazones-settings` bypasses the single-instance guard.

- **`Kirigami.Theme.separatorColor` is not a real property.** The original
  inventory saw two "uses" and every remediation pass propagated it (84
  occurrences) — all evaluated to `undefined` at runtime. Verified against
  the installed Kirigami with a runtime probe: the ONLY fake property among
  the 21 the remediation used. Canonical replacement (what Kirigami itself
  does): `Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
  Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)`. LESSON: verify
  theme API against the runtime before prescribing it in a ruleset.

- **The zone-color pipeline overrides QML theming.** Zone fills/borders are
  user-configurable Settings (`useSystemColors=true` by default →
  `Settings::applySystemColorScheme()` maps QPalette into the settings
  colors, which the daemon cascades into every overlay/popup slot —
  `overlay_data.cpp`: zone-custom → rule override → global). QML-side
  defaults (`ZoneColorDefaults`) only apply where the plumbing doesn't
  push. `applySystemColorScheme()` originally mapped inactive fill AND
  border to `QPalette::Text` @ alpha — the same textColor fabrication,
  in C++. Fixed to `AlternateBase` / `Mid`. Any future zone-color work
  must start at this function, not in QML.
- **ZoneColorDefaults flavor rule:** panel-hosted zone cards (PopupFrame
  contents, settings previews) use the opaque `preview*` flavor; only
  zones drawn naked over the desktop use the alpha overlay flavor.

## Judgment calls recorded

- `InputCapture.qml:198` `Qt.rgba(0,0,0,0.4)` — kept OK (standard
  Overlay.modal dim convention).
- `AlgorithmPreview.qml:34/114` — marked HACK but may be intentional preview
  fidelity (mirrors the overlay's user-configurable border); decide at fix
  time.
- `KeyboardShortcutOverlay.qml:37` — `textColor@0.6` scrim goes *light* on
  dark themes; replace with black scrim or backgroundColor-derived dim.
