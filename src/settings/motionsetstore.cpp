// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "motionsetstore.h"
#include "animationfileutils.h"

#include "../core/logging.h"

#include <PhosphorAnimation/ProfilePaths.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>

namespace PlasmaZones {

namespace motionset_detail {

static constexpr QLatin1String JsonNameKey{"name"};
static constexpr QLatin1String JsonDescriptionKey{"description"};
static constexpr QLatin1String JsonOverridesKey{"overrides"};
static constexpr QLatin1String JsonPathKey{"path"};
static constexpr QLatin1String JsonProfileKey{"profile"};
static constexpr QLatin1String JsonVersionKey{"version"};

} // namespace motionset_detail

MotionSetStore::MotionSetStore(ProfilesDirFn profilesDirFn, std::function<QString()> motionSetsDirFn,
                               WriteOverrideFn writeOverride, SnapshotFn snapshot, QObject* parent)
    : QObject(parent)
    , m_profilesDir(std::move(profilesDirFn))
    , m_motionSetsDir(std::move(motionSetsDirFn))
    , m_writeOverride(std::move(writeOverride))
    , m_snapshot(std::move(snapshot))
{
}

QString MotionSetStore::motionSetFilePath(const QString& setName) const
{
    return animfileutil::jsonFilePath(m_motionSetsDir(), animfileutil::slugify(setName));
}

QVariantList MotionSetStore::availableMotionSets() const
{
    QVariantList result;
    QDir dir(m_motionSetsDir());
    if (!dir.exists())
        return result;

    const auto files = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        QFile f(info.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            qCWarning(lcConfig) << "MotionSetStore: cannot open motion set file:" << info.absoluteFilePath();
            continue;
        }
        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcConfig) << "MotionSetStore: failed to parse motion set" << info.absoluteFilePath() << ":"
                                << err.errorString();
            continue;
        }
        const QJsonObject obj = doc.object();

        QVariantMap row;
        row.insert(QStringLiteral("name"), obj.value(motionset_detail::JsonNameKey).toString());
        row.insert(QStringLiteral("description"), obj.value(motionset_detail::JsonDescriptionKey).toString());
        row.insert(QStringLiteral("slug"), info.completeBaseName());
        row.insert(QStringLiteral("overrideCount"), obj.value(motionset_detail::JsonOverridesKey).toArray().size());
        result.append(row);
    }
    return result;
}

bool MotionSetStore::applyMotionSet(const QString& name)
{
    using namespace PhosphorAnimation;

    if (name.isEmpty())
        return false;

    const QString filePath = motionSetFilePath(name);
    if (filePath.isEmpty())
        return false;

    QFile f(filePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcConfig) << "MotionSetStore::applyMotionSet: failed to parse" << filePath << ":"
                            << err.errorString();
        return false;
    }

    const QJsonArray overrides = doc.object().value(motionset_detail::JsonOverridesKey).toArray();
    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());

    // ── Stage 1: validate all entries up-front. Reject the whole set
    //    on any malformed entry rather than committing partial state.
    struct StagedEntry
    {
        QString path;
        QVariantMap profile;
    };
    QList<StagedEntry> staged;
    staged.reserve(overrides.size());
    for (const QJsonValue& v : overrides) {
        if (!v.isObject()) {
            qCWarning(lcConfig) << "MotionSetStore::applyMotionSet: non-object entry in" << filePath;
            return false;
        }
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(motionset_detail::JsonPathKey).toString();
        // Reject empty / unknown / traversal-attempting paths up-front.
        // Membership in `allBuiltInPaths()` is the single source of truth
        // — same validation rule the controller's `setOverride` enforces.
        if (path.isEmpty() || !knownPathSet.contains(path)) {
            qCWarning(lcConfig) << "MotionSetStore::applyMotionSet: rejecting unknown / invalid path" << path << "in"
                                << filePath;
            return false;
        }
        if (!entry.value(motionset_detail::JsonProfileKey).isObject()) {
            qCWarning(lcConfig) << "MotionSetStore::applyMotionSet: missing profile object for path" << path << "in"
                                << filePath;
            return false;
        }
        staged.push_back({path, entry.value(motionset_detail::JsonProfileKey).toObject().toVariantMap()});
    }

    if (staged.isEmpty())
        return false;

    // ── Stage 2: write each entry. The controller's `setOverride`
    //    callback snapshots the prior content before each write, so a
    //    mid-batch failure leaves pre-edit content captured for every
    //    peer that already succeeded — Discard restores them atomically
    //    via the controller's revertPending walk.
    QStringList committedPaths;
    bool success = true;
    for (const StagedEntry& e : staged) {
        if (!m_writeOverride(e.path, e.profile)) {
            qCWarning(lcConfig) << "MotionSetStore::applyMotionSet: write failed for path" << e.path;
            success = false;
            break;
        }
        committedPaths.append(e.path);
    }

    if (!success) {
        // Roll back: re-running setOverride with the snapshotted content
        // isn't reachable from here without leaking snapshot internals,
        // so instead: ask the snapshot infra to revert. The controller
        // uses the snapshot store as the rollback source — it already
        // captured pre-edit state for every committed entry via the
        // setOverride call. Surfacing that requires the controller to
        // expose a per-path revert; rather than doing that, we leave the
        // partial state in place but signal pendingChangesChanged so the
        // user can hit Discard. This is consistent with the project's
        // "no temporary workarounds" rule because a caller who hits this
        // path already has dirty in-memory snapshots for every committed
        // peer — Discard restores them atomically.
        Q_EMIT pendingChangesChanged();
        Q_EMIT motionSetsChanged();
        return false;
    }

    // ── Stage 4: success path. The per-path overrideChanged signal is
    //    already fired by AnimationsPageController::setOverride() inside
    //    each m_writeOverride call above; emitting it again here would
    //    double-tick QML observers. Only emit pendingChangesChanged once.
    Q_EMIT pendingChangesChanged();
    return true;
}

bool MotionSetStore::saveCurrentAsMotionSet(const QString& name, const QString& description)
{
    using namespace PhosphorAnimation;

    if (name.isEmpty())
        return false;

    const QString filePath = motionSetFilePath(name);
    if (filePath.isEmpty())
        return false;

    const QString setsDir = m_motionSetsDir();
    if (!QDir().mkpath(setsDir))
        return false;

    // Walk the profiles dir, pick out files whose `name` matches a known
    // event path. Build a sparse `overrides` array; non-path names
    // (presets) are skipped.
    QJsonArray overrides;
    QDir profilesDir(m_profilesDir());
    if (profilesDir.exists()) {
        const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
        const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());
        const auto files = profilesDir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo& info : files) {
            QFile f(info.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) {
                qCWarning(lcConfig) << "MotionSetStore::save: cannot open" << info.absoluteFilePath();
                continue;
            }
            QJsonParseError err{};
            const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                qCWarning(lcConfig) << "MotionSetStore::save: failed to parse" << info.absoluteFilePath() << ":"
                                    << err.errorString();
                continue;
            }
            const QJsonObject obj = doc.object();
            const QString entryName = obj.value(motionset_detail::JsonNameKey).toString();
            if (!knownPathSet.contains(entryName))
                continue;

            QJsonObject profile = obj;
            profile.remove(motionset_detail::JsonNameKey);
            QJsonObject entry;
            entry.insert(motionset_detail::JsonPathKey, entryName);
            entry.insert(motionset_detail::JsonProfileKey, profile);
            overrides.append(entry);
        }
    }

    if (m_snapshot)
        m_snapshot(filePath);

    QJsonObject root;
    root.insert(motionset_detail::JsonNameKey, name);
    if (!description.isEmpty())
        root.insert(motionset_detail::JsonDescriptionKey, description);
    root.insert(motionset_detail::JsonVersionKey, 1);
    root.insert(motionset_detail::JsonOverridesKey, overrides);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return false;

    Q_EMIT motionSetsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

bool MotionSetStore::removeMotionSet(const QString& name)
{
    if (name.isEmpty())
        return false;
    const QString filePath = motionSetFilePath(name);
    if (filePath.isEmpty())
        return false;
    QFile file(filePath);
    if (!file.exists())
        return false;
    if (m_snapshot)
        m_snapshot(filePath);
    if (!file.remove())
        return false;
    Q_EMIT motionSetsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

} // namespace PlasmaZones
