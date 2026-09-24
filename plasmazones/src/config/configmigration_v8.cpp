// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configdefaults.h"
#include "configkeys.h"
#include "core/platform/logging.h"

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
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

/// The pre-v8 per-event override directory.
///
/// Derived from `ConfigDefaults::userProfilesSubdir()` rather than spelling the
/// path again: the accessor exists so a rename is one edit, and the settings
/// page's own `userProfilesDir()` already reads through it. Spelled twice, a
/// rename would leave this migration reading an empty directory and silently
/// importing nothing.
///
/// Note this is NOT `ConfigDefaults::profilesDir()`, which is the settings
/// PROFILE store under `~/.config` — a different directory with a confusingly
/// similar name.
///
/// Only the writable location, deliberately. v7's loader also scanned every
/// `plasmazones/profiles` under `XDG_DATA_DIRS`, but a migration folding a
/// system or packager drop-in into a user's own config would make that
/// drop-in permanent and un-updatable. The repo ships none.
QString userProfilesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + ConfigDefaults::userProfilesSubdir();
}

/// Read the pre-v8 per-event override files into a ProfileTree `overrides`
/// array. Empty when the directory is absent or holds nothing importable.
///
/// Factored out of `migrateV7ToV8` so the same import can run from the
/// finalizer below, which covers the ensureJsonConfig exits the version chain
/// never reaches.
QJsonArray readPreV8OverrideFiles()
{
    const QString dirPath = userProfilesDir();
    QDir dir(dirPath);
    if (!dir.exists()) {
        return {}; // nothing to migrate; the schema default (empty tree) applies
    }

    // ProfileTree's own serialized shape: `{ baseline, overrides: [{path, profile}] }`.
    // Built by hand rather than through ProfileTree, because parsing a Profile
    // needs a CurveRegistry and this migration has no business constructing one
    // — the stored curve is a string spec that round-trips verbatim.
    QJsonArray overrides;
    QSet<QString> seenPaths;
    const auto files = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        // Size only: `QDir::Files` has already excluded anything that is not a
        // regular file, so an isFile() check here would be dead and its warning
        // would name a condition that cannot occur.
        if (info.size() > kMaxOverrideFileBytes) {
            qCWarning(lcConfig) << "migrateV7ToV8: skipping" << info.absoluteFilePath() << "— over the"
                                << kMaxOverrideFileBytes << "byte cap";
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
        // The envelope's `name` IS the event path. v7's loader ALSO required it
        // to equal the file stem, and enforcing that here is not pedantry: a
        // file whose name disagreed with its stem was inert under v7, so
        // importing it on `name` alone would take something that had no effect
        // and make it an active override at upgrade time.
        const QString name = profile.take(QLatin1String("name")).toString();
        if (name != info.completeBaseName()) {
            continue;
        }
        if (!eventPaths().contains(name)) {
            // Not a built-in event path. Usually a user PRESET sharing the
            // directory, which must be left exactly where it is.
            //
            // It can also be an override at a path a later taxonomy retired —
            // `window.movement.snapIn` / `.snapOut` / `.maximize`, renamed into
            // placeIn / placeOut by v7. Those have been inert since that rename
            // (nothing renamed the FILE, so the v7 resolver looked up the new
            // path and found nothing), so this drops a value the user has not
            // experienced for a release rather than losing a live one. Say so,
            // because it is still the user's data going away.
            if (name.startsWith(QLatin1String("window.movement."))) {
                qCWarning(lcConfig) << "migrateV7ToV8: dropping" << info.absoluteFilePath()
                                    << "— it names the retired event path" << name
                                    << "and has had no effect since schema v7. The file is left on disk.";
            }
            continue;
        }
        // Two files can carry the same `name` (the stem check above makes that
        // require two directories' worth of collision, but entryInfoList is
        // ordered so it stays deterministic). Emitting both would put a
        // duplicate path in the tree, a shape nothing else writes.
        if (seenPaths.contains(name)) {
            qCWarning(lcConfig) << "migrateV7ToV8: ignoring" << info.absoluteFilePath() << "— the path" << name
                                << "was already migrated from an earlier file";
            continue;
        }
        seenPaths.insert(name);
        // Keep only the fields `Profile` actually round-trips, and bound what
        // a string may carry.
        //
        // The profiles directory is a filesystem boundary a user can hand-edit,
        // and everything that survives here is written verbatim into ONE shared
        // config key that every read of the timing tree copies. The UI write
        // path applies exactly this allowlist and bound (see
        // animationspagecontroller_groupwrites.cpp); without it here a stray key
        // or a 64 KB string entered through the migration door instead and then
        // stayed, since nothing downstream prunes it. `Profile::fromJson`
        // ignoring unknown keys makes that silent rather than harmless.
        static const QSet<QString> kKnownProfileFields = {
            QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve),
            QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration),
            QLatin1String(PhosphorAnimation::Profile::JsonFieldMinDistance),
            QLatin1String(PhosphorAnimation::Profile::JsonFieldSequenceMode),
            QLatin1String(PhosphorAnimation::Profile::JsonFieldStaggerInterval),
            QLatin1String(PhosphorAnimation::Profile::JsonFieldPresetName),
        };
        constexpr int kMaxFieldStringLength = 512;
        QJsonObject accepted;
        for (auto it = profile.constBegin(); it != profile.constEnd(); ++it) {
            if (!kKnownProfileFields.contains(it.key())) {
                qCWarning(lcConfig) << "migrateV7ToV8: dropping unrecognised field" << it.key() << "from" << name;
                continue;
            }
            if (it.value().isString() && it.value().toString().size() > kMaxFieldStringLength) {
                qCWarning(lcConfig) << "migrateV7ToV8: dropping over-long value for" << it.key() << "in" << name;
                continue;
            }
            accepted.insert(it.key(), it.value());
        }
        profile = accepted;

        if (profile.isEmpty()) {
            continue; // an override that overrides nothing
        }

        QJsonObject entry;
        entry.insert(QLatin1String("path"), name);
        entry.insert(QLatin1String("profile"), profile);
        overrides.append(entry);
    }

    return overrides;
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
    //
    // The cost is that a file the loop below cannot read or parse is skipped
    // and never revisited: the stamp is already down, so nothing rescans, and
    // the finalizer only runs when the config carries NO tree at all. That
    // event's timing falls back to inherited. It is not lost though — the
    // import READS, it never deletes — so the file is still sitting in the
    // profiles directory, and clearing the config key makes the finalizer pick
    // the whole directory up again on the next start.
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

    // `toObject()` yields an empty object for a group that is present but not
    // an object, and the write below would then replace it. Nothing valid is
    // lost — such a group is already unreadable to the Store — but say so
    // rather than leaving a silent replacement to be discovered later.
    const QJsonValue existingGroup = root.value(groupName);
    if (!existingGroup.isUndefined() && !existingGroup.isObject()) {
        qCWarning(lcConfig) << "migrateV7ToV8: the" << groupName
                            << "group is not an object — replacing it, its previous content was unreadable";
    }
    QJsonObject animations = existingGroup.toObject();
    // Never overwrite an existing value. A config already carrying the key was
    // written by a v8 build, and a re-run of the chain (or a hand-restored
    // file) must not clobber it with whatever stale files remain on disk.
    if (animations.contains(keyName)) {
        return;
    }

    const QJsonArray overrides = readPreV8OverrideFiles();
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

    qCInfo(lcConfig) << "migrateV7ToV8: migrated" << overrides.size() << "per-event motion overrides into config";
}

// ── The v8 counterpart of finalizeV4Conversion ──────────────────────────────
//
// `ensureJsonConfig` reaches the migration chain only when config.json exists,
// parses, and is stamped below the current version. Three exits never get
// there: a corrupt config with no INI to re-migrate from, a whitespace-only
// one, and a genuine fresh install. Every one of those then lets the Store
// stamp `_version = 8` on the file it writes, so the next launch
// short-circuits and the chain never runs at all.
//
// That is harmless for a step that only rewrites config, which is every step
// before this one. It is not harmless for v8, which IMPORTS external state: a
// user whose config.json was lost or corrupted keeps their
// `plasmazones/profiles` files on disk and silently never gets them back.
//
// The v3→v4 conversion has exactly this shape and solved it exactly this way —
// `finalizeV4Conversion` is called on every exit, not just the chain's — and
// its comment gives the same reason. This is that, for the timing files.
bool ConfigMigration::finalizeV8MotionImport(const QString& jsonPath)
{
    QFile f(jsonPath);
    if (!f.exists()) {
        // A fresh install writes config.json later, from the Store. There is
        // nothing to fold into yet, and creating one here would race that
        // writer for ownership of the version stamp. The import is therefore
        // NOT recovered for a fresh install that has leftover profile files —
        // an unusual combination (the files only exist if a previous install
        // wrote them) and a deliberate limit rather than an oversight.
        return true;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcConfig) << "finalizeV8MotionImport: cannot open" << jsonPath;
        return true; // not fatal; the config itself is still usable
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return true; // a config this cannot read is not this function's problem
    }

    QJsonObject root = doc.object();
    // Only for a config already AT the current version: below it the chain will
    // run the real step, and above it this build has no business writing.
    if (root.value(ConfigKeys::versionKey()).toInt(0) != 8) {
        return true;
    }
    const QString groupName = ConfigKeys::animationsGroup();
    const QString keyName = ConfigKeys::motionProfileTreeKey();
    QJsonObject animations = root.value(groupName).toObject();
    // Same never-overwrite rule the step itself applies: a key already present
    // was written by a v8 build and outranks whatever files remain.
    if (animations.contains(keyName)) {
        return true;
    }

    const QJsonArray overrides = readPreV8OverrideFiles();
    if (overrides.isEmpty()) {
        return true;
    }

    QJsonObject tree;
    tree.insert(QLatin1String("overrides"), overrides);
    animations.insert(keyName, tree);
    root.insert(groupName, animations);

    QSaveFile out(jsonPath);
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(payload) != payload.size()
        || !out.commit()) {
        qCWarning(lcConfig) << "finalizeV8MotionImport: could not write" << jsonPath;
        return true; // the config is intact; the import simply did not land
    }
    qCInfo(lcConfig) << "finalizeV8MotionImport: recovered" << overrides.size()
                     << "per-event motion overrides the migration chain never saw";
    return true;
}

} // namespace PlasmaZones
