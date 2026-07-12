// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_motion_sets.cpp
 * @brief AnimationsPageController motion-set and pending-changes tests.
 *
 * Split from test_animations_page_controller.cpp, and the user-preset half
 * split again into test_animations_presets.cpp, to keep each test file under
 * the project's <800-line guideline.
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
 *   - Motion is the only domain that STAGES set files, so it is the only one
 *     that can pin the two staging contracts: a write is refused when the
 *     pre-edit content cannot be captured (rather than losing it), and Discard
 *     restores set files written this session
 *   - The in-flight-discard mutation guard refuses every set write
 *   - Pending changes signal emission for revert/commit
 *   - Atomic motion-set application (rejects whole malformed set)
 *   - Motion has no baseline, so a baseline-carrying set is refused at import
 *   - The phantom-snapshot rollback drops a staging whose file is back to its
 *     pre-edit content, and KEEPS one whose edit actually landed
 *   - revertPending() reports its own refusal while an async discard is in
 *     flight, so a caller cannot mark the state clean underneath the worker
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
#include "settings/animationspagecontroller.h"
#include "settings/shadersetstore.h"
#include "../helpers/SetRowHelpers.h"

using namespace PlasmaZones;

class TestAnimationsMotionSets : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Every test here overrides the profiles dir to a QTemporaryDir, and the
    /// motion-sets dir hangs off that override, so nothing in this file reaches
    /// the real data location today. The redirect is a net for a future test
    /// that forgets setUserProfilesDirOverride: without it, that test would
    /// write into the user's real ~/.local/share.
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // ─── Motion sets ──────────────────────────────────────────────────────

    void saveCurrentAsSet_capturesPathOverridesOnly()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
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
        // Should capture the 2 path overrides, NOT the preset
        QCOMPARE(set.value(QStringLiteral("coverageCount")).toInt(), 2);
    }

    void applySet_writesPerPathFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
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

    void applySet_mergesPreservesOtherPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
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
        AnimationsPageController c;
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
        AnimationsPageController c;
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
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());
        QVERIFY(!c.hasPendingChanges());
    }

    void setOverride_emitsPendingChangesChanged()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QCOMPARE(spy.count(), 1);
        QVERIFY(c.hasPendingChanges());
    }

    void revertPending_restoresPreEditFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Establish a pre-edit baseline: write 100 ms.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 100}}));
        c.commitPending();
        QVERIFY(!c.hasPendingChanges());

        // Edit to 250 ms → should snapshot the 100ms file.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 250);

        // Revert → file content restored to 100 ms.
        c.revertPending();
        QVERIFY(!c.hasPendingChanges());
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 100);
    }

    void revertPending_deletesFilesThatDidntExistBefore()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Fresh override on a path with no prior file.
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 200}}));
        QVERIFY(c.hasOverride(QStringLiteral("osd.show")));

        c.revertPending();
        QVERIFY(!c.hasOverride(QStringLiteral("osd.show")));
    }

    void revertPending_emitsOverrideChangedPerPath()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 200}}));
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 300}}));

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);
        c.revertPending();

        // Two paths reverted → two emissions (one per file). Order is
        // hash-iteration so we check membership rather than position.
        QCOMPARE(spy.count(), 2);
        QStringList emitted;
        for (const auto& args : spy)
            emitted << args.at(0).toString();
        QVERIFY(emitted.contains(QStringLiteral("editor.snapIn")));
        QVERIFY(emitted.contains(QStringLiteral("osd.show")));
    }

    void commitPending_clearsSnapshotWithoutEmittingDataChanged()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(c.hasPendingChanges());

        // commitPending clears the snapshot and only emits
        // pendingChangesChanged (once). It must NOT re-fire the
        // per-path overrideChanged or the data signals — the user just
        // saved, no rows visually moved.
        QSignalSpy pendingSpy(&c, &AnimationsPageController::pendingChangesChanged);
        QSignalSpy overrideSpy(&c, &AnimationsPageController::overrideChanged);
        c.commitPending();
        QVERIFY(!c.hasPendingChanges());
        QCOMPARE(pendingSpy.count(), 1);
        QCOMPARE(overrideSpy.count(), 0);
    }

    // ─── Motion sets: active flag, metadata edit, portability, guard ───────

    /// `active` measures the saved payload against the CURRENT override files.
    /// It must light up right after a save, clear once a covered path is edited
    /// away, and light again after apply. It must NOT clear because of an
    /// override the set does not cover — apply merges, so that override would
    /// have survived the apply anyway (containment, not equality).
    void motionSets_activeTracksLiveOverrides()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();
        QVERIFY(sets);

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Snappy"), QString()));

        const QVariantMap saved = rowFor(sets, QStringLiteral("Snappy"));
        QVERIFY(!saved.isEmpty());
        QCOMPARE(saved.value(QStringLiteral("coverageCount")).toInt(), 1);
        QCOMPARE(saved.value(QStringLiteral("coverage")).toStringList(), (QStringList{QStringLiteral("editor")}));
        QVERIFY2(saved.value(QStringLiteral("active")).toBool(), "a just-saved motion set must read as active");

        // An override the set does NOT cover must not clear the badge.
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 111}}));
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
        AnimationsPageController c;
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
        AnimationsPageController c;
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
        AnimationsPageController c;
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
        AnimationsPageController c;
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

    /// Motion is the only domain that stages set files, so it is the only one
    /// that can prove the store's data-loss guard: when the pre-edit content of
    /// an existing set cannot be captured, the write is REFUSED rather than
    /// destroying content Discard could not restore.
    void motionSets_refusesWriteWhenSnapshotFails()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Precious"), QStringLiteral("keep me")));

        // Commit so the set file is no longer already snapshotted, then remove
        // it and put a DIRECTORY in its place. QFile::exists() is still true but
        // open(ReadOnly) fails, so snapshotFileIfFirst reports the capture
        // failure — and unlike chmod 000, this provokes it for root too, so the
        // guard is actually exercised in the project's Docker flow.
        c.commitPending();
        const QString setPath = tmp.path() + QStringLiteral("/motionsets/precious.json");
        QVERIFY(QFileInfo::exists(setPath));
        QVERIFY(QFile::remove(setPath));
        QVERIFY(QDir().mkpath(setPath));
        QVERIFY(QFileInfo(setPath).isDir());

        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        // A directory is not a regular file, so the snapshot refuses at that
        // gate. The ignoreMessage pair pins the branch: an unmatched expectation
        // fails the test, so neither the missing-file path nor an
        // already-snapshotted early return can satisfy this.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("snapshotFileIfFirst: refusing to snapshot")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing to write")));
        QVERIFY2(!sets->removeSet(QStringLiteral("Precious")),
                 "a delete must be refused when the pre-edit content cannot be captured");
        // Assert WHICH refusal fired. A missing-file refusal would also return
        // false and toast once, so the count alone cannot tell the two apart —
        // and the point of this test is the snapshot guard specifically.
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(),
                 PhosphorI18n::tr("Could not back up the existing set, so it was left untouched."));

        // Nothing was destroyed: the path is untouched.
        QVERIFY(QFileInfo(setPath).isDir());
    }

    /// Motion set writes are staged, so Discard must put the world back: a set
    /// saved this session disappears again, and a set removed this session
    /// comes back.
    void motionSets_discardRestoresSetFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Keeper"), QString()));
        c.commitPending(); // "Keeper" is now saved state, not pending
        QVERIFY(!c.hasPendingChanges());

        // Save a second set and remove the first, both this session.
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Fresh"), QString()));
        QVERIFY(sets->removeSet(QStringLiteral("Keeper")));
        QVERIFY2(c.hasPendingChanges(), "set writes must mark the page dirty");
        QVERIFY(rowFor(sets, QStringLiteral("Keeper")).isEmpty());
        QVERIFY(!rowFor(sets, QStringLiteral("Fresh")).isEmpty());

        c.revertPending();

        QVERIFY2(!rowFor(sets, QStringLiteral("Keeper")).isEmpty(), "Discard must restore a set removed this session");
        QVERIFY2(rowFor(sets, QStringLiteral("Fresh")).isEmpty(), "Discard must drop a set saved this session");
        QVERIFY(!c.hasPendingChanges());
    }

    /// The in-flight-discard guard is what stops a set write from landing
    /// mid-revert and being clobbered by the async restore walk. It moved out
    /// of the controller and into the store as an injected mutationGuard when
    /// QML started calling the store directly, so pin it: every mutator must
    /// refuse while a discard is in flight, and each must say why.
    void motionSets_mutationGuardRefusesWritesDuringDiscard()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Before"), QString()));

        // A file on hand for the importSet refusal below.
        QTemporaryDir exportDir;
        QVERIFY(exportDir.isValid());
        const QString exported = exportDir.filePath(QStringLiteral("before.json"));
        QVERIFY(sets->exportSet(QStringLiteral("Before"), exported));

        // Kick off the async discard and immediately try to write, without
        // spinning the event loop — the worker is in flight.
        c.asyncRevertPending();

        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("During"), QString()),
                 "a save must be refused while a discard is in flight");
        QVERIFY2(!sets->applySet(QStringLiteral("Before")), "an apply must be refused mid-discard");
        QVERIFY2(!sets->removeSet(QStringLiteral("Before")), "a remove must be refused mid-discard");
        QVERIFY2(!sets->updateSet(QStringLiteral("Before"), QStringLiteral("Renamed"), QString()),
                 "an update must be refused mid-discard");
        QVERIFY2(!sets->importSet(exported), "an import must be refused mid-discard");
        QCOMPARE(toastSpy.count(), 5); // each refusal carries its reason
        // Pin the REASON, not just the count. The worker is rewriting "Before"
        // while these run, so a read or missing-file refusal would produce the
        // same false-plus-one-toast shape. Only the guard says this.
        for (const QList<QVariant>& args : toastSpy)
            QCOMPARE(args.first().toString(), PhosphorI18n::tr("Cannot modify sets while a discard is in progress."));

        // Let the worker finish before reading the dir back — it is rewriting
        // the same tree underneath us.
        QSignalSpy done(&c, &PhosphorControl::StagingDomain::discardResult);
        QVERIFY(done.wait(5000));

        // The refused save never reached disk.
        QVERIFY(rowFor(sets, QStringLiteral("During")).isEmpty());
    }
    /// Motion has no baseline, so a baseline-carrying file is a decoration set
    /// (or a hand edit). Accepting it would half-apply the set: apply drops the
    /// baseline while the store still counts it, so the Active badge could never
    /// light up. It has to be refused at the boundary.
    void motionSets_importRejectsBaselineCarryingSet()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
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
    }

    /// The snapshot is the ONLY copy of a file's pre-edit content, so the
    /// phantom-dirty rollback must drop it ONLY while disk still matches it. An
    /// edit that landed keeps its way back; an edit undone by hand does not.
    void snapshotRollback_dropsPhantomButKeepsALandedEdit()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());
        const QString path = QStringLiteral("editor.snapIn");

        // Establish a committed baseline on disk, then start clean.
        QVERIFY(c.setOverride(path, {{QStringLiteral("duration"), 100}}));
        c.commitPending();
        QVERIFY(!c.hasPendingChanges());

        // An edit that LANDS: the snapshot (duration 100) is the only copy of the
        // pre-edit content and must survive a further edit.
        QVERIFY(c.setOverride(path, {{QStringLiteral("duration"), 200}}));
        QVERIFY(c.hasPendingChanges());
        QVERIFY(c.setOverride(path, {{QStringLiteral("duration"), 300}}));
        QVERIFY2(c.hasPendingChanges(), "disk no longer matches the snapshot, so it must NOT be dropped");

        // Undo by hand, back to the exact pre-edit content: the snapshot is now a
        // phantom, and keeping it would leave the page dirty with nothing to
        // discard.
        QVERIFY(c.setOverride(path, {{QStringLiteral("duration"), 100}}));
        QVERIFY2(!c.hasPendingChanges(), "an undo back to the pre-edit content must clear the staged snapshot");
    }

    /// revertPending() refuses while an async discard owns the snapshot map, and
    /// it has to SAY so: a caller that goes on to declare the state clean would
    /// otherwise strand the snapshots the worker is still restoring.
    void revertPending_refusesAndReportsWhileAsyncDiscardIsInFlight()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 200}}));
        QVERIFY(c.hasPendingChanges());

        QSignalSpy done(&c, &AnimationsPageController::discardResult);
        c.asyncRevertPending(); // sets the in-flight flag synchronously
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("revertPending: blocked while an async discard")));
        QVERIFY2(!c.revertPending(), "a synchronous revert must refuse, and report the refusal, mid-discard");

        QVERIFY(done.wait(5000));
        QVERIFY(!c.hasPendingChanges());
    }
    /// A reset that runs while the discard worker owns the snapshot map would have
    /// every clearOverride refuse individually, and the caller would read the
    /// resulting 0 as "there was nothing to clear" rather than "nothing was
    /// cleared". It reports -1 instead, and the override files stay put.
    void clearAllOverrides_refusesWhileAsyncDiscardIsInFlight()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 200}}));

        QSignalSpy done(&c, &AnimationsPageController::discardResult);
        QSignalSpy toastSpy(&c, &AnimationsPageController::toastRequested);
        c.asyncRevertPending(); // sets the in-flight flag synchronously
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("clearAllOverrides: refusing while an async discard")));
        QCOMPARE(c.clearAllOverrides(), -1);
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(), PhosphorI18n::tr("Cannot reset while a discard is in progress."));

        QVERIFY(done.wait(5000));
    }
};

QTEST_MAIN(TestAnimationsMotionSets)
#include "test_animations_motion_sets.moc"
