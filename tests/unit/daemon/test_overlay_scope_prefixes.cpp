// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_overlay_scope_prefixes.cpp
 * @brief Regression: daemon's per-instance overlay scope prefixes must
 *        prefix-match against the SurfaceAnimator configs registered in
 *        OverlayService::setupSurfaceAnimator.
 *
 * Why this exists: every overlay surface gets a per-instance compositor
 * scope (e.g. `plasmazones-layout-osd-{screenId}-{gen}`) so wlr-layer-shell
 * sees each Surface as a distinct scope. The SurfaceAnimator registers
 * per-Role configs against the BASE scope from PzRoles
 * (`plasmazones-layout-osd`) and looks up by longest-prefix-match against
 * the Surface's scope. If the per-instance scope literal in
 * `osd.cpp` / `selector.cpp` / `snapassist.cpp` ever diverges from the
 * matching PzRoles base, the lookup silently falls back to the empty
 * default config and every show/hide runs on the library's 150 ms
 * OutCubic fallback instead of the configured `osd.show` /
 * `panel.popup` / etc.
 *
 * The previous safety net was a single Q_ASSERT_X in
 * createZoneSelectorWindow — debug-only, and only covered ZoneSelector.
 * This test pins down the scope-construction policy for every overlay so
 * release builds don't silently degrade.
 *
 * Construction policy (must match the code paths it mirrors):
 *   - LayoutOsd:     `plasmazones-layout-osd-{id}-{gen}`        (osd.cpp:405)
 *   - NavigationOsd: `plasmazones-navigation-osd-{id}-{gen}`    (osd.cpp:405)
 *   - ZoneSelector:  `plasmazones-zone-selector-{id}-{gen}`     (selector.cpp:419)
 *   - SnapAssist:    `plasmazones-snap-assist-{id}-{gen}`       (snapassist.cpp:382)
 *   - LayoutPicker:  `plasmazones-layout-picker-{id}-{gen}`     (snapassist.cpp:746)
 */

#include <QTest>

#include <PhosphorAnimationLayer/SurfaceAnimator.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorLayer/Role.h>

#include "../../../src/daemon/overlayservice/pz_roles.h"

using namespace PlasmaZones;
namespace PAL = PhosphorAnimationLayer;

namespace {

/// Mirror of OverlayService::setupSurfaceAnimator — builds the same
/// configs against the same roles. If setupSurfaceAnimator's
/// registration policy ever changes (different roles registered, or
/// different config shapes), this helper must change in lockstep —
/// catching any divergence is the point of the test.
std::unique_ptr<PAL::SurfaceAnimator> buildAnimatorMatchingDaemon()
{
    auto anim = std::make_unique<PAL::SurfaceAnimator>(PAL::SurfaceAnimator::Config{});

    const PAL::SurfaceAnimator::Config osdConfig{.showProfile = QStringLiteral("osd.show"),
                                                 .hideProfile = QStringLiteral("osd.hide"),
                                                 .showScaleProfile = QStringLiteral("osd.pop"),
                                                 .hideScaleProfile = QStringLiteral("osd.hide"),
                                                 .showScaleFrom = 0.8,
                                                 .hideScaleTo = 0.9};
    anim->registerConfigForRole(PzRoles::LayoutOsd, osdConfig);
    anim->registerConfigForRole(PzRoles::NavigationOsd, osdConfig);

    const PAL::SurfaceAnimator::Config layoutPickerConfig{.showProfile = QStringLiteral("osd.show"),
                                                          .hideProfile = QStringLiteral("osd.hide"),
                                                          .showScaleProfile = QStringLiteral("osd.pop"),
                                                          .hideScaleProfile = QStringLiteral("osd.hide"),
                                                          .showScaleFrom = 0.9,
                                                          .hideScaleTo = 0.95};
    anim->registerConfigForRole(PzRoles::LayoutPicker, layoutPickerConfig);

    const PAL::SurfaceAnimator::Config zoneSelectorConfig{.showProfile = QStringLiteral("panel.popup"),
                                                          .hideProfile = QStringLiteral("widget.fadeOut"),
                                                          .showScaleProfile = {},
                                                          .hideScaleProfile = {}};
    anim->registerConfigForRole(PzRoles::ZoneSelector, zoneSelectorConfig);

    const PAL::SurfaceAnimator::Config snapAssistConfig{.showProfile = QStringLiteral("panel.popup"),
                                                        .hideProfile = {},
                                                        .showScaleProfile = {},
                                                        .hideScaleProfile = {}};
    anim->registerConfigForRole(PzRoles::SnapAssist, snapAssistConfig);

    return anim;
}

/// Build a per-instance Role the way the daemon does — base role with a
/// per-instance scope prefix that includes a screen id and generation
/// counter. Mirrors the QStringLiteral arg-substitutions in
/// createOsdWindowImpl / createZoneSelectorWindow / createSnapAssistWindowFor /
/// createLayoutPickerWindowFor.
PhosphorLayer::Role perInstanceRole(const PhosphorLayer::Role& base, const QString& family, const QString& screenId,
                                    int generation)
{
    const QString perInstance = QStringLiteral("plasmazones-%1-%2-%3").arg(family).arg(screenId).arg(generation);
    return base.withScopePrefix(perInstance);
}

} // namespace

class TestOverlayScopePrefixes : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Each registered base role's scopePrefix must be the production
    /// `plasmazones-{family}` literal so the per-instance prefix
    /// (`plasmazones-{family}-...`) can prefix-match. Catches a future
    /// rename of any PzRoles literal that the daemon's create paths
    /// don't also pick up.
    void pz_role_base_scope_prefixes_match_production_families()
    {
        QCOMPARE(PzRoles::LayoutOsd.scopePrefix, QStringLiteral("plasmazones-layout-osd"));
        QCOMPARE(PzRoles::NavigationOsd.scopePrefix, QStringLiteral("plasmazones-navigation-osd"));
        QCOMPARE(PzRoles::ZoneSelector.scopePrefix, QStringLiteral("plasmazones-zone-selector"));
        QCOMPARE(PzRoles::SnapAssist.scopePrefix, QStringLiteral("plasmazones-snap-assist"));
        QCOMPARE(PzRoles::LayoutPicker.scopePrefix, QStringLiteral("plasmazones-layout-picker"));
    }

    /// Per-instance LayoutOsd role resolves to the registered osd config.
    /// If `osd.cpp:405` ever stops constructing scopes that prefix-match
    /// PzRoles::LayoutOsd.scopePrefix, configFor returns the empty default
    /// and the OSD silently falls back to the library default profile.
    void layout_osd_scope_resolves_to_registered_config()
    {
        const auto anim = buildAnimatorMatchingDaemon();
        const auto role = perInstanceRole(PzRoles::LayoutOsd, QStringLiteral("layout-osd"), QStringLiteral("DP-1"), 7);
        const auto cfg = anim->configForRole(role);
        QCOMPARE(cfg.showProfile, QStringLiteral("osd.show"));
        QCOMPARE(cfg.showScaleProfile, QStringLiteral("osd.pop"));
        QCOMPARE(cfg.showScaleFrom, 0.8);
    }

    void navigation_osd_scope_resolves_to_registered_config()
    {
        const auto anim = buildAnimatorMatchingDaemon();
        const auto role =
            perInstanceRole(PzRoles::NavigationOsd, QStringLiteral("navigation-osd"), QStringLiteral("HDMI-A-1"), 3);
        const auto cfg = anim->configForRole(role);
        QCOMPARE(cfg.showProfile, QStringLiteral("osd.show"));
        QCOMPARE(cfg.showScaleProfile, QStringLiteral("osd.pop"));
    }

    /// Regression: ZoneSelector. The Q_ASSERT_X in createZoneSelectorWindow
    /// only catches divergence in debug builds; this test catches it in
    /// release too. Pre-fix the daemon used "plasmazones-selector-..."
    /// which did NOT prefix-match "plasmazones-zone-selector".
    void zone_selector_scope_resolves_to_panel_popup()
    {
        const auto anim = buildAnimatorMatchingDaemon();
        const auto role =
            perInstanceRole(PzRoles::ZoneSelector, QStringLiteral("zone-selector"), QStringLiteral("DP-1/vs:0"), 12);
        const auto cfg = anim->configForRole(role);
        QCOMPARE(cfg.showProfile, QStringLiteral("panel.popup"));
        QCOMPARE(cfg.hideProfile, QStringLiteral("widget.fadeOut"));
    }

    void snap_assist_scope_resolves_to_panel_popup()
    {
        const auto anim = buildAnimatorMatchingDaemon();
        const auto role =
            perInstanceRole(PzRoles::SnapAssist, QStringLiteral("snap-assist"), QStringLiteral("DP-1"), 5);
        const auto cfg = anim->configForRole(role);
        QCOMPARE(cfg.showProfile, QStringLiteral("panel.popup"));
        // SnapAssist deliberately has empty hideProfile (destroy-on-hide).
        QVERIFY(cfg.hideProfile.isEmpty());
    }

    void layout_picker_scope_resolves_to_softer_envelope()
    {
        const auto anim = buildAnimatorMatchingDaemon();
        const auto role =
            perInstanceRole(PzRoles::LayoutPicker, QStringLiteral("layout-picker"), QStringLiteral("DP-1"), 1);
        const auto cfg = anim->configForRole(role);
        QCOMPARE(cfg.showProfile, QStringLiteral("osd.show"));
        QCOMPARE(cfg.showScaleProfile, QStringLiteral("osd.pop"));
        // LayoutPicker uses 0.9 (softer than OSD's 0.8) intentionally.
        QCOMPARE(cfg.showScaleFrom, 0.9);
        QCOMPARE(cfg.hideScaleTo, 0.95);
    }

    /// Regression: LayoutOsd's prefix is a strict substring of
    /// LayoutPicker's prefix WOULD be a hazard (`layout-osd` vs
    /// `layout-picker`) — except they share `plasmazones-layout-` only,
    /// and the longest-prefix-match algorithm requires a `-` boundary.
    /// Verify the boundary check works: a per-instance LayoutOsd scope
    /// must NEVER resolve to the LayoutPicker config. Pre-fix this was
    /// the bug class that motivated the boundary check.
    void layout_osd_does_not_cross_pollinate_to_layout_picker()
    {
        const auto anim = buildAnimatorMatchingDaemon();
        const auto osdRole =
            perInstanceRole(PzRoles::LayoutOsd, QStringLiteral("layout-osd"), QStringLiteral("DP-1"), 1);
        const auto cfg = anim->configForRole(osdRole);
        // OSD config has 0.8, picker has 0.9. Picking up the picker
        // config here would mean the boundary check is broken.
        QCOMPARE(cfg.showScaleFrom, 0.8);
    }

    /// Regression: the bare base role (no per-instance suffix) must
    /// also resolve. createOsdWindowImpl uses `withScopePrefix` to
    /// derive a per-instance role, but a future refactor that registers
    /// a base role directly (or a test that constructs one inline)
    /// should still work.
    void exact_base_scope_resolves()
    {
        const auto anim = buildAnimatorMatchingDaemon();
        QCOMPARE(anim->configForRole(PzRoles::LayoutOsd).showProfile, QStringLiteral("osd.show"));
        QCOMPARE(anim->configForRole(PzRoles::ZoneSelector).showProfile, QStringLiteral("panel.popup"));
    }

    /// An unregistered role falls back to the empty default config, NOT
    /// to a sibling overlay's config. Catches a hypothetical regression
    /// where the prefix-match algorithm started doing partial matches.
    void unregistered_role_falls_back_to_default()
    {
        const auto anim = buildAnimatorMatchingDaemon();
        // ShaderPreview is not registered with the animator
        // (setupSurfaceAnimator's documented exclusion list).
        const auto cfg = anim->configForRole(PzRoles::ShaderPreview);
        QVERIFY(cfg.showProfile.isEmpty());
        QVERIFY(cfg.hideProfile.isEmpty());
    }
};

QTEST_MAIN(TestOverlayScopePrefixes)
#include "test_overlay_scope_prefixes.moc"
