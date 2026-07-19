// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file animation_config.cpp
 * @brief Per-role SurfaceAnimator config builders + animator wireup.
 *
 * Owns:
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
#include "phosphor_roles.h"

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
// matching PRole.
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

/// Resolve a path's shader effect id, applying the built-in per-event default
/// via `resolveShaderWithDefault`. A default-constructed tree (empty baseline +
/// no overrides) therefore resolves overlay show/hide paths to their default
/// ("fade") and every other path to empty ("no shader leg" - motion runs
/// alone). The setSettings() handler later re-registers configs with the live
/// tree once settings exist, applying any user overrides on top.
///
/// **Source-of-truth note.** The settings UI gates its shader picker on
/// `src/core/animationshadersupportedpaths.h::shaderSupportedEventPaths`,
/// which enumerates exactly the paths consumed by `resolveShaderEffect`
/// in the build*Config functions below. When a new shader-leg surface
/// lands here, append its leg paths to that list in lockstep so the
/// settings UI starts surfacing the picker on the new path.
QString resolveShaderEffect(const PAS::ShaderProfileTree& tree, const QString& path)
{
    // Route through the SSOT so overlay show/hide paths pick up their built-in
    // default ("fade") when the user has set no override — and a per-app/tree
    // "None" is still respected (empty id → SurfaceAnimator's C++ legs run).
    return PAS::resolveShaderWithDefault(tree, path).effectiveEffectId();
}

/// Resolve a path against the shader profile tree (via the built-in default)
/// and extract the per-event parameter overrides. When the resolved shader is
/// the built-in "fade" overlay default and the user left `scaleAmount` unset,
/// seed it from @p fadeScaleAmount — the surface's prior C++ scale depth
/// (1 - showScaleFrom on show, 1 - hideScaleTo on hide; 0.0 for opacity-only
/// surfaces) — so the shader reproduces the surface's existing pop feel. A user
/// override of the shader (to anything but fade) or of scaleAmount itself wins.
QVariantMap resolveShaderParameters(const PAS::ShaderProfileTree& tree, const QString& path, double fadeScaleAmount)
{
    const PAS::ShaderProfile resolved = PAS::resolveShaderWithDefault(tree, path);
    QVariantMap params = resolved.effectiveParameters();
    if (resolved.effectiveEffectId() == QLatin1String("fade") && !params.contains(QLatin1String("scaleAmount"))) {
        params.insert(QStringLiteral("scaleAmount"), fadeScaleAmount);
    }
    return params;
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
    // Scale envelope, shared by the C++ scale-leg fallback (showScaleFrom /
    // hideScaleTo) and the fade shader's scaleAmount seed (1 - the scale value),
    // so retuning the envelope keeps both in lockstep.
    constexpr double kShowScaleFrom = 0.92;
    constexpr double kHideScaleTo = 0.96;
    return PAL::SurfaceAnimator::Config{
        .showProfile = PP::OsdShow,
        .hideProfile = PP::OsdHide,
        .showScaleProfile = PP::OsdShow,
        .hideScaleProfile = PP::OsdHide,
        .showScaleFrom = kShowScaleFrom,
        .hideScaleTo = kHideScaleTo,
        .showShaderEffectId = resolveShaderEffect(tree, PP::OsdShow),
        .hideShaderEffectId = resolveShaderEffect(tree, PP::OsdHide),
        .showShaderProfile = PP::OsdShow,
        .hideShaderProfile = PP::OsdHide,
        .showShaderParameters = resolveShaderParameters(tree, PP::OsdShow, 1.0 - kShowScaleFrom),
        .hideShaderParameters = resolveShaderParameters(tree, PP::OsdHide, 1.0 - kHideScaleTo)};
}

/// LayoutPicker: OSD-style fade-and-pop shape with a softer scale
/// envelope (0.94→1 vs the OSD's 0.92→1) since the picker is a larger
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
    // Scale envelope (softer than the OSD's 0.92→1 since the picker is larger),
    // shared by the C++ scale-leg fallback and the fade scaleAmount seed.
    constexpr double kShowScaleFrom = 0.94;
    constexpr double kHideScaleTo = 0.97;
    return PAL::SurfaceAnimator::Config{
        .showProfile = PP::PopupLayoutPickerShow,
        .hideProfile = PP::PopupLayoutPickerHide,
        .showScaleProfile = PP::PopupLayoutPickerShow,
        .hideScaleProfile = PP::PopupLayoutPickerHide,
        .showScaleFrom = kShowScaleFrom,
        .hideScaleTo = kHideScaleTo,
        .showShaderEffectId = resolveShaderEffect(tree, PP::PopupLayoutPickerShow),
        .hideShaderEffectId = resolveShaderEffect(tree, PP::PopupLayoutPickerHide),
        .showShaderProfile = PP::PopupLayoutPickerShow,
        .hideShaderProfile = PP::PopupLayoutPickerHide,
        .showShaderParameters = resolveShaderParameters(tree, PP::PopupLayoutPickerShow, 1.0 - kShowScaleFrom),
        .hideShaderParameters = resolveShaderParameters(tree, PP::PopupLayoutPickerHide, 1.0 - kHideScaleTo)};
}

/// Cheatsheet: fade+scale twin of the layout picker. Every leg resolves
/// under `popup.cheatsheet.*` so a Settings-UI edit affects only the
/// cheatsheet; with no override set, resolution walks up to `popup` and
/// finally library defaults. The scale envelope matches the picker's
/// (large centered card, softer than the OSD pop).
PAL::SurfaceAnimator::Config buildCheatsheetConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    constexpr double kShowScaleFrom = 0.94;
    constexpr double kHideScaleTo = 0.97;
    return PAL::SurfaceAnimator::Config{
        .showProfile = PP::PopupCheatsheetShow,
        .hideProfile = PP::PopupCheatsheetHide,
        .showScaleProfile = PP::PopupCheatsheetShow,
        .hideScaleProfile = PP::PopupCheatsheetHide,
        .showScaleFrom = kShowScaleFrom,
        .hideScaleTo = kHideScaleTo,
        .showShaderEffectId = resolveShaderEffect(tree, PP::PopupCheatsheetShow),
        .hideShaderEffectId = resolveShaderEffect(tree, PP::PopupCheatsheetHide),
        .showShaderProfile = PP::PopupCheatsheetShow,
        .hideShaderProfile = PP::PopupCheatsheetHide,
        .showShaderParameters = resolveShaderParameters(tree, PP::PopupCheatsheetShow, 1.0 - kShowScaleFrom),
        .hideShaderParameters = resolveShaderParameters(tree, PP::PopupCheatsheetHide, 1.0 - kHideScaleTo)};
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
        // Opacity-only surface (no scale leg) → fade scaleAmount 0.0 (pure fade).
        .showShaderParameters = resolveShaderParameters(tree, PP::PopupZoneSelectorShow, 0.0),
        .hideShaderParameters = resolveShaderParameters(tree, PP::PopupZoneSelectorHide, 0.0)};
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
    return PAL::SurfaceAnimator::Config{
        // Popup surface family - dedicated path. A user editing
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
        // Opacity-only surface (no scale leg) → fade scaleAmount 0.0 (pure fade).
        .showShaderParameters = resolveShaderParameters(tree, PP::PopupSnapAssistShow, 0.0),
        .hideShaderParameters = resolveShaderParameters(tree, PP::PopupSnapAssistHide, 0.0)};
}

} // namespace

void OverlayService::setupSurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& profileRegistry)
{
    namespace PAL = PhosphorAnimationLayer;

    // Two existing surface types do NOT have a per-role config registered
    // and therefore fall back to the empty default (no shader effect, the
    // library-default 150 ms OutCubic motion):
    //   - ZoneOverlay (zone overlay rendering): routes through the
    //     animator (overlay.cpp passes PhosphorRoles::ZoneOverlay to
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
    // animator config registered here is keyed on PhosphorRoles::Osd's scope
    // prefix (`plasmazones-osd`). Lookups go through the role-override
    // path on beginShow / beginHide (osd.cpp passes PhosphorRoles::Osd as the
    // override role), not through the longest-prefix surface lookup, so
    // the passive-shell surface scope does not collide with this config.
    //
    // Initial registration runs with an empty tree - m_settings is wired
    // later via setSettings(). A default-constructed tree resolves each
    // path to its built-in default (overlay show/hide → "fade"; paths with
    // no default → motion-only), so this pass installs the default shader
    // configs. Once settings exist, setSettings calls
    // applyShaderProfilesToAnimator again with the live tree (applying any
    // user overrides), and connects shaderProfileTreeChanged for
    // live-reload. This keeps the constructor's invariant ("animator is
    // ready before any Surface is created") while deferring the live-tree
    // wiring to the moment settings are available.
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
    m_shellHost->registerConfigForRole(PhosphorRoles::Osd, buildOsdConfig(tree));
    m_shellHost->registerConfigForRole(PhosphorRoles::LayoutPicker, buildLayoutPickerConfig(tree));
    m_shellHost->registerConfigForRole(PhosphorRoles::ZoneSelector, buildZoneSelectorConfig(tree));
    m_shellHost->registerConfigForRole(PhosphorRoles::SnapAssist, buildSnapAssistConfig(tree));
    m_shellHost->registerConfigForRole(PhosphorRoles::Cheatsheet, buildCheatsheetConfig(tree));
}

} // namespace PlasmaZones
