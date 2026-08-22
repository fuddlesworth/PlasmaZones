// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_shader_param_writes.cpp
 * @brief AnimationsPageController params-only shader writer and the group
 *        readers that answer questions about what a path owns.
 *
 * Split out of test_animations_shader_overrides.cpp rather than added to it:
 * that file was already at 1110 lines against the project's 1150 hard ceiling,
 * and this coverage is a coherent slice on its own — everything here concerns
 * the PACK-versus-PARAMETERS distinction rather than shader overrides in
 * general.
 *
 * Pinned behaviour:
 *   - setShaderParametersOnPaths leaves `effectId` exactly as stored: engaged
 *     where the path owns a pack, unengaged where it inherits one, and
 *     engaged-EMPTY where the path holds the explicit "no shader" sentinel
 *   - an empty parameter map means "own no parameter values", which REMOVES a
 *     params-only entry outright and merely strips the parameters off a path
 *     that also owns a pack
 *   - the group contract the sibling writers share: every path in the group is
 *     written, a path with no shader leg is skipped rather than failed,
 *     repeats are deduplicated, an identical rewrite costs no signal, and an
 *     async discard refuses the whole call with -1 and one toast
 *   - shaderOverrideDescendantCountForPaths sums the same "shadowing
 *     descendant" definition the descendant clear uses, so a parent card
 *     cannot report a count its button would not act on
 *   - anyPathOwnsShaderPack answers "is there a pack here to remove", counting
 *     neither the engaged-empty sentinel nor a params-only entry
 *
 * The distinction these pin is invisible at the moment it is made. A leg that
 * inherits `pixelate` resolves `pixelate` whether or not it also stores that
 * id, so almost every assertion here has to reach for a second observation —
 * changing the parent afterwards, or reading the RAW profile rather than the
 * resolved one — to see which of the two states it is actually in.
 */

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include "config/settings.h"
#include "settings/pages/animationspagecontroller.h"
#include "helpers/AnimationsControllerFixture.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::ControllerFixture;
using PlasmaZones::TestHelpers::PopulatedControllerFixture;
using PlasmaZones::TestHelpers::storesEffectId;
namespace PP = PhosphorAnimation::ProfilePaths;

class TestAnimationsShaderParamWrites : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── effectId is preserved exactly as stored ──────────────────────

    /// The other half of the inheriting-leaf fix, and the one that had no
    /// coverage: on a leg that OWNS its pack, a parameter write must leave that
    /// pack alone.
    ///
    /// Mutation-checked: replacing the writer's `hasStored ? stored :
    /// ShaderProfile{}` with a bare `ShaderProfile{}` fails the effectId
    /// assertion below. That mutation is the obvious way to write this function
    /// and it silently drops an owned pack back to inherited the first time a
    /// slider moves, so it is worth a slot of its own.
    void setShaderParametersOnPaths_onALegThatOwnsItsPackKeepsThatPack()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString parent = PP::WindowAppearance;
        const QString leaf = PP::WindowOpen;

        QVERIFY(c.setShaderOverride(parent, QStringLiteral("pixelate"), {}));
        QVERIFY(c.setShaderOverride(leaf, QStringLiteral("dissolve"), {}));

        QCOMPARE(c.setShaderParametersOnPaths({leaf}, QVariantMap{{QStringLiteral("strength"), 0.7}}), 1);

        const QVariantMap stored = c.rawShaderProfile(leaf);
        QVERIFY2(storesEffectId(stored), "a params write dropped the pack the leg owned");
        QCOMPARE(stored.value(QStringLiteral("effectId")).toString(), QStringLiteral("dissolve"));
        QCOMPARE(stored.value(QStringLiteral("parameters")).toMap().value(QStringLiteral("strength")).toDouble(), 0.7);

        // The payload, mirroring the inheriting-leaf slot's: an owned pack must
        // NOT follow the parent. If the write had dropped the id, the leaf
        // would start tracking the parent here and the assertion above would
        // still have passed on its own.
        QVERIFY(c.setShaderOverride(parent, QStringLiteral("window-morph"), {}));
        QCOMPARE(c.resolvedShaderProfile(leaf).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("dissolve"));
    }

    /// A second parameter write must be as harmless as the first. The writer
    /// re-reads the stored profile every call, so a bug that consumed the id on
    /// first use would only show on the repeat.
    void setShaderParametersOnPaths_repeatedWritesDoNotErodeTheStoredPack()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString leaf = PP::WindowOpen;
        QVERIFY(c.setShaderOverride(leaf, QStringLiteral("dissolve"), {}));

        // Accumulated then asserted, like the group loops in this file: an
        // abort on the first bad iteration would hide whether the erosion
        // starts on the second write or the third, which is the whole point of
        // repeating.
        QStringList wrong;
        for (double v : {0.1, 0.5, 0.9}) {
            if (c.setShaderParametersOnPaths({leaf}, QVariantMap{{QStringLiteral("strength"), v}}) != 1) {
                wrong.append(QStringLiteral("write refused at %1").arg(v));
                continue;
            }
            const QVariantMap stored = c.rawShaderProfile(leaf);
            if (!storesEffectId(stored))
                wrong.append(QStringLiteral("pack dropped at %1").arg(v));
            else if (stored.value(QStringLiteral("effectId")).toString() != QStringLiteral("dissolve"))
                wrong.append(QStringLiteral("pack changed at %1").arg(v));
            if (stored.value(QStringLiteral("parameters")).toMap().value(QStringLiteral("strength")).toDouble() != v)
                wrong.append(QStringLiteral("parameter not stored at %1").arg(v));
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QLatin1String("; "))));
    }

    /// The engaged-EMPTY sentinel is an engaged effectId, so it survives a
    /// params write exactly like a real pack does. Distinguishable from the
    /// inheriting case ONLY by key presence, which is why the helper exists.
    void setShaderParametersOnPaths_keepsTheNoneSentinelEngaged()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString parent = PP::WindowAppearance;
        const QString leaf = PP::WindowOpen;
        QVERIFY(c.setShaderOverride(parent, QStringLiteral("pixelate"), {}));
        // Empty id writes the sentinel: "no shader for this event", which
        // blocks the parent's pack rather than inheriting it.
        QVERIFY(c.setShaderOverride(leaf, QString(), {}));

        QCOMPARE(c.setShaderParametersOnPaths({leaf}, QVariantMap{{QStringLiteral("strength"), 0.7}}), 1);

        const QVariantMap stored = c.rawShaderProfile(leaf);
        QVERIFY2(storesEffectId(stored), "a params write cleared the explicit no-shader sentinel");
        QCOMPARE(stored.value(QStringLiteral("effectId")).toString(), QString());
        // Still blocking: the parent's pack must not have leaked back in.
        QCOMPARE(c.resolvedShaderProfile(leaf).value(QStringLiteral("effectId")).toString(), QString());
    }

    // ── the empty-parameters branch, in all three of its arms ────────

    /// Empty parameters on a path that owns ONLY parameters leaves nothing
    /// engaged, so the entry is removed rather than stored empty. This is the
    /// "revert my parameters to inherited" path.
    void setShaderParametersOnPaths_emptyParamsRemovesAParamsOnlyEntry()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString parent = PP::WindowAppearance;
        const QString leaf = PP::WindowOpen;
        QVERIFY(c.setShaderOverride(parent, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.setShaderParametersOnPaths({leaf}, QVariantMap{{QStringLiteral("strength"), 0.7}}), 1);
        QVERIFY(!c.rawShaderProfile(leaf).isEmpty());

        QCOMPARE(c.setShaderParametersOnPaths({leaf}, QVariantMap{}), 1);

        // Gone entirely, not stored as an empty profile. An empty entry would
        // still read as a real override to the pruner, to the diff and to the
        // ancestor's shadowing walk.
        QVERIFY2(c.rawShaderProfile(leaf).isEmpty(), "an emptied params-only override was stored rather than removed");
        QCOMPARE(c.shaderOverrideDescendantCount(parent), 0);
        // And the leaf is back to following the parent.
        QCOMPARE(c.resolvedShaderProfile(leaf).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("pixelate"));
    }

    /// Empty parameters on a path that ALSO owns a pack strips the parameters
    /// and keeps the pack. The opposite outcome to the slot above, from the
    /// same argument, which is exactly why both need pinning.
    void setShaderParametersOnPaths_emptyParamsKeepsAnOwnedPack()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString leaf = PP::WindowOpen;
        QVERIFY(c.setShaderOverride(leaf, QStringLiteral("dissolve"), QVariantMap{{QStringLiteral("strength"), 0.7}}));

        QCOMPARE(c.setShaderParametersOnPaths({leaf}, QVariantMap{}), 1);

        const QVariantMap stored = c.rawShaderProfile(leaf);
        QVERIFY2(storesEffectId(stored), "stripping the parameters also removed the pack");
        QCOMPARE(stored.value(QStringLiteral("effectId")).toString(), QStringLiteral("dissolve"));
        QVERIFY2(!stored.contains(QStringLiteral("parameters")), "the parameters were not stripped");
    }

    /// Empty parameters on a path holding the sentinel keeps the sentinel, for
    /// the same reason: engaged-empty is still engaged.
    void setShaderParametersOnPaths_emptyParamsKeepsTheNoneSentinel()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString leaf = PP::WindowOpen;
        QVERIFY(c.setShaderOverride(leaf, QString(), {}));

        QCOMPARE(c.setShaderParametersOnPaths({leaf}, QVariantMap{}), 1);

        QVERIFY2(storesEffectId(c.rawShaderProfile(leaf)), "an empty params write removed the no-shader sentinel");
    }

    /// Empty parameters on a path with NOTHING stored writes nothing, and still
    /// reports the path as holding the requested end state.
    void setShaderParametersOnPaths_emptyParamsOnAnUntouchedPathIsANoOp()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QSignalSpy dirtied(&c, &AnimationsPageController::pendingChangesChanged);
        QCOMPARE(c.setShaderParametersOnPaths({PP::WindowOpen}, QVariantMap{}), 1);
        QVERIFY(c.rawShaderProfile(PP::WindowOpen).isEmpty());
        QCOMPARE(dirtied.count(), 0);
    }

    // ── the group contract ───────────────────────────────────────────

    void setShaderParametersOnPaths_writesEveryPathInTheGroup()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QStringList group{PP::WindowOpen, PP::WindowClose};
        QCOMPARE(c.setShaderParametersOnPaths(group, QVariantMap{{QStringLiteral("strength"), 0.7}}), 2);

        // Accumulate then assert, so a failure names every offending path
        // instead of aborting on the first.
        QStringList wrong;
        for (const QString& path : group) {
            if (c.rawShaderProfile(path)
                    .value(QStringLiteral("parameters"))
                    .toMap()
                    .value(QStringLiteral("strength"))
                    .toDouble()
                != 0.7)
                wrong.append(path);
        }
        QVERIFY2(wrong.isEmpty(),
                 qPrintable(QStringLiteral("paths missing the write: ") + wrong.join(QLatin1String(", "))));
    }

    /// A path with no shader leg is SKIPPED, not failed, so a group mixing
    /// supporting and non-supporting paths still reports the supporting ones.
    void setShaderParametersOnPaths_skipsPathsWithNoShaderLeg()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString supported = PP::WindowOpen;
        // Precondition, so this slot fails loudly rather than vacuously if the
        // taxonomy ever gives this path a shader leg.
        QVERIFY(c.supportsShaderLeg(supported));
        const QString unsupported = PP::EditorSnapIn;
        QVERIFY(!c.supportsShaderLeg(unsupported));

        QCOMPARE(c.setShaderParametersOnPaths({supported, unsupported}, QVariantMap{{QStringLiteral("strength"), 0.7}}),
                 1);
        QVERIFY(!c.rawShaderProfile(supported).isEmpty());
        QVERIFY(c.rawShaderProfile(unsupported).isEmpty());
    }

    /// The group is deduplicated on entry, so a card whose mirror list repeats
    /// its own path does not double-count.
    void setShaderParametersOnPaths_deduplicatesRepeatedPaths()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QVariantMap params{{QStringLiteral("strength"), 0.7}};
        QCOMPARE(c.setShaderParametersOnPaths({PP::WindowOpen, PP::WindowClose, PP::WindowOpen}, params), 2);
    }

    /// One tree write per call, so one signal — and none at all when the
    /// requested state already holds. The zero-write half is what keeps a
    /// slider resting on its current value from paying a write per tick.
    void setShaderParametersOnPaths_emitsOnceAndSkipsIdenticalRewrite()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QStringList group{PP::WindowOpen, PP::WindowClose};
        const QVariantMap params{{QStringLiteral("strength"), 0.7}};

        QSignalSpy dirtied(&c, &AnimationsPageController::pendingChangesChanged);
        QCOMPARE(c.setShaderParametersOnPaths(group, params), 2);
        QCOMPARE(dirtied.count(), 1);

        // Identical rewrite: still reports both paths as holding the state,
        // still writes nothing.
        QCOMPARE(c.setShaderParametersOnPaths(group, params), 2);
        QCOMPARE(dirtied.count(), 1);
    }

    /// The refusal parity every sibling group writer has: refused as a whole,
    /// -1 rather than 0, exactly one toast, and nothing written.
    void setShaderParametersOnPaths_refusesWhileAsyncDiscardIsInFlight()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;
        QVERIFY(c.setOverride(PP::Popup, QVariantMap{{QStringLiteral("duration"), 200}}));

        QSignalSpy done(&c, &AnimationsPageController::discardResult);
        QSignalSpy toasts(&c, &AnimationsPageController::toastRequested);
        c.asyncRevertPending();
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(QStringLiteral("setShaderParametersOnPaths: refusing while an async discard")));
        QCOMPARE(c.setShaderParametersOnPaths({PP::WindowOpen}, QVariantMap{{QStringLiteral("strength"), 0.7}}), -1);
        QCOMPARE(toasts.count(), 1);
        QVERIFY(c.rawShaderProfile(PP::WindowOpen).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    }

    // ── the group readers ────────────────────────────────────────────

    /// The group accessor must agree with the per-path one it replaced, summed.
    /// It exists for cost, not for behaviour, so the behaviour has to match.
    void shaderOverrideDescendantCountForPaths_matchesTheSumOfThePerPathCounts()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.setShaderOverride(PP::PopupLayoutPickerShow, QStringLiteral("pixelate"), {}));
        QVERIFY(c.setShaderOverride(PP::PopupZoneSelectorShow, QStringLiteral("dissolve"), {}));

        const QStringList group{PP::Popup, PP::Window};
        int expected = 0;
        for (const QString& path : group)
            expected += c.shaderOverrideDescendantCount(path);
        QCOMPARE(c.shaderOverrideDescendantCountForPaths(group), expected);
        QCOMPARE(c.shaderOverrideDescendantCountForPaths(group), 2);
    }

    /// It inherits the params-only exclusion, so it cannot report a descendant
    /// the paired clear would not remove.
    void shaderOverrideDescendantCountForPaths_ignoresAParamsOnlyDescendant()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString parent = PP::WindowAppearance;
        QVERIFY(c.setShaderOverride(parent, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.setShaderParametersOnPaths({PP::WindowOpen}, QVariantMap{{QStringLiteral("strength"), 0.7}}), 1);
        // The write really did store something — without this the count below
        // would be satisfied by a writer that dropped the call on the floor.
        QVERIFY(!c.rawShaderProfile(PP::WindowOpen).isEmpty());

        QCOMPARE(c.shaderOverrideDescendantCountForPaths({parent}), 0);
    }

    /// Deduplicated like the writers, so a parent listed twice is not counted
    /// twice.
    void shaderOverrideDescendantCountForPaths_deduplicatesRepeatedPaths()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.setShaderOverride(PP::PopupLayoutPickerShow, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.shaderOverrideDescendantCountForPaths({PP::Popup, PP::Popup}), 1);
    }

    /// "Is there a pack here to remove?" — the question the shader row's remove
    /// control asks. An owned pack answers yes.
    void anyPathOwnsShaderPack_isTrueForAnOwnedPack()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(!c.anyPathOwnsShaderPack({PP::WindowOpen}));
        QVERIFY(c.setShaderOverride(PP::WindowOpen, QStringLiteral("dissolve"), {}));
        QVERIFY(c.anyPathOwnsShaderPack({PP::WindowOpen}));
    }

    /// An INHERITED pack is not one this event owns, even though the event
    /// resolves it and the row renders it identically.
    void anyPathOwnsShaderPack_isFalseForAnInheritedPack()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.setShaderOverride(PP::WindowAppearance, QStringLiteral("pixelate"), {}));
        // Resolves the pack...
        QCOMPARE(c.resolvedShaderProfile(PP::WindowOpen).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("pixelate"));
        // ...but does not own it.
        QVERIFY(!c.anyPathOwnsShaderPack({PP::WindowOpen}));
    }

    /// Neither the sentinel nor a params-only entry is a pack to remove.
    void anyPathOwnsShaderPack_isFalseForTheSentinelAndForParamsOnly()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.setShaderOverride(PP::WindowOpen, QString(), {}));
        QVERIFY2(!c.anyPathOwnsShaderPack({PP::WindowOpen}), "the no-shader sentinel was counted as an owned pack");

        QVERIFY(c.setShaderOverride(PP::WindowAppearance, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.setShaderParametersOnPaths({PP::WindowClose}, QVariantMap{{QStringLiteral("strength"), 0.7}}), 1);
        QVERIFY(!c.rawShaderProfile(PP::WindowClose).isEmpty());
        QVERIFY2(!c.anyPathOwnsShaderPack({PP::WindowClose}), "a params-only override was counted as an owned pack");
    }

    /// ANY, not ALL: a mixed group answers yes, because the remove arm clears
    /// the owner and is a no-op on the rest, while the other arm would write
    /// the blocking sentinel over a pack the user did choose.
    void anyPathOwnsShaderPack_isTrueWhenOnlyOneGroupMemberOwnsAPack()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.setShaderOverride(PP::WindowClose, QStringLiteral("dissolve"), {}));
        QVERIFY(!c.anyPathOwnsShaderPack({PP::WindowOpen}));
        QVERIFY(c.anyPathOwnsShaderPack({PP::WindowOpen, PP::WindowClose}));
    }

    void anyPathOwnsShaderPack_isFalseForAnEmptyList()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(!c.anyPathOwnsShaderPack({}));
    }

    // ─── Shader-leg group writers ─────────────────────────────────────────
    //
    // The timing-side group writers are covered in
    // test_animations_group_writes.cpp, which constructs a bare controller.
    // These three need a real ISettings, because without one the shader tree is
    // unreachable and every one of them is a no-op that proves nothing.

    /// Writes land on every path in the group, not just the primary. A card
    /// that wrote only its own path would leave its mirror out of step and the
    /// divergence banner would report a difference the user never caused.
    void setShaderOverrideOnPaths_writesEveryPathInTheGroup()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QStringList group{PP::WindowOpen, PP::WindowClose};

        QCOMPARE(c.setShaderOverrideOnPaths(group, QStringLiteral("pixelate"), {}), 2);

        QStringList wrong;
        for (const QString& path : group) {
            if (c.rawShaderProfile(path).value(QStringLiteral("effectId")).toString() != QStringLiteral("pixelate"))
                wrong.append(path);
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral(", "))));
        QVERIFY(c.allPathsHoldShaderEffect(group, QStringLiteral("pixelate")));

        // The dedup contract every group writer opens with. QML builds a write
        // group as `[eventPath].concat(mirrorPaths)` and does not deduplicate,
        // so a card naming its own path as a mirror hands the same one in
        // twice. Untested here until now, and the shared helper's own coverage
        // lives with the timing-side writers, so a shader-side regression that
        // bypassed it would have gone unnoticed on this side.
        QCOMPARE(c.setShaderOverrideOnPaths(QStringList{PP::WindowOpen, PP::WindowClose, PP::WindowOpen},
                                            QStringLiteral("dissolve"), {}),
                 2);
    }

    /// A path that cannot host a shader leg is SKIPPED, not attempted. The
    /// count reflects that, and the supporting sibling in the same group is
    /// still written — a group mixing the two must not be all-or-nothing.
    /// The shader-leg taxonomy itself, which two assertions inside the group
    /// writer's skip slot used to pin as a side effect. Given its own slot
    /// because a taxonomy regression should fail a slot named for the
    /// taxonomy, not one named for a group writer.
    ///
    /// The strip pass made scrolling.view a consumed shader leaf (its
    /// ancestors join via the walk-up), so the Strip Scrolled card shows the
    /// picker and the pruner keeps its overrides.
    void supportsShaderLeg_coversTheStripSubtree()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.supportsShaderLeg(PP::ScrollingView));
        QVERIFY(c.supportsShaderLeg(PP::Scrolling));
    }

    void setShaderOverrideOnPaths_skipsPathsWithNoShaderLeg()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString supported = PP::WindowOpen;
        const QString unsupported = PP::EditorSnapIn;
        QVERIFY(c.supportsShaderLeg(supported));
        QVERIFY(!c.supportsShaderLeg(unsupported));

        QCOMPARE(c.setShaderOverrideOnPaths(QStringList{supported, unsupported}, QStringLiteral("pixelate"), {}), 1);

        QCOMPARE(c.rawShaderProfile(supported).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("pixelate"));
        QVERIFY(c.rawShaderProfile(unsupported).isEmpty());
    }

    /// Clearing returns the group to inheritance and reports how many paths
    /// actually held an override. A path that held none is not counted, so the
    /// return distinguishes "cleared two" from "there was nothing to clear".
    void clearShaderOverrideOnPaths_countsOnlyPathsThatHeldOne()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString primary = PP::WindowOpen;
        const QString mirror = PP::WindowClose;
        QVERIFY(c.setShaderOverride(primary, QStringLiteral("pixelate"), {}));

        QCOMPARE(c.clearShaderOverrideOnPaths(QStringList{primary, mirror}), 1);
        QVERIFY(c.rawShaderProfile(primary).isEmpty());
        QVERIFY(c.rawShaderProfile(mirror).isEmpty());
        // Cleared is not the engaged-empty "None" sentinel: a path with no
        // override does not hold the empty string either.
        QVERIFY(!c.allPathsHoldShaderEffect(QStringList{primary}, QString()));
    }

    /// The engaged-empty sentinel IS a stored value, distinct from having no
    /// override at all. `allPathsHoldShaderEffect` has to tell them apart or
    /// the card's picker renders "None" as an unset row.
    void allPathsHoldShaderEffect_distinguishesTheNoneSentinelFromNoOverride()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString primary = PP::WindowOpen;
        const QString mirror = PP::WindowClose;
        const QStringList group{primary, mirror};

        QVERIFY(!c.allPathsHoldShaderEffect(group, QString()));
        QCOMPARE(c.setShaderOverrideOnPaths(group, QString(), {}), 2);
        QVERIFY(c.allPathsHoldShaderEffect(group, QString()));
        QVERIFY(!c.allPathsHoldShaderEffect(group, QStringLiteral("pixelate")));
    }

    /// Descendant clears sum across the group. Pinned alongside the refusal
    /// case below, which is the half that actually needs the sentinel.
    void clearShaderOverrideDescendantsOnPaths_sumsAcrossTheGroup()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.setShaderOverride(PP::PopupLayoutPickerShow, QStringLiteral("pixelate"), {}));
        QVERIFY(c.setShaderOverride(PP::PopupZoneSelectorShow, QStringLiteral("pixelate"), {}));

        // One shadowing descendant under each parent, so the group total is 2
        // rather than either parent's 1.
        QCOMPARE(c.clearShaderOverrideDescendantsOnPaths(QStringList{PP::PopupLayoutPicker, PP::PopupZoneSelector}), 2);
        QVERIFY(c.rawShaderProfile(PP::PopupLayoutPickerShow).isEmpty());
        QVERIFY(c.rawShaderProfile(PP::PopupZoneSelectorShow).isEmpty());

        // The emit contract, which this slot did not record either way. The
        // wrapper calls the singular clear once per path and the singular
        // writes the tree per call, so a two-parent group emits per parent —
        // it does NOT have the batch-once property its singular counterpart
        // goes out of its way to pin. Written down rather than left implicit,
        // because the next person to batch this needs to know which of the two
        // shapes is the current one.
        QSignalSpy dirtied(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(PP::PopupLayoutPickerShow, QStringLiteral("pixelate"), {}));
        QVERIFY(c.setShaderOverride(PP::WindowOpen, QStringLiteral("pixelate"), {}));
        dirtied.clear();
        QCOMPARE(c.clearShaderOverrideDescendantsOnPaths({PP::Popup, PP::WindowAppearance}), 2);
        QCOMPARE(dirtied.count(), 2);
    }

    /// The wrapper refuses as a WHOLE while an async discard owns the tree:
    /// -1 rather than a partial count, and exactly one toast however many paths
    /// were listed.
    ///
    /// Two paths here for the shape of the call, not because the second proves
    /// anything the first does not. The gate is at the TOP of the wrapper,
    /// before the per-path loop runs at all, so a one-path list produces the
    /// same -1 and the same single toast. The per-path "stops at the first
    /// refusal" arm inside the loop is unreachable from here for the same
    /// reason, and unreachable from anywhere else too: the only way the
    /// singular clear returns negative is that same flag, which this gate has
    /// already caught.
    void clearShaderOverrideDescendantsOnPaths_reportsARefusalRatherThanASmallerCount()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        QVERIFY(c.setShaderOverride(PP::PopupLayoutPickerShow, QStringLiteral("pixelate"), {}));
        QVERIFY(c.setShaderOverride(PP::PopupZoneSelectorShow, QStringLiteral("pixelate"), {}));
        // A FILE-backed pending change too: a tree-only discard completes
        // synchronously (no worker, no in-flight window), so the refusal
        // this slot pins requires a snapshot for the worker to restore.
        QVERIFY(c.setOverride(PP::Popup, QVariantMap{{QStringLiteral("duration"), 200}}));

        // TWO paths, and that is the point. With one path, `cleared += -1` and
        // an early `return -1` are indistinguishable — both yield -1 — so a
        // single-path fixture cannot pin either the "never summed in" rule or
        // the "stops at the first refusal" one. The gate is global, so both
        // paths refuse: summing would give -2.
        const QStringList group{PP::PopupLayoutPicker, PP::PopupZoneSelector};

        // An async discard owning the tree is what the -1 sentinel reports.
        // The gate is cleared only in the watcher's `finished` handler, and no
        // event loop spins between here and the call below, so it is still up.
        QSignalSpy toasts(&c, &AnimationsPageController::toastRequested);
        QSignalSpy done(&c, &AnimationsPageController::discardResult);
        c.asyncRevertPending();

        // The refusal is reported by the method's own top-level async gate,
        // which short-circuits before any per-path work — so this is the
        // *OnPaths wrapper's message, not the per-path singular's.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral(
                                 "clearShaderOverrideDescendantsOnPaths: refusing while an async discard")));
        // Spy attached before the refused call, for the same reason as its
        // timing-side twin in test_animations_group_writes: a refusal must
        // mutate nothing, and the tree's contents here would race the discard
        // worker's restore. The worker cannot emit without an event-loop spin,
        // and none happens between the call and these assertions.
        QSignalSpy dirtied(&c, &AnimationsPageController::pendingChangesChanged);

        QCOMPARE(c.clearShaderOverrideDescendantsOnPaths(group), -1);
        // One toast for the whole call, not one per path. Note this is the
        // TOP-LEVEL gate refusing before the loop runs, not the loop stopping
        // at a first refusal — see the slot comment.
        QCOMPARE(toasts.count(), 1);
        QCOMPARE(dirtied.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    }

    /// The GROUP write path — the only one QML uses — applies the SAME
    /// effect-id boundary check as the per-path setter. Before this slot,
    /// deleting the acceptableShaderEffectId gate in setShaderOverrideOnPaths
    /// left the suite green while a typo'd id flowed into the persisted tree.
    void setShaderOverrideOnPaths_rejectsUnknownEffectIdWithPopulatedRegistry()
    {
        SKIP_WITHOUT_BUNDLED_PACKS();

        PopulatedControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;
        QVERIFY2(!registry.effectIds().isEmpty(), "precondition: registry populated so the gate is armed");

        const QStringList group{PP::PopupLayoutPickerShow, PP::PopupZoneSelectorShow};
        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QCOMPARE(c.setShaderOverrideOnPaths(group, QStringLiteral("no-such-effect"), {}), -1);
        QCOMPARE(spy.count(), 0);
        for (const QString& path : group)
            QVERIFY2(c.rawShaderProfile(path).isEmpty(), "refused group write must not touch the tree");

        QVERIFY(registry.hasEffect(QStringLiteral("pixelate")));
        QCOMPARE(c.setShaderOverrideOnPaths(group, QStringLiteral("pixelate"), {}), 2);
    }

    /// Async-refusal parity for the group setter, matching the family's
    /// per-path and descendants twins: -1, one toast, no tree write.
    void setShaderOverrideOnPaths_refusesWhileAsyncDiscardIsInFlight()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;
        QVERIFY(c.setOverride(PP::Popup, QVariantMap{{QStringLiteral("duration"), 200}}));

        QSignalSpy done(&c, &AnimationsPageController::discardResult);
        QSignalSpy toasts(&c, &AnimationsPageController::toastRequested);
        c.asyncRevertPending();
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(QStringLiteral("setShaderOverrideOnPaths: refusing while an async discard")));
        QCOMPARE(c.setShaderOverrideOnPaths({PP::PopupLayoutPickerShow}, QStringLiteral("dissolve"), {}), -1);
        QCOMPARE(toasts.count(), 1);
        QVERIFY(c.rawShaderProfile(PP::PopupLayoutPickerShow).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    }

    /// And the group clearer's refusal.
    void clearShaderOverrideOnPaths_refusesWhileAsyncDiscardIsInFlight()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;
        QVERIFY(c.setShaderOverride(PP::PopupLayoutPickerShow, QStringLiteral("pixelate"), {}));
        QVERIFY(c.setOverride(PP::Popup, QVariantMap{{QStringLiteral("duration"), 200}}));

        QSignalSpy done(&c, &AnimationsPageController::discardResult);
        QSignalSpy toasts(&c, &AnimationsPageController::toastRequested);
        c.asyncRevertPending();
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(QStringLiteral("clearShaderOverrideOnPaths: refusing while an async discard")));
        QCOMPARE(c.clearShaderOverrideOnPaths({PP::PopupLayoutPickerShow}), -1);
        QCOMPARE(toasts.count(), 1);
        QVERIFY2(!c.rawShaderProfile(PP::PopupLayoutPickerShow).isEmpty(),
                 "refused clear must leave the stored override in place");
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    }

    /// The non-empty parameters branch of the group setter, plus its
    /// compare-and-skip: params land in the persisted tree for every path,
    /// and re-sending the identical group still REPORTS every path (the
    /// requested end state holds, so each is counted as written) while
    /// performing zero settings writes — which is what keeps a param slider
    /// resting on its value from paying a write per tick. The zero-write half
    /// is observed through the absence of any dirty announcement.
    void setShaderOverrideOnPaths_writesParametersAndSkipsIdenticalRewrite()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QStringList group{PP::PopupLayoutPickerShow, PP::PopupZoneSelectorShow};
        const QVariantMap params{{QStringLiteral("strength"), 0.7}};
        QCOMPARE(c.setShaderOverrideOnPaths(group, QStringLiteral("pixelate"), params), 2);
        for (const QString& path : group) {
            const QVariantMap stored = c.rawShaderProfile(path);
            QCOMPARE(stored.value(QStringLiteral("effectId")).toString(), QStringLiteral("pixelate"));
            QCOMPARE(stored.value(QStringLiteral("parameters")).toMap().value(QStringLiteral("strength")).toDouble(),
                     0.7);
        }

        QSignalSpy dirtied(&c, &AnimationsPageController::pendingChangesChanged);
        QCOMPARE(c.setShaderOverrideOnPaths(group, QStringLiteral("pixelate"), params), 2);
        QCOMPARE(dirtied.count(), 0);
    }

    /// A parameter tweak on a leaf that INHERITS its pack must not pin that
    /// pack at the leaf.
    ///
    /// The pin is invisible at the moment it happens — the leaf resolves the
    /// same pack either way — and only shows up later, when the parent's pack
    /// changes and the leaf silently does not follow. So the payload of this
    /// test is the last two lines, not the effectId assertion.
    void setShaderParametersOnPaths_onAnInheritingLeafDoesNotPinTheParentsPack()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString parent = PP::WindowAppearance;
        const QString leaf = PP::WindowOpen;

        QVERIFY(c.setShaderOverride(parent, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.resolvedShaderProfile(leaf).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("pixelate"));

        QVariantMap params;
        params.insert(QStringLiteral("strength"), 0.7);
        QCOMPARE(c.setShaderParametersOnPaths({leaf}, params), 1);

        // The params landed, and NO effectId key was stored: an absent key is
        // the "inherit the pack" state, distinct from an engaged-empty one.
        const QVariantMap stored = c.rawShaderProfile(leaf);
        QCOMPARE(stored.value(QStringLiteral("parameters")).toMap().value(QStringLiteral("strength")).toDouble(), 0.7);
        QVERIFY2(!stored.contains(QStringLiteral("effectId")),
                 "a params-only write pinned an effectId, severing the cascade");

        // The payload: change the pack above, and the leaf follows.
        QVERIFY(c.setShaderOverride(parent, QStringLiteral("dissolve"), {}));
        QCOMPARE(c.resolvedShaderProfile(leaf).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("dissolve"));
    }

    /// A params-only descendant does not "shadow" its parent, so the parent's
    /// card must not offer to clear it.
    ///
    /// Pinned because the warning it drives carries a one-click destructive
    /// action: counting a params-only child meant the parent offered to delete
    /// a parameter tweak the user had just made on the child.
    void shaderOverrideDescendantCount_ignoresAParamsOnlyDescendant()
    {
        ControllerFixture fx;
        [[maybe_unused]] auto& [guard, settings, registry, c] = fx;

        const QString parent = PP::WindowAppearance;
        const QString leaf = PP::WindowOpen;

        QVERIFY(c.setShaderOverride(parent, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.shaderOverrideDescendantCount(parent), 0);

        QVariantMap params;
        params.insert(QStringLiteral("strength"), 0.7);
        QCOMPARE(c.setShaderParametersOnPaths({leaf}, params), 1);
        // The entry's SHAPE, not just the count. Without this the zero below is
        // also satisfied by a writer that stored nothing at all, and by one
        // that stored an effectId the walk then failed to notice.
        const QVariantMap stored = c.rawShaderProfile(leaf);
        QVERIFY2(!stored.isEmpty(), "the params write stored nothing, so the count below proves nothing");
        QVERIFY2(!stored.contains(QStringLiteral("effectId")),
                 "the params write pinned an effectId, so this is no longer the params-only case");
        QCOMPARE(c.shaderOverrideDescendantCount(parent), 0);

        // An ENGAGED effectId at the leaf does shadow, even when it names the
        // same pack the parent currently resolves: the two are one pack today
        // and independent tomorrow, and that pin is what the warning is for.
        QVERIFY(c.setShaderOverride(leaf, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.shaderOverrideDescendantCount(parent), 1);
        // What that pick did to the parameters, which the slot used to walk
        // past: picking a pack is a switch, and a switch drops the previous
        // parameter map rather than carrying it onto the new pack.
        QVERIFY(!c.rawShaderProfile(leaf).contains(QStringLiteral("parameters")));
    }
};

QTEST_MAIN(TestAnimationsShaderParamWrites)
#include "test_animations_shader_param_writes.moc"
