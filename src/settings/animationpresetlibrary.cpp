// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationpresetlibrary.h"
#include "animationfileutils.h"

#include "../core/logging.h"

#include <PhosphorAnimation/ProfilePaths.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>

namespace PlasmaZones {

namespace presetlib_detail {

static constexpr QLatin1String JsonNameKey{"name"};

} // namespace presetlib_detail

AnimationPresetLibrary::AnimationPresetLibrary(ProfilesDirFn profilesDirFn, SnapshotFn snapshot, QObject* parent)
    : QObject(parent)
    , m_profilesDir(std::move(profilesDirFn))
    , m_snapshot(std::move(snapshot))
{
}

QString AnimationPresetLibrary::presetFilePath(const QString& presetName) const
{
    return animfileutil::jsonFilePath(m_profilesDir(), animfileutil::slugify(presetName));
}

QVariantList AnimationPresetLibrary::userPresets() const
{
    using namespace PhosphorAnimation;

    QVariantList result;
    QDir dir(m_profilesDir());
    if (!dir.exists())
        return result;

    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());

    const auto entries = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : entries) {
        QFile f(info.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            qCWarning(lcConfig) << "AnimationPresetLibrary: cannot open" << info.absoluteFilePath();
            continue;
        }
        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcConfig) << "AnimationPresetLibrary: failed to parse" << info.absoluteFilePath() << ":"
                                << err.errorString();
            continue;
        }
        QJsonObject obj = doc.object();
        const QString name = obj.value(presetlib_detail::JsonNameKey).toString();
        if (name.isEmpty() || knownPathSet.contains(name))
            continue;
        // Orphan override-file filter. `setOverride` writes to a file
        // whose basename equals the event path verbatim (e.g.
        // `panel.popup.json` for the path `panel.popup`); `addUserPreset`
        // routes through `slugify`, which replaces every non-alnum
        // (including '.') with '-', so a legitimate preset basename
        // never contains a dot. Any file whose basename has a dot is
        // therefore an override file — valid ones are already skipped
        // by the `knownPathSet.contains(name)` check above, leaving
        // only orphans (paths from a previous taxonomy that this build
        // no longer recognises). Without this check, an orphan override
        // file would leak into the preset list as a fake preset named
        // after the obsolete event path.
        if (info.completeBaseName().contains(QLatin1Char('.')))
            continue;
        obj.remove(presetlib_detail::JsonNameKey);
        if (obj.isEmpty())
            continue;

        QVariantMap entry = obj.toVariantMap();
        entry.insert(QStringLiteral("name"), name);
        result.append(entry);
    }
    return result;
}

bool AnimationPresetLibrary::addUserPreset(const QString& name, const QVariantMap& profileJson)
{
    using namespace PhosphorAnimation;

    if (name.isEmpty())
        return false;

    // Reject names that match a built-in event path — the file would
    // collide with an override slot. Check both the original name and
    // the slug because slugify lowercases.
    const QString slug = animfileutil::slugify(name);
    if (slug.isEmpty())
        return false;
    const QStringList builtInPaths = ProfilePaths::allBuiltInPaths();
    if (builtInPaths.contains(name) || builtInPaths.contains(slug))
        return false;

    const QString dir = m_profilesDir ? m_profilesDir() : QString();
    const QString filePath = animfileutil::jsonFilePath(dir, slug);
    if (dir.isEmpty() || filePath.isEmpty())
        return false;
    // mkpath AFTER the name checks above, so a refused write leaves no empty
    // profiles directory behind.
    if (!QDir().mkpath(dir))
        return false;

    // A false return means the pre-edit content could not be captured. Writing
    // anyway would lose it for good, with Discard unable to restore.
    if (m_snapshot && !m_snapshot(filePath)) {
        qCWarning(lcConfig) << "AnimationPresetLibrary: refusing to write" << filePath
                            << "— could not capture its pre-edit content";
        return false;
    }

    QJsonObject obj = QJsonObject::fromVariantMap(profileJson);
    obj.insert(presetlib_detail::JsonNameKey, name);

    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    QSaveFile file(filePath);
    const bool written =
        file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(payload) == payload.size() && file.commit();
    if (!written) {
        qCWarning(lcConfig) << "AnimationPresetLibrary: could not write" << filePath << ":" << file.errorString();
        // The snapshot above already staged the pre-edit content, so the dirty
        // state moved even though the write did not. Re-notify, or the page
        // keeps a stale flag.
        Q_EMIT pendingChangesChanged();
        return false;
    }

    Q_EMIT userPresetsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

bool AnimationPresetLibrary::removeUserPreset(const QString& name)
{
    using namespace PhosphorAnimation;

    if (name.isEmpty())
        return false;

    // Override files (those whose stem is a built-in path) are off-limits
    // to the preset CRUD surface. Skip them in the directory scan so the
    // by-name walk can't accidentally delete an event override that
    // happens to embed a `name` field matching @p name.
    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());

    // Try the slug-derived path first; fall back to a directory scan
    // for the file whose `name` field matches. The fallback covers the
    // edge case where the slug rule changes between writes.
    QString filePath = presetFilePath(name);
    if (!filePath.isEmpty()) {
        // Refuse to operate on a file whose stem is an override path —
        // even if the user's display name slugifies to it. Preset CRUD
        // does not own override files.
        const QString stem = QFileInfo(filePath).completeBaseName();
        if (knownPathSet.contains(stem))
            filePath.clear();
        else if (!QFileInfo::exists(filePath))
            filePath.clear();
    }

    if (filePath.isEmpty()) {
        QDir dir(m_profilesDir());
        const auto entries = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files);
        for (const QFileInfo& info : entries) {
            // Skip override files outright — their stem is the path,
            // and a hand-edited override embedding `"name": "<the
            // preset name we're searching for>"` would otherwise be
            // deleted here (the original bug).
            if (knownPathSet.contains(info.completeBaseName()))
                continue;
            QFile raw(info.absoluteFilePath());
            if (!raw.open(QIODevice::ReadOnly))
                continue;
            const auto doc = QJsonDocument::fromJson(raw.readAll());
            if (!doc.isObject())
                continue;
            if (doc.object().value(presetlib_detail::JsonNameKey).toString() == name) {
                filePath = info.absoluteFilePath();
                break;
            }
        }
    }

    if (filePath.isEmpty())
        return false;

    QFile file(filePath);
    if (!file.exists())
        return false;
    // A false return means the pre-edit content could not be captured. Writing
    // anyway would lose it for good, with Discard unable to restore.
    if (m_snapshot && !m_snapshot(filePath)) {
        qCWarning(lcConfig) << "AnimationPresetLibrary: refusing to write" << filePath
                            << "— could not capture its pre-edit content";
        return false;
    }
    if (!file.remove()) {
        qCWarning(lcConfig) << "AnimationPresetLibrary: could not remove" << filePath;
        // The snapshot above already staged the pre-edit content, so the dirty
        // state moved even though the delete did not. Re-notify, or the page
        // keeps a stale flag.
        Q_EMIT pendingChangesChanged();
        return false;
    }

    Q_EMIT userPresetsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

} // namespace PlasmaZones
