// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_motion_sets.cpp
 * @brief AnimationsPageController user-preset, motion-set, and pending-changes tests.
 *
 * Split from test_animations_page_controller.cpp to keep each test file
 * under the project's <800-line guideline.
 *
 * Motion sets go through the shared ShaderSetStore reached from
 * `AnimationsPageController::setsBridge()`. The domain half (which paths are
 * valid, how live state is snapshotted) lives in motionsetdomain.cpp; the
 * envelope, the file lifecycle and the `active` summary are the store's.
 * Pinned behaviour:
 *   - User preset CRUD (addUserPreset, userPresets, removeUserPreset)
 *   - Motion set save/apply/remove with merge-not-replace semantics
 *   - `active` is a CONTAINMENT check: a set stays active while unrelated
 *     overrides exist, because apply would have left them alone
 *   - updateSet / exportSet / importSet round-trip, and an import is validated
 *     against the EVENT taxonomy (a decoration set is refused here)
 *   - The in-flight-discard mutation guard refuses every set write
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
#include <QStandardPaths>
#include <QUrl>

#include "settings/animationspagecontroller.h"
#include "settings/shadersetstore.h"

using namespace PlasmaZones;

namespace {

/// The row for @p name, or an empty map when no such set is listed. Callers
/// QVERIFY the result is non-empty BEFORE asserting on a field, so a missing
/// row fails loudly instead of skipping the assertion.
QVariantMap rowFor(ShaderSetStore* sets, const QString& name)
{
    const QVariantList rows = sets->availableSets();
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("name")).toString() == name)
            return map;
    }
    return QVariantMap{};
}

} // namespace

class TestAnimationsMotionSets : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Every test overrides the profiles dir to a QTemporaryDir, but the
    /// motion-sets dir is derived from GenericDataLocation. Redirect it so a
    /// test can never write into the user's real ~/.local/share.
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

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
        const QByteArray objBytes = QJsonDocument(obj).toJson();
        QCOMPARE(f.write(objBytes), static_cast<qint64>(objBytes.size()));
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
    void motionSets_saveRefusesToOverwriteExistingName()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());
        ShaderSetStore* sets = c.setsBridge();

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Taken"), QStringLiteral("first")));

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 999}}));
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Taken"), QStringLiteral("second")),
                 "saving onto an existing set name must be refused");

        // The stored set is untouched: applying restores the FIRST payload.
        QVERIFY(sets->applySet(QStringLiteral("Taken")));
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("duration")).toInt(), 250);
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
        QCOMPARE(toastSpy.count(), 4); // each refusal carries its reason

        // The refused save never reached disk.
        QVERIFY(rowFor(sets, QStringLiteral("During")).isEmpty());

        // Let the worker finish so the fixture tears down cleanly.
        QSignalSpy done(&c, &PhosphorControl::StagingDomain::discardResult);
        QVERIFY(done.wait(5000));
    }
};

QTEST_MAIN(TestAnimationsMotionSets)
#include "test_animations_motion_sets.moc"
