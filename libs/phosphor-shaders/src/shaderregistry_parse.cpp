// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file shaderregistry_parse.cpp
 * @brief Shader-pack metadata parsing: metadata.json → ShaderInfo.
 *
 * Split out of shaderregistry.cpp by concern (the registry TU keeps the
 * loader wiring, lookup surface, and uniform translation; this TU owns the
 * parse). Everything here is deterministic, filesystem-in / value-out, and
 * shared verbatim between the live scan (parseShader's strategy callback)
 * and the offline entry point (ShaderRegistry::parsePackMetadata, used by
 * the validator and the shader-render preview tool).
 */

#include "shaderregistry_parse.h"
#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include "shaderutils.h"

#include <PhosphorFsLoader/DirectoryLoader.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>
#include <QUrl>
#include <QUuid>

namespace PhosphorShaders {

Q_DECLARE_LOGGING_CATEGORY(lcShaderRegistry)

// Namespace UUID for generating deterministic shader IDs (UUID v5).
// Function-local static rather than a namespace-scope QUuid: fromString is
// not constexpr, and a dynamic-initialised global would be subject to
// cross-TU static-init ordering.
static const QUuid& shaderNamespaceUuid()
{
    static const QUuid uuid = QUuid::fromString(QStringLiteral("{a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d}"));
    return uuid;
}

namespace ShaderRegistryParse {

/// `QDir::filePath` returns an ABSOLUTE argument unchanged and never normalises
/// `..`, so `"fragmentShader": "/etc/shadow"` or `"../../../x.frag"` in a
/// third-party pack would otherwise resolve outside the pack entirely — and
/// these paths name files that get compiled and run on the GPU, or pulled into
/// a texture the shader can sample. CLAUDE.md: "Sanitize file paths to prevent
/// directory traversal."
///
/// Subdirectories INSIDE the pack stay legal (`"shaders/effect.frag"`), because
/// containment is checked on the resolved canonical path rather than by
/// refusing separators. A name that does not exist yet resolves lexically, so a
/// pack referencing a file it does not ship is still rejected later by the
/// existence checks rather than here.
///
/// @p policy defaults to `Reject`, which is right for everything a PACK FILE
/// declares: a pack ships its own assets, so an absolute path can only be a
/// mistake or an escape. Only a value the USER supplied at runtime (a file
/// picker, D-Bus) may pass `Trust`.
QString resolveWithinPack(const QDir& dir, const QString& declaredName, PhosphorFsLoader::AbsolutePathPolicy policy)
{
    // An empty declared name is ABSENT, not an escape. `toString(default)` hands
    // back an explicit `""` rather than the default (an empty string IS a
    // string), so without this the guard refused it and warned "declared a path
    // outside its own directory: ''", which points the pack author at the wrong
    // problem. Both callers now schema-gate first (minLength 1), so this is
    // pure belt-and-braces for any future gate-free caller.
    if (declaredName.isEmpty()) {
        return {};
    }
    // Delegates to the shared guard rather than hand-rolling a fifth copy: a
    // lexical-only check (which this was) misses a symlink inside the pack
    // pointing out of it, and mixing canonical with lexical fails open.
    const auto resolved = PhosphorFsLoader::resolveWithinDirectory(declaredName, dir.absolutePath(), policy);
    if (!resolved) {
        qCWarning(lcShaderRegistry) << "Shader pack declared a path outside its own directory:" << declaredName << "in"
                                    << dir.absolutePath() << "— ignoring";
        return {};
    }
    return *resolved;
}

ShaderRegistry::ShaderInfo parseShaderMetadata(const QString& shaderDir, const QJsonObject& root)
{
    ShaderRegistry::ShaderInfo info;
    QDir dir(shaderDir);
    info.packDir = dir.absolutePath();

    // Default name from directory name; the metadata `id` field overrides
    // the UUID source if present, the `name` field overrides display
    // only.
    const QString shaderName = dir.dirName();
    const QString metadataId = root.value(QLatin1String("id")).toString(shaderName);
    info.id = QUuid::createUuidV5(shaderNamespaceUuid(), metadataId).toString();
    info.name = root.value(QLatin1String("name")).toString(shaderName);
    info.description = root.value(QLatin1String("description")).toString();
    info.author = root.value(QLatin1String("author")).toString();
    info.version = root.value(QLatin1String("version")).toString(QStringLiteral("1.0"));
    info.category = root.value(QLatin1String("category")).toString();

    // Fragment shader path (default: effect.frag).
    const QString fragShaderName = root.value(QLatin1String("fragmentShader")).toString(QStringLiteral("effect.frag"));
    info.sourcePath = resolveWithinPack(dir, fragShaderName);
    // Set the URL here in the SHARED parser (not only on the live parseShader
    // path), so parsePackMetadata's returned ShaderInfo is isValid() for a
    // well-formed pack. Without it the offline info was always !isValid()
    // (isValid requires a valid shaderUrl), which made the validator's
    // parse-success assertion tautological and would silently reject every
    // pack for any future caller that gated on isValid(). A missing frag is
    // still rejected by parseShader's own QFile::exists check downstream.
    if (!info.sourcePath.isEmpty()) {
        info.shaderUrl = QUrl::fromLocalFile(info.sourcePath);
    }
    // Vertex shader: explicit metadata declaration, per-shader zone.vert, or empty
    // (ZoneShaderItem falls back to the shared zone.vert from search paths at render time).
    const QString vertShaderName = root.value(QLatin1String("vertexShader")).toString();
    if (!vertShaderName.isEmpty()) {
        const QString resolved = resolveWithinPack(dir, vertShaderName);
        if (!resolved.isEmpty() && QFile::exists(resolved)) {
            info.vertexShaderPath = resolved;
        } else {
            qCWarning(lcShaderRegistry) << "Declared vertexShader" << vertShaderName << "not found in"
                                        << dir.absolutePath();
        }
    } else {
        // Through resolveWithinPack like every other compiled path: the
        // filename is fixed, but `zone.vert` shipped as a symlink pointing
        // out of the pack would otherwise be compiled and run on the GPU —
        // exactly the case the guard exists for.
        const QString localVert = resolveWithinPack(dir, QStringLiteral("zone.vert"));
        if (!localVert.isEmpty() && QFile::exists(localVert)) {
            info.vertexShaderPath = localVert;
        }
    }

    // Multi-pass: one or more buffer pass shaders (A->B->C->D).
    info.isMultipass = root.value(QLatin1String("multipass")).toBool(false);
    const QJsonArray bufferShadersArray = root.value(QLatin1String("bufferShaders")).toArray();
    if (bufferShadersArray.size() > kMaxBufferPasses) {
        qCWarning(lcShaderRegistry) << "Shader pack" << info.name << "declares" << bufferShadersArray.size()
                                    << "buffer shaders; only the first 4 are used";
    }
    bool bufferShadersDeclared = false;
    if (!bufferShadersArray.isEmpty()) {
        bufferShadersDeclared = true;
        for (int i = 0; i < qMin(bufferShadersArray.size(), kMaxBufferPasses); ++i) {
            const QString name = bufferShadersArray.at(i).toString();
            const QString resolved = resolveWithinPack(dir, name);
            if (resolved.isEmpty()) {
                // The whole list goes, and multipass with it. These entries are
                // POSITIONALLY aligned with the per-buffer wrap/filter overrides
                // read below, so silently compacting one out would shift every
                // later override onto the wrong buffer and corrupt the A→B→C
                // chain rather than refuse it. Same contract the animation
                // registry's equivalent block states.
                // Names the actual cause: resolveWithinPack answers empty for an
                // EMPTY entry too, before it reaches the traversal guard, and
                // reporting that as a path escape sent readers looking for a
                // `../` that was never there.
                qCWarning(lcShaderRegistry) << "Shader pack" << info.name
                                            << (name.isEmpty() ? "declared an empty buffer shader entry —"
                                                               : "declared a buffer shader outside its own directory —")
                                            << "dropping the whole bufferShaders list and disabling multipass";
                info.bufferShaderPaths.clear();
                info.isMultipass = false;
                break;
            }
            info.bufferShaderPaths.append(resolved);
        }
    }
    // Only fall back to the implicit `buffer.frag` when the pack declared NO
    // bufferShaders at all. A pack whose declared list was refused above must
    // not silently run a different shader than it asked for.
    if (info.bufferShaderPaths.isEmpty() && !bufferShadersDeclared) {
        const QString bufferShaderName =
            root.value(QLatin1String("bufferShader")).toString(QStringLiteral("buffer.frag"));
        const QString resolvedBuffer = resolveWithinPack(dir, bufferShaderName);
        // Existence-checked, unlike resolveWithinPack itself (its contract
        // deliberately skips the check): this IMPLICIT fallback used to
        // publish `<pack>/buffer.frag` for every single-pass pack (22 of the
        // bundled 27 ship no such file), and the runtime's only multipass
        // gate is a non-empty buffer path — so each of those packs paid 3
        // failed disk loads plus two warnings per render-node attach.
        if (!resolvedBuffer.isEmpty() && QFile::exists(resolvedBuffer)) {
            info.bufferShaderPaths.append(resolvedBuffer);
        }
    }
    if (info.isMultipass) {
        bool allExist = true;
        for (const QString& path : info.bufferShaderPaths) {
            if (!QFile::exists(path)) {
                qCWarning(lcShaderRegistry) << "Multipass shader missing buffer shader:" << path;
                allExist = false;
                break;
            }
        }
        if (!allExist) {
            info.bufferShaderPaths.clear();
        }
    }

    info.useWallpaper = root.value(QLatin1String("wallpaper")).toBool(false);
    info.bufferFeedback = root.value(QLatin1String("bufferFeedback")).toBool(false);
    const qreal scale = root.value(QLatin1String("bufferScale")).toDouble(1.0);
    info.bufferScale = qBound(kMinBufferScale, scale, kMaxBufferScale);
    info.bufferWrap = normalizeWrapMode(root.value(QLatin1String("bufferWrap")).toString(QStringLiteral("clamp")));
    info.useDepthBuffer = root.value(QLatin1String("depthBuffer")).toBool(false);
    // Default TRUE: a buffer may store HDR radiance, signed data, or a
    // feedback accumulator, none of which survive RGBA8. A pack whose buffers
    // hold plain clamped [0,1] colour declares "halfFloatBuffers": false to
    // halve its buffer bandwidth.
    info.halfFloatBuffers = root.value(QLatin1String("halfFloatBuffers")).toBool(true);

    const QJsonArray bufferWrapsArray = root.value(QLatin1String("bufferWraps")).toArray();
    if (!bufferWrapsArray.isEmpty()) {
        for (const QJsonValue& v : bufferWrapsArray) {
            info.bufferWraps.append(normalizeWrapMode(v.toString()));
        }
        const int needed = info.bufferShaderPaths.size();
        while (info.bufferWraps.size() < needed) {
            info.bufferWraps.append(info.bufferWrap);
        }
        while (info.bufferWraps.size() > needed) {
            info.bufferWraps.removeLast();
        }
    }

    info.bufferFilter =
        normalizeFilterMode(root.value(QLatin1String("bufferFilter")).toString(QStringLiteral("linear")));

    const QJsonArray bufferFiltersArray = root.value(QLatin1String("bufferFilters")).toArray();
    if (!bufferFiltersArray.isEmpty()) {
        for (const QJsonValue& v : bufferFiltersArray) {
            info.bufferFilters.append(normalizeFilterMode(v.toString()));
        }
        const int needed = info.bufferShaderPaths.size();
        while (info.bufferFilters.size() < needed) {
            info.bufferFilters.append(info.bufferFilter);
        }
        while (info.bufferFilters.size() > needed) {
            info.bufferFilters.removeLast();
        }
    }

    // Single-pass coherence, ported from SurfaceShaderRegistry::parseEffect:
    // a pack that set buffer-only fields WITHOUT multipass (or whose
    // multipass fail-closed above) must not carry orphan buffer state — it
    // would flow through shaderInfoToVariantMap onto D-Bus/QML and into the
    // content signature for a pipeline that never runs.
    if (!info.isMultipass) {
        info.bufferShaderPaths.clear();
        info.bufferWraps.clear();
        info.bufferFilters.clear();
        info.bufferFeedback = false;
        info.useDepthBuffer = false;
        // Also reset the scalar buffer fields, matching the surface parser's
        // block this is ported from: a single-pass pack that set "bufferScale"
        // / "bufferWrap" / "bufferFilter" would otherwise publish those through
        // shaderInfoToVariantMap onto D-Bus/QML (and into the content
        // signature) for a pipeline that never runs.
        info.bufferScale = 1.0;
        info.bufferWrap = QStringLiteral("clamp");
        info.bufferFilter = QStringLiteral("linear");
        info.halfFloatBuffers = true;
    }

    // Parameters
    const QJsonArray paramsArray = root.value(QLatin1String("parameters")).toArray();
    QSet<QString> seenParamIds;
    QStringList duplicateParamIds;
    for (const QJsonValue& paramValue : paramsArray) {
        QJsonObject paramObj = paramValue.toObject();
        ShaderRegistry::ParameterInfo param;
        param.id = paramObj.value(QLatin1String("id")).toString();
        // First declaration wins: a repeated id would claim a second lane AND
        // emit a second `#define p_<id>` — glslang rejects the macro
        // redefinition and the whole pack fails to compile.
        if (!param.id.isEmpty() && seenParamIds.contains(param.id)) {
            duplicateParamIds.append(param.id);
            continue;
        }
        // Only real ids go into the seen-set, so it never holds the empty
        // string (an empty id is a different fault, handled by isValidParamId
        // downstream) and the set's contents mean what its name says.
        if (!param.id.isEmpty()) {
            seenParamIds.insert(param.id);
        }
        param.name = paramObj.value(QLatin1String("name")).toString(param.id);
        param.group = paramObj.value(QLatin1String("group")).toString();
        param.type = paramObj.value(QLatin1String("type")).toString(QStringLiteral("float"));
        param.slot = paramObj.value(QLatin1String("slot")).toInt(-1);
        param.defaultValue = paramObj.value(QLatin1String("default")).toVariant();
        param.minValue = paramObj.value(QLatin1String("min")).toVariant();
        param.maxValue = paramObj.value(QLatin1String("max")).toVariant();
        param.useZoneColor = paramObj.value(QLatin1String("use_zone_color")).toBool(false);
        param.wrap = paramObj.value(QLatin1String("wrap")).toString();

        if (!param.id.isEmpty()) {
            info.parameters.append(param);
        }
    }

    // Warn in causal order: duplicate ids are a parse-time fault (detected in
    // the loop just above), so they are reported before the slot-budget
    // overflow that the assignment pass below can only discover afterwards.
    if (!duplicateParamIds.isEmpty()) {
        qCWarning(lcShaderRegistry).noquote()
            << "Shader pack" << dir.dirName() << "declares" << duplicateParamIds.size()
            << "duplicate parameter id(s); first declaration wins:" << duplicateParamIds.join(QLatin1String(", "));
    }

    // Automatic slot assignment (T1.1): a parameter that omits `slot` is packed
    // into the next free lane of its pool in declaration order — float/int/bool
    // → 0..31, color → 0..15, image → 0..3 — so authors no longer hand-number
    // slots (the `p_<id>` preamble and the upload both derive from this same
    // slot). Explicit slots are reserved first, so a pack may mix the two; the
    // migrated zone packs drop slots entirely, becoming pure declaration order.
    // A collision (two explicit params on one lane) is left as-is for the
    // validator (T1.2) to flag, not silently reshuffled.
    {
        // An id that isn't a valid GLSL identifier can't get a p_<id> define
        // (buildParamPreamble skips it), so it must claim no lane either: force its
        // slot to -1 (overriding any explicit metadata slot) so it reserves
        // nothing, auto-fills to nothing, AND uploads nothing — uniformName()
        // returns "" for slot < 0, so translateParamsToUniforms drops it. Without
        // this, an invalid-id param with an explicit slot would still upload to
        // that lane while a valid auto-slot param (the lane was never reserved)
        // collides onto it.
        for (ShaderRegistry::ParameterInfo& p : info.parameters) {
            if (!isValidParamId(p.id)) {
                p.slot = -1;
            }
        }
        auto poolOf = [](const QString& type) -> int { // 0 = scalar, 1 = color, 2 = image
            if (type == QLatin1String("color")) {
                return 1;
            }
            if (type == QLatin1String("image")) {
                return 2;
            }
            return 0;
        };
        QSet<int> usedScalar, usedColor, usedImage;
        for (const ShaderRegistry::ParameterInfo& p : std::as_const(info.parameters)) {
            // Reserve only slots that buildParamPreamble also honors — an invalid
            // id is skipped on both sides, so the two reservation passes stay
            // byte-identical (not just the auto-fill passes).
            if (p.slot < 0 || !isValidParamId(p.id)) {
                continue;
            }
            const int pool = poolOf(p.type);
            (pool == 1 ? usedColor : pool == 2 ? usedImage : usedScalar).insert(p.slot);
        }
        int nextScalar = 0, nextColor = 0, nextImage = 0;
        for (ShaderRegistry::ParameterInfo& p : info.parameters) {
            if (p.slot >= 0) {
                continue;
            }
            // Skip ids buildParamPreamble would reject (invalid GLSL identifier),
            // so this upload-lane numbering stays byte-identical to the p_<id>
            // define numbering — a rejected param gets no define and no lane.
            if (!isValidParamId(p.id)) {
                continue;
            }
            const int pool = poolOf(p.type);
            QSet<int>& used = (pool == 1 ? usedColor : pool == 2 ? usedImage : usedScalar);
            int& next = (pool == 1 ? nextColor : pool == 2 ? nextImage : nextScalar);
            while (used.contains(next)) {
                ++next;
            }
            p.slot = next;
            used.insert(next);
            ++next;
        }

        // One summary warning per failure class (mirrors the surface
        // registry's overflow reporting): a slot past its pool budget —
        // explicit or overflowed auto-fill — is skipped identically by
        // buildParamPreamble ("slot out of range" comment) and by
        // uniformName() (empty key, no upload), so the shader compiles
        // against an undefined `p_<id>` and the author gets no signal
        // without this.
        QStringList overBudget;
        for (const ShaderRegistry::ParameterInfo& p : std::as_const(info.parameters)) {
            if (p.slot < 0) {
                continue;
            }
            const int pool = poolOf(p.type);
            const int budget = pool == 1 ? CustomColors::kColorCount
                : pool == 2              ? kMaxImageSlots
                                         : CustomParams::kFlatSlotCount;
            if (p.slot >= budget) {
                overBudget.append(p.id);
            }
        }
        if (!overBudget.isEmpty()) {
            qCWarning(lcShaderRegistry).noquote()
                << "Shader pack" << dir.dirName() << "has" << overBudget.size()
                << "parameter(s) past their pool slot budget (scalar" << CustomParams::kFlatSlotCount << "/ color"
                << CustomColors::kColorCount << "/ image" << kMaxImageSlots
                << "); they will not bind:" << overBudget.join(QLatin1String(", "));
        }
    }

    // Presets
    //
    // Image-typed preset values are pack-declared paths, exactly like an image
    // param's `default`, and must be containment-checked at parse time with the
    // same Reject policy. They cannot be trusted at translate time: a preset
    // reaches `translateParamsToUniforms` through `storedParams` (the user picked
    // the preset), so the provenance heuristic there (`storedParams.contains(id)`
    // → Trust) would wave a pack-manufactured `"tex": "/home/user/.ssh/id_rsa"`
    // straight through — the very escape the `default` path already closes. Gate
    // it here, where the value's true (pack) provenance is known.
    QSet<QString> imageParamIds;
    for (const ShaderRegistry::ParameterInfo& p : std::as_const(info.parameters)) {
        if (p.type == QLatin1String("image")) {
            imageParamIds.insert(p.id);
        }
    }
    const QJsonObject presetsObj = root.value(QLatin1String("presets")).toObject();
    for (auto it = presetsObj.begin(); it != presetsObj.end(); ++it) {
        const QJsonObject values = it.value().toObject();
        QVariantMap presetValues;
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
                const QString resolved = resolveWithinPack(dir, declared, PhosphorFsLoader::AbsolutePathPolicy::Reject);
                if (!resolved.isEmpty()) {
                    presetValues[vit.key()] = resolved;
                }
                continue;
            }
            presetValues[vit.key()] = vit.value().toVariant();
        }
        if (!presetValues.isEmpty()) {
            info.presets[it.key()] = presetValues;
        }
    }

    return info;
}

const PhosphorFsLoader::SchemaValidator& shaderMetadataValidator()
{
    static const PhosphorFsLoader::SchemaValidator validator = PhosphorFsLoader::SchemaValidator::fromResource(
        QStringLiteral(":/phosphorshaders/schemas/shader-metadata.schema.json"), lcShaderRegistry());
    return validator;
}

} // namespace ShaderRegistryParse

ShaderRegistry::ShaderInfo ShaderRegistry::parsePackMetadata(const QString& packDir, QString* error,
                                                             bool validateSchema)
{
    const QString metaPath = QDir(packDir).filePath(QStringLiteral("metadata.json"));
    QFile file(metaPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("cannot open %1").arg(metaPath);
        }
        return {};
    }
    // Same per-file size cap the live scan inherits from DirectoryLoader, so
    // the offline path cannot be fed an unbounded metadata file.
    if (file.size() > PhosphorFsLoader::DirectoryLoader::kMaxFileBytes) {
        if (error) {
            *error = QStringLiteral("metadata file too large (%1 bytes, limit %2)")
                         .arg(file.size())
                         .arg(PhosphorFsLoader::DirectoryLoader::kMaxFileBytes);
        }
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("invalid JSON: %1").arg(parseError.errorString());
        }
        return {};
    }
    if (!doc.isObject()) {
        if (error) {
            *error = QStringLiteral("metadata root is not a JSON object");
        }
        return {};
    }
    // Same structural schema gate the live scan applies (parseShader), so the
    // offline validator refuses exactly the packs the daemon refuses. Skipped
    // when validateSchema is false: the shader-render preview tool wants the
    // tolerant parse below (default names, dropped out-of-range slots) rather
    // than the daemon's gatekeeping. Path-traversal confinement below still runs.
    const QJsonObject rootObj = doc.object();
    if (validateSchema) {
        if (const auto errors = ShaderRegistryParse::shaderMetadataValidator().validate(rootObj)) {
            if (error) {
                QStringList lines;
                lines.reserve(errors->size());
                for (const auto& e : *errors) {
                    lines.append((e.path.isEmpty() ? QStringLiteral("(root)") : e.path) + QStringLiteral(": ")
                                 + e.message);
                }
                *error = QStringLiteral("metadata fails schema validation: %1").arg(lines.join(QStringLiteral("; ")));
            }
            return {};
        }
    }
    // parseShaderMetadata sets sourcePath / vertexShaderPath /
    // bufferShaderPaths from packDir and applies the same auto-slot
    // assignment the live scan does.
    return ShaderRegistryParse::parseShaderMetadata(packDir, rootObj);
}

} // namespace PhosphorShaders
