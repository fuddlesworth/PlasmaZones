// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_motion_sets.cpp
 * @brief AnimationsPageController motion-set and pending-changes tests.
 *
 * The controller's path-discovery and override CRUD are covered by
 * test_animations_page_controller.cpp, and the user-preset library by
 * test_animations_presets.cpp.
 *
 * Motion sets go through the shared ShaderSetStore reached from
 * `AnimationsPageController::setsBridge()`. The domain half (which paths are
 * valid, how live state is snapshotted) lives in motionsetdomain.cpp; the
 * envelope, the file lifecycle and the `active` summary are the store's.
 * Pinned behaviour:
 *   - Motion set save/apply/remove with merge-not-replace semantics
 *   - `active` is a CONTAINMENT check: a set stays active while unrelated
 *     overrides exist, because apply would have left them alone
 *   - updateSet / exportSet / importSet round-trip, and an import is validated
 *     against the EVENT taxonomy (a decoration set is refused here)
 *   - saveCurrentAsSet refuses an unconfirmed overwrite but honours a
 *     confirmed one
 *   - Set file CRUD is IMMEDIATE: saving one is not a staged edit, so Discard
 *     does not undo it, matching the decoration page
 *   - Pending changes signal emission for revert/commit
 *   - Atomic motion-set application (rejects whole malformed set)
 *   - Motion has no baseline, so a baseline-carrying set is refused at import
 *   - Applying a set over paths already at their defaults does not freeze those
 *     defaults as explicit overrides
 *
 * The staging contracts this file used to pin went with schema v8: set files
 * are no longer snapshotted, there is no mutation guard, and no async discard
 * worker for revertPending to report a refusal from.
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include "phosphor_i18n.h"
#include <PhosphorAnimation/ProfilePaths.h>
#include "settings/pages/animationspagecontroller.h"
#include "helpers/AnimationsControllerFixture.h"
#include "settings/stores/shadersetstore.h"
#include "helpers/SetRowHelpers.h"

using namespace PlasmaZones;

class TestAnimationsMotionSets : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Deliberately no `QStandardPaths::setTestModeEnabled(true)` here.
    ///
    /// Test mode pins the config and data locations to ONE fixed path for the
    /// whole process, which overrides the per-slot `IsolatedConfigGuard` the
    /// fixture carries. That was harmless while per-event overrides were files
    /// in each slot's own tmpdir. Since schema v8 they are config keys, so a
    /// shared config location leaks one slot's overrides into the next — which
    /// is exactly what it did, silently, before this note replaced it. The
    /// guard is the isolation.
    void initTestCase()
    {
    }

    // ─── Motion sets ──────────────────────────────────────────────────────

    void saveCurrentAsSet_capturesPathOverridesOnly()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        // Mix of path overrides and a user preset
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 222}}));
        QVERIFY(
            c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("curve"), QStringLiteral("spring:10,0.7")}}));
        QVERIFY(
            c.addUserPreset(QStringLiteral("My Preset"), {{QStringLiteral("curve"), QStringLiteral("0.5,0,0.5,1")}}));

        QSignalSpy spy(c.setsBridge(), &ShaderSetStore::setsChanged);
        QVERIFY(c.setsBridge()->saveCurrentAsSet(QStringLiteral("My Set"), QStringLiteral("test set")));
        QCOMPARE(spy.count(), 1);

        const QVariantList sets = c.setsBridge()->availableSets();
        QCOMPARE(sets.size(), 1);
        const QVariantMap set = sets.first().toMap();
        QCOMPARE(set.value(QStringLiteral("name")).toString(), QStringLiteral("My Set"));
        // A set is SELF-CONTAINED, so its coverage is the two user overrides
        // PLUS every path carrying a built-in pack default — the same way a
        // decoration set captures its seeded surfaces. What must never appear
        // is the user PRESET, which is a named library entry and not an event.
        const QStringList coverage = set.value(QStringLiteral("coverage")).toStringList();
        QVERIFY(coverage.contains(QStringLiteral("editor")));
        QVERIFY(coverage.contains(QStringLiteral("osd")));
        QVERIFY2(!coverage.contains(QStringLiteral("My Preset")), "a user preset was captured as an event override");
        QVERIFY(set.value(QStringLiteral("coverageCount")).toInt() >= 2);
    }

    void applySet_writesPerPathFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        // Build set, then clear overrides, then apply
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 333}}));
        QVERIFY(c.setsBridge()->saveCurrentAsSet(QStringLiteral("snappy-set"), QString()));
        QVERIFY(c.clearOverride(QStringLiteral("editor.snapIn")));
        QVERIFY(!c.hasOverride(QStringLiteral("editor.snapIn")));

        QVERIFY(c.setsBridge()->applySet(QStringLiteral("snappy-set")));
        QVERIFY(c.hasOverride(QStringLiteral("editor.snapIn")));
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 333);
    }

    /// The capture/de-seed round trip: a set captured while every path is on
    /// its default must apply WITHOUT freezing those defaults as overrides.
    ///
    /// The two halves are a matched pair and neither is safe alone. The
    /// snapshot bakes in what each path resolves to, so a set is self-contained
    /// and a recipient reproduces the sender's look; the de-seed on the write
    /// side strips anything that already resolves the same way locally, so
    /// applying a set does not opt every event out of future default
    /// improvements. Nothing exercised the pairing, so either half could have
    /// been changed alone — and the sweep capturing a per-path built-in default
    /// rather than the resolved pack was exactly that kind of drift.
    void applySet_onDefaultsDoesNotFreezeThemAsOverrides()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        // One real timing edit so the set has something of its own to carry;
        // every shader assignment is left untouched, i.e. on its default.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 333}}));
        QVERIFY(c.setsBridge()->saveCurrentAsSet(QStringLiteral("defaults-set"), QString()));

        QVERIFY(c.setsBridge()->applySet(QStringLiteral("defaults-set")));

        // The timing half applied.
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 333);

        // The shader half did NOT become a stored override anywhere: every one
        // of those paths still resolves the same way it did before the apply,
        // so the de-seed stripped what the sweep had carried for
        // self-containment.
        const QVariantMap raw = c.allRawShaderProfiles();
        for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
            QVERIFY2(
                !it.value().toMap().contains(QStringLiteral("effectId")),
                qPrintable(QStringLiteral("applying a set captured on defaults pinned a pack at %1").arg(it.key())));
        }
    }

    void applySet_mergesPreservesOtherPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        // Save a set with one path
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 222}}));
        QVERIFY(c.setsBridge()->saveCurrentAsSet(QStringLiteral("set-a"), QString()));
        QVERIFY(c.clearOverride(QStringLiteral("editor.snapIn")));

        // Set an UNRELATED override
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 555}}));

        // Apply set-a; osd.show should still be 555 (merge, not replace)
        QVERIFY(c.setsBridge()->applySet(QStringLiteral("set-a")));
        QCOMPARE(c.rawProfile(QStringLiteral("osd.show")).value(QStringLiteral("duration")).toInt(), 555);
        QVERIFY(c.hasOverride(QStringLiteral("editor.snapIn")));
    }

    void removeSet_emitsAndDeletes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 222}}));
        QVERIFY(c.setsBridge()->saveCurrentAsSet(QStringLiteral("To Remove"), QString()));
        QCOMPARE(c.setsBridge()->availableSets().size(), 1);

        QSignalSpy spy(c.setsBridge(), &ShaderSetStore::setsChanged);
        QVERIFY(c.setsBridge()->removeSet(QStringLiteral("To Remove")));
        QCOMPARE(spy.count(), 1);
        QVERIFY(c.setsBridge()->availableSets().isEmpty());
    }

    /// Manually plant a malformed motion-set file (mixing valid and
    /// invalid entries) and verify applySet() rejects the whole
    /// thing rather than partially writing. Pre-fix, the loop wrote
    /// each valid entry and skipped invalid ones, leaving inconsistent
    /// state.
    void applySet_malformedEntryRejectsWholeSet()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        // Make the motion-sets directory and write a hand-crafted file
        // with one VALID entry and one INVALID entry (unknown path).
        const QString setsDir = tmp.path() + QStringLiteral("/motionsets");
        QDir().mkpath(setsDir);
        const QString setPath = setsDir + QStringLiteral("/bad-set.json");

        QJsonObject validEntry;
        validEntry.insert(QStringLiteral("path"), QStringLiteral("editor.snapIn"));
        QJsonObject validProfile;
        validProfile.insert(QStringLiteral("duration"), 222);
        validEntry.insert(QStringLiteral("profile"), validProfile);

        QJsonObject invalidEntry;
        invalidEntry.insert(QStringLiteral("path"), QStringLiteral("../etc/passwd"));
        invalidEntry.insert(QStringLiteral("profile"), QJsonObject{});

        QJsonArray overrides;
        overrides.append(validEntry);
        overrides.append(invalidEntry);

        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("bad-set"));
        root.insert(QStringLiteral("overrides"), overrides);
        root.insert(QStringLiteral("version"), 1);

        QFile f(setPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray rootBytes = QJsonDocument(root).toJson();
        QCOMPARE(f.write(rootBytes), static_cast<qint64>(rootBytes.size()));
        f.close();

        // No prior override at editor.snapIn.
        QVERIFY(!c.hasOverride(QStringLiteral("editor.snapIn")));

        QVERIFY(!c.setsBridge()->applySet(QStringLiteral("bad-set")));

        // Critical: the valid entry MUST NOT have been written. Atomic
        // semantics — all-or-nothing. Pre-fix this would be true.
        QVERIFY2(!c.hasOverride(QStringLiteral("editor.snapIn")),
                 "applySet wrote partial state from a malformed set — should have rejected atomically");
    }

    // ─── Pending changes / commit / revert ────────────────────────────────

    void hasPendingChanges_falseInitially()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        QVERIFY(!c.hasPendingChanges());
    }

    void setOverride_emitsPendingChangesChanged()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QCOMPARE(spy.count(), 1);
        QVERIFY(c.hasPendingChanges());
    }

    /// Discard puts a per-event override back to its committed value.
    ///
    /// The page does not do the reverting: every value it writes is a config
    /// key, so `Settings::load()` is the revert and `revertPending()` only
    /// re-announces it so an open page rebinds. That pairing is the caller
    /// contract `SettingsController::load()` implements, and this pins it.
    void discardRestoresTheCommittedOverride()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        // Establish a committed baseline: 100 ms.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 100}}));
        fx.settings.save();
        c.refreshDirtyState();
        QVERIFY(!c.hasPendingChanges());

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 250);
        QVERIFY(c.hasPendingChanges());

        // revertPending() is asserted on directly, not just called on the way
        // to load(). The value restore below is done by `Settings::load()`, so
        // on its own it holds even if revertPending's body were emptied — this
        // slot named that function and tested nothing about it. What only
        // revertPending produces is the empty-path broadcast that tells every
        // card to re-read, so that is what is pinned.
        QSignalSpy reloadSpy(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(c.revertPending());
        QCOMPARE(reloadSpy.count(), 1);
        QVERIFY2(reloadSpy.at(0).at(0).toString().isEmpty(),
                 "revertPending must broadcast an EMPTY path — a per-path emission reaches only the cards it "
                 "names, and a Discard moves paths no card is currently showing");

        fx.settings.load();
        c.refreshDirtyState();
        QVERIFY(!c.hasPendingChanges());
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 100);
    }

    /// The mirror: an override created this session is gone after Discard,
    /// rather than surviving as an entry the committed config never had.
    void discardRemovesAnOverrideCreatedThisSession()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 200}}));
        QVERIFY(c.hasOverride(QStringLiteral("osd.show")));

        c.revertPending();
        fx.settings.load();
        QVERIFY(!c.hasOverride(QStringLiteral("osd.show")));
    }

    /// Discard re-announces with the tree-wide reload broadcast (an EMPTY
    /// path), which the cards already understand. Per-path signals are not
    /// available here: `Settings::load()` replaces every key at once, so the
    /// page cannot say which of them moved.
    void revertPendingBroadcastsAReload()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 200}}));
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 300}}));

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(c.revertPending());

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QString());
    }

    /// Apply leaves the page clean without moving any row.
    ///
    /// `commitPending` announces the dirty flip; the values themselves were
    /// already written, and the baseline catches up in `Settings::save()`. No
    /// per-path `overrideChanged` may fire — the user just saved, nothing
    /// visually moved.
    void commitPendingAnnouncesTheFlipWithoutTouchingRows()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(c.hasPendingChanges());

        QSignalSpy pendingSpy(&c, &AnimationsPageController::pendingChangesChanged);
        QSignalSpy overrideSpy(&c, &AnimationsPageController::overrideChanged);
        fx.settings.save();
        c.commitPending();
        c.refreshDirtyState();
        QVERIFY(!c.hasPendingChanges());
        QVERIFY(pendingSpy.count() >= 1);
        QCOMPARE(overrideSpy.count(), 0);
    }

    // ─── Motion sets: active flag, metadata edit, portability, guard ───────
    /// A pack-only entry stays satisfied when its path ALSO carries timing the
    /// set never mentions.
    ///
    /// An entry holds a timing half, a pack half, or both, and applying writes
    /// only the halves present — a pack-only entry deliberately leaves the
    /// path's timing alone. The `active` check compared the whole profile, so
    /// such an entry could never match a path that legitimately carried timing,
    /// and one field the set does not own kept the WHOLE set reading as
    /// inactive. Reported from a real config: a theme's set assigned a pack to
    /// `scrolling.view` while the user's own timing override sat on the same
    /// path, and the set stayed dark immediately after applying it.
    void motionSets_packOnlyEntryIgnoresTimingItDoesNotCarry()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::PopulatedControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();
        QVERIFY(sets);

        const QString path = QStringLiteral("window.appearance.open");
        const QStringList available = TestHelpers::pickerIdsFor(c, path);
        QVERIFY(!available.isEmpty());

        // A set whose entry at this path carries ONLY a pack.
        QVERIFY(c.setShaderOverride(path, available.at(0), QVariantMap{}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("PackOnly"), QString()));
        QVERIFY(rowFor(sets, QStringLiteral("PackOnly")).value(QStringLiteral("active")).toBool());

        // Now give the SAME path a timing override the set knows nothing about.
        // Applying the set would not touch it, so the set is still fully applied.
        QVERIFY(c.setOverride(path, {{QStringLiteral("duration"), 640}}));
        QVERIFY2(rowFor(sets, QStringLiteral("PackOnly")).value(QStringLiteral("active")).toBool(),
                 "timing the set does not carry must not clear its active flag");

        // The pack half still counts, though: moving it away clears the flag.
        QVERIFY(available.size() >= 2);
        QVERIFY(c.setShaderOverride(path, available.at(1), QVariantMap{}));
        QVERIFY2(!rowFor(sets, QStringLiteral("PackOnly")).value(QStringLiteral("active")).toBool(),
                 "the half the set DOES carry must still clear the flag when it moves");
    }

    /// Assigning a PACK refreshes the set rows, the same way editing a timing
    /// value does.
    ///
    /// A motion set carries both halves of an event, so both halves move the
    /// `active` flag — but the flag is recomputed only when the store is told
    /// the live state changed, and the pack half's signal was not wired to it.
    /// Assigning a pack therefore left every row's badge showing whatever it
    /// said before: a set the user had just made current never lit up, and one
    /// they had just edited away from stayed lit. Invisible while a set carried
    /// timing alone, because the pack half could not affect a flag it was
    /// missing from.
    void motionSets_activeTracksPackAssignmentsToo()
    {
        PZ_SKIP_WITHOUT_BUNDLED_PACKS();
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::PopulatedControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();
        QVERIFY(sets);

        const QString path = QStringLiteral("window.appearance.open");
        const QStringList available = TestHelpers::pickerIdsFor(c, path);
        QVERIFY2(!available.isEmpty(), "no pack is assignable to this path; pick a different fixture");
        // Two DIFFERENT packs, so the second assignment is a real move away
        // from what the set captured.
        QVERIFY2(available.size() >= 2, "need two assignable packs to move between");

        QVERIFY(c.setShaderOverride(path, available.at(0), QVariantMap{}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Packed"), QString()));
        QSignalSpy changed(sets, &ShaderSetStore::setsChanged);
        QVERIFY2(rowFor(sets, QStringLiteral("Packed")).value(QStringLiteral("active")).toBool(),
                 "a just-saved set must read as active");

        // Move the pack away: the row must stop reading as active, and the
        // store must have been told without anything else poking it.
        QVERIFY(c.setShaderOverride(path, available.at(1), QVariantMap{}));
        // The store coalesces its refresh onto the next event-loop turn, so a
        // synchronous read here would see the pre-edit rows even with the wire
        // in place. QML re-reads on that same turn.
        QTRY_VERIFY2(changed.count() > 0, "assigning a pack did not refresh the set rows");
        QVERIFY2(!rowFor(sets, QStringLiteral("Packed")).value(QStringLiteral("active")).toBool(),
                 "assigning a different pack must clear the set's active flag");

        // And back: applying the set restores it.
        QVERIFY(sets->applySet(QStringLiteral("Packed")));
        QVERIFY2(rowFor(sets, QStringLiteral("Packed")).value(QStringLiteral("active")).toBool(),
                 "the set must read as active again right after applying it");
    }

    /// `active` measures the saved payload against the CURRENT override files.
    /// It must light up right after a save, clear once a covered path is edited
    /// away, and light again after apply. It must NOT clear because of an
    /// override the set does not cover — apply merges, so that override would
    /// have survived the apply anyway (containment, not equality).
    void motionSets_activeTracksLiveOverrides()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();
        QVERIFY(sets);

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Snappy"), QString()));

        const QVariantMap saved = rowFor(sets, QStringLiteral("Snappy"));
        QVERIFY(!saved.isEmpty());
        // Self-contained, so the count also carries the built-in pack
        // defaults; the user's own override is what this slot tracks.
        QVERIFY(saved.value(QStringLiteral("coverage")).toStringList().contains(QStringLiteral("editor")));
        QVERIFY2(saved.value(QStringLiteral("active")).toBool(), "a just-saved motion set must read as active");

        // An override the set does NOT cover must not clear the badge. Picked
        // from the saved row rather than hardcoded: a self-contained set covers
        // every path with a built-in pack default, so most leaves are inside
        // its coverage and a fixed choice here would silently stop testing
        // anything the day one more default is shipped.
        const QStringList covered = saved.value(QStringLiteral("coverage")).toStringList();
        QString outside;
        for (const QString& candidate : PhosphorAnimation::ProfilePaths::allBuiltInPaths()) {
            if (!covered.contains(candidate.section(QLatin1Char('.'), 0, 0))) {
                outside = candidate;
                break;
            }
        }
        QVERIFY2(!outside.isEmpty(), "every built-in path is inside the set's coverage; pick a different fixture");
        QVERIFY(c.setOverride(outside, {{QStringLiteral("duration"), 111}}));
        QVERIFY2(rowFor(sets, QStringLiteral("Snappy")).value(QStringLiteral("active")).toBool(),
                 "an override outside the set's coverage must not clear its active flag");

        // Editing a path the set DOES cover clears it.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 900}}));
        QVERIFY2(!rowFor(sets, QStringLiteral("Snappy")).value(QStringLiteral("active")).toBool(),
                 "editing a covered path must clear the active flag");

        // Applying restores it.
        QVERIFY(sets->applySet(QStringLiteral("Snappy")));
        QVERIFY2(rowFor(sets, QStringLiteral("Snappy")).value(QStringLiteral("active")).toBool(),
                 "the set must read as active again right after applying it");
    }

    /// updateSet renames and edits the description in one write, keeping the
    /// payload, and refuses a collision rather than destroying the other set.
    void motionSets_updateRoundTripsAndRefusesCollision()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Old"), QStringLiteral("keep me")));
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 400}}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Other"), QString()));

        QVERIFY(sets->updateSet(QStringLiteral("Old"), QStringLiteral("New"), QStringLiteral("new words")));
        QVERIFY2(rowFor(sets, QStringLiteral("Old")).isEmpty(), "the old name must be freed");
        const QVariantMap renamed = rowFor(sets, QStringLiteral("New"));
        QVERIFY(!renamed.isEmpty());
        QCOMPARE(renamed.value(QStringLiteral("description")).toString(), QStringLiteral("new words"));

        // The payload survived: applying restores the original duration.
        QVERIFY(sets->applySet(QStringLiteral("New")));
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 250);

        QVERIFY2(!sets->updateSet(QStringLiteral("New"), QStringLiteral("Other"), QString()),
                 "rename onto an existing set must be refused");
        QCOMPARE(sets->availableSets().size(), 2);
    }

    /// Saving onto an existing name must be refused rather than silently
    /// destroying the set already stored there.
    void motionSets_saveRefusesUnconfirmedOverwriteAndHonoursConsent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Taken"), QStringLiteral("first")));
        QCOMPARE(sets->existingSetName(QStringLiteral("Taken")), QStringLiteral("Taken"));

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 999}}));
        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Taken"), QStringLiteral("second")),
                 "an unconfirmed overwrite must be refused");
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(),
                 PhosphorI18n::tr("A set named “%1” already exists.").arg(QStringLiteral("Taken")));

        // The stored set is untouched: applying restores the FIRST payload.
        QVERIFY(sets->applySet(QStringLiteral("Taken")));
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 250);

        // With the user's consent the set is re-pointed at the new state.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 999}}));
        QVERIFY2(sets->saveCurrentAsSet(QStringLiteral("Taken"), QStringLiteral("second"), true),
                 "a confirmed overwrite must be honoured");
        QCOMPARE(sets->availableSets().size(), 1);
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 1}}));
        QVERIFY(sets->applySet(QStringLiteral("Taken")));
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 999);
    }

    /// Export writes a file that import reads back, in both the local-path and
    /// the file:// URL form the drop zone hands over. A colliding import lands
    /// under a free name instead of overwriting.
    void motionSets_exportImportRoundTrips()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Portable"), QString()));

        QTemporaryDir exportDir;
        QVERIFY(exportDir.isValid());
        const QString exported = exportDir.filePath(QStringLiteral("portable.json"));
        QVERIFY(sets->exportSet(QStringLiteral("Portable"), exported));
        QVERIFY(QFile::exists(exported));

        // A colliding import must not overwrite the original.
        QVERIFY(sets->importSet(exported));
        QVERIFY2(!rowFor(sets, QStringLiteral("Portable (2)")).isEmpty(),
                 "a colliding import must land under a free name");

        // The drop zone hands over a file:// URL, not a local path.
        QVERIFY2(sets->importSet(QUrl::fromLocalFile(exported).toString()),
                 "importSet must accept the file:// URL form the drop zone emits");
        QCOMPARE(sets->availableSets().size(), 3);

        // The imported payload still applies.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 900}}));
        QVERIFY(sets->applySet(QStringLiteral("Portable")));
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 250);
    }

    /// Import validates against the EVENT taxonomy, so a decoration set (whose
    /// paths are surfaces, not events) is refused at the boundary — the mirror
    /// of the decoration side's foreign-payload test.
    void motionSets_importRejectsForeignPayload()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString foreign = dir.filePath(QStringLiteral("foreign.json"));

        QJsonObject profile;
        profile.insert(QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")});
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("window.tiled")); // a decoration surface
        entry.insert(QStringLiteral("profile"), profile);
        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("Foreign"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{entry});

        QFile f(foreign);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray bytes = QJsonDocument(root).toJson();
        QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
        f.close();

        QVERIFY2(!sets->importSet(foreign), "a set whose paths are not event paths must be refused");
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// Set-file CRUD is IMMEDIATE, matching the decoration page.
    ///
    /// Saving or removing a set is not a staged edit and Discard does not undo
    /// it. The same footer button used to mean two different things on the two
    /// pages: on Decoration it never touched your sets, on Animations it
    /// silently reverted them.
    void motionSetCrudIsImmediate()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        fx.settings.save();
        c.refreshDirtyState();
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Keeper"), QString()));
        QVERIFY2(!c.hasPendingChanges(), "saving a set is not a staged edit");

        c.revertPending();
        fx.settings.load();
        QVERIFY2(!rowFor(sets, QStringLiteral("Keeper")).isEmpty(), "Discard removed a set it does not own");
    }

    /// Motion has no baseline, so a baseline-carrying file is a decoration set
    /// (or a hand edit). Accepting it would half-apply the set: apply drops the
    /// baseline while the store still counts it, so the Active badge could never
    /// light up. It has to be refused at the boundary.
    void motionSets_importRejectsBaselineCarryingSet()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();

        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("editor.snapIn"));
        entry.insert(QStringLiteral("profile"), QJsonObject{{QStringLiteral("duration"), 200}});
        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("Foreign"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{entry});
        // A decoration set's global default. Motion cannot apply it.
        root.insert(QStringLiteral("baseline"), QJsonObject{{QStringLiteral("chain"), QJsonArray{}}});

        const QString payload = tmp.path() + QStringLiteral("/foreign.json");
        QFile f(payload);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray bytes = QJsonDocument(root).toJson();
        QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
        f.close();

        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->importSet(payload), "a motion set carrying a baseline must be refused");
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(), PhosphorI18n::tr("That set does not match this page."));

        // The KEY is refused, not just a non-empty value: an empty `{}` is the
        // same foreign envelope, and tolerating it would let the two domains
        // drift on what the shared format may carry.
        root.insert(QStringLiteral("baseline"), QJsonObject{});
        const QString emptyBaseline = tmp.path() + QStringLiteral("/empty-baseline.json");
        QFile f2(emptyBaseline);
        QVERIFY(f2.open(QIODevice::WriteOnly));
        const QByteArray bytes2 = QJsonDocument(root).toJson();
        QCOMPARE(f2.write(bytes2), static_cast<qint64>(bytes2.size()));
        f2.close();
        QVERIFY2(!sets->importSet(emptyBaseline), "an empty baseline object is refused the same way");
    }

    /// Dirtiness is live-versus-committed, so editing back to the committed
    /// value leaves the page clean — no sticky flag survives the round trip.
    ///
    /// The staged-snapshot model could get this wrong in both directions: it
    /// could drop the only copy of the pre-edit content, or leave the page
    /// dirty forever with nothing left to discard.
    void editingBackToTheCommittedValueLeavesThePageClean()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());
        const QString path = QStringLiteral("editor.snapIn");

        QVERIFY(c.setOverride(path, {{QStringLiteral("duration"), 100}}));
        fx.settings.save();
        c.refreshDirtyState();
        QVERIFY(!c.hasPendingChanges());

        QVERIFY(c.setOverride(path, {{QStringLiteral("duration"), 200}}));
        QVERIFY(c.hasPendingChanges());
        QVERIFY(c.setOverride(path, {{QStringLiteral("duration"), 300}}));
        QVERIFY(c.hasPendingChanges());

        QVERIFY(c.setOverride(path, {{QStringLiteral("duration"), 100}}));
        QVERIFY2(!c.hasPendingChanges(), "editing back to the committed value must leave the page clean");
    }
};

QTEST_MAIN(TestAnimationsMotionSets)
#include "test_animations_motion_sets.moc"
