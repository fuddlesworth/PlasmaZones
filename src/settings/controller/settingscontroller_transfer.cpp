// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Settings transfer for SettingsController: whole-config export/import (JSON
// dump and restore, legacy INI conversion, schema-version and rule-store
// guards, backup/restore discipline) and the KZones import wrappers.
//
// Split out of settingscontroller_session.cpp, which sits at its size
// ceiling; same class, separate translation unit, no API change.

#include "settingscontroller.h"

#include "config/configdefaults.h"
#include "config/configmigration.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include "phosphor_i18n.h"
#include "settings/utils/dbusutils.h"
#include "settings/utils/kzonesimporter.h"
#include <PhosphorRules/RuleSet.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {
namespace {

/// Whether an imported settings blob is one this build can migrate FORWARD to
/// the current schema.
///
/// The migration chain cannot answer this on its own: it reports success both
/// for a blob already at the target and for one stamped ABOVE it (the loop
/// breaks on `readVersion >= target` and an unchanged version returns true).
/// A newer blob accepted as "migrated" then reads as every moved key absent,
/// load() substitutes schema defaults, and the first Save stamps this version
/// over the user's settings — with the import backup already removed.
///
/// An ABSENT version stamp is treated as upgradable: that is a pre-versioning
/// (v1-era) config, exactly what the chain exists to lift.
bool importedBlobIsUpgradable(const QJsonObject& root)
{
    const QJsonValue version = root.value(QLatin1String("_version"));
    if (!version.isDouble()) {
        return true;
    }
    return version.toInt() <= PlasmaZones::ConfigSchemaVersion;
}

/// The first schema version whose config lives alongside a rules.json store. A
/// blob stamped below this still carries its zone assignments, disable lists and
/// exclusions inside config.json.
constexpr int FirstRuleStoreSchemaVersion = 4;

/// Whether an imported settings blob predates the v4 rule-store conversion. An
/// absent version stamp is a pre-versioning (v1-era) config, older than v4 too.
bool importedBlobIsPreV4(const QJsonObject& root)
{
    const QJsonValue version = root.value(QLatin1String("_version"));
    return !version.isDouble() || version.toInt() < FirstRuleStoreSchemaVersion;
}

/// Whether a valid rules.json is already on disk for this profile. See the
/// pre-v4 refusal in importAllSettings for why that combination is data loss.
bool haveValidRuleStore()
{
    // Non-empty, not merely parseable: an empty rules.json has nothing an
    // import could drop, and refusing the import over it would send the
    // user to a fresh profile for no reason.
    const auto rules = PhosphorRules::RuleSet::loadFromFile(PlasmaZones::ConfigDefaults::rulesFilePath());
    return rules.has_value() && rules->count() > 0;
}

/// Canonical identity of @p info for same-file comparison.
///
/// canonicalFilePath() is empty for a file that does not exist yet, which on a
/// fresh profile is exactly the live config path, and two empty strings compare
/// equal. Resolving the PARENT directory and rejoining the filename answers that
/// case and catches `<symlink-to-config-dir>/config.json` too. The cleaned
/// lexical path is the last resort, for when the parent is missing as well.
QString canonicalIdentity(const QFileInfo& info)
{
    if (info.exists()) {
        return info.canonicalFilePath();
    }
    const QString parentCanonical = info.dir().canonicalPath();
    if (parentCanonical.isEmpty()) {
        return QDir::cleanPath(info.filePath());
    }
    return parentCanonical + QLatin1Char('/') + info.fileName();
}

} // namespace

namespace {

// Drop a leading UTF-8 BOM from @p bytes. Qt's JSON parser treats a BOM as a
// syntax error rather than skipping it, so any bytes handed to
// QJsonDocument::fromJson have to come through here first. Shared by the import
// probe's head sniff and its whole-file parse so the two agree on where the
// content starts.
QByteArray withoutUtf8Bom(const QByteArray& bytes)
{
    static const QByteArray kUtf8Bom("\xEF\xBB\xBF", 3);
    return bytes.startsWith(kUtf8Bom) ? bytes.mid(kUtf8Bom.size()) : bytes;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Config export/import
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::exportAllSettings(const QString& filePath)
{
    // Reachable from QML: urlToLocalFile yields an empty string for a URL that
    // is not a local file, so a remote destination lands here rather than being
    // a programmer error.
    if (filePath.isEmpty()) {
        Q_EMIT settingsTransferFailed(PhosphorI18n::tr("Settings can only be exported to a local file."));
        return false;
    }
    // Defence-in-depth: same sanitiser the per-layout import/export uses.
    // A QML or D-Bus caller passing a path with `..` traversal segments,
    // an embedded NUL, or a leading `~` is treated as a programmer error.
    const QString safeFilePath = Utils::sanitizeIOPath(filePath);
    if (safeFilePath.isEmpty()) {
        qCWarning(lcCore) << "exportAllSettings: refusing unsafe path" << filePath;
        Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That export path is not allowed."));
        return false;
    }
    const QString configPath = PlasmaZones::ConfigDefaults::configFilePath();
    // Exporting onto the live config is refused: the write below would put the
    // current settings into the file the app is using, which is an implicit
    // Save of whatever the user has pending, and it would leave that file
    // disagreeing with the baseline per-page Discard reverts to. The file
    // picker can reach the config directory. canonicalIdentity resolves through
    // the parent directory for a file that is not on disk yet, so a symlink, a
    // `.` segment, or a fresh profile's not-yet-written config all compare
    // correctly.
    if (canonicalIdentity(QFileInfo(safeFilePath)) == canonicalIdentity(QFileInfo(configPath))) {
        qCWarning(PlasmaZones::lcCore) << "exportAllSettings: destination is the live config file, refusing"
                                       << safeFilePath;
        Q_EMIT settingsTransferFailed(
            PhosphorI18n::tr("That is the settings file this app is using. Export to a different file."));
        return false;
    }
    // Serialize the current settings straight to the destination rather than
    // copying the live config. The old form flushed with Settings::save()
    // first, so that the export carried what the user could see rather than the
    // last-saved snapshot — but save() commits to ~/.config and re-baselines,
    // so pressing Export persisted edits the user had never chosen to save and
    // left a later per-page Discard with nothing to revert to. exportTo writes
    // the same values (the setters already keep the backend's in-memory root
    // current) without touching the live file or the baseline, and it writes
    // atomically, so a failed export leaves whatever was at the destination
    // alone instead of unlinking it up front to make room.
    if (!m_settings.exportTo(safeFilePath)) {
        qCWarning(PlasmaZones::lcCore) << "Failed to export settings to:" << safeFilePath;
        Q_EMIT settingsTransferFailed(
            PhosphorI18n::tr("Could not write the export. Check that the folder is writable."));
        return false;
    }
    return true;
}

bool SettingsController::importAllSettings(const QString& filePath)
{
    // Same as the export side: an empty path here is a URL that is not a local
    // file, which a user can pick.
    if (filePath.isEmpty()) {
        Q_EMIT settingsTransferFailed(PhosphorI18n::tr("Settings can only be imported from a local file."));
        return false;
    }
    // Split rather than combined: a refused path and a file that is not there
    // are the same `false` to a caller but different words to a user, and the
    // log wants them apart too.
    const QString safeFilePath = Utils::sanitizeIOPath(filePath);
    if (safeFilePath.isEmpty()) {
        qCWarning(lcCore) << "importAllSettings: refusing unsafe path" << filePath;
        Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That file path is not allowed."));
        return false;
    }
    if (!QFile::exists(safeFilePath)) {
        qCWarning(lcCore) << "importAllSettings: file not found" << filePath;
        Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That settings file is no longer there."));
        return false;
    }

    const QString configPath = PlasmaZones::ConfigDefaults::configFilePath();

    // Importing the live config onto itself is a no-op, but the copy below
    // removes the destination first and would then find its source gone. The
    // backup restores it, so nothing is lost, yet the user is told the import
    // failed when there was simply nothing to do. Refuse it up front instead.
    // Same canonicalIdentity comparison the export side uses, so a source
    // reached through a symlinked config directory is caught here too.
    if (canonicalIdentity(QFileInfo(safeFilePath)) == canonicalIdentity(QFileInfo(configPath))) {
        qCWarning(PlasmaZones::lcCore) << "importAllSettings: source is the live config file, refusing" << safeFilePath;
        Q_EMIT settingsTransferFailed(
            PhosphorI18n::tr("Those are the settings this app is already using. Pick a different file to import."));
        return false;
    }

    // Detect if the imported file is legacy INI format (not JSON).
    // If so, run the migration converter to produce a JSON file.
    bool isLegacyIni = false;
    {
        QFile f(safeFilePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // Read enough bytes to find the first non-whitespace character.
            // JSON files start with '{' (or '[' for arrays, though config is always an object).
            QByteArray head = f.peek(256);
            // Reject non-UTF-8 BOMs explicitly — a UTF-16/UTF-32 file would
            // not be valid JSON for our config format AND is not legacy INI
            // either; misclassifying it as INI would corrupt data on
            // migration. Caller gets the same "not legacy" path which then
            // fails JSON validation and aborts cleanly.
            const auto byte = [&head](int i) {
                return static_cast<unsigned char>(head.at(i));
            };
            if (head.size() >= 4 && byte(0) == 0x00 && byte(1) == 0x00 && byte(2) == 0xFE && byte(3) == 0xFF) {
                qCWarning(PlasmaZones::lcCore) << "Import file has UTF-32 BE BOM, refusing:" << filePath;
                Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That is not a settings file this app can read."));
                return false;
            }
            if (head.size() >= 4 && byte(0) == 0xFF && byte(1) == 0xFE && byte(2) == 0x00 && byte(3) == 0x00) {
                qCWarning(PlasmaZones::lcCore) << "Import file has UTF-32 LE BOM, refusing:" << filePath;
                Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That is not a settings file this app can read."));
                return false;
            }
            if (head.size() >= 2 && ((byte(0) == 0xFE && byte(1) == 0xFF) || (byte(0) == 0xFF && byte(1) == 0xFE))) {
                qCWarning(PlasmaZones::lcCore) << "Import file has UTF-16 BOM, refusing:" << filePath;
                Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That is not a settings file this app can read."));
                return false;
            }
            // Skip UTF-8 BOM (EF BB BF) if present — trimmed() only strips ASCII whitespace.
            head = withoutUtf8Bom(head.trimmed()).trimmed();
            // A leading '[' is ambiguous: a legacy INI file starts with its
            // first "[Group]" header, but so does a JSON array. The array is
            // valid JSON yet never a settings file (the config root is always
            // an object), and feeding it to the INI migrator would produce a
            // misleading "older settings file" error. Disambiguate by parsing
            // the whole file: valid JSON here means it is not INI, so refuse
            // it with the accurate message; a parse failure means a real INI
            // section header and the migration path proceeds as before.
            if (!head.isEmpty() && head.at(0) == '[') {
                // Parse the body under the SAME normalization `head` got. peek()
                // left the read position at 0, so readAll() re-reads the BOM that
                // was stripped from `head` above, and Qt's JSON parser rejects a
                // leading BOM outright — a BOM'd JSON array would look like a
                // parse failure, fall through to isLegacyIni, and earn exactly the
                // misleading "older settings file" error this branch exists to
                // prevent.
                if (!QJsonDocument::fromJson(withoutUtf8Bom(f.readAll().trimmed())).isNull()) {
                    qCWarning(PlasmaZones::lcCore)
                        << "Import file is a JSON array, not a settings object, refusing:" << filePath;
                    Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That is not a settings file this app can read."));
                    return false;
                }
            }
            isLegacyIni = !head.isEmpty() && head.at(0) != '{';
        }
    }

    // Whether this profile had settings at all before the import. A fresh
    // profile has nothing to back up, so a later failure has no backup to
    // restore — and whatever this import wrote would stay behind as the live
    // config. The failure path below uses this to clean that up.
    const bool hadExistingConfig = QFile::exists(configPath);

    // Backup current config
    const QString backupPath = configPath + QStringLiteral(".bak");
    if (QFile::exists(backupPath)) {
        QFile::remove(backupPath);
    }
    if (QFile::exists(configPath) && !QFile::copy(configPath, backupPath)) {
        qCWarning(PlasmaZones::lcCore) << "Failed to backup config to:" << backupPath;
        Q_EMIT settingsTransferFailed(
            PhosphorI18n::tr("Could not back up your current settings, so nothing was imported."));
        return false;
    }

    bool ok = false;
    // Whether any arm below actually wrote configPath. The pre-write
    // refusals (unreadable file, newer schema, pre-v4 over live rules) leave
    // the live config untouched, and the restore block must not remove and
    // re-rename a file nothing damaged — that round trip turns a refusal
    // into a window where the config exists only as the .bak.
    bool configWritten = false;
    if (isLegacyIni) {
        // A legacy INI converts to v1 JSON, so it is always older than the
        // rule-store conversion and carries the data-loss risk the JSON arm
        // below spells out. Refuse for the same reason, in the same words.
        if (haveValidRuleStore()) {
            qCWarning(PlasmaZones::lcCore)
                << "importAllSettings: refusing a legacy INI import over an existing rules.json:" << safeFilePath;
            Q_EMIT settingsTransferFailed(
                PhosphorI18n::tr("That settings file is older than the window rules this profile already has, and "
                                 "importing it would drop them. Set up a new settings profile and import it there."));
            ok = false;
        } else {
            // Convert INI to JSON in-place using the migration module
            if (QFile::exists(configPath)) {
                QFile::remove(configPath);
            }
            configWritten = true;
            // finalizeV4Conversion is PAIRED with the conversion here as at every
            // other call site. migrateIniToJson runs the chain in-memory, and its
            // v3→v4 step only STASHES the exclusion, assignment and animation
            // data under _v4* root keys. The finalize step is what turns those
            // into rules.json and quicklayouts.json and strips the scratch keys.
            // Unpaired, the stashes sit orphaned in config.json and the next
            // launch strips them unported, dropping every imported assignment.
            ok = PlasmaZones::ConfigMigration::migrateIniToJson(safeFilePath, configPath)
                && PlasmaZones::ConfigMigration::finalizeV4Conversion(configPath);
            if (!ok) {
                qCWarning(PlasmaZones::lcCore) << "Failed to convert legacy INI file:" << safeFilePath;
                Q_EMIT settingsTransferFailed(PhosphorI18n::tr("Could not read that older settings file."));
            }
        }
    } else {
        // Validate JSON before overwriting current config
        QFile importFile(safeFilePath);
        if (!importFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCWarning(PlasmaZones::lcCore) << "Failed to open import file:" << safeFilePath;
            Q_EMIT settingsTransferFailed(PhosphorI18n::tr("Could not read that settings file."));
            ok = false;
        } else {
            QJsonParseError parseErr;
            // Strip a leading UTF-8 BOM, the same normalization the head sniff
            // above applied: Qt's parser calls a BOM a syntax error, so a BOM'd
            // but perfectly good settings file would be rejected as unreadable.
            // These stripped bytes are what gets written to configPath below, so
            // the file the app then loads parses for the same reason.
            const QByteArray importBytes = withoutUtf8Bom(importFile.readAll());
            QJsonDocument importDoc = QJsonDocument::fromJson(importBytes, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !importDoc.isObject()) {
                qCWarning(PlasmaZones::lcCore)
                    << "Invalid JSON in import file:" << safeFilePath << parseErr.errorString();
                Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That is not a settings file this app can read."));
                ok = false;
            } else if (!importedBlobIsUpgradable(importDoc.object())) {
                // A blob stamped NEWER than this build cannot be migrated down,
                // and runMigrationChain reports "success" for it (readVersion >=
                // target breaks the loop immediately and an unchanged version
                // returns true), so the chain result alone cannot detect it.
                // Unchecked, load() reads every moved key as absent and takes
                // schema defaults, and the first Save stamps this version over
                // them. Checked BEFORE anything is written: on a fresh profile
                // there is no backup, so a rejection after the write left the
                // newer blob in place as the live config with nothing to restore.
                qCWarning(PlasmaZones::lcCore) << "Imported settings are from a newer schema:" << safeFilePath;
                Q_EMIT settingsTransferFailed(
                    PhosphorI18n::tr("That settings file is from a newer version of this app."));
                ok = false;
            } else if (importedBlobIsPreV4(importDoc.object()) && haveValidRuleStore()) {
                // A pre-v4 blob still carries its disable lists, exclusions and
                // zone assignments inside config.json, and the v3→v4 step only
                // stashes them under _v4* keys for finalizeV4Conversion to port.
                // The finalizer ports a stash only when it has to BUILD
                // rules.json; with a valid store already on disk it takes its
                // cleanup-only branch, strips the stashes unread, and reports
                // success. The import would look like it worked while quietly
                // discarding everything the blob held in those groups. There is
                // no merge of two rule sets to fall back on, so refuse and point
                // at a fresh profile, where the porting rebuild does run.
                qCWarning(PlasmaZones::lcCore)
                    << "importAllSettings: refusing a pre-v4 blob over an existing rules.json:" << safeFilePath;
                Q_EMIT settingsTransferFailed(PhosphorI18n::tr(
                    "That settings file is older than the window rules this profile already has, and "
                    "importing it would drop them. Set up a new settings profile and import it there."));
                ok = false;
            } else {
                // Write the parsed bytes rather than copying the source file, so
                // a BOM'd import lands as plain JSON the config reader accepts.
                configWritten = true;
                QFile out(configPath);
                ok = out.open(QIODevice::WriteOnly | QIODevice::Truncate)
                    && out.write(importBytes) == static_cast<qint64>(importBytes.size()) && out.flush();
                out.close();
                if (!ok) {
                    qCWarning(PlasmaZones::lcCore) << "Failed to import settings from:" << safeFilePath;
                    // Says what failed, not what the restore below will do:
                    // that runs after this and can fail too.
                    Q_EMIT settingsTransferFailed(PhosphorI18n::tr("Could not replace your settings with that file."));
                } else if (!ConfigMigration::runMigrationChain(configPath)
                           || !ConfigMigration::finalizeV4Conversion(configPath)) {
                    // An imported blob can be ANY older schema version, and
                    // ensureJsonConfig's one-shot latch has long since fired, so
                    // nothing else would migrate it. An unmigrated blob reads as
                    // "every moved key absent", load() takes the schema
                    // defaults, and the first Save stamps the current version
                    // over them with the backup already removed. The finalize
                    // step is paired with the chain for the reason the INI arm
                    // above spells out.
                    //
                    // The blob is parsed, object-validated and version-checked
                    // by this point, so the only way this arm fires is a file
                    // WRITE error (disk full, read-only config dir), which is
                    // what the message names.
                    qCWarning(PlasmaZones::lcCore) << "Imported settings could not be migrated:" << safeFilePath;
                    Q_EMIT settingsTransferFailed(
                        PhosphorI18n::tr("Your settings file was read but the upgrade could not be saved."));
                    ok = false;
                }
            }
        }
    }

    if (!ok && !configWritten) {
        // A refusal that wrote nothing damaged nothing: the live config is
        // exactly as it was, so put the untouched backup copy away and stop.
        QFile::remove(backupPath);
    } else if (!ok) {
        // Restore backup on failure. `QFile::rename` on Qt REFUSES to
        // overwrite an existing destination (regardless of POSIX rename(2)
        // atomicity at the syscall layer), so we must explicitly remove
        // configPath first. In the open-fail / JSON-parse-fail paths above
        // configPath was never touched, so it still exists — without this
        // pre-remove the rename silently fails and the user is left with
        // their original (pre-import) config plus a misleading "Failed to
        // restore" warning while the actual restore-from-backup is
        // unreachable. There's a one-syscall window between remove and
        // rename where configPath doesn't exist; that's preferable to the
        // silent-failure semantics of the previous form.
        if (QFile::exists(backupPath)) {
            // Both arms leave the user without their settings, which the
            // failure reported above does not cover: it says the import
            // failed, not that the restore behind it failed too and the only
            // copy is now the backup. Name the file, since recovering it by
            // hand is the one action left.
            if (QFile::exists(configPath) && !QFile::remove(configPath)) {
                qCWarning(PlasmaZones::lcCore)
                    << "Failed to remove configPath before restore. Backup remains at:" << backupPath;
                Q_EMIT settingsTransferFailed(
                    PhosphorI18n::tr("Your settings could not be put back. A copy is saved at %1.").arg(backupPath));
            } else if (!QFile::rename(backupPath, configPath)) {
                qCWarning(PlasmaZones::lcCore)
                    << "Failed to restore config from backup after failed import. Backup remains at:" << backupPath;
                Q_EMIT settingsTransferFailed(
                    PhosphorI18n::tr("Your settings could not be put back. A copy is saved at %1.").arg(backupPath));
            }
        } else if (!hadExistingConfig && QFile::exists(configPath)) {
            // Fresh profile: nothing to back up, so the only thing at configPath
            // is what this failed import wrote. Leaving it makes a blob this
            // function refused into the app's live settings. Remove it and let
            // the next launch write defaults.
            if (!QFile::remove(configPath)) {
                qCWarning(PlasmaZones::lcCore) << "Failed to remove the refused import left at:" << configPath;
            }
        }
    } else {
        // Clean up backup on success
        QFile::remove(backupPath);
        // The imported files are on disk, so tell the daemon before this process
        // re-reads anything. Both notifications are blocking and both are
        // needed: reloadSettings covers config.json and deliberately does NOT
        // reload the daemon's borrowed rule store, so without the second call
        // the rules revert below pulls the daemon's PRE-import rule set straight
        // back over the imported rules.json.
        DaemonDBus::notifyReload();
        DaemonDBus::notifyRulesReload();
        // Adopting the on-disk state is exactly what a reload deferred by
        // onExternalSettingsChanged() would have done, so drop that flag.
        m_pendingExternalReload = false;
        // Adopt the imported state the way Discard adopts a reverted one.
        // asyncRevertInFlight does NOT count as clean here: on Discard that
        // worker IS the restore, but here it holds snapshots of pre-import
        // content for files the import just rewrote.
        //
        // One asymmetry worth knowing: the rules revert inside is ASYNC, so the
        // success toast is raised before it resolves. A failed re-fetch still
        // surfaces, through the permanent revertFinished listener in
        // settingscontroller.cpp, which re-badges the Rules page rather than
        // reporting through this return value.
        const bool adopted = adoptOnDiskState(/*treatAsyncRevertAsClean=*/false);
        if (!adopted) {
            qCWarning(lcConfig) << "importConfig: animation snapshots are still staged after the revert (a discard is "
                                   "in flight, or a restore failed)";
            // The settings on disk are the imported ones, but the animation
            // page still holds pre-import snapshots. That is a partial result
            // the user has to know about: without this the import reports plain
            // success while the page shows something else.
            Q_EMIT settingsTransferFailed(
                PhosphorI18n::tr("Your settings were imported, but the animation pages still show the old ones. "
                                 "Reopen the settings window to see the imported values."));
            // Report not-fully-landed. The bool's only job is gating the General
            // page's success toast (see the settingsTransferFailed doc), and the
            // toast surface replaces whatever is in flight. Returning true here
            // would let the caller overwrite the reason just emitted, inside the
            // same JS statement, so the user would only ever read "Settings
            // imported" on the one path that exists to say otherwise.
            ok = false;
        } else {
            setNeedsSave(false);
        }
    }
    return ok;
}

// ═══════════════════════════════════════════════════════════════════════════════
// KZones Import — thin wrappers around kzonesimporter.{h,cpp}
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::hasKZonesConfig()
{
    return KZonesImporter::hasKZonesConfig();
}

int SettingsController::importFromKZones()
{
    return finishKZonesImport(KZonesImporter::importFromKwinrc());
}

int SettingsController::importFromKZonesFile(const QString& filePath)
{
    // Defence-in-depth: same sanitiser, and same QFileDialog entry point, as
    // the layout and algorithm imports.
    const QString safe = Utils::sanitizeIOPath(filePath);
    if (safe.isEmpty()) {
        qCWarning(lcCore) << "importFromKZonesFile: refusing unsafe path" << filePath;
        Q_EMIT kzonesImportFinished(0, PhosphorI18n::tr("That file path is not allowed."));
        return 0;
    }
    return finishKZonesImport(KZonesImporter::importFromFile(safe));
}

int SettingsController::finishKZonesImport(const KZonesImporter::ImportResult& result)
{
    if (result.imported > 0) {
        m_pendingSelectLayoutId = result.pendingSelectLayoutId;
        scheduleLayoutLoad();
    }
    Q_EMIT kzonesImportFinished(result.imported, result.message);
    return result.imported;
}

} // namespace PlasmaZones
