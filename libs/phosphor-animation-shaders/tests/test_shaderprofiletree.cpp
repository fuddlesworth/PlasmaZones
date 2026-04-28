// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/ShaderProfile.h>
#include <PhosphorAnimationShaders/ShaderProfileTree.h>

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
        tree.setOverride(PP::ZoneSnapIn, p);
        QVERIFY(tree.hasOverride(PP::ZoneSnapIn));

        QVERIFY(tree.clearOverride(PP::ZoneSnapIn));
        QVERIFY(!tree.hasOverride(PP::ZoneSnapIn));
        QVERIFY(!tree.clearOverride(PP::ZoneSnapIn));
    }

    void testClearAllOverrides()
    {
        ShaderProfileTree tree;
        ShaderProfile p;
        tree.setOverride(PP::Window, p);
        tree.setOverride(PP::Zone, p);

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
        original.setOverride(PP::ZoneSnapIn, overrideB);

        const QJsonObject encoded = original.toJson();
        const ShaderProfileTree restored = ShaderProfileTree::fromJson(encoded);

        QCOMPARE(restored, original);
    }

    void testJsonPreservesOverrideOrder()
    {
        ShaderProfileTree original;
        original.setOverride(PP::Window, ShaderProfile());
        original.setOverride(PP::Zone, ShaderProfile());
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
};

QTEST_MAIN(TestShaderProfileTree)
#include "test_shaderprofiletree.moc"
