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
 * Every slot redirects override-file I/O into a tmpdir via
 * `setUserProfilesDirOverride()`, so the real user XDG dirs are never touched.
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QRegularExpression>
#include <QVariant>

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"
#include "phosphor_i18n.h"
#include "settings/pages/animationspagecontroller.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using P = PhosphorAnimation::Profile;

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
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Stand in for what a motion set left on the leaf.
        QVERIFY(c.setOverride(kPrimary,
                              QVariantMap{{QStringLiteral("duration"), 200},
                                          {QStringLiteral("minDistance"), 42},
                                          {QStringLiteral("staggerInterval"), 30},
                                          {QStringLiteral("presetName"), QStringLiteral("Snappy")}}));

        // The card edits ONE field.
        QVERIFY(c.setOverrideMergedOnPaths(QStringList{kPrimary}, QVariantMap{{QStringLiteral("duration"), 900}},
                                           QVariant()));

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
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverrideMergedOnPaths(group(), QVariantMap{{QStringLiteral("duration"), 750}}, QVariant()));

        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("duration")).toInt(), 750);
        QCOMPARE(c.rawProfile(kMirror).value(QStringLiteral("duration")).toInt(), 750);
    }

    /// An INVALID QVariant is QML's `undefined` arriving here, and it means the
    /// user did not touch the curve. Each path must keep its OWN: the path that
    /// owns one keeps it, and the path that inherits stays inheriting. Handing
    /// both the resolved curve instead is the defect this distinction exists to
    /// prevent — the inheriting path would silently stop tracking its parent.
    void anAbsentCurveLeavesEachPathsOwnCurveAlone()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Primary owns a curve; the mirror owns none.
        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")}}));

        QVERIFY(c.setOverrideMergedOnPaths(group(), QVariantMap{{QStringLiteral("duration"), 300}}, QVariant()));

        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("curve")).toString(), QStringLiteral("0.4,0,0.2,1"));
        QVERIFY2(!c.rawProfile(kMirror).contains(QStringLiteral("curve")),
                 "a duration-only edit pinned a curve on a path that was inheriting one");
        QCOMPARE(c.rawProfile(kMirror).value(QStringLiteral("duration")).toInt(), 300);
    }

    /// The other half: a curve the user actually edited travels to every path,
    /// overwriting whatever each held.
    void anEditedCurveTravelsToEveryPath()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")}}));

        QVERIFY(c.setOverrideMergedOnPaths(group(), QVariantMap{}, QVariant(QStringLiteral("spring:14.00,0.60"))));

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
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 200}}));

        QVERIFY(c.setOverrideMergedOnPaths(
            QStringList{kPrimary},
            QVariantMap{{QStringLiteral("duration"), 400}, {QStringLiteral("curve"), QStringLiteral("0.9,0,0.1,1")}},
            QVariant()));

        const QVariantMap after = c.rawProfile(kPrimary);
        QCOMPARE(after.value(QStringLiteral("duration")).toInt(), 400);
        QVERIFY2(!after.contains(QStringLiteral("curve")),
                 "a curve passed through `fields` was written, so the card decided a curve on the user's behalf");
    }

    /// The same rule the other way round: a path that DOES own a curve keeps
    /// its own, and `fields` cannot overwrite it either.
    void aCurveSuppliedThroughFieldsCannotOverwriteAPathsOwn()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("curve"), QStringLiteral("0.4,0,0.2,1")}}));

        QVERIFY(c.setOverrideMergedOnPaths(
            QStringList{kPrimary}, QVariantMap{{QStringLiteral("curve"), QStringLiteral("0.9,0,0.1,1")}}, QVariant()));

        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("curve")).toString(), QStringLiteral("0.4,0,0.2,1"));
    }

    // ─── clearFieldOnPaths ────────────────────────────────────────────────

    /// One field goes, the other stays. This is the per-field revert link.
    void clearingOneFieldLeavesTheOtherAndTheMotionSetFieldsPut()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

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
    void clearingTheLastFieldRemovesTheFileOutright()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 600}}));
        QVERIFY(c.hasOverride(kPrimary));

        QCOMPARE(c.clearFieldOnPaths(QStringList{kPrimary}, QStringLiteral("duration")), 1);

        QVERIFY2(!c.hasOverride(kPrimary), "the override file survived as an empty object");
    }

    /// A path that does not carry the field is skipped rather than rewritten,
    /// so a revert on a group where only some paths own the field does not
    /// create override files for the rest.
    void clearingAFieldSkipsPathsThatDoNotCarryIt()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

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
    void clearingAFieldCreatesNoFileForAPathWithoutOne()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 600}}));

        QCOMPARE(c.clearFieldOnPaths(group(), QStringLiteral("duration")), 1);
        QVERIFY2(!c.hasOverride(kMirror), "a path that never owned the field got an override file created for it");
    }

    /// Allowlisted, not passed through to the JSON. This removes a key from a
    /// file on disk, and the only fields a card's revert links own are the
    /// timing pair — honouring anything else could strip a motion set's fields.
    void clearingAnUnrecognisedFieldIsRefused()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

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
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverrideMergedOnPaths(group(), QVariantMap{{QStringLiteral("duration"), 500}}, QVariant()));

        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kMirror}, /*compareCurve=*/true), 0);
    }

    /// Diverging mirrors PLUS ONE for the primary, which each of them differs
    /// from and which the converging edit also rewrites. A bare mirror count
    /// would under-report the number of events the next edit touches.
    void aDivergentGroupCountsTheMirrorsPlusThePrimary()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 500}}));
        QVERIFY(c.setOverride(kMirror, QVariantMap{{QStringLiteral("duration"), 900}}));

        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{kMirror}, /*compareCurve=*/true), 2);
    }

    /// An empty mirror list is a card fronting one event, which can never
    /// diverge from itself.
    void aGroupWithNoMirrorsNeverDiverges()
    {
        AnimationsPageController c;
        QCOMPARE(c.divergentPathCount(kPrimary, QStringList{}, /*compareCurve=*/true), 0);
    }

    /// The curve counts only when the caller can converge it. Simple mode has
    /// no curve control at all, so counting a curve difference there would latch
    /// the banner ON permanently over an axis nothing on the card can clear.
    void theCurveIsComparedOnlyWhenTheCallerCanConvergeIt()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

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
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

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
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

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
        AnimationsPageController c;

        // Both window.appearance legs take a shader leg.
        QVERIFY(c.anyPathSupportsShaderLeg(group()));
        // A group of paths the resolver never walks takes none.
        QVERIFY(!c.anyPathSupportsShaderLeg(QStringList{QStringLiteral("editor"), QStringLiteral("editor.snapIn")}));
        // One supporting path among non-supporting ones is enough — that is
        // exactly the case the primary-only test got wrong.
        QVERIFY(c.anyPathSupportsShaderLeg(QStringList{QStringLiteral("editor.snapIn"), kMirror}));
        QVERIFY(!c.anyPathSupportsShaderLeg(QStringList{}));
    }

    /// With no ISettings the shader tree is unreachable, so no path can hold an
    /// effect. Pinned because the card asks this to decide whether its picker
    /// row is already selected, and answering "yes" from an unreadable tree
    /// would render a selection the tree does not contain.
    void allPathsHoldShaderEffectIsFalseWhenNoPathStoresOne()
    {
        AnimationsPageController c;
        QVERIFY(!c.allPathsHoldShaderEffect(group(), QStringLiteral("dissolve")));
        // The engaged-empty "None" sentinel is a stored value too, so an absent
        // override is not equal to it either.
        QVERIFY(!c.allPathsHoldShaderEffect(group(), QString()));
    }
    // ─── Refusal ──────────────────────────────────────────────────────────

    /// While an async discard owns the snapshot map, `clearFieldOnPaths` must
    /// REFUSE the whole call rather than let every write fail individually and
    /// report the resulting 0 as "the field was already inherited everywhere".
    /// The sibling `clearShaderOverrideDescendants` has had this shape pinned
    /// since it was written; this is its timing-side twin.
    void clearingAFieldIsRefusedWhileAnAsyncDiscardIsInFlight()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c(nullptr, &settings);
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(kPrimary, QVariantMap{{QStringLiteral("duration"), 600}}));

        QSignalSpy toasts(&c, &AnimationsPageController::toastRequested);
        QSignalSpy done(&c, &AnimationsPageController::discardResult);
        c.asyncRevertPending();

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("refusing while an async discard is in flight")));
        // What this slot pins is the DIAGNOSTIC, not safety. With a discard in
        // flight the per-path `setOverride` / `removeOverrideFile` calls refuse
        // individually anyway, so nothing is mutated whether or not the early
        // guard is there — moving the guard below the loop keeps every
        // assertion here green. The guard's value is that the caller gets -1
        // and a toast instead of a 0 it would read as "the field was already
        // inherited everywhere", and that is what the -1 mutation test proves.
        //
        // The spies are still worth attaching: they would catch a future change
        // that made the per-path calls stop refusing, which is the only way a
        // refused batch could start mutating.
        //
        // Asserting the FILE instead would race: the discard this slot started
        // is itself restoring the profiles directory on a worker thread, so the
        // override's presence on disk says nothing about the refusal.
        QSignalSpy touched(&c, &AnimationsPageController::overrideChanged);
        QSignalSpy dirtied(&c, &AnimationsPageController::pendingChangesChanged);

        QCOMPARE(c.clearFieldOnPaths(group(), QStringLiteral("duration")), -1);
        QCOMPARE(toasts.count(), 1);
        QCOMPARE(touched.count(), 0);
        QCOMPARE(dirtied.count(), 0);
        QCOMPARE(toasts.first().at(0).toString(),
                 PhosphorI18n::tr("Cannot change this while a discard is in progress."));

        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    }
};

QTEST_MAIN(TestAnimationsGroupWrites)
#include "test_animations_group_writes.moc"
