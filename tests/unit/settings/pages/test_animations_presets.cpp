// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_presets.cpp
 * @brief AnimationsPageController user-preset library tests.
 *
 * Split from test_animations_motion_sets.cpp because presets and sets are
 * separate sub-services (AnimationPresetLibrary vs ShaderSetStore). Pinned
 * behaviour:
 *   - User preset CRUD (addUserPreset, userPresets, removeUserPreset), with
 *     slugified filenames
 *   - A preset can never shadow an event-override slot: a name matching a
 *     known event path is refused, and an override FILE never surfaces as a
 *     preset (not even an orphan left at a path this build no longer knows)
 *   - removeUserPreset must not touch override files, even when an override's
 *     embedded `name` happens to match the preset being removed
 *   - Malformed preset JSON logs and is skipped rather than breaking the list
 *   - A write that cannot reach disk toasts rather than failing silently
 *   - Preset CRUD is IMMEDIATE: it does not stage, so Discard does not undo it
 *
 * The snapshot-and-rollback contract this file used to pin went with schema v8.
 * The library is constructed with no snapshot or rollback hooks at all.
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
#include <QVariantList>
#include <QVariantMap>

#include "settings/pages/animationspagecontroller.h"
#include "helpers/AnimationsControllerFixture.h"

using namespace PlasmaZones;

class TestAnimationsPresets : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Deliberately EMPTY, and it must stay that way.
    ///
    /// This used to call `QStandardPaths::setTestModeEnabled(true)`, which
    /// defeats every slot's `IsolatedConfigGuard`: the guard isolates by
    /// setting `XDG_CONFIG_HOME`, and test mode makes QStandardPaths ignore
    /// that and return one fixed `~/.qttest/config` for the whole process. So
    /// every slot shared a single config file, and since schema v8 put the
    /// per-event overrides IN config, one slot's overrides leaked into the
    /// next — quietly satisfying assertions that were supposed to be testing
    /// their own slot's setup. `test_animations_motion_sets` removed the same
    /// call for the same reason.
    ///
    /// Isolation is already complete without it: each slot's guard gives it a
    /// private XDG_CONFIG_HOME and XDG_DATA_HOME, the CMake sweep gives the
    /// whole target its own XDG root, and the file-backed preset library is
    /// redirected per slot by `setUserProfilesDirOverride`.
    void initTestCase()
    {
    }

    // ─── User preset library ──────────────────────────────────────────────

    void addUserPreset_writesFileWithSlugFilename()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
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
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        // Preset file
        QVERIFY(
            c.addUserPreset(QStringLiteral("My Curve"), {{QStringLiteral("curve"), QStringLiteral("0.5,0,0.5,1")}}));

        // An override file at a KNOWN path, planted by hand. It has to be
        // planted rather than written through setOverride: since schema v8
        // setOverride writes a config key and puts nothing in this directory,
        // so driving it here would leave the assertion below true no matter
        // what the library filtered. The file is what a pre-v8 build left
        // behind, and it is still sitting in the profiles dir the preset
        // library reads — the migration copies those files, it does not remove
        // them.
        //
        // This is the KNOWN-path case, which the orphan slot below does not
        // reach: its file names a path allBuiltInPaths() no longer contains, so
        // it exercises the dotted-basename guard. This one is caught earlier,
        // by the known-path membership check.
        QFile f(tmp.path() + QStringLiteral("/editor.snapIn.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), QStringLiteral("editor.snapIn"));
        obj.insert(QStringLiteral("duration"), 250);
        const QByteArray objBytes = QJsonDocument(obj).toJson();
        QCOMPARE(f.write(objBytes), static_cast<qint64>(objBytes.size()));
        f.close();

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
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
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
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
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
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(!c.addUserPreset(QString(), {{QStringLiteral("duration"), 100}}));
        // All-symbol slugifies to empty, so there is no usable filename.
        QVERIFY(!c.addUserPreset(QStringLiteral("@@@"), {{QStringLiteral("duration"), 100}}));
    }

    void removeUserPreset_emitsAndDeletes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
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
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
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
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        // A LEFTOVER override FILE, planted by hand because that is the state
        // the v8 migration actually creates: it reads the pre-v8 per-event
        // files into config and deliberately leaves them on disk so an
        // older version still works.
        //
        // Planted rather than written through setOverride: since v8 that
        // writes to CONFIG, so the profiles dir would stay empty and the
        // remove-by-name walk — which only ever touches files — could not
        // collateral-damage anything no matter how broken it was. Asserted
        // that way this slot could not fail; deleting the guard it names left
        // it green.
        const QString orphanPath = tmp.path() + QStringLiteral("/editor.snapIn.json");
        {
            QFile f(orphanPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(QJsonObject{{QStringLiteral("name"), QStringLiteral("editor.snapIn")},
                                              {QStringLiteral("duration"), 250}})
                        .toJson());
        }
        QVERIFY(QFileInfo::exists(orphanPath));

        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        QVERIFY(!c.removeUserPreset(QStringLiteral("editor.snapIn")));
        QCOMPARE(spy.count(), 0);

        // The load-bearing assertion: the walk must recognise this name as an
        // event path rather than a preset, and refuse, leaving the file alone.
        QVERIFY2(QFileInfo::exists(orphanPath),
                 "removeUserPreset(\"editor.snapIn\") deleted a leftover override file — preset CRUD MUST NOT "
                 "touch anything named after an event path");
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
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
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
    /// A preset write that cannot reach disk reports the failure to the user
    /// rather than failing silently.
    ///
    /// This slot used to assert that the failure also left no staged snapshot
    /// behind. That assertion is gone because it can no longer fail: preset CRUD
    /// is immediate since schema v8, the library is constructed with no snapshot
    /// or rollback hooks at all, and `hasPendingChanges()` is a value comparison
    /// over the two config trees that never consults a preset file. Asserting
    /// `!hasPendingChanges()` on a page nothing here can dirty would pass with
    /// the whole write path deleted.
    void failedWriteToastsRatherThanFailingSilently()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        // Read-execute only: the directory still exists, so the write is
        // attempted, but QSaveFile cannot create anything inside it.
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

        // The load-bearing assertion: the user is told. Deleting the
        // toastRequested emit in AnimationPresetLibrary's write-failure arm
        // fails this.
        QCOMPARE(toastSpy.count(), 1);
        // And the preset really did not land, so the list is unchanged.
        QVERIFY(c.userPresets().isEmpty());
    }

    /// Preset CRUD is IMMEDIATE, matching the decoration page's set files.
    /// Creating or deleting one is not a staged edit and Discard does not undo
    /// it — the same footer button used to mean two different things on the two
    /// pages, silently reverting an animation preset a user had just saved.
    void presetCrudIsImmediateAndDoesNotDirtyThePage()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        // Dirty the page FIRST with a real staged timing edit. Asserting
        // `!hasPendingChanges()` on a page that was never dirty is an
        // assertion that cannot fail — it would hold just as well if preset
        // CRUD staged nothing because it did nothing. What the slot is
        // actually about is that preset CRUD leaves the page's dirty state
        // exactly as it found it, so there has to be a state to leave.
        QVERIFY(!c.hasPendingChanges());
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(c.hasPendingChanges());

        QVERIFY(c.addUserPreset(QStringLiteral("Temp"), {{QStringLiteral("duration"), 200}}));
        QVERIFY2(c.hasPendingChanges(), "saving a preset changed the page's dirty state; it is not a staged edit");

        QVERIFY(c.removeUserPreset(QStringLiteral("Temp")));
        QVERIFY2(c.hasPendingChanges(), "deleting a preset changed the page's dirty state");

        // And the CRUD really was immediate: a Discard reverts the timing edit
        // but leaves the preset library alone.
        QVERIFY(c.addUserPreset(QStringLiteral("Keeper"), {{QStringLiteral("duration"), 210}}));
        QVERIFY(c.revertPending());
        fx.settings.load();
        c.refreshDirtyState();
        QVERIFY2(!c.hasPendingChanges(), "Discard did not revert the staged timing edit");
        const QVariantList afterDiscard = c.userPresets();
        bool keeperSurvived = false;
        for (const QVariant& preset : afterDiscard) {
            if (preset.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Keeper")) {
                keeperSurvived = true;
                break;
            }
        }
        QVERIFY2(keeperSurvived, "Discard removed a saved preset; preset CRUD is immediate and outside the staged set");
    }
};

QTEST_MAIN(TestAnimationsPresets)
#include "test_animations_presets.moc"
