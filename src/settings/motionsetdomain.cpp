// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Motion-set domain closures for the shared ShaderSetStore. Motion sets
// snapshot the per-event override FILES under the profiles directory; the
// generic store handles the envelope (name / description / version), the
// coverage summary, and every file operation.

#include "motionsetdomain.h"

#include "../core/logging.h"

#include <PhosphorAnimation/ProfilePaths.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QLoggingCategory>
#include <QSet>
#include <QStringList>

namespace PlasmaZones::motionset {

namespace {

constexpr QLatin1String kNameKey{"name"};
constexpr QLatin1String kOverridesKey{"overrides"};
constexpr QLatin1String kPathKey{"path"};
constexpr QLatin1String kProfileKey{"profile"};

struct StagedEntry
{
    QString path;
    QVariantMap profile;
};

/// Validate + stage every entry in @p root. Whole-set discipline: one
/// malformed entry rejects the set rather than committing partial state.
/// Shared by validate (import / apply gate) and apply (the commit), so the
/// two can never drift apart on what counts as a valid entry.
bool stageEntries(const QJsonObject& root, QList<StagedEntry>* staged)
{
    using namespace PhosphorAnimation;

    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());

    const QJsonArray overrides = root.value(kOverridesKey).toArray();
    staged->reserve(overrides.size());
    for (const QJsonValue& v : overrides) {
        if (!v.isObject()) {
            qCWarning(lcConfig) << "motionset: non-object entry in set";
            return false;
        }
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(kPathKey).toString();
        // Membership in `allBuiltInPaths()` is the single source of truth —
        // the same rule the controller's setOverride enforces. It also
        // rejects empty and traversal-attempting paths.
        if (path.isEmpty() || !knownPathSet.contains(path)) {
            qCWarning(lcConfig) << "motionset: rejecting unknown / invalid path" << path;
            return false;
        }
        if (!entry.value(kProfileKey).isObject()) {
            qCWarning(lcConfig) << "motionset: missing profile object for path" << path;
            return false;
        }
        staged->push_back({path, entry.value(kProfileKey).toObject().toVariantMap()});
    }
    return !staged->isEmpty();
}

} // namespace

ShaderSetStore::Config makeConfig(std::function<QString()> profilesDir, std::function<QString()> setsDir,
                                  std::function<bool(const QString&, const QVariantMap&)> writeOverride,
                                  std::function<bool(const QString&)> fileSnapshot,
                                  std::function<QString()> mutationGuard)
{
    // The domain cannot function without these: a missing callable is a wiring
    // bug, not a runtime condition. Assert in debug; the lambdas below still
    // check, so a release build degrades to "nothing to snapshot / refuse the
    // write" instead of throwing std::bad_function_call.
    Q_ASSERT(profilesDir);
    Q_ASSERT(writeOverride);

    ShaderSetStore::Config config;
    config.setsDir = std::move(setsDir);
    config.fileSnapshot = std::move(fileSnapshot);
    config.mutationGuard = std::move(mutationGuard);

    // ── Snapshot: walk the profiles dir and keep the files whose `name`
    //    matches a known event path. Non-path names (user presets) are
    //    skipped so a set stays portable and self-contained. QDir::Name
    //    ordering keeps the output deterministic, which the store relies on
    //    when it compares this snapshot against a saved set to decide which
    //    one is active.
    config.snapshot = [profilesDir = std::move(profilesDir)]() -> QJsonObject {
        using namespace PhosphorAnimation;

        if (!profilesDir) {
            return QJsonObject{};
        }
        QJsonArray overrides;
        QDir dir(profilesDir());
        if (!dir.exists()) {
            return QJsonObject{};
        }
        const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
        const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());
        const auto files = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo& info : files) {
            QFile f(info.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) {
                qCWarning(lcConfig) << "motionset snapshot: cannot open" << info.absoluteFilePath();
                continue;
            }
            QJsonParseError err{};
            const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                qCWarning(lcConfig) << "motionset snapshot: failed to parse" << info.absoluteFilePath() << ":"
                                    << err.errorString();
                continue;
            }
            const QJsonObject obj = doc.object();
            const QString entryName = obj.value(kNameKey).toString();
            if (!knownPathSet.contains(entryName)) {
                continue;
            }
            QJsonObject profile = obj;
            profile.remove(kNameKey);
            QJsonObject entry;
            entry.insert(kPathKey, entryName);
            entry.insert(kProfileKey, profile);
            overrides.append(entry);
        }

        QJsonObject root;
        root.insert(kOverridesKey, overrides);
        return root;
    };

    config.validate = [](const QJsonObject& root) -> bool {
        QList<StagedEntry> staged;
        return stageEntries(root, &staged);
    };

    // ── Apply: validate everything up-front, then write each entry. The
    //    controller's setOverride callback snapshots the prior content
    //    before each write, so a mid-batch failure still leaves pre-edit
    //    content captured for every peer that already succeeded — Discard
    //    restores them atomically via the controller's revertPending walk.
    config.apply = [writeOverride = std::move(writeOverride)](const QJsonObject& root) -> bool {
        if (!writeOverride) {
            return false;
        }
        QList<StagedEntry> staged;
        if (!stageEntries(root, &staged)) {
            return false;
        }
        QStringList committedPaths;
        for (const StagedEntry& e : staged) {
            if (!writeOverride(e.path, e.profile)) {
                qCWarning(lcConfig) << "motionset apply: write failed for path" << e.path;
                if (!committedPaths.isEmpty()) {
                    qCWarning(lcConfig) << "motionset apply: partial apply committed" << committedPaths.size()
                                        << "paths before failure:" << committedPaths;
                }
                return false;
            }
            committedPaths.append(e.path);
        }
        return true;
    };

    return config;
}

} // namespace PlasmaZones::motionset
