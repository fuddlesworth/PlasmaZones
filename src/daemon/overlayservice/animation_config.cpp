// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file animation_config.cpp
 * @brief Per-role SurfaceAnimator config builders + animator wireup.
 *
 * Split from overlayservice.cpp to keep each translation unit under the
 * project's <800-line guideline. Owns:
 *   - resolveShaderEffect / resolveShaderParameters (tree → config field
 *     adapters)
 *   - build*Config factories (one per overlay role: Notification,
 *     LayoutPicker, ZoneSelector, SnapAssist) — each documents the
 *     visual shape it encodes
 *   - OverlayService::setupSurfaceAnimator — animator construction +
 *     initial config registration
 *   - OverlayService::applyShaderProfilesToAnimator — per-role re-
 *     registration on shader-tree changes (settings-edit live reload)
 */

#include "internal.h"
#include "../overlayservice.h"
#include "pz_roles.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorAnimation/SurfaceAnimator.h>

namespace PlasmaZones {

namespace {

// ── Per-role Config builders ───────────────────────────────────────────
// One factory per overlay role so adding/tweaking a 6th overlay touches
// exactly one named function rather than appending to a 100-line
// setupSurfaceAnimator. Each builder documents the visual shape it
// encodes; setupSurfaceAnimator just wires the builder output to the
// matching PzRole.
//
// **Surface-family separation policy.** Profile paths are partitioned by
// surface family so each overlay tunes independently:
//
//   - **OSD family (`osd.*`)** — genuine OSDs only. Used by the
//     Notification surface (LayoutOsd / NavigationOsd). A JSON edit to
//     `osd.hide` affects OSDs and ONLY OSDs.
//   - **Popup family (`popup.<surface>.*`)** — non-OSD overlay
//     surfaces. Each gets its own leaf paths under
//     `popup.<surface>` per surface's needs:
//       • `popup.layoutPicker.{show, hide, popIn}` (opacity legs +
//         scale show; hide-scale couples to `.hide`)
//       • `popup.zoneSelector.{show, hide}` (opacity-only)
//       • `popup.snapAssist.show` (destroy-on-hide; no `.hide`
//         leaf because no frame ever paints)
//     A JSON edit to `popup.layoutPicker.hide` affects ONLY the
//     layout picker; siblings are unaffected.
//
// The shipped tree carries zero bundled per-leaf profile JSONs — every
// profile is sourced from the Settings UI's per-node overrides via
// `PhosphorProfileRegistry::registerProfile`, with
// `resolveWithInheritance()` walking the parent chain so a parent-node
// edit (e.g. "All Popups → 2000 ms" written to `popup`)
// propagates to every leaf under it. Unset paths fall through to
// library defaults (150 ms OutCubic). User-authored JSONs at
// `~/.local/share/plasmazones/profiles/<path>.json` are still loaded
// by ProfileLoader for advanced users who want file-based overrides.
//
// **Within-family scale-leg coupling (intentional, scoped).** Each
// surface family's hide-leg-scale reuses the surface family's
// hide-leg-opacity path (e.g. `osd.hide` drives both opacity and scale
// hide for OSDs). Editing `osd.hide` to add Spring physics affects both
// legs of the OSD hide. This is the same pattern OSDs and (formerly)
// LayoutPicker shared — kept INSIDE each surface family but not BETWEEN
// them, which is the change here. If a future surface needs decoupled
// opacity/scale tuning, introduce a sibling path
// (e.g. `popup.layoutPicker.popOut`) and register it on
// `hideScaleProfile`.

namespace PAL = PhosphorAnimationLayer;
namespace PAS = PhosphorAnimationShaders;

/// Resolve a path against the shader profile tree. A default-constructed
/// tree (empty baseline + no overrides) resolves every path to an empty
/// effect id, equivalent to "no shader leg" — motion runs alone, identical
/// to the pre-shader-wireup behaviour. The setSettings() handler later
/// re-registers configs with the live tree once settings exist.
///
/// **Source-of-truth note.** The settings UI gates its shader picker on
/// `src/core/animationshadersupportedpaths.h::shaderSupportedEventPaths`,
/// which enumerates exactly the paths consumed by `resolveShaderEffect`
/// in the build*Config functions below. When a new shader-leg surface
/// lands here, append its leg paths to that list in lockstep so the
/// settings UI starts surfacing the picker on the new path.
QString resolveShaderEffect(const PAS::ShaderProfileTree& tree, const QString& path)
{
    return tree.resolve(path).effectiveEffectId();
}

/// Resolve a path against the shader profile tree and extract the per-event
/// parameter overrides. Empty map when the profile didn't override anything
/// — the shader runs with its declared defaults from metadata.json.
QVariantMap resolveShaderParameters(const PAS::ShaderProfileTree& tree, const QString& path)
{
    return tree.resolve(path).effectiveParameters();
}

/// Default config — empty. Surfaces that route through the animator
/// without a registered config fall back to AnimatedValue's library
/// default (150 ms OutCubic), same as a missing-profile lookup. Every
/// Surface that goes through Surface::show()/hide() in this service has
/// an explicit registration below; the empty default is documentation.
PAL::SurfaceAnimator::Config buildDefaultConfig()
{
    return PAL::SurfaceAnimator::Config{};
}

/// LayoutOsd / NavigationOsd: identical fade-and-pop shape. Shader leg
/// resolves osd.show / osd.hide from the user's tree so OSDs can fly in
/// (slide), dissolve, etc. independently of zone or popup events.
///
/// **Genuine-OSD surface family.** All four leg paths live under
/// `osd.*`. A JSON edit to `osd.hide` affects OSDs and ONLY OSDs —
/// LayoutPicker / ZoneSelector / SnapAssist live in the
/// `popup.*` family.
PAL::SurfaceAnimator::Config buildOsdConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return PAL::SurfaceAnimator::Config{.showProfile = PP::OsdShow,
                                        .hideProfile = PP::OsdHide,
                                        .showScaleProfile = PP::OsdShow,
                                        .hideScaleProfile = PP::OsdHide,
                                        .showScaleFrom = 0.8,
                                        .hideScaleTo = 0.9,
                                        .showShaderEffectId = resolveShaderEffect(tree, PP::OsdShow),
                                        .hideShaderEffectId = resolveShaderEffect(tree, PP::OsdHide),
                                        .showShaderProfile = PP::OsdShow,
                                        .hideShaderProfile = PP::OsdHide,
                                        .showShaderParameters = resolveShaderParameters(tree, PP::OsdShow),
                                        .hideShaderParameters = resolveShaderParameters(tree, PP::OsdHide)};
}

/// LayoutPicker: OSD-style fade-and-pop shape with a softer scale
/// envelope (0.9→1 vs the OSD's 0.8→1) since the picker is a larger
/// surface.
///
/// **Popup surface family — dedicated path partition.** Every leg
/// resolves under `popup.layoutPicker.*`, NOT under `osd.*`. A
/// Settings-UI edit at `popup.layoutPicker.hide` affects ONLY
/// the layout picker; OSD timings stay independent. With no override
/// set, `resolveWithInheritance` walks up to `popup` and finally
/// to library defaults (150 ms OutCubic).
///
/// Shader legs key on the same `.show` / `.hide` leaves so a user can
/// dissolve in and slide out (or any asymmetric pair). Both leaves walk
/// up to `popup.layoutPicker` and on to `popup`, so a user
/// who wants symmetric treatment overrides the surface path once and
/// skips the leaves.
PAL::SurfaceAnimator::Config buildLayoutPickerConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return PAL::SurfaceAnimator::Config{
        .showProfile = PP::PopupLayoutPickerShow,
        .hideProfile = PP::PopupLayoutPickerHide,
        .showScaleProfile = PP::PopupLayoutPickerShow,
        .hideScaleProfile = PP::PopupLayoutPickerHide,
        .showScaleFrom = 0.9,
        .hideScaleTo = 0.95,
        .showShaderEffectId = resolveShaderEffect(tree, PP::PopupLayoutPickerShow),
        .hideShaderEffectId = resolveShaderEffect(tree, PP::PopupLayoutPickerHide),
        .showShaderProfile = PP::PopupLayoutPickerShow,
        .hideShaderProfile = PP::PopupLayoutPickerHide,
        .showShaderParameters = resolveShaderParameters(tree, PP::PopupLayoutPickerShow),
        .hideShaderParameters = resolveShaderParameters(tree, PP::PopupLayoutPickerHide)};
}

/// ZoneSelector: opacity-only show/hide. `keepMappedOnHide=true` so the
/// hide animation actually paints.
///
/// **Popup surface family — dedicated path partition.** Every leg
/// resolves under `popup.zoneSelector.*`, NOT under the shared
/// `popup` baseline or the generic `widget.fadeOut` it previously
/// borrowed. A Settings-UI edit at `popup.zoneSelector.hide`
/// affects ONLY the zone selector. With no override set,
/// `resolveWithInheritance` walks up to `popup` then library
/// defaults.
PAL::SurfaceAnimator::Config buildZoneSelectorConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return PAL::SurfaceAnimator::Config{
        .showProfile = PP::PopupZoneSelectorShow,
        .hideProfile = PP::PopupZoneSelectorHide,
        .showScaleProfile = {},
        .hideScaleProfile = {},
        .showShaderEffectId = resolveShaderEffect(tree, PP::PopupZoneSelectorShow),
        .hideShaderEffectId = resolveShaderEffect(tree, PP::PopupZoneSelectorHide),
        .showShaderProfile = PP::PopupZoneSelectorShow,
        .hideShaderProfile = PP::PopupZoneSelectorHide,
        .showShaderParameters = resolveShaderParameters(tree, PP::PopupZoneSelectorShow),
        .hideShaderParameters = resolveShaderParameters(tree, PP::PopupZoneSelectorHide)};
}

/// SnapAssist: pop-in show only. The overlay uses destroy-on-hide
/// (keepMappedOnHide=false), so ~Surface synchronously cancels any
/// in-flight beginHide before the hide animation can paint a frame.
/// Only `popup.snapAssist.show` is meaningful — `.hide` is
/// intentionally absent from the taxonomy because no frame ever paints.
///
/// **Inheritance caveat (popup-family-only).** ShaderProfileTree::resolve
/// walks parent paths, so within the popup family a user setting
/// `popup` to dissolve cascades to LayoutPicker and ZoneSelector
/// (both legs) and to SnapAssist's show leg via the chain
/// `popup.<surface>.<leg>` → `popup.<surface>` →
/// `popup` → `global`. SnapAssist's HIDE leg deliberately drops
/// that walk-up — the surface destroys before paint, so the resolve
/// would be contract-pure-and-runtime-no-op. The genuine OSD
/// (`osd.show`/`osd.hide`) is in a separate subtree and is NOT touched
/// by `popup` overrides regardless. If someone ever flips
/// `keepMappedOnHide` to true on snap-assist, restore the
/// `resolveShaderEffect(tree, PopupSnapAssistHide)` line below in
/// lockstep with adding the new path constant.
PAL::SurfaceAnimator::Config buildSnapAssistConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return PAL::SurfaceAnimator::Config{// Popup surface family — dedicated path. A user editing
                                        // `popup.snapAssist.show.json` affects ONLY the snap
                                        // assist; siblings are unaffected. Built-in default mirrors the
                                        // prior `popup` (150 ms widget-out) so behaviour is
                                        // preserved.
                                        .showProfile = PP::PopupSnapAssistShow,
                                        .hideProfile = {},
                                        .showScaleProfile = {},
                                        .hideScaleProfile = {},
                                        .showShaderEffectId = resolveShaderEffect(tree, PP::PopupSnapAssistShow),
                                        .hideShaderEffectId = {},
                                        .showShaderProfile = PP::PopupSnapAssistShow,
                                        .hideShaderProfile = {},
                                        .showShaderParameters = resolveShaderParameters(tree, PP::PopupSnapAssistShow),
                                        .hideShaderParameters = {}};
}

} // namespace

void OverlayService::setupSurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& profileRegistry)
{
    namespace PAL = PhosphorAnimationLayer;

    // Two existing surface types deliberately BYPASS this animator and
    // therefore never read any config (default or per-role):
    //   - Overlay (zone overlay rendering): hidden via direct
    //     window->hide() in dismissOverlayWindow because the rapid
    //     drag-mode path uses _idled property toggling rather than the
    //     show/hide state machine.
    //   - ShaderPreview (editor preview window): shown via direct
    //     window->show() in showShaderPreview because the editor controls
    //     visibility imperatively and re-creates on every open.
    m_surfaceAnimator = std::make_unique<PAL::SurfaceAnimator>(profileRegistry, buildDefaultConfig());
    if (m_animShaderRegistry) {
        m_surfaceAnimator->setAnimationShaderRegistry(m_animShaderRegistry);
    }
    // Lifecycle invariant: `setupSurfaceAnimator` runs from the ctor
    // before `setSettings` is ever called, so `m_settings` is null here
    // and the animator stays at its default-enabled state until
    // `setSettings` wires the live `animationsEnabled` value. If a
    // future caller re-runs `setupSurfaceAnimator` after settings have
    // been wired, route the gate through `setSettings` rather than
    // re-introducing a defensive branch here.

    // Profile names are the same paths PhosphorMotionAnimation in QML
    // uses today, so the live-reload path (drop a JSON, see it apply on
    // next show) automatically applies to the C++ side too.
    //
    // Phase-2 surface unification: a single per-screen
    // PzRoles::Notification surface (NotificationOverlay.qml) hosts both
    // layout-OSD and navigation-OSD content via a Loader. Production
    // surfaces are scoped `plasmazones-notification-{screenId}-{gen}`
    // and resolve to this config via the animator's longest-prefix
    // lookup.
    //
    // Initial registration runs with an empty tree — m_settings is wired
    // later via setSettings(). A default-constructed tree resolves every
    // path to an empty effect id, so this pass installs motion-only
    // configs (identical to the pre-shader-wireup behaviour). Once
    // settings exist, setSettings calls applyShaderProfilesToAnimator
    // again with the live tree, and connects shaderProfileTreeChanged
    // for live-reload. This keeps the constructor's invariant ("animator
    // is ready before any Surface is created") while deferring the
    // shader wiring to the moment settings are available.
    applyShaderProfilesToAnimator(PAS::ShaderProfileTree{});
}

void OverlayService::applyShaderProfilesToAnimator(const PAS::ShaderProfileTree& tree)
{
    if (!m_surfaceAnimator) {
        return;
    }
    // Diagnostic log gated on lcOverlay().isDebugEnabled() — qCDebug
    // gates the OUTPUT but Qt evaluates argument expressions
    // unconditionally, so the seven extra resolveShaderEffect calls
    // would run even when debug logging is disabled. Each
    // ShaderProfileTree::resolve walks the parent chain and overlay-
    // merges every override; on a typical settings-edit signal storm
    // this is wasted work the explicit gate eliminates.
    if (lcOverlay().isDebugEnabled()) {
        namespace PP = PhosphorAnimation::ProfilePaths;
        qCDebug(lcOverlay).nospace() << "applyShaderProfilesToAnimator: overrides=" << tree.overriddenPaths().size()
                                     << " resolved: osd.show=" << resolveShaderEffect(tree, PP::OsdShow)
                                     << " osd.hide=" << resolveShaderEffect(tree, PP::OsdHide)
                                     << " zoneSelector.show=" << resolveShaderEffect(tree, PP::PopupZoneSelectorShow)
                                     << " zoneSelector.hide=" << resolveShaderEffect(tree, PP::PopupZoneSelectorHide)
                                     << " layoutPicker.show=" << resolveShaderEffect(tree, PP::PopupLayoutPickerShow)
                                     << " layoutPicker.hide=" << resolveShaderEffect(tree, PP::PopupLayoutPickerHide)
                                     << " snapAssist.show=" << resolveShaderEffect(tree, PP::PopupSnapAssistShow);
    }
    m_surfaceAnimator->registerConfigForRole(PzRoles::Notification, buildOsdConfig(tree));
    m_surfaceAnimator->registerConfigForRole(PzRoles::LayoutPicker, buildLayoutPickerConfig(tree));
    m_surfaceAnimator->registerConfigForRole(PzRoles::ZoneSelector, buildZoneSelectorConfig(tree));
    m_surfaceAnimator->registerConfigForRole(PzRoles::SnapAssist, buildSnapAssistConfig(tree));
}

} // namespace PlasmaZones
