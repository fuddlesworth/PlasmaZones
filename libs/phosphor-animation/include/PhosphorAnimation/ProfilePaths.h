// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>
#include <QStringList>

namespace PhosphorAnimation {

/**
 * @brief Named constants for well-known animation event paths.
 *
 * The ProfileTree uses **dot-path strings** (not an enum) so plugins and
 * shell extensions can introduce new event categories without requiring
 * a library change. This header supplies the default taxonomy used by
 * PlasmaZones + any PhosphorAnimation consumer that wants to hit the
 * same well-known paths.
 *
 * ## Taxonomy
 *
 *   global
 *     ├── window             ┐ lifecycle + geometry for individual windows
 *     │   ├── window.open
 *     │   ├── window.close
 *     │   ├── window.minimize
 *     │   ├── window.unminimize
 *     │   ├── window.maximize
 *     │   ├── window.unmaximize
 *     │   ├── window.move
 *     │   ├── window.resize
 *     │   └── window.focus
 *     │
 *     ├── zone               ┐ zone / tiling animations (PlasmaZones-native)
 *     │   ├── zone.snapIn
 *     │   ├── zone.snapOut
 *     │   ├── zone.snapResize
 *     │   ├── zone.highlight
 *     │   ├── zone.layoutSwitchIn
 *     │   └── zone.layoutSwitchOut
 *     │
 *     ├── workspace          ┐ virtual desktop / activity switching
 *     │   ├── workspace.switchIn
 *     │   ├── workspace.switchOut
 *     │   └── workspace.overview
 *     │
 *     ├── osd                ┐ on-screen display, feedback
 *     │   ├── osd.show
 *     │   ├── osd.hide
 *     │   ├── osd.pop          (scale-leg show)
 *     │   └── osd.dim
 *     │
 *     ├── panel              ┐ docks, bars, notifications
 *     │   ├── panel.slideIn
 *     │   ├── panel.slideOut
 *     │   └── panel.popup
 *     │       ├── panel.popup.zoneSelector
 *     │       │   ├── panel.popup.zoneSelector.show
 *     │       │   └── panel.popup.zoneSelector.hide
 *     │       ├── panel.popup.layoutPicker
 *     │       │   ├── panel.popup.layoutPicker.show
 *     │       │   ├── panel.popup.layoutPicker.hide
 *     │       │   └── panel.popup.layoutPicker.popIn   (scale leg)
 *     │       └── panel.popup.snapAssist
 *     │           └── panel.popup.snapAssist.show   (no .hide — destroy-on-hide)
 *     │
 *     ├── cursor             ┐ pointer / input feedback
 *     │   ├── cursor.hover
 *     │   ├── cursor.click
 *     │   └── cursor.drag    (reserved — no built-in renderer wires this today;
 *     │                       see header notes on CursorDrag)
 *     │
 *     └── shader             ┐ shader-driven composite transitions
 *         ├── shader.open
 *         ├── shader.close
 *         └── shader.switch
 *
 * ## Inheritance
 *
 * ProfileTree::resolve() walks segments right-to-left: a request for
 * `window.open` checks `window.open`, then `window`, then `global`, and
 * returns the first explicit override (or a library default).
 *
 * ## Extension
 *
 * Plugins add their own paths freely — e.g., a shell extension for
 * notifications might use `widget.toast.slideIn`, falling back through
 * `widget.toast`, `widget`, `global`. No library change required.
 */
namespace ProfilePaths {

// Root
PHOSPHORANIMATION_EXPORT extern const QString Global;

// window.*
PHOSPHORANIMATION_EXPORT extern const QString Window;
PHOSPHORANIMATION_EXPORT extern const QString WindowOpen;
PHOSPHORANIMATION_EXPORT extern const QString WindowClose;
PHOSPHORANIMATION_EXPORT extern const QString WindowMinimize;
PHOSPHORANIMATION_EXPORT extern const QString WindowUnminimize;
PHOSPHORANIMATION_EXPORT extern const QString WindowMaximize;
PHOSPHORANIMATION_EXPORT extern const QString WindowUnmaximize;
PHOSPHORANIMATION_EXPORT extern const QString WindowMove;
PHOSPHORANIMATION_EXPORT extern const QString WindowResize;
PHOSPHORANIMATION_EXPORT extern const QString WindowFocus;

// zone.*
PHOSPHORANIMATION_EXPORT extern const QString Zone;
PHOSPHORANIMATION_EXPORT extern const QString ZoneSnapIn;
PHOSPHORANIMATION_EXPORT extern const QString ZoneSnapOut;
PHOSPHORANIMATION_EXPORT extern const QString ZoneSnapResize;
PHOSPHORANIMATION_EXPORT extern const QString ZoneHighlight;
PHOSPHORANIMATION_EXPORT extern const QString ZoneLayoutSwitchIn;
PHOSPHORANIMATION_EXPORT extern const QString ZoneLayoutSwitchOut;

// workspace.*
PHOSPHORANIMATION_EXPORT extern const QString Workspace;
PHOSPHORANIMATION_EXPORT extern const QString WorkspaceSwitchIn;
PHOSPHORANIMATION_EXPORT extern const QString WorkspaceSwitchOut;
PHOSPHORANIMATION_EXPORT extern const QString WorkspaceOverview;

// osd.*
PHOSPHORANIMATION_EXPORT extern const QString Osd;
PHOSPHORANIMATION_EXPORT extern const QString OsdShow;
PHOSPHORANIMATION_EXPORT extern const QString OsdHide;
PHOSPHORANIMATION_EXPORT extern const QString OsdPop; ///< Scale-leg show profile for OSDs (data/profiles/osd.pop.json)
PHOSPHORANIMATION_EXPORT extern const QString OsdDim;

// panel.*
PHOSPHORANIMATION_EXPORT extern const QString Panel;
PHOSPHORANIMATION_EXPORT extern const QString PanelSlideIn;
PHOSPHORANIMATION_EXPORT extern const QString PanelSlideOut;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopup;
// panel.popup.* — popup-style overlay sub-paths so users can tune zone selector,
// layout picker, and snap-assist independently while still inheriting from
// panel.popup as a baseline. Setting `panel.popup` covers all three; overriding
// e.g. `panel.popup.layoutPicker` lets the picker diverge while siblings stay
// on the baseline. Walk-up inheritance handles this automatically.
//
// Per-leg `.show` / `.hide` leaves let users diverge the show- and hide-leg
// shader effects (e.g. dissolve in, slide out). Both legs walk up to the
// surface path (`panel.popup.layoutPicker`) and on to `panel.popup`, so a
// user who only wants symmetric show/hide treatment can override at the
// surface path and ignore the leaves entirely. SnapAssist exposes `.show`
// only because the surface destroys-on-hide and the hide animation never
// paints a frame.
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupZoneSelector;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupZoneSelectorShow;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupZoneSelectorHide;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupLayoutPicker;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupLayoutPickerShow;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupLayoutPickerHide;
/// Scale-leg show profile for LayoutPicker. The picker is the only
/// non-OSD surface with a scale envelope (zone selector and snap assist
/// are opacity-only); putting its scale leg under a dedicated path keeps
/// it tunable independently from the genuine OSD's `osd.pop`. Hide-leg
/// scale reuses `PanelPopupLayoutPickerHide` (same coupling pattern OSD
/// uses for `osd.hide`); a future `PopOut` split would land here.
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupLayoutPickerPopIn;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupSnapAssist;
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupSnapAssistShow;

// cursor.*
//
// `CursorDrag` is reserved in the taxonomy but currently has no built-in
// renderer. The kwin-effect's `OffscreenEffect` pipeline operates on
// **window content** — applying a shader at drag start through that path
// just shadows `window.move`'s shader (last-event-wins on the same trigger),
// not "the cursor." A future cursor-decoration / drag-shadow surface would
// give this path real semantics; until then it sits unused. Don't wire it
// to a window-content shader transition — that creates the appearance of a
// configurable feature that just collides with `window.move`.
PHOSPHORANIMATION_EXPORT extern const QString Cursor;
PHOSPHORANIMATION_EXPORT extern const QString CursorHover;
PHOSPHORANIMATION_EXPORT extern const QString CursorClick;
PHOSPHORANIMATION_EXPORT extern const QString CursorDrag;

// shader.*
PHOSPHORANIMATION_EXPORT extern const QString Shader;
PHOSPHORANIMATION_EXPORT extern const QString ShaderOpen;
PHOSPHORANIMATION_EXPORT extern const QString ShaderClose;
PHOSPHORANIMATION_EXPORT extern const QString ShaderSwitch;

// widget.*  — generic UI widget animations.
// Added in the PR-344 re-migration: the original QML call-site migration
// collapsed ~60 distinct Behavior sites onto the single "global" profile,
// destroying per-site timing + curve intent (OutBack spring on toggles,
// badge overshoot, slow needs-save tint, etc.). This branch gives each
// widget archetype its own path so library-supplied defaults can preserve
// the original motion, and users can tune individual archetypes via JSON
// files under `plasmazones/profiles/`.
PHOSPHORANIMATION_EXPORT extern const QString Widget;
PHOSPHORANIMATION_EXPORT extern const QString WidgetHover; ///< 150 ms OutCubic — card/list/row hover
PHOSPHORANIMATION_EXPORT extern const QString WidgetPress; ///< 100 ms OutCubic — button press feedback
PHOSPHORANIMATION_EXPORT extern const QString WidgetToggle; ///< 250 ms OutBack — toggle-switch knob slide (spring feel)
PHOSPHORANIMATION_EXPORT extern const QString WidgetBadge; ///< 200 ms OutBack — badge pop-in overshoot
PHOSPHORANIMATION_EXPORT extern const QString WidgetTint; ///< 300 ms Linear — slow background tint (needs-save, etc.)
PHOSPHORANIMATION_EXPORT extern const QString WidgetDim; ///< 200 ms OutCubic — generic dimension/layout change
PHOSPHORANIMATION_EXPORT extern const QString WidgetFade; ///< 150 ms OutCubic — generic opacity fade
PHOSPHORANIMATION_EXPORT extern const QString WidgetReorder; ///< 200 ms OutCubic — list reorder / drag-drop settle
PHOSPHORANIMATION_EXPORT extern const QString WidgetAccordion; ///< 250 ms OutCubic — expand/collapse section
PHOSPHORANIMATION_EXPORT extern const QString WidgetProgress; ///< 200 ms OutCubic — wizard/step progress indicators

/**
 * @brief Full list of built-in paths, in taxonomy order.
 *
 * Useful for settings UIs that want to enumerate every recognized event
 * for per-event override editing. Extension paths added at runtime via
 * ProfileTree::setOverride() are NOT included here — enumerate the tree
 * itself for those.
 *
 * **Reserved paths are excluded.** A path is "reserved" when it lives in
 * the taxonomy but has no consumer that fires it (today: `cursor.drag`
 * and `zone.layoutSwitchOut`). A settings UI surfacing a reserved path
 * would let the user assign a shader to it that never plays — confusing
 * UX. Use `isReservedPath()` to detect these explicitly if a UI needs to
 * include them with an "(unimplemented)" annotation.
 */
PHOSPHORANIMATION_EXPORT QStringList allBuiltInPaths();

/**
 * @brief True when @p path is in the taxonomy but has no built-in producer.
 *
 * Reserved paths exist as named slots so plugins / future PlasmaZones
 * versions can wire them without a library change, but no consumer in
 * the current build fires them. `allBuiltInPaths()` filters these out
 * so settings UIs don't surface assignable slots that silently no-op.
 *
 * Currently reserved:
 *   - `cursor.drag` — the kwin-effect's previous wire-up was
 *     guaranteed-shadowed by `window.move` (last-event-wins on the same
 *     window), so it never rendered. Reserved for a future
 *     cursor-decoration / drag-shadow surface.
 *   - `zone.layoutSwitchOut` — the symmetric counterpart of
 *     `zone.layoutSwitchIn`. The QML-side flash and C++-side resnap
 *     shader both consume `zone.layoutSwitchIn`; the outgoing direction
 *     has no distinct consumer today.
 */
PHOSPHORANIMATION_EXPORT bool isReservedPath(const QString& path);

/**
 * @brief Walk @p path up one level.
 *
 * Examples:
 *   - `"window.open"`  → `"window"`
 *   - `"window"`       → `"global"`
 *   - `"global"`       → `""` (empty string — no parent)
 *   - `""`             → `""`
 *
 * Used by ProfileTree::resolve() to implement inheritance; exposed here
 * because shell extensions sometimes need to construct inheritance
 * chains themselves (e.g., for settings-UI path pickers).
 */
PHOSPHORANIMATION_EXPORT QString parentPath(const QString& path);

} // namespace ProfilePaths

} // namespace PhosphorAnimation
