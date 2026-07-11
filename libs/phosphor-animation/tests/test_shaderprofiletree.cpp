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

    void testDefaultShaderForPathSnapEvents()
    {
        // Snap events default to window-morph; others to none.
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::WindowSnapIn), QStringLiteral("window-morph"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::WindowSnapOut), QStringLiteral("window-morph"));
        QCOMPARE(PP::defaultShaderEffectIdForPath(PP::WindowLayoutSwitch), QStringLiteral("window-morph"));
        // The interactive-drag leaf carries NO default: a crossfade pack
        // cannot drive the held drag transition, and the move-class packs
        // (wobble) are opt-in.
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::WindowMove).isEmpty());
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::WindowOpen).isEmpty());
        QVERIFY(PP::defaultShaderEffectIdForPath(PP::WindowClose).isEmpty());
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
        // A non-move window event correctly stays None under the category None.
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
        // settings shadowing-children walk: exactly the interactive-drag
        // leaf, never its parent, siblings, or unrelated paths.
        QVERIFY(PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::WindowMove));
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::WindowMovement));
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::WindowSnapIn));
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(PP::WindowOpen));
        QVERIFY(!PhosphorAnimationShaders::shaderPathResolvesInIsolation(QString()));
    }
};

QTEST_MAIN(TestShaderProfileTree)
#include "test_shaderprofiletree.moc"
