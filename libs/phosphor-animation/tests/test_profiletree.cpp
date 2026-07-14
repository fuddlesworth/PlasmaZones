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
        QCOMPARE(PP::parentPath(PP::WindowOpen), PP::WindowAppearance);
        QCOMPARE(PP::parentPath(PP::WindowAppearance), PP::Window);
        QCOMPARE(PP::parentPath(PP::WindowMove), PP::WindowMovement);
        QCOMPARE(PP::parentPath(PP::WindowMovement), PP::Window);
        QCOMPARE(PP::parentPath(PP::Window), PP::Global);
        // Desktop transition family walks up desktop.switch / desktop.peek →
        // desktop → global.
        QCOMPARE(PP::parentPath(PP::DesktopSwitch), PP::Desktop);
        QCOMPARE(PP::parentPath(PP::DesktopPeek), PP::Desktop);
        QCOMPARE(PP::parentPath(PP::Desktop), PP::Global);
        QCOMPARE(PP::parentPath(PP::Global), QString());
        QCOMPARE(PP::parentPath(QString()), QString());
    }

    // The desktop-family classifier underpins the whole opt-in policy: the
    // desktop.switch and desktop.peek paths must classify as EventClassDesktop
    // so the two-texture packs surface only there, and desktop's parent
    // 'global' must NOT.
    void testEventClassForDesktopPaths()
    {
        QCOMPARE(PP::eventClassForPath(PP::DesktopSwitch), PP::EventClassDesktop);
        // The show-desktop peek leaf shares the desktop class so the same
        // two-texture packs drive it.
        QCOMPARE(PP::eventClassForPath(PP::DesktopPeek), PP::EventClassDesktop);
        QCOMPARE(PP::eventClassForPath(PP::Desktop), PP::EventClassDesktop);
        // A geometry/appearance path never classifies as desktop.
        QVERIFY(PP::eventClassForPath(PP::WindowOpen) != PP::EventClassDesktop);
        QVERIFY(PP::eventClassForPath(PP::WindowMove) != PP::EventClassDesktop);
        // Global root carries no single class.
        QCOMPARE(PP::eventClassForPath(PP::Global), QString());
    }

    // The move classifier underpins the drag opt-in policy: only the
    // interactive-drag LEAF classifies as EventClassMove — its cascade parent
    // and every sibling crossfade leg stay geometry, so the move-physics
    // packs surface only on the "Moved" row.
    void testEventClassForMovePath()
    {
        QCOMPARE(PP::eventClassForPath(PP::WindowMove), PP::EventClassMove);
        QCOMPARE(PP::eventClassForPath(PP::WindowMovement), PP::EventClassGeometry);
        QCOMPARE(PP::eventClassForPath(PP::WindowSnapIn), PP::EventClassGeometry);
        QCOMPARE(PP::eventClassForPath(PP::WindowMaximize), PP::EventClassGeometry);
    }

    void testAllBuiltInPathsNonEmpty()
    {
        const QStringList paths = PP::allBuiltInPaths();
        QVERIFY(!paths.isEmpty());
        QVERIFY(paths.contains(PP::Global));
        QVERIFY(paths.contains(PP::WindowOpen));
        QVERIFY(paths.contains(PP::EditorSnapIn));
    }

    // Pin the post-rename taxonomy: every new path constant from the
    // zone.* → window.* / editor.* / widget.* split must appear in
    // allBuiltInPaths(), and no legacy zone.* string may slip back in.
    void testPostRenameTaxonomyComplete()
    {
        const QStringList paths = PP::allBuiltInPaths();
        // Editor family (layout-editor zone-rect animations).
        QVERIFY(paths.contains(PP::Editor));
        QVERIFY(paths.contains(PP::EditorSnapIn));
        QVERIFY(paths.contains(PP::EditorSnapOut));
        QVERIFY(paths.contains(PP::EditorSnapResize));
        // Window snap family (kwin-effect window-quad animations). There are
        // deliberately NO resize paths — window.movement.resize and
        // window.movement.snapResize were dropped from the taxonomy.
        QVERIFY(paths.contains(PP::WindowSnapIn));
        QVERIFY(paths.contains(PP::WindowSnapOut));
        QVERIFY(paths.contains(PP::WindowLayoutSwitch));
        QVERIFY(!paths.contains(QStringLiteral("window.movement.resize")));
        QVERIFY(!paths.contains(QStringLiteral("window.movement.snapResize")));
        // Widget zone-rect highlight family.
        QVERIFY(paths.contains(PP::WidgetZoneHighlight));
        QVERIFY(paths.contains(PP::WidgetZoneHighlightPop));
        QVERIFY(paths.contains(PP::WidgetZoneHighlightBorder));
        QVERIFY(paths.contains(PP::WidgetZoneOverlayFlash));
        // Desktop transition family (full-screen two-texture blends).
        QVERIFY(paths.contains(PP::Desktop));
        QVERIFY(paths.contains(PP::DesktopSwitch));
        QVERIFY(paths.contains(PP::DesktopPeek));
        // No regression: legacy zone.* strings must not reappear.
        for (const QString& path : paths) {
            QVERIFY2(!path.startsWith(QLatin1String("zone.")) && path != QLatin1String("zone"),
                     qPrintable(QStringLiteral("legacy zone.* path leaked back into allBuiltInPaths(): ") + path));
        }
    }

    // ─── Resolve walk-up ───

    void testResolveEmptyTreeReturnsLibraryDefaults()
    {
        ProfileTree tree;
        const Profile resolved = tree.resolve(PP::WindowOpen);
        // No override, no baseline → library defaults filled by
        // withDefaults(). `withDefaults` now fills every field including
        // `curve` (default-constructed Easing = OutCubic cubic-bezier);
        // callers no longer need to substitute a fallback themselves.
        QCOMPARE(*resolved.duration, Profile::DefaultDuration);
        QCOMPARE(*resolved.staggerInterval, Profile::DefaultStaggerInterval);
        QVERIFY(resolved.curve != nullptr);
        QCOMPARE(resolved.curve->typeId(), QStringLiteral("bezier"));
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

    // ─── overlayChainOnto ───

    // The KWin effect holds the authoritative "global" motion profile in its
    // WindowAnimator and needs only the per-event OVERRIDES layered on top —
    // NOT the tree's own baseline. This is the movement-duration regression:
    // a `window.movement` "All" override (curve + duration) must reach a
    // `window.movement.snapIn` geometry animation.
    void testOverlayChainOntoAppliesMovementAllToLeaf()
    {
        ProfileTree tree;

        // Tree baseline is deliberately DIFFERENT from the caller's base to
        // prove the baseline is ignored (the effect owns global elsewhere).
        Profile treeBaseline;
        treeBaseline.duration = 150.0;
        tree.setBaseline(treeBaseline);

        // The user's "All" window.movement override: 500 ms + a spring curve.
        Profile movementAll;
        movementAll.duration = 500.0;
        movementAll.curve = std::make_shared<Spring>(Spring::snappy());
        tree.setOverride(PP::WindowMovement, movementAll);

        // Caller base = the animator's global profile (300 ms, easing).
        Profile animatorGlobal;
        animatorGlobal.duration = 300.0;
        animatorGlobal.curve = std::make_shared<Easing>();

        const Profile composed = tree.overlayChainOnto(PP::WindowSnapIn, animatorGlobal);
        // Both axes come from the movement "All" node, NOT the tree baseline
        // and NOT the caller's global.
        QCOMPARE(*composed.duration, 500.0);
        QVERIFY(composed.curve != nullptr);
        QVERIFY(std::dynamic_pointer_cast<const Spring>(composed.curve) != nullptr);
    }

    // The "All Desktop Events" node is a REAL cascade parent for MOTION, not
    // just for the shader pick: a `desktop` curve/duration override must reach
    // BOTH screen-level leaves. The kwin-effect composes each leg's timing
    // through this exact call (overlayChainOnto onto the animator's global
    // profile) in the desktopChanged / showingDesktopChanged handlers, so
    // without this the peek and the switch could silently ignore the parent
    // the settings page presents as governing them.
    void testOverlayChainOntoAppliesDesktopAllToBothLeaves()
    {
        ProfileTree tree;

        // Baseline deliberately differs from the caller's base, to prove the
        // tree's own baseline stays out of it (the effect owns global).
        Profile treeBaseline;
        treeBaseline.duration = 150.0;
        tree.setBaseline(treeBaseline);

        Profile desktopAll;
        desktopAll.duration = 500.0;
        desktopAll.curve = std::make_shared<Spring>(Spring::snappy());
        tree.setOverride(PP::Desktop, desktopAll);

        Profile animatorGlobal;
        animatorGlobal.duration = 300.0;
        animatorGlobal.curve = std::make_shared<Easing>();

        for (const QString& leaf : {PP::DesktopSwitch, PP::DesktopPeek}) {
            const Profile composed = tree.overlayChainOnto(leaf, animatorGlobal);
            QVERIFY2(composed.duration.has_value(), qPrintable(leaf));
            QCOMPARE(*composed.duration, 500.0);
            QVERIFY2(std::dynamic_pointer_cast<const Spring>(composed.curve) != nullptr, qPrintable(leaf));
        }
    }

    // A leaf override beats the desktop "All" parent on the leaf that sets it,
    // and leaves its sibling on the parent's value. This is what makes the two
    // legs independently tunable under one parent — the peek can run its own
    // duration without retiming the switch.
    void testOverlayChainOntoDesktopLeafBeatsAllForThatLeafOnly()
    {
        ProfileTree tree;

        Profile desktopAll;
        desktopAll.duration = 500.0;
        tree.setOverride(PP::Desktop, desktopAll);

        Profile peekLeaf;
        peekLeaf.duration = 80.0;
        tree.setOverride(PP::DesktopPeek, peekLeaf);

        Profile animatorGlobal;
        animatorGlobal.duration = 300.0;

        QCOMPARE(*tree.overlayChainOnto(PP::DesktopPeek, animatorGlobal).duration, 80.0); // leaf
        QCOMPARE(*tree.overlayChainOnto(PP::DesktopSwitch, animatorGlobal).duration, 500.0); // parent "All"
    }

    // Leaf override wins over the "All" parent; a field the leaf leaves unset
    // still inherits the parent's; a field neither sets keeps the caller base.
    void testOverlayChainOntoLeafBeatsParentKeepsBaseForUnset()
    {
        ProfileTree tree;

        Profile movementAll;
        movementAll.duration = 500.0;
        movementAll.staggerInterval = 60;
        tree.setOverride(PP::WindowMovement, movementAll);

        Profile leaf;
        leaf.duration = 80.0; // leaf wins on duration
        tree.setOverride(PP::WindowSnapIn, leaf);

        Profile animatorGlobal;
        animatorGlobal.duration = 300.0;
        animatorGlobal.minDistance = 25; // set by nobody in the chain

        const Profile composed = tree.overlayChainOnto(PP::WindowSnapIn, animatorGlobal);
        QCOMPARE(*composed.duration, 80.0); // leaf
        QCOMPARE(*composed.staggerInterval, 60); // parent "All"
        QCOMPARE(*composed.minDistance, 25); // untouched caller base
    }

    // A leaf-only override (no ancestor engaged) still overlays onto the base:
    // the chain walk reaches the leaf directly.
    void testOverlayChainOntoLeafOnlyOverride()
    {
        ProfileTree tree;

        Profile leaf;
        leaf.duration = 80.0;
        tree.setOverride(PP::WindowSnapIn, leaf); // only the leaf, no "All"

        Profile animatorGlobal;
        animatorGlobal.duration = 300.0;
        animatorGlobal.staggerInterval = 40; // untouched by the leaf

        const Profile composed = tree.overlayChainOnto(PP::WindowSnapIn, animatorGlobal);
        QCOMPARE(*composed.duration, 80.0); // leaf
        QCOMPARE(*composed.staggerInterval, 40); // base
    }

    // A CURVE-ONLY override must apply its curve and PRESERVE the caller's base
    // duration. This is the exact regression overlayChainOnto was built to fix:
    // a curve-only node collapsing a user's global duration to the library
    // default. It is also the only field that is a bare shared_ptr rather than a
    // std::optional, so it takes an `if (src.curve)` pointer-truthiness branch in
    // overlay() that no other field exercises — testOverlayChainOntoLeafOnly...
    // covers duration-only, and the movement-All case below always sets curve AND
    // duration together, so neither can tell "curve applied, duration preserved"
    // apart from "curve applied, duration overwritten".
    void testOverlayChainOntoCurveOnlyOverrideKeepsBaseDuration()
    {
        ProfileTree tree;

        Profile movementAll;
        movementAll.curve = std::make_shared<Spring>(Spring::snappy());
        QVERIFY(!movementAll.duration.has_value()); // curve ONLY
        tree.setOverride(PP::WindowMovement, movementAll);

        Profile animatorGlobal;
        animatorGlobal.duration = 300.0;
        animatorGlobal.curve = std::make_shared<Easing>();

        const Profile composed = tree.overlayChainOnto(PP::WindowSnapIn, animatorGlobal);
        QVERIFY(composed.curve != nullptr);
        QCOMPARE(composed.curve->typeId(), QStringLiteral("spring")); // the override's curve
        QCOMPARE(*composed.duration, 300.0); // the caller's base, NOT the library default
    }

    // presetName and sequenceMode overlay through the chain like every other
    // field (the shared overlay() helper copies all six).
    void testOverlayChainOntoPresetAndSequenceMode()
    {
        ProfileTree tree;

        Profile movementAll;
        movementAll.presetName = QStringLiteral("snappy");
        movementAll.sequenceMode = SequenceMode::Cascade;
        tree.setOverride(PP::WindowMovement, movementAll);

        Profile animatorGlobal;
        animatorGlobal.duration = 300.0; // kept — no override touches it

        const Profile composed = tree.overlayChainOnto(PP::WindowSnapIn, animatorGlobal);
        QCOMPARE(*composed.presetName, QStringLiteral("snappy"));
        QCOMPARE(*composed.sequenceMode, SequenceMode::Cascade);
        QCOMPARE(*composed.duration, 300.0); // base
    }

    // The tree's own baseline must NOT leak into overlayChainOnto (the caller
    // owns the authoritative global), but an override stored AT `global` is a
    // real chain member and MUST apply. This pins the split that the D-Bus
    // adaptor relies on: it routes a `global`-keyed profile to setBaseline() and
    // every other path to setOverride(), so the baseline never double-applies on
    // top of the caller's base.
    void testOverlayChainOntoGlobalNodeIsNotTheBaseline()
    {
        ProfileTree tree;

        Profile treeBaseline;
        treeBaseline.duration = 150.0; // must be IGNORED
        treeBaseline.staggerInterval = 99; // must be IGNORED
        tree.setBaseline(treeBaseline);

        Profile animatorGlobal;
        animatorGlobal.duration = 300.0;
        animatorGlobal.staggerInterval = 40;

        // Baseline ignored: the caller's base survives untouched.
        const Profile noOverride = tree.overlayChainOnto(PP::WindowSnapIn, animatorGlobal);
        QVERIFY(noOverride == animatorGlobal);

        // An override AT `global` is part of the chain and does apply.
        Profile globalOverride;
        globalOverride.duration = 700.0;
        tree.setOverride(PP::Global, globalOverride);

        const Profile withGlobal = tree.overlayChainOnto(PP::WindowSnapIn, animatorGlobal);
        QCOMPARE(*withGlobal.duration, 700.0); // from the `global` override
        QCOMPARE(*withGlobal.staggerInterval, 40); // still the caller's base, not the baseline's 99
    }

    // With the tree non-empty, a chain carrying no matching override still
    // returns the caller's base untouched — an override on an unrelated path
    // must not leak in.
    void testOverlayChainOntoNoOverrideReturnsBaseUnchanged()
    {
        ProfileTree tree;

        // An override on an UNRELATED path must not leak into this chain.
        Profile appearance;
        appearance.duration = 900.0;
        tree.setOverride(PP::WindowOpen, appearance);

        Profile animatorGlobal;
        animatorGlobal.duration = 300.0;
        animatorGlobal.curve = std::make_shared<Easing>();

        const Profile composed = tree.overlayChainOnto(PP::WindowSnapIn, animatorGlobal);
        QVERIFY(composed == animatorGlobal);
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
        tree.setOverride(PP::EditorSnapIn, p);
        QVERIFY(tree.hasOverride(PP::EditorSnapIn));

        QVERIFY(tree.clearOverride(PP::EditorSnapIn));
        QVERIFY(!tree.hasOverride(PP::EditorSnapIn));
        QVERIFY(tree.overriddenPaths().isEmpty());

        QVERIFY(!tree.clearOverride(PP::EditorSnapIn));
    }

    void testClearAllOverrides()
    {
        ProfileTree tree;
        Profile p;
        tree.setOverride(PP::Window, p);
        tree.setOverride(PP::Editor, p);
        tree.setOverride(PP::Osd, p);

        Profile baseline;
        baseline.duration = 123.0;
        tree.setBaseline(baseline);

        tree.clearAllOverrides();
        QVERIFY(tree.overriddenPaths().isEmpty());
        QCOMPARE(*tree.baseline().duration, 123.0);
    }

    // hasAnyOverride() is the compositor's default-state fast-path gate: false
    // means "skip the cascade resolve and hand the caller's base straight back".
    // It must track the override set exactly, and must NOT be confused by a
    // baseline (which is not an override).
    void testHasAnyOverrideTracksTheOverrideSet()
    {
        ProfileTree tree;
        QVERIFY(!tree.hasAnyOverride()); // empty tree

        Profile baseline;
        baseline.duration = 123.0;
        tree.setBaseline(baseline);
        QVERIFY(!tree.hasAnyOverride()); // a baseline is not an override

        Profile p;
        p.duration = 400.0;
        tree.setOverride(PP::WindowSnapIn, p);
        QVERIFY(tree.hasAnyOverride());
        QCOMPARE(tree.hasAnyOverride(), !tree.overriddenPaths().isEmpty()); // agrees with the slow form

        tree.clearOverride(PP::WindowSnapIn);
        QVERIFY(!tree.hasAnyOverride());

        tree.setOverride(PP::WindowSnapIn, p);
        tree.setOverride(PP::Osd, p);
        tree.clearAllOverrides();
        QVERIFY(!tree.hasAnyOverride());
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
        original.setOverride(PP::EditorSnapIn, overrideB);

        const QJsonObject encoded = original.toJson();
        const ProfileTree restored = ProfileTree::fromJson(encoded, PhosphorAnimation::CurveRegistry{});

        QCOMPARE(restored, original);
    }

    void testJsonPreservesOverrideOrder()
    {
        ProfileTree original;
        original.setOverride(PP::Window, Profile());
        original.setOverride(PP::Editor, Profile());
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
