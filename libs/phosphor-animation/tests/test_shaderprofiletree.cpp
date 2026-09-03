// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <PhosphorAnimation/ProfilePaths.h>

#include <QTest>

using PhosphorAnimationShaders::ShaderProfile;
using PhosphorAnimationShaders::ShaderProfileTree;

namespace PP = PhosphorAnimation::ProfilePaths;

class TestShaderProfileTree : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Resolve walk-up ───

    void testResolveEmptyTreeReturnsDefaults()
    {
        ShaderProfileTree tree;
        const ShaderProfile resolved = tree.resolve(PP::WindowOpen);
        QVERIFY(resolved.effectId.has_value());
        QVERIFY(resolved.effectId->isEmpty());
    }

    void testResolveBaselineFillsGaps()
    {
        ShaderProfileTree tree;
        ShaderProfile baseline;
        baseline.effectId = QStringLiteral("dissolve");
        tree.setBaseline(baseline);

        const ShaderProfile resolved = tree.resolve(PP::WindowOpen);
        QCOMPARE(*resolved.effectId, QStringLiteral("dissolve"));
    }

    void testResolveLeafOverrideWinsOverBaseline()
    {
        ShaderProfileTree tree;
        ShaderProfile baseline;
        baseline.effectId = QStringLiteral("dissolve");
        tree.setBaseline(baseline);

        ShaderProfile leaf;
        leaf.effectId = QStringLiteral("slide");
        tree.setOverride(PP::WindowOpen, leaf);

        const ShaderProfile resolved = tree.resolve(PP::WindowOpen);
        QCOMPARE(*resolved.effectId, QStringLiteral("slide"));
    }

    void testResolveCategoryOverrideInherited()
    {
        ShaderProfileTree tree;
        ShaderProfile category;
        category.effectId = QStringLiteral("morph");
        tree.setOverride(PP::Window, category);

        const ShaderProfile resolved = tree.resolve(PP::WindowOpen);
        QCOMPARE(*resolved.effectId, QStringLiteral("morph"));
    }

    void testResolveDesktopCategoryOverrideInherited()
    {
        // The "All Desktop Events" row writes an override at the `desktop`
        // family root and relies on it cascading to both leaves. That is the
        // row's entire contract — it is a real cascade parent, not a bulk
        // write to each leaf — so pin the walk-up here rather than only for
        // the window family.
        ShaderProfileTree tree;
        ShaderProfile category;
        category.effectId = QStringLiteral("desktop-fade");
        tree.setOverride(PP::Desktop, category);

        QCOMPARE(*tree.resolve(PP::DesktopPeek).effectId, QStringLiteral("desktop-fade"));
        QCOMPARE(*tree.resolve(PP::DesktopSwitch).effectId, QStringLiteral("desktop-fade"));
    }

    void testResolveLeafFillsFromCategoryThenBaseline()
    {
        ShaderProfileTree tree;

        ShaderProfile baseline;
        baseline.parameters = QVariantMap({{QStringLiteral("grain"), 0.5}});
        tree.setBaseline(baseline);

        ShaderProfile category;
        category.effectId = QStringLiteral("dissolve");
        tree.setOverride(PP::Window, category);

        ShaderProfile leaf;
        leaf.parameters = QVariantMap({{QStringLiteral("threshold"), 0.8}});
        tree.setOverride(PP::WindowClose, leaf);

        const ShaderProfile resolved = tree.resolve(PP::WindowClose);
        QCOMPARE(*resolved.effectId, QStringLiteral("dissolve"));
        QCOMPARE(resolved.parameters->value(QStringLiteral("threshold")).toDouble(), 0.8);
        // overlay replaces the entire parameter map — baseline's "grain"
        // is NOT merged into the leaf's engaged map
        QVERIFY(!resolved.parameters->contains(QStringLiteral("grain")));
    }

    void testResolveLeafCanExplicitlyDisableShader()
    {
        ShaderProfileTree tree;
        ShaderProfile baseline;
        baseline.effectId = QStringLiteral("dissolve");
        tree.setBaseline(baseline);

        ShaderProfile leaf;
        leaf.effectId = QString();
        tree.setOverride(PP::WindowOpen, leaf);

        const ShaderProfile resolved = tree.resolve(PP::WindowOpen);
        QVERIFY(resolved.effectId.has_value());
        QVERIFY(resolved.effectId->isEmpty());
    }

    // ─── Mutation ───

    void testSetOverrideInsertsOnce()
    {
        ShaderProfileTree tree;
        ShaderProfile p;
        p.effectId = QStringLiteral("dissolve");
        tree.setOverride(PP::WindowOpen, p);
        QCOMPARE(tree.overriddenPaths().size(), 1);

        p.effectId = QStringLiteral("slide");
        tree.setOverride(PP::WindowOpen, p);
        QCOMPARE(tree.overriddenPaths().size(), 1);
        QCOMPARE(*tree.directOverride(PP::WindowOpen).effectId, QStringLiteral("slide"));
    }

    void testClearOverride()
    {
        ShaderProfileTree tree;
        ShaderProfile p;
        p.effectId = QStringLiteral("glitch");
        tree.setOverride(PP::EditorSnapIn, p);
        QVERIFY(tree.hasOverride(PP::EditorSnapIn));

        QVERIFY(tree.clearOverride(PP::EditorSnapIn));
        QVERIFY(!tree.hasOverride(PP::EditorSnapIn));
        QVERIFY(!tree.clearOverride(PP::EditorSnapIn));
    }

    void testClearAllOverrides()
    {
        ShaderProfileTree tree;
        ShaderProfile p;
        tree.setOverride(PP::Window, p);
        tree.setOverride(PP::Editor, p);

        ShaderProfile baseline;
        baseline.effectId = QStringLiteral("dissolve");
        tree.setBaseline(baseline);

        tree.clearAllOverrides();
        QVERIFY(tree.overriddenPaths().isEmpty());
        QCOMPARE(*tree.baseline().effectId, QStringLiteral("dissolve"));
    }

    void testSetOverrideRejectsEmptyPath()
    {
        ShaderProfileTree tree;
        tree.setOverride(QString(), ShaderProfile());
        QVERIFY(tree.overriddenPaths().isEmpty());
    }

    // ─── Serialization ───

    void testJsonRoundTrip()
    {
        ShaderProfileTree original;

        ShaderProfile baseline;
        baseline.effectId = QStringLiteral("dissolve");
        original.setBaseline(baseline);

        ShaderProfile overrideA;
        overrideA.effectId = QStringLiteral("slide");
        overrideA.parameters = QVariantMap({{QStringLiteral("direction"), 1}});
        original.setOverride(PP::Window, overrideA);

        ShaderProfile overrideB;
        overrideB.effectId = QString();
        original.setOverride(PP::EditorSnapIn, overrideB);

        const QJsonObject encoded = original.toJson();
        const ShaderProfileTree restored = ShaderProfileTree::fromJson(encoded);

        QCOMPARE(restored, original);
    }

    void testJsonPreservesOverrideOrder()
    {
        ShaderProfileTree original;
        original.setOverride(PP::Window, ShaderProfile());
        original.setOverride(PP::Editor, ShaderProfile());
        original.setOverride(PP::Osd, ShaderProfile());

        const QStringList before = original.overriddenPaths();
        const ShaderProfileTree restored = ShaderProfileTree::fromJson(original.toJson());
        QCOMPARE(restored.overriddenPaths(), before);
    }

    // ─── Plugin-defined paths ───

    void testResolveWalksUpThroughNonTaxonomyPath()
    {
        ShaderProfileTree tree;
        ShaderProfile baseline;
        baseline.effectId = QStringLiteral("dissolve");
        tree.setBaseline(baseline);

        ShaderProfile category;
        category.parameters = QVariantMap({{QStringLiteral("grain"), 0.3}});
        tree.setOverride(QStringLiteral("widget.toast"), category);

        const ShaderProfile resolved = tree.resolve(QStringLiteral("widget.toast.slideIn"));
        QCOMPARE(*resolved.effectId, QStringLiteral("dissolve"));
        QCOMPARE(resolved.parameters->value(QStringLiteral("grain")).toDouble(), 0.3);
    }

    void testGlobalOverrideConsultedByResolve()
    {
        ShaderProfileTree tree;
        ShaderProfile global;
        global.effectId = QStringLiteral("glitch");
        tree.setOverride(QStringLiteral("global"), global);

        const ShaderProfile resolved = tree.resolve(PP::WindowOpen);
        QCOMPARE(*resolved.effectId, QStringLiteral("glitch"));
    }

    // ─── resolveShaderWithDefault: built-in per-event default ───

    // Named for the whole predicate rather than for the snap legs alone: it
    // grew past them long ago and now pins the three opt-in classes' absence
    // of a default too, which is the load-bearing half.
    void testDefaultShaderEffectIdForPath()
    {
        // Geometry events default to window-morph; others to none. Maximize is
        // in the set because the engines' own maximizes (scroll column
        // maximize-to-edges, monocle) ride it instead of snapIn.
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::WindowSnapIn), QStringLiteral("window-morph"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::WindowSnapOut), QStringLiteral("window-morph"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::WindowLayoutSwitch), QStringLiteral("window-morph"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::WindowMaximize), QStringLiteral("window-morph"));
        // The interactive-drag leaf carries NO default: a crossfade pack
        // cannot drive the held drag transition, and the move-class packs
        // (wobble) are opt-in.
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::WindowMove).isEmpty());
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::WindowOpen).isEmpty());
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::WindowClose).isEmpty());
        // The desktop transitions are OPT-IN by contract: both are intrusive
        // full-screen blends that contend with KWin's own effects, so a
        // regression adding a default here would silently auto-enable them on
        // fresh configs.
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::DesktopSwitch).isEmpty());
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::DesktopPeek).isEmpty());
        // The strip pass carries no built-in default either: a full-output
        // capture-and-repaint on every scroll stays opt-in, so a fresh config
        // scrolls with the plain translation until the user picks a strip
        // pack.
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::ScrollingView).isEmpty());
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::Scrolling).isEmpty());
    }

    void testDefaultShaderForPathOverlayEvents()
    {
        // OSD + popup show/hide events default to the fade shader; others none.
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::OsdShow), QStringLiteral("fade"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::OsdHide), QStringLiteral("fade"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::PopupZoneSelectorShow), QStringLiteral("fade"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::PopupZoneSelectorHide), QStringLiteral("fade"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::PopupLayoutPickerShow), QStringLiteral("fade"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::PopupLayoutPickerHide), QStringLiteral("fade"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::PopupSnapAssistShow), QStringLiteral("fade"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::PopupSnapAssistHide), QStringLiteral("fade"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::PopupCheatsheetShow), QStringLiteral("fade"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::PopupCheatsheetHide), QStringLiteral("fade"));
        // The category roots and the OSD "pop" event carry no built-in default.
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::Osd).isEmpty());
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::Popup).isEmpty());
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::OsdPop).isEmpty());
    }

    void testResolveWithDefaultUnsetOverlayEventGetsFade()
    {
        // Truly-unset overlay show/hide resolves to the built-in fade default;
        // an explicit per-event "None" is still respected.
        ShaderProfileTree tree;
        QCOMPARE(PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::OsdShow).effectiveEffectId(),
                 QStringLiteral("fade"));
        QCOMPARE(PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::PopupSnapAssistHide).effectiveEffectId(),
                 QStringLiteral("fade"));
        ShaderProfile none;
        none.effectId = QString();
        tree.setOverride(PP::OsdShow, none);
        QVERIFY(PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::OsdShow).effectiveEffectId().isEmpty());
    }

    void testResolveWithDefaultUnsetSnapEventGetsMorph()
    {
        // Truly-unset snap event resolves to the built-in window-morph default.
        ShaderProfileTree tree;
        const ShaderProfile r = PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowSnapIn);
        QCOMPARE(r.effectiveEffectId(), QStringLiteral("window-morph"));
    }

    void testResolveWithDefaultUnsetNonSnapEventStaysEmpty()
    {
        ShaderProfileTree tree;
        const ShaderProfile r = PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowOpen);
        QVERIFY(r.effectiveEffectId().isEmpty());
    }

    void testResolveWithDefaultUserOverrideWins()
    {
        // A real user override beats the built-in default.
        ShaderProfileTree tree;
        ShaderProfile leaf;
        leaf.effectId = QStringLiteral("slide");
        tree.setOverride(PP::WindowSnapIn, leaf);
        const ShaderProfile r = PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowSnapIn);
        QCOMPARE(r.effectiveEffectId(), QStringLiteral("slide"));
    }

    void testResolveWithDefaultExplicitNoneRespected()
    {
        // An explicit "None" (engaged-empty override) suppresses the default.
        ShaderProfileTree tree;
        ShaderProfile none;
        none.effectId = QString(); // engaged-empty
        tree.setOverride(PP::WindowSnapIn, none);
        const ShaderProfile r = PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowSnapIn);
        QVERIFY(r.effectiveEffectId().isEmpty());
    }

    void testResolveWithDefaultInheritedRealShaderRespected()
    {
        // An ancestor (window) that chose a REAL shader is inherited, not
        // replaced by the default.
        ShaderProfileTree tree;
        ShaderProfile cat;
        cat.effectId = QStringLiteral("dissolve");
        tree.setOverride(PP::Window, cat);
        const ShaderProfile r = PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowSnapIn);
        QCOMPARE(r.effectiveEffectId(), QStringLiteral("dissolve"));
    }

    void testResolveWithDefaultAncestorNoneDoesNotSuppressDefault()
    {
        // Regression: a category-level "None" ("window" → "") must NOT suppress
        // the per-event built-in default — a snap event still defaults to
        // window-morph. (Only a per-event override suppresses it.)
        ShaderProfileTree tree;
        ShaderProfile catNone;
        catNone.effectId = QString(); // engaged-empty "None" at the category
        tree.setOverride(PP::Window, catNone);
        // A snap event without its own override still gets the default.
        QCOMPARE(PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowSnapIn).effectiveEffectId(),
                 QStringLiteral("window-morph"));
        // A non-snap window event (no built-in default) correctly stays None
        // under the category None.
        QVERIFY(
            PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowMinimize).effectiveEffectId().isEmpty());
        // A per-event None under the category still wins (no default).
        ShaderProfile leafNone;
        leafNone.effectId = QString();
        tree.setOverride(PP::WindowSnapIn, leafNone);
        QVERIFY(
            PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowSnapIn).effectiveEffectId().isEmpty());
    }

    void testResolveWithDefaultParamsOnlyLeafStillGetsDefault()
    {
        // A params-only leaf override (parameters set, effectId UNSET) is "no
        // shader chosen" — the per-event default still applies, and the user's
        // params overlay onto it. (Gated on the leaf's effectId engagement, not
        // merely hasOverride.)
        ShaderProfileTree tree;
        ShaderProfile paramsOnly;
        paramsOnly.parameters = QVariantMap({{QStringLiteral("speed"), 0.5}});
        tree.setOverride(PP::WindowSnapIn, paramsOnly);
        const ShaderProfile r = PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowSnapIn);
        QCOMPARE(r.effectiveEffectId(), QStringLiteral("window-morph"));
        QVERIFY(r.parameters.has_value());
        QCOMPARE(r.parameters->value(QStringLiteral("speed")).toDouble(), 0.5);
    }

    void testResolveMoveLeafTakesNoInheritedShader()
    {
        // The interactive-drag leaf (window.movement.move) takes NO inherited
        // shader: an ancestor pick is by construction a crossfade pack that
        // cannot drive the held drag, so resolve() reads only the direct leaf
        // override. Sibling legs inherit normally.
        ShaderProfileTree tree;
        ShaderProfile parent;
        parent.effectId = QStringLiteral("ripple-snap");
        tree.setOverride(PP::WindowMovement, parent);
        QVERIFY(tree.resolve(PP::WindowMove).effectiveEffectId().isEmpty());
        QCOMPARE(tree.resolve(PP::WindowSnapIn).effectiveEffectId(), QStringLiteral("ripple-snap"));
        // resolveShaderWithDefault injects no default either (move has none).
        QVERIFY(PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowMove).effectiveEffectId().isEmpty());
        // A direct leaf override applies untouched.
        ShaderProfile leaf;
        leaf.effectId = QStringLiteral("wobble");
        tree.setOverride(PP::WindowMove, leaf);
        QCOMPARE(tree.resolve(PP::WindowMove).effectiveEffectId(), QStringLiteral("wobble"));
        // Isolation cuts both ways in outcome: the leaf override does not
        // bleed into sibling or ancestor resolution either.
        QCOMPARE(tree.resolve(PP::WindowSnapIn).effectiveEffectId(), QStringLiteral("ripple-snap"));
        QCOMPARE(tree.resolve(PP::WindowMovement).effectiveEffectId(), QStringLiteral("ripple-snap"));
        // The baseline is skipped too, exactly like named ancestors.
        tree.clearOverride(PP::WindowMove);
        ShaderProfile base;
        base.effectId = QStringLiteral("fade");
        tree.setBaseline(base);
        QVERIFY(tree.resolve(PP::WindowMove).effectiveEffectId().isEmpty());
    }

    void testResolveMoveLeafParamsOnlyStaysEmpty()
    {
        // A params-only override on the MOVE leaf (parameters set, effectId
        // unset) resolves to an EMPTY effect id even through
        // resolveShaderWithDefault: the leaf takes no inherited shader AND
        // carries no built-in default, so unlike the snap-event case
        // (testResolveWithDefaultParamsOnlyLeafStillGetsDefault) there is
        // nothing for the params to overlay onto. The params themselves
        // still survive on the resolved profile.
        ShaderProfileTree tree;
        ShaderProfile paramsOnly;
        paramsOnly.parameters = QVariantMap({{QStringLiteral("strength"), 2.0}});
        tree.setOverride(PP::WindowMove, paramsOnly);
        const ShaderProfile r = PhosphorAnimationShaders::resolveShaderWithDefault(tree, PP::WindowMove);
        QVERIFY(r.effectiveEffectId().isEmpty());
        QVERIFY(r.parameters.has_value());
        QCOMPARE(r.parameters->value(QStringLiteral("strength")).toDouble(), 2.0);
    }

    void testShaderPathResolvesInIsolation()
    {
        // The isolation predicate is the SSOT shared by resolve() and the
        // settings shadowing-children walk: the interactive-drag leaf and the
        // scrolling tab swap, never their parents, siblings, or unrelated
        // paths.
        QVERIFY(PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::WindowMove));
        // The tab leaf's membership is what keeps a strip-class pack set on
        // `scrolling` from being inherited by a leaf whose applicability gate
        // would then refuse it at install — the leaf would animate nothing
        // while settings showed an inherited "current shader" that never
        // runs. Dropping the membership compiles and passes everything else,
        // which is exactly why it is pinned here.
        QVERIFY(PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::ScrollingTabSwitch));
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::WindowMovement));
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::WindowSnapIn));
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::WindowOpen));
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(QString()));
        // Both desktop leaves must stay NON-isolated. The predicate is a single
        // hardcoded equality, so extending it to a desktop leaf would compile
        // and pass everything else while silently killing the "All Desktop
        // Events" cascade row, whose whole premise is that neither leaf
        // resolves in isolation (AnimationsDesktopsPage.qml). Pin it here.
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::DesktopPeek));
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::DesktopSwitch));
        // The strip leaf too. It is the class most easily confused with
        // `move` (both are continuous motion), and move IS the isolated one,
        // so a fix that generalised the predicate from the move leaf to "the
        // continuous classes" would take the strip leaf's inheritance with
        // it.
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::ScrollingView));
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::Scrolling));
    }

    // The other half of the same property: because the strip leaf is NOT
    // isolated, a shader set on the `scrolling` root cascades down to it.
    // That is what makes the picker's ambiguous-row policy a policy rather
    // than a proof, and what the settings page's parent-row decision rests
    // on, so pin the cascade itself rather than only the predicate.
    void testScrollingRootCascadesToView()
    {
        ShaderProfileTree tree;
        ShaderProfile root;
        root.effectId = QStringLiteral("strip-motion-blur");
        tree.setOverride(PP::Scrolling, root);

        const ShaderProfile resolved = tree.resolve(PP::ScrollingView);
        QVERIFY(resolved.effectId.has_value());
        QCOMPARE(*resolved.effectId, QStringLiteral("strip-motion-blur"));
    }

    void testShellSubtreeTakesNothingFromAboveItsRoot()
    {
        // The whole point of the shell root: the surfaces under it belong to
        // plasmashell, so what the user chose for their own windows must never
        // start playing on the system tray. Both routes in are pinned, because
        // the resolver needs two separate edits to close them and each one
        // passes every other test on its own — the baseline (the tree's global
        // default) and the `global` NODE, which is a real chain member the
        // user can assign a pack to.
        ShaderProfileTree tree;
        ShaderProfile baseline;
        baseline.effectId = QStringLiteral("dissolve");
        tree.setBaseline(baseline);
        ShaderProfile globalNode;
        globalNode.effectId = QStringLiteral("slide");
        tree.setOverride(PP::Global, globalNode);

        QVERIFY(tree.resolve(PP::Shell).effectiveEffectId().isEmpty());
        QVERIFY(tree.resolve(PP::ShellAppletPopupShow).effectiveEffectId().isEmpty());
        QVERIFY(tree.resolve(PP::ShellAppletPopupHide).effectiveEffectId().isEmpty());
        // And nothing about the isolation leaks onto the window family, which
        // still inherits both. Both halves need their own tree: on the tree above
        // the `global` node shadows the baseline, so that assertion alone observes
        // only the node and would still pass if the baseline had been cut for
        // everyone.
        QCOMPARE(tree.resolve(PP::WindowOpen).effectiveEffectId(), QStringLiteral("slide"));

        ShaderProfileTree baselineOnly;
        baselineOnly.setBaseline(baseline);
        QCOMPARE(baselineOnly.resolve(PP::WindowOpen).effectiveEffectId(), QStringLiteral("dissolve"));
        // The same tree also pins the baseline-drop edit on its own, with no
        // `global` node present for the chain trim to be doing the work instead.
        QVERIFY(baselineOnly.resolve(PP::Shell).effectiveEffectId().isEmpty());
        QVERIFY(baselineOnly.resolve(PP::ShellAppletPopupShow).effectiveEffectId().isEmpty());
    }

    void testShellRootCascadesWithinItsOwnSubtree()
    {
        // Isolation is not the same as no inheritance. Inside the subtree the
        // ordinary cascade holds, which is what makes the page's "All Shell
        // Surfaces" row mean anything: set a pack there and both legs take it,
        // then a leg can override it. shaderPathResolvesInIsolation (the
        // leaf-only predicate) would have killed exactly this, which is why
        // the shell family needed the subtree form instead.
        ShaderProfileTree tree;
        ShaderProfile root;
        root.effectId = QStringLiteral("dissolve");
        tree.setOverride(PP::Shell, root);

        QCOMPARE(tree.resolve(PP::ShellAppletPopupShow).effectiveEffectId(), QStringLiteral("dissolve"));
        QCOMPARE(tree.resolve(PP::ShellAppletPopupHide).effectiveEffectId(), QStringLiteral("dissolve"));

        ShaderProfile leg;
        leg.effectId = QStringLiteral("pixelate");
        tree.setOverride(PP::ShellAppletPopupHide, leg);
        QCOMPARE(tree.resolve(PP::ShellAppletPopupShow).effectiveEffectId(), QStringLiteral("dissolve"));
        QCOMPARE(tree.resolve(PP::ShellAppletPopupHide).effectiveEffectId(), QStringLiteral("pixelate"));
    }

    void testShellIsolationRootIsTheSharedDefinition()
    {
        // The predicate resolve() uses, exposed for the shadowing-aware
        // consumers so a second copy of "where does inheritance start" cannot
        // drift from the resolver. Every path in the subtree answers the root
        // itself, and nothing outside it answers at all — including the
        // character-prefix near-miss, which is the failure this boundary test
        // exists for.
        QCOMPARE(PhosphorAnimationShaders::shaderPathIsolationRoot(PP::Shell), PP::Shell);
        QCOMPARE(PhosphorAnimationShaders::shaderPathIsolationRoot(PP::ShellAppletPopup), PP::Shell);
        QCOMPARE(PhosphorAnimationShaders::shaderPathIsolationRoot(PP::ShellAppletPopupShow), PP::Shell);
        QVERIFY(PhosphorAnimationShaders::shaderPathIsolationRoot(PP::WindowOpen).isEmpty());
        QVERIFY(PhosphorAnimationShaders::shaderPathIsolationRoot(PP::Global).isEmpty());
        QVERIFY(PhosphorAnimationShaders::shaderPathIsolationRoot(QString()).isEmpty());
        QVERIFY(PhosphorAnimationShaders::shaderPathIsolationRoot(QStringLiteral("shellfish")).isEmpty());
        // The shell paths are NOT leaf-isolated: the two predicates answer
        // different questions and a "simplification" that folded them would
        // break the cascade the test above pins.
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::ShellAppletPopupShow));
    }
};

QTEST_MAIN(TestShaderProfileTree)
#include "test_shaderprofiletree.moc"
