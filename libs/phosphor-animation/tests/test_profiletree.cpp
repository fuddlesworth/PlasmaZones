// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ProfileTree.h>
#include <PhosphorAnimation/Spring.h>

#include <QTest>

using PhosphorAnimation::Easing;
using PhosphorAnimation::Profile;
using PhosphorAnimation::ProfileTree;
using PhosphorAnimation::SequenceMode;
using PhosphorAnimation::Spring;

namespace PP = PhosphorAnimation::ProfilePaths;

class TestProfileTree : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Path walking ───

    void testParentPath()
    {
        QCOMPARE(PP::parentPath(PP::WindowOpen), PP::Window);
        QCOMPARE(PP::parentPath(PP::Window), PP::Global);
        QCOMPARE(PP::parentPath(PP::Global), QString());
        QCOMPARE(PP::parentPath(QString()), QString());
    }

    void testAllBuiltInPathsNonEmpty()
    {
        const QStringList paths = PP::allBuiltInPaths();
        QVERIFY(!paths.isEmpty());
        QVERIFY(paths.contains(PP::Global));
        QVERIFY(paths.contains(PP::WindowOpen));
        QVERIFY(paths.contains(PP::ZoneSnapIn));
    }

    // ─── Resolve walk-up ───

    void testResolveEmptyTreeReturnsLibraryDefaults()
    {
        ProfileTree tree;
        const Profile resolved = tree.resolve(PP::WindowOpen);
        // No override, no baseline → library defaults filled by withDefaults().
        QCOMPARE(*resolved.duration, Profile::DefaultDuration);
        QCOMPARE(*resolved.staggerInterval, Profile::DefaultStaggerInterval);
        QVERIFY(resolved.curve == nullptr);
    }

    void testResolveBaselineFillsGaps()
    {
        ProfileTree tree;
        Profile baseline;
        baseline.duration = 400.0;
        baseline.curve = std::make_shared<Easing>();
        tree.setBaseline(baseline);

        const Profile resolved = tree.resolve(PP::WindowOpen);
        QCOMPARE(*resolved.duration, 400.0);
        QVERIFY(resolved.curve != nullptr);
        // Fields not in baseline fill from library defaults.
        QCOMPARE(*resolved.staggerInterval, Profile::DefaultStaggerInterval);
    }

    void testResolveLeafOverrideWinsOverBaseline()
    {
        ProfileTree tree;
        Profile baseline;
        baseline.duration = 150.0;
        tree.setBaseline(baseline);

        Profile leaf;
        leaf.duration = 80.0;
        leaf.curve = std::make_shared<Spring>(Spring::snappy());
        tree.setOverride(PP::WindowOpen, leaf);

        const Profile resolved = tree.resolve(PP::WindowOpen);
        QCOMPARE(*resolved.duration, 80.0);
        QCOMPARE(resolved.curve->typeId(), QStringLiteral("spring"));
    }

    void testResolveCategoryOverrideInherited()
    {
        ProfileTree tree;

        Profile category;
        category.duration = 200.0;
        tree.setOverride(PP::Window, category);

        const Profile resolved = tree.resolve(PP::WindowOpen);
        QCOMPARE(*resolved.duration, 200.0);
    }

    void testResolveLeafFillsFromCategoryThenBaseline()
    {
        ProfileTree tree;

        Profile baseline;
        baseline.curve = std::make_shared<Easing>();
        tree.setBaseline(baseline);

        Profile category;
        category.staggerInterval = 60;
        tree.setOverride(PP::Window, category);

        Profile leaf;
        leaf.duration = 99.0;
        tree.setOverride(PP::WindowClose, leaf);

        const Profile resolved = tree.resolve(PP::WindowClose);
        QCOMPARE(*resolved.duration, 99.0); // from leaf
        QCOMPARE(*resolved.staggerInterval, 60); // from category
        QVERIFY(resolved.curve != nullptr); // from baseline
    }

    void testResolveLeafCanResetToLibraryDefault()
    {
        // REGRESSION: with sentinel-based inheritance, setting a child
        // override field to the same value as the library default was
        // silently treated as "unset" and the parent's value leaked
        // through. With std::optional fields, an explicitly-set default
        // value MUST win over the parent.
        ProfileTree tree;

        Profile baseline;
        baseline.duration = 500.0;
        baseline.staggerInterval = 75;
        baseline.minDistance = 25;
        baseline.sequenceMode = SequenceMode::Cascade;
        tree.setBaseline(baseline);

        Profile leaf;
        leaf.duration = Profile::DefaultDuration; // 150
        leaf.staggerInterval = Profile::DefaultStaggerInterval; // 30
        leaf.minDistance = Profile::DefaultMinDistance; // 0
        leaf.sequenceMode = Profile::DefaultSequenceMode; // AllAtOnce
        tree.setOverride(PP::WindowOpen, leaf);

        const Profile resolved = tree.resolve(PP::WindowOpen);
        QCOMPARE(*resolved.duration, Profile::DefaultDuration);
        QCOMPARE(*resolved.staggerInterval, Profile::DefaultStaggerInterval);
        QCOMPARE(*resolved.minDistance, Profile::DefaultMinDistance);
        QCOMPARE(*resolved.sequenceMode, Profile::DefaultSequenceMode);
    }

    // ─── Mutation ───

    void testSetOverrideInsertsOnce()
    {
        ProfileTree tree;
        Profile p;
        p.duration = 200.0;
        tree.setOverride(PP::WindowOpen, p);

        QCOMPARE(tree.overriddenPaths().size(), 1);

        p.duration = 300.0;
        tree.setOverride(PP::WindowOpen, p);
        QCOMPARE(tree.overriddenPaths().size(), 1);
        QCOMPARE(*tree.directOverride(PP::WindowOpen).duration, 300.0);
    }

    void testClearOverride()
    {
        ProfileTree tree;
        Profile p;
        p.duration = 999.0;
        tree.setOverride(PP::ZoneSnapIn, p);
        QVERIFY(tree.hasOverride(PP::ZoneSnapIn));

        QVERIFY(tree.clearOverride(PP::ZoneSnapIn));
        QVERIFY(!tree.hasOverride(PP::ZoneSnapIn));
        QVERIFY(tree.overriddenPaths().isEmpty());

        QVERIFY(!tree.clearOverride(PP::ZoneSnapIn));
    }

    void testClearAllOverrides()
    {
        ProfileTree tree;
        Profile p;
        tree.setOverride(PP::Window, p);
        tree.setOverride(PP::Zone, p);
        tree.setOverride(PP::Osd, p);

        Profile baseline;
        baseline.duration = 123.0;
        tree.setBaseline(baseline);

        tree.clearAllOverrides();
        QVERIFY(tree.overriddenPaths().isEmpty());
        QCOMPARE(*tree.baseline().duration, 123.0);
    }

    void testSetOverrideRejectsEmptyPath()
    {
        ProfileTree tree;
        Profile p;
        tree.setOverride(QString(), p);
        QVERIFY(tree.overriddenPaths().isEmpty());
    }

    // ─── Serialization ───

    void testJsonRoundTrip()
    {
        ProfileTree original;

        Profile baseline;
        baseline.curve = std::make_shared<Spring>(Spring::smooth());
        baseline.duration = 180.0;
        original.setBaseline(baseline);

        Profile overrideA;
        overrideA.duration = 500.0;
        overrideA.staggerInterval = 75;
        original.setOverride(PP::Window, overrideA);

        Profile overrideB;
        overrideB.curve = std::make_shared<Easing>();
        overrideB.sequenceMode = SequenceMode::Cascade;
        original.setOverride(PP::ZoneSnapIn, overrideB);

        const QJsonObject encoded = original.toJson();
        const ProfileTree restored = ProfileTree::fromJson(encoded, PhosphorAnimation::CurveRegistry{});

        QCOMPARE(restored, original);
    }

    void testJsonPreservesOverrideOrder()
    {
        ProfileTree original;
        original.setOverride(PP::Window, Profile());
        original.setOverride(PP::Zone, Profile());
        original.setOverride(PP::Osd, Profile());

        const QStringList before = original.overriddenPaths();
        const ProfileTree restored = ProfileTree::fromJson(original.toJson(), PhosphorAnimation::CurveRegistry{});
        QCOMPARE(restored.overriddenPaths(), before);
    }

    void testJsonExplicitDefaultSurvivesRoundTrip()
    {
        // REGRESSION: a Profile with duration set to the library default
        // must survive the JSON round-trip with the field still engaged.
        ProfileTree original;
        Profile leaf;
        leaf.duration = Profile::DefaultDuration;
        original.setOverride(PP::WindowOpen, leaf);

        const ProfileTree restored = ProfileTree::fromJson(original.toJson(), PhosphorAnimation::CurveRegistry{});
        const Profile restoredLeaf = restored.directOverride(PP::WindowOpen);
        QVERIFY(restoredLeaf.duration.has_value());
        QCOMPARE(*restoredLeaf.duration, Profile::DefaultDuration);
    }

    // ─── Plugin-defined paths ───

    void testResolveWalksUpThroughNonTaxonomyPath()
    {
        // The doc promises that plugin-defined dot-paths (outside the
        // built-in taxonomy) resolve through `parentPath()` up to
        // `global`. Verify with a `widget.toast.slideIn`-style chain.
        ProfileTree tree;
        Profile baseline;
        baseline.duration = 500.0;
        tree.setBaseline(baseline);

        Profile categoryOverride;
        categoryOverride.staggerInterval = 77;
        tree.setOverride(QStringLiteral("widget.toast"), categoryOverride);

        const Profile resolved = tree.resolve(QStringLiteral("widget.toast.slideIn"));
        QCOMPARE(*resolved.duration, 500.0); // from baseline
        QCOMPARE(*resolved.staggerInterval, 77); // from widget.toast
    }

    // ─── Preset name inheritance (optional semantics) ───

    void testPresetNameEngagedEmptyOverridesParent()
    {
        // With presetName as std::optional<QString>, an engaged-but-empty
        // leaf must win over a non-empty parent — same semantics as every
        // other optional field.
        ProfileTree tree;
        Profile baseline;
        baseline.presetName = QStringLiteral("Default Preset");
        tree.setBaseline(baseline);

        Profile leaf;
        leaf.presetName = QString(); // engaged, empty — "explicitly clear"
        tree.setOverride(PP::WindowOpen, leaf);

        const Profile resolved = tree.resolve(PP::WindowOpen);
        QVERIFY(resolved.presetName.has_value());
        QVERIFY(resolved.presetName->isEmpty());
    }

    void testPresetNameNulloptInheritsParent()
    {
        ProfileTree tree;
        Profile baseline;
        baseline.presetName = QStringLiteral("From Baseline");
        tree.setBaseline(baseline);

        Profile leaf;
        leaf.duration = 200.0; // presetName stays nullopt
        tree.setOverride(PP::WindowOpen, leaf);

        const Profile resolved = tree.resolve(PP::WindowOpen);
        QVERIFY(resolved.presetName.has_value());
        QCOMPARE(*resolved.presetName, QStringLiteral("From Baseline"));
    }
};

QTEST_MAIN(TestProfileTree)
#include "test_profiletree.moc"
