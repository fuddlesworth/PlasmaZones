// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// DecorationPageController decoration-set CRUD — the Motion Sets twin for
// surface-pack decoration. A set is a named snapshot of the decoration
// profile tree (baseline + per-surface direct overrides), persisted as one
// JSON file under ~/.local/share/plasmazones/decorationsets. Unlike motion
// sets (which snapshot per-path override FILES), the decoration tree is
// config-backed, so apply mutates ONE tree and persists it through
// ISettings::setDecorationProfileTree — dirty / apply / discard ride the
// normal settings staging flow with no extra snapshot plumbing.

#include "decorationpagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/isettings.h"
#include "../core/logging.h"
#include "../phosphor_i18n.h"
#include "animationfileutils.h"

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorSurface/DecorationSupportedPaths.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>

namespace PlasmaZones {

namespace {

constexpr QLatin1String kNameKey{"name"};
constexpr QLatin1String kDescriptionKey{"description"};
constexpr QLatin1String kVersionKey{"version"};
constexpr QLatin1String kBaselineKey{"baseline"};
constexpr QLatin1String kOverridesKey{"overrides"};
constexpr QLatin1String kPathKey{"path"};
constexpr QLatin1String kProfileKey{"profile"};

} // namespace

QString DecorationPageController::decorationSetsDirectoryPath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userDecorationSetsSubdir());
}

QString DecorationPageController::decorationSetFilePath(const QString& setName) const
{
    return animfileutil::jsonFilePath(decorationSetsDirectoryPath(), animfileutil::slugify(setName));
}

QVariantList DecorationPageController::availableDecorationSets() const
{
    QVariantList result;
    QDir dir(decorationSetsDirectoryPath());
    if (!dir.exists()) {
        return result;
    }
    const auto files = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        QFile f(info.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            qCWarning(lcConfig) << "availableDecorationSets: cannot open" << info.absoluteFilePath();
            continue;
        }
        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcConfig) << "availableDecorationSets: failed to parse" << info.absoluteFilePath() << ":"
                                << err.errorString();
            continue;
        }
        const QJsonObject obj = doc.object();
        QVariantMap row;
        row.insert(QStringLiteral("name"), obj.value(kNameKey).toString());
        row.insert(QStringLiteral("description"), obj.value(kDescriptionKey).toString());
        row.insert(QStringLiteral("slug"), info.completeBaseName());
        // Baseline counts as one covered surface in the row summary.
        const int baselineCount = obj.contains(kBaselineKey) ? 1 : 0;
        row.insert(QStringLiteral("overrideCount"), obj.value(kOverridesKey).toArray().size() + baselineCount);
        result.append(row);
    }
    return result;
}

bool DecorationPageController::applyDecorationSet(const QString& name)
{
    using PhosphorSurfaceShaders::DecorationProfile;
    using PhosphorSurfaceShaders::DecorationProfileTree;
    if (!m_settings || name.isEmpty()) {
        return false;
    }
    const QString filePath = decorationSetFilePath(name);
    if (filePath.isEmpty()) {
        return false;
    }
    QFile f(filePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcConfig) << "applyDecorationSet: failed to parse" << filePath << ":" << err.errorString();
        return false;
    }
    const QJsonObject root = doc.object();

    // Validate every entry up-front — reject the whole set on any malformed
    // entry rather than committing partial state (same staging discipline as
    // MotionSetStore::applyMotionSet). Path membership in the supported
    // surface taxonomy is the same rule the controller setters enforce.
    struct StagedEntry
    {
        QString path;
        PhosphorSurfaceShaders::DecorationProfile profile;
    };
    QList<StagedEntry> staged;
    const QJsonArray overrides = root.value(kOverridesKey).toArray();
    staged.reserve(overrides.size());
    for (const QJsonValue& v : overrides) {
        if (!v.isObject()) {
            qCWarning(lcConfig) << "applyDecorationSet: non-object entry in" << filePath;
            return false;
        }
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(kPathKey).toString();
        if (path.isEmpty() || !PhosphorSurfaceShaders::decorationSurfaceSupported(path)) {
            qCWarning(lcConfig) << "applyDecorationSet: rejecting unknown surface path" << path << "in" << filePath;
            return false;
        }
        if (!entry.value(kProfileKey).isObject()) {
            qCWarning(lcConfig) << "applyDecorationSet: missing profile object for" << path << "in" << filePath;
            return false;
        }
        staged.push_back({path, DecorationProfile::fromJson(entry.value(kProfileKey).toObject())});
    }
    const bool hasBaseline = root.value(kBaselineKey).isObject();
    if (staged.isEmpty() && !hasBaseline) {
        return false;
    }

    // Merge into ONE tree and persist once. Surfaces the set does not
    // cover keep their current overrides (motion-set semantics).
    DecorationProfileTree tree = m_settings->decorationProfileTree();
    if (hasBaseline) {
        tree.setBaseline(DecorationProfile::fromJson(root.value(kBaselineKey).toObject()));
    }
    for (const StagedEntry& e : staged) {
        tree.setOverride(e.path, e.profile);
    }
    m_settings->setDecorationProfileTree(tree);
    return true;
}

bool DecorationPageController::saveCurrentAsDecorationSet(const QString& name, const QString& description)
{
    using PhosphorSurfaceShaders::DecorationProfile;
    using PhosphorSurfaceShaders::DecorationProfileTree;
    if (!m_settings || name.isEmpty()) {
        return false;
    }
    const QString filePath = decorationSetFilePath(name);
    if (filePath.isEmpty()) {
        return false;
    }
    if (!QDir().mkpath(decorationSetsDirectoryPath())) {
        return false;
    }
    const DecorationProfileTree tree = m_settings->decorationProfileTree();

    QJsonObject rootObj;
    rootObj.insert(kNameKey, name);
    if (!description.isEmpty()) {
        rootObj.insert(kDescriptionKey, description);
    }
    rootObj.insert(kVersionKey, 1);
    // The baseline is part of the look — capture it when it carries any
    // engaged field (an empty profile serialises to an empty object).
    const QJsonObject baselineJson = tree.baseline().toJson();
    if (!baselineJson.isEmpty()) {
        rootObj.insert(kBaselineKey, baselineJson);
    }
    QJsonArray overrides;
    QStringList paths = tree.overriddenPaths();
    paths.sort(); // deterministic file content across saves
    for (const QString& p : paths) {
        QJsonObject entry;
        entry.insert(kPathKey, p);
        entry.insert(kProfileKey, tree.directOverride(p).toJson());
        overrides.append(entry);
    }
    rootObj.insert(kOverridesKey, overrides);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return false;
    }
    Q_EMIT decorationSetsChanged();
    return true;
}

bool DecorationPageController::removeDecorationSet(const QString& name)
{
    if (name.isEmpty()) {
        return false;
    }
    const QString filePath = decorationSetFilePath(name);
    if (filePath.isEmpty()) {
        return false;
    }
    QFile file(filePath);
    if (!file.exists() || !file.remove()) {
        return false;
    }
    Q_EMIT decorationSetsChanged();
    return true;
}

} // namespace PlasmaZones
