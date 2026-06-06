// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_overlay_scope_prefixes.cpp
 * @brief Regression: daemon's per-instance overlay scope prefixes must
 *        prefix-match against the SurfaceAnimator configs registered in
 *        OverlayService::setupSurfaceAnimator.
 *
 * Why this exists: every overlay surface gets a per-instance compositor
 * scope (e.g. `plasmazones-osd-{screenId}-{gen}`) so wlr-layer-shell
 * sees each Surface as a distinct scope. The SurfaceAnimator registers
 * per-Role configs against the BASE scope from PhosphorRoles
 * (`plasmazones-osd`) and looks up by longest-prefix-match against
 * the Surface's scope. If the per-instance scope literal in
 * `osd.cpp` / `selector.cpp` / `snapassist.cpp` ever diverges from the
 * matching PhosphorRoles base, the lookup silently falls back to the empty
 * default config and every show/hide runs on the library's 150 ms
 * OutCubic fallback instead of the configured `osd.show` /
 * `popup.zoneSelector.show` / etc.
 *
 * The previous safety net was a single Q_ASSERT_X in
 * createZoneSelectorWindow: debug-only, and only covered ZoneSelector.
 * Production now constructs every per-instance role via
 * `PhosphorRoles::makePerInstanceRole(base, id, gen)` so the prefix-match is
 * guaranteed by construction; the tests below pin that helper's behavior
 * against the registered configs so a future change to either side
 * trips the test.
 *
 * Construction policy (centralized in phosphor_roles.h):
 *   `<base.scopePrefix>-<screenId>-<generation>`
 *   where base is one of PhosphorRoles::{Osd, ZoneSelector,
 *   SnapAssist, LayoutPicker}.
 */

#include <QTest>

#include <PhosphorAnimation/SurfaceAnimator.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorLayer/Role.h>

#include "../../../src/daemon/overlayservice/phosphor_roles.h"

using namespace PlasmaZones;
namespace PAL = PhosphorAnimationLayer;

namespace {

/// Mirror of OverlayService::setupSurfaceAnimator. Builds the same
/// configs against the same roles. If setupSurfaceAnimator's
/// registration policy ever changes (different roles registered, or
/// different config shapes), this helper must change in lockstep:
/// catching any divergence is the point of the test.
std::unique_ptr<PAL::SurfaceAnimator> buildAnimatorMatchingDaemon(PhosphorAnimation::PhosphorProfileRegistry& registry)
{
    auto anim = std::make_unique<PAL::SurfaceAnimator>(registry, PAL::SurfaceAnimator::Config{});

    // Explicit empty shader fields suppress -Wmissing-field-initializers; mirrors the
    // pattern in src/daemon/overlayservice.cpp's buildXxxConfig() helpers.
    const PAL::SurfaceAnimator::Config osdConfig{.showProfile = QStringLiteral("osd.show"),
                                                 .hideProfile = QStringLiteral("osd.hide"),
                                                 .showScaleProfile = QStringLiteral("osd.show"),
                                                 .hideScaleProfile = QStringLiteral("osd.hide"),
                                                 .showScaleFrom = 0.92,
                                                 .hideScaleTo = 0.96,
                                                 .showShaderEffectId = {},
                                                 .hideShaderEffectId = {},
                                                 .showShaderProfile = {},
                                                 .hideShaderProfile = {},
                                                 .showShaderParameters = {},
                                                 .hideShaderParameters = {}};
    anim->registerConfigForRole(PhosphorRoles::Osd, osdConfig);

    // LayoutPicker: popup surface family. Path-family separation means
    // every leg resolves under `popup.layoutPicker.*`, NOT under
    // `osd.*`. A regression that re-bundles LayoutPicker onto osd paths
    // (or accidentally leaves it on the OSD family during a refactor)
    // would surface as a config-mismatch in this test fixture.
    const PAL::SurfaceAnimator::Config layoutPickerConfig{.showProfile = QStringLiteral("popup.layoutPicker.show"),
                                                          .hideProfile = QStringLiteral("popup.layoutPicker.hide"),
                                                          .showScaleProfile = QStringLiteral("popup.layoutPicker.show"),
                                                          .hideScaleProfile = QStringLiteral("popup.layoutPicker.hide"),
                                                          .showScaleFrom = 0.94,
                                                          .hideScaleTo = 0.97,
                                                          .showShaderEffectId = {},
                                                          .hideShaderEffectId = {},
                                                          .showShaderProfile = {},
                                                          .hideShaderProfile = {},
                                                          .showShaderParameters = {},
                                                          .hideShaderParameters = {}};
    anim->registerConfigForRole(PhosphorRoles::LayoutPicker, layoutPickerConfig);

    // ZoneSelector: popup surface family, dedicated `.show` / `.hide`
    // leaves under `popup.zoneSelector.*`. Previously borrowed
    // `popup` (show) and `widget.fadeOut` (hide); now its own
    // family.
    const PAL::SurfaceAnimator::Config zoneSelectorConfig{.showProfile = QStringLiteral("popup.zoneSelector.show"),
                                                          .hideProfile = QStringLiteral("popup.zoneSelector.hide"),
                                                          .showScaleProfile = {},
                                                          .hideScaleProfile = {},
                                                          .showShaderEffectId = {},
                                                          .hideShaderEffectId = {},
                                                          .showShaderProfile = {},
                                                          .hideShaderProfile = {},
                                                          .showShaderParameters = {},
                                                          .hideShaderParameters = {}};
    anim->registerConfigForRole(PhosphorRoles::ZoneSelector, zoneSelectorConfig);

    // SnapAssist: popup surface family, dedicated `.show` leaf.
    // Previously borrowed `popup` directly; now its own.
    const PAL::SurfaceAnimator::Config snapAssistConfig{.showProfile = QStringLiteral("popup.snapAssist.show"),
                                                        .hideProfile = {},
                                                        .showScaleProfile = {},
                                                        .hideScaleProfile = {},
                                                        .showShaderEffectId = {},
                                                        .hideShaderEffectId = {},
                                                        .showShaderProfile = {},
                                                        .hideShaderProfile = {},
                                                        .showShaderParameters = {},
                                                        .hideShaderParameters = {}};
    anim->registerConfigForRole(PhosphorRoles::SnapAssist, snapAssistConfig);

    return anim;
}

/// Build a per-instance Role the way the daemon does. Routes through the
/// production helper `PhosphorRoles::makePerInstanceRole` (used by the OSD,
/// zone-selector, snap-assist and layout-picker creation paths) so the
/// test and production share a single source of truth for the
/// per-instance scope construction policy. A regression in the helper
/// itself is caught here; a regression in any production caller that
/// bypasses the helper is caught by code review (no production caller
/// hand-rolls the literal anymore).
PhosphorLayer::Role perInstanceRole(const PhosphorLayer::Role& base, const QString& screenId, quint64 generation)
{
    return PlasmaZones::PhosphorRoles::makePerInstanceRole(base, screenId, generation);
}

} // namespace

class TestOverlayScopePrefixes : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Each registered base role's scopePrefix must be the production
    /// `plasmazones-{family}` literal so the per-instance prefix
    /// (`plasmazones-{family}-...`) can prefix-match. Catches a future
    /// rename of any PhosphorRoles literal that the daemon's create paths
    /// don't also pick up.
    void p_role_base_scope_prefixes_match_production_families()
    {
        QCOMPARE(PhosphorRoles::Osd.scopePrefix, QStringLiteral("plasmazones-osd"));
        QCOMPARE(PhosphorRoles::ZoneSelector.scopePrefix, QStringLiteral("plasmazones-zone-selector"));
        QCOMPARE(PhosphorRoles::SnapAssist.scopePrefix, QStringLiteral("plasmazones-snap-assist"));
        QCOMPARE(PhosphorRoles::LayoutPicker.scopePrefix, QStringLiteral("plasmazones-layout-picker"));
    }

    /// Per-instance OSD role resolves to the registered osd config. If
    /// the daemon ever stops constructing scopes that prefix-match
    /// PhosphorRoles::Osd.scopePrefix, configFor returns the empty default
    /// and the OSD silently falls back to the library default profile.
    /// Both layout-OSD and navigation-OSD content share this surface
    /// post-Phase-2, so this test covers both modes.
    void osd_scope_resolves_to_registered_config()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        const auto anim = buildAnimatorMatchingDaemon(registry);
        const auto role = perInstanceRole(PhosphorRoles::Osd, QStringLiteral("DP-1"), 7);
        const auto cfg = anim->configForRole(role);
        QCOMPARE(cfg.showProfile, QStringLiteral("osd.show"));
        QCOMPARE(cfg.showScaleProfile, QStringLiteral("osd.show"));
        QCOMPARE(cfg.showScaleFrom, 0.92);
    }

    /// Different screen, different generation: the per-instance
    /// suffix must not affect the prefix-match outcome.
    void osd_scope_resolves_independent_of_screen_id()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        const auto anim = buildAnimatorMatchingDaemon(registry);
        const auto role = perInstanceRole(PhosphorRoles::Osd, QStringLiteral("HDMI-A-1/vs:0"), 3);
        const auto cfg = anim->configForRole(role);
        QCOMPARE(cfg.showProfile, QStringLiteral("osd.show"));
        QCOMPARE(cfg.showScaleProfile, QStringLiteral("osd.show"));
    }

    /// Regression: ZoneSelector. The Q_ASSERT_X in createZoneSelectorWindow
    /// only catches divergence in debug builds; this test catches it in
    /// release too. Pre-fix the daemon used "plasmazones-selector-..."
    /// which did NOT prefix-match "plasmazones-zone-selector".
    ///
    /// Also pins the surface-family separation: ZoneSelector lives under
    /// `popup.zoneSelector.*`, NOT under the shared `popup`
    /// baseline or the generic `widget.fadeOut` it previously borrowed.
    /// A regression that re-bundles ZoneSelector onto the shared paths
    /// would surface here.
    void zone_selector_scope_resolves_to_dedicated_paths()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        const auto anim = buildAnimatorMatchingDaemon(registry);
        const auto role = perInstanceRole(PhosphorRoles::ZoneSelector, QStringLiteral("DP-1/vs:0"), 12);
        const auto cfg = anim->configForRole(role);
        QCOMPARE(cfg.showProfile, QStringLiteral("popup.zoneSelector.show"));
        QCOMPARE(cfg.hideProfile, QStringLiteral("popup.zoneSelector.hide"));
    }

    /// SnapAssist: dedicated `popup.snapAssist.show` path. Empty
    /// hideProfile because the surface destroys-on-hide and the hide
    /// animation never paints.
    void snap_assist_scope_resolves_to_dedicated_path()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        const auto anim = buildAnimatorMatchingDaemon(registry);
        const auto role = perInstanceRole(PhosphorRoles::SnapAssist, QStringLiteral("DP-1"), 5);
        const auto cfg = anim->configForRole(role);
        QCOMPARE(cfg.showProfile, QStringLiteral("popup.snapAssist.show"));
        // SnapAssist deliberately has empty hideProfile (destroy-on-hide).
        QVERIFY(cfg.hideProfile.isEmpty());
    }

    /// LayoutPicker: dedicated `popup.layoutPicker.*` paths
    /// (previously borrowed osd.*; surface-family separation moved it
    /// off the OSD family so JSON edits to `osd.hide` no longer leak
    /// into the picker's hide animation).
    void layout_picker_scope_resolves_to_dedicated_paths()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        const auto anim = buildAnimatorMatchingDaemon(registry);
        const auto role = perInstanceRole(PhosphorRoles::LayoutPicker, QStringLiteral("DP-1"), 1);
        const auto cfg = anim->configForRole(role);
        QCOMPARE(cfg.showProfile, QStringLiteral("popup.layoutPicker.show"));
        QCOMPARE(cfg.hideProfile, QStringLiteral("popup.layoutPicker.hide"));
        QCOMPARE(cfg.showScaleProfile, QStringLiteral("popup.layoutPicker.show"));
        // LayoutPicker uses 0.94 (softer than OSD's 0.92) intentionally.
        QCOMPARE(cfg.showScaleFrom, 0.94);
        QCOMPARE(cfg.hideScaleTo, 0.97);
    }

    /// Surface-family separation invariant: a JSON edit to `osd.hide`
    /// (genuine OSD hide path) MUST NOT leak into LayoutPicker's hide.
    /// Pre-separation, LayoutPicker borrowed `osd.show`/`osd.hide`/
    /// `osd.pop`, so any per-OSD timing tweak would also reshape the
    /// picker. Exactly the coupling the user asked to break. Pin the
    /// invariant: no LayoutPicker leg references an `osd.*` path.
    void layout_picker_does_not_borrow_osd_paths()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        const auto anim = buildAnimatorMatchingDaemon(registry);
        const auto cfg = anim->configForRole(PhosphorRoles::LayoutPicker);
        QVERIFY2(!cfg.showProfile.startsWith(QStringLiteral("osd.")),
                 "LayoutPicker.showProfile must not live under osd.*: surface families are partitioned");
        QVERIFY2(!cfg.hideProfile.startsWith(QStringLiteral("osd.")),
                 "LayoutPicker.hideProfile must not live under osd.*: surface families are partitioned");
        QVERIFY2(!cfg.showScaleProfile.startsWith(QStringLiteral("osd.")),
                 "LayoutPicker.showScaleProfile must not live under osd.*: surface families are partitioned");
        QVERIFY2(!cfg.hideScaleProfile.startsWith(QStringLiteral("osd.")),
                 "LayoutPicker.hideScaleProfile must not live under osd.*: surface families are partitioned");
    }

    /// Regression: longest-prefix matching must respect `-` boundaries.
    /// `plasmazones-osd` is NOT a prefix of any other role's
    /// scope, but a future rename that introduces overlap (e.g. adding a
    /// `plasmazones-osd-bar`) must not silently route to the wrong
    /// config. Verify the boundary check works by confirming a
    /// per-instance OSD scope picks up the OSD config (showScaleFrom
    /// 0.92), not the picker (0.94).
    void osd_does_not_cross_pollinate_to_layout_picker()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        const auto anim = buildAnimatorMatchingDaemon(registry);
        const auto role = perInstanceRole(PhosphorRoles::Osd, QStringLiteral("DP-1"), 1);
        const auto cfg = anim->configForRole(role);
        // OSD config has 0.92, picker has 0.94. Picking up the picker
        // config here would mean the boundary check is broken.
        QCOMPARE(cfg.showScaleFrom, 0.92);
    }

    /// Regression: the bare base role (no per-instance suffix) must
    /// also resolve. The OSD creation path uses `withScopePrefix` to
    /// derive a per-instance role, but a future refactor that registers
    /// a base role directly (or a test that constructs one inline)
    /// should still work.
    void exact_base_scope_resolves()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        const auto anim = buildAnimatorMatchingDaemon(registry);
        QCOMPARE(anim->configForRole(PhosphorRoles::Osd).showProfile, QStringLiteral("osd.show"));
        QCOMPARE(anim->configForRole(PhosphorRoles::ZoneSelector).showProfile,
                 QStringLiteral("popup.zoneSelector.show"));
    }

    /// An unregistered role falls back to the empty default config, NOT
    /// to a sibling overlay's config. Catches a hypothetical regression
    /// where the prefix-match algorithm started doing partial matches.
    void unregistered_role_falls_back_to_default()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        const auto anim = buildAnimatorMatchingDaemon(registry);
        // ShaderPreview is not registered with the animator
        // (setupSurfaceAnimator's documented exclusion list).
        const auto cfg = anim->configForRole(PhosphorRoles::ShaderPreview);
        QVERIFY(cfg.showProfile.isEmpty());
        QVERIFY(cfg.hideProfile.isEmpty());
    }
};

QTEST_MAIN(TestOverlayScopePrefixes)
#include "test_overlay_scope_prefixes.moc"
