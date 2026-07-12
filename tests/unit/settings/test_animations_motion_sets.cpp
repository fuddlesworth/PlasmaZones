// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_motion_sets.cpp
 * @brief AnimationsPageController user-preset, motion-set, and pending-changes tests.
 *
 * Split from test_animations_page_controller.cpp to keep each test file
 * under the project's <800-line guideline. Pinned behaviour:
 *   - User preset CRUD (addUserPreset, userPresets, removeUserPreset)
 *   - Motion set save/apply/remove with merge-not-replace semantics
 *   - Pending changes signal emission for revert/commit
 *   - Atomic motion-set application (rejects whole malformed set)
 *   - Cross-cutting safety: removeUserPreset must not touch override files,
 *     malformed preset JSON logs+skips
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

#include <PhosphorAnimation/Profile.h>

#include "settings/animationspagecontroller.h"
#include "settings/shadersetstore.h"

using namespace PlasmaZones;

class TestAnimationsMotionSets : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── User preset library ──────────────────────────────────────────────

    void addUserPreset_writesFileWithSlugFilename()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        QVERIFY(c.addUserPreset(
            QStringLiteral("My Snappy Spring!"),
            {{QStringLiteral("curve"), QStringLiteral("spring:14.0,0.7")}, {QStringLiteral("duration"), 200}}));
        QCOMPARE(spy.count(), 1);

        // Slugified filename: lowercase, non-alnum collapsed to '-',
        // trailing '-' trimmed.
        QVERIFY(QFileInfo::exists(tmp.path() + QStringLiteral("/my-snappy-spring.json")));
    }

    void userPresets_excludesPathNamedFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Preset file
        QVERIFY(
            c.addUserPreset(QStringLiteral("My Curve"), {{QStringLiteral("curve"), QStringLiteral("0.5,0,0.5,1")}}));
        // Override file at a known path
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));

        // userPresets sees ONLY the preset, not the override
        const QVariantList presets = c.userPresets();
        QCOMPARE(presets.size(), 1);
        QCOMPARE(presets.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("My Curve"));
    }

    /// An orphan override file at a path that this build no longer
    /// recognises (e.g. left over after a taxonomy rename like PR #400's
    /// `panel.popup.*` → `popup.*`) MUST NOT leak into the preset list.
    /// `userPresets` previously filtered only on the current
    /// `allBuiltInPaths()`; an orphan file at the obsolete path would
    /// pass that gate and surface as a fake preset named after the
    /// obsolete event path. The guard is the basename-contains-dot
    /// check: `setOverride` writes verbatim path filenames (always
    /// dotted for non-root paths), `addUserPreset` slugifies (strips
    /// dots), so a dotted basename is the override-file fingerprint.
    void userPresets_excludesOrphanOverrideFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Plant an orphan override file directly: the on-disk shape
        // setOverride would have written for the obsolete path
        // `panel.popup` (which existed pre-PR-400 but isn't in
        // `allBuiltInPaths()` post-rename). `setOverride` itself rejects
        // unknown paths now, so we hand-craft the file the way an older
        // build would have.
        QFile f(tmp.path() + QStringLiteral("/panel.popup.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), QStringLiteral("panel.popup"));
        obj.insert(QStringLiteral("duration"), 1000);
        obj.insert(QStringLiteral("curve"), QStringLiteral("0.33,1,0.68,1"));
        f.write(QJsonDocument(obj).toJson());
        f.close();

        // Add a real preset alongside.
        QVERIFY(c.addUserPreset(QStringLiteral("My Curve"), {{QStringLiteral("duration"), 200}}));

        // userPresets must see ONLY the real preset; the orphan file
        // is a stale override, not a preset.
        const QVariantList presets = c.userPresets();
        QCOMPARE(presets.size(), 1);
        QCOMPARE(presets.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("My Curve"));
    }

    void addUserPreset_rejectsKnownPathNames()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        // "editor.snapIn" is a known event path — would shadow the
        // override slot.
        QVERIFY(!c.addUserPreset(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 100}}));
        QCOMPARE(spy.count(), 0);
        QVERIFY(c.userPresets().isEmpty());
    }

    void addUserPreset_rejectsEmptyAndAllSymbol()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(!c.addUserPreset(QString(), {{QStringLiteral("duration"), 100}}));
        // All-symbol slugifies to empty → reject (would write to ".json")
        QVERIFY(!c.addUserPreset(QStringLiteral("@@@"), {{QStringLiteral("duration"), 100}}));
    }

    void removeUserPreset_emitsAndDeletes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(
            c.addUserPreset(QStringLiteral("To Delete"), {{QStringLiteral("curve"), QStringLiteral("spring:10,0.5")}}));
        QCOMPARE(c.userPresets().size(), 1);

        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        QVERIFY(c.removeUserPreset(QStringLiteral("To Delete")));
        QCOMPARE(spy.count(), 1);
        QVERIFY(c.userPresets().isEmpty());
    }

    void removeUserPreset_unknownReturnsFalse()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        QVERIFY(!c.removeUserPreset(QStringLiteral("nonexistent")));
        QCOMPARE(spy.count(), 0);
    }

    /// `removeUserPreset` MUST not delete an override file even when its
    /// embedded `name` field happens to match the supplied preset name.
    /// Pre-fix, the directory-scan fallback walked every JSON file in
    /// the profiles dir and matched by `name`; an override file whose
    /// `name` matched the searched preset would be deleted.
    void removeUserPreset_doesNotTouchOverrideFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Write an override file. Its on-disk `name` field is "editor.snapIn"
        // (per setOverride's stamping rule).
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        const QString overrideFilePath = tmp.path() + QStringLiteral("/editor.snapIn.json");
        QVERIFY(QFileInfo::exists(overrideFilePath));

        // Hand-craft a hostile preset on disk: `name` field = "editor.snapIn"
        // but the FILE is not at the canonical override slot. The library
        // skips it on userPresets() (collision filter) but a naive
        // remove-by-name walk would still delete it. We're asserting the
        // override file in the canonical slot remains intact regardless.
        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        QVERIFY(!c.removeUserPreset(QStringLiteral("editor.snapIn")));
        QCOMPARE(spy.count(), 0);

        // The override file MUST still exist — this is the load-bearing
        // assertion: removeUserPreset cannot collateral-damage overrides.
        QVERIFY2(
            QFileInfo::exists(overrideFilePath),
            "override file was deleted by removeUserPreset(\"editor.snapIn\") — preset CRUD MUST NOT touch override "
            "slots");
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 250);
    }

    // ─── Motion sets ──────────────────────────────────────────────────────

    void saveCurrentAsMotionSet_capturesPathOverridesOnly()
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

    void applyMotionSet_writesPerPathFiles()
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

    void applyMotionSet_mergesPreservesOtherPaths()
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

    void removeMotionSet_emitsAndDeletes()
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
    /// invalid entries) and verify applyMotionSet() rejects the whole
    /// thing rather than partially writing. Pre-fix, the loop wrote
    /// each valid entry and skipped invalid ones, leaving inconsistent
    /// state.
    void applyMotionSet_malformedEntryRejectsWholeSet()
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
        f.write(QJsonDocument(root).toJson());
        f.close();

        // No prior override at editor.snapIn.
        QVERIFY(!c.hasOverride(QStringLiteral("editor.snapIn")));

        QVERIFY(!c.setsBridge()->applySet(QStringLiteral("bad-set")));

        // Critical: the valid entry MUST NOT have been written. Atomic
        // semantics — all-or-nothing. Pre-fix this would be true.
        QVERIFY2(!c.hasOverride(QStringLiteral("editor.snapIn")),
                 "applyMotionSet wrote partial state from a malformed set — should have rejected atomically");
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

    // ─── Logging on malformed JSON ────────────────────────────────────────

    /// Plant an unparseable JSON file in the profiles dir; userPresets()
    /// should skip it AND log a warning at the qCWarning level. The
    /// emission is the load-bearing piece — pre-fix the parse error was
    /// silently swallowed.
    void userPresets_malformedJsonLogsAndSkips()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Write a known-good preset so the iteration has at least one
        // success result.
        QVERIFY(c.addUserPreset(QStringLiteral("Good"), {{QStringLiteral("duration"), 100}}));

        // Plant a malformed file directly.
        QFile bad(tmp.path() + QStringLiteral("/garbage.json"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ this is not valid json");
        bad.close();

        // Expect a warning to be logged for the malformed file. The
        // exact message is "AnimationPresetLibrary: failed to parse <path> : <error>".
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("AnimationPresetLibrary: failed to parse.*garbage")));

        const QVariantList presets = c.userPresets();
        // The good preset still surfaces; the malformed one is skipped.
        QCOMPARE(presets.size(), 1);
        QCOMPARE(presets.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Good"));
    }
};

QTEST_MAIN(TestAnimationsMotionSets)
#include "test_animations_motion_sets.moc"
