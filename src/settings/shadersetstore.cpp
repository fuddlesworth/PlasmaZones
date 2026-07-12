// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shadersetstore.h"
#include "animationfileutils.h"

#include "../core/logging.h"
#include "../phosphor_i18n.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QSaveFile>
#include <QUrl>

#include <cmath>

namespace PlasmaZones {

namespace {

constexpr QLatin1String kNameKey{"name"};
constexpr QLatin1String kDescriptionKey{"description"};
constexpr QLatin1String kVersionKey{"version"};
constexpr QLatin1String kBaselineKey{"baseline"};
constexpr QLatin1String kOverridesKey{"overrides"};
constexpr QLatin1String kPathKey{"path"};
constexpr QLatin1String kProfileKey{"profile"};

/// True when @p root carries a real baseline. An EMPTY `"baseline": {}` is not
/// one: the snapshot side omits an empty baseline (it carries no engaged field),
/// so treating it as real would advertise coverage a set does not have and, on
/// apply, overwrite the user's baseline with an all-inherit profile. Kept here
/// as the single definition both the store and the domain validators use.
bool carriesBaseline(const QJsonObject& root)
{
    return !root.value(kBaselineKey).toObject().isEmpty();
}

/// Root section of a dotted path ("window.appearance.open" → "window").
/// Drives the coverage chips; QML maps the token to a translated label.
QString rootSection(const QString& path)
{
    return path.section(QLatin1Char('.'), 0, 0);
}

/// The set's covered sections, in first-seen (taxonomy) order.
QStringList coverageSections(const QJsonObject& root)
{
    QStringList sections;
    const QJsonArray overrides = root.value(kOverridesKey).toArray();
    for (const QJsonValue& v : overrides) {
        const QString section = rootSection(v.toObject().value(kPathKey).toString());
        if (!section.isEmpty() && !sections.contains(section)) {
            sections.append(section);
        }
    }
    return sections;
}

/// True when every entry @p set carries is already live in @p live with an
/// equal profile. Containment, NOT equality: applying a set MERGES (paths it
/// does not cover keep their current values), so unrelated live overrides
/// must not clear the "active" badge — otherwise a set would fail to light up
/// the instant after the user applied it.
bool payloadContainedIn(const QJsonObject& set, const QJsonObject& live)
{
    const bool hasBaseline = carriesBaseline(set);
    const QJsonArray setOverrides = set.value(kOverridesKey).toArray();
    // An empty set covers nothing; it is never "active".
    if (!hasBaseline && setOverrides.isEmpty()) {
        return false;
    }
    if (hasBaseline && live.value(kBaselineKey).toObject() != set.value(kBaselineKey).toObject()) {
        return false;
    }

    QHash<QString, QJsonObject> liveByPath;
    const QJsonArray liveOverrides = live.value(kOverridesKey).toArray();
    for (const QJsonValue& v : liveOverrides) {
        const QJsonObject entry = v.toObject();
        liveByPath.insert(entry.value(kPathKey).toString(), entry.value(kProfileKey).toObject());
    }

    for (const QJsonValue& v : setOverrides) {
        const QJsonObject entry = v.toObject();
        const auto it = liveByPath.constFind(entry.value(kPathKey).toString());
        if (it == liveByPath.cend() || *it != entry.value(kProfileKey).toObject()) {
            return false;
        }
    }
    return true;
}

} // namespace

ShaderSetStore::ShaderSetStore(Config config, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    // The three domain closures and the directory accessor are the store's
    // reason to exist: a domain that forgets one is a programming error, not a
    // runtime condition. Assert in debug, and note that every call site below
    // still null-checks so a release build refuses cleanly instead of throwing
    // std::bad_function_call.
    Q_ASSERT(m_config.setsDir);
    Q_ASSERT(m_config.snapshot);
    Q_ASSERT(m_config.validate);
    Q_ASSERT(m_config.apply);
}

QString ShaderSetStore::setsDirectory() const
{
    return m_config.setsDir ? m_config.setsDir() : QString();
}

QString ShaderSetStore::setFilePath(const QString& setName) const
{
    const QString dir = setsDirectory();
    if (dir.isEmpty()) {
        return QString(); // an unconfigured store must not resolve to "/<slug>.json"
    }
    return animfileutil::jsonFilePath(dir, animfileutil::slugify(setName));
}

void ShaderSetStore::notifyLiveStateChanged()
{
    // Collapse a burst (one call per restored path during a bulk revert) into
    // a single emission on the next event-loop turn. Each setsChanged costs
    // QML a full availableSets() — a sets-dir walk plus a live-state snapshot
    // — so the per-path emission would put that whole walk on the GUI thread
    // once per path.
    if (m_liveStateNotifyQueued) {
        return;
    }
    m_liveStateNotifyQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_liveStateNotifyQueued = false;
            Q_EMIT setsChanged();
        },
        Qt::QueuedConnection);
}

bool ShaderSetStore::mutationAllowed()
{
    if (!m_config.mutationGuard) {
        return true;
    }
    const QString refusal = m_config.mutationGuard();
    if (refusal.isEmpty()) {
        return true;
    }
    qCWarning(lcConfig) << "ShaderSetStore: mutation blocked:" << refusal;
    Q_EMIT toastRequested(refusal);
    return false;
}

bool ShaderSetStore::snapshotFile(const QString& filePath)
{
    if (!m_config.fileSnapshot) {
        return true; // the domain does not stage set files
    }
    if (m_config.fileSnapshot(filePath)) {
        return true;
    }
    // Writing now would destroy content we failed to capture, and Discard
    // could not restore it. Refuse instead.
    qCWarning(lcConfig) << "ShaderSetStore: refusing to write" << filePath
                        << "— could not capture its pre-edit content";
    Q_EMIT toastRequested(PhosphorI18n::tr("Could not back up the existing set, so it was left untouched."));
    return false;
}

bool ShaderSetStore::readSetFile(const QString& filePath, QJsonObject* out) const
{
    QFile f(filePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        qCWarning(lcConfig) << "ShaderSetStore: cannot open set file:" << filePath;
        return false;
    }
    // importSet hands this a user-chosen path, so cap the read before it
    // happens rather than slurping an arbitrarily large file into memory only
    // for the JSON parser to reject it.
    if (f.size() > kMaxSetFileBytes) {
        qCWarning(lcConfig) << "ShaderSetStore: set file" << filePath << "is" << f.size() << "bytes, over the"
                            << kMaxSetFileBytes << "byte cap — refusing";
        return false;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcConfig) << "ShaderSetStore: failed to parse set file" << filePath << ":" << err.errorString();
        return false;
    }
    *out = doc.object();
    return true;
}

void ShaderSetStore::rollbackSnapshot(const QString& filePath)
{
    if (m_config.snapshotRollback) {
        m_config.snapshotRollback(filePath);
    }
}

void ShaderSetStore::notifyPendingChanges()
{
    if (m_config.fileSnapshot) {
        Q_EMIT pendingChangesChanged();
    }
}

bool ShaderSetStore::writeSetFile(const QString& filePath, const QJsonObject& root)
{
    QSaveFile file(filePath);
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    const bool written =
        file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(payload) == payload.size() && file.commit();
    if (!written) {
        qCWarning(lcConfig) << "ShaderSetStore: could not write" << filePath << ":" << file.errorString();
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not write the set to disk."));
        // snapshotFile() staged this path for Discard, but the write never
        // landed, so the file is untouched. Un-stage it rather than leave the
        // page claiming an unsaved change that does not exist.
        rollbackSnapshot(filePath);
        notifyPendingChanges();
        return false;
    }
    return true;
}

bool ShaderSetStore::versionAccepted(const QJsonObject& root, const QString& context) const
{
    const QJsonValue versionVal = root.value(kVersionKey);
    // A present-but-non-numeric version is malformed. Treat it as unknown
    // (newer) and refuse, rather than reading it as the current format and
    // committing a set this build may not fully understand. A non-integral
    // number is refused for the same reason: QJsonValue::toInt() would hand
    // back the default for it, silently reading "1.5" as the current version.
    if (!versionVal.isUndefined()
        && (!versionVal.isDouble() || versionVal.toDouble() != std::floor(versionVal.toDouble()))) {
        qCWarning(lcConfig) << "ShaderSetStore:" << context << "— version is not a whole number, refusing";
        return false;
    }
    const int version = versionVal.toInt(m_config.formatVersion);
    if (version > m_config.formatVersion) {
        qCWarning(lcConfig) << "ShaderSetStore:" << context << "— set version" << version
                            << "is newer than this build understands (" << m_config.formatVersion << "), refusing";
        return false;
    }
    return true;
}

QString ShaderSetStore::uniqueSetName(const QString& desiredName) const
{
    if (desiredName.isEmpty() || animfileutil::slugify(desiredName).isEmpty()) {
        return QString();
    }
    QString candidate = desiredName;
    for (int suffix = 2; QFile::exists(setFilePath(candidate)); ++suffix) {
        candidate = QStringLiteral("%1 (%2)").arg(desiredName).arg(suffix);
        // Guard against a name whose slug can't grow (pathological input).
        if (suffix > 999) {
            return QString();
        }
    }
    return candidate;
}

QVariantList ShaderSetStore::availableSets() const
{
    QVariantList result;
    const QString dirPath = setsDirectory();
    QDir dir(dirPath);
    if (dirPath.isEmpty() || !dir.exists()) {
        return result;
    }

    // Snapshot live state ONCE for the whole listing — every row's `active`
    // flag is measured against it.
    const QJsonObject live = m_config.snapshot ? m_config.snapshot() : QJsonObject{};

    const auto files = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        QJsonObject root;
        if (!readSetFile(info.absoluteFilePath(), &root)) {
            continue;
        }
        const QStringList sections = coverageSections(root);
        const bool hasBaseline = carriesBaseline(root);

        // A set file whose stored name is missing or unslugifiable would list a
        // row that every mutator then refuses (they all resolve name -> path),
        // leaving the user unable to even delete it. Fall back to the filename,
        // which is by construction a valid slug — the same fallback importSet uses.
        QString rowName = root.value(kNameKey).toString();
        if (rowName.isEmpty() || animfileutil::slugify(rowName).isEmpty()) {
            rowName = info.completeBaseName();
        }

        QVariantMap row;
        row.insert(QLatin1String("name"), rowName);
        row.insert(QLatin1String("description"), root.value(kDescriptionKey).toString());
        row.insert(QLatin1String("slug"), info.completeBaseName());
        row.insert(QLatin1String("coverage"), sections);
        // The baseline counts as one covered surface in the summary.
        row.insert(QLatin1String("coverageCount"), root.value(kOverridesKey).toArray().size() + (hasBaseline ? 1 : 0));
        row.insert(QLatin1String("hasBaseline"), hasBaseline);
        row.insert(QLatin1String("active"), payloadContainedIn(root, live));
        // File mtime, for the row's "Updated …" line.
        row.insert(QLatin1String("modified"), info.lastModified());
        result.append(row);
    }
    return result;
}

bool ShaderSetStore::applySet(const QString& name)
{
    if (name.isEmpty() || !mutationAllowed()) {
        return false;
    }
    const QString filePath = setFilePath(name);
    if (filePath.isEmpty()) {
        return false;
    }
    // Every failure below is silent from the UI's side (QML fires and forgets
    // the Apply), so each one carries its own reason to the toast. Without
    // that the user clicks Apply and simply watches nothing happen.
    QJsonObject root;
    if (!readSetFile(filePath, &root)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not read the set \"%1\".").arg(name));
        return false;
    }
    if (!versionAccepted(root, QStringLiteral("applySet(%1)").arg(name))) {
        Q_EMIT toastRequested(PhosphorI18n::tr("\"%1\" was written by a newer version of PlasmaZones.").arg(name));
        return false;
    }
    if (m_config.validate && !m_config.validate(root)) {
        qCWarning(lcConfig) << "ShaderSetStore::applySet: validation refused" << filePath;
        Q_EMIT toastRequested(PhosphorI18n::tr("\"%1\" does not match this page.").arg(name));
        return false;
    }
    if (!m_config.apply || !m_config.apply(root)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not apply \"%1\".").arg(name));
        return false;
    }
    // Live state moved, so every row's `active` flag is stale.
    Q_EMIT setsChanged();
    notifyPendingChanges();
    return true;
}

QString ShaderSetStore::existingSetName(const QString& name) const
{
    const QString filePath = setFilePath(name);
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return QString();
    }
    // Report the stored spelling, not the caller's: the two collide by slug.
    QJsonObject root;
    if (readSetFile(filePath, &root)) {
        const QString stored = root.value(kNameKey).toString();
        if (!stored.isEmpty()) {
            return stored;
        }
    }
    return name;
}

bool ShaderSetStore::saveCurrentAsSet(const QString& name, const QString& description, bool overwrite)
{
    if (name.isEmpty() || !mutationAllowed()) {
        return false;
    }
    const QString filePath = setFilePath(name);
    if (filePath.isEmpty()) {
        // The name has nothing a filename can be built from (e.g. "!!!").
        // The Save button only checks for non-blank text, so say why.
        Q_EMIT toastRequested(PhosphorI18n::tr("That name cannot be used. Try one with letters or numbers in it."));
        return false;
    }
    if (!m_config.snapshot) {
        return false;
    }
    // Overwriting destroys the stored payload, and on a domain with no
    // fileSnapshot hook nothing could restore it. Allowed, but only with
    // explicit consent — QML confirms first and then passes overwrite=true.
    if (!overwrite && QFile::exists(filePath)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("A set named \"%1\" already exists.").arg(name));
        return false;
    }

    QJsonObject root = m_config.snapshot();
    // An empty snapshot would save a set that applySet then refuses (nothing
    // to stage). Refuse the save so the user isn't left with a do-nothing set
    // on disk. Checked before mkpath so a rejected save leaves no empty
    // directory behind.
    if (root.value(kOverridesKey).toArray().isEmpty() && !carriesBaseline(root)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("There is nothing to capture yet."));
        return false;
    }

    const QString dirPath = setsDirectory();
    if (dirPath.isEmpty() || !QDir().mkpath(dirPath)) {
        qCWarning(lcConfig) << "ShaderSetStore::saveCurrentAsSet: cannot create" << dirPath;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not create the sets folder."));
        return false;
    }
    if (!snapshotFile(filePath)) {
        return false;
    }

    root.insert(kNameKey, name);
    if (!description.isEmpty()) {
        root.insert(kDescriptionKey, description);
    }
    root.insert(kVersionKey, m_config.formatVersion);

    if (!writeSetFile(filePath, root)) {
        return false;
    }

    Q_EMIT setsChanged();
    notifyPendingChanges();
    return true;
}

bool ShaderSetStore::removeSet(const QString& name)
{
    if (name.isEmpty() || !mutationAllowed()) {
        return false;
    }
    const QString filePath = setFilePath(name);
    if (filePath.isEmpty()) {
        return false;
    }
    QFile file(filePath);
    if (!file.exists()) {
        qCWarning(lcConfig) << "ShaderSetStore::removeSet: no such set:" << filePath;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not delete \"%1\".").arg(name));
        return false;
    }
    if (!snapshotFile(filePath)) {
        return false;
    }
    if (!file.remove()) {
        qCWarning(lcConfig) << "ShaderSetStore::removeSet: could not remove" << filePath;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not delete \"%1\".").arg(name));
        // The snapshot is already staged, so the domain's dirty state moved
        // even though the delete failed. Re-notify so the page can re-evaluate.
        notifyPendingChanges();
        return false;
    }
    Q_EMIT setsChanged();
    notifyPendingChanges();
    return true;
}

bool ShaderSetStore::updateSet(const QString& oldName, const QString& newName, const QString& description)
{
    if (oldName.isEmpty() || !mutationAllowed()) {
        return false;
    }
    if (newName.isEmpty()) {
        Q_EMIT toastRequested(PhosphorI18n::tr("A set needs a name."));
        return false;
    }
    const QString oldPath = setFilePath(oldName);
    const QString newPath = setFilePath(newName);
    if (newPath.isEmpty()) {
        // The new name has nothing a filename can be built from (e.g. "!!!").
        Q_EMIT toastRequested(PhosphorI18n::tr("That name cannot be used. Try one with letters or numbers in it."));
        return false;
    }
    if (oldPath.isEmpty()) {
        return false;
    }
    if (!QFile::exists(oldPath)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not read the set \"%1\".").arg(oldName));
        return false;
    }
    // Renaming onto another set would destroy it. Refuse, with the reason.
    if (newPath != oldPath && QFile::exists(newPath)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("A set named \"%1\" already exists.").arg(newName));
        return false;
    }

    QJsonObject root;
    if (!readSetFile(oldPath, &root)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not read the set \"%1\".").arg(oldName));
        return false;
    }
    root.insert(kNameKey, newName);
    // Same convention as save: an empty description is omitted, not stored
    // as an empty string.
    if (description.isEmpty()) {
        root.remove(kDescriptionKey);
    } else {
        root.insert(kDescriptionKey, description);
    }

    if (!snapshotFile(oldPath)) {
        return false;
    }
    if (newPath != oldPath && !snapshotFile(newPath)) {
        return false;
    }

    if (!writeSetFile(newPath, root)) {
        return false;
    }
    // Only drop the old file once the new one is safely committed. If the
    // delete fails we are left with two files carrying the same `name`, which
    // lists as a duplicate row that removeSet can never reach (it resolves the
    // slug of the NEW name). Tell the user rather than logging into the void.
    if (newPath != oldPath && !QFile::remove(oldPath)) {
        qCWarning(lcConfig) << "ShaderSetStore::updateSet: wrote" << newPath << "but could not remove" << oldPath;
        Q_EMIT toastRequested(
            PhosphorI18n::tr("Renamed the set, but the old file could not be removed. Delete it by hand from the "
                             "sets folder."));
    }

    Q_EMIT setsChanged();
    notifyPendingChanges();
    return true;
}

bool ShaderSetStore::exportSet(const QString& name, const QString& destLocalPath)
{
    if (name.isEmpty()) {
        return false;
    }
    if (destLocalPath.isEmpty()) {
        // urlToLocalFile yields an empty string for a non-local save target.
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not write to that location."));
        return false;
    }
    const QString sourcePath = setFilePath(name);
    if (sourcePath.isEmpty()) {
        return false;
    }
    QFile source(sourcePath);
    if (!source.exists() || !source.open(QIODevice::ReadOnly)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not read the set \"%1\".").arg(name));
        return false;
    }
    const QByteArray payload = source.readAll();

    QSaveFile dest(destLocalPath);
    const bool written =
        dest.open(QIODevice::WriteOnly | QIODevice::Truncate) && dest.write(payload) == payload.size() && dest.commit();
    if (!written) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not write to %1.").arg(destLocalPath));
        return false;
    }
    return true;
}

bool ShaderSetStore::importSet(const QString& sourcePathOrUrl)
{
    if (sourcePathOrUrl.isEmpty() || !mutationAllowed()) {
        return false;
    }
    // The drop zone hands over raw file:// URLs; the file dialog hands over
    // local paths. Accept both.
    QString sourcePath = sourcePathOrUrl;
    const QUrl url(sourcePathOrUrl);
    if (url.isLocalFile()) {
        sourcePath = url.toLocalFile();
    }

    QJsonObject root;
    if (!readSetFile(sourcePath, &root)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("That file is not a readable set."));
        return false;
    }
    if (!versionAccepted(root, QStringLiteral("importSet(%1)").arg(sourcePath))) {
        Q_EMIT toastRequested(PhosphorI18n::tr("That set was written by a newer version of PlasmaZones."));
        return false;
    }
    // A set carrying nothing would import as a row that applySet then refuses.
    // Name it for what it is rather than falling through to the taxonomy
    // message below, which would misdescribe an empty file as a foreign one.
    if (root.value(kOverridesKey).toArray().isEmpty() && !carriesBaseline(root)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("That set is empty."));
        return false;
    }
    // Validate against THIS domain's taxonomy, so a motion set dropped on the
    // decoration page is refused at the boundary instead of failing later.
    if (m_config.validate && !m_config.validate(root)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("That set does not match this page."));
        return false;
    }

    // Fall back to the file name when the payload carries no usable name.
    QString desiredName = root.value(kNameKey).toString();
    if (desiredName.isEmpty() || animfileutil::slugify(desiredName).isEmpty()) {
        desiredName = QFileInfo(sourcePath).completeBaseName();
    }
    const QString name = uniqueSetName(desiredName);
    if (name.isEmpty()) {
        Q_EMIT toastRequested(PhosphorI18n::tr("That set has no usable name."));
        return false;
    }
    const QString destPath = setFilePath(name);
    const QString dirPath = setsDirectory();
    if (destPath.isEmpty() || dirPath.isEmpty() || !QDir().mkpath(dirPath)) {
        qCWarning(lcConfig) << "ShaderSetStore::importSet: cannot create" << dirPath;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not create the sets folder."));
        return false;
    }
    root.insert(kNameKey, name);

    if (!snapshotFile(destPath)) {
        return false;
    }
    if (!writeSetFile(destPath, root)) {
        return false;
    }

    Q_EMIT setsChanged();
    notifyPendingChanges();
    return true;
}

void ShaderSetStore::openSetsDirectory()
{
    const QString dir = setsDirectory();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        qCWarning(lcConfig) << "ShaderSetStore::openSetsDirectory: cannot create" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not create the sets folder."));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        qCWarning(lcConfig) << "ShaderSetStore::openSetsDirectory: no handler for" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not open the sets folder."));
    }
}

} // namespace PlasmaZones
