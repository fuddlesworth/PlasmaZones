// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_group_writes.cpp
 * @brief The group-write API an event card applies across its whole write-path
 *        group: the per-field merge, the per-field clear, the shader-leg group
 *        queries, and the divergence measure.
 *
 * These rules used to live as JS loops inside AnimationEventCard.qml, where the
 * only thing that could pin them was a textual scrape of the QML source. They
 * are now Q_INVOKABLEs on the controller, so each is driven directly here.
 *
 * The three properties worth pinning, and the bug each one prevents:
 *   - the merge is OVER each path's stored profile, so a motion set's fields
 *     (minDistance, sequenceMode, staggerInterval, presetName) survive a
 *     duration edit instead of being truncated
 *   - an absent `curveFromCommit` means "the user did not touch the curve", so
 *     each path keeps its own rather than being handed a copy of the resolved
 *     one, which would silently stop it tracking its parent
 *   - divergence is measured only on what a single edit can converge, so the
 *     card's banner cannot latch on over an axis no control can clear
 *
 * Isolation is the fixture's `IsolatedConfigGuard`: the timing writes these
 * slots drive are config keys, so that guard is what keeps them off the real
 * user config. `setUserProfilesDirOverride()` still redirects the preset and
 * motion-set FILES, which this file does not exercise.
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVariant>

#include "config/configdefaults.h"
#include "config/configkeys.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"
#include "settings/pages/animationspagecontroller.h"
#include "helpers/AnimationsControllerFixture.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// The card group these slots use throughout: the two window.appearance legs,
/// which is the real mirrored group in the shipped UI (open mirrored onto
/// close) and the only one where mirroring is exercised at all.
const QString kPrimary = QStringLiteral("window.appearance.open");
const QString kMirror = QStringLiteral("window.appearance.close");
QStringList group()
{
    return QStringList{kPrimary, kMirror};
}

} // namespace

class TestAnimationsGroupWrites : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Guards against a leaked process-wide registry publish from an earlier
    /// slot, the same way the sibling animation-controller test files do.
    void init()
    {
        QCOMPARE(PhosphorAnimation::PhosphorProfileRegistry::defaultRegistry(), nullptr);
    }

    // ─── setOverrideMergedOnPaths ─────────────────────────────────────────

    /// The whole reason the writer merges rather than replaces. A motion set
    /// writes minDistance / sequenceMode / staggerInterval / presetName to a
    /// leaf; a card that replaced the map would drop all four the moment the
    /// user nudged Duration.
    void mergePreservesTheFieldsTheCardDoesNotEdit()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        // Stand in for what a motion set left on the leaf.
        QVERIFY(c.setOverride(kPrimary,
                              QVariantMap{{QStringLiteral("duration"), 200},
                                          {QStringLiteral("minDistance"), 42},
                                          {QStringLiteral("staggerInterval"), 30},
                                          {QStringLiteral("presetName"), QStringLiteral("Snappy")}}));

        // The card edits ONE field.
        QCOMPARE(c.setOverrideMergedOnPaths(QStringList{kPrimary}, QVariantMap{{QStringLiteral("duration"), 900}},
                                            QVariant()),
                 1);

        const QVariantMap after = c.rawProfile(kPrimary);
        QCOMPARE(after.value(QStringLiteral("duration")).toInt(), 900);
        QCOMPARE(after.value(QStringLiteral("minDistance")).toInt(), 42);
        QCOMPARE(after.value(QStringLiteral("staggerInterval")).toInt(), 30);
        QCOMPARE(after.value(QStringLiteral("presetName")).toString(), QStringLiteral("Snappy"));
    }

    /// Every path in the group is written, not just the primary. A card that
    /// wrote only its own path would leave its mirror silently out of step, and
    /// the divergence banner would then report a difference the user never
    /// caused.
    void mergeWritesEveryPathInTheGroup()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QCOMPARE(c.setOverrideMergedOnPaths(group(), QVariantMap{{QStringLiteral("duration"), 750}}, QVariant()), 2);

        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("duration")).toInt(), 750);
        QCOMPARE(c.rawProfile(kMirror).value(QStringLiteral("duration")).toInt(), 750);
    }

    /// QML builds a group as `[eventPath].concat(mirrorPaths)` without
    /// deduping, so the primary can arrive in its own mirror list. The write
    /// side must apply each path ONCE: a doubled write is a doubled
    /// overrideChanged storm at drag rate, and for clearFieldOnPaths a doubled
    /// count misreports how many events were actually reverted.
    void aDuplicatedPathInTheGroupIsWrittenOnce()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QSignalSpy touched(&c, &AnimationsPageController::overrideChanged);
        // Two, not three: the repeated primary is deduplicated on entry.
        QCOMPARE(c.setOverrideMergedOnPaths(QStringList{kPrimary, kPrimary, kMirror},
                                            QVariantMap{{QStringLiteral("duration"), 750}}, QVariant()),
                 2);
        int primaryEmits = 0;
        for (int i = 0; i < touched.count(); ++i) {
            if (touched.at(i).at(0).toString() == kPrimary)
                ++primaryEmits;
        }
        QCOMPARE(primaryEmits, 1);

        // And the clear side counts the duplicated path once.
        QCOMPARE(c.clearFieldOnPaths(QStringList{kPrimary, kPrimary, kMirror}, QStringLiteral("duration")), 2);
    }

    /// An INVALID QVariant is QML's `undefined` arriving here, and it means the
    /// user did not touch the curve. Each path must keep its OWN: the path that
    /// owns one keeps it, and the path that inherits stays inheriting. Handing
    /// both the resolved curve instead is the defect this distinction exists to
    /// prevent — the inheriting path would silently stop tracking its parent.
    void anAbsentCurveLeavesEachPathsOwnCurveAlone()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        // Primary owns a curve; the mirror owns none.
        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")}}));

        QCOMPARE(c.setOverrideMergedOnPaths(group(), QVariantMap{{QStringLiteral("duration"), 300}}, QVariant()), 2);

        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("curve")).toString(), QStringLiteral("0.4,0,0.2,1"));
        QVERIFY2(!c.rawProfile(kMirror).contains(QStringLiteral("curve")),
                 "a duration-only edit pinned a curve on a path that was inheriting one");
        QCOMPARE(c.rawProfile(kMirror).value(QStringLiteral("duration")).toInt(), 300);
    }

    /// The other half: a curve the user actually edited travels to every path,
    /// overwriting whatever each held.
    void anEditedCurveTravelsToEveryPath()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")}}));

        QCOMPARE(c.setOverrideMergedOnPaths(group(), QVariantMap{}, QVariant(QStringLiteral("spring:14.00,0.60"))), 2);

        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("curve")).toString(), QStringLiteral("spring:14.00,0.60"));
        QCOMPARE(c.rawProfile(kMirror).value(QStringLiteral("curve")).toString(), QStringLiteral("spring:14.00,0.60"));
    }

    /// A curve arriving through @p fields, rather than through
    /// `curveFromCommit`, must NOT be written. `fields` is "what the user
    /// edited on this card"; the curve is decided separately precisely so a
    /// caller cannot hand every path a curve none of them chose. A path with no
    /// curve of its own must come out of the merge still carrying none.
    ///
    /// This is also the only route that reaches the drop at all: `rawProfile`
    /// sanitises a present-but-empty stored curve away on read, so the merge
    /// base can never carry one and a fixture built that way passes vacuously.
    void aCurveSuppliedThroughFieldsIsNotWritten()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 200}}));

        QCOMPARE(c.setOverrideMergedOnPaths(QStringList{kPrimary},
                                            QVariantMap{{QStringLiteral("duration"), 400},
                                                        {QStringLiteral("curve"), QStringLiteral("0.9,0,0.1,1")}},
                                            QVariant()),
                 1);

        const QVariantMap after = c.rawProfile(kPrimary);
        QCOMPARE(after.value(QStringLiteral("duration")).toInt(), 400);
        QVERIFY2(!after.contains(QStringLiteral("curve")),
                 "a curve passed through `fields` was written, so the card decided a curve on the user's behalf");
    }

    /// The same rule the other way round: a path that DOES own a curve keeps
    /// its own, and `fields` cannot overwrite it either.
    void aCurveSuppliedThroughFieldsCannotOverwriteAPathsOwn()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")}}));

        // ZERO paths written, and that is the assertion rather than an
        // inconvenience: the path already owns a curve, `fields` cannot
        // overwrite it, so the merged object matches what is on disk and the
        // writer correctly finds nothing to do. The count distinguishes that
        // from a refusal (-1) and from a real write (1), which the old boolean
        // return could not.
        QCOMPARE(c.setOverrideMergedOnPaths(QStringList{kPrimary},
                                            QVariantMap{{QStringLiteral("curve"), QStringLiteral("0.9,0,0.1,1")}},
                                            QVariant()),
                 0);

        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("curve")).toString(), QStringLiteral("0.4,0,0.2,1"));
    }

    // ─── clearFieldOnPaths ────────────────────────────────────────────────

    /// One field goes, the other stays. This is the per-field revert link.
    void clearingOneFieldLeavesTheOtherAndTheMotionSetFieldsPut()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        for (const QString& path : group()) {
            QVERIFY(c.setOverride(path,
                                  QVariantMap{{QStringLiteral("duration"), 600},
                                              {QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")},
                                              {QStringLiteral("minDistance"), 42}}));
        }

        QCOMPARE(c.clearFieldOnPaths(group(), QStringLiteral("duration")), 2);

        // Collected, not asserted per iteration: QVERIFY/QCOMPARE return from
        // the slot on the first failure, so a two-path regression would report
        // as one and the mirror's state would never be checked at all.
        QStringList wrong;
        for (const QString& path : group()) {
            const QVariantMap raw = c.rawProfile(path);
            if (raw.contains(QStringLiteral("duration")))
                wrong.append(path + QStringLiteral(": duration survived the clear"));
            if (raw.value(QStringLiteral("curve")).toString() != QStringLiteral("0.4,0,0.2,1"))
                wrong.append(path + QStringLiteral(": curve did not survive the clear"));
            if (raw.value(QStringLiteral("minDistance")).toInt() != 42)
                wrong.append(path + QStringLiteral(": minDistance did not survive the clear"));
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral("; "))));
    }

    /// A path whose override empties out has its FILE removed, not left as an
    /// empty object. The two resolve identically, but the card's Override toggle
    /// and the pending-changes walk both key on the file existing — so an empty
    /// file leaves the toggle stuck on with nothing behind it.
    void clearingTheLastFieldRemovesTheEntryOutright()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 600}}));
        QVERIFY(c.hasOverride(kPrimary));

        // The REMOVED path owes exactly one overrideChanged so the QML re-reads
        // through resolvedProfile and the revert link goes live. Deleting the
        // emit loop for `removed` paths in clearFieldOnPaths otherwise leaves
        // the whole suite green — this is the only slot that pins it.
        QSignalSpy announced(&c, &AnimationsPageController::overrideChanged);
        QCOMPARE(c.clearFieldOnPaths(QStringList{kPrimary}, QStringLiteral("duration")), 1);
        int primaryAnnouncements = 0;
        for (int i = 0; i < announced.count(); ++i) {
            if (announced.at(i).at(0).toString() == kPrimary)
                ++primaryAnnouncements;
        }
        QCOMPARE(primaryAnnouncements, 1);

        QVERIFY2(!c.hasOverride(kPrimary), "the entry survived as an empty object");
    }

    /// A path that does not carry the field is skipped rather than rewritten,
    /// so a revert on a group where only some paths own the field does not
    /// create override files for the rest.
    void clearingAFieldSkipsPathsThatDoNotCarryIt()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        // The mirror carries a DIFFERENT field, which is the case that
        // distinguishes the skip from its absence: without it the mirror gets
        // rewritten verbatim, the count over-reports, and a spurious
        // overrideChanged fires for a path nothing asked to change. A mirror
        // with no file at all cannot tell the two apart, so it is covered by
        // the slot below rather than here.
        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 600}}));
        QVERIFY(c.setOverride(kMirror, QVariantMap{{QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")}}));

        QSignalSpy announced(&c, &AnimationsPageController::overrideChanged);
        QCOMPARE(c.clearFieldOnPaths(group(), QStringLiteral("duration")), 1);

        QCOMPARE(c.rawProfile(kMirror).value(QStringLiteral("curve")).toString(), QStringLiteral("0.4,0,0.2,1"));
        int mirrorAnnouncements = 0;
        for (const QList<QVariant>& emission : announced) {
            if (emission.at(0).toString() == kMirror)
                ++mirrorAnnouncements;
        }
        QCOMPARE(mirrorAnnouncements, 0);
    }

    /// The degenerate half of the same rule: a path with no override file at
    /// all must not have one created for it.
    void clearingAFieldCreatesNoEntryForAPathWithoutOne()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 600}}));

        QCOMPARE(c.clearFieldOnPaths(group(), QStringLiteral("duration")), 1);
        QVERIFY2(!c.hasOverride(kMirror), "a path that never owned the field got an override file created for it");
    }

    /// Allowlisted, not passed through to the JSON. This removes a key from a
    /// file on disk, and the only fields a card's revert links own are the
    /// timing pair — honouring anything else could strip a motion set's fields.
    void clearingAnUnrecognisedFieldIsRefused()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QVERIFY(c.setOverride(kPrimary,
                              QVariantMap{{QStringLiteral("duration"), 600}, {QStringLiteral("minDistance"), 42}}));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing to clear unrecognised field")));
        // -1, not 0: a refusal must be distinguishable from "the field was
        // already inherited everywhere", or a caller reports a revert that
        // never happened.
        QCOMPARE(c.clearFieldOnPaths(QStringList{kPrimary}, QStringLiteral("minDistance")), -1);
        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("minDistance")).toInt(), 42);
    }

    // ─── divergentPathCount ───────────────────────────────────────────────

    /// Zero when the group agrees, so the banner never renders a stale count.
    void aConvergedGroupReportsNoDivergence()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QCOMPARE(c.setOverrideMergedOnPaths(group(), QVariantMap{{QStringLiteral("duration"), 500}}, QVariant()), 2);

        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kMirror}, /*compareCurve=*/true), 0);
    }

    /// Diverging mirrors PLUS ONE for the primary, which each of them differs
    /// from and which the converging edit also rewrites. A bare mirror count
    /// would under-report the number of events the next edit touches.
    void aDivergentGroupCountsTheMirrorsPlusThePrimary()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 500}}));
        QVERIFY(c.setOverride(kMirror, QVariantMap{{QStringLiteral("duration"), 900}}));

        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kMirror}, /*compareCurve=*/true), 2);
    }

    /// An empty mirror list is a card fronting one event, which can never
    /// diverge from itself.
    void aGroupWithNoMirrorsNeverDiverges()
    {
        // Isolated like every other slot in this file even though an empty
        // mirror list short-circuits before any file is touched: the file's own
        // rule is that a controller is never built pointing at the developer's
        // real profiles, and an exception to it is one refactor away from
        // reading them.
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{}, /*compareCurve=*/true), 0);
    }

    /// The curve counts only when the caller can converge it. Simple mode has
    /// no curve control at all, so counting a curve difference there would latch
    /// the banner ON permanently over an axis nothing on the card can clear.
    void theCurveIsComparedOnlyWhenTheCallerCanConvergeIt()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        // Same duration, different curves — the ONLY axis in disagreement.
        QVERIFY(c.setOverride(
            kPrimary,
            QVariantMap{{QStringLiteral("duration"), 500}, {QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")}}));
        QVERIFY(c.setOverride(
            kMirror,
            QVariantMap{{QStringLiteral("duration"), 500}, {QStringLiteral("curve"), QStringLiteral("0.1,0,0.9,1")}}));

        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kMirror}, /*compareCurve=*/true), 2);
        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kMirror}, /*compareCurve=*/false), 0);
    }

    /// The motion-set fields are never compared, on either setting. The merged
    /// writer preserves each path's own rather than converging them, so
    /// counting them would latch the banner with no control able to clear it.
    void theMotionSetFieldsAreNeverCompared()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        QVERIFY(c.setOverride(kPrimary,
                              QVariantMap{{QStringLiteral("duration"), 500}, {QStringLiteral("minDistance"), 10}}));
        QVERIFY(c.setOverride(kMirror,
                              QVariantMap{{QStringLiteral("duration"), 500},
                                          {QStringLiteral("minDistance"), 99},
                                          {QStringLiteral("staggerInterval"), 30}}));

        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kMirror}, /*compareCurve=*/true), 0);
    }

    /// A missing override and an empty one must compare equal: both mean "this
    /// path stores nothing", and reporting them as divergent would show the
    /// banner on a group nobody has edited.
    void anAbsentOverrideComparesEqualToAnEmptyOne()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        // Neither path has a file at all.
        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kMirror}, /*compareCurve=*/true), 0);

        // One path now has a file carrying only a field that is never compared.
        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("minDistance"), 10}}));
        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kMirror}, /*compareCurve=*/true), 0);
    }

    // ─── Shader-leg group queries ─────────────────────────────────────────

    /// Gating a group mutation on the PRIMARY alone would skip a mirror that
    /// does take a shader leg, whose override would then survive the card's
    /// toggle-off and show as a divergence no control could clear.
    void anyPathSupportsShaderLegAnswersForTheWholeGroup()
    {
        // Pure taxonomy, so nothing here reads a file — isolated anyway, for
        // the reason the slot above gives.
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        // Both window.appearance legs take a shader leg.
        QVERIFY(c.anyPathSupportsShaderLeg(group()));
        // A group of paths the resolver never walks takes none.
        QVERIFY(!c.anyPathSupportsShaderLeg(QStringList{QStringLiteral("editor"), QStringLiteral("editor.snapIn")}));
        // One supporting path among non-supporting ones is enough — that is
        // exactly the case the primary-only test got wrong.
        QVERIFY(c.anyPathSupportsShaderLeg(QStringList{QStringLiteral("editor.snapIn"), kMirror}));
        QVERIFY(!c.anyPathSupportsShaderLeg(QStringList{}));
    }

    /// With no ISettings wired (the fixture passes none) the shader tree is
    /// unreachable, so no path can hold an effect — this pins the !m_settings
    /// guard specifically, not the stored-state walk. Pinned because the card
    /// asks this to decide whether its picker row is already selected, and
    /// answering "yes" from an unreadable tree would render a selection the
    /// tree does not contain.
    void allPathsHoldShaderEffectIsFalseWithoutSettings()
    {
        // No ISettings is the point of the slot, so this one deliberately does
        // NOT take the fixture: it constructs the controller bare.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());
        QVERIFY(!c.allPathsHoldShaderEffect(group(), QStringLiteral("dissolve")));
        // The engaged-empty "None" sentinel is a stored value too, so an absent
        // override is not equal to it either.
        QVERIFY(!c.allPathsHoldShaderEffect(group(), QString()));
    }
    // ─── Refusal ──────────────────────────────────────────────────────────

    void aMixedBatchDoesNotLeakTheRemovedPathsStaleValue()
    {
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;

        // A populated registry, so a stale entry has somewhere to be stale IN.
        // The controller reads the config tree ahead of the registry, so the
        // registry's value must never surface for a path the tree still covers
        // — and must surface for one it does not.
        PhosphorAnimation::PhosphorProfileRegistry registry;
        PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(&registry);
        const auto unpublish = qScopeGuard([]() {
            PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
        });
        // Registered at the PARENT, which is where a real seed lives. A stale
        // entry at kPrimary itself would be a legitimate fallback once the
        // override is cleared, so it could not distinguish the bug.
        PhosphorAnimation::Profile seed;
        seed.duration = 999;
        registry.registerProfile(QStringLiteral("window"), seed);

        // The parent holds the value the removed leaf must fall back to.
        QVERIFY(c.setOverride(QStringLiteral("window.appearance"), {{QStringLiteral("duration"), 123}}));
        // kPrimary: duration only, so clearing duration EMPTIES it → removal.
        QVERIFY(c.setOverride(kPrimary, {{QStringLiteral("duration"), 600}}));
        // kMirror: duration AND curve, so clearing duration leaves a curve → rewrite.
        QVERIFY(c.setOverride(
            kMirror,
            QVariantMap{{QStringLiteral("duration"), 600}, {QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")}}));

        // Read the removed path from INSIDE an overrideChanged handler, which is
        // where the QML cards read it from.
        int seenFromHandler = -1;
        const auto conn = connect(&c, &AnimationsPageController::overrideChanged, &c, [&](const QString&) {
            if (seenFromHandler == -1)
                seenFromHandler = c.resolvedProfile(kPrimary).value(QStringLiteral("duration")).toInt();
        });
        const auto disconnector = qScopeGuard([&conn]() {
            QObject::disconnect(conn);
        });

        QCOMPARE(c.clearFieldOnPaths(group(), QStringLiteral("duration")), 2);

        QVERIFY2(seenFromHandler != 600, "a handler on the emitting stack saw the removed leaf's STALE duration");
        QCOMPARE(seenFromHandler, 123);
        // End state: kPrimary inherits, kMirror kept its curve and lost its duration.
        QVERIFY(!c.hasOverride(kPrimary));
        QCOMPARE(c.rawProfile(kMirror).value(QStringLiteral("curve")).toString(), QStringLiteral("0.4,0,0.2,1"));
        QVERIFY(!c.rawProfile(kMirror).contains(QStringLiteral("duration")));
    }

    /// The other half of the mixed batch: it must still announce the dirty flip.
    ///
    /// A removal defers its own signal, and the rewrite's `setOverride` samples
    /// pending state AFTER the removal already made the page dirty — so it sees
    /// no flip and stays silent too. Gating the batch's emit on "no rewrites
    /// ran" therefore loses the clean→dirty transition entirely, and the
    /// Unsaved-changes footer never appears.
    void aMixedBatchStillAnnouncesTheDirtyFlip()
    {
        TestHelpers::TimingControllerFixture fx;
        Settings& settings = fx.settings;
        AnimationsPageController& c = fx.c;

        // Written, then COMMITTED, so the page starts clean: dirtiness is
        // live-versus-baseline, and an uncommitted write would leave the page
        // already dirty before the batch runs.
        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 600}}));
        QVERIFY(c.setOverride(
            kMirror,
            QVariantMap{{QStringLiteral("duration"), 600}, {QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")}}));
        settings.save();
        c.refreshDirtyState();
        QVERIFY2(!c.hasPendingChanges(), "the fixture did not start clean");

        QSignalSpy dirtied(&c, &AnimationsPageController::pendingChangesChanged);
        QCOMPARE(c.clearFieldOnPaths(group(), QStringLiteral("duration")), 2);

        QVERIFY(c.hasPendingChanges());
        // Exactly one: a group write coalesces its per-path mutations into a
        // single net dirty flip. `> 0` would also pass a per-path storm of
        // announcements, which the batching exists precisely to prevent.
        QCOMPARE(dirtied.count(), 1);
    }

    // ─── Refusal parity across the group writers ──────────────────────────

    /// A malformed MotionProfileTree blob can be repaired by writing over it.
    ///
    /// The key is hand-editable. A blob that is not a map at all reads back as
    /// an empty map, so the setter's equality short-circuit saw "already empty,
    /// nothing to do" and returned before writing. The junk then sat in the
    /// config with no route to repair it from inside the app, since every
    /// writer goes through this setter.
    void aMalformedTreeBlobIsRepairedRatherThanLeftInPlace()
    {
        IsolatedConfigGuard guard;

        // Planted the only way it can happen: straight into config.json, the
        // way a hand edit does. Every writer inside the app goes through the
        // setter, so the app itself cannot produce this state.
        const QString configPath = ConfigDefaults::configFilePath();
        QVERIFY(QDir().mkpath(QFileInfo(configPath).absolutePath()));
        QJsonObject root;
        {
            QFile in(configPath);
            if (in.open(QIODevice::ReadOnly))
                root = QJsonDocument::fromJson(in.readAll()).object();
        }
        QJsonObject animations = root.value(ConfigKeys::animationsGroup()).toObject();
        animations.insert(ConfigKeys::motionProfileTreeKey(), QStringLiteral("not a tree"));
        root.insert(ConfigKeys::animationsGroup(), animations);
        {
            QFile out(configPath);
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            const QByteArray bytes = QJsonDocument(root).toJson();
            QCOMPARE(out.write(bytes), static_cast<qint64>(bytes.size()));
        }

        Settings settings;
        // Reads back empty, indistinguishable from "no overrides stored".
        QVERIFY(settings.motionProfileTree().isEmpty());

        // The repair case is the one that canonicalises to EMPTY, which is
        // what a reset asks for: with the short-circuit comparing against the
        // canonical read, "already empty" was true and the setter returned
        // before touching the junk.
        settings.setMotionProfileTree({});
        settings.save();

        QJsonObject after;
        {
            QFile in(configPath);
            QVERIFY(in.open(QIODevice::ReadOnly));
            after = QJsonDocument::fromJson(in.readAll()).object();
        }
        const QJsonValue stored =
            after.value(ConfigKeys::animationsGroup()).toObject().value(ConfigKeys::motionProfileTreeKey());
        QVERIFY2(!stored.isString(), "the malformed blob survived a reset, with no route to repair it from the app");
    }

    /// A timing entry at a path outside the built-in taxonomy is still visible
    /// to the scoped dirty check and still removable by a scoped clear.
    ///
    /// The motion tree is one hand-editable config key, so an entry can sit at
    /// a path no page renders. Every scoped walk gated on `isValidEventPath`,
    /// which is membership in the compile-time built-in list, so such an entry
    /// was invisible to dirty, Discard and Reset alike and could not be removed
    /// from inside the app at all. The traversal characters stay refused, and
    /// the strict gate still guards the writes that CREATE an entry, which this
    /// slot pins too.
    void aStoredPathOutsideTheTaxonomyIsStillReachableByScopedWalks()
    {
        TestHelpers::TimingControllerFixture fx;
        Settings& settings = fx.settings;
        AnimationsPageController& c = fx.c;

        const QString stray = QStringLiteral("window.appearance.notabuiltin");
        QVERIFY2(!c.isValidEventPath(stray), "the fixture path is in the taxonomy after all");

        // Creating one still needs the strict gate, so plant it the way a hand
        // edit does: straight into the config key.
        QVariantMap tree;
        tree.insert(
            QStringLiteral("overrides"),
            QVariantList{QVariantMap{{QStringLiteral("path"), stray},
                                     {QStringLiteral("profile"), QVariantMap{{QStringLiteral("duration"), 700}}}}});
        settings.setMotionProfileTree(tree);
        QVERIFY(c.isRemovableEventPath(stray));

        // Writes that would INVENT the path are still refused.
        QVERIFY(!c.setOverride(QStringLiteral("../etc/passwd"), QVariantMap{{QStringLiteral("duration"), 1}}));

        const QStringList scoped{kPrimary, stray};
        QVERIFY2(c.hasScopedPendingOverrides(scoped),
                 "a stray entry the user cannot see is also not reported as unsaved");
        QCOMPARE(c.clearOverridesUnder(scoped), 1);
        QVERIFY(!c.isRemovableEventPath(stray));
    }

    /// The merged-write failure toast fires ONCE per run of failures and clears
    /// on the first write that fully lands.
    ///
    /// The failure arm had no coverage at all. Since schema v8 its only
    /// remaining cause is an invalid path in the caller's list, so this drives
    /// it that way. Both halves matter and fail for different reasons: without
    /// the latch this is reached from the duration slider's per-move commit and
    /// re-toasts at pointer rate, restarting the pill's fade before it can be
    /// read and queueing the same sentence into the screen reader over and
    /// over; without the RESET the user is told nothing the next time a real
    /// failure begins, because the latch from the previous one is still set.
    void aMergedWriteFailureToastsOnceAndRearmsAfterASuccess()
    {
        TestHelpers::TimingControllerFixture fx;
        AnimationsPageController& c = fx.c;

        QSignalSpy toasts(&c, &AnimationsPageController::toastRequested);
        const QStringList mixed{kPrimary, QStringLiteral("not.an.event.path")};
        const QVariantMap fields{{QStringLiteral("duration"), 400}};

        // The valid path still lands, so this is a partial failure rather than
        // a refusal: the return is the count that was written, never -1.
        QCOMPARE(c.setOverrideMergedOnPaths(mixed, fields, QVariant()), 1);
        QCOMPARE(toasts.count(), 1);

        // A second failing tick, exactly as the slider produces. Latched.
        QCOMPARE(c.setOverrideMergedOnPaths(mixed, QVariantMap{{QStringLiteral("duration"), 420}}, QVariant()), 1);
        QVERIFY2(toasts.count() == 1, "the merged-write failure toast re-fired on a repeat failure");

        // A write where every path lands clears the latch.
        QVERIFY(c.setOverrideMergedOnPaths(QStringList{kPrimary}, QVariantMap{{QStringLiteral("duration"), 440}},
                                           QVariant())
                >= 0);
        QCOMPARE(toasts.count(), 1);

        // …so the next failure is announced again.
        QCOMPARE(c.setOverrideMergedOnPaths(mixed, QVariantMap{{QStringLiteral("duration"), 460}}, QVariant()), 1);
        QCOMPARE(toasts.count(), 2);
    }

    void divergenceCountsEachPathOnce()
    {
        TestHelpers::TimingControllerFixture fx;
        AnimationsPageController& c = fx.c;

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 500}}));
        QVERIFY(c.setOverride(kMirror, QVariantMap{{QStringLiteral("duration"), 900}}));

        const int baseline = c.divergentPathCount(kPrimary, QStringList{kMirror}, /*compareCurve=*/true);
        QCOMPARE(baseline, 2);
        // A REPEATED mirror is the case the dedup actually closes: without it the
        // same file is compared twice and the count reads 3.
        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kMirror, kMirror}, /*compareCurve=*/true), baseline);
        // The primary naming ITSELF is asserted too, but note it is NOT the dedup
        // that absorbs it: the primary's key always equals its own, so it can never
        // increment `diverged` whether or not it is removed from the list first.
        // This row pins the observable answer, not the `removeAll` line — deleting
        // that line leaves both of these passing, and the comment used to claim
        // otherwise.
        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kPrimary, kMirror}, /*compareCurve=*/true), baseline);
    }

    /// A group whose only "mirror" is the primary itself can never diverge.
    ///
    /// Pinned as a CONTRACT, not as a test of the mirror-dedup: with mirrors ==
    /// {primary} the comparison is the primary against itself, which matches by
    /// construction, so this holds for the same reason the row above does. It is
    /// here so a future change that starts counting self-comparisons is caught.
    void aGroupWhoseOnlyMirrorIsThePrimaryNeverDiverges()
    {
        TestHelpers::TimingControllerFixture fx;
        AnimationsPageController& c = fx.c;

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 500}}));
        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kPrimary}, /*compareCurve=*/true), 0);
    }

    /// An unrecognised MIRROR path must not count as divergent.
    ///
    /// The primary is gated by `isValidEventPath`, the mirror list was not: a
    /// non-built-in mirror yields the all-empty comparison key (no stored profile,
    /// no shader leg), which differs from any primary holding real state and so
    /// counted as divergence over a path no edit could ever converge — exactly the
    /// latch the primary gate exists to prevent. Not reachable from the shipped
    /// card, which pre-filters its mirrors, but this is a Q_INVOKABLE.
    void anUnrecognisedMirrorIsNotCountedAsDivergent()
    {
        TestHelpers::TimingControllerFixture fx;
        AnimationsPageController& c = fx.c;

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 500}}));
        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{QStringLiteral("not.a.real.event")},
                                      /*compareCurve=*/true),
                 0);
    }

    /// An invalid PRIMARY path returns 0 rather than counting every mirror.
    void anInvalidPrimaryReportsNoDivergence()
    {
        TestHelpers::TimingControllerFixture fx;
        AnimationsPageController& c = fx.c;

        QVERIFY(c.setOverride(kMirror, QVariantMap{{QStringLiteral("duration"), 900}}));
        QCOMPARE(c.divergentPathCount(QStringLiteral("not.a.real.event"), QStringList{kMirror},
                                      /*compareCurve=*/true),
                 0);
    }

    /// `allPathsHoldShaderEffect` is FALSE for an empty list, not vacuously true.
    void allPathsHoldShaderEffectIsFalseForAnEmptyList()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);
        QVERIFY(!c.allPathsHoldShaderEffect(QStringList{}, QStringLiteral("pixelate")));
    }
};

QTEST_MAIN(TestAnimationsGroupWrites)
#include "test_animations_group_writes.moc"
