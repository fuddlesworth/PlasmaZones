// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configkeys.h"
#include "configmigration_util.h"
#include "core/platform/logging.h"

#include <PhosphorAnimation/ProfilePaths.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLatin1String>
#include <QSet>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

namespace {

/// Ceiling on one override file read during the migration. The profiles dir is
/// hand-editable, so it is a filesystem boundary like any other; the same cap
/// the motion-set snapshot and the preset library apply.
constexpr qint64 kMaxOverrideFileBytes = 4 * 1024 * 1024;

/// The built-in event paths, as a set. A file whose `name` is one of these is a
/// per-event OVERRIDE and migrates; anything else in the same directory is a
/// user PRESET (named by the user, saved from the curve editor) and must be
/// left exactly where it is.
const QSet<QString>& eventPaths()
{
    static const QSet<QString> paths = [] {
        const QStringList list = PhosphorAnimation::ProfilePaths::allBuiltInPaths();
        return QSet<QString>(list.cbegin(), list.cend());
    }();
    return paths;
}

QString userProfilesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/profiles");
}

} // namespace

// ── v7 → v8: per-event motion overrides move from loose files into config ────
//
// See the declaration in configmigration.h for why. In short: timing was the
// last part of an animation kept outside config, which is what made a settings
// profile capture an event's pack but not its timing.
//
// The files are READ, not moved or deleted. Leaving them costs a few kilobytes
// and buys two things: a downgrade to v7 still finds its overrides, and the
// preset files that share the directory are never at risk from a bulk delete.
// The v8 code paths read config and ignore them.
void ConfigMigration::migrateV7ToV8(QJsonObject& root, bool importOverrideFiles)
{
    // Defense-in-depth idempotency guard, mirroring the earlier steps.
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 8) {
        return;
    }
    // Stamped up front rather than at each exit. Every path below is a valid
    // completion — an absent profiles directory, no override files, a config
    // that already carries the key — and the runner ABORTS the whole chain if
    // a step returns without bumping the version, so an early return that
    // forgot to stamp would stall every later migration too.
    root[ConfigKeys::versionKey()] = 8;

    if (!importOverrideFiles) {
        // A settings-profile delta. It carries only the keys that profile
        // changes, and importing this machine's override files into it would
        // write the migrating user's own timings into a document that never
        // held them. The stamp above is all this path needs.
        return;
    }

    const QString groupName = ConfigKeys::animationsGroup();
    const QString keyName = ConfigKeys::motionProfileTreeKey();

    QJsonObject animations = root.value(groupName).toObject();
    // Never overwrite an existing value. A config already carrying the key was
    // written by a v8 build, and a re-run of the chain (or a hand-restored
    // file) must not clobber it with whatever stale files remain on disk.
    if (animations.contains(keyName)) {
        return;
    }

    const QString dirPath = userProfilesDir();
    QDir dir(dirPath);
    if (!dir.exists()) {
        return; // nothing to migrate; the schema default (empty tree) applies
    }

    // ProfileTree's own serialized shape: `{ baseline, overrides: [{path, profile}] }`.
    // Built by hand rather than through ProfileTree, because parsing a Profile
    // needs a CurveRegistry and this migration has no business constructing one
    // — the stored curve is a string spec that round-trips verbatim.
    QJsonArray overrides;
    const auto files = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        if (!info.isFile() || info.size() > kMaxOverrideFileBytes) {
            qCWarning(lcConfig) << "migrateV7ToV8: skipping" << info.absoluteFilePath()
                                << "— not a regular file, or over the" << kMaxOverrideFileBytes << "byte cap";
            continue;
        }
        QFile f(info.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            qCWarning(lcConfig) << "migrateV7ToV8: cannot open" << info.absoluteFilePath();
            continue;
        }
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcConfig) << "migrateV7ToV8: failed to parse" << info.absoluteFilePath() << ":"
                                << err.errorString();
            continue;
        }

        QJsonObject profile = doc.object();
        // The envelope's `name` IS the event path, and the loader requires it to
        // equal the file stem. A name that is not a built-in event path is a
        // user preset sharing the directory — leave it be.
        const QString name = profile.take(QLatin1String("name")).toString();
        if (!eventPaths().contains(name)) {
            continue;
        }
        if (profile.isEmpty()) {
            continue; // an override that overrides nothing
        }

        QJsonObject entry;
        entry.insert(QLatin1String("path"), name);
        entry.insert(QLatin1String("profile"), profile);
        overrides.append(entry);
    }

    if (overrides.isEmpty()) {
        return; // the schema default (empty tree) is already the right answer
    }

    QJsonObject tree;
    // An empty baseline, matching ProfileTree::toJson on a tree whose baseline
    // was never set. The per-event files never carried one: the global profile
    // is its own config key (Animations/Profile), not this tree's baseline.
    tree.insert(QLatin1String("baseline"), QJsonObject{});
    tree.insert(QLatin1String("overrides"), overrides);

    animations.insert(keyName, tree);
    root.insert(groupName, animations);

    qCInfo(lcConfig) << "migrateV7ToV8: migrated" << overrides.size() << "per-event motion overrides from" << dirPath
                     << "into config";
}

} // namespace PlasmaZones
