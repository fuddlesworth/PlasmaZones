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

void writeProfileFile(const QString& stem, const QJsonObject& body)
{
    QVERIFY(QDir().mkpath(profilesDir()));
    QFile f(profilesDir() + QLatin1Char('/') + stem + QStringLiteral(".json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(body).toJson());
    f.close();
}

/// The migrated entry for @p path, or an empty object when the tree has none.
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
        writeProfileFile(QStringLiteral("window.appearance.open"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("window.appearance.open")},
                                     {QStringLiteral("duration"), 280},
                                     {QStringLiteral("curve"), QStringLiteral("ink-settle")}});

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
        writeProfileFile(
            QStringLiteral("My Preset"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("My Preset")}, {QStringLiteral("duration"), 400}});
        writeProfileFile(
            QStringLiteral("osd.show"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.show")}, {QStringLiteral("duration"), 220}});

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
        writeProfileFile(
            QStringLiteral("osd.show"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.show")}, {QStringLiteral("duration"), 999}});

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
        writeProfileFile(QStringLiteral("osd.hide"), QJsonObject{{QStringLiteral("name"), QStringLiteral("osd.hide")}});

        QJsonObject root;
        ConfigMigration::migrateV7ToV8(root);

        QVERIFY(entryFor(root, QStringLiteral("osd.hide")).isEmpty());
    }
};

QTEST_MAIN(TestMigrationV7ToV8)
#include "test_migration_v7_to_v8.moc"
