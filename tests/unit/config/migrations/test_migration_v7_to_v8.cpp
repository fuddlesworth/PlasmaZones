// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @brief v7 → v8: per-event motion overrides move from loose files into config.
 *
 * The step must:
 *   - fold each `<data>/plasmazones/profiles/<event.path>.json` into
 *     Animations/MotionProfileTree in ProfileTree's own serialized shape;
 *   - leave USER PRESETS alone — files in the same directory whose `name` is
 *     not a built-in event path are named by the user from the curve editor,
 *     and migrating one would invent an override for a path that does not
 *     exist;
 *   - never overwrite an existing MotionProfileTree, so re-running the chain
 *     (or restoring a config by hand) cannot clobber a v8 value with whatever
 *     stale files are still on disk;
 *   - leave the files themselves in place, so a downgrade still finds them;
 *   - do nothing gracefully when the directory is absent.
 */

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileTree.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

#include "config/configkeys.h"
#include "config/configmigration.h"

using namespace PlasmaZones;

namespace {

QString profilesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/profiles");
}

/// @return false when the fixture could not be written.
///
/// Returns rather than QVERIFYing: QVERIFY expands to a `return` from the
/// function it appears in, so failing inside a void helper abandons the HELPER
/// and lets the slot carry on against a file that was never created — the real
/// failure then surfaces as a confusing assertion further down. Callers
/// QVERIFY the result.
[[nodiscard]] bool writeProfileFile(const QString& stem, const QJsonObject& body)
{
    if (!QDir().mkpath(profilesDir())) {
        return false;
    }
    QFile f(profilesDir() + QLatin1Char('/') + stem + QStringLiteral(".json"));
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(body).toJson();
    const bool ok = f.write(payload) == payload.size();
    f.close();
    return ok;
}

/// Plant a file whose bytes are not JSON at all.
[[nodiscard]] bool writeRawProfileFile(const QString& stem, const QByteArray& bytes)
{
    if (!QDir().mkpath(profilesDir())) {
        return false;
    }
    QFile f(profilesDir() + QLatin1Char('/') + stem + QStringLiteral(".json"));
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    return ok;
}

/// Whether an entry for @p path exists at all, regardless of what it carries.
///
/// Distinct from `entryFor`, which returns the entry's `profile` and so cannot
/// tell "no entry" from "an entry with an empty profile". Any assertion about
/// a path being SKIPPED has to use this one.
bool hasEntryFor(const QJsonObject& root, const QString& path)
{
    const QJsonArray overrides = root.value(ConfigKeys::animationsGroup())
                                     .toObject()
                                     .value(ConfigKeys::motionProfileTreeKey())
                                     .toObject()
                                     .value(QStringLiteral("overrides"))
                                     .toArray();
    for (const QJsonValue& v : overrides) {
        if (v.toObject().value(QStringLiteral("path")).toString() == path) {
            return true;
        }
    }
    return false;
}

QJsonObject entryFor(const QJsonObject& root, const QString& path)
{
    const QJsonArray overrides = root.value(ConfigKeys::animationsGroup())
                                     .toObject()
                                     .value(ConfigKeys::motionProfileTreeKey())
                                     .toObject()
                                     .value(QStringLiteral("overrides"))
                                     .toArray();
    for (const QJsonValue& v : overrides) {
        if (v.toObject().value(QStringLiteral("path")).toString() == path) {
            return v.toObject().value(QStringLiteral("profile")).toObject();
        }
    }
    return {};
}

} // namespace

class TestMigrationV7ToV8 : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        // Each slot owns the directory outright: these tests write into the
        // qttest data location, and a file left by a previous slot would leak
        // into the next one's tree.
        QDir(profilesDir()).removeRecursively();
    }

    void cleanupTestCase()
    {
        QDir(profilesDir()).removeRecursively();
    }

    void overrideFilesFoldIntoTheConfigTree()
    {
        QVERIFY(writeProfileFile(QStringLiteral("window.appearance.open"),
                                 QJsonObject{{QStringLiteral("name"), QStringLiteral("window.appearance.open")},
                                             {QStringLiteral("duration"), 280},
                                             {QStringLiteral("curve"), QStringLiteral("ink-settle")}}));

        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root);

        // The runner aborts the chain when a step returns without bumping the
        // version, so every exit path has to stamp — including the early ones
        // this file's other slots take.
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 8);

        const QJsonObject profile = entryFor(root, QStringLiteral("window.appearance.open"));
        QVERIFY2(!profile.isEmpty(), "the override file did not reach the config tree");
        QCOMPARE(profile.value(QStringLiteral("duration")).toInt(), 280);
        QCOMPARE(profile.value(QStringLiteral("curve")).toString(), QStringLiteral("ink-settle"));
        // The envelope's `name` is the entry's path and must not survive inside
        // the profile, where it is not a Profile field.
        QVERIFY(!profile.contains(QStringLiteral("name")));

        // The file is READ, not consumed: a downgrade has to still find it.
        QVERIFY(QFile::exists(profilesDir() + QStringLiteral("/window.appearance.open.json")));
    }

    /// A user preset shares the directory and is named by the user, so its
    /// `name` is not an event path. Migrating one would invent an override for
    /// a path that does not exist.
    void userPresetsAreLeftAlone()
    {
        QVERIFY(writeProfileFile(
            QStringLiteral("My Preset"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("My Preset")}, {QStringLiteral("duration"), 400}}));
        QVERIFY(writeProfileFile(
            QStringLiteral("osd.show"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.show")}, {QStringLiteral("duration"), 220}}));

        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root);

        QVERIFY(!entryFor(root, QStringLiteral("osd.show")).isEmpty());
        QVERIFY2(entryFor(root, QStringLiteral("My Preset")).isEmpty(),
                 "a user preset was migrated as if it were an event override");
        const QJsonArray overrides = root.value(ConfigKeys::animationsGroup())
                                         .toObject()
                                         .value(ConfigKeys::motionProfileTreeKey())
                                         .toObject()
                                         .value(QStringLiteral("overrides"))
                                         .toArray();
        QCOMPARE(overrides.size(), 1);
        QVERIFY(QFile::exists(profilesDir() + QStringLiteral("/My Preset.json")));
    }

    /// Re-running the chain over a config that already carries the key must not
    /// clobber it with whatever files remain on disk.
    void anExistingTreeIsNeverOverwritten()
    {
        QVERIFY(writeProfileFile(
            QStringLiteral("osd.show"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.show")}, {QStringLiteral("duration"), 999}}));

        QJsonObject existing{{QStringLiteral("baseline"), QJsonObject{}}, {QStringLiteral("overrides"), QJsonArray{}}};
        QJsonObject root;
        root.insert(ConfigKeys::animationsGroup(), QJsonObject{{ConfigKeys::motionProfileTreeKey(), existing}});

        ConfigMigration::migrateV7ToV8(root);

        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 8);
        QVERIFY2(entryFor(root, QStringLiteral("osd.show")).isEmpty(),
                 "a v8 config was clobbered by stale override files");
    }

    void anAbsentDirectoryIsHandled()
    {
        QDir(profilesDir()).removeRecursively();
        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root); // must not crash
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 8);
        QVERIFY(!root.value(ConfigKeys::animationsGroup()).toObject().contains(ConfigKeys::motionProfileTreeKey()));
    }

    /// An override file that overrides nothing leaves no entry: an empty
    /// profile is a no-op the tree should not carry.
    void anEmptyOverrideIsSkipped()
    {
        QVERIFY(writeProfileFile(QStringLiteral("osd.hide"),
                                 QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.hide")}}));

        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root);

        // Asserted as the ABSENCE OF AN ENTRY, not through entryFor: that
        // helper answers with the entry's `profile` object, so an entry that
        // was appended carrying an empty profile reads identically to no entry
        // at all. Written the old way this slot could not fail — deleting the
        // skip left it green.
        QVERIFY(!hasEntryFor(root, QStringLiteral("osd.hide")));
    }

    /// A hand-edited file cannot smuggle arbitrary keys or unbounded strings
    /// into config.
    ///
    /// The profiles directory is a filesystem boundary, and everything that
    /// survives the import is written verbatim into one shared config key that
    /// every read of the timing tree copies. `Profile::fromJson` ignores keys
    /// it does not know, so without the allowlist a stray field would be
    /// invisible rather than harmless — and nothing downstream prunes it.
    void ahandEditedFileCannotSmuggleFieldsIntoConfig()
    {
        const QString overlong(4096, QLatin1Char('x'));
        QVERIFY(writeProfileFile(QStringLiteral("osd.show"),
                                 QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.show")},
                                             {QStringLiteral("duration"), 250},
                                             {QStringLiteral("bogusKey"), QStringLiteral("nope")},
                                             {QStringLiteral("presetName"), overlong}}));

        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root);

        const QJsonObject stored = entryFor(root, QStringLiteral("osd.show"));
        QCOMPARE(stored.value(QStringLiteral("duration")).toInt(), 250);
        QVERIFY2(!stored.contains(QStringLiteral("bogusKey")), "an unrecognised field reached the config tree");
        QVERIFY2(!stored.contains(QStringLiteral("presetName")), "an over-long string value reached the config tree");
    }

    /// A settings-profile delta must be stamped but NOT have this machine's
    /// files folded into it.
    ///
    /// ProfileStore passes `ExternalImports::Disabled` precisely so a document
    /// that never carried the migrating user's timings does not acquire them.
    /// Nothing exercised that arm, so deleting the early return left the whole
    /// suite green while every captured profile silently gained local timing.
    void aProfileDeltaIsStampedButNotImported()
    {
        QVERIFY(writeProfileFile(
            QStringLiteral("osd.show"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.show")}, {QStringLiteral("duration"), 250}}));

        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root, /*importOverrideFiles=*/false);

        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 8);
        QVERIFY(!root.value(ConfigKeys::animationsGroup()).toObject().contains(ConfigKeys::motionProfileTreeKey()));
    }

    /// Re-running the step on an already-migrated root must not re-import.
    ///
    /// Distinct from `anExistingTreeIsNeverOverwritten`, which enters at v7 and
    /// stops at the key-present check. This one pins the version guard itself:
    /// without it a later chain run would fold the (deliberately preserved)
    /// files back in on top of whatever the user has since configured.
    /// One unreadable file must not cost the user the others.
    ///
    /// The loop skips a file it cannot parse and carries on, which is the right
    /// call — aborting would trade one lost override for all of them. That
    /// choice is worth pinning because the failure is PERMANENT in a way the
    /// others are not: `_version` is stamped before the loop, so a later run
    /// short-circuits, and since schema v8 nothing rescans the directory. The
    /// skipped file stays on disk but is never read again.
    void aMalformedFileDoesNotAbortTheRest()
    {
        QVERIFY(writeRawProfileFile(QStringLiteral("osd.show"), QByteArrayLiteral("{ this is not json")));
        QVERIFY(writeProfileFile(
            QStringLiteral("osd.hide"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.hide")}, {QStringLiteral("duration"), 190}}));

        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root);

        QVERIFY2(!hasEntryFor(root, QStringLiteral("osd.show")), "an unparseable file must not produce an entry");
        QCOMPARE(entryFor(root, QStringLiteral("osd.hide")).value(QStringLiteral("duration")).toInt(), 190);
    }

    /// A file over the size cap is skipped, and its neighbours still land.
    void anOversizeFileIsSkipped()
    {
        QJsonObject big{{QStringLiteral("name"), QStringLiteral("osd.show")}, {QStringLiteral("duration"), 200}};
        // Comfortably past the 4 MiB cap, in a field the allowlist would drop
        // anyway — the cap has to fire before any of that matters.
        big.insert(QStringLiteral("filler"), QString(5 * 1024 * 1024, QLatin1Char('x')));
        QVERIFY(writeProfileFile(QStringLiteral("osd.show"), big));
        QVERIFY(writeProfileFile(
            QStringLiteral("osd.pop"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.pop")}, {QStringLiteral("duration"), 240}}));

        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root);

        QVERIFY2(!hasEntryFor(root, QStringLiteral("osd.show")), "a file over the cap must not produce an entry");
        QCOMPARE(entryFor(root, QStringLiteral("osd.pop")).value(QStringLiteral("duration")).toInt(), 240);
    }

    /// A file whose `name` disagrees with its stem was INERT under v7, whose
    /// loader required the two to match. Importing it on `name` alone would
    /// take something that had no effect and make it an active override at
    /// upgrade time, which is a behaviour change the user never asked for.
    void aFileWhoseNameDisagreesWithItsStemIsIgnored()
    {
        QVERIFY(writeProfileFile(
            QStringLiteral("some-preset-name"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.show")}, {QStringLiteral("duration"), 900}}));

        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root);

        QVERIFY2(!hasEntryFor(root, QStringLiteral("osd.show")),
                 "a file whose name disagrees with its stem was inert and must stay inert");
    }

    /// What the migration writes must be what the runtime can read.
    ///
    /// Every other slot here asserts on the JSON shape, which cannot catch a
    /// migration that produces a well-formed tree the resolver then reads
    /// differently. This one takes the exact output through the same
    /// ProfileTree parse the daemon performs and asserts the resolved value.
    void theMigratedTreeResolvesBackToWhatTheFileHeld()
    {
        QVERIFY(writeProfileFile(QStringLiteral("osd.show"),
                                 QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.show")},
                                             {QStringLiteral("duration"), 275},
                                             {QStringLiteral("curve"), QStringLiteral("0.33,1,0.68,1")}}));

        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root);

        const QJsonObject treeJson =
            root.value(ConfigKeys::animationsGroup()).toObject().value(ConfigKeys::motionProfileTreeKey()).toObject();
        PhosphorAnimation::CurveRegistry curves;
        const auto tree = PhosphorAnimation::ProfileTree::fromJson(treeJson, curves);
        const auto resolved = tree.directOverride(QStringLiteral("osd.show"));
        QCOMPARE(resolved.duration.value_or(0.0), 275.0);
        QVERIFY2(resolved.curve != nullptr, "a bezier spec the migration copied verbatim must still parse");
    }

    void anAlreadyMigratedRootIsLeftAlone()
    {
        QVERIFY(writeProfileFile(
            QStringLiteral("osd.show"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.show")}, {QStringLiteral("duration"), 250}}));

        QJsonObject root;
        root.insert(QStringLiteral("_version"), 9);

        ConfigMigration::migrateV7ToV8(root);

        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 9);
        QVERIFY(!root.contains(ConfigKeys::animationsGroup()));
    }
};

QTEST_MAIN(TestMigrationV7ToV8)
#include "test_migration_v7_to_v8.moc"
