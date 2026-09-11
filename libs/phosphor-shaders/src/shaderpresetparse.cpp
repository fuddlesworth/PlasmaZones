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

    const QJsonValue presetsValue = root.value(QLatin1String("presets"));
    // A `presets` of the wrong type used to vanish: toObject() answers {} for an
    // array, a string or a number, so the pack silently shipped no presets, with
    // nothing in the log and nothing for the offline validator to lint (it receives
    // an already-empty map). Two of the four family schemas are CI-only, so the
    // schema cannot be relied on to catch it for a user-installed pack.
    if (!presetsValue.isUndefined() && !presetsValue.isNull() && !presetsValue.isObject()) {
        qCWarning(log).noquote() << "Shader pack" << packDir.dirName()
                                 << "declares `presets` as something other than an object; ignoring it";
        return presets;
    }

    const QJsonObject presetsObj = presetsValue.toObject();
    for (auto it = presetsObj.begin(); it != presetsObj.end(); ++it) {
        // Same reasoning one level down: a non-object preset BODY yielded an empty
        // value map and the preset was then dropped by the isEmpty() test below,
        // with no diagnostic — unlike the refused-image case, which deliberately
        // names the preset so it cannot vanish unexplained.
        if (!it.value().isObject()) {
            qCWarning(log).noquote() << "Shader pack" << packDir.dirName() << "preset" << it.key()
                                     << "is not an object; ignoring it";
            continue;
        }
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
            // A JSON null is not "leave this at its default": every numeric
            // consumer reads an invalid variant as 0, so carrying it through
            // would PIN the parameter to zero and override the pack's declared
            // default. Dropping the key is what actually means "say nothing
            // about this one". Contrast the empty-string image value above,
            // which really does mean "no texture".
            if (vit.value().isNull()) {
                qCWarning(log).noquote() << "Shader pack" << packDir.dirName() << "preset" << it.key() << "sets"
                                         << vit.key() << "to null; ignoring that entry";
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
        // An empty preset is kept when the AUTHOR declared it empty, and dropped
        // when it is empty only because every value was refused. The two look the
        // same here and mean opposite things:
        //
        //   • `"Default": {}` legitimately means "this preset is the pack's
        //     declared defaults". It resolves correctly, and dropping it also hid
        //     it from the offline validator — which lints the PARSED struct, so the
        //     effect headers that point an author at the validator for a typo were
        //     promising coverage it could not give.
        //   • every value refused (an escaping texture path) means the preset
        //     cannot do what it says. Keeping it would offer the user a preset that
        //     silently does nothing; the refusal is already named in the log above.
        if (!presetValues.isEmpty() || (values.isEmpty() && refusedImageEntries.isEmpty())) {
            presets[it.key()] = presetValues;
        }
    }

    return presets;
}

} // namespace PhosphorShaders
