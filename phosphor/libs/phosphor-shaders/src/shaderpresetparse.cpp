// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPresetParse.h>

#include <PhosphorShaders/ShaderPackPaths.h>
// MaxParams, so the per-preset value cap is the ONE number the read side applies
// whether the map came from a pack's metadata or from a user preset file.
#include <PhosphorShaders/ShaderPreset.h>

#include <QJsonValue>
#include <QLoggingCategory>
#include <QStringList>

namespace PhosphorShaders {

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

    // FAIL CLOSED when there are image-typed parameters but no pack directory to
    // resolve them against.
    //
    // `imageParamIds` is NOT statically empty for the three non-overlay families:
    // each derives it from its declared parameter types, and `type` is read raw
    // from a hand-editable metadata.json with no enum validation. So a
    // user-installed animation or surface pack writing `"type": "image"` flips
    // this branch on — and those two families call this from `fromJson`, where
    // `sourceDir` is still empty because the registry stamps it afterwards.
    //
    // An empty QDir is NOT caught downstream: `QDir(QString())` behaves as
    // `QDir(".")`, so `absolutePath()` answers the process WORKING DIRECTORY,
    // which is non-empty — `resolveWithinDirectory`'s own empty-directory
    // fail-closed therefore never fires, and a relative preset texture path gets
    // confined to the CWD subtree and ACCEPTED. With a compositor CWD of "/" that
    // is most of the filesystem.
    //
    // Refusing the image values here keeps the guarantee a property of the code
    // rather than of the data: the rest of each preset still loads, and those
    // parameters fall back to their declared defaults, which is exactly what a
    // refused path already does below.
    const bool packDirUnusable = packDir.path().isEmpty() || packDir.path() == QLatin1String(".");
    const bool refuseAllImages = packDirUnusable && !imageParamIds.isEmpty();
    if (refuseAllImages) {
        qCWarning(log).noquote() << "Shader pack declares image-typed parameters but was parsed with no pack "
                                    "directory; refusing every image-typed preset value rather than resolving it "
                                    "against the process working directory";
    }

    const QJsonObject presetsObj = presetsValue.toObject();
    // COUNT-bounded here, not only in the schemas. The schemas cap `presets` and each
    // preset's value map, but only the animation and wallpaper registries validate
    // against a schema at load — the surface and pointer ones do not, so for those two
    // families a user-installed pack's unbounded `presets` object was parsed whole and
    // held for the session. Every other preset path in this feature bounds at its own
    // boundary rather than trusting a gate upstream, and this is that boundary.
    //
    // Truncating rather than refusing, like the directory loader's entry cap: a pack
    // that merely declares too many presets still offers the ones that fit.
    static constexpr qsizetype kMaxPresetsPerPack = 64;
    int kept = 0;
    for (auto it = presetsObj.begin(); it != presetsObj.end(); ++it) {
        if (kept >= kMaxPresetsPerPack) {
            qCWarning(log).noquote() << "Shader pack" << packDir.dirName() << "declares more than" << kMaxPresetsPerPack
                                     << "usable presets; ignoring the rest from" << it.key();
            break;
        }
        // Same reasoning one level down: a non-object preset BODY yielded an empty
        // value map and the preset was then dropped by the isEmpty() test below,
        // with no diagnostic — unlike the refused-image case, which deliberately
        // names the preset so it cannot vanish unexplained.
        // A pack-declared KEY is deliberately NOT screened here, and the reason is
        // worth stating because the obvious change is wrong.
        //
        // Dropping an unusable key at parse time looks like the safe move, but it
        // makes the OFFLINE VALIDATOR blind: the validator lints the PARSED preset
        // map, so a preset this function silently discarded produces no error in
        // the report and the pack ships green with a log line no author reads. A
        // runtime refusal and an authoring-time diagnostic are not
        // interchangeable — the diagnostic is the one that reaches the person who
        // can fix it.
        //
        // Nothing forces a screen here either: `applyPackBucket` takes the key as
        // the preset's id and name with `sourcePath` empty, and a pack preset never
        // becomes a filename — duplicating one into a user preset mints a fresh
        // UUID. So the enforcement lives in `reportPresetProblems`, which states
        // the rules without claiming this function refuses them.
        if (!it.value().isObject()) {
            qCWarning(log).noquote() << "Shader pack" << packDir.dirName() << "preset" << it.key()
                                     << "is not an object; ignoring it";
            continue;
        }
        // COUNTED after the shape check above, not before it: counting on entry spent the
        // budget on presets that were then discarded, so a pack with 64 malformed entries
        // followed by one good one lost the good one and the warning miscounted.
        ++kept;
        const QJsonObject values = it.value().toObject();
        QVariantMap presetValues;
        QStringList refusedImageEntries;
        for (auto vit = values.begin(); vit != values.end(); ++vit) {
            // The per-preset cap, the same number ShaderPreset::fromJson applies to a user
            // preset file. Deliberately ABOVE the schemas' own `maxProperties: 48` for a
            // preset body, for the reason ShaderPreset::MaxParams gives: 48 is the
            // declared-parameter ceiling, so a key past it names no declared parameter and
            // is inert at resolve, and refusing the whole preset over one would discard
            // the usable keys beside it.
            if (presetValues.size() >= PhosphorShaders::ShaderPreset::MaxParams) {
                qCWarning(log).noquote() << "Shader pack" << packDir.dirName() << "preset" << it.key()
                                         << "declares more than" << PhosphorShaders::ShaderPreset::MaxParams
                                         << "values; ignoring the rest from" << vit.key();
                break;
            }
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
                const QString resolved = refuseAllImages
                    ? QString()
                    : resolveWithinPack(packDir, declared, PhosphorFsLoader::AbsolutePathPolicy::Reject, log);
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
        //   • every value REFUSED (an escaping texture path) means the preset cannot do
        //     what it says. Keeping it would offer the user a preset that silently does
        //     nothing; the refusal is already named in the log above.
        //
        // A JSON `null` is NOT a refusal, and that distinction is what the test below
        // turns on. The schemas say a null "means the preset says nothing about that
        // parameter, so it keeps the pack's default" — which is exactly what omitting the
        // key means, so a preset whose every value is null is the author-declared `{}`
        // written the long way and is KEPT. Dropping it made the loader contradict the
        // schema prose on the one case that prose describes.
        //
        // So the test is on whether anything was REFUSED, not on whether the declared map
        // was empty. (An earlier pass removed this conjunct as dead, which was true while
        // a refusal was the only way to empty a non-empty map; the null drop is the second
        // way, and it must not be treated like the first.)
        if (!presetValues.isEmpty() || refusedImageEntries.isEmpty()) {
            presets[it.key()] = presetValues;
        }
    }

    return presets;
}

} // namespace PhosphorShaders
