// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_shader_overrides.cpp
 * @brief AnimationsPageController shader-override tests.
 *
 * Pinned behaviour:
 *   - setShaderOverride round-trip through Settings/ShaderProfileTree
 *   - setShaderOverride emits pendingChangesChanged on every mutation
 *     (set, clear, repeated picks, empty-effect-clear shorthand)
 *   - No-op short-circuit when the request matches the existing tree
 *     state (prevents the QML two-way binding from re-dirtying on every
 *     refresh)
 *   - clearShaderOverride on an unset path is a no-op (false, no signal)
 *   - a populated registry rejects an unknown effectId (false, no write,
 *     no signal) while an empty registry stays permissive
 *
 *   - window.movement.move leaf isolation as the controller surfaces it:
 *     no inherited ancestor shader, no built-in default, runtime-truth
 *     blanking of a stale geometry pack, and a picker that offers only
 *     move-class packs
 *
 * The shader-leg GROUP writers and the group readers live next door, in
 * test_animations_shader_param_writes.cpp. They moved out when this file
 * reached the project's file-size ceiling, and the split is along a real seam:
 * everything there takes a path LIST and answers for a whole write group,
 * everything here takes one path. Both need a real ISettings to reach the
 * shader tree at all, which is why neither can live in
 * test_animations_group_writes.cpp alongside the timing-side group writers.
 * The shared fixtures are in helpers/AnimationsControllerFixture.h.
 */

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
// Written to directly by the strip-leaf blanking slot: an incompatible stored
// pack is a state the controller's own setters refuse to create, so the only
// way to set it up is through the tree.
#include <PhosphorAnimation/ShaderProfileTree.h>

#include "config/settings.h"
#include "phosphor_i18n.h"
#include "settings/pages/animationspagecontroller.h"
#include "helpers/AnimationsControllerFixture.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::ControllerFixture;
using PlasmaZones::TestHelpers::pickerIdsFor;
using PlasmaZones::TestHelpers::PopulatedControllerFixture;
using PlasmaZones::TestHelpers::storesEffectId;
// File-scope, so every slot names paths through the taxonomy rather than
// through a string literal that a leaf rename would leave quietly asserting
// against a path that no longer exists.
namespace PP = PhosphorAnimation::ProfilePaths;

class TestAnimationsShaderOverrides : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Simulates the full AnimationEventCard combo flow:
    ///   user clicks pixelate
    ///     → setShaderOverride(path, "pixelate", {})
    ///   refresh fires
    ///     → resolvedShaderProfile(path) → expect effectId="pixelate"
    /// If resolvedShaderProfile returns empty effectId here, the QML
    /// combo would snap back to "None" — mirrors the user's symptom.
    void setShaderOverride_resolveReturnsTheJustWrittenEffect()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString path = PP::OsdShow;

        QVERIFY(c.setShaderOverride(path, QStringLiteral("pixelate"), {}));

        const QVariantMap raw = c.rawShaderProfile(path);
        const QVariantMap resolved = c.resolvedShaderProfile(path);

        // Raw read MUST contain effectId=pixelate.
        QCOMPARE(raw.value(QStringLiteral("effectId")).toString(), QStringLiteral("pixelate"));
        // Resolved read (what the QML refresh path uses) MUST too.
        QCOMPARE(resolved.value(QStringLiteral("effectId")).toString(), QStringLiteral("pixelate"));
    }

    /// Symptom-mirroring test: pick A, then immediately pick B.
    /// Both reads must return the corresponding effect.
    void setShaderOverride_repeatedPicksPreserveLatest()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString path = PP::OsdShow;

        QVERIFY(c.setShaderOverride(path, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("pixelate"));

        QVERIFY(c.setShaderOverride(path, QStringLiteral("dissolve"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("dissolve"));

        QVERIFY(c.setShaderOverride(path, QStringLiteral("glitch"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(), QStringLiteral("glitch"));
    }

    /// `setShaderOverride(path, "")` — the empty-effectId disable shorthand
    /// — MUST emit pendingChangesChanged. Behavior change: empty-effectId
    /// now writes the engaged-empty disable sentinel (blocks inheritance
    /// from a parent shader) rather than removing the override entry. The
    /// raw profile is therefore NOT empty after the write — it carries an
    /// engaged-empty `effectId`.
    void setShaderOverride_emptyEffectClearsAndEmits()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        // First write something so there's state to clear.
        QVERIFY(c.setShaderOverride(PP::OsdShow, QStringLiteral("pixelate"), {}));

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(PP::OsdShow, QString(), {}));
        QCOMPARE(spy.count(), 1);
        // Engaged-empty sentinel: `effectId` is engaged but the value is
        // an empty string, signalling "explicitly no shader" (blocks
        // parent-inheritance cascade).
        const QVariantMap raw = c.rawShaderProfile(PP::OsdShow);
        QVERIFY2(!raw.isEmpty(), "engaged-empty disable sentinel must persist a profile entry");
        // Key PRESENCE first. `value()` default-constructs for a missing key, so
        // the QCOMPARE below reads an ABSENT effectId and an engaged-EMPTY one
        // identically as "" — and a params-only entry (which this PR's
        // parameter writer makes an everyday shape) has exactly that absence
        // while still being non-empty. Without this line the pair is satisfied
        // by a profile carrying only `parameters`.
        QVERIFY2(storesEffectId(raw), "the sentinel must store an ENGAGED effectId, not merely a non-empty profile");
        QCOMPARE(raw.value(QStringLiteral("effectId")).toString(), QString());
    }

    /// Set then explicit clear → both calls fire pendingChangesChanged.
    ///
    /// Subsumes the plain set-emits-once slot this replaced: same path, same
    /// spy attached at the same point. The count below pins the PAIR jointly
    /// rather than each half, so the set's own single emit is pinned here only
    /// in sum. It is pinned individually by the empty-id and no-op slots above,
    /// which is why deleting the third copy cost no real coverage. Pre-fix, setShaderOverride and clearShaderOverride
    /// mutated settings without emitting at all, so the Save button never lit
    /// up for a shader edit — that is the regression this guards.
    void setShaderOverride_thenClear_emitsTwice()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(PP::OsdShow, QStringLiteral("pixelate"), {}));
        QVERIFY(c.clearShaderOverride(PP::OsdShow));
        // Exactly two: the set makes the page dirty, the clear makes it clean
        // again. `>=` here would let a mutation that announces twice per write
        // through, and a duplicate pendingChangesChanged is a real defect (the
        // footer flickers and every listener re-queries).
        QCOMPARE(spy.count(), 2);
    }

    /// clearShaderOverride on a path with NO override is a no-op: it returns
    /// false and emits nothing (mirrors clearOverride_noFileReturnsFalseNoSignal
    /// and the clearShaderOverrideDescendants no-op case). Without this the QML
    /// refresh would re-dirty the page on every combo tick.
    void clearShaderOverride_noOverrideReturnsFalseNoSignal()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(!c.clearShaderOverride(PP::OsdShow));
        QCOMPARE(spy.count(), 0);
    }

    /// The unknown-effectId gate (rejecting a typo'd id before it corrupts the
    /// shader-profile tree) only engages when the registry is populated — an
    /// empty registry can't tell "unknown" from "not yet scanned", so it stays
    /// permissive (every other test here relies on that). With a real registry a
    /// bogus id MUST be refused (no write, no dirty), while a registered id still
    /// succeeds.
    void setShaderOverride_rejectsUnknownEffectIdWithPopulatedRegistry()
    {
        SKIP_WITHOUT_BUNDLED_PACKS();

        PopulatedControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;
        QVERIFY2(!registry.effectIds().isEmpty(), "precondition: registry populated so the gate is armed");

        const QString path = PP::OsdShow;

        // A typo'd id is refused: false, no tree write, no pendingChangesChanged.
        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(!c.setShaderOverride(path, QStringLiteral("no-such-effect"), {}));
        QCOMPARE(spy.count(), 0);
        QVERIFY2(c.rawShaderProfile(path).isEmpty(), "refused write must not touch the tree");

        // A registered id still succeeds and writes through.
        QVERIFY(registry.hasEffect(QStringLiteral("pixelate")));
        QVERIFY(c.setShaderOverride(path, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("pixelate"));
    }

    /// Runtime-truth mirror in resolvedShaderProfile: a PERSISTED effect the
    /// registry knows but that provably cannot drive its event (the same
    /// canonical-predicate check the compositor's resolvedShaderAppliesToEvent
    /// gate applies) is blanked from the VIEW, while the persisted override
    /// itself stays untouched so the state self-heals on the user's next
    /// pick. An applicable pack is returned unchanged. Unknown ids passing
    /// through mid-warmup is pinned by the empty-registry tests above, where
    /// hasEffect is false for every id.
    void resolvedShaderProfile_blanksInapplicablePersistedEffect()
    {
        SKIP_WITHOUT_BUNDLED_PACKS();

        PopulatedControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        // window-morph declares appliesTo ["geometry"]; window.appearance.open
        // is an appearance leg — a provable class mismatch. Only a stale or
        // hand-edited config can produce this shape (the picker filters it),
        // and setShaderOverride's gate is unknown-id only, so the write lands.
        QVERIFY2(registry.hasEffect(QStringLiteral("window-morph")), "precondition: bundled pack scanned");
        const QString path = PP::WindowOpen;
        QVERIFY(c.setShaderOverride(path, QStringLiteral("window-morph"), {}));

        // Persisted override untouched...
        QCOMPARE(c.rawShaderProfile(path).value(QStringLiteral("effectId")).toString(), QStringLiteral("window-morph"));
        // ...but the resolved VIEW blanks the id.
        QVERIFY(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString().isEmpty());

        // Symmetric positive: an applicable pack is returned unchanged.
        QVERIFY(registry.hasEffect(QStringLiteral("dissolve")));
        QVERIFY(c.setShaderOverride(path, QStringLiteral("dissolve"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("dissolve"));
    }

    /// The drag-leaf isolation as the controller surfaces it (what the
    /// Window Dragging page combo reads back): an ancestor shader on
    /// `window` or `window.movement` must NOT be inherited by
    /// window.movement.move, while a sibling geometry leg still inherits
    /// normally. Empty registry, so the runtime-truth blanking stays out
    /// of the way and this pins pure tree resolution through
    /// resolvedShaderProfile → resolveShaderWithDefault → resolve.
    void resolvedShaderProfile_moveLeafRefusesAncestorInheritance()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        // Family root carries a shader...
        QVERIFY(c.setShaderOverride(PP::Window, QStringLiteral("matrix"), {}));
        // ...the sibling geometry leg inherits it...
        QCOMPARE(c.resolvedShaderProfile(PP::WindowSnapIn).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("matrix"));
        // ...the drag leaf refuses it.
        QVERIFY(c.resolvedShaderProfile(PP::WindowMove).value(QStringLiteral("effectId")).toString().isEmpty());

        // Same with the movement sub-parent (the "All Windows" card on the
        // Window Motion page — the CHANGELOG's "no longer takes the All
        // Windows shader" promise).
        QVERIFY(c.setShaderOverride(PP::WindowMovement, QStringLiteral("glitch"), {}));
        QCOMPARE(c.resolvedShaderProfile(PP::WindowSnapIn).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("glitch"));
        QVERIFY(c.resolvedShaderProfile(PP::WindowMove).value(QStringLiteral("effectId")).toString().isEmpty());

        // A direct pick on the drag leaf itself still lands.
        QVERIFY(c.setShaderOverride(PP::WindowMove, QStringLiteral("wobble"), {}));
        QCOMPARE(c.resolvedShaderProfile(PP::WindowMove).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("wobble"));
    }

    /// A fresh tree gives the drag leaf NO built-in default: the snap legs
    /// default to window-morph, but `defaultShaderEffectIdForPath` excludes
    /// window.movement.move (a crossfade cannot drive the held drag), so the
    /// Window Dragging combo starts at "None" until the user opts in. A
    /// regression re-adding the default would make the drag page advertise
    /// window-morph as current for every fresh config.
    void resolvedShaderProfile_moveLeafHasNoBuiltInDefault()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.resolvedShaderProfile(PP::WindowMove).value(QStringLiteral("effectId")).toString().isEmpty());
        // Contrast: the snap sibling gets the built-in geometry default.
        QCOMPARE(c.resolvedShaderProfile(PP::WindowSnapIn).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("window-morph"));
    }

    /// The move-leaf variant of the runtime-truth blank: a stale pre-split
    /// GEOMETRY pack persisted on the drag leaf (the exact shape the
    /// compositor gate's comment names) is blanked from the resolved VIEW
    /// while the persisted override survives, and the bundled move pack
    /// passes through. This exercises the opt-in EventClassMove branch of
    /// shaderEffectAppliesToEventPath — a different code path from the
    /// appearance-leg case above.
    void resolvedShaderProfile_blanksStaleGeometryPackOnMoveLeaf()
    {
        SKIP_WITHOUT_BUNDLED_PACKS();

        PopulatedControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY2(registry.hasEffect(QStringLiteral("window-morph")), "precondition: bundled pack scanned");
        QVERIFY(c.setShaderOverride(PP::WindowMove, QStringLiteral("window-morph"), {}));
        // Persisted override untouched...
        QCOMPARE(c.rawShaderProfile(PP::WindowMove).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("window-morph"));
        // ...but the resolved VIEW blanks the geometry pack on the move leaf.
        QVERIFY(c.resolvedShaderProfile(PP::WindowMove).value(QStringLiteral("effectId")).toString().isEmpty());

        // Symmetric positive: the bundled move pack passes through unchanged.
        QVERIFY2(registry.hasEffect(QStringLiteral("wobble")), "precondition: bundled move pack scanned");
        QVERIFY(c.setShaderOverride(PP::WindowMove, QStringLiteral("wobble"), {}));
        QCOMPARE(c.resolvedShaderProfile(PP::WindowMove).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("wobble"));
    }

    /// Picker contract for the Window Dragging page (the CHANGELOG's "its
    /// picker offers only drag shaders such as Wobbly Move"): the move leaf
    /// offers the bundled move pack and none of the crossfade packs, and a
    /// crossfade leg does not offer the move pack.
    void availableShaderEffectsForPath_moveLeafOffersOnlyDragShaders()
    {
        SKIP_WITHOUT_BUNDLED_PACKS();

        PopulatedControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QStringList moveIds = pickerIdsFor(c, PP::WindowMove);
        QVERIFY2(moveIds.contains(QStringLiteral("wobble")), "bundled move pack must be offered on the drag leaf");
        QVERIFY2(!moveIds.contains(QStringLiteral("window-morph")),
                 "geometry pack must not be offered on the drag leaf");
        QVERIFY2(!moveIds.contains(QStringLiteral("dissolve")),
                 "universal crossfade must not be offered on the drag leaf");

        const QStringList snapIds = pickerIdsFor(c, PP::WindowSnapIn);
        QVERIFY2(snapIds.contains(QStringLiteral("window-morph")), "geometry pack must stay offered on snap legs");
        QVERIFY2(!snapIds.contains(QStringLiteral("wobble")), "move pack must not be offered on a crossfade leg");
    }

    /// The same picker contract for the Scrolling page's Strip Scrolled row.
    /// The page's own comment promises the picker offers only strip packs, so
    /// pin it the way the drag leaf above is pinned: bundled strip packs in,
    /// every other class out, and strip packs absent everywhere else.
    void availableShaderEffectsForPath_stripLeafOffersOnlyStripShaders()
    {
        SKIP_WITHOUT_BUNDLED_PACKS();

        PopulatedControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QStringList stripIds = pickerIdsFor(c, PP::ScrollingView);
        QVERIFY2(stripIds.contains(QStringLiteral("strip-motion-blur")),
                 "bundled strip pack must be offered on the strip leaf");
        QVERIFY2(stripIds.contains(QStringLiteral("phosphor-gate")),
                 "every bundled strip pack must be offered, not just the first");
        QVERIFY2(!stripIds.contains(QStringLiteral("window-morph")),
                 "geometry pack must not be offered on the strip leaf");
        QVERIFY2(!stripIds.contains(QStringLiteral("wobble")), "move pack must not be offered on the strip leaf");
        QVERIFY2(!stripIds.contains(QStringLiteral("dissolve")),
                 "universal crossfade must not be offered on the strip leaf");
        QVERIFY2(!stripIds.contains(QStringLiteral("desktop-fade")),
                 "desktop pack must not be offered on the strip leaf");

        // …and the strip packs stay out of every other picker.
        //
        // The non-emptiness check is load-bearing, not defensive noise: every
        // assertion here is a NEGATIVE one, so a regression that made the
        // picker return nothing at all for these paths would satisfy the lot.
        for (const QString& path : {PP::WindowSnapIn, PP::WindowOpen, PP::WindowMove, PP::DesktopSwitch}) {
            const QStringList ids = pickerIdsFor(c, path);
            QVERIFY2(!ids.isEmpty(), qPrintable(QStringLiteral("picker offered nothing at all on ") + path));
            QVERIFY2(!ids.contains(QStringLiteral("strip-motion-blur")),
                     qPrintable(QStringLiteral("strip pack offered on ") + path));
            QVERIFY2(!ids.contains(QStringLiteral("phosphor-gate")),
                     qPrintable(QStringLiteral("strip pack offered on ") + path));
        }
    }

    /// The other half of the picker contract: an override left behind from a
    /// pack that no longer applies must not resolve. A stale non-strip id on
    /// the strip leaf (hand-edited config, or a pack whose appliesTo changed)
    /// blanks rather than arming the pass with an incompatible pack.
    void resolvedShaderProfile_stripLeafBlanksAnIncompatibleOverride()
    {
        SKIP_WITHOUT_BUNDLED_PACKS();

        PopulatedControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        // Control: a COMPATIBLE pack resolves, so the blanking below is
        // attributable to compatibility rather than to the leaf resolving
        // nothing whatever it is given.
        QVERIFY(c.setShaderOverride(PP::ScrollingView, QStringLiteral("strip-motion-blur"), {}));
        QCOMPARE(c.resolvedShaderProfile(PP::ScrollingView).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("strip-motion-blur"));

        // The payload the slot is named for, and which it did not previously
        // exercise: an INCOMPATIBLE id stored on the strip leaf blanks on
        // resolve rather than arming the pass with a pack that cannot drive it.
        // Written through the tree directly, because that is the only way this
        // state arises — a hand-edited config, or a pack whose appliesTo
        // narrowed after it was assigned. The picker would refuse it today.
        PhosphorAnimationShaders::ShaderProfileTree tree = settings.shaderProfileTree();
        PhosphorAnimationShaders::ShaderProfile stale;
        stale.effectId = QStringLiteral("window-morph");
        tree.setOverride(PP::ScrollingView, stale);
        settings.setShaderProfileTree(tree);

        QVERIFY2(c.resolvedShaderProfile(PP::ScrollingView).value(QStringLiteral("effectId")).toString().isEmpty(),
                 "a geometry pack stored on the strip leaf resolved instead of blanking");
    }

    /// The tab-leaf picker contract, mirroring the drag and strip leaves
    /// above: the Tab Switched row offers only tab packs (bundled generics
    /// and the branded pair), and tab packs stay out of every other picker.
    void availableShaderEffectsForPath_tabLeafOffersOnlyTabShaders()
    {
        SKIP_WITHOUT_BUNDLED_PACKS();

        PopulatedControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QStringList tabIds = pickerIdsFor(c, PP::ScrollingTabSwitch);
        QVERIFY2(tabIds.contains(QStringLiteral("tab-fade")), "bundled tab pack must be offered on the tab leaf");
        QVERIFY2(tabIds.contains(QStringLiteral("phosphor-transfer")),
                 "branded tab pack must be offered, not just the generics");
        QVERIFY2(tabIds.contains(QStringLiteral("phosphor-iris")),
                 "the whole branded pair must be offered, not just the first of it");
        QVERIFY2(!tabIds.contains(QStringLiteral("window-morph")), "geometry pack must not be offered on the tab leaf");
        QVERIFY2(!tabIds.contains(QStringLiteral("wobble")), "move pack must not be offered on the tab leaf");
        QVERIFY2(!tabIds.contains(QStringLiteral("dissolve")),
                 "universal crossfade must not be offered on the tab leaf");
        QVERIFY2(!tabIds.contains(QStringLiteral("strip-motion-blur")),
                 "strip pack must not be offered on the tab leaf");
        QVERIFY2(!tabIds.contains(QStringLiteral("desktop-fade")), "desktop pack must not be offered on the tab leaf");

        // …and the tab packs stay out of every other picker, including the
        // strip sibling that shares the scrolling subtree.
        for (const QString& path :
             {PP::WindowSnapIn, PP::WindowOpen, PP::WindowMove, PP::DesktopSwitch, PP::ScrollingView}) {
            const QStringList ids = pickerIdsFor(c, path);
            // Same reason as the strip twin: without this every assertion in
            // the loop is satisfied by an empty list.
            QVERIFY2(!ids.isEmpty(), qPrintable(QStringLiteral("picker offered nothing at all on ") + path));
            QVERIFY2(!ids.contains(QStringLiteral("tab-fade")),
                     qPrintable(QStringLiteral("tab pack offered on ") + path));
            QVERIFY2(!ids.contains(QStringLiteral("phosphor-transfer")),
                     qPrintable(QStringLiteral("tab pack offered on ") + path));
        }
    }

    /// The tab-leaf isolation twin of the move-leaf slot above: an ancestor
    /// shader on `scrolling` must NOT be inherited by scrolling.tabSwitch,
    /// while the strip sibling still inherits normally. Empty registry, so
    /// this pins pure tree resolution.
    void resolvedShaderProfile_tabLeafRefusesAncestorInheritance()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        // Family root carries a shader...
        QVERIFY(c.setShaderOverride(PP::Scrolling, QStringLiteral("matrix"), {}));
        // ...the strip sibling inherits it...
        QCOMPARE(c.resolvedShaderProfile(PP::ScrollingView).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("matrix"));
        // ...the tab leaf refuses it.
        QVERIFY(c.resolvedShaderProfile(PP::ScrollingTabSwitch).value(QStringLiteral("effectId")).toString().isEmpty());

        // A direct pick on the tab leaf itself still lands.
        QVERIFY(c.setShaderOverride(PP::ScrollingTabSwitch, QStringLiteral("tab-fade"), {}));
        QCOMPARE(c.resolvedShaderProfile(PP::ScrollingTabSwitch).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("tab-fade"));
    }

    /// A fresh tree gives the tab leaf NO built-in default: the switch is
    /// instant in-place unless the user opts in, matching the drag and strip
    /// leaves. A regression re-adding a default would fade every tab switch
    /// for every fresh config.
    void resolvedShaderProfile_tabLeafHasNoBuiltInDefault()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.resolvedShaderProfile(PP::ScrollingTabSwitch).value(QStringLiteral("effectId")).toString().isEmpty());

        // Contrast, the way the move-leaf twin above does it. On a fresh tree
        // EVERY path resolves empty, so the assertion alone is equally
        // satisfied by a `defaultShaderEffectIdForPath` that returns nothing
        // for anything. A path that DOES carry a built-in default is what makes
        // the emptiness above a statement about the tab leaf.
        QCOMPARE(c.resolvedShaderProfile(PP::WindowSnapIn).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("window-morph"));
    }

    /// User-reported scenario: parent ("All Window Events" / "All
    /// Popups" / etc.) has an explicit shader. The user wants to override
    /// a CHILD event with a different shader. Each step's resolution
    /// must reflect the latest write — otherwise the picker appears to
    /// "reject" the user's selection because the resolved value the QML
    /// reads back is still the inherited parent shader.
    void setShaderOverride_childOverridesParentInheritance()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        // Parent ("All Window Events") set to matrix
        QVERIFY(c.setShaderOverride(PP::Window, QStringLiteral("matrix"), {}));
        QCOMPARE(c.resolvedShaderProfile(PP::Window).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("matrix"));
        // Child inherits matrix.
        QCOMPARE(c.resolvedShaderProfile(PP::WindowOpen).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("matrix"));
        // Child has NO direct override yet.
        QVERIFY(c.rawShaderProfile(PP::WindowOpen).isEmpty());

        // Child overrides to a DIFFERENT shader.
        QVERIFY(c.setShaderOverride(PP::WindowOpen, QStringLiteral("dissolve"), {}));

        // Child must resolve to dissolve (direct override wins over
        // parent's matrix). Parent's window resolution is unchanged.
        QCOMPARE(c.resolvedShaderProfile(PP::WindowOpen).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("dissolve"));
        QCOMPARE(c.resolvedShaderProfile(PP::Window).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("matrix"));
        // Direct override is now visible at the child path.
        QCOMPARE(c.rawShaderProfile(PP::WindowOpen).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("dissolve"));

        // Switch the child to a third shader — must take effect with no
        // residual influence from the prior child override.
        QVERIFY(c.setShaderOverride(PP::WindowOpen, QStringLiteral("glitch"), {}));
        QCOMPARE(c.resolvedShaderProfile(PP::WindowOpen).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("glitch"));
    }

    /// Repeated identical OFF toggles MUST short-circuit after the first
    /// write — the engaged-empty disable sentinel is materially the same
    /// state on disk, so a no-op second call should not light the Save
    /// button. Pins the comparison contract documented next to the
    /// engaged-empty branch in `setShaderOverride`: `ShaderProfile::operator==`
    /// is engaged-state-sensitive, and the constructed `disabledProfile`
    /// matches the disk-loaded round-trip shape (parameters = nullopt).
    void setShaderOverride_repeatedDisableIsNoOp()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString path = PP::Popup;

        // First disable write — fires pendingChangesChanged once.
        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(path, QString(), {}));
        QCOMPARE(spy.count(), 1);

        // Second identical disable write — must short-circuit, no extra
        // signal. If this fires a second time, the compare-and-skip path
        // diverged from the engaged-state-sensitive equality contract.
        QVERIFY(c.setShaderOverride(path, QString(), {}));
        QCOMPARE(spy.count(), 1);
        // Pin tree state too — a future regression that returns true while
        // quietly mutating the tree (e.g. clearing the engaged-empty
        // sentinel, or engaging the parameters optional) wouldn't trip
        // the spy-count check.
        //
        // The `contains` check is what actually catches the clearing half, and
        // it is not redundant with the QCOMPARE beside it: reading a MISSING
        // key through `value()` also yields an empty QString, so a regression
        // that removed the entry outright passed the QCOMPARE unchanged.
        QVERIFY2(storesEffectId(c.rawShaderProfile(path)), "the sentinel was cleared rather than left engaged-empty");
        QCOMPARE(c.rawShaderProfile(path).value(QStringLiteral("effectId")).toString(), QString());
        QVERIFY(!c.rawShaderProfile(path).contains(QStringLiteral("parameters")));

        // Third identical disable write — same invariant.
        QVERIFY(c.setShaderOverride(path, QString(), {}));
        QCOMPARE(spy.count(), 1);
        QVERIFY2(storesEffectId(c.rawShaderProfile(path)), "the sentinel was cleared rather than left engaged-empty");
        QCOMPARE(c.rawShaderProfile(path).value(QStringLiteral("effectId")).toString(), QString());
        QVERIFY(!c.rawShaderProfile(path).contains(QStringLiteral("parameters")));
    }

    // The descendant-coverage tests below each construct a fresh
    // `Settings` under an `IsolatedConfigGuard`, so the guard redirects
    // `QStandardPaths` away from the user's real `~/.config` for the
    // lifetime of the test. Each case is therefore hermetic — no state
    // leaks between tests, no dependency on Qt Test's invocation order,
    // and no risk of clobbering the developer's actual config.

    /// `shaderOverrideDescendantCount` must count strict descendants only —
    /// dot-bounded prefix match, and not the path itself. Pin both the
    /// non-zero and zero branches plus a deeply-nested chain.
    ///
    /// The walk's trailing-dot boundary ALSO excludes a sibling sharing a bare
    /// character prefix ("popups" under "popup"), and that half is deliberately
    /// not asserted here: no two paths in ProfilePaths stand in that relation,
    /// and every writer gates on isValidEventPath, so the state cannot be built
    /// through the controller. Asserting it would mean writing a synthetic path
    /// straight into the tree to test a case the taxonomy cannot produce. The
    /// claim used to be in this comment without a matching assertion, which is
    /// worse than either option.
    void shaderOverrideDescendantCount_strictDescendantsOnly()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        // Empty tree → count is zero for any path.
        QCOMPARE(c.shaderOverrideDescendantCount(PP::Popup), 0);
        QCOMPARE(c.shaderOverrideDescendantCount(PP::PopupLayoutPicker), 0);

        // Self-only override at popup.layoutPicker — descendants of it: 0.
        QVERIFY(c.setShaderOverride(PP::PopupLayoutPicker, QStringLiteral("dissolve"), {}));
        QCOMPARE(c.shaderOverrideDescendantCount(PP::PopupLayoutPicker), 0);
        // Descendants of `popup` includes `popup.layoutPicker`: 1.
        QCOMPARE(c.shaderOverrideDescendantCount(PP::Popup), 1);

        // Add a deeply-nested descendant.
        QVERIFY(c.setShaderOverride(PP::PopupLayoutPickerShow, QStringLiteral("pixelate"), {}));
        // popup sees both descendants now.
        QCOMPARE(c.shaderOverrideDescendantCount(PP::Popup), 2);
        // popup.layoutPicker sees the leaf only (itself excluded by strict prefix).
        QCOMPARE(c.shaderOverrideDescendantCount(PP::PopupLayoutPicker), 1);
        // The leaf itself has no descendants.
        QCOMPARE(c.shaderOverrideDescendantCount(PP::PopupLayoutPickerShow), 0);

        // Empty path → zero (defensive: collectShaderOverrideDescendants
        // bails on empty path so the prefix isn't a bare ".").
        QCOMPARE(c.shaderOverrideDescendantCount(QString()), 0);
    }

    /// `clearShaderOverrideDescendants` removes every strict descendant,
    /// preserves the path-itself override (parent-card "Clear shadowing
    /// children" semantics), and is a no-op when count is zero.
    void clearShaderOverrideDescendants_clearsStrictDescendantsOnly()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        // Build: parent override + two descendants at different depths.
        QVERIFY(c.setShaderOverride(PP::Popup, QStringLiteral("dissolve"), {}));
        QVERIFY(c.setShaderOverride(PP::PopupLayoutPickerShow, QStringLiteral("pixelate"), {}));
        QVERIFY(c.setShaderOverride(PP::PopupZoneSelectorShow, QStringLiteral("matrix"), {}));
        QCOMPARE(c.shaderOverrideDescendantCount(PP::Popup), 2);

        // Clear from popup — both leaves go, popup itself remains
        // (it's the parent the user is keeping intact).
        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QCOMPARE(c.clearShaderOverrideDescendants(PP::Popup), 2);
        // Exactly one for the whole batch: the batch entry point clears every
        // descendant into one tree, writes it once, and the write is what
        // emits. Not a NET-flip comparison — the emitter fires unconditionally
        // per tree write and defers the dirty compare to the next
        // hasPendingChanges() query — so the count here is a count of WRITES.
        // `>=` would have accepted the per-path-emit regression the batch
        // exists to avoid.
        QCOMPARE(spy.count(), 1);
        // Verify count is now zero.
        QCOMPARE(c.shaderOverrideDescendantCount(PP::Popup), 0);
        // Parent override survived.
        QCOMPARE(c.rawShaderProfile(PP::Popup).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("dissolve"));
        // Leaf overrides really gone.
        QVERIFY(c.rawShaderProfile(PP::PopupLayoutPickerShow).isEmpty());
        QVERIFY(c.rawShaderProfile(PP::PopupZoneSelectorShow).isEmpty());

        // No-op call — count already zero.
        QSignalSpy spy2(&c, &AnimationsPageController::pendingChangesChanged);
        QCOMPARE(c.clearShaderOverrideDescendants(PP::Popup), 0);
        QCOMPARE(spy2.count(), 0);
    }

    /// A clear that runs while the discard worker owns the pending state must
    /// report the refusal as -1, distinct from the 0 a genuine "no descendants"
    /// no-op returns, so a caller cannot mistake "refused, retry later" for
    /// "nothing to clear". Same convention as clearAllOverrides.
    void clearShaderOverrideDescendants_refusesWhileAsyncDiscardIsInFlight()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.setShaderOverride(PP::Popup, QStringLiteral("dissolve"), {}));
        QVERIFY(c.setShaderOverride(PP::PopupLayoutPickerShow, QStringLiteral("pixelate"), {}));
        // A FILE-backed pending change too: a tree-only discard completes
        // synchronously (no worker, no in-flight window), so the refusal
        // this slot pins requires a snapshot for the worker to restore.
        QVERIFY(c.setOverride(PP::Popup, QVariantMap{{QStringLiteral("duration"), 200}}));

        QSignalSpy done(&c, &AnimationsPageController::discardResult);
        QSignalSpy toastSpy(&c, &AnimationsPageController::toastRequested);
        c.asyncRevertPending(); // sets the in-flight flag synchronously
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(QStringLiteral("clearShaderOverrideDescendants: refusing while async discard")));
        QCOMPARE(c.clearShaderOverrideDescendants(PP::Popup), -1);
        QCOMPARE(toastSpy.count(), 1);
        // Not the "Cannot reset" wording: this entry point's only caller is the
        // event card's "Clear shadowing children" button, which the user did
        // not experience as a reset. `clearAllOverrides`, which IS reset-only,
        // keeps that wording (see test_animations_motion_sets.cpp).
        QCOMPARE(toastSpy.first().first().toString(),
                 PhosphorI18n::tr("Cannot change this while a discard is in progress."));

        // QTRY, not `done.wait()`: `asyncRevertPending` emits `discardResult`
        // SYNCHRONOUSLY on two early-return branches, and `QSignalSpy::wait()`
        // ignores an emission already recorded before it was called — so on
        // either branch `wait()` would block the full timeout and then fail for
        // the wrong reason. Every sibling slot in this file uses QTRY for this.
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    }

    /// The leaf-isolated interactive-drag path (window.movement.move) is a
    /// prefix-descendant of window.movement but resolves in ISOLATION —
    /// ShaderProfileTree::resolve takes no ancestor overlay there — so its
    /// override can never shadow the "All Windows" parent. The shadowing-
    /// children count must skip it, and the parent card's "Clear shadowing
    /// children" action must NOT wipe the drag shader the user configured
    /// on the separate Window Dragging page.
    void shaderOverrideDescendants_skipLeafIsolatedMovePath()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        // Drag-leaf override + one genuine shadowing sibling for contrast.
        QVERIFY(c.setShaderOverride(PP::WindowMove, QStringLiteral("wobble"), {}));
        QVERIFY(c.setShaderOverride(PP::WindowSnapIn, QStringLiteral("window-morph"), {}));

        // Only the snapIn override shadows the movement parent.
        QCOMPARE(c.shaderOverrideDescendantCount(PP::WindowMovement), 1);

        // Clearing shadowing children removes snapIn but PRESERVES the
        // isolated drag-leaf override.
        QCOMPARE(c.clearShaderOverrideDescendants(PP::WindowMovement), 1);
        QVERIFY(c.rawShaderProfile(PP::WindowSnapIn).isEmpty());
        QCOMPARE(c.rawShaderProfile(PP::WindowMove).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("wobble"));
    }

    /// `setShaderOverride` short-circuits when the request matches the
    /// existing tree state. Without this, the QML two-way binding round-
    /// trip (combo activation → controller → settings → tree → QML
    /// refresh → re-emit) would dirty the page on every reload.
    void setShaderOverride_noOpWhenTreeAlreadyMatches()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.setShaderOverride(PP::OsdShow, QStringLiteral("pixelate"), {}));

        QSignalSpy pendingSpy(&c, &AnimationsPageController::pendingChangesChanged);
        // Same effectId + same (empty) parameters — must early-return.
        QVERIFY(c.setShaderOverride(PP::OsdShow, QStringLiteral("pixelate"), {}));
        QCOMPARE(pendingSpy.count(), 0);
    }
};

QTEST_MAIN(TestAnimationsShaderOverrides)
#include "test_animations_shader_overrides.moc"
