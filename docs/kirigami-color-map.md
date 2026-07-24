<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Kirigami Color Usage — Ruleset

Which Kirigami theme role to reach for when painting QML in `src/settings/qml`,
`src/shared`, `src/ui`, `src/editor/qml`, and `libs/phosphor-control`. The shell
libraries (`phosphor-shell*`, `phosphor-popout`, `phosphor-theme`) have their own
token system and are out of scope; `phosphor-control` is in scope because it
paints the settings app's own sidebar and footer chrome with `Kirigami.Theme`.

This started as a remediation map for a full-tree colour audit. The audit and its
fixes landed on `fix/theme-color-pipeline`; the per-file inventory tables have
been dropped as a spent pre-remediation snapshot. What survives below is the
guidance that still governs new code.

## Why this exists

The app used to fabricate most surfaces by compositing `Kirigami.Theme.textColor`
at low alpha over the window background (`Qt.rgba(textColor, 0.03–0.4)`).
On a dark scheme textColor is near-white, so every card, tile, and inactive
zone rendered as flat grey-slate and the wallpaper-generated theme colors
could not reach them. Separately, focus/hover ring code assumed
highlight/focus/hover share a hue (they deliberately don't in the desktop
scheme), and status colors (active/positive/neutral) were used as a
decorative palette.

## The ruleset (what each thing SHOULD be)

| If the code paints… | Use | Never |
|---|---|---|
| a card/tile/list surface | `Kirigami.Theme.backgroundColor` / `alternateBackgroundColor` under `Kirigami.Theme.colorSet: Kirigami.Theme.View` | `Qt.rgba(textColor, α)` |
| a subtle border / separator | `Kirigami.ColorUtils.linearInterpolation(backgroundColor, textColor, frameContrast)` — **`Kirigami.Theme.separatorColor` DOES NOT EXIST** (verified against installed Kirigami; it evaluates undefined and broke 84 call sites before being swept) | `Qt.rgba(textColor, 0.08–0.3)` |
| a strong border / stroke | `alternateBackgroundColor` | `Qt.rgba(textColor, 0.9)` |
| a selection tint / selected fill | `highlightColor` (full or @α ≤ 0.25) | — |
| text on a highlight fill | `highlightedTextColor` | plain `textColor` |
| a keyboard-focus ring | `Kirigami.Theme.focusColor` | `highlightColor` |
| a hover affordance | `Kirigami.Theme.hoverColor` | `highlightColor` |
| de-emphasized text | `disabledTextColor` | `textColor` @ α |
| success / warning / error / attention **status** | `positive` / `neutral` / `negative` / `activeTextColor` | using these as badge/category decoration |
| a decorative badge/category tint | `textColor` or `highlightColor` @ α | status colors |
| brighten/darken a theme color | `Qt.lighter()` / `Qt.darker()` / `Qt.tint()` | per-channel `Qt.rgba(c.r*k, …)` |
| toolbars / page headers / sidebar chrome | declare `colorSet: Kirigami.Theme.Header` | inheriting Window silently |
| tooltips / toasts | `colorSet: Kirigami.Theme.Tooltip` | hand-inverted colors |
| an overlay scrim over arbitrary desktop content (compositor OSDs) | self-contained `Qt.rgba(0,0,0,α)` is sanctioned | textColor-derived scrims (invert on light themes) |

KDE→Kirigami source mapping for reference: `highlightColor` ←
`[Colors:Selection] BackgroundNormal`; `focusColor`/`hoverColor` ←
`DecorationFocus`/`DecorationHover`; `activeTextColor` ← `ForegroundActive`
(**attention** semantics, not "active item"); Header colorSet ←
`[Colors:Header]`. In the wallpaper-generated scheme, Selection+Header carry
the wallpaper's second hue, Decoration* the primary hue, active/positive the
tertiary accent, which is exactly why the misuses above render in
"wrong-looking" colors.

## In-tree exemplars (copy these)

- `EasingPreview.qml` / `SpringPreview.qml` — correct `View` colorSet
- `ShaderBrowserCard.qml`, `ParameterRow.qml` — correct `focusColor`
- `DimensionTooltip.qml` — correct `Tooltip` colorSet
- `UnsavedChangesFooter.qml` — correct `Window` pin with `inherit: false`

## Standing findings

- **`Kirigami.Theme.separatorColor` is not a real property.** The original
  inventory saw two "uses" and every remediation pass propagated it (84
  occurrences), all of which evaluated to `undefined` at runtime. Verified
  against the installed Kirigami with a runtime probe: the ONLY fake property
  among the 21 the remediation used. Canonical replacement (what Kirigami
  itself does): `Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
  Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)`. LESSON: verify
  theme API against the runtime before prescribing it in a ruleset.

- **The zone-color pipeline overrides QML theming.** Zone fills and borders are
  user-configurable Settings (`useSystemColors=true` by default →
  `Settings::applySystemColorScheme()` maps QPalette into the settings
  colors, which the daemon cascades into every overlay/popup slot —
  `overlay_data.cpp`: zone-custom → rule override → global). QML-side
  defaults (`ZoneColorDefaults`) only apply where the plumbing doesn't
  push. `applySystemColorScheme()` originally mapped inactive fill AND
  border to `QPalette::Text` @ alpha, the same textColor fabrication
  expressed in C++. It now uses `AlternateBase` / `Mid`. Any future
  zone-color work must start at this function, not in QML.

- **ZoneColorDefaults flavor rule:** panel-hosted zone cards (PopupFrame
  contents, settings previews) use the opaque `preview*` flavor; only
  zones drawn naked over the desktop use the alpha overlay flavor.

- **The settings app links the shared QML plugin.** It used to embed a
  hand-listed copy of `org.plasmazones.common` with a hand-written qmldir,
  which drifted and broke LayoutThumbnail→LayoutCard ("X is not a type" only
  in settings, while the daemon and tests worked). Settings now links
  `plasmazones_shared_qmlplugin` + `Q_IMPORT_QML_PLUGIN` exactly like the
  daemon. Two build notes: `Q_IMPORT_QML_PLUGIN` must sit at FILE scope
  (inside an anonymous namespace the extern symbol binds internally →
  undefined reference), and the importing .cpp must be excluded from unity
  builds (`SKIP_UNITY_BUILD_INCLUSION`). To debug the settings app past its
  single-instance guard: `dbus-run-session -- env QT_QPA_PLATFORM=offscreen
  ./build/bin/plasmazones-settings`.
