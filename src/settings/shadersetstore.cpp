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
#include <QSaveFile>
#include <QSet>
#include <QUrl>

namespace PlasmaZones {

namespace {

constexpr QLatin1String kNameKey{"name"};
constexpr QLatin1String kDescriptionKey{"description"};
constexpr QLatin1String kVersionKey{"version"};
constexpr QLatin1String kBaselineKey{"baseline"};
constexpr QLatin1String kOverridesKey{"overrides"};
constexpr QLatin1String kPathKey{"path"};
constexpr QLatin1String kProfileKey{"profile"};

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
    const bool hasBaseline = set.value(kBaselineKey).isObject();
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
}

QString ShaderSetStore::setFilePath(const QString& setName) const
{
    return animfileutil::jsonFilePath(m_config.setsDir(), animfileutil::slugify(setName));
}

void ShaderSetStore::notifyLiveStateChanged()
{
    Q_EMIT setsChanged();
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

bool ShaderSetStore::readSetFile(const QString& filePath, QJsonObject* out) const
{
    QFile f(filePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        qCWarning(lcConfig) << "ShaderSetStore: cannot open set file:" << filePath;
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

bool ShaderSetStore::versionAccepted(const QJsonObject& root, const QString& context) const
{
    const QJsonValue versionVal = root.value(kVersionKey);
    // A present-but-non-numeric version is malformed. Treat it as unknown
    // (newer) and refuse, rather than reading it as the current format and
    // committing a set this build may not fully understand.
    if (!versionVal.isUndefined() && !versionVal.isDouble()) {
        qCWarning(lcConfig) << "ShaderSetStore:" << context << "— non-numeric version, refusing";
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
    QDir dir(m_config.setsDir());
    if (!dir.exists()) {
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
        const bool hasBaseline = root.value(kBaselineKey).isObject();

        QVariantMap row;
        row.insert(QLatin1String("name"), root.value(kNameKey).toString());
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
    QJsonObject root;
    if (!readSetFile(filePath, &root)) {
        return false;
    }
    if (!versionAccepted(root, QStringLiteral("applySet(%1)").arg(name))) {
        return false;
    }
    if (m_config.validate && !m_config.validate(root)) {
        qCWarning(lcConfig) << "ShaderSetStore::applySet: validation refused" << filePath;
        return false;
    }
    if (!m_config.apply || !m_config.apply(root)) {
        return false;
    }
    // Live state moved, so every row's `active` flag is stale.
    Q_EMIT setsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

bool ShaderSetStore::saveCurrentAsSet(const QString& name, const QString& description)
{
    if (name.isEmpty() || !mutationAllowed()) {
        return false;
    }
    const QString filePath = setFilePath(name);
    if (filePath.isEmpty() || !m_config.snapshot) {
        return false;
    }

    QJsonObject root = m_config.snapshot();
    // An empty snapshot would save a set that applySet then refuses (nothing
    // to stage). Refuse the save so the user isn't left with a do-nothing set
    // on disk. Checked before mkpath so a rejected save leaves no empty
    // directory behind.
    if (root.value(kOverridesKey).toArray().isEmpty() && !root.value(kBaselineKey).isObject()) {
        return false;
    }

    if (!QDir().mkpath(m_config.setsDir())) {
        return false;
    }
    if (m_config.fileSnapshot) {
        m_config.fileSnapshot(filePath);
    }

    root.insert(kNameKey, name);
    if (!description.isEmpty()) {
        root.insert(kDescriptionKey, description);
    }
    root.insert(kVersionKey, m_config.formatVersion);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return false;
    }

    Q_EMIT setsChanged();
    Q_EMIT pendingChangesChanged();
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
        return false;
    }
    if (m_config.fileSnapshot) {
        m_config.fileSnapshot(filePath);
    }
    if (!file.remove()) {
        return false;
    }
    Q_EMIT setsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

bool ShaderSetStore::updateSet(const QString& oldName, const QString& newName, const QString& description)
{
    if (oldName.isEmpty() || newName.isEmpty() || !mutationAllowed()) {
        return false;
    }
    const QString oldPath = setFilePath(oldName);
    const QString newPath = setFilePath(newName);
    if (oldPath.isEmpty() || newPath.isEmpty()) {
        return false;
    }
    if (!QFile::exists(oldPath)) {
        return false;
    }
    // Renaming onto another set would destroy it. Refuse, with the reason.
    if (newPath != oldPath && QFile::exists(newPath)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("A set named \"%1\" already exists.").arg(newName));
        return false;
    }

    QJsonObject root;
    if (!readSetFile(oldPath, &root)) {
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

    if (m_config.fileSnapshot) {
        m_config.fileSnapshot(oldPath);
        if (newPath != oldPath) {
            m_config.fileSnapshot(newPath);
        }
    }

    QSaveFile file(newPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return false;
    }
    // Only drop the old file once the new one is safely committed.
    if (newPath != oldPath && !QFile::remove(oldPath)) {
        qCWarning(lcConfig) << "ShaderSetStore::updateSet: wrote" << newPath << "but could not remove" << oldPath;
    }

    Q_EMIT setsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

bool ShaderSetStore::exportSet(const QString& name, const QString& destLocalPath)
{
    if (name.isEmpty() || destLocalPath.isEmpty()) {
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
    if (!dest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not write to %1.").arg(destLocalPath));
        return false;
    }
    dest.write(payload);
    if (!dest.commit()) {
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
    if (destPath.isEmpty() || !QDir().mkpath(m_config.setsDir())) {
        return false;
    }
    root.insert(kNameKey, name);

    if (m_config.fileSnapshot) {
        m_config.fileSnapshot(destPath);
    }
    QSaveFile file(destPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return false;
    }

    Q_EMIT setsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

void ShaderSetStore::openSetsDirectory()
{
    const QString dir = m_config.setsDir();
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
