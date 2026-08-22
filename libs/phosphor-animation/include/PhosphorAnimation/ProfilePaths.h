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
/// That is a property of the MOTION tree only. The SHADER tree admits just the
/// paths registered in shaderConsumedLeafEventPaths() and their ancestors:
/// pruneShaderProfileTreeToSupportedPaths runs on both read and write, so a shader
/// override on any other path is dropped rather than kept.
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
//
// snapIn and snapOut are the two DIRECTIONS of one animator
// (ZoneFillAnimation.qml), picked by whether the zone is taking space or
// giving it up: snapIn on the fill preview and on a neighbour absorbing a
// deleted zone, snapOut on the original half when a zone is split. They are
// not two animators, so a caller that changes geometry gets the right leg by
// direction rather than by naming one.
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
// carries its own opt-in `tab` class (see EventClassTab below), not the strip
// class its siblings do.
PHOSPHORANIMATION_EXPORT extern const QString Scrolling;
PHOSPHORANIMATION_EXPORT extern const QString ScrollingView;
PHOSPHORANIMATION_EXPORT extern const QString ScrollingTabSwitch;

// shell.* — surfaces owned by the desktop shell rather than by an
// application: today the Plasma applet popups (the launcher, the system tray
// flyouts, any widget's expanded view). Appearance legs like the window ones,
// driven by the same kwin-effect lifecycle hooks, but on their OWN root for
// the reason the decoration tree gives them one: a foreign surface must not
// inherit what the user chose for their windows. That is enforced in the
// shader tree, where the whole subtree resolves inside itself
// (shaderPathIsolationRoot) — engaging a pack HERE is the entire opt-in, and
// an unconfigured shell surface animates exactly as it did before the root
// existed. Show / hide rather than open / close: these are ephemeral
// surfaces, the same family the osd and popup roots name that way.
//
// The isolation is the SHADER tree's alone. ProfileTree has no isolation arm, so
// these legs take their duration and curve from the ordinary walk, `global`
// included — deliberately, since the timing is inert until a pack is engaged and a
// user who sets one duration for everything means it. Anything describing this
// subtree to a user must scope the claim to the shader, or it is wrong about
// timing.
PHOSPHORANIMATION_EXPORT extern const QString Shell;
PHOSPHORANIMATION_EXPORT extern const QString ShellAppletPopup;
PHOSPHORANIMATION_EXPORT extern const QString ShellAppletPopupShow;
PHOSPHORANIMATION_EXPORT extern const QString ShellAppletPopupHide;

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
// A coarse capability axis layered over the path taxonomy. Two of the six
// classes are the general ones: an animation either reshapes a window's
// GEOMETRY (it has a before-rect and an after-rect) or it changes a
// surface's APPEARANCE (a single surface fading / scaling / glitching in or
// out). A geometry-only shader such as window-morph cross-fades
// `iFromRect → iToRect` and is a silent no-op on an appearance event, so a
// shader declares which classes it supports (AnimationShaderEffect::appliesTo)
// and the settings UI filters the rows it can't drive. The other four —
// DESKTOP, MOVE, STRIP and TAB — each name a distinct uniform contract a
// pack must opt into explicitly, and a universal (empty `appliesTo`) pack
// never reaches them. These string tokens are the SSOT for that vocabulary —
// matched verbatim against `appliesTo` entries and `eventClassForPath`.
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
//   5. `shaderPathResolvesInIsolation` (shaderprofiletree.cpp) — decide
//      whether the new class's leaves may inherit a shader from their
//      ancestors. A leaf whose ONLY ancestors carry other classes must join
//      the predicate, or it inherits packs the applicability gate then
//      refuses at install: the leaf animates nothing while settings shows an
//      inherited "current shader" that never runs (the tab class shipped
//      with exactly this hazard).
//   6. `shaderConsumedLeafEventPaths` (animationshadersupportedpaths.h) —
//      the new class's consumed leaves must be registered, or the picker
//      hides the shader row and the prune drops stored overrides.
// (The coverage chips in AnimationsMotionSetsPage.qml / DecorationSetsPage.qml
// key on the path ROOT segment, not on class tokens — a new class needs a
// case there only if it also introduces a new path root.)
//
// Adding a path ROOT? A different list, and longer than it looks. The `shell`
// root needed every one of these:
//   1. `allBuiltInPaths()` below — the taxonomy every UI and validity check
//      reads. It is NOT a persistence filter: the motion tree stores and
//      loads whatever path it is given (ProfileTree::setOverride/fromJson
//      reject only an empty one), so a path missing here is not dropped, it
//      just becomes unreachable from the UI and inert at resolve time. The
//      writer-side reject lives in AnimationsPageController::isValidEventPath.
//   2. `eventClassForPath` — an unclassified root reads as the ambiguous-row
//      fallback in the pack pickers.
//   3. `shaderConsumedLeafEventPaths` (animationshadersupportedpaths.h) — the
//      shader tree's admission list, per the note at the top of this file.
//   4. An isolation decision: ordinary inheritance, a leaf in
//      `shaderPathResolvesInIsolation`, or a subtree in
//      `shaderPathIsolationRoot`. Deciding nothing means ordinary, which for a
//      foreign surface is usually wrong.
//   5. `segmentLabel` (animationspagecontroller_paths.cpp) — every segment of the
//      built-in taxonomy is translated there; a miss ships untranslated English
//      through the mechanical humanizeSegment fallback.
//   6. The coverage-chip `case` in BOTH AnimationsMotionSetsPage.qml and
//      DecorationSetsPage.qml, per the parenthetical above.
//   7. A settings page plus its registration, topology, page scope and search
//      catalogue entries.
//   8. `animationPageScope` — without an entry the page falls through to the
//      whole-tree branch and its Reset wipes every animation override there is.

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
/// Like `move`, the tab leaf takes NO inherited shader from its ancestors
/// (shaderPathResolvesInIsolation / ShaderProfileTree::resolve). The reason is the
/// opt-in class stated just above, not the ancestors' classes: because a pack must
/// declare `appliesTo: ["tab"]`, every pack offered on an ancestor for that
/// ancestor's own sake is refused here. It has three levels above it —
/// parentPath("scrolling") is `global`, not empty, and the baseline sits above
/// that. A HYBRID declaring `tab` beside the ancestor's class does survive the
/// gate, so the isolation is a policy choice for that case: a pack engaged for
/// window appearance must not start driving tab swaps unasked. Only a direct
/// override at the leaf applies; motion (curve/duration) inheritance is
/// unaffected.
///
/// A tab-ONLY pack is compositor-only by `shaderEffectIsCompositorOnly`'s
/// appearance rule, which is what lets it include `old_content.glsl`
/// unguarded: that sampler is binding-less and the daemon's strict SPIR-V
/// bake rejects it. The BUNDLED-pack validator gate enforces this (a hybrid
/// declaring "appearance" beside "tab" fails the daemon-dialect bake loudly,
/// with a hint naming the fix); a user-installed hybrid bypasses the gate and
/// degrades to logged bake failures rather than being rejected — the same
/// standing gap every geometry+appearance old-content pack shares.
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
/// are `appearance`, and so are the `shell` root and every `shell.*` leaf (what
/// sets that family apart is whose surface it is, which the isolation answers,
/// not the pack vocabulary); the window leaves split by motion-vs-lifecycle; the
/// `window.movement.move` leaf is `move` (held interactive drag) while the
/// rest of the movement sub-tree is `geometry`; the `desktop` root and every
/// `desktop.*` leaf are `desktop`; the `scrolling` root and `scrolling.view`
/// are `strip` while the `scrolling.tabSwitch` leaf is `tab` (the same
/// leaf-beats-subtree carve-out `window.movement.move` gets); the `window`
/// root itself is mixed → empty.
PHOSPHORANIMATION_EXPORT QString eventClassForPath(const QString& path);

/// True when @p path is resolved AGAINST A PARTICULAR WINDOW, so per-window
/// state can reach it.
///
/// The property this answers is narrow and mechanical: does the compositor
/// reach this event holding the window it is for. Ten paths do — the four
/// `window.appearance` leaves, the five `window.movement` leaves, and
/// `scrolling.tabSwitch`. No other path in the taxonomy is, either because its
/// subject is not a window at all (a desktop switch, the scrolling strip
/// itself, an OSD, a panel, the editor's own widgets) or because it is a
/// surface no application owns (the `shell` subtree, deliberately, so a rule
/// cannot retarget a pack engaged on the Shell page). Many of those are never
/// resolved by a compositor leg at all; the rest resolve windowless.
///
/// WHY IT MATTERS TO CALLERS: a Rule's animation actions are stored per event
/// path and resolved through the rule evaluator, which needs a window to match
/// against. On a windowless path the resolvers short-circuit before the
/// evaluator runs, so a rule naming one is stored, shown, and never consulted.
/// The rule editor uses this to avoid offering such a path in the first place.
/// The compositor ALSO reads it to decide whether to run its window filter, so
/// a path this calls windowless skips the user's Animations.WindowFiltering
/// exclusions as well as its rule tier. On `scrolling.tabSwitch` the window a
/// rule matches against is the ARRIVING tab; a rule written against the
/// departing application never fires on that event.
///
/// KEEPING IT HONEST: `tryBeginShaderForEvent` consults this to decide whether
/// to resolve an event windowless, so for that leg the list is not a
/// description of the routing that could drift from it — it IS the routing. The
/// other resolve legs do not consult it and are windowless or per-window by
/// construction at their own call sites: the desktop legs in lifecycle_wiring,
/// the strip in tiling, the daemon's overlay legs in animation_config, and
/// `applyWindowGeometry`'s per-window resolve in drag_snap.
///
/// The hazard runs in the fail-open direction. Add a PER-WINDOW leg without
/// listing it here and tryBeginShaderForEvent resolves it windowless, dropping
/// both its rule tier and its window filter with no warning. (Listing a
/// windowless leg here is the opposite mistake: it would then run through
/// shouldAnimateWindow, whose blanket plasma-shell reject kills it outright.)
/// The exact set is pinned by test_profiletree, in both directions, so a
/// taxonomy addition has to make the call explicitly rather than default into
/// either failure.
PHOSPHORANIMATION_EXPORT bool eventPathResolvesPerWindow(const QString& path);

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
///     opacity/scale legs. Neither the category roots (osd, popup) nor the
///     osd.pop leaf carries a default.
/// Every other event defaults to none. The default applies only when the user
/// has set no override for the path or an ancestor (an explicit "None" is an
/// override and is respected) — see `resolveShaderWithDefault` in
/// ShaderProfileTree.h. Consumed by the kwin-effect resolution, the daemon
/// overlay resolution (animation_config), and the settings UI so the default
/// both plays at runtime and shows as the current value in settings.
PHOSPHORANIMATION_EXPORT QString defaultShaderEffectIdForPath(const QString& path);

} // namespace ProfilePaths

} // namespace PhosphorAnimation
