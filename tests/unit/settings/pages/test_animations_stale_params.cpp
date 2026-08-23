// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_stale_params.cpp
 * @brief The orphaned-parameter surface: shaderParamsAreStale,
 *        staleParamDescendantCountForPaths and clearStaleParamDescendantsOnPaths.
 *
 * The state under test only exists because `ShaderProfile::overlay` REPLACES
 * the parameter map rather than merging keys. A descendant that tunes a slider
 * while inheriting its pack stores parameters and no `effectId`, and keeps
 * following the ancestor on the pack axis alone. Switch the ancestor's pack and
 * those stored ids belong to a pack nothing resolves any more: the descendant
 * renders the new pack at its defaults, and no amount of editing the ancestor's
 * parameters reaches it again.
 *
 * Nothing crashes and nothing is corrupt, which is exactly why it needs
 * pinning — the failure is silent detachment, and every assertion here has to
 * reach past the resolved profile to see it.
 *
 * Pinned behaviour:
 *   - the predicate is about the RESOLVED pack's declared ids, not about which
 *     pack authored the values, so re-pointing the ancestor back un-stales them
 *   - a path that owns a pack is never stale (its values match by construction)
 *     and neither is the engaged-empty "no shader" sentinel
 *   - PARTIAL overlap is not stale: two packs sharing a parameter id means the
 *     stored values still do something, and discarding them would be a loss
 *   - an unpopulated registry refuses to judge rather than declaring every
 *     override orphaned, so a controller built without an animation bootstrap
 *     cannot offer to delete a user's parameters
 *   - the count UNIONS descendants across the group, so a path reachable from
 *     two members is counted once, and the clear acts on exactly what the count
 *     reported
 */

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include "config/settings.h"
#include "helpers/AnimationsControllerFixture.h"
#include "settings/pages/animationspagecontroller.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::ControllerFixture;
using PlasmaZones::TestHelpers::PopulatedControllerFixture;
using PlasmaZones::TestHelpers::storesEffectId;
namespace PP = PhosphorAnimation::ProfilePaths;

namespace {

/// The declared parameter ids of @p effectId, as a set.
QSet<QString> declaredIds(const AnimationsPageController& c, const QString& effectId)
{
    QSet<QString> ids;
    const QVariantList declared = c.shaderParameters(effectId);
    for (const QVariant& entry : declared)
        ids.insert(entry.toMap().value(QStringLiteral("id")).toString());
    return ids;
}

/// A pair of bundled packs whose declared parameter ids are non-empty and
/// DISJOINT, plus a shared id if any pair overlaps.
///
/// Chosen at runtime rather than hard-coded, because pinning two pack ids here
/// would make this file fail the day a bundled pack gains or renames a
/// parameter — a data edit, unrelated to the code under test. The slots skip
/// when the bundled tree offers no such pair, which is honest: the property
/// being tested would then have nothing to observe.
struct PackPair
{
    QString a;
    QString b;
    bool found = false;
};

PackPair disjointPacks(const AnimationsPageController& c, const QString& path)
{
    const QVariantList offers = c.availableShaderEffectsForPath(path);
    QStringList ids;
    for (const QVariant& v : offers) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && !declaredIds(c, id).isEmpty())
            ids.append(id);
    }
    for (int i = 0; i < ids.size(); ++i) {
        for (int j = i + 1; j < ids.size(); ++j) {
            if (!declaredIds(c, ids.at(i)).intersects(declaredIds(c, ids.at(j))))
                return {ids.at(i), ids.at(j), true};
        }
    }
    return {};
}

} // namespace

class TestAnimationsStaleParams : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── the predicate ───────────────────────────────────────────────

    /// The headline case. A leaf tunes a slider while inheriting `a`, the
    /// ancestor switches to `b`, and the leaf's stored ids now name nothing the
    /// resolved pack declares.
    void shaderParamsAreStale_afterAnAncestorSwitchesPack()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        PopulatedControllerFixture fx;
        auto& c = fx.c;

        const QString parent = PP::WindowAppearance;
        const QString leaf = PP::WindowOpen;
        const PackPair packs = disjointPacks(c, leaf);
        if (!packs.found)
            QSKIP("no two bundled packs declare disjoint parameter ids");

        QVERIFY(c.setShaderOverride(parent, packs.a, {}));
        const QString paramA = *declaredIds(c, packs.a).constBegin();
        QCOMPARE(c.setShaderParametersOnPaths({leaf}, QVariantMap{{paramA, 0.5}}), 1);

        // Still following `a`, so the values apply. Asserted BEFORE the switch:
        // a predicate that answered "stale" unconditionally would otherwise
        // pass the interesting assertion below for the wrong reason.
        QVERIFY2(!c.shaderParamsAreStale(leaf), "values authored against the pack in force read as orphaned");

        QVERIFY(c.setShaderOverride(parent, packs.b, {}));
        QVERIFY2(c.shaderParamsAreStale(leaf), "values whose ids the resolved pack does not declare read as live");

        // And the leaf really is detached: it renders `b`, at `b`'s defaults.
        QCOMPARE(c.resolvedShaderProfile(leaf).value(QStringLiteral("effectId")).toString(), packs.b);
        QVERIFY2(!storesEffectId(c.rawShaderProfile(leaf)), "the leaf stopped inheriting its pack");
    }

    /// The predicate reads the pack in force, not the pack that authored the
    /// values, so pointing the ancestor back makes the same stored map live
    /// again. That is why the banner's action says DISCARD: the values are
    /// recoverable right up until it runs.
    void shaderParamsAreStale_isUndoneByPointingTheAncestorBack()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        PopulatedControllerFixture fx;
        auto& c = fx.c;

        const QString parent = PP::WindowAppearance;
        const QString leaf = PP::WindowOpen;
        const PackPair packs = disjointPacks(c, leaf);
        if (!packs.found)
            QSKIP("no two bundled packs declare disjoint parameter ids");

        QVERIFY(c.setShaderOverride(parent, packs.a, {}));
        QCOMPARE(c.setShaderParametersOnPaths({leaf}, QVariantMap{{*declaredIds(c, packs.a).constBegin(), 0.5}}), 1);
        QVERIFY(c.setShaderOverride(parent, packs.b, {}));
        QVERIFY(c.shaderParamsAreStale(leaf));

        QVERIFY(c.setShaderOverride(parent, packs.a, {}));
        QVERIFY2(!c.shaderParamsAreStale(leaf), "the predicate remembers which pack authored the values");
    }

    /// A path that owns its pack is never stale. Its values were authored
    /// against the pack it still resolves, and the whole notion of an ancestor
    /// switching out from under it does not apply.
    ///
    /// Mutation-checked: dropping the `stored.effectId.has_value()` guard makes
    /// this fail, because the leaf below stores ids from `a` while owning `b`.
    void shaderParamsAreStale_isFalseForAPathThatOwnsItsPack()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        PopulatedControllerFixture fx;
        auto& c = fx.c;

        const QString leaf = PP::WindowOpen;
        const PackPair packs = disjointPacks(c, leaf);
        if (!packs.found)
            QSKIP("no two bundled packs declare disjoint parameter ids");

        QVERIFY(c.setShaderOverride(PP::WindowAppearance, packs.a, {}));
        QCOMPARE(c.setShaderParametersOnPaths({leaf}, QVariantMap{{*declaredIds(c, packs.a).constBegin(), 0.5}}), 1);
        // Promote the leaf to owning `b` while keeping the `a`-era values.
        QVERIFY(
            c.setShaderOverride(leaf, packs.b, c.rawShaderProfile(leaf).value(QStringLiteral("parameters")).toMap()));

        QVERIFY2(storesEffectId(c.rawShaderProfile(leaf)), "the promotion did not take");
        QVERIFY2(!c.shaderParamsAreStale(leaf), "a path that owns its pack was reported as orphaned");
    }

    /// Two packs that share a parameter id leave the stored values doing
    /// something, so the map is not orphaned. Discarding on partial overlap
    /// would throw away values that still work.
    void shaderParamsAreStale_isFalseOnPartialOverlap()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        PopulatedControllerFixture fx;
        auto& c = fx.c;

        const QString parent = PP::WindowAppearance;
        const QString leaf = PP::WindowOpen;
        const PackPair packs = disjointPacks(c, leaf);
        if (!packs.found)
            QSKIP("no two bundled packs declare disjoint parameter ids");

        QVERIFY(c.setShaderOverride(parent, packs.b, {}));

        // One id from each pack. `b` is what resolves, so the `b` id overlaps
        // and the `a` id does not — the mixed map the carve-out is about.
        const QVariantMap mixed{{*declaredIds(c, packs.a).constBegin(), 0.5},
                                {*declaredIds(c, packs.b).constBegin(), 0.5}};
        QCOMPARE(c.setShaderParametersOnPaths({leaf}, mixed), 1);

        QVERIFY2(!c.shaderParamsAreStale(leaf), "a map with one live id was discarded as wholly orphaned");
    }

    /// With no packs scanned the registry declares nothing, and "declares
    /// nothing" must not read as "declares none of these". A controller built
    /// without an animation bootstrap has to refuse to judge, or the settings
    /// app would offer to delete every parameter a user ever set.
    ///
    /// Mutation-checked: removing the `declared.isEmpty()` early return makes
    /// this fail.
    void shaderParamsAreStale_refusesToJudgeWithoutAPopulatedRegistry()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.setShaderOverride(PP::WindowAppearance, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.setShaderParametersOnPaths({PP::WindowOpen}, QVariantMap{{QStringLiteral("strength"), 0.5}}), 1);

        QVERIFY2(!c.shaderParamsAreStale(PP::WindowOpen), "an empty registry declared a stored map orphaned");
    }

    /// A path resolving no pack at all has nothing to mismatch. Covers both
    /// shapes: no ancestor override anywhere, and the engaged-empty sentinel.
    void shaderParamsAreStale_isFalseWithNoResolvedPack()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        PopulatedControllerFixture fx;
        auto& c = fx.c;

        const QString leaf = PP::WindowOpen;
        const PackPair packs = disjointPacks(c, leaf);
        if (!packs.found)
            QSKIP("no two bundled packs declare disjoint parameter ids");

        QCOMPARE(c.setShaderParametersOnPaths({leaf}, QVariantMap{{*declaredIds(c, packs.a).constBegin(), 0.5}}), 1);
        QVERIFY2(!c.shaderParamsAreStale(leaf), "a path resolving no pack was reported as orphaned");

        // The sentinel: engaged effectId, empty string. The path owns a pack
        // slot, so the owns-a-pack guard answers it before the id comparison
        // ever runs.
        QVERIFY(
            c.setShaderOverride(leaf, QString(), c.rawShaderProfile(leaf).value(QStringLiteral("parameters")).toMap()));
        QVERIFY2(!c.shaderParamsAreStale(leaf), "the no-shader sentinel was reported as orphaned");
    }

    // ── the group count and the group clear ─────────────────────────

    /// The count is what the parent's banner reports and the clear acts on, so
    /// the two have to agree on the same population.
    void staleParamDescendants_areCountedThenClearedAsOne()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        PopulatedControllerFixture fx;
        auto& c = fx.c;

        const QString parent = PP::WindowAppearance;
        const PackPair packs = disjointPacks(c, PP::WindowOpen);
        if (!packs.found)
            QSKIP("no two bundled packs declare disjoint parameter ids");
        const QString paramA = *declaredIds(c, packs.a).constBegin();

        QVERIFY(c.setShaderOverride(parent, packs.a, {}));
        QCOMPARE(c.setShaderParametersOnPaths({PP::WindowOpen, PP::WindowClose}, QVariantMap{{paramA, 0.5}}), 2);

        // Nothing is orphaned yet — the ancestor still holds the pack the
        // values were authored against.
        QCOMPARE(c.staleParamDescendantCountForPaths({parent}), 0);

        QVERIFY(c.setShaderOverride(parent, packs.b, {}));
        QCOMPARE(c.staleParamDescendantCountForPaths({parent}), 2);

        QCOMPARE(c.clearStaleParamDescendantsOnPaths({parent}), 2);
        QCOMPARE(c.staleParamDescendantCountForPaths({parent}), 0);

        // The entries are gone rather than emptied: a params-only path with no
        // parameters left has nothing engaged, so storing it would leave a
        // no-op override behind.
        QVERIFY2(c.rawShaderProfile(PP::WindowOpen).isEmpty(), "a cleared descendant kept a no-op override");
        // And it follows the ancestor again, which is the point of the action.
        QCOMPARE(c.resolvedShaderProfile(PP::WindowOpen).value(QStringLiteral("effectId")).toString(), packs.b);
    }

    /// A descendant reachable from two group members is counted once, matching
    /// the sibling shadowing count. A per-path sum would tell the user "2
    /// events" about one event.
    void staleParamDescendantCount_unionsRatherThanSums()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        PopulatedControllerFixture fx;
        auto& c = fx.c;

        const QString parent = PP::WindowAppearance;
        const PackPair packs = disjointPacks(c, PP::WindowOpen);
        if (!packs.found)
            QSKIP("no two bundled packs declare disjoint parameter ids");

        QVERIFY(c.setShaderOverride(parent, packs.a, {}));
        QCOMPARE(
            c.setShaderParametersOnPaths({PP::WindowOpen}, QVariantMap{{*declaredIds(c, packs.a).constBegin(), 0.5}}),
            1);
        QVERIFY(c.setShaderOverride(parent, packs.b, {}));

        // `window` and `window.appearance` both reach `window.appearance.open`.
        QCOMPARE(c.staleParamDescendantCountForPaths({QStringLiteral("window"), parent}), 1);
    }

    /// A descendant that owns its own PACK is the sibling shadowing case, not
    /// this one, and must not be swept up by an action whose banner says the
    /// events below still follow this pack.
    void staleParamDescendants_excludeDescendantsThatOwnAPack()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        PopulatedControllerFixture fx;
        auto& c = fx.c;

        const QString parent = PP::WindowAppearance;
        const PackPair packs = disjointPacks(c, PP::WindowOpen);
        if (!packs.found)
            QSKIP("no two bundled packs declare disjoint parameter ids");
        const QString paramA = *declaredIds(c, packs.a).constBegin();

        QVERIFY(c.setShaderOverride(parent, packs.a, {}));
        QVERIFY(c.setShaderOverride(PP::WindowClose, packs.a, QVariantMap{{paramA, 0.5}}));
        QCOMPARE(c.setShaderParametersOnPaths({PP::WindowOpen}, QVariantMap{{paramA, 0.5}}), 1);
        QVERIFY(c.setShaderOverride(parent, packs.b, {}));

        QCOMPARE(c.staleParamDescendantCountForPaths({parent}), 1);
        QCOMPARE(c.clearStaleParamDescendantsOnPaths({parent}), 1);
        QVERIFY2(storesEffectId(c.rawShaderProfile(PP::WindowClose)), "the clear swept up a path that owned its pack");
    }

    /// The group writers all refuse while an async discard is in flight, and
    /// this one is destructive, so the refusal matters more here than on most
    /// of them. -1 rather than 0: the caller has to be able to tell "refused"
    /// from "nothing to do", and the banner's action would otherwise report
    /// success having cleared nothing.
    void clearStaleParamDescendants_isRefusedWhileAnAsyncDiscardIsInFlight()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        PopulatedControllerFixture fx;
        auto& c = fx.c;

        const QString parent = PP::WindowAppearance;
        const PackPair packs = disjointPacks(c, PP::WindowOpen);
        if (!packs.found)
            QSKIP("no two bundled packs declare disjoint parameter ids");

        QVERIFY(c.setShaderOverride(parent, packs.a, {}));
        QCOMPARE(
            c.setShaderParametersOnPaths({PP::WindowOpen}, QVariantMap{{*declaredIds(c, packs.a).constBegin(), 0.5}}),
            1);
        QVERIFY(c.setShaderOverride(parent, packs.b, {}));
        QCOMPARE(c.staleParamDescendantCountForPaths({parent}), 1);

        // The discard needs something of its own to revert or it never
        // dispatches a worker, and with no worker in flight there is no
        // refusal to observe. A profile override rather than a shader edit:
        // the shader tree lives in Settings, which the discard worker does not
        // own, so only the profile files give it real work.
        QVERIFY(c.setOverride(QStringLiteral("global"), QVariantMap{{QStringLiteral("duration"), 200}}));

        QSignalSpy toasts(&c, &AnimationsPageController::toastRequested);
        QSignalSpy done(&c, &AnimationsPageController::discardResult);
        c.asyncRevertPending();

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("refusing while an async discard is in flight")));
        QCOMPARE(c.clearStaleParamDescendantsOnPaths({parent}), -1);
        QCOMPARE(toasts.count(), 1);

        // The shader tree lives in Settings rather than in the profile files
        // the worker restores, so the refused call leaving it intact is a real
        // no-mutation check rather than a race with the discard.
        QVERIFY2(!c.rawShaderProfile(PP::WindowOpen).isEmpty(), "the refused clear deleted the override anyway");

        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    }

    /// An empty group, and a group of paths that carry nothing, are both zero
    /// rather than a refusal.
    void staleParamDescendants_areZeroForAnEmptyOrUntouchedGroup()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        PopulatedControllerFixture fx;
        auto& c = fx.c;

        QCOMPARE(c.staleParamDescendantCountForPaths({}), 0);
        QCOMPARE(c.clearStaleParamDescendantsOnPaths({}), 0);
        QCOMPARE(c.staleParamDescendantCountForPaths({PP::WindowAppearance}), 0);
        QCOMPARE(c.clearStaleParamDescendantsOnPaths({PP::WindowAppearance}), 0);
    }
};

QTEST_MAIN(TestAnimationsStaleParams)
#include "test_animations_stale_params.moc"
