// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>
#include <QStringList>

namespace PhosphorAnimation {

/// Dot-path constants for well-known animation events.
/// ProfileTree::resolve() walks segments right-to-left for inheritance.
/// Plugins add paths freely (e.g. "widget.toast.slideIn") without library changes.
///
/// Naming convention (apply to new paths):
///   show / hide                 — ephemeral surfaces (osd, popup, badge)
///   open / close                — persistent surfaces with a stateful open/closed
///   <verb>In / <verb>Out        — directional motion (slideIn, snapIn,
///                                 fadeIn …); `switch`, `layoutSwitch` and
///                                 `peek` are bidirectional-leg exceptions
///                                 with no In/Out suffix, and `view`
///                                 (scrolling.view) is a continuous-motion
///                                 exception with no legs to name at all
///   expand / collapse           — size reveal of inline content (accordion)
///   on / off                    — bistable controls (toggle)
///   <event>.<variant>           — speed/intensity variants (pulse.fast, tint.fast)
namespace ProfilePaths {

// Root
PHOSPHORANIMATION_EXPORT extern const QString Global;

// window.* — runtime window-lifecycle animations driven by the
// kwin-effect's OffscreenEffect via tryBeginShaderForEvent. The
// snap/layout-switch leaves are window events triggered by zone
// interaction (the WINDOW animates when it snaps into/out of a zone
// or when a layout switch repositions it).
PHOSPHORANIMATION_EXPORT extern const QString Window;
// window.appearance.* — a window surface materialising / dissolving (the
// appearance shader contract). WindowAppearance is the cascade parent.
PHOSPHORANIMATION_EXPORT extern const QString WindowAppearance;
PHOSPHORANIMATION_EXPORT extern const QString WindowOpen;
PHOSPHORANIMATION_EXPORT extern const QString WindowClose;
PHOSPHORANIMATION_EXPORT extern const QString WindowMinimize;
PHOSPHORANIMATION_EXPORT extern const QString WindowFocus;
// window.movement.* — a window changing geometry, old-rect → new-rect (the
// geometry-morph shader contract). WindowMovement is the cascade parent.
// WindowMove is the exception: the held interactive drag, its own opt-in
// `move` class (see EventClassMove below). There are NO resize legs — the
// interactive edge-drag resize and the never-routed snapResize were dropped
// (see the rationale note in profilepaths.cpp); discrete resizes are
// covered by snapIn / layoutSwitch / maximize.
PHOSPHORANIMATION_EXPORT extern const QString WindowMovement;
PHOSPHORANIMATION_EXPORT extern const QString WindowMaximize;
PHOSPHORANIMATION_EXPORT extern const QString WindowMove;
PHOSPHORANIMATION_EXPORT extern const QString WindowSnapIn;
PHOSPHORANIMATION_EXPORT extern const QString WindowSnapOut;
PHOSPHORANIMATION_EXPORT extern const QString WindowLayoutSwitch;

// desktop.* — full-screen two-texture transitions driven by the kwin-effect's
// screen-level paint pass. Unlike the per-window window.* events, these blend
// two full-screen scene captures, so they use the desktop event class and its
// own two-texture shader contract rather than the single-surface pipeline.
// `switch` blends the OUTGOING desktop against the INCOMING desktop; `peek`
// (show desktop) blends the windows scene against the bare desktop, and its
// show-back leg reuses the same node, running the same blend with time
// reversed so an asymmetric pack retraces its own motion.
PHOSPHORANIMATION_EXPORT extern const QString Desktop;
PHOSPHORANIMATION_EXPORT extern const QString DesktopSwitch;
PHOSPHORANIMATION_EXPORT extern const QString DesktopPeek;

// editor.* — Layout-editor-only zone manipulation animations
// (fill-preview, drag-resize-preview). NOT triggered by runtime
// window snapping — window-snap animations are KWin's
// compositor-level domain. These paths only fire inside the
// Phosphor layout editor.
PHOSPHORANIMATION_EXPORT extern const QString Editor;
PHOSPHORANIMATION_EXPORT extern const QString EditorSnapIn;
PHOSPHORANIMATION_EXPORT extern const QString EditorSnapOut;
PHOSPHORANIMATION_EXPORT extern const QString EditorSnapResize;

// scrolling.* — the scrolling strip's VIEW. `scrolling.view` is one leg for
// the whole strip: the compositor springs it once per output and every column
// rides that offset, rather than each column springing itself. It is its own
// root rather than a window.movement.* leaf because its subject is the view
// and not any window.
// `scrolling.tabSwitch` shares the root for grouping only: its subject IS a
// window (the tab arriving in the rect the outgoing one vacated), so it
// carries the APPEARANCE class, not the strip class its siblings do.
PHOSPHORANIMATION_EXPORT extern const QString Scrolling;
PHOSPHORANIMATION_EXPORT extern const QString ScrollingView;
PHOSPHORANIMATION_EXPORT extern const QString ScrollingTabSwitch;

// osd.*
PHOSPHORANIMATION_EXPORT extern const QString Osd;
PHOSPHORANIMATION_EXPORT extern const QString OsdShow;
PHOSPHORANIMATION_EXPORT extern const QString OsdPop;
PHOSPHORANIMATION_EXPORT extern const QString OsdHide;

// popup.* — transient overlays invoked by user action.
// Per-leg .show/.hide leaves let show/hide shader effects diverge.
PHOSPHORANIMATION_EXPORT extern const QString Popup;
PHOSPHORANIMATION_EXPORT extern const QString PopupZoneSelector;
PHOSPHORANIMATION_EXPORT extern const QString PopupZoneSelectorShow;
PHOSPHORANIMATION_EXPORT extern const QString PopupZoneSelectorHide;
PHOSPHORANIMATION_EXPORT extern const QString PopupLayoutPicker;
PHOSPHORANIMATION_EXPORT extern const QString PopupLayoutPickerShow;
PHOSPHORANIMATION_EXPORT extern const QString PopupLayoutPickerHide;
PHOSPHORANIMATION_EXPORT extern const QString PopupSnapAssist;
PHOSPHORANIMATION_EXPORT extern const QString PopupSnapAssistShow;
PHOSPHORANIMATION_EXPORT extern const QString PopupSnapAssistHide;
PHOSPHORANIMATION_EXPORT extern const QString PopupCheatsheet;
PHOSPHORANIMATION_EXPORT extern const QString PopupCheatsheetShow;
PHOSPHORANIMATION_EXPORT extern const QString PopupCheatsheetHide;

// panel.* — persistent in-app side surfaces (settings nav rail, editor
// property panel). Absorbs the former sidebar.* root — sidebars are panels.
PHOSPHORANIMATION_EXPORT extern const QString Panel;
PHOSPHORANIMATION_EXPORT extern const QString PanelSlideIn;
PHOSPHORANIMATION_EXPORT extern const QString PanelSlideOut;
PHOSPHORANIMATION_EXPORT extern const QString PanelFadeIn;
PHOSPHORANIMATION_EXPORT extern const QString PanelFadeOut;

// cursor.*
PHOSPHORANIMATION_EXPORT extern const QString Cursor;
PHOSPHORANIMATION_EXPORT extern const QString CursorHover;
PHOSPHORANIMATION_EXPORT extern const QString CursorClick;

// shader.*
PHOSPHORANIMATION_EXPORT extern const QString Shader;
PHOSPHORANIMATION_EXPORT extern const QString ShaderOpen;
PHOSPHORANIMATION_EXPORT extern const QString ShaderClose;
PHOSPHORANIMATION_EXPORT extern const QString ShaderSwitch;

// widget.* — per-archetype paths so library defaults preserve original motion.
PHOSPHORANIMATION_EXPORT extern const QString Widget;
PHOSPHORANIMATION_EXPORT extern const QString WidgetHover; ///< 150 ms OutCubic (family seed)
PHOSPHORANIMATION_EXPORT extern const QString WidgetPress; ///< 100 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetDim; ///< 200 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetTint; ///< 300 ms widget-out (family root)
PHOSPHORANIMATION_EXPORT extern const QString WidgetTintFast; ///< 120 ms (variant)
PHOSPHORANIMATION_EXPORT extern const QString WidgetToggleOn; ///< 250 ms OutBack (spring feel)
PHOSPHORANIMATION_EXPORT extern const QString WidgetToggleOff; ///< 250 ms OutBack
PHOSPHORANIMATION_EXPORT extern const QString WidgetBadgeShow; ///< 200 ms OutBack
PHOSPHORANIMATION_EXPORT extern const QString WidgetBadgeHide; ///< 150 ms InCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetBadgePulse; ///< 400 ms count-change pulse
PHOSPHORANIMATION_EXPORT extern const QString WidgetAccordionExpand; ///< 250 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetAccordionCollapse; ///< 180 ms InCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetFadeIn; ///< 200 ms OutCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetFadeOut; ///< 400 ms InCubic
PHOSPHORANIMATION_EXPORT extern const QString WidgetReorder; ///< 150 ms OutCubic (family seed)
PHOSPHORANIMATION_EXPORT extern const QString WidgetProgress; ///< 150 ms OutCubic (family seed)
PHOSPHORANIMATION_EXPORT extern const QString WidgetPulse; ///< 1000 ms sinusoidal (family root)
PHOSPHORANIMATION_EXPORT extern const QString WidgetPulseFast; ///< 500 ms
PHOSPHORANIMATION_EXPORT extern const QString WidgetPulseSlow; ///< 1500 ms
// Zone-rect widget (used by ZoneItem.qml,
// ZonePreview.qml — i.e. the reusable QML zone-rectangle that gets
// embedded in the runtime overlay, settings dialogs, layout
// thumbnails, etc.). The animation lives with the widget; the
// surface it's hosted on is incidental.
PHOSPHORANIMATION_EXPORT extern const QString WidgetZoneHighlight;
PHOSPHORANIMATION_EXPORT extern const QString WidgetZoneHighlightPop;
PHOSPHORANIMATION_EXPORT extern const QString WidgetZoneHighlightBorder;
// One-shot flash on the main zone-overlay surface when the active
// layout changes mid-drag (ZoneOverlayContent.qml). A widget-level
// content effect on the overlay, not a per-zone animation.
PHOSPHORANIMATION_EXPORT extern const QString WidgetZoneOverlayFlash;

// ── Event classes ───────────────────────────────────────────────────────
// A coarse capability axis layered over the path taxonomy. Two of the five
// classes are the general ones: an animation either reshapes a window's
// GEOMETRY (it has a before-rect and an after-rect) or it changes a
// surface's APPEARANCE (a single surface fading / scaling / glitching in or
// out). A geometry-only shader such as window-morph cross-fades
// `iFromRect → iToRect` and is a silent no-op on an appearance event, so a
// shader declares which classes it supports (AnimationShaderEffect::appliesTo)
// and the settings UI filters the rows it can't drive. The other three —
// DESKTOP, MOVE and STRIP — each name a distinct uniform contract a pack must
// opt into explicitly, and a universal (empty `appliesTo`) pack never reaches
// them. These string tokens are the SSOT for that vocabulary — matched
// verbatim against `appliesTo` entries and `eventClassForPath`.
//
// Adding a class token? It is not enough to declare it here. Every one of
// these must be updated too, and the last three have each already shipped a
// bug from being missed:
//   1. `allEventClassTokens()` below — the exported vocabulary. `fromJson`
//      (animationshadereffect.cpp) validates `appliesTo` against it and the
//      pack validator's lint lists it in the diagnostic, so both follow for
//      free once it is here.
//   2. `shaderEffectAppliesToEventPath` (AnimationShaderEffect.h) — an
//      opt-in class needs its own branch plus an exclusion in the
//      ambiguous-row fallback, or it silently behaves as universal.
//   3. `shaderEffectIsCompositorOnly` (AnimationShaderEffect.h) hardcodes
//      "appearance is the only class that reaches a daemon surface" — a new
//      daemon-driven class must be added there, or its packs are silently
//      classified compositor-only and skipped by the daemon.
//   4. `_typeCatalog` in ShaderBrowserPage.qml — the browser's type axis.
//      A missing entry ships an untranslated badge sorted last.
//   5. `coverageLabel` in AnimationsMotionSetsPage.qml — the coverage chip.
//      A missing case ships the raw lowercase token.

/// Geometry transitions: snapIn/snapOut, layoutSwitch, maximize — every leg
/// that carries an old and new rect.
PHOSPHORANIMATION_EXPORT extern const QString EventClassGeometry;

/// Appearance transitions: open, close, minimize, focus, and every OSD /
/// popup show/hide — a single surface materialising or dissolving.
PHOSPHORANIMATION_EXPORT extern const QString EventClassAppearance;

/// Desktop transitions: full-screen blends of two scene captures — the
/// virtual-desktop switch (outgoing desktop against incoming one) and the
/// show-desktop peek (windows scene against bare desktop). A distinct
/// TWO-texture contract (from/to full-screen samplers), incompatible with the
/// single-surface geometry/appearance shaders — a shader must opt into it
/// explicitly via `appliesTo: ["desktop"]`. A universal single-surface effect
/// (empty `appliesTo`) does NOT apply to desktop paths, because its lone
/// surface sampler would be unbound in the two-texture pass.
PHOSPHORANIMATION_EXPORT extern const QString EventClassDesktop;

/// Interactive-drag transitions: the `window.movement.move` leaf only. A drag
/// installs a HELD transition — no old→new crossfade plays (iFromRect stays
/// invalid, progress clamps while the pointer is down), so a geometry
/// crossfade pack is a guaranteed no-op there. Only a position / mesh backed
/// pack consuming the move-physics inputs (iMoveMesh / iMoveOffset /
/// iMoveVelocity* / iMoveTrail) can drive it, and it must opt in explicitly
/// via `appliesTo: ["move"]` (wobble). Like `desktop`, this class is opt-in
/// rather than universal-permissive, and the move leaf takes NO inherited
/// shader from its ancestors (see ShaderProfileTree::resolve).
PHOSPHORANIMATION_EXPORT extern const QString EventClassMove;

/// Strip transitions: the scrolling strip's view leg (`scrolling.view`). Like
/// `move`, the motion is CONTINUOUS — wheel scrolling retargets the per-output
/// view spring on every batch, so there are no discrete from/to legs and a
/// crossfade pack has nothing to play. Like `desktop`, the pass is per-output
/// and full-screen: the compositor renders the already-translated scene into
/// one capture and the pack decorates it (motion blur, smear, edge warp)
/// driven by offset/velocity uniforms (iStripMotion), converging to the
/// identity image at settle. A distinct one-scene-sampler contract
/// (strip_transition.glsl), incompatible with the single-surface and
/// two-texture pipelines — a shader must opt in explicitly via
/// `appliesTo: ["strip"]`, and a universal effect does NOT apply here.
/// A strip-ONLY pack is therefore compositor-only, which is exactly what
/// `shaderEffectIsCompositorOnly`'s appearance rule concludes. That is a
/// property of the pack, not of the class: a hybrid `["strip", "appearance"]`
/// pack is still daemon-routable through its appearance leg.
PHOSPHORANIMATION_EXPORT extern const QString EventClassStrip;

/// Tab transitions: the swap inside a tabbed scrolling column
/// (`scrolling.tabSwitch`). TWO textures like `desktop`, but on a window quad
/// rather than a screen: the outgoing tab's captured content is bound as
/// `uOldWindow` (the same shared old-content sampler the geometry crossfades
/// use) and the pack blends it into the arriving tab's live surface over a
/// discrete forward leg.
///
/// Opt-in rather than universal-permissive, for a reason of its own. A
/// universal single-surface pack would not fail here the way it does on a
/// desktop path — its sampler IS bound — it would simply fade the arriving tab
/// in over whatever lies behind the column, which is the wallpaper. That reads
/// as a flash of desktop between two windows that never moved, so a pack must
/// declare `appliesTo: ["tab"]` to be offered.
///
/// A tab-ONLY pack is compositor-only by `shaderEffectIsCompositorOnly`'s
/// appearance rule, which is what lets it include `old_content.glsl`
/// unguarded: that sampler is binding-less and the daemon's strict SPIR-V bake
/// rejects it, so a pack reaching for the outgoing tab must never be
/// daemon-routable.
PHOSPHORANIMATION_EXPORT extern const QString EventClassTab;

/// Every event-class token, in the order the classes are declared above.
/// This is the vocabulary `AnimationShaderEffect::appliesTo` is validated
/// against and the one the pack validator lints and names in its diagnostic.
/// Consume it rather than re-spelling the tokens; a hand-maintained copy is
/// how a new class ends up accepted by one consumer and rejected by another.
PHOSPHORANIMATION_EXPORT QStringList allEventClassTokens();

/// Classify @p path into an event class, or empty string when the path has
/// no single class (a mixed ancestor like `window`, or a path outside the
/// classified families — editor / panel / widget / cursor / shader / global).
/// Resolution is leaf-aware: the OSD and popup roots and all their descendants
/// are `appearance`; the window leaves split by motion-vs-lifecycle; the
/// `window.movement.move` leaf is `move` (held interactive drag) while the
/// rest of the movement sub-tree is `geometry`; the `desktop` root and every
/// `desktop.*` leaf are `desktop`; the `scrolling` root and every
/// `scrolling.*` leaf are `strip`; the `window` root itself is mixed → empty.
PHOSPHORANIMATION_EXPORT QString eventClassForPath(const QString& path);

/// Full list of built-in paths in taxonomy order.
PHOSPHORANIMATION_EXPORT QStringList allBuiltInPaths();

/// Walk @p path up one level
/// ("window.appearance.open" -> "window.appearance" -> "window" -> "global" -> "").
PHOSPHORANIMATION_EXPORT QString parentPath(const QString& path);

/// Built-in default shader effect id for an event @p path, or empty for none.
///
/// SSOT for "what shader does this event animate with out of the box". Two
/// families default to a shader:
///   • Window SNAP (snap in/out, layout-switch) → "window-morph" (geometry
///     cross-fade), run by the kwin-effect. The interactive-drag leaf
///     (`window.movement.move`) carries NO default — a crossfade pack
///     cannot drive a held drag, and the move-class packs (wobble) stay
///     opt-in.
///   • Overlay show/hide leaves (osd.{show,hide},
///     popup.{zoneSelector,layoutPicker,snapAssist,cheatsheet}.{show,hide})
///     → "fade"
///     (fade-and-scale), run by the daemon SurfaceAnimator instead of its C++
///     opacity/scale legs. The category roots (osd, popup, osd.pop) carry no
///     default.
/// Every other event defaults to none. The default applies only when the user
/// has set no override for the path or an ancestor (an explicit "None" is an
/// override and is respected) — see `resolveShaderWithDefault` in
/// ShaderProfileTree.h. Consumed by the kwin-effect resolution, the daemon
/// overlay resolution (animation_config), and the settings UI so the default
/// both plays at runtime and shows as the current value in settings.
PHOSPHORANIMATION_EXPORT QString defaultShaderEffectIdForPath(const QString& path);

} // namespace ProfilePaths

} // namespace PhosphorAnimation
