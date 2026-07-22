// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animation_page_scope.cpp
 * @brief Animation page→event-root scoping behind per-page Reset/Discard/dirty.
 *
 * Pins the pure logic SettingsController's animation branches dispatch
 * through (animationpagescope.{h,cpp}): the page→roots mapping, the
 * include/exclude membership test, the built-in path filter, and the
 * scope-limited tree diff.
 *
 * The load-bearing case is the ONE carve-out in the taxonomy: Window Motion
 * owns `window.movement` but NOT `window.movement.move`, which the Window
 * Dragging page owns. Getting that wrong cross-wires two pages' Reset and
 * Discard — a Motion reset would silently wipe the drag effect, and a
 * Dragging edit would light Motion's dirty badge. A prefix off-by-one would
 * do the same across unrelated roots ("window.movementX" leaking into
 * "window.movement").
 */

#include <QTest>

#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include "settings/pages/animationpagescope.h"

using namespace PlasmaZones;
using PhosphorAnimationShaders::ShaderProfile;
using PhosphorAnimationShaders::ShaderProfileTree;

namespace {

ShaderProfile effectProfile(const QString& effectId)
{
    ShaderProfile p;
    p.effectId = effectId;
    return p;
}

} // namespace

class TestAnimationPageScope : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void generalIsConfigOnly()
    {
        const AnimationPageScope scope = animationPageScope(QStringLiteral("animations-general"));
        QCOMPARE(scope.kind, AnimationPageScope::ConfigOnly);
        QVERIFY(scope.include.isEmpty());
        QVERIFY(scope.exclude.isEmpty());
    }

    void libraryLeavesAreWholeTree()
    {
        // Presets / motion sets / shaders act on the entire editable tree, and
        // an unknown page falls back to the same. The condensed simple page is
        // deliberately NOT here — see simplePageScopesToItsOwnCards.
        for (const auto* page :
             {"animations-presets", "animations-motionsets", "animations-shaders", "not-an-animation-page"}) {
            // QVERIFY2 over QCOMPARE: a bare compare in this loop names neither
            // the failing page nor the rows it never reached.
            const AnimationPageScope scope = animationPageScope(QLatin1String(page));
            QVERIFY2(scope.kind == AnimationPageScope::WholeTree, page);
        }
    }

    void simplePageScopesToItsOwnCards()
    {
        // The condensed page shows five event cards plus the global timing and
        // window-filter cards. Falling back to WholeTree here would make its
        // Reset clear every override on surfaces it does not even display
        // (osd / popup / panel / widget / editor), which is silent data loss.
        const AnimationPageScope simple = animationPageScope(QStringLiteral("animations-simple"));
        QCOMPARE(simple.kind, AnimationPageScope::EventSubtree);
        QVERIFY(simple.includeGeneralKeys);
        QVERIFY(simple.exclude.isEmpty());

        // Every card the page binds is in scope, including the movement parent
        // and the .move leaf it shows alongside it (no carve-out here).
        // window.movement.maximize rides the parent card, as intended.
        for (const auto* path :
             {"window.appearance.open", "window.appearance.close", "window.appearance.minimize", "window.movement",
              "window.movement.move", "window.movement.maximize", "desktop.switch"}) {
            QVERIFY2(animationPathInScope(QLatin1String(path), simple), path);
        }

        // Surfaces the page does not show stay untouched by its Reset. All real
        // taxonomy paths (ProfilePaths) — a typo here would assert nothing,
        // since any unknown string is trivially out of scope.
        for (const auto* path : {"osd.show", "popup.snapAssist.show", "panel.slideIn", "widget.fadeIn", "editor.snapIn",
                                 "desktop.peek", "window.appearance.focus"}) {
            QVERIFY2(!animationPathInScope(QLatin1String(path), simple), path);
        }
    }

    void surfaceLeavesOwnOneRoot()
    {
        const AnimationPageScope windows = animationPageScope(QStringLiteral("animations-windows"));
        QCOMPARE(windows.kind, AnimationPageScope::EventSubtree);
        QCOMPARE(windows.include, QStringList{QStringLiteral("window.appearance")});
        QVERIFY(windows.exclude.isEmpty());

        const AnimationPageScope osds = animationPageScope(QStringLiteral("animations-osds"));
        QCOMPARE(osds.include, QStringList{QStringLiteral("osd")});
    }

    void motionExcludesTheDragCarveOut()
    {
        // THE fragile case: Motion owns window.movement minus .move, and
        // Dragging owns exactly .move. Neither may claim the other's paths.
        const AnimationPageScope motion = animationPageScope(QStringLiteral("animations-window-motion"));
        QCOMPARE(motion.include, QStringList{QStringLiteral("window.movement")});
        QCOMPARE(motion.exclude, QStringList{QStringLiteral("window.movement.move")});

        QVERIFY(animationPathInScope(QStringLiteral("window.movement"), motion));
        QVERIFY(animationPathInScope(QStringLiteral("window.movement.maximize"), motion));
        QVERIFY(animationPathInScope(QStringLiteral("window.movement.snapIn"), motion));
        QVERIFY(!animationPathInScope(QStringLiteral("window.movement.move"), motion));

        const AnimationPageScope dragging = animationPageScope(QStringLiteral("animations-window-dragging"));
        QVERIFY(animationPathInScope(QStringLiteral("window.movement.move"), dragging));
        QVERIFY(!animationPathInScope(QStringLiteral("window.movement.maximize"), dragging));
        // The parent itself belongs to Motion, never to Dragging.
        QVERIFY(!animationPathInScope(QStringLiteral("window.movement"), dragging));
    }

    void prefixMatchIsSegmentAware()
    {
        // "window.movementX" must NOT count as under "window.movement" — a
        // bare startsWith without the dot separator would leak it in.
        const QStringList roots{QStringLiteral("window.movement")};
        QVERIFY(animationPathUnderAny(QStringLiteral("window.movement"), roots));
        QVERIFY(animationPathUnderAny(QStringLiteral("window.movement.snapIn"), roots));
        QVERIFY(!animationPathUnderAny(QStringLiteral("window.movementX"), roots));
        QVERIFY(!animationPathUnderAny(QStringLiteral("window.appearance.open"), roots));
        QVERIFY(!animationPathUnderAny(QStringLiteral("window.movement"), QStringList{}));
    }

    void scopedBuiltInPathsRespectTheCarveOut()
    {
        const QStringList motion =
            animationScopedBuiltInPaths(animationPageScope(QStringLiteral("animations-window-motion")));
        QVERIFY(!motion.isEmpty());
        QVERIFY(motion.contains(QStringLiteral("window.movement.snapIn")));
        QVERIFY(!motion.contains(QStringLiteral("window.movement.move")));
        for (const QString& path : motion) {
            QVERIFY2(path.startsWith(QStringLiteral("window.movement")), qPrintable(path));
        }

        const QStringList dragging =
            animationScopedBuiltInPaths(animationPageScope(QStringLiteral("animations-window-dragging")));
        QCOMPARE(dragging, QStringList{QStringLiteral("window.movement.move")});
    }

    void treeDiffSeesOnlyItsOwnScope()
    {
        const AnimationPageScope motion = animationPageScope(QStringLiteral("animations-window-motion"));
        ShaderProfileTree baseline;
        ShaderProfileTree current;

        // Identical trees never differ.
        QVERIFY(!shaderTreeScopeDiffers(current, baseline, motion));

        // An edit OUTSIDE the scope must not register.
        current.setOverride(QStringLiteral("osd.show"), effectProfile(QStringLiteral("fade")));
        QVERIFY(!shaderTreeScopeDiffers(current, baseline, motion));

        // An edit on the excluded carve-out belongs to Dragging, not Motion.
        current.setOverride(QStringLiteral("window.movement.move"), effectProfile(QStringLiteral("wobble")));
        QVERIFY(!shaderTreeScopeDiffers(current, baseline, motion));
        QVERIFY(shaderTreeScopeDiffers(current, baseline,
                                       animationPageScope(QStringLiteral("animations-window-dragging"))));

        // An edit INSIDE the scope does register.
        current.setOverride(QStringLiteral("window.movement.snapIn"), effectProfile(QStringLiteral("slide")));
        QVERIFY(shaderTreeScopeDiffers(current, baseline, motion));
    }

    void treeDiffIsDirectionAgnostic()
    {
        // The walk unions both trees' overridden paths, so an override
        // present in the BASELINE and absent from current counts too —
        // otherwise a cleared override would read as clean.
        const AnimationPageScope motion = animationPageScope(QStringLiteral("animations-window-motion"));
        ShaderProfileTree baseline;
        baseline.setOverride(QStringLiteral("window.movement.snapIn"), effectProfile(QStringLiteral("slide")));
        const ShaderProfileTree current;
        QVERIFY(shaderTreeScopeDiffers(current, baseline, motion));
    }

    void nonSubtreeScopesYieldNothingFromTheSubtreeHelpers()
    {
        // Both helpers funnel through animationPathInScope, which requires a
        // non-empty `include`. So a WholeTree scope yields the EMPTY path set
        // and reports "no difference" — NOT "everything", which is what the
        // name suggests and what a caller forgetting to branch on scope.kind
        // would assume. Every current caller pre-branches, so this pins the
        // degenerate contract the header documents rather than a behaviour
        // anyone should rely on.
        const AnimationPageScope whole = animationPageScope(QStringLiteral("animations-presets"));
        QCOMPARE(whole.kind, AnimationPageScope::WholeTree);
        QVERIFY(animationScopedBuiltInPaths(whole).isEmpty());

        ShaderProfileTree current;
        current.setOverride(QStringLiteral("window.movement.snapIn"), effectProfile(QStringLiteral("slide")));
        const ShaderProfileTree baseline;
        // Trees plainly differ, yet a WholeTree scope reports no difference.
        QVERIFY(!shaderTreeScopeDiffers(current, baseline, whole));
    }
};

QTEST_MAIN(TestAnimationPageScope)
#include "test_animation_page_scope.moc"
