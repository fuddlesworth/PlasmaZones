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

#include "settings/animationpagescope.h"

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
        // Presets / motion sets / shaders act on the entire editable tree,
        // as does the condensed simple page (it spans several roots plus the
        // General keys, so no single subtree fits).
        for (const auto* page : {"animations-presets", "animations-motionsets", "animations-shaders",
                                 "animations-simple", "not-an-animation-page"}) {
            const AnimationPageScope scope = animationPageScope(QLatin1String(page));
            QCOMPARE(scope.kind, AnimationPageScope::WholeTree);
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
};

QTEST_MAIN(TestAnimationPageScope)
#include "test_animation_page_scope.moc"
