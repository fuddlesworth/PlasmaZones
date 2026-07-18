// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "profilestore.h"

#include "../core/logging.h"
#include "../phosphor_i18n.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QUrl>

namespace PlasmaZones {

namespace {

constexpr QLatin1String kProfVersionKey{"_version"};
constexpr QLatin1String kProfIdKey{"id"};
constexpr QLatin1String kProfNameKey{"name"};
constexpr QLatin1String kProfDescriptionKey{"description"};
constexpr QLatin1String kProfParentKey{"parent"};
constexpr QLatin1String kProfConfigKey{"config"};
constexpr QLatin1String kProfRulesKey{"rules"};
constexpr QLatin1String kProfActiveKey{"active"};
constexpr QLatin1String kProfOrderKey{"order"};

} // namespace

ProfileStore::ProfileStore(Config config, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
{
}

// ── Paths ────────────────────────────────────────────────────────────────────

QString ProfileStore::profilesDirectory() const
{
    return m_config.profilesDir ? m_config.profilesDir() : QString();
}

QString ProfileStore::profileFilePath(const QUuid& id) const
{
    const QString dir = profilesDirectory();
    if (dir.isEmpty() || id.isNull()) {
        return QString();
    }
    // Filesystem paths use the brace-less UUID form (CLAUDE.md convention).
    return dir + QLatin1Char('/') + id.toString(QUuid::WithoutBraces) + QStringLiteral(".json");
}

QString ProfileStore::indexFilePath() const
{
    const QString dir = profilesDirectory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/index.json");
}

// ── File I/O ──────────────────────────────────────────────────────────────────

bool ProfileStore::readProfileFile(const QString& path, Record* out) const
{
    // isFile() BEFORE open: the profiles dir is user-writable and
    // openProfilesDirectory() invites hand-placed files, so guard against a
    // fifo/device (which would block or report size 0) the same way the shader
    // set store does.
    const QFileInfo info(path);
    if (!info.isFile()) {
        qCWarning(lcConfig) << "ProfileStore: refusing to read a non-regular file" << path;
        return false;
    }
    if (info.size() > kMaxProfileFileBytes) {
        qCWarning(lcConfig) << "ProfileStore: profile file" << path << "is" << info.size() << "bytes, over the"
                            << kMaxProfileFileBytes << "byte cap — refusing";
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcConfig) << "ProfileStore: cannot open profile file:" << path;
        return false;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcConfig) << "ProfileStore: failed to parse profile file" << path << ":" << err.errorString();
        return false;
    }
    const QJsonObject root = doc.object();

    // Refuse a file stamped with a different schema version rather than
    // mis-applying config-delta keys whose shape moved between versions.
    const QJsonValue versionVal = root.value(kProfVersionKey);
    if (!versionVal.isDouble() || versionVal.toInt() != m_config.formatVersion) {
        qCWarning(lcConfig) << "ProfileStore: profile file" << path << "has version" << versionVal
                            << "but this build expects" << m_config.formatVersion << "— refusing";
        return false;
    }

    const QUuid id(root.value(kProfIdKey).toString());
    if (id.isNull()) {
        qCWarning(lcConfig) << "ProfileStore: profile file" << path << "has no valid id — refusing";
        return false;
    }

    out->id = id;
    out->name = root.value(kProfNameKey).toString();
    out->description = root.value(kProfDescriptionKey).toString();
    out->parent = QUuid(root.value(kProfParentKey).toString()); // null for a root profile
    out->configDelta = root.value(kProfConfigKey).toObject();
    out->rulesSection = root.value(kProfRulesKey).toObject();
    return true;
}

QJsonObject ProfileStore::recordToJson(const Record& rec, int formatVersion)
{
    QJsonObject root;
    root.insert(kProfVersionKey, formatVersion);
    root.insert(kProfIdKey, rec.id.toString());
    root.insert(kProfNameKey, rec.name);
    root.insert(kProfDescriptionKey, rec.description);
    // A root profile stores null so the field is always present and explicit.
    root.insert(kProfParentKey, rec.parent.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(rec.parent.toString()));
    root.insert(kProfConfigKey, rec.configDelta);
    root.insert(kProfRulesKey, rec.rulesSection);
    return root;
}

bool ProfileStore::writeProfileRecord(const Record& rec)
{
    const QString dir = profilesDirectory();
    const QString path = profileFilePath(rec.id);
    if (path.isEmpty() || dir.isEmpty() || !QDir().mkpath(dir)) {
        qCWarning(lcConfig) << "ProfileStore: cannot create the profiles folder" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not create the profiles folder."));
        return false;
    }
    QSaveFile file(path);
    const QByteArray payload = QJsonDocument(recordToJson(rec, m_config.formatVersion)).toJson(QJsonDocument::Indented);
    const bool written =
        file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(payload) == payload.size() && file.commit();
    if (!written) {
        qCWarning(lcConfig) << "ProfileStore: could not write" << path << ":" << file.errorString();
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not write the profile to disk."));
        return false;
    }
    return true;
}

QHash<QUuid, ProfileStore::Record> ProfileStore::loadAll() const
{
    QHash<QUuid, Record> result;
    const QString dirPath = profilesDirectory();
    if (dirPath.isEmpty()) {
        return result;
    }
    const QDir dir(dirPath);
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString& name : files) {
        if (name == QLatin1String("index.json")) {
            continue;
        }
        Record rec;
        if (readProfileFile(dir.absoluteFilePath(name), &rec)) {
            result.insert(rec.id, rec);
        }
    }
    return result;
}

// ── Inheritance resolution / delta ────────────────────────────────────────────

void ProfileStore::overlayConfig(QJsonObject& base, const QJsonObject& delta)
{
    for (auto git = delta.constBegin(); git != delta.constEnd(); ++git) {
        const QJsonObject deltaGroup = git.value().toObject();
        QJsonObject baseGroup = base.value(git.key()).toObject();
        for (auto kit = deltaGroup.constBegin(); kit != deltaGroup.constEnd(); ++kit) {
            baseGroup.insert(kit.key(), kit.value());
        }
        base.insert(git.key(), baseGroup);
    }
}

QJsonObject ProfileStore::diffConfig(const QJsonObject& full, const QJsonObject& base)
{
    QJsonObject delta;
    for (auto git = full.constBegin(); git != full.constEnd(); ++git) {
        // Skip the top-level `_version` marker — only group objects are walked.
        if (!git.value().isObject()) {
            continue;
        }
        const QJsonObject fullGroup = git.value().toObject();
        const QJsonObject baseGroup = base.value(git.key()).toObject();
        QJsonObject deltaGroup;
        for (auto kit = fullGroup.constBegin(); kit != fullGroup.constEnd(); ++kit) {
            if (baseGroup.value(kit.key()) != kit.value()) {
                deltaGroup.insert(kit.key(), kit.value());
            }
        }
        if (!deltaGroup.isEmpty()) {
            delta.insert(git.key(), deltaGroup);
        }
    }
    return delta;
}

QJsonObject ProfileStore::resolveConfig(const QUuid& id, const QHash<QUuid, Record>& all) const
{
    // Collect the chain root → … → id so deltas overlay in inheritance order
    // (a child overrides its ancestors). A broken parent link (missing id) or a
    // cycle stops the walk; the accumulated chain is still applied.
    QList<QUuid> chain;
    QUuid cursor = id;
    QSet<QUuid> seen;
    while (!cursor.isNull() && all.contains(cursor) && !seen.contains(cursor)) {
        seen.insert(cursor);
        chain.prepend(cursor);
        cursor = all.value(cursor).parent;
    }

    QJsonObject resolved = m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject();
    for (const QUuid& node : chain) {
        overlayConfig(resolved, all.value(node).configDelta);
    }
    return resolved;
}

bool ProfileStore::isSelfOrAncestor(const QUuid& maybeAncestor, const QUuid& id, const QHash<QUuid, Record>& all) const
{
    QUuid cursor = id;
    QSet<QUuid> seen;
    while (!cursor.isNull() && !seen.contains(cursor)) {
        if (cursor == maybeAncestor) {
            return true;
        }
        seen.insert(cursor);
        if (!all.contains(cursor)) {
            break;
        }
        cursor = all.value(cursor).parent;
    }
    return false;
}

QString ProfileStore::uniqueName(const QString& desired, const QHash<QUuid, Record>& all, const QUuid& excludeId) const
{
    const QString trimmed = desired.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    const auto taken = [&](const QString& candidate) {
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            if (it.key() != excludeId && it.value().name.compare(candidate, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };
    QString candidate = trimmed;
    for (int suffix = 2; taken(candidate); ++suffix) {
        if (suffix > 999) {
            return QString();
        }
        candidate = QStringLiteral("%1 (%2)").arg(trimmed).arg(suffix);
    }
    return candidate;
}

// ── index.json (active pointer + display order) ───────────────────────────────

QJsonObject ProfileStore::readIndex() const
{
    const QString path = indexFilePath();
    if (path.isEmpty()) {
        return QJsonObject();
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    const auto doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

void ProfileStore::writeIndex(const QJsonObject& index)
{
    const QString dir = profilesDirectory();
    const QString path = indexFilePath();
    if (path.isEmpty() || dir.isEmpty() || !QDir().mkpath(dir)) {
        qCWarning(lcConfig) << "ProfileStore: cannot write index.json under" << dir;
        return;
    }
    QSaveFile file(path);
    const QByteArray payload = QJsonDocument(index).toJson(QJsonDocument::Indented);
    if (!(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(payload) == payload.size()
          && file.commit())) {
        qCWarning(lcConfig) << "ProfileStore: could not write index.json:" << file.errorString();
    }
}

QList<QUuid> ProfileStore::readOrder() const
{
    QList<QUuid> order;
    const QJsonArray arr = readIndex().value(kProfOrderKey).toArray();
    for (const QJsonValue& v : arr) {
        const QUuid id(v.toString());
        if (!id.isNull()) {
            order.append(id);
        }
    }
    return order;
}

void ProfileStore::appendToOrder(const QUuid& id)
{
    QJsonObject index = readIndex();
    QJsonArray order = index.value(kProfOrderKey).toArray();
    for (const QJsonValue& v : order) {
        if (QUuid(v.toString()) == id) {
            return; // already present
        }
    }
    order.append(id.toString());
    index.insert(kProfOrderKey, order);
    writeIndex(index);
}

void ProfileStore::removeFromOrder(const QUuid& id)
{
    QJsonObject index = readIndex();
    const QJsonArray order = index.value(kProfOrderKey).toArray();
    QJsonArray kept;
    for (const QJsonValue& v : order) {
        if (QUuid(v.toString()) != id) {
            kept.append(v);
        }
    }
    index.insert(kProfOrderKey, kept);
    writeIndex(index);
}

QString ProfileStore::committedActiveId() const
{
    return readIndex().value(kProfActiveKey).toString();
}

void ProfileStore::writeActiveId(const QString& id)
{
    QJsonObject index = readIndex();
    index.insert(kProfActiveKey, id.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(id));
    writeIndex(index);
}

// ── Query ─────────────────────────────────────────────────────────────────────

QString ProfileStore::activeProfileId() const
{
    return m_config.stagedActiveId ? m_config.stagedActiveId() : QString();
}

void ProfileStore::refresh()
{
    Q_EMIT profilesChanged();
}

QVariantList ProfileStore::availableProfiles() const
{
    const QHash<QUuid, Record> all = loadAll();
    const QString stagedActive = activeProfileId();

    // Display order: index.json order first (for ids still present), then any
    // profiles not yet in the order (hand-placed / freshly imported), by name.
    QList<QUuid> ordered;
    for (const QUuid& id : readOrder()) {
        if (all.contains(id) && !ordered.contains(id)) {
            ordered.append(id);
        }
    }
    QList<QUuid> leftovers;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        if (!ordered.contains(it.key())) {
            leftovers.append(it.key());
        }
    }
    std::sort(leftovers.begin(), leftovers.end(), [&](const QUuid& a, const QUuid& b) {
        return all.value(a).name.compare(all.value(b).name, Qt::CaseInsensitive) < 0;
    });
    ordered.append(leftovers);

    QVariantList rows;
    for (const QUuid& id : ordered) {
        const Record& rec = all.value(id);
        const QString idStr = rec.id.toString();
        QVariantMap row;
        row.insert(QStringLiteral("id"), idStr);
        row.insert(QStringLiteral("name"), rec.name);
        row.insert(QStringLiteral("description"), rec.description);
        row.insert(QStringLiteral("parentId"), rec.parent.isNull() ? QString() : rec.parent.toString());
        row.insert(QStringLiteral("parentName"),
                   rec.parent.isNull() || !all.contains(rec.parent) ? QString() : all.value(rec.parent).name);
        row.insert(QStringLiteral("isRoot"), rec.parent.isNull());
        row.insert(QStringLiteral("active"), idStr == stagedActive);
        rows.append(row);
    }
    return rows;
}

// ── CRUD ──────────────────────────────────────────────────────────────────────

QString ProfileStore::createProfile(const QString& name, const QString& description, const QString& parentId)
{
    const QHash<QUuid, Record> all = loadAll();

    const QString finalName = uniqueName(name, all);
    if (finalName.isEmpty()) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Please enter a name for the profile."));
        return QString();
    }

    const QUuid parent(parentId); // null when empty
    if (!parent.isNull() && !all.contains(parent)) {
        qCWarning(lcConfig) << "ProfileStore::createProfile: unknown parent" << parentId;
        return QString();
    }

    Record rec;
    rec.id = QUuid::createUuid();
    rec.name = finalName;
    rec.description = description.trimmed();
    rec.parent = parent;
    // Capture the current live config as a delta against the parent's resolved
    // config (schema defaults for a root profile).
    const QJsonObject parentResolved = parent.isNull()
        ? (m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject())
        : resolveConfig(parent, all);
    const QJsonObject current = m_config.currentConfig ? m_config.currentConfig() : QJsonObject();
    rec.configDelta = diffConfig(current, parentResolved);

    if (!writeProfileRecord(rec)) {
        return QString();
    }
    appendToOrder(rec.id);

    // The new profile matches the current settings, so it becomes the staged
    // active one without staging any config change.
    if (m_config.setStagedActiveId) {
        m_config.setStagedActiveId(rec.id.toString());
    }
    Q_EMIT profilesChanged();
    return rec.id.toString();
}

bool ProfileStore::renameProfile(const QString& id, const QString& newName, const QString& description)
{
    QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return false;
    }
    const QString finalName = uniqueName(newName, all, uid);
    if (finalName.isEmpty()) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Please enter a name for the profile."));
        return false;
    }
    Record rec = all.value(uid);
    rec.name = finalName;
    rec.description = description.trimmed();
    if (!writeProfileRecord(rec)) {
        return false;
    }
    Q_EMIT profilesChanged();
    return true;
}

QString ProfileStore::duplicateProfile(const QString& id)
{
    const QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return QString();
    }
    Record rec = all.value(uid);
    rec.id = QUuid::createUuid();
    rec.name = uniqueName(PhosphorI18n::tr("%1 (copy)").arg(rec.name), all);
    if (rec.name.isEmpty()) {
        return QString();
    }
    if (!writeProfileRecord(rec)) {
        return QString();
    }
    appendToOrder(rec.id);
    Q_EMIT profilesChanged();
    return rec.id.toString();
}

bool ProfileStore::setParent(const QString& id, const QString& parentId)
{
    QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    const QUuid parent(parentId); // null → root
    if (!all.contains(uid)) {
        return false;
    }
    if (!parent.isNull() && !all.contains(parent)) {
        return false;
    }
    // Reject a cycle: the new parent must not be the profile itself or one of
    // its descendants.
    if (!parent.isNull() && isSelfOrAncestor(uid, parent, all)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("A profile cannot inherit from itself."));
        return false;
    }

    Record rec = all.value(uid);
    if (rec.parent == parent) {
        return true; // no-op
    }
    // Re-flatten the delta so the RESOLVED config is unchanged after reparenting.
    const QJsonObject resolvedSelf = resolveConfig(uid, all);
    const QJsonObject newParentResolved = parent.isNull()
        ? (m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject())
        : resolveConfig(parent, all);
    rec.parent = parent;
    rec.configDelta = diffConfig(resolvedSelf, newParentResolved);
    if (!writeProfileRecord(rec)) {
        return false;
    }
    Q_EMIT profilesChanged();
    return true;
}

bool ProfileStore::removeProfile(const QString& id)
{
    QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return false;
    }
    const QUuid grandparent = all.value(uid).parent;

    // Rebind each direct child to the deleted node's parent, re-flattening its
    // delta so its resolved config is unchanged. Compute every child's resolved
    // config against the CURRENT tree before mutating anything.
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().parent != uid) {
            continue;
        }
        const QUuid childId = it.key();
        const QJsonObject childResolved = resolveConfig(childId, all);
        const QJsonObject newParentResolved = grandparent.isNull()
            ? (m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject())
            : resolveConfig(grandparent, all);
        Record child = it.value();
        child.parent = grandparent;
        child.configDelta = diffConfig(childResolved, newParentResolved);
        if (!writeProfileRecord(child)) {
            return false;
        }
    }

    const QString path = profileFilePath(uid);
    if (!path.isEmpty() && QFile::exists(path) && !QFile::remove(path)) {
        qCWarning(lcConfig) << "ProfileStore::removeProfile: could not delete" << path;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not delete the profile."));
        return false;
    }
    removeFromOrder(uid);

    // Clear the staged active pointer if it referenced the deleted profile.
    if (m_config.stagedActiveId && m_config.setStagedActiveId && m_config.stagedActiveId() == id) {
        m_config.setStagedActiveId(QString());
    }
    Q_EMIT profilesChanged();
    return true;
}

bool ProfileStore::activateProfile(const QString& id)
{
    const QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return false;
    }
    // Resolve the full config and stage it into the settings store (uncommitted,
    // lights the Save footer). The controller's Save commits it.
    if (m_config.applyConfig) {
        m_config.applyConfig(resolveConfig(uid, all));
    }
    if (m_config.setStagedActiveId) {
        m_config.setStagedActiveId(id);
    }
    Q_EMIT profilesChanged();
    return true;
}

// ── Import / export ────────────────────────────────────────────────────────────

bool ProfileStore::exportProfile(const QString& id, const QString& destLocalPath)
{
    if (destLocalPath.isEmpty()) {
        // urlToLocalFile yields an empty string for a non-local save target.
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not write to that location."));
        return false;
    }
    const QUuid uid(id);
    const QString sourcePath = profileFilePath(uid);
    if (sourcePath.isEmpty()) {
        return false;
    }
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.isFile() || sourceInfo.size() > kMaxProfileFileBytes) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not read that profile."));
        return false;
    }
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not read that profile."));
        return false;
    }
    const QByteArray payload = source.readAll();
    QSaveFile dest(destLocalPath);
    if (!(dest.open(QIODevice::WriteOnly | QIODevice::Truncate) && dest.write(payload) == payload.size()
          && dest.commit())) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not write to %1.").arg(destLocalPath));
        return false;
    }
    return true;
}

QString ProfileStore::importProfile(const QString& sourcePathOrUrl)
{
    if (sourcePathOrUrl.isEmpty()) {
        return QString();
    }
    // The drop zone hands over raw file:// URLs; the file dialog hands over
    // local paths. Accept both.
    QString sourcePath = sourcePathOrUrl;
    const QUrl url(sourcePathOrUrl);
    if (url.isLocalFile()) {
        sourcePath = url.toLocalFile();
    }

    Record rec;
    if (!readProfileFile(sourcePath, &rec)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("That file is not a readable profile."));
        return QString();
    }

    QHash<QUuid, Record> all = loadAll();
    // Always mint a FRESH id so an import never overwrites an existing profile.
    rec.id = QUuid::createUuid();
    // Drop a dangling parent reference (the parent is not in this install).
    if (!rec.parent.isNull() && !all.contains(rec.parent)) {
        rec.parent = QUuid();
    }
    QString desiredName = rec.name;
    if (desiredName.trimmed().isEmpty()) {
        desiredName = QFileInfo(sourcePath).completeBaseName();
    }
    rec.name = uniqueName(desiredName, all);
    if (rec.name.isEmpty()) {
        rec.name = uniqueName(PhosphorI18n::tr("Imported profile"), all);
    }
    if (rec.name.isEmpty() || !writeProfileRecord(rec)) {
        return QString();
    }
    appendToOrder(rec.id);
    Q_EMIT profilesChanged();
    return rec.id.toString();
}

void ProfileStore::openProfilesDirectory()
{
    const QString dir = profilesDirectory();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        qCWarning(lcConfig) << "ProfileStore::openProfilesDirectory: cannot create" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not create the profiles folder."));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        qCWarning(lcConfig) << "ProfileStore::openProfilesDirectory: no handler for" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not open the profiles folder."));
    }
}

} // namespace PlasmaZones
