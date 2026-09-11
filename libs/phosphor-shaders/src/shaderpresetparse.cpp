// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPresetParse.h>

#include <QJsonValue>
#include <QLoggingCategory>
#include <QStringList>

namespace PhosphorShaders {

QString resolveWithinPack(const QDir& packDir, const QString& declaredName, PhosphorFsLoader::AbsolutePathPolicy policy,
                          const QLoggingCategory& log)
{
    // An empty declared name is ABSENT, not an escape. `toString(default)` hands
    // back an explicit `""` rather than the default (an empty string IS a
    // string), so without this the guard refused it and warned "declared a path
    // outside its own directory: ''", which points the pack author at the wrong
    // problem.
    if (declaredName.isEmpty()) {
        return {};
    }
    const auto resolved = PhosphorFsLoader::resolveWithinDirectory(declaredName, packDir.absolutePath(), policy);
    if (!resolved) {
        qCWarning(log) << "Shader pack declared a path outside its own directory:" << declaredName << "in"
                       << packDir.absolutePath() << "— ignoring";
        return {};
    }
    return *resolved;
}

PackPresets parsePackPresets(const QDir& packDir, const QSet<QString>& imageParamIds, const QJsonObject& root,
                             const QLoggingCategory& log)
{
    PackPresets presets;

    const QJsonObject presetsObj = root.value(QLatin1String("presets")).toObject();
    for (auto it = presetsObj.begin(); it != presetsObj.end(); ++it) {
        const QJsonObject values = it.value().toObject();
        QVariantMap presetValues;
        QStringList refusedImageEntries;
        for (auto vit = values.begin(); vit != values.end(); ++vit) {
            if (imageParamIds.contains(vit.key())) {
                const QString declared = vit.value().toVariant().toString();
                if (declared.isEmpty()) {
                    // Empty = "no texture" for this slot; carry through.
                    presetValues[vit.key()] = QString();
                    continue;
                }
                // Reject, like every other pack-declared path: an absolute or
                // escaping preset texture is a mistake or an attack, never
                // legitimate. A refused value is dropped from the preset so it
                // falls back to the param's default rather than binding an
                // arbitrary file.
                const QString resolved =
                    resolveWithinPack(packDir, declared, PhosphorFsLoader::AbsolutePathPolicy::Reject, log);
                if (!resolved.isEmpty()) {
                    presetValues[vit.key()] = resolved;
                } else {
                    refusedImageEntries.append(vit.key());
                }
                continue;
            }
            presetValues[vit.key()] = vit.value().toVariant();
        }
        if (!refusedImageEntries.isEmpty()) {
            // Name the preset. Without this a preset whose image entries were
            // all refused vanishes from the pack with nothing in the log
            // pointing at which one, or why.
            qCWarning(log).noquote() << "Shader pack" << packDir.dirName() << "preset" << it.key()
                                     << "declares texture path(s) outside the pack; refused:"
                                     << refusedImageEntries.join(QLatin1String(", "));
        }
        if (!presetValues.isEmpty()) {
            presets[it.key()] = presetValues;
        }
    }

    return presets;
}

} // namespace PhosphorShaders
