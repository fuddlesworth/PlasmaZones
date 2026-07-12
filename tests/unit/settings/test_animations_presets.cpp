// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_presets.cpp
 * @brief AnimationsPageController user-preset library tests.
 *
 * Split from test_animations_motion_sets.cpp to keep each test file under the
 * project's 800-line cap, and because presets and sets are separate
 * sub-services (AnimationPresetLibrary vs ShaderSetStore). Pinned behaviour:
 *   - User preset CRUD (addUserPreset, userPresets, removeUserPreset), with
 *     slugified filenames
 *   - A preset can never shadow an event-override slot: a name matching a
 *     known event path is refused, and an override FILE never surfaces as a
 *     preset (not even an orphan left at a path this build no longer knows)
 *   - removeUserPreset must not touch override files, even when an override's
 *     embedded `name` happens to match the preset being removed
 *   - Malformed preset JSON logs and is skipped rather than breaking the list
 *   - A write that fails AFTER the pre-edit snapshot was taken un-stages that
 *     snapshot, so the page does not report an unsaved change to a file that
 *     was never touched
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include "settings/animationspagecontroller.h"

using namespace PlasmaZones;

class TestAnimationsPresets : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Every test overrides the profiles dir to a QTemporaryDir. Redirect
    /// GenericDataLocation too, so nothing can reach the real ~/.local/share.
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
        // All-symbol slugifies to empty, so there is no usable filename. The
        // library refuses and toasts rather than failing silently.
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

        // The override file's own `name` field is "editor.snapIn", so a naive
        // remove-by-name directory walk would match it and delete it. The
        // library must refuse: preset CRUD does not own override files.
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
        const QByteArray garbage = "{ this is not valid json";
        QCOMPARE(bad.write(garbage), static_cast<qint64>(garbage.size()));
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
    /// A failed write leaves the file untouched, so the snapshot it staged for
    /// Discard has to go back too. Without the rollback the page sits dirty
    /// with nothing to restore, and the only way out is a no-op Discard.
    void failedWriteDoesNotLeaveThePageDirty()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());
        QVERIFY(!c.hasPendingChanges());

        // Read-execute only: the directory still exists (so the snapshot side
        // stages a "did not exist" entry for the new file and allows the write),
        // but QSaveFile cannot create anything inside it.
        QVERIFY(QFile::setPermissions(tmp.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));

        QSignalSpy toastSpy(&c, &AnimationsPageController::toastRequested);
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("AnimationPresetLibrary: could not write")));
        const bool written = c.addUserPreset(QStringLiteral("Doomed"), {{QStringLiteral("duration"), 200}});

        // Restore before any assertion can abort the test and leak the mode
        // (QTemporaryDir cannot clean up a directory it may not write).
        QVERIFY(QFile::setPermissions(tmp.path(),
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        if (written)
            QSKIP("The write succeeded, so this environment ignores directory permissions (running as root?).");

        QCOMPARE(toastSpy.count(), 1);
        // The load-bearing assertion: the failed write staged nothing.
        QVERIFY(!c.hasPendingChanges());
    }
};

QTEST_MAIN(TestAnimationsPresets)
#include "test_animations_presets.moc"
