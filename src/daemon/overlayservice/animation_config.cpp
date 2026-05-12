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
 *   - build*Config factories (one per overlay role: Osd, LayoutPicker,
 *     ZoneSelector, SnapAssist). Each documents the visual shape it
 *     encodes.
 *   - OverlayService::setupSurfaceAnimator - animator construction +
 *     initial config registration
 *   - OverlayService::applyShaderProfilesToAnimator - per-role re-
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

#include <PhosphorOverlay/ShellHost.h>

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
//   - **OSD family (`osd.*`)**: genuine OSDs only. Driven by the
//     passive-shell OSD slot (LayoutOsd and NavigationOsd content). A
//     JSON edit to `osd.hide` affects OSDs and ONLY OSDs.
//   - **Popup family (`popup.<surface>.*`)** - non-OSD overlay
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
// The shipped tree carries zero bundled per-leaf profile JSONs - every
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
// LayoutPicker shared - kept INSIDE each surface family but not BETWEEN
// them, which is the change here. If a future surface needs decoupled
// opacity/scale tuning, introduce a sibling path
// (e.g. `popup.layoutPicker.popOut`) and register it on
// `hideScaleProfile`.

namespace PAL = PhosphorAnimationLayer;
namespace PAS = PhosphorAnimationShaders;

/// Resolve a path against the shader profile tree. A default-constructed
/// tree (empty baseline + no overrides) resolves every path to an empty
/// effect id, equivalent to "no shader leg" - motion runs alone, identical
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
/// - the shader runs with its declared defaults from metadata.json.
QVariantMap resolveShaderParameters(const PAS::ShaderProfileTree& tree, const QString& path)
{
    return tree.resolve(path).effectiveParameters();
}

/// Default config - empty. Surfaces that route through the animator
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
/// `osd.*`. A JSON edit to `osd.hide` affects OSDs and ONLY OSDs -
/// LayoutPicker / ZoneSelector / SnapAssist live in the
/// `popup.*` family.
PAL::SurfaceAnimator::Config buildOsdConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return PAL::SurfaceAnimator::Config{.showProfile = PP::OsdShow,
                                        .hideProfile = PP::OsdHide,
                                        .showScaleProfile = PP::OsdShow,
                                        .hideScaleProfile = PP::OsdHide,
                                        .showScaleFrom = 0.92,
                                        .hideScaleTo = 0.96,
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
/// **Popup surface family - dedicated path partition.** Every leg
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
        .showScaleFrom = 0.94,
        .hideScaleTo = 0.97,
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
/// **Popup surface family - dedicated path partition.** Every leg
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

/// SnapAssist: full show/hide pair. Pre-unified-shell, snap-assist
/// owned its own wl_surface and tore it down on hide (~Surface
/// synchronously cancelling any in-flight beginHide), so the hide leg
/// never painted and `popup.snapAssist.hide` was intentionally absent
/// from the taxonomy. After the unified-shell migration the surface
/// stays mapped (keepMappedOnHide=true on the shared shell wl_surface)
/// and snap-assist's hide runs a normal SurfaceAnimator::beginHide on
/// the slot - so the hide leg paints frames and a per-event shader
/// assignment is now meaningful.
///
/// **Inheritance.** ShaderProfileTree::resolve walks parent paths, so
/// within the popup family a user setting `popup` to dissolve cascades
/// to LayoutPicker and ZoneSelector (both legs) and to SnapAssist's
/// show AND hide legs via the chain `popup.<surface>.<leg>` →
/// `popup.<surface>` → `popup` → `global`. The genuine OSD
/// (`osd.show`/`osd.hide`) is in a separate subtree and is NOT touched
/// by `popup` overrides regardless.
PAL::SurfaceAnimator::Config buildSnapAssistConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return PAL::SurfaceAnimator::Config{// Popup surface family - dedicated path. A user editing
                                        // `popup.snapAssist.show.json` affects ONLY the snap
                                        // assist; siblings are unaffected. Built-in default mirrors the
                                        // prior `popup` (150 ms widget-out) so behaviour is
                                        // preserved.
                                        .showProfile = PP::PopupSnapAssistShow,
                                        .hideProfile = PP::PopupSnapAssistHide,
                                        .showScaleProfile = {},
                                        .hideScaleProfile = {},
                                        .showShaderEffectId = resolveShaderEffect(tree, PP::PopupSnapAssistShow),
                                        .hideShaderEffectId = resolveShaderEffect(tree, PP::PopupSnapAssistHide),
                                        .showShaderProfile = PP::PopupSnapAssistShow,
                                        .hideShaderProfile = PP::PopupSnapAssistHide,
                                        .showShaderParameters = resolveShaderParameters(tree, PP::PopupSnapAssistShow),
                                        .hideShaderParameters = resolveShaderParameters(tree, PP::PopupSnapAssistHide)};
}

} // namespace

void OverlayService::setupSurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& profileRegistry)
{
    namespace PAL = PhosphorAnimationLayer;

    // Two existing surface types do NOT have a per-role config registered
    // and therefore fall back to the empty default (no shader effect, the
    // library-default 150 ms OutCubic motion):
    //   - ZoneOverlay (zone overlay rendering): routes through the
    //     animator (overlay.cpp passes PzRoles::ZoneOverlay to
    //     beginShow/beginHide on the passive-shell slot) but the default
    //     motion is the intended visual; no shader leg is configured.
    //   - ShaderPreview (editor preview window): shown via direct
    //     window->show() in showShaderPreview because the editor controls
    //     visibility imperatively and re-creates on every open.
    m_surfaceAnimator = std::make_unique<PAL::SurfaceAnimator>(profileRegistry, buildDefaultConfig());
    if (m_animShaderRegistry) {
        m_surfaceAnimator->setAnimationShaderRegistry(m_animShaderRegistry);
    }
    // Wire the new animator into the ShellHost BEFORE
    // applyShaderProfilesToAnimator runs, since that function routes
    // every per-role config write through ShellHost::registerConfigForRole
    // (which is a no-op without an animator). The ShellHost is
    // constructed earlier in the OverlayService ctor.
    //
    // Release-build fatal (qFatal aborts the process) on a null host
    // so a future caller that reorders the ctor and reaches this point
    // with m_shellHost still nullptr exits with a clear diagnostic
    // instead of segfaulting on the next `m_shellHost->`. A null-deref
    // here previously caused a systemd-respawn loop in production
    // because applyShaderProfilesToAnimator's chain led straight into
    // m_shellHost->registerConfigForRole before the host was up.
    if (!m_shellHost) {
        qFatal(
            "OverlayService::setupSurfaceAnimator: m_shellHost must be constructed first "
            "(applyShaderProfilesToAnimator dereferences it on every call)");
    }
    m_shellHost->setSurfaceAnimator(m_surfaceAnimator.get());
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
    // Post-shell-migration: the wl_surface that hosts OSD content is the
    // unified PassiveShell (one per screen, scoped
    // `plasmazones-passive-shell-{screenId}-{gen}`); OSD content rides
    // a slot inside it via `PassiveOverlayShell.qml`'s Loader. The
    // animator config registered here is keyed on PzRoles::Osd's scope
    // prefix (`plasmazones-osd`). Lookups go through the role-override
    // path on beginShow / beginHide (osd.cpp passes PzRoles::Osd as the
    // override role), not through the longest-prefix surface lookup, so
    // the passive-shell surface scope does not collide with this config.
    //
    // Initial registration runs with an empty tree - m_settings is wired
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
    // Diagnostic log gated on lcOverlay().isDebugEnabled() - qCDebug
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
                                     << " snapAssist.show=" << resolveShaderEffect(tree, PP::PopupSnapAssistShow)
                                     << " snapAssist.hide=" << resolveShaderEffect(tree, PP::PopupSnapAssistHide);
    }
    // Route through the lib so animator-config writes share the same
    // host that owns slot lifecycle (3.x) and surface lifecycle (2.x).
    // The lib is now the sole SurfaceAnimator client for both slot
    // hides and per-role config registration; the daemon retains the
    // SHAPE of each config (curves, durations, shader paths) via the
    // build*Config helpers above.
    m_shellHost->registerConfigForRole(PzRoles::Osd, buildOsdConfig(tree));
    m_shellHost->registerConfigForRole(PzRoles::LayoutPicker, buildLayoutPickerConfig(tree));
    m_shellHost->registerConfigForRole(PzRoles::ZoneSelector, buildZoneSelectorConfig(tree));
    m_shellHost->registerConfigForRole(PzRoles::SnapAssist, buildSnapAssistConfig(tree));
}

} // namespace PlasmaZones
